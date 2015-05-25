/*
 * $Id: dncp_proto.c $
 *
 * Author: Markus Stenberg <mstenber@cisco.com>
 *
 * Copyright (c) 2013 cisco Systems, Inc.
 *
 * Created:       Tue Nov 26 08:34:59 2013 mstenber
 * Last modified: Mon May 25 14:13:03 2015 mstenber
 * Edit time:     908 min
 *
 */

#include "dncp_i.h"

/*
 * This module contains the logic to handle receiving and sending of
 * traffic from single- or multicast sources. The actual low-level IO
 * is performed in <profile>_io.
 */

/***************************************************** Low-level TLV pushing */

static bool _push_node_state_tlv(struct tlv_buf *tb, dncp_node n,
                                 bool incl_data)
{
  hnetd_time_t now = dncp_time(n->dncp);
  dncp_t_node_state s;
  int l = incl_data && n->tlv_container ? tlv_len(n->tlv_container) : 0;
  int nilen = DNCP_NI_LEN(n->dncp);
  int hlen = DNCP_HASH_LEN(n->dncp);
  int tlen = nilen + sizeof(*s) + hlen + l;
  struct tlv_attr *a = tlv_new(tb, DNCP_T_NODE_STATE, tlen);

  if (!a)
    return false;

  void *p = tlv_data(a);
  memcpy(p, &n->node_identifier, nilen);
  p += nilen;

  s = p;
  s->update_number = cpu_to_be32(n->update_number);
  s->ms_since_origination = cpu_to_be32(now - n->origination_time);
  p += sizeof(*s);

  memcpy(p, &n->node_data_hash, hlen);
  p += hlen;

  if (l)
    memcpy(p, tlv_data(n->tlv_container), l);

  return true;
}

static bool _push_network_state_tlv(struct tlv_buf *tb, dncp o)
{
  struct tlv_attr *a = tlv_new(tb, DNCP_T_NET_STATE, DNCP_HASH_LEN(o));

  if (!a)
    return false;
  dncp_calculate_network_hash(o);
  memcpy(tlv_data(a), &o->network_hash, DNCP_HASH_LEN(o));
  return true;
}

static bool _push_link_id_tlv(struct tlv_buf *tb, dncp_ep_i l)
{
  dncp_t_link_id lid;
  int tl = DNCP_NI_LEN(l->dncp) + sizeof(*lid);
  struct tlv_attr *a = tlv_new(tb, DNCP_T_ENDPOINT_ID, tl);

  if (!a)
    return false;
  memcpy(tlv_data(a), &l->dncp->own_node->node_identifier, DNCP_NI_LEN(l->dncp));
  lid = tlv_data(a) + DNCP_NI_LEN(l->dncp);
  lid->link_id = l->iid;
  return true;
}

static bool _push_keepalive_interval_tlv(struct tlv_buf *tb,
                                         uint32_t link_id,
                                         uint32_t value)
{
  dncp_t_keepalive_interval ki;
  struct tlv_attr *a = tlv_new(tb, DNCP_T_KEEPALIVE_INTERVAL, sizeof(*ki));

  if (!a)
    return false;
  ki = tlv_data(a);
  ki->link_id = link_id;
  ki->interval_in_ms = cpu_to_be32(value);
  return true;
}

/****************************************** Actual payload sending utilities */

void dncp_ep_i_send_network_state(dncp_ep_i l,
                                  struct sockaddr_in6 *src,
                                  struct sockaddr_in6 *dst,
                                  size_t maximum_size)
{
  struct tlv_buf tb;
  dncp o = l->dncp;
  dncp_node n;

  memset(&tb, 0, sizeof(tb));
  tlv_buf_init(&tb, 0); /* not passed anywhere */
  if (!_push_link_id_tlv(&tb, l))
    goto done;
  if (!_push_network_state_tlv(&tb, o))
    goto done;

  /* We multicast only 'stable' state. Unicast, we give everything we have. */
  if (!o->graph_dirty || !maximum_size)
    {
      int nn = 0;

      if (maximum_size)
        dncp_for_each_node(o, n)
          nn++;
      if (!maximum_size
          || maximum_size >= (tlv_len(tb.head)
                              + (4 + sizeof(dncp_t_keepalive_interval_s))
                              + nn * (4 + sizeof(dncp_t_node_state_s))))
        {
          dncp_for_each_node(o, n)
            {
              if (!_push_node_state_tlv(&tb, n, false))
                goto done;
            }
        }
    }
  if (l->conf.keepalive_interval != DNCP_KEEPALIVE_INTERVAL(l->dncp))
    if (!_push_keepalive_interval_tlv(&tb, l->iid, l->conf.keepalive_interval))
      goto done;
  if (maximum_size && tlv_len(tb.head) > maximum_size)
    {
      L_ERR("dncp_ep_i_send_network_state failed: %d > %d",
            (int)tlv_len(tb.head), (int)maximum_size);
      goto done;
    }
  L_DEBUG("dncp_ep_i_send_network_state -> " SA6_F "%%" DNCP_LINK_F,
          SA6_D(dst), DNCP_LINK_D(l));
  o->ext->cb.send(o->ext, &l->conf, src, dst,
                  tlv_data(tb.head), tlv_len(tb.head));
 done:
  tlv_buf_free(&tb);
}

void dncp_ep_i_send_node_state(dncp_ep_i l,
                               struct sockaddr_in6 *src,
                               struct sockaddr_in6 *dst,
                               dncp_node n)
{
  struct tlv_buf tb;
  dncp o = l->dncp;

  memset(&tb, 0, sizeof(tb));
  tlv_buf_init(&tb, 0); /* not passed anywhere */
  if (_push_link_id_tlv(&tb, l)
      && _push_node_state_tlv(&tb, n, true))
    {
      L_DEBUG("dncp_ep_i_send_node_data %s -> " SA6_F " %%" DNCP_LINK_F,
              DNCP_NODE_REPR(n), SA6_D(dst), DNCP_LINK_D(l));
      o->ext->cb.send(o->ext, &l->conf, src, dst,
                      tlv_data(tb.head), tlv_len(tb.head));
    }
  tlv_buf_free(&tb);
}

void dncp_ep_i_send_req_network_state(dncp_ep_i l,
                                      struct sockaddr_in6 *src,
                                      struct sockaddr_in6 *dst)
{
  struct tlv_buf tb;
  dncp o = l->dncp;

  memset(&tb, 0, sizeof(tb));
  tlv_buf_init(&tb, 0); /* not passed anywhere */
  if (_push_link_id_tlv(&tb, l)
      && _push_network_state_tlv(&tb, l->dncp) /* SHOULD include local */
      && tlv_new(&tb, DNCP_T_REQ_NET_STATE, 0))
    {
      L_DEBUG("dncp_ep_i_send_req_network_state -> " SA6_F "%%" DNCP_LINK_F,
              SA6_D(dst), DNCP_LINK_D(l));
      o->ext->cb.send(o->ext, &l->conf, src, dst,
                      tlv_data(tb.head), tlv_len(tb.head));
    }
  tlv_buf_free(&tb);
}

void dncp_ep_i_send_req_node_data(dncp_ep_i l,
                                  struct sockaddr_in6 *src,
                                  struct sockaddr_in6 *dst,
                                  dncp_t_node_state ns)
{
  struct tlv_buf tb;
  struct tlv_attr *a;
  dncp o = l->dncp;

  memset(&tb, 0, sizeof(tb));
  tlv_buf_init(&tb, 0); /* not passed anywhere */
  if (_push_link_id_tlv(&tb, l)
      && (a = tlv_new(&tb, DNCP_T_REQ_NODE_STATE, DNCP_NI_LEN(o))))
    {
      L_DEBUG("dncp_ep_i_send_req_node_data -> " SA6_F "%%" DNCP_LINK_F,
              SA6_D(dst), DNCP_LINK_D(l));
      dncp_node_identifier ni = dncp_tlv_get_node_identifier(l->dncp, ns);
      memcpy(tlv_data(a), ni, DNCP_NI_LEN(o));
      o->ext->cb.send(o->ext, &l->conf, src, dst,
                      tlv_data(tb.head), tlv_len(tb.head));
    }
  tlv_buf_free(&tb);
}

/************************************************************ Input handling */

static dncp_tlv
_heard(dncp_ep_i l, dncp_t_link_id lid, struct sockaddr_in6 *src,
       bool multicast)
{
  int nplen = sizeof(dncp_t_neighbor_s) + DNCP_NI_LEN(l->dncp);
  void *np = alloca(nplen);
  dncp_t_neighbor n_sample = np + DNCP_NI_LEN(l->dncp);
  memcpy(np, dncp_tlv_get_node_identifier(l->dncp, lid), DNCP_NI_LEN(l->dncp));
  n_sample->neighbor_link_id = lid->link_id;
  n_sample->link_id = l->iid;

  dncp_neighbor n;
  dncp_tlv t = dncp_find_tlv(l->dncp, DNCP_T_NEIGHBOR, np, nplen);
  if (!t)
    {
      /* Doing add based on multicast is relatively insecure. */
      if (multicast)
        return NULL;
      t =
        dncp_add_tlv(l->dncp, DNCP_T_NEIGHBOR, &np, sizeof(np),
                     sizeof(*n));
      if (!t)
        return NULL;
      n = dncp_tlv_get_extra(t);
      n->last_contact = dncp_time(l->dncp);
      L_DEBUG("Neighbor %s added on " DNCP_LINK_F,
              DNCP_STRUCT_REPR(lid->node_identifier), DNCP_LINK_D(l));
    }
  else
    n = dncp_tlv_get_extra(t);

  if (!multicast)
    {
      n->last_sa6 = *src;
    }
  return t;
}

/* Handle a single received message. */
static void
handle_message(dncp_ep_i l,
               struct sockaddr_in6 *src,
               struct sockaddr_in6 *dst,
               struct tlv_attr *msg)
{
  dncp o = l->dncp;
  struct tlv_attr *a;
  dncp_node n;
  dncp_t_link_id lid = NULL;
  dncp_tlv tne = NULL;
  dncp_neighbor ne = NULL;
  struct tlv_buf tb;
  uint32_t new_update_number;
  bool should_request_network_state = false;
  bool updated_or_requested_state = false;
  bool got_tlv = false;
  bool multicast = IN6_IS_ADDR_MULTICAST(&dst->sin6_addr);
  int nilen = DNCP_NI_LEN(l->dncp);
  int hlen = DNCP_HASH_LEN(l->dncp);

  /* Make sure source is IPv6 link-local (for now..) */
  if (!IN6_IS_ADDR_LINKLOCAL(&src->sin6_addr))
    return;

  /* Non-multicast destination has to be too. */
  if (!multicast && !IN6_IS_ADDR_LINKLOCAL(&dst->sin6_addr))
    return;

  /* Validate that link id exists (if this were TCP, we would keep
   * track of the remote link id on per-stream basis). */
  tlv_for_each_attr(a, msg)
    if (tlv_id(a) == DNCP_T_ENDPOINT_ID)
      {
        /* Error to have multiple top level link id's. */
        if (lid)
          {
            L_INFO("got multiple link ids - ignoring");
            return;
          }
        if (tlv_len(a) != sizeof(*lid) + nilen)
          {
            L_INFO("got invalid sized link id - ignoring");
            return;
          }
        lid = tlv_data(a) + nilen;
      }

  bool is_local = memcmp(dncp_tlv_get_node_identifier(l->dncp, lid),
                         &o->own_node->node_identifier,
                         nilen) == 0;
  if (!is_local && lid)
    {
      tne = _heard(l, lid, src, multicast);
      if (!tne)
        {
          if (!multicast)
            return; /* OOM */
          should_request_network_state = true;
        }
      ne = tne ? dncp_tlv_get_extra(tne) : NULL;
    }

  tlv_for_each_attr(a, msg)
    {
      got_tlv = true;
      switch (tlv_id(a))
        {
        case DNCP_T_REQ_NET_STATE:
          /* Ignore if in multicast. */
          if (multicast)
            L_INFO("ignoring req-net-hash in multicast");
          else
            dncp_ep_i_send_network_state(l, dst, src, 0);
          break;

        case DNCP_T_REQ_NODE_STATE:
          /* Ignore if in multicast. */
          if (multicast)
            {
              L_INFO("ignoring req-node-data in multicast");
              break;
            }
          void *p = tlv_data(a);
          if (tlv_len(a) != DNCP_HASH_LEN(o))
            break;
          n = dncp_find_node_by_node_identifier(o, p, false);
          if (!n)
            break;
          if (n != o->own_node)
            {
              if (o->graph_dirty)
                {
                  L_DEBUG("prune pending, ignoring node data request");
                  break;
                }

              if (n->last_reachable_prune != o->last_prune)
                {
                  L_DEBUG("not reachable request, ignoring");
                  break;
                }
            }
          dncp_ep_i_send_node_state(l, dst, src, n);
          break;

        case DNCP_T_NET_STATE:
          if (tlv_len(a) != DNCP_HASH_LEN(o))
            {
              L_DEBUG("got invalid network hash length: %d", tlv_len(a));
              break;
            }
          unsigned char *nethash = tlv_data(a);
          bool consistent = memcmp(nethash, &o->network_hash,
                                   DNCP_HASH_LEN(o)) == 0;
          L_DEBUG("received network state which is %sconsistent (%s)",
                  consistent ? "" : "in",
                  is_local ? "local" : ne ? "remote" : "unknown remote");

          if (consistent)
            {
              /* Increment Trickle count + last in sync time.*/
              if (ne)
                {
                  l->trickle_c++;
                  ne->last_contact = dncp_time(l->dncp);
                }
              else
                {
                  /* Send an unicast request, to potentially set up the
                   * peer structure. */
                  should_request_network_state = true;
                }
            }
          else
            {
              /* MUST: rate limit check */
              if ((dncp_time(o) - l->last_req_network_state) < l->conf.trickle_imin)
                break;
              l->last_req_network_state = dncp_time(o);

              should_request_network_state = true;
            }
          break;

        case DNCP_T_NODE_STATE:
          if (tlv_len(a) < sizeof(dncp_t_node_state_s) + nilen + hlen)
            {
              L_INFO("invalid length node state TLV received - ignoring");
              break;
            }
          dncp_t_node_state ns = tlv_data(a) + nilen;
          dncp_node_identifier ni = tlv_data(a);
          dncp_hash h = tlv_data(a) + nilen + sizeof(*ns);

          n = dncp_find_node_by_node_identifier(o, ni, false);
          new_update_number = be32_to_cpu(ns->update_number);
          bool interesting = !n
            || (dncp_update_number_gt(n->update_number, new_update_number)
                || (new_update_number == n->update_number
                    && memcmp(&n->node_data_hash, h, hlen) != 0));
          L_DEBUG("saw %s %s for %s/%p (update number %d)",
                  interesting ? "new" : "old",
                  tlv_len(a) == sizeof(*ns) ? "state" : "state+data",
                  DNCP_NODE_REPR(ns), n, new_update_number);
          if (!interesting)
            break;
          bool found_data = false;
          int nd_len = tlv_len(a) - sizeof(*ns);
#ifdef DTLS
          /* We don't accept node data via multicast in secure mode. */
          if (multicast && o->profile_data.d)
            nd_len = 0;
#endif /* DTLS */
          if (nd_len > 0)
            {
              unsigned char *nd_data = (unsigned char *)ns + sizeof(*ns);

              n = n ? n: dncp_find_node_by_node_identifier(o, ni, true);
              if (!n)
                return; /* OOM */
              if (dncp_node_is_self(n))
                {
                  L_DEBUG("received %d update number from network, own %d",
                          new_update_number, n->update_number);
                  if (o->collided)
                    {
                      if (o->ext->cb.handle_collision(o->ext))
                        return;
                    }
                  else
                    {
                      o->collided = true;
                      n->update_number = new_update_number + 1000 - 1;
                      /* republish increments the count too */
                    }
                  o->republish_tlvs = true;
                  dncp_schedule(o);
                  return;
                }
              /* Ok. nd contains more recent TLV data than what we have
               * already. Woot. */
              memset(&tb, 0, sizeof(tb));
              tlv_buf_init(&tb, 0); /* not passed anywhere */
              if (tlv_put_raw(&tb, nd_data, nd_len))
                {
                  dncp_node_set(n, new_update_number,
                                dncp_time(o) - be32_to_cpu(ns->ms_since_origination),
                                tb.head);
                }
              else
                {
                  L_DEBUG("tlv_put_raw failed");
                  tlv_buf_free(&tb);
                }
              found_data = true;
            }
          if (!found_data)
            {
              L_DEBUG("node data %s for %s",
                      multicast ? "not supplied" : "missing",
                      DNCP_NODE_REPR(ns));
              dncp_ep_i_send_req_node_data(l, dst, src, ns);
            }
          updated_or_requested_state = true;
          break;

        default:
          /* Unknown TLV - MUST ignore. */
          continue;
        }

    }

  /* Shared got unicast from the other party handling. */
  if (!multicast && got_tlv && ne)
    ne->last_contact = dncp_time(l->dncp);

  if (should_request_network_state && !updated_or_requested_state && !is_local)
    dncp_ep_i_send_req_network_state(l, dst, src);
}


void dncp_poll(dncp o)
{
  unsigned char buf[DNCP_MAXIMUM_PAYLOAD_SIZE+sizeof(struct tlv_attr)];
  struct tlv_attr *msg = (struct tlv_attr *)buf;
  ssize_t read;
  struct sockaddr_in6 *src;
  struct sockaddr_in6 *dst;
  dncp_ep_i l;
  dncp_ep ep;
  dncp_subscriber s;

  while ((read = o->ext->cb.recv(o->ext, &ep, &src, &dst,
                                 msg->data, DNCP_MAXIMUM_PAYLOAD_SIZE)) > 0)
    {
      tlv_init(msg, 0, read + sizeof(struct tlv_attr));

      l = container_of(ep, dncp_ep_i_s, conf);

      /* If the link is enabled, pass it along to the DNCP protocol core. */
      if (l->enabled)
        handle_message(l, src, dst, msg);

      /* _always_ provide it as callback to the users though. */
      list_for_each_entry(s, &o->subscribers[DNCP_CALLBACK_SOCKET_MSG],
                          lhs[DNCP_CALLBACK_SOCKET_MSG])
        s->msg_received_callback(s, ep, src, dst, msg);
    }
}

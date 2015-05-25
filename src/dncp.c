/*
 * $Id: dncp.c $
 *
 * Author: Markus Stenberg <markus stenberg@iki.fi>
 *
 * Copyright (c) 2013 cisco Systems, Inc.
 *
 * Created:       Wed Nov 20 16:00:31 2013 mstenber
 * Last modified: Mon May 25 14:46:21 2015 mstenber
 * Edit time:     897 min
 *
 */

#include "dncp_i.h"
#include <net/ethernet.h>
#include <arpa/inet.h>

int dncp_node_cmp(dncp_node n1, dncp_node n2)
{
  return memcmp(&n1->node_identifier, &n2->node_identifier,
                DNCP_NI_LEN(n1->dncp));
}

static int
compare_nodes(const void *a, const void *b, void *ptr __unused)
{
  dncp_node n1 = (dncp_node) a, n2 = (dncp_node) b;

  return dncp_node_cmp(n1, n2);
}

void dncp_schedule(dncp o)
{
  if (o->immediate_scheduled)
    return;
  o->ext->cb.schedule_timeout(o->ext, 0);
  o->immediate_scheduled = true;
}

void dncp_node_set(dncp_node n, uint32_t update_number,
                   hnetd_time_t t, struct tlv_attr *a)
{
  struct tlv_attr *a_valid = a;

  L_DEBUG("dncp_node_set %s update #%d %p (@%lld (-%lld))",
          DNCP_NODE_REPR(n), (int) update_number, a,
          (long long)t, (long long)(hnetd_time()-t));

  /* If the data is same, and update number is same, skip. */
  if (update_number == n->update_number
      && (!a || tlv_attr_equal(a, n->tlv_container)))
    {
      L_DEBUG(" .. spurious (no change, we ignore time delta)");
      return;
    }

  /* If new data is set, consider if similar, and if not,
   * handle version check  */
  if (a)
    {
      if (n->tlv_container && tlv_attr_equal(n->tlv_container, a))
        {
          if (n->tlv_container != a)
            {
              free(a);
              a = n->tlv_container;
            }
          a_valid = n->tlv_container_valid;
        }
      else
        {
          a_valid = n->dncp->ext->cb.validate_node_data(n, a);
        }
    }

  /* Replace update number if any */
  n->update_number = update_number;

  /* Replace origination time if any */
  if (t)
    n->origination_time = t;

  /* Replace data (if it is a different valid pointer) */
  if (a && n->tlv_container != a)
    {
      if (n->last_reachable_prune == n->dncp->last_prune)
        dncp_notify_subscribers_tlvs_changed(n, n->tlv_container_valid,
                                             a_valid);
      if (n->tlv_container)
        free(n->tlv_container);
      n->tlv_container = a;
      n->tlv_container_valid = a_valid;
      n->tlv_index_dirty = true;
      n->node_data_hash_dirty = true;
      n->dncp->graph_dirty = true;
    }

  /* _anything_ we do here dirties network hash. */
  n->dncp->network_hash_dirty = true;

  dncp_schedule(n->dncp);
}


static void update_node(__unused struct vlist_tree *t,
                        struct vlist_node *node_new,
                        struct vlist_node *node_old)
{
  dncp o = container_of(t, dncp_s, nodes);
  dncp_node n_old = container_of(node_old, dncp_node_s, in_nodes);
  __unused dncp_node n_new = container_of(node_new, dncp_node_s, in_nodes);

  if (n_old == n_new)
    return;
  if (n_old)
    {
      dncp_node_set(n_old, 0, 0, NULL);
      if (n_old->tlv_index)
        free(n_old->tlv_index);
      free(n_old);
    }
  if (n_new)
    {
      n_new->node_data_hash_dirty = true;
      n_new->tlv_index_dirty = true;
      /* By default unreachable */
      n_new->last_reachable_prune = o->last_prune - 1;
    }
  o->network_hash_dirty = true;
  o->graph_dirty = true;
  dncp_schedule(o);
}


static int
compare_tlvs(const void *a, const void *b, void *ptr __unused)
{
  dncp_tlv t1 = (dncp_tlv) a, t2 = (dncp_tlv) b;

  return tlv_attr_cmp(&t1->tlv, &t2->tlv);
}

static void update_tlv(struct vlist_tree *t,
                       struct vlist_node *node_new,
                       struct vlist_node *node_old)
{
  dncp o = container_of(t, dncp_s, tlvs);
  dncp_tlv t_old = container_of(node_old, dncp_tlv_s, in_tlvs);
  __unused dncp_tlv t_new = container_of(node_new, dncp_tlv_s, in_tlvs);

  if (t_old)
    {
      dncp_notify_subscribers_local_tlv_changed(o, &t_old->tlv, false);
      free(t_old);
    }
  if (t_new)
    dncp_notify_subscribers_local_tlv_changed(o, &t_new->tlv, true);

  o->tlvs_dirty = true;
  dncp_schedule(o);
}

static int
compare_links(const void *a, const void *b, void *ptr __unused)
{
  dncp_ep_i t1 = (dncp_ep_i) a, t2 = (dncp_ep_i) b;

  return strcmp(t1->conf.ifname, t2->conf.ifname);
}

void dncp_ep_i_set_keepalive_interval(dncp_ep_i l, uint32_t value)
{
  if (l->published_keepalive_interval == value)
    return;
  dncp o = l->dncp;
  if (l->published_keepalive_interval != DNCP_KEEPALIVE_INTERVAL(o))
    {
      dncp_t_keepalive_interval_s ka = { .link_id = l->iid,
                                         .interval_in_ms = cpu_to_be32(l->published_keepalive_interval) };
      dncp_remove_tlv_matching(o, DNCP_T_KEEPALIVE_INTERVAL, &ka, sizeof(ka));
    }
  if (value != DNCP_KEEPALIVE_INTERVAL(o))
    {
      dncp_t_keepalive_interval_s ka = { .link_id = l->iid,
                                         .interval_in_ms = cpu_to_be32(value) };
      dncp_add_tlv(o, DNCP_T_KEEPALIVE_INTERVAL, &ka, sizeof(ka), 0);
    }
  l->published_keepalive_interval = value;
}


static void update_link(struct vlist_tree *t,
                        struct vlist_node *node_new,
                        struct vlist_node *node_old)
{
  dncp o = container_of(t, dncp_s, links);
  dncp_ep_i t_old = container_of(node_old, dncp_ep_i_s, in_links);
  dncp_ep_i t_new = container_of(node_new, dncp_ep_i_s, in_links);

  if (t_old)
    {
      dncp_tlv t, t2;
      dncp_for_each_local_tlv_safe(o, t, t2)
        if (tlv_id(&t->tlv) == DNCP_T_NEIGHBOR)
          {
            dncp_t_neighbor ne = tlv_data(&t->tlv);
            if (ne->link_id == t_old->iid)
              dncp_remove_tlv(o, t);
          }
      /* kill TLV, if any */
      dncp_ep_i_set_keepalive_interval(t_old, DNCP_KEEPALIVE_INTERVAL(o));
      free(t_old);
    }
  else
    {
      t_new->published_keepalive_interval = DNCP_KEEPALIVE_INTERVAL(o);
    }
  dncp_schedule(o);
}


dncp_node
dncp_find_node_by_node_identifier(dncp o, dncp_node_identifier ni, bool create)
{
  dncp_node ch = container_of(ni, dncp_node_s, node_identifier);
  dncp_node n = vlist_find(&o->nodes, ch, ch, in_nodes);

  if (n)
    return n;
  if (!create)
    return NULL;
  n = calloc(1, sizeof(*n));
  if (!n)
    return false;
  n->node_identifier = *ni;
  n->dncp = o;
  n->tlv_index_dirty = true;
  vlist_add(&o->nodes, &n->in_nodes, n);
  return n;
}

bool dncp_init(dncp o, dncp_ext ext, const void *node_identifier, int len)
{
  dncp_hash_s h;
  int i;

  memset(o, 0, sizeof(*o));
  o->ext = ext;
  for (i = 0 ; i < NUM_DNCP_CALLBACKS; i++)
    INIT_LIST_HEAD(&o->subscribers[i]);
  vlist_init(&o->nodes, compare_nodes, update_node);
  o->nodes.keep_old = true;
  vlist_init(&o->tlvs, compare_tlvs, update_tlv);
  vlist_init(&o->links, compare_links, update_link);
  INIT_LIST_HEAD(&o->link_confs);
  dncp_calculate_hash(node_identifier, len, &h);
  o->first_free_iid = 1;
  o->last_prune = 1;
  /* this way new nodes with last_prune=0 won't be reachable */
  return dncp_set_own_node_identifier(o, (dncp_node_identifier)&h);
}

bool dncp_set_own_node_identifier(dncp o, dncp_node_identifier ni)
{
  if (o->own_node)
    {
      vlist_delete(&o->nodes, &o->own_node->in_nodes);
      o->own_node = NULL;
    }
  dncp_node n = dncp_find_node_by_node_identifier(o, ni, true);
  if (!n)
    {
      L_ERR("unable to create own node");
      return false;
    }
  o->own_node = n;
  o->tlvs_dirty = true; /* by default, they are, even if no neighbors yet. */
  n->last_reachable_prune = o->last_prune; /* we're always reachable */
  dncp_schedule(o);
  return true;
}

dncp dncp_create(dncp_ext ext)
{
  dncp o;
  unsigned char buf[ETHER_ADDR_LEN * 2], *c = buf;

  /* dncp_init does memset 0 -> we can just malloc here. */
  o = malloc(sizeof(*o));
  if (!o)
    return NULL;
  c += ext->cb.get_hwaddrs(ext, buf, sizeof(buf));
  if (c == buf)
    {
      L_ERR("no hardware address available, fatal error");
      goto err;
    }
  if (!dncp_init(o, ext, buf, c-buf))
    {
      /* Error produced elsewhere .. */
      goto err;
    }
  return o;
 err:
  free(o);
  return NULL;
}

void dncp_uninit(dncp o)
{
  /* TLVs should be freed first; they're local phenomenom, but may be
   * reflected on links/nodes. */
  vlist_flush_all(&o->tlvs);

  /* Link destruction will refer to node -> have to be taken out
   * before nodes. */
  vlist_flush_all(&o->links);

  /* All except own node should be taken out first. */
  vlist_update(&o->nodes);
  o->own_node->in_nodes.version = -1;
  vlist_flush(&o->nodes);

  /* Finally, we can kill own node too. */
  vlist_flush_all(&o->nodes);

  /* Get rid of TLV index. */
  if (o->num_tlv_indexes)
    free(o->tlv_type_to_index);
}

void dncp_destroy(dncp o)
{
  if (!o) return;
  dncp_uninit(o);
  free(o);
}

dncp_node dncp_get_first_node(dncp o)
{
  dncp_node n;

  if (avl_is_empty(&o->nodes.avl))
    return NULL;
  n = avl_first_element(&o->nodes.avl, n, in_nodes.avl);
  if (n->last_reachable_prune == o->last_prune)
    return n;
  return dncp_node_get_next(n);
}

dncp_tlv
dncp_add_tlv(dncp o, uint16_t type, void *data, uint16_t len, int extra_bytes)
{
  int plen =
    (TLV_SIZE + len + TLV_ATTR_ALIGN - 1) & ~(TLV_ATTR_ALIGN - 1);
  dncp_tlv t = calloc(1, sizeof(*t) + plen + extra_bytes);

  if (!t)
    return NULL;
  tlv_init(&t->tlv, type, len + TLV_SIZE);
  memcpy(tlv_data(&t->tlv), data, len);
  tlv_fill_pad(&t->tlv);
  vlist_add(&o->tlvs, &t->in_tlvs, t);
  return t;
}

void dncp_remove_tlv(dncp o, dncp_tlv tlv)
{
  if (!tlv)
    return;
  vlist_delete(&o->tlvs, &tlv->in_tlvs);
}

int dncp_remove_tlvs_by_type(dncp o, int type)
{
  dncp_tlv t, t2;
  int c = 0;

  avl_for_each_element_safe(&o->tlvs.avl, t, in_tlvs.avl, t2)
    {
      if ((int)tlv_id(&t->tlv) == type)
        {
          dncp_remove_tlv(o, t);
          c++;
        }
    }
  return c;
}

static void dncp_ep_set_default(dncp o, dncp_ep conf, const char *ifname)
{
  *conf = o->ext->conf.per_link;
  strncpy(conf->dnsname, ifname, sizeof(conf->ifname));
  strncpy(conf->ifname, ifname, sizeof(conf->ifname));
}

dncp_ep dncp_ep_find_by_name(dncp o, const char *ifname)
{
  dncp_ep_i l = dncp_find_link_by_name(o, ifname, true);

  return l ? &l->conf : NULL;
}

dncp_ep_i dncp_find_link_by_name(dncp o, const char *ifname, bool create)
{
  dncp_ep_i cl = container_of(ifname, dncp_ep_i_s, conf.ifname[0]);
  dncp_ep_i l;

  if (!ifname || !*ifname)
    return NULL;

  l = vlist_find(&o->links, cl, cl, in_links);

  if (create && !l)
    {
      l = (dncp_ep_i) calloc(1, sizeof(*l));
      if (!l)
        return NULL;
      l->dncp = o;
      l->iid = o->first_free_iid++;
      dncp_ep_set_default(o, &l->conf, ifname);
      vlist_add(&o->links, &l->in_links, l);
    }
  return l;
}

dncp_ep_i dncp_find_link_by_id(dncp o, uint32_t link_id)
{
  dncp_ep_i l;
  /* XXX - this could be also made more efficient. Oh well. */
  vlist_for_each_element(&o->links, l, in_links)
    if (l->iid == link_id)
      return l;
  return NULL;
}

void dncp_ext_ep_ready(dncp_ep ep, bool enabled)
{
  dncp_ep_i l = container_of(ep, dncp_ep_i_s, conf);

  L_DEBUG("dncp_ext_ep_ready %s %s %s", ep->ifname, enabled ? "+" : "-",
          !l->enabled == !enabled ? "(redundant)" : "");
  if (!l->enabled == !enabled)
      return;
  dncp_notify_subscribers_link_changed(l, enabled ? DNCP_EVENT_ADD : DNCP_EVENT_REMOVE);
  l->enabled = enabled;
}

bool dncp_node_is_self(dncp_node n)
{
  return n->dncp->own_node == n;
}

dncp_node dncp_node_get_next(dncp_node n)
{
  dncp o = n->dncp;
  dncp_node last = avl_last_element(&o->nodes.avl, n, in_nodes.avl);

  if (!n || n == last)
    return NULL;
  while (1)
    {
      n = avl_next_element(n, in_nodes.avl);
      if (n->last_reachable_prune == o->last_prune)
        return n;
      if (n == last)
        return NULL;
    }
}

static struct tlv_attr *_produce_new_tlvs(dncp_node n)
{
  struct tlv_buf tb;
  dncp o = n->dncp;
  dncp_tlv t;

  if (!o->tlvs_dirty)
    return NULL;

  /* Dump the contents of dncp->tlvs to single tlv_buf. */
  /* Based on whether or not that would cause change in things, 'do stuff'. */
  memset(&tb, 0, sizeof(tb));
  tlv_buf_init(&tb, 0); /* not passed anywhere */
  vlist_for_each_element(&o->tlvs, t, in_tlvs)
    {
      struct tlv_attr *a = tlv_put_raw(&tb, &t->tlv, tlv_pad_len(&t->tlv));
      if (!a)
        {
          L_ERR("dncp_self_flush: tlv_put_raw failed?!?");
          tlv_buf_free(&tb);
          return NULL;
        }
      tlv_fill_pad(a);
    }
  tlv_fill_pad(tb.head);

  /* Ok, all puts _did_ succeed. */
  o->tlvs_dirty = false;

  if (n->tlv_container && tlv_attr_equal(tb.head, n->tlv_container))
    {
      tlv_buf_free(&tb);
      return NULL;
    }
  return tb.head;
}

void dncp_self_flush(dncp_node n)
{
  dncp o = n->dncp;
  struct tlv_attr *a, *a2;

  if (!(a = _produce_new_tlvs(n)) && !o->republish_tlvs)
    {
      L_DEBUG("dncp_self_flush: state did not change -> nothing to flush");
      return;
    }

  L_DEBUG("dncp_self_flush: notify about to republish tlvs");
  dncp_notify_subscribers_about_to_republish_tlvs(n);

  o->republish_tlvs = false;
  a2 = _produce_new_tlvs(n);
  if (a2)
    {
      if (a)
        free(a);
      a = a2;
    }
  dncp_node_set(n, n->update_number + 1, dncp_time(o),
                a ? a : n->tlv_container);
}

struct tlv_attr *dncp_node_get_tlvs(dncp_node n)
{
  return n->tlv_container_valid;
}


void dncp_calculate_node_data_hash(dncp_node n)
{
  int l;

  if (!n->node_data_hash_dirty)
    return;
  n->node_data_hash_dirty = false;
  l = n->tlv_container ? tlv_len(n->tlv_container) : 0;
  n->dncp->ext->cb.hash(tlv_data(n->tlv_container), l, &n->node_data_hash);
  L_DEBUG("dncp_calculate_node_data_hash %s=%llx%s",
          DNCP_NODE_REPR(n),
          dncp_hash64(&n->node_data_hash),
          n == n->dncp->own_node ? " [self]" : "");
}

void dncp_calculate_network_hash(dncp o)
{
  dncp_node n;

  if (!o->network_hash_dirty)
    return;

  /* Store original network hash for future study. */
  dncp_hash_s old_hash = o->network_hash;

  int cnt = 0;
  dncp_for_each_node(o, n)
    cnt++;
  int onelen = 4 + DNCP_HASH_LEN(o);
  void *buf = malloc(cnt * onelen);
  if (!buf)
    return;
  cnt = 0;
  dncp_for_each_node(o, n)
    {
      void *dst = buf + cnt++ * onelen;
      uint32_t update_number = cpu_to_be32(n->update_number);
      dncp_calculate_node_data_hash(n);
      *((uint32_t *)dst) = update_number;
      memcpy(dst + 4, &n->node_data_hash, DNCP_HASH_LEN(o));
      L_DEBUG(".. %s/%d=%llx",
              DNCP_NODE_REPR(n), n->update_number,
              dncp_hash64(&n->node_data_hash));
    }
  o->ext->cb.hash(buf, cnt * onelen, &o->network_hash);
  L_DEBUG("dncp_calculate_network_hash =%llx",
          dncp_hash64(&o->network_hash));

  if (memcmp(&old_hash, &o->network_hash, DNCP_HASH_LEN(o)))
    dncp_trickle_reset(o);

  o->network_hash_dirty = false;
}

bool dncp_add_tlv_index(dncp o, uint16_t type)
{
  if (type < o->tlv_type_to_index_length)
    {
      if (o->tlv_type_to_index[type])
        {
          L_DEBUG("dncp_add_tlv_index called for existing index (type %d)",
                  (int)type);
          return true;
        }
    }
  else
    {
      int old_len = o->tlv_type_to_index_length;
      int old_size = old_len * sizeof(o->tlv_type_to_index[0]);
      int new_len = type + 1;
      int new_size = new_len * sizeof(o->tlv_type_to_index[0]);
      int *ni = realloc(o->tlv_type_to_index, new_size);
      if (!ni)
        return false;
      memset((void *)ni + old_size, 0, new_size - old_size);
      o->tlv_type_to_index = ni;
      o->tlv_type_to_index_length = new_len;
      L_DEBUG("dncp_add_tlv_index grew tlv_type_to_index to %d", new_len);
    }

  L_DEBUG("dncp_add_tlv_index: type #%d = index #%d", type, o->num_tlv_indexes);
  o->tlv_type_to_index[type] = ++o->num_tlv_indexes;

  /* Free existing indexes */
  dncp_node n;
  dncp_for_each_node_including_unreachable(o, n)
    {
      if (n->tlv_index)
        {
          free(n->tlv_index);
          n->tlv_index = NULL;
          n->tlv_index_dirty = true;
        }
      assert(n->tlv_index_dirty);
    }
  return true;
}


bool dncp_ep_has_highest_id(dncp_ep ep)
{
  dncp_ep_i l = container_of(ep, dncp_ep_i_s, conf);
  dncp o = l->dncp;
  uint32_t iid = l->iid;
  struct tlv_attr *a;
  dncp_t_neighbor nh;

  dncp_node_for_each_tlv_with_type(o->own_node, a, DNCP_T_NEIGHBOR)
    if ((nh = dncp_tlv_neighbor(o, a)))
      {
        if (nh->link_id != iid)
          continue;

        if (memcmp(dncp_tlv_get_node_identifier(o, nh),
                   &o->own_node->node_identifier, DNCP_NI_LEN(o)) > 0)
          return false;
      }
  return true;
}


void dncp_node_recalculate_index(dncp_node n)
{
  int size = n->dncp->num_tlv_indexes * 2 * sizeof(n->tlv_index[0]);

  assert(n->tlv_index_dirty);
  if (!n->tlv_index)
    {
      n->tlv_index = calloc(1, size);
      if (!n->tlv_index)
        return;
    }
  else
    {
      memset(n->tlv_index, 0, size);
    }

  dncp o = n->dncp;
  struct tlv_attr *a;
  int type = -1;
  int idx = 0;

  /* Note: This algorithm isn't particularly clever - while linear in
   * speed (O(# of indexes + # of entries in tlv_container), it has bit
   * too significant constant factor for comfort. */
  dncp_node_for_each_tlv(n, a)
    {
      if ((int)tlv_id(a) != type)
        {
          type = tlv_id(a);
          /* No more indexes here -> stop iteration */
          if (type >= o->tlv_type_to_index_length)
            break;
          if (!(idx = o->tlv_type_to_index[type]))
            continue;
          n->tlv_index[2 * idx - 2] = a;
          assert(idx <= n->dncp->num_tlv_indexes);
        }
      if (idx)
        n->tlv_index[2 * idx - 1] = tlv_next(a);
    }
  n->tlv_index_dirty = false;
}

dncp_tlv dncp_find_tlv(dncp o, uint16_t type, void *data, uint16_t len)
{
  /* This is actually slower than list iteration if publishing only
   * 'some' data. Oh well. I suppose the better performance for 'large
   * N' cases is more useful. */
  dncp_tlv dt = alloca(sizeof(dncp_tlv_s) + len);
  if (!dt)
    return NULL;
  tlv_init(&dt->tlv, type, len + TLV_SIZE);
  memcpy(tlv_data(&dt->tlv), data, len);
  tlv_fill_pad(&dt->tlv);
  return vlist_find(&o->tlvs, dt, dt, in_tlvs);
}

void *dncp_tlv_get_extra(dncp_tlv t)
{
  unsigned int ofs = tlv_pad_len(&t->tlv);
  return ((unsigned char *)t + sizeof(*t) + ofs);
}

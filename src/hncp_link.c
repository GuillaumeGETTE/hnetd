/*
 * Author: Steven Barth <steven@midlink.org>
 *
 * Copyright (c) 2015 cisco Systems, Inc.
 */


#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <net/if.h>

#include "iface.h"
#include "dncp_i.h"
#include "hncp_i.h"
#include "hncp_proto.h"
#include "hncp_link.h"

struct hncp_link {
	dncp dncp;
	dncp_tlv versiontlv;
	dncp_subscriber_s subscr;
	struct iface_user iface;
	struct list_head users;
};

static void notify(struct hncp_link *l, const char *ifname, hncp_ep_id ids, size_t cnt,
		enum hncp_link_elected elected)
{
	L_DEBUG("hncp_link_notify: %s neighbors: %d elected(SMPHL): %x", ifname, (int)cnt, elected);

	struct hncp_link_user *u;
	list_for_each_entry(u, &l->users, head) {
		if (u->cb_link)
			u->cb_link(u, ifname, ids, cnt);

		if (u->cb_elected)
			u->cb_elected(u, ifname, elected);
	}
}

static void calculate_link(struct hncp_link *l, dncp_ep ep, bool enable)
{
	hncp_t_version ourvertlv = NULL;
	enum hncp_link_elected elected = HNCP_LINK_NONE;
	hncp_ep_id peers = NULL;
	size_t peercnt = 0, peerpos = 0;
	if (!ep)
		return;

	if (l->versiontlv) {
		ourvertlv = tlv_data(dncp_tlv_get_attr(l->versiontlv));

		if (ourvertlv->cap_mdnsproxy)
			elected |= HNCP_LINK_MDNSPROXY;

		if (ourvertlv->cap_prefixdel)
			elected |= HNCP_LINK_PREFIXDEL;

		if (ourvertlv->cap_hostnames)
			elected |= HNCP_LINK_HOSTNAMES;

		if (ourvertlv->cap_legacy)
			elected |= HNCP_LINK_LEGACY;

		if (!dncp_ep_is_enabled(ep))
			elected |= HNCP_LINK_STATELESS;
	}

	L_DEBUG("hncp_link_calculate: %s peer-candidates: %d preelected(SMPHL): %x",
			ep->ifname, (int)peercnt, elected);

	if (enable) {
		struct tlv_attr *c;
		dncp_node_for_each_tlv(dncp_get_own_node(l->dncp), c) {
			dncp_t_neighbor ne = dncp_tlv_neighbor(l->dncp, c);
			hncp_t_assigned_prefix_header ah = hncp_tlv_ap(c);

			if (ne && ne->ep_id == dncp_ep_get_id(ep))
				++peercnt;
			else if (ah && ah->ep_id == dncp_ep_get_id(ep))
				elected |= HNCP_LINK_STATELESS;
		}

		if (peercnt)
			peers = calloc(1, sizeof(*peers) * peercnt);

		L_DEBUG("hncp_link_calculate: local node advertises %d "
			"neighbors on iface %d", (int)peercnt, (int)dncp_ep_get_id(ep));

		dncp_node_for_each_tlv(dncp_get_own_node(l->dncp), c) {
			dncp_t_neighbor cn = dncp_tlv_neighbor(l->dncp, c);

			if (!cn || cn->ep_id != dncp_ep_get_id(ep))
				continue;

			dncp_node peer = dncp_find_node_by_node_id(l->dncp, dncp_tlv_get_node_id(l->dncp, cn), false);

			if (!peer || !peers)
				continue;

			bool mutual = false;
			hncp_t_version peervertlv = NULL;

			struct tlv_attr *pc;
			dncp_node_for_each_tlv(peer, pc) {
				if (tlv_id(pc) == HNCP_T_VERSION &&
						tlv_len(pc) > sizeof(*peervertlv))
					peervertlv = tlv_data(pc);

				dncp_t_neighbor pn = dncp_tlv_neighbor(l->dncp, pc);
				if (!pn || pn->ep_id != cn->neighbor_ep_id ||
				    memcmp(dncp_tlv_get_node_id(l->dncp, pn), &dncp_get_own_node(l->dncp)->node_id, DNCP_NI_LEN(l->dncp)))
					continue;

				if (pn->neighbor_ep_id == dncp_ep_get_id(ep)) {
					// Matching reverse neighbor entry
					L_DEBUG("hncp_link_calculate: if %"PRIu32" -> neigh %s:%"PRIu32,
							dncp_ep_get_id(ep), DNCP_STRUCT_REPR(peer->node_id), pn->ep_id);
					mutual = true;
					memcpy(&peers[peerpos].node_id, &peer->node_id, HNCP_NI_LEN);
					peers[peerpos].ep_id = pn->ep_id;
					++peerpos;
				} else if (pn->neighbor_ep_id < dncp_ep_get_id(ep)) {
					L_WARN("hncp_link_calculate: %s links %d and %d appear to be connected",
							ep->ifname, dncp_ep_get_id(ep), pn->neighbor_ep_id);

					// Two of our links seem to be connected
					enable = false;
					break;
				}
			}

			if (!enable)
				break;

			// Capability election
			if (mutual && ourvertlv && peervertlv) {
				int ourcaps = ourvertlv->cap_mdnsproxy << 12 |
						ourvertlv->cap_prefixdel << 8 |
						ourvertlv->cap_hostnames << 4 |
						ourvertlv->cap_legacy;
				int peercaps = peervertlv->cap_mdnsproxy << 12 |
						peervertlv->cap_prefixdel << 8 |
						peervertlv->cap_hostnames << 4 |
						peervertlv->cap_legacy;

				if (ourvertlv->cap_mdnsproxy < peervertlv->cap_mdnsproxy)
					elected &= ~HNCP_LINK_MDNSPROXY;

				if (ourvertlv->cap_prefixdel < peervertlv->cap_prefixdel)
					elected &= ~HNCP_LINK_PREFIXDEL;

				if (ourvertlv->cap_hostnames < peervertlv->cap_hostnames)
					elected = (elected & ~HNCP_LINK_HOSTNAMES) | HNCP_LINK_OTHERMNGD;

				if (ourvertlv->cap_legacy < peervertlv->cap_legacy)
					elected &= ~HNCP_LINK_LEGACY;

				if (ourcaps < peercaps || (ourcaps == peercaps &&
						memcmp(&dncp_get_own_node(l->dncp)->node_id, &peer->node_id, DNCP_NI_LEN(l->dncp)) < 0)) {
					if (peervertlv->cap_mdnsproxy &&
							ourvertlv->cap_mdnsproxy == peervertlv->cap_mdnsproxy)
						elected &= ~HNCP_LINK_MDNSPROXY;

					if (peervertlv->cap_prefixdel &&
							ourvertlv->cap_prefixdel == peervertlv->cap_prefixdel)
						elected &= ~HNCP_LINK_PREFIXDEL;

					if (peervertlv->cap_hostnames &&
							ourvertlv->cap_hostnames == peervertlv->cap_hostnames)
						elected = (elected & ~HNCP_LINK_HOSTNAMES) | HNCP_LINK_OTHERMNGD;

					if (peervertlv->cap_legacy &&
							ourvertlv->cap_legacy == peervertlv->cap_legacy)
						elected &= ~HNCP_LINK_LEGACY;
				}

				L_DEBUG("hncp_link_calculate: %s peer: %x peer-caps: %x ourcaps: %x pre-elected(SMPHL): %x",
						ep->ifname, *((uint32_t*)&peer->node_id), peercaps, ourcaps, elected);
			}
		}
	}

	notify(l, ep->ifname, (!enable) ? NULL : (peers) ? peers : (void*)1,
			(enable) ? peerpos : 0, enable ? elected : HNCP_LINK_NONE);
	free(peers);
}

static void cb_intiface(struct iface_user *u, const char *ifname, bool enabled)
{
	struct hncp_link *l = container_of(u, struct hncp_link, iface);
	calculate_link(l, dncp_find_ep_by_name(l->dncp, ifname), enabled);
}

static void cb_intaddr(struct iface_user *u, const char *ifname,
			const struct prefix *addr6 __unused, const struct prefix *addr4 __unused)
{
	struct iface *iface = iface_get(ifname);
	cb_intiface(u, ifname, iface && iface->internal);
}

static void cb_tlv(dncp_subscriber s, dncp_node n,
		struct tlv_attr *tlv, bool add __unused)
{
	struct hncp_link *l = container_of(s, struct hncp_link, subscr);
	dncp_t_neighbor ne = dncp_tlv_neighbor(l->dncp, tlv);
	dncp_ep ep = NULL;

	if (ne) {
		if (dncp_node_is_self(n)) {
			L_DEBUG("hncp_link: local neighbor tlv changed");
			ep = dncp_find_ep_by_id(l->dncp, ne->ep_id);
		} else if (!memcmp(dncp_tlv_get_node_id(l->dncp, ne),  &dncp_get_own_node(l->dncp)->node_id, DNCP_NI_LEN(l->dncp))) {
			L_DEBUG("hncp_link: other node neighbor tlv changed");
			ep = dncp_find_ep_by_id(l->dncp, ne->neighbor_ep_id);
		}
	}

	if (ep) {
		struct iface *iface = iface_get(ep->ifname);
		L_DEBUG("hncp_link: iface is %s (%d)", ep->ifname, (int)dncp_ep_get_id(ep));
		calculate_link(l, ep, iface && iface->internal);
	}
}

struct hncp_link* hncp_link_create(dncp dncp, const struct hncp_link_config *conf)
{
	struct hncp_link *l = calloc(1, sizeof(*l));
	if (l) {
		l->dncp = dncp;
		INIT_LIST_HEAD(&l->users);

		l->subscr.tlv_change_cb = cb_tlv;
		dncp_subscribe(dncp, &l->subscr);

		l->iface.cb_intiface = cb_intiface;
		l->iface.cb_intaddr = cb_intaddr;
		iface_register_user(&l->iface);

		if (conf && conf->version == HNCP_T_VERSION_INDICATED_VERSION) {
			struct __packed {
				hncp_t_version_s version;
				char agent[sizeof(conf->agent)];
			} data = {
				{0, 0, conf->cap_mdnsproxy, conf->cap_prefixdel,
						conf->cap_hostnames, conf->cap_legacy}, {0}
			};
			memcpy(data.agent, conf->agent, sizeof(data.agent));

			l->versiontlv = dncp_add_tlv(dncp, HNCP_T_VERSION, &data,
					sizeof(hncp_t_version_s) + strlen(conf->agent) + 1, 0);
		}
	}
	return l;
}

void hncp_link_destroy(struct hncp_link *l)
{
	while (!list_empty(&l->users))
		list_del(l->users.next);

	dncp_remove_tlv(l->dncp, l->versiontlv);
	dncp_unsubscribe(l->dncp, &l->subscr);
	iface_unregister_user(&l->iface);
	free(l);
}

void hncp_link_register(struct hncp_link *l, struct hncp_link_user *user)
{
	list_add(&user->head, &l->users);
}

void hncp_link_unregister(struct hncp_link_user *user)
{
	list_del(&user->head);
}

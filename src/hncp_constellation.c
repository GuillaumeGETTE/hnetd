/*
 * $Id: hncp_constellation.c $
 */


#include "hncp_constellation.h"

typedef struct hncp_constellation_struct
{
	dncp dncp;
	struct uloop_timeout monitor_timeout;
	struct uloop_timeout localization_timeout;

	/* Creation-time parameters */
	//hncp_multicast_params_s p;

	/* Interface list */
	/*
	   struct list_head ifaces;
	   struct exeq exeq;

	   char has_rpa : 1;
	   char has_address : 1;
	   char is_controller : 1;
	   struct in6_addr current_rpa;
	   dncp_tlv rpa_tlv;
	   struct in6_addr current_address;
	   */

	/* Callbacks from other modules */
	/*
	   struct iface_user iface;
	   dncp_subscriber_s subscriber;
	   */
} *hc;


static int i_ = 0, j_ = 0, k_ = 0;

static void power_update(hc hc) {
	FILE* f = fopen("/tmp/hnet-log", "a");
	fprintf(f, "In\t'power_update' %d\n", i_);
	/* TODO Really make this function */
	fprintf(f, "Out\t'power_update' %d\n", i_);
	fclose(f);
	i_++;
};

static void localization_update(hc hc) {
	FILE* f = fopen("/tmp/hnet-log", "a");
	fprintf(f, "In\t'localization_update' %d\n", j_);
	/* TODO Really make this function */
	fprintf(f, "Out\t'localization_update' %d\n", j_);
	fclose(f);
	j_++;
};

static void _monitor_timeout(struct uloop_timeout *t)
{
	hncp_constellation c = container_of(t, hncp_constellation_s, monitor_timeout);
	/* TODO Make it better */
	power_update(c);
	uloop_timeout_set(&c->monitor_timeout, MONITOR_TIMEOUT);
}

static void _localization_timeout(struct uloop_timeout *t)
{
	hncp_constellation c = container_of(t, hncp_constellation_s, localization_timeout);
	/* TODO Make it better */
	localization_update(c);
	uloop_timeout_set(&c->localization_timeout, LOCALIZATION_TIMEOUT);
}

hncp_constellation hncp_constellation_create(hncp h/*, hncp_constellation_params p*/) {
	FILE* f = fopen("/tmp/hnet-log", "a");
	fprintf(f, "In\t'hncp_constellation_create' %d\n", k_);
	fclose(f);

	/* Initialization  */
	hncp_constellation c;
	if (!(c = calloc(1, sizeof(*c))))
		return NULL;

	c->dncp = hncp_get_dncp(h);
	c->monitor_timeout.cb = _monitor_timeout;
	c->localization_timeout.cb = _localization_timeout;

	/* TODO Initialize the monitoring and the localization */

	/* Set the timeouts */
	uloop_timeout_set(&c->monitor_timeout, MONITOR_TIMEOUT);
	uloop_timeout_set(&c->localization_timeout, LOCALIZATION_TIMEOUT);

	f = fopen("/tmp/hnet-log", "a");
	fprintf(f, "Out\t'hncp_constellation_create' %d\n", k_);
	fclose(f);
	k_++;

	return c;
}

void hncp_constellation_destroy(hncp_constellation c) {
	uloop_timeout_cancel(&c->monitor_timeout);
	uloop_timeout_cancel(&c->localization_timeout);
	free(c);
}

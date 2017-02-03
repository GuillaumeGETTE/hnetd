/*
 * hncp_constellation.c
 */


#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include "hncp_constellation.h"
#include "power_monitor.h"
#include "dncp_i.h"
#include "dncp.h"
#include "hncp_proto.h"
#include "constellation_localization/Structures.h"
#include "constellation_localization/Calcul_distance_Rn.h"

#define PACKET_BUFFER_SIZE 1000

const char version[59] = "Ornithorynque 2.0";

typedef struct constellation_data {
	char router_id[6];
	char user_id[6];
	float power;
} cd;

struct data_list {
	struct data_list* next;
	dncp_tlv tlv;
	struct constellation_data data;
	bool to_update;
};

typedef struct hncp_constellation_struct {
	dncp dncp;
  	dncp_subscriber_s subscriber;

	/* Monitoring */
	struct uloop_timeout monitor_timeout;
	char hwaddr[6];
	int monitor_socket;
	char* packet_buffer;
	int buffer_size;
	struct data_list* data_list;

	/* Localization */
	struct uloop_timeout localization_timeout;
	int nb_routeurs;
	char* routers;
	Liste references;
	Liste utilisateurs;
} *hc;


/* DEBUG */
static void fprintf_hwaddr(FILE* f, char* addr) {
	fprintf(f, "%.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

/*
 * Set 'addr' to the hardware address of the router on the interface ifname.
 * Return 0 if ok, -1 otherwise.
 */
static int get_hwaddr(char* addr, char* ifname) {
	struct ifreq ifr;
	int fd;
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		return -1;
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1)
		return -1;
	close(fd);
	memcpy(addr, ifr.ifr_hwaddr.sa_data, 6);
	return 0;
}

/*
 * Update the data_list of 'c' with 'data'.
 * To publish all the gathered data, use 'publish_constellation_data(c)'.
 */
static void update_constellation_data(hc c, cd* data) {
	struct data_list** l = &c->data_list;
	while (*l != NULL && memcmp((*l)->data.user_id, data->user_id, 6) != 0) {
		l = &(*l)->next;
	}
	if (*l == NULL) {
		*l = malloc(sizeof(struct data_list));
		(*l)->next = NULL;
		(*l)->tlv = NULL;
	}
	memcpy(&(*l)->data, data, sizeof(*data));
	(*l)->to_update = 1;
}

/*
 * Publish all the needed TLV to update the monitoring data to homenet.
 */
static void publish_constellation_data(hc c) {
	struct data_list* l = c->data_list;
	while (l != NULL) {
		if (l->to_update) {
			if (l->tlv != NULL)
				dncp_remove_tlv(c->dncp, l->tlv);
			l->tlv = dncp_add_tlv(c->dncp, HNCP_T_CONSTELLATION, &l->data, sizeof(l->data), 0);
			l->to_update = 0;
		}
		l = l->next;
	}
}

/*
 * Callback function.
 * Read all wi-fi packets and publish TLVs containing transmission power of the
 * packets from each device the router can hear.
 */
static void _monitor_timeout(struct uloop_timeout *t)
{
	hncp_constellation c = container_of(t, hncp_constellation_s, monitor_timeout);

	/* Main loop, updating the power TLVs */
	struct monitored_data_struct md;
	cd data;
	memcpy(data.router_id, c->hwaddr, 6);
	int retv;
	while ((retv = power_monitoring_next(&md, c->monitor_socket, c->packet_buffer, c->buffer_size)) != -1) {
		if (retv == 1) /* The packet didn't contain expected information */
			continue;
		memcpy(data.user_id, md.hwaddr, 6);
		data.power = md.power;
		update_constellation_data(c, &data);
	}
	publish_constellation_data(c);

	uloop_timeout_set(&c->monitor_timeout, MONITOR_TIMEOUT);
}

/*
 * Callback function.
 * Do localization things... TODO To complete.
 */
static void _tlv_change_callback(dncp_subscriber s, dncp_node n, struct tlv_attr* tlv, bool add) {
	/* DEBUG */
	FILE* f = fopen("/tmp/hnet-log", "a");
	cd* data;
	char user_id[7];

	hncp_constellation c = container_of(s, hncp_constellation_s, subscriber);
	switch (tlv_id(tlv)) {
		case HNCP_T_CONSTELLATION:
			if (add) {
				data = (cd*) tlv->data;
				memcpy(user_id, data->user_id, 6);
				user_id[6] = 0; /* FIXME Il ne faut pas utiliser ça comme id des routeurs */
				/* On trouve de quel routeur il s’agit FIXME pas cool */
				int k;
				for (k = 0 ; k < c->nb_routeurs ; ++k) {
					/* DEBUG
					fprintf(f, "-------");
					fprintf_hwaddr(f, c->routers + 6*k);
					fprintf(f, "-------\n");
					*/
					if (!memcmp(c->routers + 6 * k, data->router_id, 6))
						break;
				}
				if (k == c->nb_routeurs) {
					/* Le routeur ne fait pas partis de ceux étudiés, ça ne devrait pas arriver… Que faire ? FIXME */
					/* END DEBUG */
					fprintf(f, "Unidentified router");
					fprintf_hwaddr(f, data->router_id);
					fprintf(f, " saw ");
					fprintf_hwaddr(f, data->user_id);
					fprintf(f, " with power %.1f\n", data->power);
					/* DEBUG */
				} else {
					/* END DEBUG */
					fprintf(f, "Router n°%d saw ", k);
					fprintf_hwaddr(f, data->user_id);
					fprintf(f, " with power %.1f\n", data->power);
					maj_utilisateur(user_id, k, data->power, &c->utilisateurs, c->nb_routeurs);
					/* DEBUG */
				}
			}
			break;
		default:
			break;
	}
	fclose(f);
}

/*
 * Today, it just prints the rooms where the user is, for every user.
 * TODO Do better…
 */
static void _localization_timeout(struct uloop_timeout *t) {
	FILE* f = fopen("/tmp/hnet-log", "a");
	hncp_constellation c = container_of(t, hncp_constellation_s, localization_timeout);
	char user_id[7];
	fprintf(f, "%.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx found:\n",
				c->hwaddr[0], c->hwaddr[1], c->hwaddr[2],
				c->hwaddr[3], c->hwaddr[4], c->hwaddr[5]);

	for (liste l = *c->utilisateurs ; !est_vide(&l) ; l = *l.tl) {
		memcpy(user_id, l.hd.nom, 6);
		user_id[6] = 0; /* FIXME Il ne faut pas utiliser ça comme id des routeurs */
				/* END DEBUG */
		fprintf(f, "\t%.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx ===> %s\n",
				user_id[0], user_id[1], user_id[2],
				user_id[3], user_id[4], user_id[5],
				salle(user_id, c->utilisateurs, c->references));
	}
	fclose(f);
	uloop_timeout_set(&c->localization_timeout, LOCALIZATION_TIMEOUT);
}

hncp_constellation hncp_constellation_create(hncp h, char* ifname) {
	/* Initialization */
	hncp_constellation c;
	if (!(c = calloc(1, sizeof(*c))))
		return NULL;

	c->dncp = hncp_get_dncp(h);
	c->monitor_timeout.cb = _monitor_timeout;
	c->subscriber.tlv_change_cb = _tlv_change_callback;
	c->localization_timeout.cb = _localization_timeout;

	/* Initialize power monitoring */
	c->buffer_size = PACKET_BUFFER_SIZE;
	if (power_monitoring_init(&c->monitor_socket, &c->packet_buffer, c->buffer_size, ifname)) {
		free(c);
		return NULL;
	}
	get_hwaddr(c->hwaddr, ifname);

	/* DEBUG */
	FILE* f = fopen("/tmp/hnet-log", "a");
	fprintf(f, "Version: %s\n", version);
	fprintf(f, "My hardware address = %.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx:%.2hhx\n",
			c->hwaddr[0], c->hwaddr[1], c->hwaddr[2],
			c->hwaddr[3], c->hwaddr[4], c->hwaddr[5]);

	/* Initialize localization */
	/* FIXME changer le nom du fichier */
	c->nb_routeurs = init("/etc/config/caca_le_fichier_de_calibrage", &c->references, &c->utilisateurs, &c->routers);
	fprintf(f, "Number of routers: %d\n", c->nb_routeurs);
	fclose(f);

	/* Set the timeouts */
	uloop_timeout_set(&c->monitor_timeout, MONITOR_TIMEOUT);
	dncp_subscribe(c->dncp, &c->subscriber);
	uloop_timeout_set(&c->localization_timeout, LOCALIZATION_TIMEOUT);

	return c;
}

void hncp_constellation_destroy(hncp_constellation c) {
	/* Remove the timeouts */
	uloop_timeout_cancel(&c->monitor_timeout);
	dncp_unsubscribe(c->dncp, &c->subscriber);

	/* Uninitialize power monitoring */
	power_monitoring_stop(c->monitor_socket, c->packet_buffer);
	/* free data_list */
	struct data_list* l = c->data_list;
	while (l != NULL) {
		dncp_remove_tlv(c->dncp, l->tlv);
		struct data_list* next = l->next;
		free(l);
		l = next;
	}

	/* Uninitialize power monitoring */
	/* FIXME team algo */

	free(c);
}
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
#include "Structures.h"
#include "Calcul_distance_Rn.h"

#include <libubox/blobmsg_json.h>
#include "platform.h"

#define PACKET_BUFFER_SIZE 1000
#define TIME_LIMIT_SEEN 10 // Temps (en nombre d’appel à une publication des TLV) au dela duquel on vire un utilisateur qui n’est pas présent.

const char version[59] = "Okapi 1";
const char calibration_file[] = "/etc/config/caca_le_fichier_de_calibrage";

typedef struct constellation_data {
	char router_id[6];
	char user_id[6];
	float power;
} cd;

struct data_list {
	struct data_list* next;
	char user_id[6]; // Le nom de l’utilisateur concerné
	dncp_tlv tlv; // La tlv qui s’occupe de lui
	int nb_packet; // Le nombre de packets qu’on a vu depuis la dernière publication
	struct sorted_power_list {
		struct sorted_power_list* next;
		float value;
	}* power_list; // La liste des puissances vues pour cet utilisateur
	int not_seen_since; // Temps depuis lequel on l’a pas vu sur le réseau
};


typedef struct hncp_constellation_struct {
	dncp dncp;
	dncp_subscriber_s subscriber;

	/* Packet monitoring */
	struct uloop_timeout monitor_timeout;
	char hwaddr[6];
	int monitor_socket;
	char* packet_buffer;
	int buffer_size;
	struct data_list* data_list;

	/* Localization */
	struct uloop_timeout localization_timeout;
	char* routers;
	int nb_routers;
	loc_list areas;
	loc_list users;

	/* Auto-calibration */
	char recorded_hwaddr[6];
	int recording_number;
	double *recorded_coords_sum;
} *hc;

static hc calibration_hc = NULL;

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
/* TODO : make this better, try to sort this */
static void update_constellation_data(hc c, char user_id[6], float power) {
	struct data_list** l = &c->data_list;
	while (*l != NULL && memcmp((*l)->user_id, user_id, 6))
		l = &(*l)->next;
	if (*l == NULL) {
		*l = calloc(1, sizeof(struct data_list));
		memcpy(&(*l)->user_id, user_id, 6);
	}
	struct data_list* d = *l;

	++d->nb_packet;
	struct sorted_power_list** pl = &d->power_list;
	while (*pl != NULL && (*pl)->value < power)
		pl = &(*pl)->next;
	struct sorted_power_list* inserted_value = malloc(sizeof(struct sorted_power_list));
	inserted_value->value = power;
	inserted_value->next = (*pl)->next;
	(*pl)->next = inserted_value;
}

/*
 * Publish all the needed TLV to update the monitoring data to homenet.
 */
static void publish_constellation_data(hc c) {
	struct constellation_data data;
	memcpy(data.router_id, c->hwaddr, 6);
	for (struct data_list** l = &c->data_list ; *l ; l = &(*l)->next) {
		struct data_list* d = *l;
		int n = d->nb_packet;
		if (n) {
			memcpy(data.user_id, d->user_id, 6);

			/* Let's find the power value to publish, and free all this.
			 * We'll take the median */
			struct sorted_power_list* pl = d->power_list;
			for (int k = 0 ; k < (n-1) / 2 ; ++k) {
				struct sorted_power_list* next = pl->next;
				free(pl);
				pl = next;
			}
			data.power = pl->value;
			for (int k = (n-1) / 2 ; k < n ; ++k) {
				struct sorted_power_list* next = pl->next;
				free(pl);
				pl = next;
			}
			d->nb_packet = 0;
			d->not_seen_since = 0;

			if (d->tlv)
				dncp_remove_tlv(c->dncp, d->tlv);
			d->tlv = dncp_add_tlv(c->dncp, HNCP_T_CONSTELLATION, &data, sizeof(data), 0);
		} else {
			/* If it's been a long time we didn't see it, remove it */
			if (++d->not_seen_since > TIME_LIMIT_SEEN) {
				if (d->tlv)
					dncp_remove_tlv(c->dncp, d->tlv);
				struct data_list* to_remove = *l;
				*l = (*l)->next;
				free(to_remove);
			}
		}
	}
}

/*
 * Callback function.
 * Read all wi-fi packets and publish TLVs containing transmission power of the
 * packets from each device the router can hear.
 */
static void _monitor_timeout(struct uloop_timeout *t) {
	hncp_constellation c = container_of(t, hncp_constellation_s, monitor_timeout);

	/* Main loop, updating the power TLVs */
	struct monitored_data_struct md;
	cd data;
	memcpy(data.router_id, c->hwaddr, 6);
	int retv;
	while ((retv = power_monitoring_next(&md, c->monitor_socket, c->packet_buffer, c->buffer_size)) != -1) {
		if (retv == 1) /* The packet didn't contain expected information */
			continue;
		update_constellation_data(c, md.hwaddr, md.power);
	}
	publish_constellation_data(c);

	uloop_timeout_set(&c->monitor_timeout, MONITOR_TIMEOUT);
}

#include <string.h>
/*
 * Callback function.
 * Do localization things... TODO To complete.
 */
static void _tlv_change_callback(dncp_subscriber s, dncp_node n __attribute__((unused)), struct tlv_attr* tlv, bool add) {
	hncp_constellation c = container_of(s, hncp_constellation_s, subscriber);
	cd* data;
	char user_id[7];

	FILE* f = fopen("/tmp/hnet-log", "a"); /* DEBUG */
	switch (tlv_id(tlv)) {
		case HNCP_T_CONSTELLATION:
			if (add) {
				data = (cd*) tlv->data;
				memcpy(user_id, data->user_id, 6);
				user_id[6] = 0; /* FIXME Il ne faut pas utiliser ça comme id des routeurs */
				/* On trouve de quel routeur il s’agit FIXME pas cool */
				int k;
				for (k = 0 ; k < c->nb_routers && memcmp(c->routers + 6*k, data->router_id, 6) ; ++k);
				if (k == c->nb_routers) {
					/* Le routeur ne fait pas parti du groude de routeurs utilisés */
					/* DEBUG */
					fprintf(f, "Unidentified router ");
					fprintf_hwaddr(f, data->router_id);
				} else {
					loc_maj_utilisateur(user_id, k, data->power, &c->users, c->nb_routers);
					/* DEBUG */
					fprintf(f, "Router n°%d", k);
				}
				fprintf(f, " saw ");
				fprintf_hwaddr(f, data->user_id);
				fprintf(f, " with power %.1f\n", data->power);
			}
			break;
		default:
			break;
	}
	fclose(f);
}

/*
 * Today, it just prints the area where the users are.
 * TODO Do better…
 */
static void _localization_timeout(struct uloop_timeout *t) {
	hncp_constellation c = container_of(t, hncp_constellation_s, localization_timeout);
	char user_id[7];

	FILE* f = fopen("/tmp/hnet-log", "a");
	fprintf_hwaddr(f, c->hwaddr);
	fprintf(f, " found:\n");

	for (loc_list l = c->users ; l != NULL ; l = l->tl) {
		memcpy(user_id, l->hd.nom, 6);
		user_id[6] = 0; /* FIXME Il ne faut pas utiliser ça comme id des routeurs */
		/* DEBUG */
		fprintf(f, "\t");
		fprintf_hwaddr(f, user_id);
		fprintf(f, " ===> %s\n", loc_salle(user_id, c->users, c->areas)->nom);
	}

	/* automatic calibration */
	/* TODO Prendre en compte le temps */
	if (c == calibration_hc && c->recorded_coords_sum) {
		loc_list l;
		for (l = c->users ; l != NULL && memcmp(c->recorded_hwaddr, l->hd.nom, 6) ; l = l->tl);
		if (l != NULL) {
			for (int i = 0 ; i < l->hd.nombre_routeurs ; ++i)
				c->recorded_coords_sum[i] += l->hd.coordonnees[i];
			++c->recording_number;
		}
	}

	uloop_timeout_set(&c->localization_timeout, LOCALIZATION_TIMEOUT);
}

static int hc_main(struct platform_rpc_method *method __attribute__((unused)), int argc, char* const argv[]) {
	/* DEBUG */
	printf("Je suis là 883\n");
	if (argc < 2) {
		printf("Error: Expected an instruction\n");
		return 69;
	}

	if (!strcmp(argv[1], "rec")) {
		if (argc >= 3) {
			char hw_addr[6];
			int read = sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
					hw_addr + 0, hw_addr + 1, hw_addr + 2,
					hw_addr + 3, hw_addr + 4, hw_addr + 5);
			if (read == 6) {

				struct blob_buf b = {NULL, NULL, 0, NULL};
				blob_buf_init(&b, 0);
				blob_put(&b, 0, hw_addr, 6);

				int r = platform_rpc_cli("rec-constellation", b.head);
				return r;
			}
		}
		printf("Error: Expected a valid hardware address as 2nd argument\n");
	}

	else if (!strcmp(argv[1], "add")) {
		if (argc >= 3) {
			struct blob_buf b = {NULL, NULL, 0, NULL};
			blob_buf_init(&b, 0);
			blob_put_string(&b, 0, argv[2]);

			int r = platform_rpc_cli("add-constellation", b.head);
			return r;
		}
		printf("Error: Expected the name of the calibrated area as 2nd argument\n");
	}

	else if (!strcmp(argv[1], "print")) {
		if (argc >= 3) {
			char hw_addr[6];
			int read = sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
					hw_addr + 0, hw_addr + 1, hw_addr + 2,
					hw_addr + 3, hw_addr + 4, hw_addr + 5);
			if (read == 6) {
				struct blob_buf b = {NULL, NULL, 0, NULL};
				blob_buf_init(&b, 0);
				blob_put(&b, 0, hw_addr, 6);
				int r = platform_rpc_cli("print-constellation", b.head);
				return r;
			}
		}
		printf("Error: Expected a valid hardware address as 2nd argument\n");
	}

	return 69;
}

static int hc_rec_cb(struct platform_rpc_method *method __attribute__((unused)), const struct blob_attr *in, struct blob_buf *b) {
	hc c = calibration_hc;
	if (c) {
		if (c->recorded_coords_sum)
			free(c->recorded_coords_sum);
		/* FIXME C’est pas normal ce +4 */
		memcpy(c->recorded_hwaddr, blob_data(in) + 4, 6);
		c->recording_number = 0;
		c->recorded_coords_sum = malloc(c->nb_routers * sizeof(double));
		for (int k = 0 ; k < c->nb_routers ; ++k)
			c->recorded_coords_sum[k] = 0;
		return 0;
	} else {
		blobmsg_add_string(b, "Error", "No calibrage structure prepared.");
		return 1;
	}
}

/* TODO Unused b, est-ce que c’est grave ? */
static int hc_add_cb(struct platform_rpc_method *method __attribute__((unused)), const struct blob_attr *in, struct blob_buf *b) {
	hc c = calibration_hc;
	if (c) {
		if (c->recorded_coords_sum) {
			FILE* cal_file = fopen(calibration_file, "a");
			/* FIXME C’est pas normal ce +4 */
			fprintf(cal_file, "%s ", 4 + blob_get_string(in));
			if (!c->recording_number) {
				blobmsg_add_string(b, "Error", "Not enough information recorded.");
				return 1;
			}
			for (int k = 0 ; k < c->nb_routers ; ++k)
				fprintf(cal_file, "%lf ", c->recorded_coords_sum[k] / c->recording_number);
			fprintf(cal_file, "\n");
			fclose(cal_file);
			free(c->recorded_coords_sum);
			c->recorded_coords_sum = NULL;
			return 0;
		} else {
			blobmsg_add_string(b, "Error", "No information recorded. Use hncp_constellation rec 'hwaddr'.");
			return 1;
		}
	} else {
		blobmsg_add_string(b, "Error", "No calibrage structure prepared.");
		return 1;
	}
}

/* TODO Unused b, est-ce que c’est grave ? */
static int hc_print_cb(struct platform_rpc_method *method __attribute__((unused)), const struct blob_attr *in, struct blob_buf *b) {
	hc c = calibration_hc;
	if (c) {
		loc_list l;
		char user_id[7];
		user_id[6] = 0;
		/* FIXME C’est pas normal ce +4 */
		memcpy(user_id, blob_data(in) + 4, 6);
		for (l = c->users ; l != NULL && memcmp(l->hd.nom, user_id, 6) ; l = l->tl);
		if (l != NULL) {
			blobmsg_add_u32(b, "nb_routers", c->nb_routers);
			struct blob_buf array = {NULL, NULL, 0, NULL};
			blob_buf_init(&array, BLOBMSG_TYPE_ARRAY);
			for (int i = 0 ; i < c->nb_routers ; ++i)
				blobmsg_add_u32(&array, NULL, (unsigned int) (-l->hd.coordonnees[i]));
			blobmsg_add_field(b, BLOBMSG_TYPE_ARRAY, "coordonnees", blobmsg_data(array.head), blobmsg_data_len(array.head));
			blob_buf_free(&array);
			blobmsg_add_string(b, "salle", loc_salle(user_id, c->users, c->areas)->nom);
			return 1;
		}
	}
	return 0;
}

#define NB_RPC_METHOD 4

static struct platform_rpc_method hncp_rpc_constellation[NB_RPC_METHOD] = {
	{.name = "constellation", .main = hc_main},
	{.name = "rec-constellation", .cb = hc_rec_cb},
	{.name = "add-constellation", .cb = hc_add_cb},
	{.name = "print-constellation", .cb = hc_print_cb},
};

void hc_register_rpc() {
	for (unsigned int k = 0 ; k < NB_RPC_METHOD ; ++k)
		platform_rpc_register(hncp_rpc_constellation + k);
}

hncp_constellation hncp_constellation_create(hncp h, char* ifname, bool calibrate) {
	/* Initialization */
	hncp_constellation c;
	if (!(c = calloc(1, sizeof(*c))))
		return NULL;

	c->dncp = hncp_get_dncp(h);
	c->monitor_timeout.cb = _monitor_timeout;
	c->localization_timeout.cb = _localization_timeout;
	c->subscriber.tlv_change_cb = _tlv_change_callback;

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
	fprintf(f, "My hardware address = ");
	fprintf_hwaddr(f, c->hwaddr);
	fprintf_hwaddr(f, "\n");

	/* Initialize localization */
	/* FIXME changer le nom du fichier */
	c->nb_routers = loc_init(calibration_file, &c->areas, &c->users, &c->routers);
	fprintf(f, "Number of routers: %d\n", c->nb_routers);
	fclose(f);

	/* Initialize interface */
	if (calibrate && !calibration_hc)
		calibration_hc = c;

	/* Set the timeouts */
	uloop_timeout_set(&c->monitor_timeout, MONITOR_TIMEOUT);
	uloop_timeout_set(&c->localization_timeout, LOCALIZATION_TIMEOUT);
	dncp_subscribe(c->dncp, &c->subscriber);

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

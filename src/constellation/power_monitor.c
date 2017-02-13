/*
 * power_monitor.c
 */

#include <stdlib.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <endian.h>
#include <stddef.h>

#include "power_monitor.h"
#include "radiotap.h"
#include <stdio.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define le16(x) (x)
# define le32(x) (x)
#else
# define le16(x) (le16toh(x))
# define le32(x) (le32toh(x))
#endif

typedef struct ieee80211_radiotap_header rh;

struct radiotap_align_size {
	int align;
	int size;
};

static const struct radiotap_align_size rtap_namespace_sizes[] = {
	[IEEE80211_RADIOTAP_TSFT] = { .align = 8, .size = 8, },
	[IEEE80211_RADIOTAP_FLAGS] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_RATE] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_CHANNEL] = { .align = 2, .size = 4, },
	[IEEE80211_RADIOTAP_FHSS] = { .align = 2, .size = 2, },
	[IEEE80211_RADIOTAP_DBM_ANTSIGNAL] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_DBM_ANTNOISE] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_LOCK_QUALITY] = { .align = 2, .size = 2, },
	[IEEE80211_RADIOTAP_TX_ATTENUATION] = { .align = 2, .size = 2, },
	[IEEE80211_RADIOTAP_DB_TX_ATTENUATION] = { .align = 2, .size = 2, },
	[IEEE80211_RADIOTAP_DBM_TX_POWER] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_ANTENNA] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_DB_ANTSIGNAL] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_DB_ANTNOISE] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_RX_FLAGS] = { .align = 2, .size = 2, },
	[IEEE80211_RADIOTAP_TX_FLAGS] = { .align = 2, .size = 2, },
	[IEEE80211_RADIOTAP_RTS_RETRIES] = { .align = 1, .size = 1, },
	[IEEE80211_RADIOTAP_DATA_RETRIES] = { .align = 1, .size = 1, },
};


/*
 * Extract the hardware address of the packet's source.
 * The packet must be in 'buffer', with length in 'packet_len'.
 * Return a pointer to the hardware address in the buffer, or NULL if there
 * wasn't the required address.
 */
static char* get_source_hwaddr(char* buffer, size_t packet_len) {
	/* The structure of the ieee80211 frame's header is defined by the flags in
	 * the frame control. */
	uint16_t frame_control = le16(*((uint16_t*) buffer));
	switch (frame_control & 0x000C) /* Frame type */ {

		case 0x0000: /* Management frame */
			return (packet_len < 24) ? NULL : buffer + 10; /* 2nd address */

		case 0x0004: /* Control frame */
			return NULL;

		case 0x0008: /* Data frame */
			/* Check the Distribution System (DS) */
			switch (frame_control & 0x0300) {
				case 0x0000: /* Same BSS */
				case 0x0100: /* To DS */
					return (packet_len < 24) ? NULL : buffer + 10; /* 2nd address */
				case 0x0200: /* From DS */
					return (packet_len < 24) ? NULL : buffer + 16; /* 3rd address */
				case 0x0300: /* Wireless DS */
					return (packet_len < 30) ? NULL : buffer + 24; /* 4th address */
				default:
					return NULL;
			}

		default:
			return NULL;
	}
}

/*
 * Return a pointer to the field with type 'field' in the radiotap header.
 * 'buffer' must be a pointer to the beginning of the radiotap header.
 * If the wanted field is not here, return NULL.
 */
static char* get_radiotap_field(char* buffer, enum ieee80211_radiotap_type field) {
	/* Count how many 'it_present' there is */
	uint32_t* it_present = &((rh*) buffer)->it_present;
	unsigned int c = 0;
	while (le32(it_present[c]) & (0x80000000))
		++c;
	if (c < field / 32 || (le32(it_present[field / 32]) & (1 << (field % 32))) == 0)
		/* The required field is not in the header */
		return NULL;

	int offset = ((char*) (it_present + c + 1)) - buffer;
	uint32_t present = le32(*it_present);
	uint32_t mask = 1;
	unsigned int k;
	for (k = 0 ; k < field ; ++k) {
		if (mask == 0) {
			mask = 1;
			present = le32(*it_present);
			++it_present;
		} else if (mask & present) {
			/* Respect alignment */
			int align = rtap_namespace_sizes[k].align;
			offset = ((offset + align - 1) / align) * align;
			offset += rtap_namespace_sizes[k].size;
		}
		mask <<= 1;
	}
	return buffer + offset;
}

int power_monitoring_init(int* sock, char** buffer, int buffer_size, char* ifname) {
	*buffer = malloc(buffer_size);
	unsigned int ifindex;
	struct ifreq interface;
	struct sockaddr_ll bindaddr;

	/* Get index of the interface to monitor */
	if ((ifindex = if_nametoindex(ifname)) == 0)
		return -1;

	/* Open the socket */
	if ((*sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
		return -1;

	/* Set up non-blocking monitoring */
	strcpy(interface.ifr_name, ifname);
	if (ioctl(*sock, SIOCGIFFLAGS, &interface) == -1)
		return -1;
	interface.ifr_flags |= IFF_UP;
	if (ioctl(*sock, SIOCSIFFLAGS, &interface) == -1)
		return -1;
	if (fcntl(*sock, F_SETFL, O_NONBLOCK) == -1)
		return -1;

	/* Binding the socket on the interface */
	memset(&bindaddr, 0, sizeof(struct sockaddr_ll));
	bindaddr.sll_family = AF_PACKET;
	bindaddr.sll_ifindex = ifindex;

	return bind(*sock, (struct sockaddr*) &bindaddr, sizeof(struct sockaddr_ll));
}

int power_monitoring_next(struct monitored_data_struct* res, int socket,
		char* buffer, int buffer_size) {
	ssize_t packet_len = recv(socket, buffer, buffer_size, 0);
	if (packet_len == -1) {
		return -1;
	}

	/* Check if the packet is formated as expected */
	if ((size_t) packet_len < sizeof(rh))
		return 1;
	size_t rh_size = le16(((rh*) buffer)->it_len);
	if ((size_t) packet_len < rh_size + 30) /* 30 = minimum size of packet */
		return 1;

	/* Get hardware address of the sender */
	char* hwaddr = get_source_hwaddr(buffer + rh_size, packet_len - rh_size);
	if (hwaddr == NULL)
		return 1;

	/* Get transmission power in dBm */
	char* power = get_radiotap_field(buffer, IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
	if (power == NULL)
		return 1;

	memcpy(res->hwaddr, hwaddr, 6);
	res->power = (float) *power;

	return 0;
}

void power_monitoring_stop(int socket, char* buffer) {
	free(buffer);
	close(socket);
}

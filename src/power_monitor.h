/*
 * power_monitor.h
 */

#pragma once


struct monitored_data_struct {
	// TODO Use the time too?
	char hwaddr[6];
	float power;
};

/*
 * Initialize the monitoring of packet to get the transmition power.
 * Set '*socket' as the monitoring socket.
 * Set '*buffer' as the address of the buffer containing the last monitored
 * packet, with a maximum size of 'buffer_size'.
 * Return 0 if correctly initialize, else -1.
 */
int power_monitoring_init(int* socket, char** buffer, int buffer_size, char* ifname);

/*
 * Read the next packet from 'socket' using 'buffer' (with size 'buffer_size'),
 * and set '*res' as the wanted piece of information.
 * Return:
 * 	0 if '*res' has been properly found.
 * 	1 if '*res' couldn't be read.
 * 	-1 if theres's no packet to be read anymore.
 */
int power_monitoring_next(struct monitored_data_struct* res, int socket,
		char* buffer, int buffer_size);

/*
 * Uninitialize monitoring.
 */
void power_monitoring_stop(int socket, char* buffer);

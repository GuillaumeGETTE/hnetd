/*
 * hncp_constellation.h
 */

#pragma once

#include "hncp.h"

#define LOCALIZATION_TIMEOUT 3000
#define MONITOR_TIMEOUT 3000

typedef struct hncp_constellation_struct hncp_constellation_s, *hncp_constellation;

hncp_constellation hncp_constellation_create(hncp h, char* ifname);
void hncp_constellation_destroy(hncp_constellation c);

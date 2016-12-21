/*
 * $Id: hncp_constellation.h $
 */

#pragma once

#include "hncp.h"

#define LOCALIZATION_TIMEOUT 1000
#define MONITOR_TIMEOUT 200

typedef struct hncp_constellation_struct hncp_constellation_s, *hncp_constellation;

typedef struct {
} hncp_constellation_params_s, *hncp_constellation_params;

hncp_constellation hncp_constellation_create(hncp h/*, hncp_constellation_params p*/);

void hncp_constellation_destroy(hncp_constellation c);

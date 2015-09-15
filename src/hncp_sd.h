/*
 * $Id: hncp_sd.h $
 *
 * Author: Markus Stenberg <markus stenberg@iki.fi>
 *
 * Copyright (c) 2014-2015 cisco Systems, Inc.
 *
 * Created:       Tue Jan 14 20:09:23 2014 mstenber
 * Last modified: Tue Sep 15 11:01:26 2015 mstenber
 * Edit time:     10 min
 *
 */

#pragma once

#include "dncp.h"
#include "hncp_link.h"
#include "hncp.h"

typedef struct hncp_sd_struct hncp_sd_s, *hncp_sd;

/* These are the parameters SD code uses. The whole structure's memory
 * is owned by the external party, and is assumed to be valid from
 * sd_create to sd_destroy. */
typedef struct hncp_sd_params_struct
{
  /* Which script is used to prod at dnsmasq (required for SD) */
  const char *dnsmasq_script;

  /* And where to store the dnsmasq.conf (required for SD) */
  const char *dnsmasq_bonus_file;

  /* Which script is used to prod at ohybridproxy (required for SD) */
  const char *ohp_script;

  /* DDZ changed script - helpful with e.g. zonestitcher; it is called
  * with the locally configured domain as the first argument, and then
  * each browse zone FQDN as separate arguments. */
  const char *ddz_script;

  /* Which script is used to prod at minimalist-pcproxy (optional) */
  const char *pcp_script;

  /* Router name (if desired, optional) */
  const char *router_name;

  /* Domain name (if desired, optional, copied from others if set there) */
  const char *domain_name;
} hncp_sd_params_s, *hncp_sd_params;

hncp_sd hncp_sd_create(hncp h, hncp_sd_params p, struct hncp_link *l);

void hncp_sd_dump_link_fqdn(hncp_sd sd, dncp_ep ep, const char *ifname,
                            char *buf, size_t buf_len);

void hncp_sd_destroy(hncp_sd sd);

bool hncp_sd_busy(hncp_sd sd);

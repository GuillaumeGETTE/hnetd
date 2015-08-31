/*
 * $Id: dncp_proto.h $
 *
 * Author: Markus Stenberg <mstenber@cisco.com>
 *
 * Copyright (c) 2013-2015 cisco Systems, Inc.
 *
 * Created:       Wed Nov 27 18:17:46 2013 mstenber
 * Last modified: Mon Aug 31 13:04:23 2015 mstenber
 * Edit time:     127 min
 *
 */

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>

/******************************************************************* TLV T's */

enum {
  /* Request TLVs (not to be really stored anywhere) */
  DNCP_T_REQ_NET_STATE = 1, /* empty */
  DNCP_T_REQ_NODE_STATE = 2, /* = just normal hash */

  DNCP_T_NODE_ENDPOINT = 3,

  DNCP_T_NET_STATE = 4, /* = just normal hash, accumulated from node states so sensible to send later */
  DNCP_T_NODE_STATE = 5,
  /* was: DNCP_T_CUSTOM = 6, */
  /* was: DNCP_T_FRAGMENT_COUNT = 7 */
  DNCP_T_PEER = 8,
  DNCP_T_KEEPALIVE_INTERVAL = 9,
  DNCP_T_TRUST_VERDICT = 10
};

#define TLV_SIZE sizeof(struct tlv_attr)

#define DNCP_SHA256_LEN 32

typedef struct __packed {
  unsigned char buf[DNCP_SHA256_LEN];
} dncp_sha256_s, *dncp_sha256;

/* DNCP_T_REQ_NET_STATE has no content */

/* DNCP_T_REQ_NODE_STATE has only (node identifier) hash */

typedef uint32_t ep_id_t;

/* DNCP_T_NODE_ENDPOINT */
typedef struct __packed {
  /* dncp_node_id_s node_id; variable length, encoded here */
  ep_id_t ep_id;
} dncp_t_ep_id_s, *dncp_t_ep_id;

/* DNCP_T_NET_STATE has only (network state) hash */

/* DNCP_T_NODE_STATE */
typedef struct __packed {
  /* dncp_node_id_s node_id; variable length, encoded here */
  uint32_t update_number;
  uint32_t ms_since_origination;
  /* + hash + + optional node data after this */
} dncp_t_node_state_s, *dncp_t_node_state;

/* DNCP_T_CUSTOM custom data, with H-64 of URI at start to identify type TBD */

/* DNCP_T_PEER */
typedef struct __packed {
  /* dncp_node_id_s node_id; variable length, encoded here */
  uint32_t peer_ep_id;
  uint32_t ep_id;
} dncp_t_peer_s, *dncp_t_peer;

/* DNCP_T_KEEPALIVE_INTERVAL */
typedef struct __packed {
  uint32_t ep_id;
  uint32_t interval_in_ms;
} dncp_t_keepalive_interval_s, *dncp_t_keepalive_interval;

typedef enum {
  DNCP_VERDICT_NONE = -1, /* internal, should not be stored */
  DNCP_VERDICT_NEUTRAL = 0,
  DNCP_VERDICT_CACHED_POSITIVE = 1,
  DNCP_VERDICT_CACHED_NEGATIVE = 2,
  DNCP_VERDICT_CONFIGURED_POSITIVE = 3,
  DNCP_VERDICT_CONFIGURED_NEGATIVE = 4,
  NUM_DNCP_VERDICT = 5
} dncp_trust_verdict;

#define DNCP_T_TRUST_VERDICT_CNAME_LEN 64

/* DNCP_T_TRUST_VERDICT */
typedef struct __packed {
  uint8_t verdict;
  uint8_t reserved[3];
  dncp_sha256_s sha256_hash;
  char cname[];
} dncp_t_trust_verdict_s, *dncp_t_trust_verdict;

/*
 * $Id: test_hcp.c $
 *
 * Author: Markus Stenberg <mstenber@cisco.com>
 *
 * Copyright (c) 2013 cisco Systems, Inc.
 *
 * Created:       Thu Nov 21 13:26:21 2013 mstenber
 * Last modified: Thu Nov 28 11:14:30 2013 mstenber
 * Edit time:     44 min
 *
 */

#include "hcp.h"
#include "sput.h"

void tlv_iter(void)
{
  struct tlv_buf tb;
  struct tlv_attr *a, *a1, *a2, *a3;
  int c;
  unsigned int rem;
  void *tmp;

  /* Initialize test structure. */
  memset(&tb, 0, sizeof(tb));
  tlv_buf_init(&tb, 0);
  a1 = tlv_new(&tb, 1, 0);
  a2 = tlv_new(&tb, 2, 1);
  a3 = tlv_new(&tb, 3, 4);
  sput_fail_unless(a1 && a2 && a3, "a1-a3 create");

  /* Make sure iteration is sane. */
  c = 0;
  tlv_for_each_attr(a, tb.head, rem)
    c++;
  sput_fail_unless(c == 3, "right iter result 1");

  /* remove 3 bytes -> a3 header complete but not body. */
  tlv_init(tb.head, 0, tlv_raw_len(tb.head) - 3);
  c = 0;
  tlv_for_each_attr(a, tb.head, rem)
    c++;
  sput_fail_unless(c == 2, "right iter result 2");

  /* remove 2 bytes -> a3 header not complete (no body). */
  tlv_init(tb.head, 0, tlv_raw_len(tb.head) - 2);
  c = 0;
  tmp = malloc(tlv_raw_len(tb.head));
  memcpy(tmp, tb.head, tlv_raw_len(tb.head));
  tlv_for_each_attr(a, tmp, rem)
    c++;
  sput_fail_unless(c == 2, "right iter result 3");
  free(tmp);

  /* Free structures. */
  tlv_buf_free(&tb);
}

void hcp_ext(void)
{
  hcp o = hcp_create();
  hcp_node n;
  bool r;
  struct tlv_buf tb;
  struct tlv_attr *t, *t_data;

  sput_fail_if(!o, "create works");
  n = hcp_get_first_node(o);
  sput_fail_unless(n, "first node exists");

  sput_fail_unless(hcp_node_is_self(n), "self node");

  memset(&tb, 0, sizeof(tb));
  tlv_buf_init(&tb, 0);
  t_data = tlv_put(&tb, 123, NULL, 0);

  /* Put the 123 type length = 0 TLV as TLV to hcp. */
  r = hcp_add_tlv(o, t_data);
  sput_fail_unless(r, "hcp_add_tlv ok (should work)");

  hcp_node_get_tlvs(n, &t);
  sput_fail_unless(tlv_attr_equal(t, tb.head), "tlvs consistent");

  /* Should be able to enable it on a link. */
  r = hcp_set_link_enabled(o, "eth0", true);
  sput_fail_unless(r, "hcp_set_link_enabled eth0");

  r = hcp_set_link_enabled(o, "eth1", true);
  sput_fail_unless(r, "hcp_set_link_enabled eth1");

  r = hcp_set_link_enabled(o, "eth1", true);
  sput_fail_unless(!r, "hcp_set_link_enabled eth1 (2nd true)");

  hcp_node_get_tlvs(n, &t);
  sput_fail_unless(tlv_attr_equal(t, tb.head), "tlvs should be same");

  r = hcp_set_link_enabled(o, "eth1", false);
  sput_fail_unless(r, "hcp_set_link_enabled eth1 (false)");

  r = hcp_set_link_enabled(o, "eth1", false);
  sput_fail_unless(!r, "hcp_set_link_enabled eth1 (2nd false)");

  hcp_node_get_tlvs(n, &t);
  sput_fail_unless(tlv_attr_equal(t, tb.head), "tlvs should be same");

  /* Make sure run doesn't blow things up */
  hcp_run(o);

  /* Similarly, poll should also be nop (socket should be non-blocking). */
  hcp_poll(o);

  r = hcp_remove_tlv(o, t_data);
  sput_fail_unless(r, "hcp_remove_tlv should work");

  r = hcp_remove_tlv(o, t_data);
  sput_fail_unless(!r, "hcp_remove_tlv should not work");

  n = hcp_node_get_next(n);
  sput_fail_unless(!n, "second node should not exist");

  hcp_destroy(o);

  tlv_buf_free(&tb);
}

#include "hcp_i.h"

void hcp_int(void)
{
  /* If we want to do bit more whitebox unit testing of the whole hcp,
   * do it here. */
  hcp_s s;
  hcp o = &s;
  unsigned char hwbuf[] = "foo";
  hcp_node n;
  hcp_link l;

  memset(&s, 0, sizeof(s));
  hcp_init(o, hwbuf, strlen((char *)hwbuf));

  /* Make sure we can add nodes if we feel like it. */
  unsigned char buf[HCP_HASH_LEN];
  hcp_hash("bar", 3, buf);
  n = hcp_find_node_by_hash(o, buf, false);
  sput_fail_unless(!n, "hcp_find_node_by_hash w/ create=false => none");
  n = hcp_find_node_by_hash(o, buf, true);
  sput_fail_unless(n, "hcp_find_node_by_hash w/ create=false => !none");
  sput_fail_unless(hcp_find_node_by_hash(o, buf, false), "should exist");
  sput_fail_unless(hcp_find_node_by_hash(o, buf, false) == n, "still same");

  /* Similarly, links */
  const char *ifn = "foo";
  l = hcp_find_link(o, ifn, false);
  sput_fail_unless(!l, "hcp_find_link w/ create=false => none");
  l = hcp_find_link(o, ifn, true);
  sput_fail_unless(l, "hcp_find_link w/ create=false => !none");
  sput_fail_unless(hcp_find_link(o, ifn, false) == l, "still same");

  hcp_uninit(o);
}

int main(__unused int argc, __unused char **argv)
{
  sput_start_testing();
  sput_enter_suite("hcp"); /* optional */
  sput_run_test(tlv_iter);
  sput_run_test(hcp_ext);
  sput_run_test(hcp_int);
  sput_leave_suite(); /* optional */
  sput_finish_testing();
  return sput_get_return_value();
}

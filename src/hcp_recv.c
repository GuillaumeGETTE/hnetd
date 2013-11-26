/*
 * $Id: hcp_recv.c $
 *
 * Author: Markus Stenberg <mstenber@cisco.com>
 *
 * Copyright (c) 2013 cisco Systems, Inc.
 *
 * Created:       Tue Nov 26 08:34:59 2013 mstenber
 * Last modified: Tue Nov 26 08:36:18 2013 mstenber
 * Edit time:     2 min
 *
 */

#include "hcp_i.h"

/*
 * This module contains the logic to handle reception of traffic from
 * single- or multicast sources. The actual low-level IO is performed
 * in hcp_io.
 */


void hcp_poll(hcp o)
{
  unsigned char buf[HCP_MAXIMUM_PAYLOAD_SIZE];
  ssize_t read;
  char srcif[IFNAMSIZ];
  struct in6_addr src;

  while ((read = hcp_io_recvfrom(o, buf, sizeof(buf), srcif, &src)) > 0)
    {

    }
}
// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pcap-int.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pcap_fuchsia {
  struct pcap_stat stat;

  // Used to force the read loop's poll fn to return.
  int poll_breakloop_fd;

  // The index of the bound-to interface.
  int ifindex;

  // The timeout for the call to poll.
  int poll_timeout;
};

// Wraps libpcap's PCAP_CREATE_COMMON macro, which cannot be used in C++.
//
// PCAP_CREATE_COMMON expands to an expression containing `sizeof` and
// `offsetof` expressions which contain a struct type definition. C++
// specifically forbids such expressions; this function is implemented in C as a
// means of working around this restriction.
pcap_t *pcap_create_common_fuchsia(char *ebuf);

#ifdef __cplusplus
}  // extern "C"
#endif

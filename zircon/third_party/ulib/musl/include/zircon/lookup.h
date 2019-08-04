// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_LOOKUP_H_
#define SYSROOT_ZIRCON_LOOKUP_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

struct address {
  int family;
  unsigned scopeid;
  uint8_t addr[16];
  int sortkey;
};

/* The limit of 48 results is a non-sharp bound on the number of addresses
 * that can fit in one 512-byte DNS packet full of v4 results and a second
 * packet full of v6 results. Due to headers, the actual limit is lower. */
#define MAXADDRS 48

// This function is used by musl to perform an actual DNS lookup - it takes
// a name and address family, sends a DNS query, and fills out the addresses
// and canonical name with the response.
int _getaddrinfo_from_dns(struct address buf[MAXADDRS], char canon[256], const char* name,
                          int family);

__END_CDECLS

#endif  // SYSROOT_ZIRCON_LOOKUP_H_

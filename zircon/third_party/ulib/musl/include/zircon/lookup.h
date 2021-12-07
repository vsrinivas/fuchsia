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

// Bound the number of addresses we can handle in a DNS response at the maximum
// the server will send, fuchsia.net/MAX_LOOKUP_IPS.
#define MAXADDRS 256

// This function is used by musl to perform an actual DNS lookup - it takes
// a name and address family, sends a DNS query, and fills out the addresses
// and canonical name with the response.
int _getaddrinfo_from_dns(struct address buf[MAXADDRS], char canon[256], const char* name,
                          int family);

__END_CDECLS

#endif  // SYSROOT_ZIRCON_LOOKUP_H_

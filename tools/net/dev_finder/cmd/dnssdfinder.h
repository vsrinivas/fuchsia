// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <dns_sd.h>
#else
typedef struct _DNSServiceRef_t *DNSServiceRef;
#endif

int dnsBrowse(char *domain, DNSServiceRef *ref, void *ctx);
int dnsResolve(char *hostname, DNSServiceRef *ref, bool ipv4, bool ipv6, void *ctx);
int dnsProcessResults(DNSServiceRef ref);
int dnsPollDaemon(DNSServiceRef ref, int timeout_milliseconds);
int dnsAllocate(DNSServiceRef *ref);
void dnsDeallocate(DNSServiceRef ref);

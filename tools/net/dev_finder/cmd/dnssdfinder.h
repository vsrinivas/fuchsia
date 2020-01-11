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

// dnsBrowse attempts to find all Fuchsia targets running on the network.
//
// Use `dnsPollDaemon` in conjunction with `dnsProcessResults` to get results
// for this function.
//
// When running `dnsProcessResults` this will lead to one or more callbacks to
// `browseCallbackGoFunc` in dnssdfinder.go (and exported to _cgo_export.h).
int dnsBrowse(char *domain, DNSServiceRef *ref, void *ctx);

// dnsResolve takes a Fuchsia target service and resolves the IP address.
//
// Attempts to resolve either the IPv4 address, the IPv6 address, or both.
//
// Use `dnsPollDaemon` in conjunction with `dnsProcessResults` to get results
// for this function.
//
// When running `dnsProcessResults` this will lead to one or more callbacks to
// `resolveCallbackGoFunc` in dnssdfinder.go (and exported to _cgo_export.h).
//
// This should not be confused with DNSServiceResolve which only returns the
// hostname of a given service. It is, at the time of writing this comment
// (January 9th, 2020), possible to assume that the fuchsia domain name and
// hostname are identical, so this skips the DNSServiceResolve step.
int dnsResolve(char *hostname, DNSServiceRef *ref, bool ipv4, bool ipv6, void *ctx);

// dnsProcessResults takes a DNSServiceRef |ref| which the client has verified
// to already have results available via the `dnsPollDaemon` function (see
// below). It is strongly encouraged to use `dnsPollDaemon` in conjunction with
// `dnsProcessResults` for the following reasons.
//
// If there are no results `dnsProcessResults` will block indefinitely, making
// it difficult to reason about the lifetime of a query within go (given this a
// callback-based command), as queries are tied to a timeout there can be a race
// wherein a callback is fired after or even during cleanup, causing the program
// to crash. It may be possible to work around these issues wherein one thread
// wishes to cleanup while another is in the middle of reporting results, but it
// is generally simpler to reason about queries done with `dnsPollDaemon`.
int dnsProcessResults(DNSServiceRef ref);

// dnsPollDaemon checks on the DNSServiceRef's underlying file descriptor for
// whether or not there are available query results.
int dnsPollDaemon(DNSServiceRef ref, int timeout_milliseconds);

// dnsAllocate creates a DNSServiceRef that is connected to the mdnsResponder
// daemon.
int dnsAllocate(DNSServiceRef *ref);

// dnsDeallocate destroys a reference to the DNSServiceRef originally created in
// `dnsAllocate`.
void dnsDeallocate(DNSServiceRef ref);

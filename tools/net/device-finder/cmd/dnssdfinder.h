// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <stdbool.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <dns_sd.h>
#else
typedef struct _DNSServiceRef_t *DNSServiceRef;
// These are all redefinitions from dns_sd.h, here to ensure tests still work
// when built on non-apple systems.
enum {
    kDNSServiceErr_NoError                   = 0,
    kDNSServiceErr_Unknown                   = -65537,
    kDNSServiceErr_NoSuchName                = -65538,
    kDNSServiceErr_NoMemory                  = -65539,
    kDNSServiceErr_BadParam                  = -65540,
    kDNSServiceErr_BadReference              = -65541,
    kDNSServiceErr_BadState                  = -65542,
    kDNSServiceErr_BadFlags                  = -65543,
    kDNSServiceErr_Unsupported               = -65544,
    kDNSServiceErr_NotInitialized            = -65545,
    kDNSServiceErr_AlreadyRegistered         = -65547,
    kDNSServiceErr_NameConflict              = -65548,
    kDNSServiceErr_Invalid                   = -65549,
    kDNSServiceErr_Firewall                  = -65550,
    kDNSServiceErr_Incompatible              = -65551,
    kDNSServiceErr_BadInterfaceIndex         = -65552,
    kDNSServiceErr_Refused                   = -65553,
    kDNSServiceErr_NoSuchRecord              = -65554,
    kDNSServiceErr_NoAuth                    = -65555,
    kDNSServiceErr_NoSuchKey                 = -65556,
    kDNSServiceErr_NATTraversal              = -65557,
    kDNSServiceErr_DoubleNAT                 = -65558,
    kDNSServiceErr_BadTime                   = -65559,
    kDNSServiceErr_BadSig                    = -65560,
    kDNSServiceErr_BadKey                    = -65561,
    kDNSServiceErr_Transient                 = -65562,
    kDNSServiceErr_ServiceNotRunning         = -65563,
    kDNSServiceErr_NATPortMappingUnsupported = -65564,
    kDNSServiceErr_NATPortMappingDisabled    = -65565,
    kDNSServiceErr_NoRouter                  = -65566,
    kDNSServiceErr_PollingMode               = -65567,
    kDNSServiceErr_Timeout                   = -65568
};
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
// whether or not there are available query results. If an err is encountered,
// `err_out` will be set to `errno` (if it is not NULL).
int dnsPollDaemon(DNSServiceRef ref, int timeout_milliseconds, int *err_out);

// dnsAllocate creates a DNSServiceRef that is connected to the mdnsResponder
// daemon.
int dnsAllocate(DNSServiceRef *ref);

// dnsDeallocate destroys a reference to the DNSServiceRef originally created in
// `dnsAllocate`.
void dnsDeallocate(DNSServiceRef ref);

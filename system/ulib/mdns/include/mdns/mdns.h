// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

/**
 * This is a library that implements a subset of mDNS (RFC 6762) to support the
 * Fuchsia boot process. The structure of an mDNS packet is largely borrowed
 * from the DNS package structure (RFC 1035).
 **/

#ifdef __cplusplus
extern "C" {
#endif

// The default IPv4 multicast address.
#define MDNS_IPV4_ADDRESS "224.0.0.251";

// The default IPv6 multicast address.
#define MDNS_IPV6_ADDRESS "ff02::fb";

// The maxinum number of characters in a domain name.
#define MAX_DOMAIN_LENGTH 253
#define MAX_DOMAIN_LABEL 63

// The number of bytes in a DNS message header.
#define MDNS_HEADER_SIZE 12

// A DNS message header.
//
// id is a unique identifier used to match queries with responses.
//
// flags is a set of flags represented as a collection of sub-fields.
// The format of the flags section is as follows:
//
// Bit no. | Meaning
// -------------------
// 1        0 = query
//          1 = reply
//
// 2-5      0000 = standard query
//          0100 = inverse
//          0010 & 0001 not used.
//
// 6        0 = non-authoritative DNS answer
//          1 = authoritative DNS answer
//
// 7        0 = message not truncated
//          1 = message truncated
//
// 8        0 = non-recursive query
//          1 = recursive query
//
// 9        0 = recursion not available
//          1 = recursion available
//
// 10 & 12  reserved
//
// 11       0 = answer/authority portion was not authenticated by the
//              server
//          1 = answer/authority portion was authenticated by the
//              server
//
// 13 - 16  0000 = no error
//          0100 = Format error in query
//          0010 = Server failure
//          0001 = Name does not exist
typedef struct mdns_header_t {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count; // Question count
    uint16_t an_count; // Answer count
    uint16_t ns_count; // Name server count
    uint16_t ar_count; // Additional record count
} mdns_header;

// An mDNS question.
typedef struct mdns_question_t {
    char domain[MAX_DOMAIN_LENGTH];
    uint16_t qtype;
    uint16_t qclass;
    struct mdns_question_t* next;
} mdns_question;

// An mDNS resource record
typedef struct mdns_rr_t {
    char* name;
    uint16_t type;
    uint16_t clazz;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t* rdata;
    struct mdns_rr_t* next;
} mdns_rr;

// An mDNS query packet
typedef struct mdns_message_t {
    mdns_header header;
    mdns_question* questions;
    mdns_rr* answers;
    mdns_rr* authorities;
    mdns_rr* additionals;
} mdns_message;

// Zeroes the values contained in the given mdns_message.
void mdns_init_message(mdns_message* message);

#ifdef __cplusplus
}  // extern "C"
#endif


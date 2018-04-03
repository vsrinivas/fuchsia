// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>

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
#define MAX_DOMAIN_LENGTH 255
#define MAX_DOMAIN_LABEL 63

// The number of bytes in a DNS message header.
#define MDNS_HEADER_SIZE 12

/**
 * Resource record types.
 *
 * A record type communicates a given record's intended-use.
 *
 * This list is incomplete, since all record types are not necessarily useful to
 * Zircon. Add to this list as needed.
 **/

// A records contain 32-bit IPv4 host addresses. They are used to map hostnames
// to IP addresses of a given host.
#define RR_TYPE_A 0x01

// AAAA Records contain 128-bit IPv6 host addresses. Used to map hostnames IP
// addresses of a given host.
#define RR_TYPE_AAAA 0x1C

/**
 * Resource record classes.
 *
 * These are DNS classes, which are individual namespaces that map to various
 * DNS Zones.
 *
 * This list is incomplete, since all record classes are not necessarily useful
 * to Zircon. Add to this list as needed.
 **/

// IN is a class for common DNS records involving internet hostnames, servers
// or IP addresses.
#define RR_CLASS_IN 0x0001

// A DNS message header.
//
// The message header should not be modified by hand.  When creating a message
// for sending, invalid changes, such as specifying a qd_count that differs from
// the actual number of questions in a message, are replaced with their correct
// values.  When reading a received message, modifying the header can obviously
// lead to confusing inconsistencies between the header information and its
// corresponding message.
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
    uint16_t ns_count; // Authoritative name server count
    uint16_t ar_count; // Additional record count
} mdns_header;

// An mDNS question.
typedef struct mdns_question_t {
    char domain[MAX_DOMAIN_LENGTH + 1];
    uint16_t qtype;
    uint16_t qclass;
    struct mdns_question_t* next;
} mdns_question;

// An mDNS resource record
typedef struct mdns_rr_t {
    char name[MAX_DOMAIN_LENGTH + 1];
    uint16_t type;
    uint16_t clazz;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t* rdata;
    struct mdns_rr_t* next;
} mdns_rr;

// An mDNS query packet.
typedef struct mdns_message_t {
    mdns_header header;
    mdns_question* questions;
    mdns_rr* answers;
    mdns_rr* authorities;
    mdns_rr* additionals;
} mdns_message;

// Reads an mdns_message.
//
// buf_len is the number of bytes in buf. Data is unmarshalled into the given
// mdns_message container which is zeroed before writing via mdns_init_message.
// The message is zeroed even if unmarshalling fails.
//
// If buf_len is less than MDNS_HEADER_SIZE or the complete message is longer
// than buf_len bytes (data is missing), -1 is returned and errno is set to
// EBADMSG.
//
// Returns the number of bytes read from buf.
int mdns_unmarshal(const void* buf,
                   const size_t buf_len,
                   mdns_message* container);

// Zeroes the values contained in the given mdns_message.
void mdns_init_message(mdns_message* message);

// Appends a question to a message.
//
// Assumes mdns_init_message(&message) has been called.
//
// If domain is longer than MAX_DOMAIN_LENGTH bytes, a negative value is
// returned and errno is set to ENAMETOOLONG. The message header's question
// count is incremented by one.  This count is guaranteed to be in-sync with
// the actual number of questions in the message.  If memory cannot be
// allocated for the question, a negative value is returned and errno is set to
// ENOMEM.
//
// Returns 0 on success.
int mdns_add_question(mdns_message* message,
                      const char* domain,
                      uint16_t qtype,
                      uint16_t qclass);

// Appends an answer resource record to a message.
//
// Assumes mdns_init_message(&message) has been called.
//
// name is the domain name associated with this resource record, and is expected
// to be a null-terminated string. Type must be one of the RR_TYPE* constants
// and specifies the type of rdata. clazz must be one the RR_CLASS* constants
// and specifies the class of rdata. If type or clazz is invalid, a negative
// value is returned and errno is set to EINVAL. rdata and rdlenth are the data
// and its length, respectively. ttl specifies the time interval in seconds that
// the record may be cached before it should be discarded. A ttl of zero means
// that the record should not be cached.
//
// If memory cannot be allocated for the resource record, a negative value is
// returned and errno is set to ENOMEM.
//
// Returns 0 on success.
int mdns_add_answer(mdns_message*,
                    char* name,
                    uint16_t type,
                    uint16_t clazz,
                    uint8_t* rdata,
                    uint16_t rdlength,
                    uint32_t ttl);

// Appends an authority resource record to a message.
//
// See mdns_add_answer for documentation.
int mdns_add_authority(mdns_message*,
                       char* name,
                       uint16_t type,
                       uint16_t clazz,
                       uint8_t* rdata,
                       uint16_t rdlength,
                       uint32_t ttl);

// Appends an additional info resource record to a message.
//
// See mdns_add_answer for documentation.
int mdns_add_additional(mdns_message*,
                        char* name,
                        uint16_t type,
                        uint16_t clazz,
                        uint8_t* rdata,
                        uint16_t rdlength,
                        uint32_t ttl);

// Zeroes all pointers and values associated with the given message.
//
// Clients should free(message) after calling mdns_free_message(message) if the
// message was allocated on the heap.
void mdns_free_message(mdns_message* message);

#ifdef __cplusplus
} // extern "C"
#endif

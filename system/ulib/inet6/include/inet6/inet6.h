// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct mac_addr mac_addr_t;
typedef union ip6_addr ip6_addr_t;
typedef struct ip6_hdr ip6_hdr_t;
typedef struct udp_hdr udp_hdr_t;
typedef struct icmp6_hdr icmp6_hdr_t;
typedef struct ndp_n_hdr ndp_n_hdr_t;

#define ETH_ADDR_LEN 6
#define ETH_HDR_LEN 14
#define ETH_MTU 1514

#define IP6_ADDR_LEN 16
#define IP6_U32_LEN 4
#define IP6_U64_LEN 2

#define IP6_HDR_LEN 40

#define IP6_MIN_MTU 1280

#define UDP_HDR_LEN 8

struct mac_addr {
    uint8_t x[ETH_ADDR_LEN];
} __attribute__((packed));

union ip6_addr {
    uint8_t u8[IP6_ADDR_LEN];
    uint32_t u32[IP6_U32_LEN];
    uint64_t u64[IP6_U64_LEN];
} __attribute__((packed));

extern const ip6_addr_t ip6_ll_all_nodes;
extern const ip6_addr_t ip6_ll_all_routers;

static inline bool ip6_addr_eq(const ip6_addr_t* a, const ip6_addr_t* b) {
    return ((a->u64[0] == b->u64[0]) && (a->u64[1] == b->u64[1]));
}

#define ETH_IP4 0x0800
#define ETH_ARP 0x0806
#define ETH_IP6 0x86DD

#define HDR_HNH_OPT 0
#define HDR_TCP 6
#define HDR_UDP 17
#define HDR_ROUTING 43
#define HDR_FRAGMENT 44
#define HDR_ICMP6 58
#define HDR_NONE 59
#define HDR_DST_OPT 60

struct ip6_hdr {
    uint32_t ver_tc_flow;
    uint16_t length;
    uint8_t next_header;
    uint8_t hop_limit;
    ip6_addr_t src;
    ip6_addr_t dst;
} __attribute__((packed));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

#define ICMP6_DEST_UNREACHABLE 1
#define ICMP6_PACKET_TOO_BIG 2
#define ICMP6_TIME_EXCEEDED 3
#define ICMP6_PARAMETER_PROBLEM 4

#define ICMP6_ECHO_REQUEST 128
#define ICMP6_ECHO_REPLY 129

#define ICMP6_NDP_N_SOLICIT 135
#define ICMP6_NDP_N_ADVERTISE 136

struct icmp6_hdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
} __attribute__((packed));

struct ndp_n_hdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint32_t flags;
    uint8_t target[IP6_ADDR_LEN];
    uint8_t options[0];
} __attribute__((packed));

#define NDP_N_SRC_LL_ADDR 1
#define NDP_N_TGT_LL_ADDR 2
#define NDP_N_PREFIX_INFO 3
#define NDP_N_REDIRECTED_HDR 4
#define NDP_N_MTU 5

#ifndef ntohs
#define ntohs(n) _swap16(n)
#define htons(n) _swap16(n)
static inline uint16_t _swap16(uint16_t n) {
    return (n >> 8) | (n << 8);
}
#endif

#ifndef ntohl
#define ntohl(n) _swap32(n)
#define htonl(n) _swap32(n)
static inline uint32_t _swap32(uint32_t n) {
    return (n >> 24) | ((n >> 8) & 0xFF00) |
           ((n & 0xFF00) << 8) | (n << 24);
}
#endif

// Formats an IP6 address into the provided buffer (which must be
// at least IP6TOAMAX bytes in size), and returns the buffer address.
char* ip6toa(char* _out, void* ip6addr);
#define IP6TOAMAX 40

// provided by inet6.c
void ip6_init(void* macaddr);
void eth_recv(void* data, size_t len);

typedef struct eth_buffer eth_buffer_t;

// provided by interface driver
int eth_get_buffer(size_t len, void** data, eth_buffer_t** out);
void eth_put_buffer(eth_buffer_t* ethbuf);

int eth_send(eth_buffer_t* ethbuf, size_t skip, size_t len);

int eth_add_mcast_filter(const mac_addr_t* addr);

// call to transmit a UDP packet
int udp6_send(const void* data, size_t len,
              const ip6_addr_t* daddr, uint16_t dport,
              uint16_t sport);

// implement to recive UDP packets
void udp6_recv(void* data, size_t len,
               const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport);

unsigned ip6_checksum(ip6_hdr_t* ip, unsigned type, size_t length);

// NOTES
//
// This is an extremely minimal IPv6 stack, supporting just enough
// functionality to talk to link local hosts over UDP.
//
// It responds to ICMPv6 Neighbor Solicitations for its link local
// address, which is computed from the mac address provided by the
// ethernet interface driver.
//
// It responds to PINGs.
//
// It can only transmit to multicast addresses or to the address it
// last received a packet from (general usecase is to reply to a UDP
// packet from the UDP callback, which this supports)
//
// It does not currently do duplicate address detection, which is
// probably the most severe bug.
//
// It does not support any IPv6 options and will drop packets with
// options.
//
// It expects the network stack to provide transmit buffer allocation
// and free functionality.  It will allocate a single transmit buffer
// from udp6_send() or icmp6_send() to fill out and either pass to the
// network stack via eth_send() or, in the event of an error, release
// via eth_put_buffer().
//

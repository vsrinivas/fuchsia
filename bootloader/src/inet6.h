// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef struct mac_addr_t mac_addr;
typedef struct ip6_addr_t ip6_addr;
typedef struct ip6_hdr_t ip6_hdr;
typedef struct udp_hdr_t udp_hdr;
typedef struct icmp6_hdr_t icmp6_hdr;
typedef struct ndp_n_hdr_t ndp_n_hdr;

#define ETH_ADDR_LEN 6
#define ETH_HDR_LEN 14
#define ETH_MTU 1514

#define IP6_ADDR_LEN 16
#define IP6_HDR_LEN 40

#define IP6_MIN_MTU 1280

#define UDP_HDR_LEN 8

struct mac_addr_t {
    uint8_t x[ETH_ADDR_LEN];
} __attribute__((packed));

struct ip6_addr_t {
    uint8_t x[IP6_ADDR_LEN];
} __attribute__((packed));

extern const ip6_addr ip6_ll_all_nodes;
extern const ip6_addr ip6_ll_all_routers;

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

struct ip6_hdr_t {
    uint32_t ver_tc_flow;
    uint16_t length;
    uint8_t next_header;
    uint8_t hop_limit;
    uint8_t src[IP6_ADDR_LEN];
    uint8_t dst[IP6_ADDR_LEN];
} __attribute__((packed));

struct udp_hdr_t {
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

struct icmp6_hdr_t {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
} __attribute__((packed));

struct ndp_n_hdr_t {
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
mac_addr eth_addr();

// provided by interface driver
void* eth_get_buffer(size_t len);
void eth_put_buffer(void* ptr);
int eth_send(void* data, size_t len);
int eth_add_mcast_filter(const mac_addr* addr);

// call to transmit a UDP packet
int udp6_send(const void* data, size_t len,
              const ip6_addr* daddr, uint16_t dport,
              uint16_t sport);

// handle a netboot UDP packet
void netboot_recv(void* data, size_t len, const ip6_addr* saddr, uint16_t sport);

// handle a TFTP (over UDP) packet
void tftp_recv (void* data, size_t len, const ip6_addr* daddr, uint16_t dport,
                const ip6_addr* saddr, uint16_t sport);

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

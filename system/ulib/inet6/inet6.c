// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <inet6/inet6.h>
#include <zircon/misc/fnv1hash.h>
#include <zircon/syscalls.h>

#define REPORT_BAD_PACKETS 0

#if REPORT_BAD_PACKETS
#define BAD_PACKET(reason) report_bad_packet(NULL, reason)
#define BAD_PACKET_FROM(addr, reason) report_bad_packet(addr, reason)
#else
#define BAD_PACKET(reason)
#define BAD_PACKET_FROM(addr, reason)
#endif

// useful addresses
const ip6_addr_t ip6_ll_all_nodes = {
    .u8 = {0xFF, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
};
const ip6_addr_t ip6_ll_all_routers = {
    .u8 = {0xFF, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
};


// If non-zero, this setting causes us to generate our
// MAC-derived link-local IPv6 address in a way that
// is different from the spec, so we our link-local traffic
// is distinct from traffic from Fuchsia's netstack service.
#define INET6_COEXIST_WITH_NETSTACK 1

// Convert MAC Address to IPv6 Link Local Address
// aa:bb:cc:dd:ee:ff => FF80::aabb:ccFF:FEdd:eeff
// bit 2 (U/L) of the mac is inverted
void ll6addr_from_mac(ip6_addr_t* _ip, const mac_addr_t* _mac) {
    uint8_t* ip = _ip->u8;
    const uint8_t* mac = _mac->x;
    memset(ip, 0, sizeof(ip6_addr_t));
    ip[0] = 0xFE;
    ip[1] = 0x80;
    memset(ip + 2, 0, 6);
    // Flip the globally-unique bit from the MAC
    // since the sense of this is backwards in
    // IPv6 Interface Identifiers.
    ip[8] = mac[0] ^ 2;
    ip[9] = mac[1];
    ip[10] = mac[2];
#if INET6_COEXIST_WITH_NETSTACK
    ip[11] = 'M';
#else
    ip[11] = 0xFF;
#endif
    ip[12] = 0xFE;
    ip[13] = mac[3];
    ip[14] = mac[4];
    ip[15] = mac[5];
}

// Convert MAC Address to IPv6 Solicit Neighbor Multicast Address
// aa:bb:cc:dd:ee:ff -> FF02::1:FFdd:eeff
void snmaddr_from_mac(ip6_addr_t* _ip, const mac_addr_t* _mac) {
    uint8_t* ip = _ip->u8;
    const uint8_t* mac = _mac->x;
    ip[0] = 0xFF;
    ip[1] = 0x02;
    memset(ip + 2, 0, 9);
    ip[11] = 0x01;
    ip[12] = 0xFF;
    ip[13] = mac[3];
    ip[14] = mac[4];
    ip[15] = mac[5];
}

// Convert IPv6 Multicast Address to Ethernet Multicast Address
void multicast_from_ip6(mac_addr_t* _mac, const ip6_addr_t* _ip6) {
    const uint8_t* ip = _ip6->u8;
    uint8_t* mac = _mac->x;
    mac[0] = 0x33;
    mac[1] = 0x33;
    mac[2] = ip[12];
    mac[3] = ip[13];
    mac[4] = ip[14];
    mac[5] = ip[15];
}

// ip6 stack configuration
static mac_addr_t ll_mac_addr;
static ip6_addr_t ll_ip6_addr;
static mac_addr_t snm_mac_addr;
static ip6_addr_t snm_ip6_addr;

// cache for the last source addresses we've seen
#define MAC_TBL_BUCKETS 256
#define MAC_TBL_ENTRIES 5
typedef struct ip6_to_mac {
    zx_time_t last_used;  // A value of 0 indicates "unused"
    ip6_addr_t ip6;
    mac_addr_t mac;
} ip6_to_mac_t;
static ip6_to_mac_t mac_lookup_tbl[MAC_TBL_BUCKETS][MAC_TBL_ENTRIES];
static mtx_t mac_cache_lock = MTX_INIT;

// Clear all entries
static void mac_cache_init(void) {
    size_t bucket_ndx;
    size_t entry_ndx;
    mtx_lock(&mac_cache_lock);
    for (bucket_ndx = 0; bucket_ndx < MAC_TBL_BUCKETS; bucket_ndx++) {
        for (entry_ndx = 0; entry_ndx < MAC_TBL_ENTRIES; entry_ndx++) {
            mac_lookup_tbl[bucket_ndx][entry_ndx].last_used = 0;
        }
    }
    mtx_unlock(&mac_cache_lock);
}

void ip6_init(void* macaddr) {
    char tmp[IP6TOAMAX];
    mac_addr_t all;

    // Clear our ip6 -> MAC address lookup table
    mac_cache_init();

    // save our ethernet MAC and synthesize link layer addresses
    memcpy(&ll_mac_addr, macaddr, 6);
    ll6addr_from_mac(&ll_ip6_addr, &ll_mac_addr);
    snmaddr_from_mac(&snm_ip6_addr, &ll_mac_addr);
    multicast_from_ip6(&snm_mac_addr, &snm_ip6_addr);

    eth_add_mcast_filter(&snm_mac_addr);

    multicast_from_ip6(&all, &ip6_ll_all_nodes);
    eth_add_mcast_filter(&all);

    printf("macaddr: %02x:%02x:%02x:%02x:%02x:%02x\n",
           ll_mac_addr.x[0], ll_mac_addr.x[1], ll_mac_addr.x[2],
           ll_mac_addr.x[3], ll_mac_addr.x[4], ll_mac_addr.x[5]);
    printf("ip6addr: %s\n", ip6toa(tmp, &ll_ip6_addr));
    printf("snmaddr: %s\n", ip6toa(tmp, &snm_ip6_addr));
}

static uint8_t mac_cache_hash(const ip6_addr_t* ip) {
    static_assert(MAC_TBL_BUCKETS == 256, "hash algorithms must be updated");
    uint32_t hash = fnv1a32(ip, sizeof(*ip));
    return ((hash >> 8) ^ hash) & 0xff;
}

// Find the MAC corresponding to a given IP6 address
static int mac_cache_lookup(mac_addr_t* mac, const ip6_addr_t* ip) {
    int result = -1;
    uint8_t key = mac_cache_hash(ip);

    mtx_lock(&mac_cache_lock);
    for (size_t entry_ndx = 0; entry_ndx < MAC_TBL_ENTRIES; entry_ndx++) {
        ip6_to_mac_t* entry = &mac_lookup_tbl[key][entry_ndx];

        if (entry->last_used == 0) {
            // All out of entries
            break;
        }

        if (!memcmp(ip, &entry->ip6, sizeof(ip6_addr_t))) {
            // Match!
            memcpy(mac, &entry->mac, sizeof(*mac));
            result = 0;
            break;
        }
    }
    mtx_unlock(&mac_cache_lock);
    return result;
}

static int resolve_ip6(mac_addr_t* _mac, const ip6_addr_t* _ip) {
    const uint8_t* ip = _ip->u8;

    // Multicast addresses are a simple transform
    if (ip[0] == 0xFF) {
        multicast_from_ip6(_mac, _ip);
        return 0;
    }

    return mac_cache_lookup(_mac, _ip);
}

static uint16_t checksum(const void* _data, size_t len, uint16_t _sum) {
    uint32_t sum = _sum;
    const uint16_t* data = _data;
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    if (len) {
        sum += (*data & 0xFF);
    }
    while (sum > 0xFFFF) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return sum;
}

typedef struct {
    uint8_t eth[16];
    ip6_hdr_t ip6;
    uint8_t data[0];
} ip6_pkt_t;

typedef struct {
    uint8_t eth[16];
    ip6_hdr_t ip6;
    udp_hdr_t udp;
    uint8_t data[0];
} udp_pkt_t;

unsigned ip6_checksum(ip6_hdr_t* ip, unsigned type, size_t length) {
    uint16_t sum;

    // length and protocol field for pseudo-header
    sum = checksum(&ip->length, 2, htons(type));
    // src/dst for pseudo-header + payload
    sum = checksum(&ip->src, 32 + length, sum);

    // 0 is illegal, so 0xffff remains 0xffff
    if (sum != 0xffff) {
        return ~sum;
    } else {
        return sum;
    }
}

static int ip6_setup(ip6_pkt_t* p, const ip6_addr_t* daddr, size_t length, uint8_t type) {
    mac_addr_t dmac;

    if (resolve_ip6(&dmac, daddr))
        return -1;

    // ethernet header
    memcpy(p->eth + 2, &dmac, ETH_ADDR_LEN);
    memcpy(p->eth + 8, &ll_mac_addr, ETH_ADDR_LEN);
    p->eth[14] = (ETH_IP6 >> 8) & 0xFF;
    p->eth[15] = ETH_IP6 & 0xFF;

    // ip6 header
    p->ip6.ver_tc_flow = 0x60; // v=6, tc=0, flow=0
    p->ip6.length = htons(length);
    p->ip6.next_header = type;
    p->ip6.hop_limit = 255;
    p->ip6.src = ll_ip6_addr;
    p->ip6.dst = *daddr;

    return 0;
}

#define UDP6_MAX_PAYLOAD (ETH_MTU - ETH_HDR_LEN - IP6_HDR_LEN - UDP_HDR_LEN)

zx_status_t udp6_send(const void* data, size_t dlen, const ip6_addr_t* daddr, uint16_t dport,
                      uint16_t sport, bool block) {
    if (dlen > UDP6_MAX_PAYLOAD)
        return ZX_ERR_INVALID_ARGS;
    size_t length = dlen + UDP_HDR_LEN;
    udp_pkt_t* p;
    eth_buffer_t* ethbuf;
    zx_status_t status = eth_get_buffer(ETH_MTU + 2, (void**) &p, &ethbuf, block);
    if (status != ZX_OK) {
        return status;
    }
    if (ip6_setup((void*)p, daddr, length, HDR_UDP)) {
        eth_put_buffer(ethbuf);
        return ZX_ERR_INVALID_ARGS;
    }

    // udp header
    p->udp.src_port = htons(sport);
    p->udp.dst_port = htons(dport);
    p->udp.length = htons(length);
    p->udp.checksum = 0;

    memcpy(p->data, data, dlen);
    p->udp.checksum = ip6_checksum(&p->ip6, HDR_UDP, length);
    return eth_send(ethbuf, 2, ETH_HDR_LEN + IP6_HDR_LEN + length);
}

#define ICMP6_MAX_PAYLOAD (ETH_MTU - ETH_HDR_LEN - IP6_HDR_LEN)

static zx_status_t icmp6_send(const void* data, size_t length, const ip6_addr_t* daddr,
                              bool block) {
    if (length > ICMP6_MAX_PAYLOAD)
        return ZX_ERR_INVALID_ARGS;
    eth_buffer_t* ethbuf;
    ip6_pkt_t* p;
    icmp6_hdr_t* icmp;

    zx_status_t status = eth_get_buffer(ETH_MTU + 2, (void**) &p, &ethbuf, block);
    if (status != ZX_OK) {
        return status;
    }
    if (ip6_setup(p, daddr, length, HDR_ICMP6)) {
        eth_put_buffer(ethbuf);
        return ZX_ERR_INVALID_ARGS;
    }

    icmp = (void*)p->data;
    memcpy(icmp, data, length);
    icmp->checksum = ip6_checksum(&p->ip6, HDR_ICMP6, length);
    return eth_send(ethbuf, 2, ETH_HDR_LEN + IP6_HDR_LEN + length);
}

#if REPORT_BAD_PACKETS
static void report_bad_packet(ip6_addr_t* ip6_addr, const char* msg) {
    if (ip6_addr == NULL) {
        printf("inet6: dropping packet: %s\n", msg);
    } else {
        char addr_str[IP6TOAMAX];
        ip6toa(addr_str, ip6_addr);
        printf("inet6: dropping packet from %s: %s\n", addr_str, msg);
    }
}
#endif

void _udp6_recv(ip6_hdr_t* ip, void* _data, size_t len) {
    udp_hdr_t* udp = _data;
    uint16_t sum, n;

    if (unlikely(len < UDP_HDR_LEN)) {
        BAD_PACKET_FROM(&ip->src, "invalid header in UDP packet");
        return;
    }
    if (unlikely(udp->checksum == 0)) {
        BAD_PACKET_FROM(&ip->src, "missing checksum in UDP packet");
        return;
    }
    if (udp->checksum == 0xFFFF)
        udp->checksum = 0;

    sum = checksum(&ip->length, 2, htons(HDR_UDP));
    sum = checksum(&ip->src, 32 + len, sum);
    if (unlikely(sum != 0xFFFF)) {
        BAD_PACKET_FROM(&ip->src, "incorrect checksum in UDP packet");
        return;
    }

    n = ntohs(udp->length);
    if (unlikely(n < UDP_HDR_LEN)) {
        BAD_PACKET_FROM(&ip->src, "UDP length too short");
        return;
    }
    if (unlikely(n > len)) {
        BAD_PACKET_FROM(&ip->src, "UDP length too long");
        return;
    }
    len = n - UDP_HDR_LEN;

    udp6_recv((uint8_t*)_data + UDP_HDR_LEN, len,
              (void*)&ip->dst, ntohs(udp->dst_port),
              (void*)&ip->src, ntohs(udp->src_port));
}

void icmp6_recv(ip6_hdr_t* ip, void* _data, size_t len) {
    icmp6_hdr_t* icmp = _data;
    uint16_t sum;

    if (unlikely(icmp->checksum == 0)) {
        BAD_PACKET_FROM(&ip->src, "missing checksum in ICMP packet");
        return;
    }
    if (icmp->checksum == 0xFFFF)
        icmp->checksum = 0;

    sum = checksum(&ip->length, 2, htons(HDR_ICMP6));
    sum = checksum(&ip->src, 32 + len, sum);
    if (unlikely(sum != 0xFFFF)) {
        BAD_PACKET_FROM(&ip->src, "incorrect checksum in ICMP packet");
        return;
    }

    zx_status_t status;
    if (icmp->type == ICMP6_NDP_N_SOLICIT) {
        ndp_n_hdr_t* ndp = _data;
        struct {
            ndp_n_hdr_t hdr;
            uint8_t opt[8];
        } msg;

        if (unlikely(len < sizeof(ndp_n_hdr_t))) {
            BAD_PACKET_FROM(&ip->src, "bogus NDP message");
            return;
        }
        if (unlikely(ndp->code != 0)) {
            BAD_PACKET_FROM(&ip->src, "bogus NDP code");
            return;
        }
#if !INET6_COEXIST_WITH_NETSTACK
        if (!ip6_addr_eq((ip6_addr_t*)ndp->target, &ll_ip6_addr)) {
            char src_addr_str[IP6TOAMAX];
            char dst_addr_str[IP6TOAMAX];
            ip6toa(src_addr_str, &ip->src);
            ip6toa(dst_addr_str, (ip6_addr_t*)ndp->target);
            printf("inet6: ignoring NDP packet sent from %s to %s\n", src_addr_str, dst_addr_str);
            return;
        }
#endif

        msg.hdr.type = ICMP6_NDP_N_ADVERTISE;
        msg.hdr.code = 0;
        msg.hdr.checksum = 0;
        msg.hdr.flags = 0x60; // (S)olicited and (O)verride flags
        memcpy(msg.hdr.target, &ll_ip6_addr, sizeof(ip6_addr_t));
        msg.opt[0] = NDP_N_TGT_LL_ADDR;
        msg.opt[1] = 1;
        memcpy(msg.opt + 2, &ll_mac_addr, ETH_ADDR_LEN);

        status = icmp6_send(&msg, sizeof(msg), (void*)&ip->src, false);
    } else if (icmp->type == ICMP6_ECHO_REQUEST) {
        icmp->checksum = 0;
        icmp->type = ICMP6_ECHO_REPLY;
        status = icmp6_send(_data, len, (void*)&ip->src, false);
    } else {
        // Ignore
        return;
    }
    if (status == ZX_ERR_SHOULD_WAIT) {
        printf("inet6: No buffers available, dropping ICMP response\n");
    } else if (status < 0) {
        printf("inet6: Failed to send ICMP response (err = %d)\n", status);
    }
}

// If ip is not in cache already, add it. Otherwise, update its last access time.
static void mac_cache_save(mac_addr_t* mac, ip6_addr_t* ip) {
    uint8_t key = mac_cache_hash(ip);

    mtx_lock(&mac_cache_lock);
    ip6_to_mac_t* oldest_entry = &mac_lookup_tbl[key][0];
    zx_time_t curr_time = zx_clock_get_monotonic();

    for (size_t entry_ndx = 0; entry_ndx < MAC_TBL_ENTRIES; entry_ndx++) {
        ip6_to_mac_t* entry = &mac_lookup_tbl[key][entry_ndx];

        if (entry->last_used == 0) {
            // Unused entry -- fill it
            oldest_entry = entry;
            break;
        }

        if (!memcmp(ip, &entry->ip6, sizeof(ip6_addr_t))) {
            // Match found
            if (memcmp(mac, &entry->mac, sizeof(mac_addr_t))) {
                // If mac has changed, update it
                memcpy(&entry->mac, mac, sizeof(mac_addr_t));
            }
            entry->last_used = curr_time;
            goto done;
        }

        if ((entry_ndx > 0) && (entry->last_used < oldest_entry->last_used)) {
            oldest_entry = entry;
        }
    }

    // No available entry found -- replace oldest
    memcpy(&oldest_entry->mac, mac, sizeof(mac_addr_t));
    memcpy(&oldest_entry->ip6, ip, sizeof(ip6_addr_t));
    oldest_entry->last_used = curr_time;

done:
    mtx_unlock(&mac_cache_lock);
}

void eth_recv(void* _data, size_t len) {
    uint8_t* data = _data;
    ip6_hdr_t* ip;
    uint32_t n;

    if (unlikely(len < (ETH_HDR_LEN + IP6_HDR_LEN))) {
        BAD_PACKET("bogus header length");
        return;
    }
    if (data[12] != (ETH_IP6 >> 8))
        return;
    if (data[13] != (ETH_IP6 & 0xFF))
        return;

    ip = (void*)(data + ETH_HDR_LEN);
    data += (ETH_HDR_LEN + IP6_HDR_LEN);
    len -= (ETH_HDR_LEN + IP6_HDR_LEN);

    // require v6
    if (unlikely((ip->ver_tc_flow & 0xF0) != 0x60)) {
        BAD_PACKET("unknown IP6 version");
        return;
    }

    // ensure length is sane
    n = ntohs(ip->length);
    if (unlikely(n > len)) {
        BAD_PACKET("IP6 length mismatch");
        return;
    }

    // ignore any trailing data in the ethernet frame
    len = n;

    // require that we are the destination
    if (!ip6_addr_eq(&ll_ip6_addr, &ip->dst) &&
        !ip6_addr_eq(&snm_ip6_addr, &ip->dst) &&
        !ip6_addr_eq(&ip6_ll_all_nodes, &ip->dst)) {
        return;
    }

    // stash the sender's info to simplify replies
    mac_cache_save((void*)_data + 6, &ip->src);

    switch (ip->next_header) {
    case HDR_ICMP6:
        icmp6_recv(ip, data, len);
        break;
    case HDR_UDP:
        _udp6_recv(ip, data, len);
        break;
    default:
        // do nothing
        break;
    }
}

char* ip6toa(char* _out, void* ip6addr) {
    const uint8_t* x = ip6addr;
    const uint8_t* end = x + 16;
    char* out = _out;
    uint16_t n;

    n = (x[0] << 8) | x[1];
    while ((n == 0) && (x < end)) {
        x += 2;
        n = (x[0] << 8) | x[1];
    }

    if ((end - x) < 16) {
        if (end == x) {
            // all 0s - special case
            sprintf(out, "::");
            return _out;
        }
        // we consumed some number of leading 0s
        out += sprintf(out, ":");
        while (x < end) {
            out += sprintf(out, ":%x", n);
            x += 2;
            n = (x[0] << 8) | x[1];
        }
        return _out;
    }

    while (x < (end - 2)) {
        out += sprintf(out, "%x:", n);
        x += 2;
        n = (x[0] << 8) | x[1];
        if (n == 0)
            goto middle_zeros;
    }
    out += sprintf(out, "%x", n);
    return _out;

middle_zeros:
    while ((n == 0) && (x < end)) {
        x += 2;
        n = (x[0] << 8) | x[1];
    }
    if (x == end) {
        out += sprintf(out, ":");
        return _out;
    }
    out += sprintf(out, ":%x", n);
    while (x < (end - 2)) {
        x += 2;
        n = (x[0] << 8) | x[1];
        out += sprintf(out, ":%x", n);
    }
    return _out;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/zircon-internal/fnv1hash.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <zircon/syscalls.h>

#include <inet6/inet6.h>
#include <inet6/netifc-discover.h>

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

// Convert MAC Address to IPv6 Unique Local Address.
void ula6addr_from_mac(ip6_addr_t* _ip, const mac_addr_t* _mac) {
  uint8_t* ip = _ip->u8;
  const uint8_t* mac = _mac->x;
  memset(ip, 0, sizeof(ip6_addr_t));

  ip[0] = 0xFD;
  ip[1] = mac[1];
  ip[2] = mac[2];
  ip[3] = mac[3];
  ip[4] = mac[4];
  ip[5] = mac[5];

  // We leave byte-0 out above because it is the least unique but we want
  // it just in case by some slight chance there are two NICs with the other
  // bytes matching.
  ip[13] = mac[0];
  // We need these down here to keep us matching the snmaddr.
  ip[13] = mac[3];
  ip[14] = mac[4];
  ip[15] = mac[5];
}

// Convert MAC Address to IPv6 Link Local Address
// aa:bb:cc:dd:ee:ff => FF80::aabb:cc4D:FEdd:eeff
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
  // Normally this would be set to 0xFF when generating the modified EUI-64 interface identifier, as
  // per RFC 4291 section 2.5.1. However, various bits of infrastructure rely on having knowledge of
  // this address generation algorithm.
  //
  // TODO(fxbug.dev/60888): change this to 0xFF when infrastructure no longer relies on this magic.
  ip[11] = 'M';
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
static ip6_addr_t ula_ip6_addr;
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

void ip6_init(void* macaddr, bool quiet) {
  char tmp[IP6TOAMAX];
  mac_addr_t all;

  // Clear our ip6 -> MAC address lookup table
  mac_cache_init();

  // save our ethernet MAC and synthesize link layer addresses
  memcpy(&ll_mac_addr, macaddr, 6);
  ula6addr_from_mac(&ula_ip6_addr, &ll_mac_addr);
  ll6addr_from_mac(&ll_ip6_addr, &ll_mac_addr);
  snmaddr_from_mac(&snm_ip6_addr, &ll_mac_addr);
  multicast_from_ip6(&snm_mac_addr, &snm_ip6_addr);

  eth_add_mcast_filter(&snm_mac_addr);

  multicast_from_ip6(&all, &ip6_ll_all_nodes);
  eth_add_mcast_filter(&all);

  if (!quiet) {
    printf("macaddr: %02x:%02x:%02x:%02x:%02x:%02x\n", ll_mac_addr.x[0], ll_mac_addr.x[1],
           ll_mac_addr.x[2], ll_mac_addr.x[3], ll_mac_addr.x[4], ll_mac_addr.x[5]);
    printf("ip6addr (LL) : %s\n", ip6toa(tmp, &ll_ip6_addr));
    printf("ip6addr (ULA): %s\n", ip6toa(tmp, &ula_ip6_addr));
    printf("snmaddr: %s\n", ip6toa(tmp, &snm_ip6_addr));
  }
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

static int ip6_setup(ip6_pkt_t* p, const ip6_addr_t* saddr, const ip6_addr_t* daddr, size_t length,
                     uint8_t type) {
  mac_addr_t dmac;

  if (resolve_ip6(&dmac, daddr))
    return -1;

  // ethernet header
  memcpy(p->eth + 2, &dmac, ETH_ADDR_LEN);
  memcpy(p->eth + 8, &ll_mac_addr, ETH_ADDR_LEN);
  p->eth[14] = (ETH_IP6 >> 8) & 0xFF;
  p->eth[15] = ETH_IP6 & 0xFF;

  // ip6 header
  p->ip6.ver_tc_flow = 0x60;  // v=6, tc=0, flow=0
  p->ip6.length = htons(length);
  p->ip6.next_header = type;
  p->ip6.hop_limit = 255;
  p->ip6.src = *saddr;
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
  zx_status_t status = eth_get_buffer(ETH_MTU + 2, (void**)&p, &ethbuf, block);
  if (status != ZX_OK) {
    return status;
  }

  const bool ula = (*daddr).u8[0] == ula_ip6_addr.u8[0];
  const ip6_addr_t* const saddr = ula ? &ula_ip6_addr : &ll_ip6_addr;
  if (ip6_setup((void*)p, saddr, daddr, length, HDR_UDP)) {
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

static zx_status_t icmp6_send(const void* data, size_t length, const ip6_addr_t* saddr,
                              const ip6_addr_t* daddr, bool block) {
  if (length > ICMP6_MAX_PAYLOAD)
    return ZX_ERR_INVALID_ARGS;
  eth_buffer_t* ethbuf;
  ip6_pkt_t* p;
  icmp6_hdr_t* icmp;

  zx_status_t status = eth_get_buffer(ETH_MTU + 2, (void**)&p, &ethbuf, block);
  if (status != ZX_OK) {
    return status;
  }
  if (ip6_setup(p, saddr, daddr, length, HDR_ICMP6)) {
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

// This is the cornerstone of SLAAC networking. This will have connected clients
// add an ipv6 address that can talk to our ULA address.
void send_router_advertisement() {
  // This struct is not a generic advert packet, it is specific to sending a
  // single prefix, if you want to do more look at the spec and extend.
  static struct {
    icmp6_hdr_t hdr;
    uint8_t hop_limit;  // 0 means this router has no opinion.
    uint8_t autoconf_flags;
    uint16_t router_lifetime_ms;   // 0 means don't use this router.
    uint32_t reachable_time_ms;    // 0 means this router has no opinion.
    uint32_t retransmit_timer_ms;  // 0 means this router has no opinion.
    uint8_t option_type;           // We are using a prefix option of 3
    uint8_t option_length;         // length is units of 8 bytes (for some reason).
    uint8_t prefix_length;         // valid bits of prefix.
    uint8_t prefix_flags;
    uint32_t prefix_lifetime_s;
    uint32_t prefix_pref_lifetime_s;
    uint32_t reserved;
    uint8_t prefix[16];  // prefix for all devices on this link to communicate.
  } __attribute__((packed)) msg;

  memset(&msg, 0, sizeof(msg));

  msg.hdr.type = ICMP6_NDP_R_ADVERTISE;
  msg.hdr.code = 0;
  msg.hdr.checksum = 0;
  msg.option_type = 3;                      // Prefix option.
  msg.option_length = 4;                    // From spec, length is in 64bit units.
  msg.prefix_length = 64;                   // 64 leading bits of address are all we care about.
  msg.prefix_flags = 0b11000000;            // valid on this link and used for autoconf.
  msg.prefix_lifetime_s = 0xFFFFFFFF;       // valid while this link is up.
  msg.prefix_pref_lifetime_s = 0xFFFFFFFF;  // preferred while this link is up.

  // Copy first 8 bytes (64bits) as our prefix, rest will be 0.
  memcpy(msg.prefix, &ula_ip6_addr, 8);

  // We need to send this on the link-local address because nothing is talking
  // to the ula address yet.
  zx_status_t status =
      icmp6_send(&msg, sizeof(msg), (void*)&ll_ip6_addr, (void*)&ip6_ll_all_nodes, false);
  if (status == ZX_ERR_SHOULD_WAIT) {
    printf("inet6: No buffers available, dropping RA\n");
  } else if (status < 0) {
    printf("inet6: Failed to send RA (err = %d)\n", status);
  }
}

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

  sum = ip6_checksum(ip, HDR_UDP, len);
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

  udp6_recv((uint8_t*)_data + UDP_HDR_LEN, len, (void*)&ip->dst, ntohs(udp->dst_port),
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

  sum = ip6_checksum(ip, HDR_ICMP6, len);
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

    // Ignore the neighbor solicitation if it is targetting another node, as per
    // RFC 4861 section 7.2.3.
    if (!ip6_addr_eq((ip6_addr_t*)ndp->target, &ll_ip6_addr) &&
        !ip6_addr_eq((ip6_addr_t*)ndp->target, &ula_ip6_addr)) {
      return;
    }

    msg.hdr.type = ICMP6_NDP_N_ADVERTISE;
    msg.hdr.code = 0;
    msg.hdr.checksum = 0;
    msg.hdr.flags = 0x60;  // (S)olicited and (O)verride flags
    memcpy(msg.hdr.target, ndp->target, sizeof(ip6_addr_t));
    msg.opt[0] = NDP_N_TGT_LL_ADDR;
    msg.opt[1] = 1;
    memcpy(msg.opt + 2, &ll_mac_addr, ETH_ADDR_LEN);

    // If the target was on the ula network, respond from it.
    // Otherwise respond from the ll address.
    const bool ula = ndp->target[0] == ula_ip6_addr.u8[0];
    const ip6_addr_t* const saddr = ula ? &ula_ip6_addr : &ll_ip6_addr;

    status = icmp6_send(&msg, sizeof(msg), saddr, (void*)&ip->src, false);
  } else if (icmp->type == ICMP6_ECHO_REQUEST) {
    icmp->checksum = 0;
    icmp->type = ICMP6_ECHO_REPLY;
    status = icmp6_send(_data, len, (void*)&ip->dst, (void*)&ip->src, false);
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
  if (!ip6_addr_eq(&ll_ip6_addr, &ip->dst) && !ip6_addr_eq(&snm_ip6_addr, &ip->dst) &&
      !ip6_addr_eq(&ip6_ll_all_nodes, &ip->dst) && !ip6_addr_eq(&ula_ip6_addr, &ip->dst)) {
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

char* ip6toa(char* _out, const void* ip6addr) {
  // Encode our address using the scheme laid out in RFC 1884 section 2.2
  //
  // Basically, we have eight 16 bit words in RAM (in network byte order, aka
  // big endian) which need to be rendered in hex with ':'s separating each
  // word.  Once per encoding, we may choose to replace a run of 0s with "::"
  // instead of the run.  This implementation will always replace the first run,
  // it will not make any effort to find and replace the longest run.
  const size_t kIPv6AddrWords = 8;

  const uint16_t* addr = ip6addr;
  char* out = _out;
  size_t i = 0;

  // Start by encoding while keeping on the lookout for any zeros.
  for (; i < kIPv6AddrWords; ++i) {
    // Have we found some zeros?  If so, skip the run, replace it with a "::"
    // instead.  There is no need to do any potential endian flipping here as
    // zero is always zero, regardless of endianness.
    if (addr[i] == 0) {
      while ((++i < kIPv6AddrWords) && (addr[i] == 0)) {
      }

      // If the address ends with a 0-run, then emit the full :: token and we
      // are finished.
      if (i == kIPv6AddrWords) {
        sprintf(out, "::");
        return _out;
      }

      // There are still words to be encoded, emit a single ':' and then move
      // onto phase 2 (post-0-run encoding).
      *(out++) = ':';
      break;
    }

    // Skip the ':' separator if this is the first word in the sequence.
    if (i != 0) {
      *(out++) = ':';
    }

    // Output the word, skipping leading zeros to save space.
    out += sprintf(out, "%x", ntohs(addr[i]));
  }

  // Phase 2 of processing.  At this point, we no longer need to look for any
  // zero runs since we have already spent our "::" token.  Also, there is no
  // need to worry about being the first word in the sequence, so we can
  // unconditionally separate words with ":".
  for (; i < kIPv6AddrWords; ++i) {
    out += sprintf(out, ":%x", ntohs(addr[i]));
  }

  return _out;
}

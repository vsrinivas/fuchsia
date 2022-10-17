// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/inet6.h"

#include <arpa/inet.h>
#include <lib/zircon-internal/fnv1hash.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <mutex>
#include <optional>

#include "src/bringup/bin/netsvc/netifc-discover.h"
#include "src/bringup/bin/netsvc/netifc.h"

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

namespace {

// Convert MAC Address to IPv6 Unique Local Address.
ip6_addr_t ula6addr_from_mac(const mac_addr_t& mac) {
  return {.u8 = {
              0xFD,
              mac.x[1],
              mac.x[2],
              mac.x[3],
              mac.x[4],
              mac.x[5],
              0,
              0,
              0,
              0,
              0,
              0,
              // We need these down here to keep us matching the snmaddr.
              mac.x[3],
              mac.x[4],
              mac.x[5],
          }};
}

// Convert MAC Address to IPv6 Link Local Address
// aa:bb:cc:dd:ee:ff => FF80::aabb:cc4D:FEdd:eeff
// bit 2 (U/L) of the mac is inverted
ip6_addr_t ll6addr_from_mac(const mac_addr_t& mac) {
  return {.u8 = {
              0xFE,
              0x80,
              0,
              0,
              0,
              0,
              0,
              0,
              // Flip the globally-unique bit from the MAC
              // since the sense of this is backwards in
              // IPv6 Interface Identifiers.
              static_cast<uint8_t>(mac.x[0] ^ 2),
              mac.x[1],
              mac.x[2],
              0xFE,
              0xFE,
              mac.x[3],
              mac.x[4],
              mac.x[5],
          }};
}

// Convert MAC Address to IPv6 Solicit Neighbor Multicast Address
// aa:bb:cc:dd:ee:ff -> FF02::1:FFdd:eeff
ip6_addr_t snmaddr_from_mac(const mac_addr_t& mac) {
  return {.u8 = {
              0xFF,
              0x02,
              0,
              0,
              0,
              0,
              0,
              0,
              0,
              0,
              0,
              0x01,
              0xFF,
              mac.x[3],
              mac.x[4],
              mac.x[5],
          }};
}

// Convert IPv6 Multicast Address to Ethernet Multicast Address
mac_addr_t multicast_from_ip6(const ip6_addr_t& ip6) {
  return {.x = {
              0x33,
              0x33,
              ip6.u8[12],
              ip6.u8[13],
              ip6.u8[14],
              ip6.u8[15],
          }};
}

// cache for the last source addresses we've seen
#define MAC_TBL_BUCKETS 256
#define MAC_TBL_ENTRIES 5
struct ip6_to_mac_t {
  zx_time_t last_used;  // A value of 0 indicates "unused".
  ip6_addr_t ip6;
  mac_addr_t mac;
};

uint8_t mac_cache_hash(const ip6_addr_t& ip) {
  static_assert(MAC_TBL_BUCKETS == 256, "hash algorithms must be updated");
  uint32_t hash = fnv1a32(&ip, sizeof(ip));
  return ((hash >> 8) ^ hash) & 0xff;
}

// ip6 stack configuration.
struct Ip6Stack {
  const mac_addr_t ll_mac_addr;
  const ip6_addr_t ll_ip6_addr;
  const ip6_addr_t ula_ip6_addr;
  const ip6_addr_t snm_ip6_addr;
  const mac_addr_t snm_mac_addr;
  ip6_to_mac_t mac_lookup_tbl[MAC_TBL_BUCKETS][MAC_TBL_ENTRIES] __TA_GUARDED(mac_cache_lock);
  std::mutex mac_cache_lock;

  Ip6Stack(mac_addr_t macaddr, bool quiet)
      : ll_mac_addr(macaddr),
        ll_ip6_addr(ll6addr_from_mac(macaddr)),
        ula_ip6_addr(ula6addr_from_mac(macaddr)),
        snm_ip6_addr(snmaddr_from_mac(macaddr)),
        snm_mac_addr(multicast_from_ip6(snm_ip6_addr)) {
    size_t bucket_ndx;
    size_t entry_ndx;
    for (bucket_ndx = 0; bucket_ndx < MAC_TBL_BUCKETS; bucket_ndx++) {
      for (entry_ndx = 0; entry_ndx < MAC_TBL_ENTRIES; entry_ndx++) {
        mac_lookup_tbl[bucket_ndx][entry_ndx].last_used = 0;
      }
    }

    eth_add_mcast_filter(&snm_mac_addr);

    mac_addr_t all = multicast_from_ip6(ip6_ll_all_nodes);
    eth_add_mcast_filter(&all);

    if (!quiet) {
      printf("macaddr: %02x:%02x:%02x:%02x:%02x:%02x\n", ll_mac_addr.x[0], ll_mac_addr.x[1],
             ll_mac_addr.x[2], ll_mac_addr.x[3], ll_mac_addr.x[4], ll_mac_addr.x[5]);

      char tmp[INET6_ADDRSTRLEN];
      printf("ip6addr (LL) : %s\n", inet_ntop(AF_INET6, &ll_ip6_addr, tmp, sizeof(tmp)));
      printf("ip6addr (ULA): %s\n", inet_ntop(AF_INET6, &ula_ip6_addr, tmp, sizeof(tmp)));
      printf("snmaddr: %s\n", inet_ntop(AF_INET6, &snm_ip6_addr, tmp, sizeof(tmp)));
    }
  }

  // Find the MAC corresponding to a given IP6 address
  std::optional<mac_addr_t> ResolveIp6(const ip6_addr_t& ip) {
    // Multicast addresses are a simple transform
    if (ip.u8[0] == 0xFF) {
      return multicast_from_ip6(ip);
    }

    uint8_t key = mac_cache_hash(ip);

    std::lock_guard lock(mac_cache_lock);
    for (size_t entry_ndx = 0; entry_ndx < MAC_TBL_ENTRIES; entry_ndx++) {
      ip6_to_mac_t* entry = &mac_lookup_tbl[key][entry_ndx];

      if (entry->last_used == 0) {
        // All out of entries
        break;
      }

      if (entry->ip6 == ip) {
        // Match!
        return entry->mac;
      }
    }
    return std::nullopt;
  }

  // If ip is not in cache already, add it. Otherwise, update its last access time.
  void SaveMacCache(const mac_addr_t& mac, const ip6_addr_t& ip) {
    uint8_t key = mac_cache_hash(ip);

    std::lock_guard lock(mac_cache_lock);
    ip6_to_mac_t* oldest_entry = &mac_lookup_tbl[key][0];
    zx_time_t curr_time = zx_clock_get_monotonic();

    for (size_t entry_ndx = 0; entry_ndx < MAC_TBL_ENTRIES; entry_ndx++) {
      ip6_to_mac_t* entry = &mac_lookup_tbl[key][entry_ndx];

      if (entry->last_used == 0) {
        // Unused entry -- fill it
        oldest_entry = entry;
        break;
      }

      if (entry->ip6 == ip) {
        // Match found.
        // Update mac and last seen.
        entry->mac = mac;
        entry->last_used = curr_time;
        return;
      }

      if ((entry_ndx > 0) && (entry->last_used < oldest_entry->last_used)) {
        oldest_entry = entry;
      }
    }

    // No available entry found -- replace oldest.
    *oldest_entry = {
        .last_used = curr_time,
        .ip6 = ip,
        .mac = mac,
    };
  }

  std::optional<std::tuple<uint8_t*, uint16_t>> PrepareIp6Packet(uint8_t* data,
                                                                 const ip6_addr_t& saddr,
                                                                 const ip6_addr_t& daddr,
                                                                 size_t length, uint8_t type) {
    std::optional mac = ResolveIp6(daddr);
    if (!mac.has_value()) {
      printf("%s: Failed to resolve ipv6 addr\n", __func__);
      return std::nullopt;
    }

    mac_addr_t& dmac = mac.value();
    // Ethernet header.
    data = std::copy(std::begin(dmac.x), std::end(dmac.x), data);
    data = std::copy(std::begin(ll_mac_addr.x), std::end(ll_mac_addr.x), data);
    *data++ = (ETH_IP6 >> 8) & 0xFF;
    *data++ = ETH_IP6 & 0xFF;
    // ip6 header.
    const ip6_hdr hdr = {
        .ver_tc_flow = 0x60,  // v=6, tc=0, flow=0
        .length = htons(length),
        .next_header = type,
        .hop_limit = 255,
        .src = saddr,
        .dst = daddr,
    };
    data = std::copy_n(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr), data);
    return std::make_tuple(data, ip6_header_checksum(hdr, type));
  }

  static constexpr size_t kIcmp6MaxPayload = ETH_MTU - ETH_HDR_LEN - IP6_HDR_LEN;

  zx_status_t SendIcmp6(const void* data, size_t length, const ip6_addr_t& saddr,
                        const ip6_addr_t& daddr, bool block) {
    if (length > kIcmp6MaxPayload) {
      return ZX_ERR_INVALID_ARGS;
    }

    zx::result status = DeviceBuffer::Get(ETH_MTU, block);
    if (status.is_error()) {
      return status.status_value();
    }
    DeviceBuffer& buffer = status.value();

    std::optional prepared =
        PrepareIp6Packet(buffer.data().begin(), saddr, daddr, length, HDR_ICMP6);
    if (!prepared.has_value()) {
      return ZX_ERR_INVALID_ARGS;
    }
    auto [body, checksum] = prepared.value();
    std::copy_n(static_cast<const uint8_t*>(data), length, body);
    checksum = ip6_finalize_checksum(checksum, data, length);
    std::copy_n(reinterpret_cast<const uint8_t*>(&checksum), sizeof(checksum),
                body + offsetof(icmp6_hdr_t, checksum));
    return buffer.Send(ETH_HDR_LEN + IP6_HDR_LEN + length);
  }
};

std::optional<Ip6Stack> g_state;

}  // namespace

void ip6_init(mac_addr_t macaddr, bool quiet) { g_state.emplace(macaddr, quiet); }

#define UDP6_MAX_PAYLOAD (ETH_MTU - ETH_HDR_LEN - IP6_HDR_LEN - UDP_HDR_LEN)

zx_status_t udp6_send(const void* data, size_t dlen, const ip6_addr_t* daddr, uint16_t dport,
                      uint16_t sport, bool block) {
  if (dlen > UDP6_MAX_PAYLOAD)
    return ZX_ERR_INVALID_ARGS;
  size_t length = dlen + UDP_HDR_LEN;
  zx::result status = DeviceBuffer::Get(ETH_MTU, block);
  if (status.is_error()) {
    printf("%s: DeviceBuffer::Get failed: %s\n", __func__, status.status_string());
    return status.status_value();
  }
  DeviceBuffer& buffer = status.value();

  ZX_ASSERT(g_state.has_value());
  Ip6Stack& stack_state = g_state.value();
  const bool ula = (*daddr).u8[0] == stack_state.ula_ip6_addr.u8[0];
  const ip6_addr_t& saddr = ula ? stack_state.ula_ip6_addr : stack_state.ll_ip6_addr;

  std::optional prepared =
      stack_state.PrepareIp6Packet(buffer.data().begin(), saddr, *daddr, length, HDR_UDP);
  if (!prepared.has_value()) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto [body, checksum] = prepared.value();
  const udp_hdr_t udp = {
      .src_port = htons(sport),
      .dst_port = htons(dport),
      .length = htons(length),
      .checksum = 0,
  };
  uint8_t* payload = std::copy_n(reinterpret_cast<const uint8_t*>(&udp), sizeof(udp), body);
  std::copy_n(static_cast<const uint8_t*>(data), dlen, payload);
  checksum = ip6_finalize_checksum(checksum, body, sizeof(udp) + dlen);
  // Do not transmit all zeroes checksum, replace with its one's complement.
  //
  // https://datatracker.ietf.org/doc/html/rfc768
  if (checksum == 0) {
    checksum = 0xFFFF;
  }
  std::copy_n(reinterpret_cast<const uint8_t*>(&checksum), sizeof(checksum),
              body + offsetof(udp_hdr_t, checksum));
  zx_status_t ret = buffer.Send(ETH_HDR_LEN + IP6_HDR_LEN + length);
  if (ret != ZX_OK) {
    printf("%s: buffer.Send(%zu) failed: %s\n", __func__, ETH_HDR_LEN + IP6_HDR_LEN + length,
           zx_status_get_string(ret));
  }
  return ret;
}

#if REPORT_BAD_PACKETS
static void report_bad_packet(ip6_addr_t* ip6_addr, const char* msg) {
  if (ip6_addr == NULL) {
    printf("inet6: dropping packet: %s\n", msg);
  } else {
    char addr_str[INET6_ADDRSTRLEN];
    printf("inet6: dropping packet from %s: %s\n",
           inet_ntop(AF_INET6, ip6_addr, addr_str, sizeof(addr_str)), msg);
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

  ZX_ASSERT(g_state.has_value());
  Ip6Stack& stack_state = g_state.value();

  // Copy first 8 bytes (64bits) as our prefix, rest will be 0.
  memcpy(msg.prefix, &stack_state.ula_ip6_addr, 8);

  // We need to send this on the link-local address because nothing is talking
  // to the ula address yet.
  zx_status_t status =
      stack_state.SendIcmp6(&msg, sizeof(msg), stack_state.ll_ip6_addr, ip6_ll_all_nodes, false);
  if (status == ZX_ERR_SHOULD_WAIT) {
    printf("inet6: No buffers available, dropping RA\n");
  } else if (status < 0) {
    printf("inet6: Failed to send RA (err = %d)\n", status);
  }
}

void udp6_recv_internal(async_dispatcher_t* dispatcher, ip6_hdr_t* ip, void* _data, size_t len) {
  udp_hdr_t* udp = static_cast<udp_hdr_t*>(_data);
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

  sum = ip6_finalize_checksum(ip6_header_checksum(*ip, HDR_UDP), udp, len);
  if (unlikely(sum != 0)) {
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

  udp6_recv(dispatcher, static_cast<uint8_t*>(_data) + UDP_HDR_LEN, len, &ip->dst,
            ntohs(udp->dst_port), &ip->src, ntohs(udp->src_port));
}

void icmp6_recv(ip6_hdr_t* ip, void* _data, size_t len) {
  icmp6_hdr_t* icmp = static_cast<icmp6_hdr_t*>(_data);
  uint16_t sum;

  sum = ip6_finalize_checksum(ip6_header_checksum(*ip, HDR_ICMP6), icmp, len);
  if (unlikely(sum != 0)) {
    BAD_PACKET_FROM(&ip->src, "incorrect checksum in ICMP packet");
    return;
  }

  ZX_ASSERT(g_state.has_value());
  Ip6Stack& stack_state = g_state.value();

  zx_status_t status;
  if (icmp->type == ICMP6_NDP_N_SOLICIT) {
    ndp_n_hdr_t* ndp = static_cast<ndp_n_hdr_t*>(_data);
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

    // Ignore the neighbor solicitation if it is targeting another node, as per
    // RFC 4861 section 7.2.3.
    {
      const ip6_addr_t& ndp_target = *reinterpret_cast<ip6_addr_t*>(ndp->target);
      if (ndp_target != stack_state.ll_ip6_addr && ndp_target != stack_state.ula_ip6_addr) {
        return;
      }
    }

    msg.hdr.type = ICMP6_NDP_N_ADVERTISE;
    msg.hdr.code = 0;
    msg.hdr.checksum = 0;
    msg.hdr.flags = 0x60;  // (S)olicited and (O)verride flags
    memcpy(msg.hdr.target, ndp->target, sizeof(ip6_addr_t));
    msg.opt[0] = NDP_N_TGT_LL_ADDR;
    msg.opt[1] = 1;
    memcpy(msg.opt + 2, &stack_state.ll_mac_addr, ETH_ADDR_LEN);

    // If the target was on the ula network, respond from it.
    // Otherwise respond from the ll address.
    const bool ula = ndp->target[0] == stack_state.ula_ip6_addr.u8[0];
    const ip6_addr_t& saddr = ula ? stack_state.ula_ip6_addr : stack_state.ll_ip6_addr;

    status = stack_state.SendIcmp6(&msg, sizeof(msg), saddr, ip->src, false);
  } else if (icmp->type == ICMP6_ECHO_REQUEST) {
    icmp->checksum = 0;
    icmp->type = ICMP6_ECHO_REPLY;
    status = stack_state.SendIcmp6(_data, len, ip->dst, ip->src, false);
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

void eth_recv(async_dispatcher_t* dispatcher, void* _data, size_t len) {
  uint8_t* data = static_cast<uint8_t*>(_data);
  uint32_t n;

  if (unlikely(len < (ETH_HDR_LEN + IP6_HDR_LEN))) {
    BAD_PACKET("bogus header length");
    return;
  }
  if (data[12] != (ETH_IP6 >> 8))
    return;
  if (data[13] != (ETH_IP6 & 0xFF))
    return;

  // Copy IP header to prevent misaligned access.
  ip6_hdr_t ip;
  std::copy_n(data + ETH_HDR_LEN, IP6_HDR_LEN, reinterpret_cast<uint8_t*>(&ip));
  data += (ETH_HDR_LEN + IP6_HDR_LEN);
  len -= (ETH_HDR_LEN + IP6_HDR_LEN);

  // Require v6.
  if (unlikely((ip.ver_tc_flow & 0xF0) != 0x60)) {
    BAD_PACKET("unknown IP6 version");
    return;
  }

  // Ensure length is in bounds.
  n = ntohs(ip.length);
  if (unlikely(n > len)) {
    BAD_PACKET("IP6 length mismatch");
    return;
  }

  ZX_ASSERT(g_state.has_value());
  Ip6Stack& stack_state = g_state.value();

  // Ignore any trailing data in the ethernet frame.
  len = n;
  // Require that we are the destination.
  if (ip.dst != stack_state.ll_ip6_addr && ip.dst != stack_state.snm_ip6_addr &&
      ip.dst != ip6_ll_all_nodes && ip.dst != stack_state.ula_ip6_addr) {
    return;
  }

  // stash the sender's info to simplify replies
  mac_addr& mac = *reinterpret_cast<mac_addr*>(static_cast<uint8_t*>(_data) + ETH_ADDR_LEN);
  stack_state.SaveMacCache(mac, ip.src);

  switch (ip.next_header) {
    case HDR_ICMP6:
      icmp6_recv(&ip, data, len);
      break;
    case HDR_UDP:
      udp6_recv_internal(dispatcher, &ip, data, len);
      break;
    default:
      // do nothing
      break;
  }
}

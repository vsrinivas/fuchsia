// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_MDNS_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_MDNS_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include "inet6.h"
#include "netifc.h"

__BEGIN_CDECLS

// Start broadcasting mDNS information.
// |namegen| is `generation` parameter passed to device_id(), which determines if the device
// nodename is an old-style step-atom-yard-juicy name or a new-style fuchsia-5254-0063-5e7a name.
//
// This expects sole access to the netifc timer.
void mdns_start(uint32_t namegen);
void mdns_poll(void);
// Stop broadcasting mDNS information.
void mdns_stop(void);

// Everything below this point is only available
// outside of mdns.c for testing purposes.
#define MDNS_TYPE_PTR 12
#define MDNS_TYPE_AAAA 28
#define MDNS_TYPE_SRV 33

#define MDNS_MAX_PKT (ETH_MTU - IP6_HDR_LEN - UDP_HDR_LEN)
// Indicates that name is already present in the mDNS packet, at the offset in the low byte.
#define MDNS_NAME_AT_OFFSET_FLAG (0xc000)
// A buffer used during the construction of mDNS packets.
// The various mdns_write_* calls take this as an argument.
struct mdns_buf {
  // Packet data.
  uint8_t data[MDNS_MAX_PKT];
  // Amount of data in the packet.
  size_t used;
};

struct mdns_header {
  uint16_t id;
  uint16_t flags;
  uint16_t question_count;
  uint16_t answer_count;
  uint16_t authority_count;
  uint16_t additional_count;
};

// Represents a part of a name. Each segment must not have a ".".
// `loc` should be set to zero every time a new packet is created.
// `next` should point to the next name segment in this name.
//
// e.g. "www.google.com" would correspond to:
// struct mdns_name_segment google[3] = {
//  {.name = "wwww", .loc = 0, .next = &google[1], },
//  {.name = "google", .loc = 0, .next = &google[2], },
//  {.name = "com", .loc = 0, .next = NULL, },
//};
struct mdns_name_segment {
  const char* name;
  uint16_t loc;
  struct mdns_name_segment* next;
};

struct mdns_ptr_record {
  struct mdns_name_segment* name;
};

struct mdns_aaaa_record {
  ip6_addr addr;
};

struct mdns_srv_record {
  uint16_t priority;
  uint16_t weight;
  uint16_t port;
  struct mdns_name_segment* target;
};

struct mdns_record {
  struct mdns_name_segment* name;
  uint16_t type;
  uint16_t record_class;
  uint32_t time_to_live;
  union {
    struct mdns_ptr_record ptr;
    struct mdns_aaaa_record aaaa;
    struct mdns_srv_record srv;
  } data;
};

bool mdns_write_u16(struct mdns_buf* b, uint16_t v);
bool mdns_write_u32(struct mdns_buf* b, uint32_t v);
bool mdns_write_name(struct mdns_buf* b, struct mdns_name_segment* name);
bool mdns_write_record(struct mdns_buf* b, struct mdns_record* r);
bool mdns_write_packet(struct mdns_header* hdr, struct mdns_record* records, struct mdns_buf* pkt);

// Writes the full fastboot mDNS packet.
//
// Args:
//   finished: true for the final mDNS packet with TTL = 0.
//   tcp: true for TCP, false for UDP.
//   packet_buf: packet buffer to fill.
//
// Returns true on success.
bool mdns_write_fastboot_packet(bool finished, bool tcp, struct mdns_buf* packet_buf);

// The default nodename, will be replaced as soon as mdns_start() is called,
// but exposed here so that tests can more easily validate behavior.
#define MDNS_DEFAULT_NODENAME_FOR_TEST "<no_nodename>"

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_MDNS_H_

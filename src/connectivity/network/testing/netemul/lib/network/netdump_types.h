// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_TYPES_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_TYPES_H_

#include <stdint.h>

namespace netemul {

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define PAD(a) ROUNDUP(a, 4)

constexpr uint32_t kSectionHeaderBlockType = 0x0A0D0D0A;
constexpr uint32_t kInterfaceDescriptionBlockType = 0x00000001;
constexpr uint32_t kSectionHeaderByteOrderMagic = 0x1A2B3C4D;
constexpr uint16_t kLinkTypeEthernet = 0x0001;
constexpr uint16_t kOptionInterfaceName = 0x0002;
constexpr uint32_t kEnhancedPacketBlockType = 0x00000006;

typedef struct {
  uint32_t type;
  uint32_t blk_tot_len;
  uint32_t magic;
  uint16_t major;
  uint16_t minor;
  uint64_t section_len;
  uint32_t blk_tot_len2;
} __attribute__((packed)) pcap_shb_t;

typedef struct {
  uint32_t type;
  uint32_t blk_tot_len;
  uint16_t linktype;
  uint16_t reserved;
  uint32_t snaplen;
  // options
} __attribute__((packed)) pcap_idb_t;

typedef struct {
  uint32_t type;
  uint32_t blk_tot_len;
  uint32_t interface_id;
  uint64_t timestamp;
  uint32_t orig_len;
  uint32_t captured_len;
} __attribute__((packed)) enhanced_pkt_t;

typedef struct {
  uint16_t type;
  uint16_t len;
} __attribute__((packed)) option_tlv_t;

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_TYPES_H_

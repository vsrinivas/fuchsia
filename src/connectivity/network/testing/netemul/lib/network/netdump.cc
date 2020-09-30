// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netdump.h"

#include <lib/zx/clock.h>

#include <iomanip>

#include "netdump_types.h"

namespace netemul {

// Classes below implement the PCAP Next Generation (pcapng) format.
// The specification can be found at:
// https://github.com/pcapng/pcapng

#define ENHANCED_PKT_MIN_SIZE (sizeof(enhanced_pkt_t) + sizeof(uint32_t))
#define IDB_MIN_SIZE (sizeof(pcap_idb_t) + sizeof(uint32_t))
#define OPTION_LEN(len) (sizeof(option_tlv_t) + PAD(len))

void NetworkDump::Write(const void* data, size_t len) {
  out_->write(reinterpret_cast<const char*>(data), len);
}

void NetworkDump::WriteOption(uint16_t type, const void* data, uint16_t len) {
  option_tlv_t opt = {type, len};
  Write(&opt, sizeof(opt));
  Write(data, len);
  auto padded_len = static_cast<size_t>(PAD(len));
  if (padded_len > len) {
    auto padding = padded_len - len;
    // pcap padding is a round up aligned to 32 bits.
    // if padding is not in interval [0,3] something went
    // terribly wrong.
    ZX_DEBUG_ASSERT(padding <= 3);
    static const uint32_t zero = 0;
    Write(&zero, padding);
  }
}

void NetworkDump::WriteHeaders() {
  pcap_shb_t shb;
  shb.type = kSectionHeaderBlockType;
  shb.blk_tot_len = sizeof(pcap_shb_t);
  shb.magic = kSectionHeaderByteOrderMagic;
  shb.major = 1;
  shb.minor = 0;
  shb.section_len = 0xFFFFFFFFFFFFFFFF;
  shb.blk_tot_len2 = sizeof(pcap_shb_t);
  Write(&shb, sizeof(shb));
}

uint32_t NetworkDump::AddInterface(const std::string& name) {
  pcap_idb_t idb;
  idb.type = kInterfaceDescriptionBlockType;
  idb.blk_tot_len = static_cast<uint32_t>(IDB_MIN_SIZE + OPTION_LEN(name.length()));
  idb.linktype = kLinkTypeEthernet;
  idb.reserved = 0;
  // We can't use a zero here, but tcpdump also rejects 2^32 - 1. Try 2^16 - 1.
  // See http://seclists.org/tcpdump/2012/q2/8.
  idb.snaplen = 0xFFFF;
  Write(&idb, sizeof(idb));
  // write interface name, option = 0x0002
  WriteOption(kOptionInterfaceName, name.c_str(), static_cast<uint16_t>(name.length()));
  Write(&idb.blk_tot_len, sizeof(idb.blk_tot_len));
  return interface_counter_++;
}

void NetworkDump::WritePacket(const void* data, size_t len, uint32_t interface) {
  size_t padded_len = PAD(len);
  auto ts_usec = static_cast<uint64_t>(zx::clock::get_monotonic().get() / ZX_USEC(1));
  ts_usec = ((ts_usec & 0xFFFFFFFF) << 32) | (ts_usec >> 32);
  enhanced_pkt_t pkt = {
      kEnhancedPacketBlockType,                                   // type
      static_cast<uint32_t>(ENHANCED_PKT_MIN_SIZE + padded_len),  // blk_tot_len
      interface,                                                  // interface id
      ts_usec,                                                    // timestamp
      static_cast<uint32_t>(len),                                 // orig_len
      static_cast<uint32_t>(len),                                 // captured_len
  };

  Write(&pkt, sizeof(pkt));
  Write(data, len);
  if (padded_len > len) {
    size_t padding = padded_len - len;
    // pcap padding is a round up aligned to 32 bits.
    // if padding is not in interval [0,3] something went
    // terribly wrong.
    ZX_DEBUG_ASSERT(padding <= 3);
    static const uint32_t zero = 0;
    Write(&zero, padding);
  }
  Write(&pkt.blk_tot_len, sizeof(pkt.blk_tot_len));
  packet_count_++;
}

void InMemoryDump::DumpHex(std::ostream* out) const {
  auto str = mem_.str();
  const auto* buff = reinterpret_cast<const uint8_t*>(str.c_str());
  auto len = str.length();
  constexpr int cols = 50;
  int col = cols;
  while (len--) {
    *out << std::setw(2) << std::hex << std::setfill('0') << (int)*buff++;
    col--;
    if (col == 0) {
      col = cols;
      *out << std::endl;
    }
  }
  if (col != cols) {
    *out << std::endl;
  }
}

std::vector<uint8_t> InMemoryDump::CopyBytes() const {
  auto str = mem_.str();
  const auto* b = reinterpret_cast<const uint8_t*>(str.c_str());
  return std::vector<uint8_t>(b, b + str.length());
}

}  // namespace netemul

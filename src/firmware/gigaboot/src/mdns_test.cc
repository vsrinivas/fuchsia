// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mdns.h"

#include <zircon/compiler.h>

#include <cstring>
#include <string>
#include <tuple>
#include <utility>

#include <gtest/gtest.h>

#include "device_id.h"

__BEGIN_CDECLS
// Stub implementations of networking functions to make the compiler happy.
// These should never be called from the tests.

mac_addr ll_mac_addr;
ip6_addr ll_ip6_addr;
const ip6_addr ip6_ll_all_nodes{.x = {}};
const ip6_addr ip6_mdns_broadcast{.x = {}};
int udp6_send(const void *data, size_t len, const ip6_addr *daddr, uint16_t dport, uint16_t sport) {
  abort();
}
int netifc_timer_expired(void) { abort(); }
void device_id(mac_addr addr, char out[DEVICE_ID_MAX], uint32_t generation) { abort(); }
void netifc_set_timer(uint32_t ms) { abort(); }
__END_CDECLS

namespace {

// Header size, not including the variable-length name.
// 2 bytes type + 2 bytes class + 4 bytes TTL + 2 bytes length.
constexpr size_t kMdnsRecordHeaderSize = 10;

// 2 bytes priority + 2 bytes weight + 2 bytes port.
constexpr size_t kMdnsSrvHeaderSize = 6;

TEST(MdnsTest, TestWriteU16) {
  struct mdns_buf b = {
      .used = 0,
  };

  ASSERT_TRUE(mdns_write_u16(&b, 0xaabb));
  ASSERT_EQ(b.used, 2u);
  ASSERT_EQ(*(uint16_t *)b.data, htons(0xaabbu));
}

TEST(MdnsTest, TestWriteU32) {
  struct mdns_buf b = {
      .used = 0,
  };

  ASSERT_TRUE(mdns_write_u32(&b, 0x11223344));
  ASSERT_EQ(b.used, 4u);
  ASSERT_EQ(*(uint32_t *)b.data, htonl(0x11223344u));
}

TEST(MdnsTest, TestWriteSingleNameComponent) {
  struct mdns_buf b = {
      // Putting a string at offset 0 in the packet
      // would cause its loc to be set to 0, which would mean that
      // the string looks like it hasn't yet been inserted.
      //
      // This is OK in practice because an mDNS packet never starts with a name component.
      // Here, we workaround it by pretending the first byte of the packet has already been used.
      // This means that we can check that "loc" is set correctly.
      .used = 1,
  };

  struct mdns_name_segment seg = {
      .name = "test",
      .loc = 0,
      .next = nullptr,
  };

  ASSERT_TRUE(mdns_write_name(&b, &seg));
  std::string expected = "\x04test";
  ASSERT_EQ(b.used - 1, expected.length() + 1);
  ASSERT_EQ(reinterpret_cast<char *>(&b.data[1]), expected);
  ASSERT_EQ(seg.loc, 1);
}

TEST(MdnsTest, TestWriteMultipleNameComponents) {
  struct mdns_buf b = {
      .used = 1,
  };

  struct mdns_name_segment segs[] = {
      {
          .name = "hello",
          .loc = 0,
          .next = &segs[1],
      },
      {
          .name = "there",
          .loc = 0,
          .next = nullptr,
      },
  };

  std::string expected = "\x05hello\x05there";
  ASSERT_TRUE(mdns_write_name(&b, segs));
  ASSERT_EQ(b.used - 1, expected.length() + 1);
  ASSERT_EQ(reinterpret_cast<char *>(&b.data[1]), expected);

  ASSERT_EQ(segs[0].loc, 1);
  ASSERT_EQ(segs[1].loc, 7);
}

TEST(MdnsTest, TestWriteNameComponentWithLoc) {
  struct mdns_buf b = {
      .used = 0,
  };

  struct mdns_name_segment seg = {
      .name = "hello",
      .loc = 0xab,
      .next = nullptr,
  };

  ASSERT_TRUE(mdns_write_name(&b, &seg));
  ASSERT_EQ(*(uint16_t *)b.data, htons(0xab | MDNS_NAME_AT_OFFSET_FLAG));
}

TEST(MdnsTest, TestWriteRecord) {
  struct mdns_buf b = {
      .used = 1,
  };

  struct mdns_name_segment seg = {
      .name = "hi",
      .loc = 0,
      .next = nullptr,
  };
  struct mdns_record r = {
      .name = &seg,
      .type = MDNS_TYPE_PTR,
      .record_class = 0,
      .time_to_live = 0,
      .data =
          {
              .ptr =
                  {
                      .name = &seg,
                  },
          },
  };

  ASSERT_TRUE(mdns_write_record(&b, &r));

  size_t expected_len = 2 + strlen(seg.name);  // length byte + name bytes + null byte
  expected_len += sizeof(uint16_t) * 2;        // type, record_class
  expected_len += sizeof(uint32_t);            // time_to_live
  size_t record_data_offset = expected_len;
  expected_len += sizeof(uint16_t);  // record data length
  expected_len += sizeof(uint16_t);  // string back-reference

  ASSERT_EQ(b.used - 1, (uint16_t)expected_len);
  // All the other writes are tested above, so we mostly care that data length is calculated
  // correctly and in the right place.
  uint16_t data = 0;
  memcpy(&data, &b.data[record_data_offset + 1], sizeof(data));
  ASSERT_EQ(data, htons(sizeof(uint16_t)));
}

TEST(MdnsTest, TestNoSpaceLeft) {
  struct mdns_buf b = {
      .used = MDNS_MAX_PKT - 1,
  };
  struct mdns_name_segment seg = {
      .name = "hi",
      .loc = 0,
      .next = nullptr,
  };

  ASSERT_FALSE(mdns_write_u16(&b, 2));
  ASSERT_FALSE(mdns_write_u32(&b, 2));
  ASSERT_FALSE(mdns_write_name(&b, &seg));
}

// Reads a 16-bit big-endian value from a buffer.
uint16_t ReadU16(const uint8_t *buffer) {
  return static_cast<uint16_t>(buffer[0] << 8) + buffer[1];
}

// Reads a segmented name from an mDNS packet starting at |index|.
// Returns the resulting assembled string, and the index of the next item in
// the packet.
std::pair<std::string, size_t> ReadMdnsName(const mdns_buf &buf, size_t index) {
  std::pair<std::string, size_t> result;

  while (1) {
    // Safety check so we don't run off into the weeds if the packet is
    // malformed.
    if (index >= buf.used) {
      ADD_FAILURE() << "mDNS index " << index << " exceeds packet buffer";
      return result;
    }

    // If this is a pointer, jump to where it says.
    uint16_t ptr = ReadU16(&buf.data[index]);
    if (ptr & MDNS_NAME_AT_OFFSET_FLAG) {
      // If this is our first jump, save the next index, this will be
      // the next item in the packet.
      if (result.second == 0) {
        result.second = index + 2;
      }
      index = (ptr & ~MDNS_NAME_AT_OFFSET_FLAG);
      continue;
    }

    uint8_t length = buf.data[index];

    // If the length is 0, we're done.
    if (length == 0) {
      // If we never jumped, the next item follows our current index.
      if (result.second == 0) {
        result.second = index + 1;
      }
      return result;
    }

    // Otherwise append this segment and proceed to the next.
    const char *segment_start = reinterpret_cast<const char *>(&buf.data[index + 1]);
    if (!result.first.empty()) {
      result.first += ".";
    }
    result.first += std::string(segment_start, segment_start + length);
    index += length + 1;
  }
}

// Verifies that the mDNS packet looks like we expect for fastboot.
//
// We don't want to implement full mDNS packet parsing here, just check
// a few things to give us reasonable confidence that the packet is good.
//
// Args:
//   packet_buffer: buffer containing the mDNS packet.
//   protocol: expected protocol, "udp" or "tcp".
void VerifyFastbootMdnsPacket(const mdns_buf &packet_buffer, std::string protocol) {
  std::string ptr_name = "_fastboot._" + protocol + ".local";
  std::string srv_name = MDNS_DEFAULT_NODENAME_FOR_TEST "._fastboot._" + protocol + ".local";
  std::string aaaa_name = MDNS_DEFAULT_NODENAME_FOR_TEST ".local";

  // PTR packet should come first, pointing to the SRV name.
  auto [name, index] = ReadMdnsName(packet_buffer, sizeof(mdns_header));
  ASSERT_EQ(ptr_name, name);
  ASSERT_EQ(MDNS_TYPE_PTR, ReadU16(&packet_buffer.data[index]));
  std::tie(name, index) = ReadMdnsName(packet_buffer, index + kMdnsRecordHeaderSize);
  ASSERT_EQ(srv_name, name);

  // SRV should come next, pointing to the AAAA name.
  std::tie(name, index) = ReadMdnsName(packet_buffer, index);
  ASSERT_EQ(srv_name, name);
  ASSERT_EQ(MDNS_TYPE_SRV, ReadU16(&packet_buffer.data[index]));
  std::tie(name, index) =
      ReadMdnsName(packet_buffer, index + kMdnsRecordHeaderSize + kMdnsSrvHeaderSize);
  ASSERT_EQ(aaaa_name, name);

  // AAAA should come last.
  std::tie(name, index) = ReadMdnsName(packet_buffer, index);
  ASSERT_EQ(aaaa_name, name);
  ASSERT_EQ(MDNS_TYPE_AAAA, ReadU16(&packet_buffer.data[index]));
}

TEST(MdnsTest, TestWriteFastbootUdpPacket) {
  mdns_buf packet_buffer = {};

  ASSERT_TRUE(mdns_write_fastboot_packet(false, false, &packet_buffer));
  VerifyFastbootMdnsPacket(packet_buffer, "udp");
}

TEST(MdnsTest, TestWriteFastbootTcpPacket) {
  mdns_buf packet_buffer = {};

  ASSERT_TRUE(mdns_write_fastboot_packet(false, true, &packet_buffer));
  VerifyFastbootMdnsPacket(packet_buffer, "tcp");
}

}  // namespace

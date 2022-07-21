// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "src/bringup/bin/netsvc/inet6.h"

TEST(ChecksumTest, Checksum) {
  // Calculate the checksum of a valid Neighbor Solicitation packet. The full
  // packet from the Ethernet layer used for this example is:
  //
  // 33 33 FF 3D 78 5C F6 AD 04 5F 8F 68 86 DD 60 00 00 00 00 20 3A FF FE 80 00 00
  // 00 00 00 00 12 57 DF 14 68 B8 B6 1A FF 02 00 00 00 00 00 00 00 00 00 01 FF 3D
  // 78 5C 87 00 00 00 00 00 00 00 FE 80 00 00 00 00 00 00 50 B6 A1 FE FE 3D 78 5C
  // 01 01 F6 AD 04 5F 8F 68

  constexpr uint8_t kPayload[] = {0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x80, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0xB6, 0xA1, 0xFE, 0xFE, 0x3D,
                                  0x78, 0x5C, 0x01, 0x01, 0xF6, 0xAD, 0x04, 0x5F, 0x8F, 0x68};

  struct ip6_hdr hdr = {
      .length = htons(static_cast<uint16_t>(std::size(kPayload))),
      .src = {.u8 = {0xFE, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x57, 0xDF, 0x14, 0x68,
                     0xB8, 0xB6, 0x1A}},
      .dst = {.u8 = {0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF,
                     0x3D, 0x78, 0x5C}},

  };

  uint16_t header_csum = ip6_header_checksum(hdr, HDR_ICMP6);
  EXPECT_EQ(ip6_finalize_checksum(header_csum, kPayload, std::size(kPayload)), 0);
}

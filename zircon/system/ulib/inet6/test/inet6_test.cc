// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inet6/inet6.h>
#include <zxtest/zxtest.h>

// Provide missing symbols.
//
// The inet6 library seems to demand a number of different symbols that it does
// not provide on its own (nor does it provide weak aliases).  Depending on the
// build optimization settings, this can produce a failure to link in some
// situations.
//
// This needs to get cleaned up.  Until then, however, this stub implementation
// approach to satisfy the linker seems to be what other tests which include
// inet6 seem to be going with.  So, that is what we are doing here.
extern "C" {
void udp6_recv(void* data, size_t len, const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {}

void netifc_recv(void* data, size_t len) {}
bool netifc_send_pending() { return false; }
}

namespace {

TEST(Inet6TestCase, ip6toa) {
  struct TestVector {
    const uint8_t addr[IP6_ADDR_LEN];
    const char* const expected;
  };

  // Verify that |ip6toa| produces ascii encodings of IPv6 addresses in a
  // fashion which follows the rules laid out in RFC 1884 section 2.2
  //
  // Note that there are degrees of freedom in this encoding.  We do not test to
  // make sure that any valid encoding is being produced.  Instead we check to
  // make sure that the optimizations our implementation makes are present in
  // the encoding.  If these optimizations change, the tests will need updating.
  // Currently, we expect the following specific behaviors where there is
  // ambiguity.
  //
  // 1) The first run of zeros (if any) will be replaced by the "::" token.  No
  //    effort will be made to identify the longest run present in the address.
  // 2) Only lower case hex will be produced.
  // 3) Leading zeros of non-zero words will always be stripped.
  //
  // Remember that the addresses are made up of eight 16 bit words, and are
  // packed in network byte order (big-endian).
  constexpr std::array kTestVectors = {
      // All 0
      TestVector{.addr = {0x00}, .expected = "::"},
      // Ends with 0
      TestVector{.addr = {0x55, 0xAA, 0x00}, .expected = "55aa::"},
      // Starts with 0
      TestVector{.addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x55, 0xAA},
                 .expected = "::55aa"},
      // Zeros in the middle
      TestVector{.addr = {0xAB, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x55, 0xAA},
                 .expected = "ab54::55aa"},

      // Zeros in the middle, and both of the ends.
      TestVector{.addr = {0x00, 0x00, 0xAB, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x55, 0xAA, 0x00, 0x00},
                 .expected = "::ab54:0:0:0:0:55aa:0"},

      // More than one run of zeros in the middle.
      TestVector{.addr = {0xAB, 0x54, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x55, 0xAA},
                 .expected = "ab54::1111:0:0:0:55aa"},
      // words with leading zeros.
      TestVector{.addr = {0x01, 0x22, 0x00, 0x44, 0x00, 0x06, 0x00}, .expected = "122:44:6::"},
  };

  for (const auto& v : kTestVectors) {
    char rendered_addr[IP6TOAMAX] = {0};
    char* func_ret;

    func_ret = ::ip6toa(rendered_addr, v.addr);

    // The address we get back from ip6toa should always be the same as the
    // address we pass in.
    EXPECT_EQ(rendered_addr, func_ret);

    // Our rendered return address had better still be null terminated, at the
    // very end of the string at least.  If not, it is not safe to perform the
    // next check.
    ASSERT_EQ(0, rendered_addr[sizeof(rendered_addr) - 1]);

    // The string which has been rendered should match what we expect exactly.
    EXPECT_STR_EQ(v.expected, rendered_addr);
  }
}

}  // namespace

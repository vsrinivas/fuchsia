// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/align.h"

#include <gtest/gtest.h>

namespace debug_agent {

namespace {

std::string PrintOptional(const std::optional<debug::AddressRange>& opt) {
  if (!opt.has_value())
    return "<nullopt>";
  return opt->ToString();
}

bool VerifyRange(std::optional<debug::AddressRange> got,
                 std::optional<debug::AddressRange> expected) {
  if (got != expected) {
    ADD_FAILURE() << "Got: " << PrintOptional(got) << ", Expected: " << PrintOptional(expected);
    return false;
  }
  return true;
}

}  // namespace

TEST(AlignRange, AlignedRanges) {
  // 0 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x10}), std::nullopt));

  // 1 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x11}), debug::AddressRange(0x10, 0x11)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x12}), debug::AddressRange(0x11, 0x12)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x13}), debug::AddressRange(0x12, 0x13)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x14}), debug::AddressRange(0x13, 0x14)));

  // 2 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x12}), debug::AddressRange(0x10, 0x12)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x13}), debug::AddressRange(0x10, 0x14)));

  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x14}), debug::AddressRange(0x12, 0x14)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x15}), debug::AddressRange(0x10, 0x18)));

  ASSERT_TRUE(VerifyRange(AlignRange({0x14, 0x16}), debug::AddressRange(0x14, 0x16)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x15, 0x17}), debug::AddressRange(0x14, 0x18)));

  ASSERT_TRUE(VerifyRange(AlignRange({0x16, 0x18}), debug::AddressRange(0x16, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x17, 0x19}), std::nullopt));

  // 3 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x13}), debug::AddressRange(0x10, 0x14)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x14}), debug::AddressRange(0x10, 0x14)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x15}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x16}), debug::AddressRange(0x10, 0x18)));

  ASSERT_TRUE(VerifyRange(AlignRange({0x14, 0x17}), debug::AddressRange(0x14, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x15, 0x18}), debug::AddressRange(0x14, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x16, 0x19}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x17, 0x1a}), std::nullopt));

  ASSERT_TRUE(VerifyRange(AlignRange({0x18, 0x1b}), debug::AddressRange(0x18, 0x1c)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x19, 0x1c}), debug::AddressRange(0x18, 0x1c)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1a, 0x1d}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1b, 0x1e}), debug::AddressRange(0x18, 0x20)));

  // 4 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x14}), debug::AddressRange(0x10, 0x14)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x15}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x16}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x17}), debug::AddressRange(0x10, 0x18)));

  ASSERT_TRUE(VerifyRange(AlignRange({0x14, 0x18}), debug::AddressRange(0x14, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x15, 0x19}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x16, 0x1a}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x17, 0x1b}), std::nullopt));

  ASSERT_TRUE(VerifyRange(AlignRange({0x18, 0x1c}), debug::AddressRange(0x18, 0x1c)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x19, 0x1d}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1a, 0x1e}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1b, 0x1f}), debug::AddressRange(0x18, 0x20)));

  ASSERT_TRUE(VerifyRange(AlignRange({0x1c, 0x20}), debug::AddressRange(0x1c, 0x20)));

  // 5 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x15}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x16}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x17}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x18}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x14, 0x19}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x15, 0x1a}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x16, 0x1b}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x17, 0x1c}), std::nullopt));

  ASSERT_TRUE(VerifyRange(AlignRange({0x18, 0x1d}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x19, 0x1e}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1a, 0x1f}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1b, 0x20}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1c, 0x21}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1d, 0x22}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1e, 0x23}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1f, 0x24}), std::nullopt));

  // 6 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x16}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x17}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x18}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x19}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x14, 0x1a}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x15, 0x1b}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x16, 0x1c}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x17, 0x1d}), std::nullopt));

  ASSERT_TRUE(VerifyRange(AlignRange({0x18, 0x1e}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x19, 0x1f}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1a, 0x20}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1b, 0x21}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1c, 0x22}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1d, 0x23}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1e, 0x24}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1f, 0x25}), std::nullopt));

  // 7 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x17}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x18}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x19}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x1a}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x14, 0x1b}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x15, 0x1c}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x16, 0x1d}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x17, 0x1e}), std::nullopt));

  ASSERT_TRUE(VerifyRange(AlignRange({0x18, 0x1f}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x19, 0x20}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1a, 0x21}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1b, 0x22}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1c, 0x23}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1d, 0x24}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1e, 0x25}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1f, 0x26}), std::nullopt));

  // 8 byte range.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x18}), debug::AddressRange(0x10, 0x18)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x11, 0x19}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x12, 0x1a}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x13, 0x1b}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x14, 0x1c}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x15, 0x1d}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x16, 0x1e}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x17, 0x1f}), std::nullopt));

  ASSERT_TRUE(VerifyRange(AlignRange({0x18, 0x20}), debug::AddressRange(0x18, 0x20)));
  ASSERT_TRUE(VerifyRange(AlignRange({0x19, 0x21}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1a, 0x22}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1b, 0x23}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1c, 0x24}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1d, 0x25}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1e, 0x26}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x1f, 0x27}), std::nullopt));
}

TEST(AlignRange, InvalidRanges) {
  // Way to big.
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x19}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x1a}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x1b}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x1c}), std::nullopt));
  ASSERT_TRUE(VerifyRange(AlignRange({0x10, 0x1d}), std::nullopt));
}

}  // namespace debug_agent

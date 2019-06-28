// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fcs.h"

#include <zxtest/zxtest.h>

#include <vector>

namespace {

TEST(FcsTestCase, Blank) {
  std::string data;
  static constexpr uint16_t expect = 0xffff;

  const uint16_t fcs =
      ppp::Fcs(ppp::kFrameCheckSequenceInit,
               fbl::Span<const uint8_t>(
                   reinterpret_cast<const uint8_t*>(data.data()), data.size()));

  ASSERT_EQ(fcs, expect);
}

TEST(FcsTestCase, Single) {
  std::string data = "A";
  static constexpr uint16_t expect = 0x5c0a;

  const uint16_t fcs =
      ppp::Fcs(ppp::kFrameCheckSequenceInit,
               fbl::Span<const uint8_t>(
                   reinterpret_cast<const uint8_t*>(data.data()), data.size()));

  ASSERT_EQ(fcs, expect);
}

TEST(FcsTestCase, Digits) {
  std::string data = "0123456789";
  static constexpr uint16_t expect = 0xc3e9;

  const uint16_t fcs =
      ppp::Fcs(ppp::kFrameCheckSequenceInit,
               fbl::Span<const uint8_t>(
                   reinterpret_cast<const uint8_t*>(data.data()), data.size()));

  ASSERT_EQ(fcs, expect);
}

TEST(FcsTestCase, Alphabet) {
  std::string data = "abcdefghijklmnopqrstuvwxyz";
  static constexpr uint16_t expect = 0xf2bc;

  const uint16_t fcs =
      ppp::Fcs(ppp::kFrameCheckSequenceInit,
               fbl::Span<const uint8_t>(
                   reinterpret_cast<const uint8_t*>(data.data()), data.size()));

  ASSERT_EQ(fcs, expect);
}

}  // namespace

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"

#include <gtest/gtest.h>

namespace overnet {
namespace unreliable_framer_test {

struct Param {
  Slice input;
  std::vector<Slice> output;
};

std::ostream& operator<<(std::ostream& out, const Param& param) {
  out << param.input << " --> {";
  bool first = true;
  for (const auto& output : param.output) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << output;
  }
  out << "}";
  return out;
}

struct UnreliableFramerTest : public ::testing::TestWithParam<Param> {};

TEST_P(UnreliableFramerTest, UnframesCorrectly_AtOnce) {
  UnreliableFramer framer;
  framer.Push(GetParam().input);
  for (const auto& expect : GetParam().output) {
    while (true) {
      auto frame = framer.Pop();
      ASSERT_TRUE(frame.is_ok()) << frame;
      if (frame->has_value()) {
        EXPECT_EQ(expect, **frame);
        break;  // from while loop
      } else {
        // No frame ready: skip any noise (simulates timeout), try again.
        EXPECT_TRUE(framer.SkipNoise().has_value());
      }
    }
  }
  auto frame = framer.Pop();
  ASSERT_TRUE(frame.is_ok());
  ASSERT_FALSE(frame->has_value());
}

TEST_P(UnreliableFramerTest, UnframesCorrectly_OneByteAtATime) {
  UnreliableFramer framer;
  auto expect_it = GetParam().output.begin();
  auto expect_end = GetParam().output.end();
  for (auto c : GetParam().input) {
    framer.Push(Slice::RepeatedChar(1, c));
    auto frame = framer.Pop();
    ASSERT_TRUE(frame.is_ok());
    if (expect_it == expect_end) {
      EXPECT_FALSE(frame->has_value());
    } else if (frame->has_value()) {
      EXPECT_EQ(*expect_it, **frame);
      ++expect_it;
    } else {
      // nothing to do
    }
  }
  while (expect_it != expect_end) {
    EXPECT_TRUE(framer.SkipNoise().has_value());
    while (expect_it != expect_end) {
      auto frame = framer.Pop();
      ASSERT_TRUE(frame.is_ok()) << frame;
      if (frame->has_value()) {
        EXPECT_EQ(*expect_it, **frame);
        ++expect_it;
      } else {
        // No frame ready: skip any noise (simulates timeout), try again.
        break;  // from while loop
      }
    }
  }
  EXPECT_EQ(size_t(expect_it - GetParam().output.begin()),
            GetParam().output.size());
}

INSTANTIATE_TEST_SUITE_P(
    UnreliableFramerSuite, UnreliableFramerTest,
    ::testing::Values(
        // Simple correct frame
        Param{Slice::FromContainer({'\n', 2, 'a', 'b', 'c', 0xc2, 0x41, 0x24,
                                    0x35}),
              {Slice::FromContainer({'a', 'b', 'c'})}},
        // Correct frame prefixed with noise, and suffixed with noise
        Param{Slice::FromContainer({'h', 'e', 'l', 'l', 'o', '\n', 2, 'a', 'b',
                                    'c', 0xc2, 0x41, 0x24, 0x35, '\n'}),
              {Slice::FromContainer({'a', 'b', 'c'})}},
        // Badly formed frame (incorrect CRC)
        Param{Slice::FromContainer({'\n', 2, 'a', 'b', 'c', 0xc2, 0x41, 0x00,
                                    0x35}),
              {}},
        // Correct frame prefixed with noise, and suffixed with noise, then a
        // new frame
        Param{Slice::FromContainer({'h',  'e', 'l',  'l',  'o',  '\n', 2,
                                    'a',  'b', 'c',  0xc2, 0x41, 0x24, 0x35,
                                    '\n', 'b', 'o',  'b',  '\n', 2,    'a',
                                    'b',  'c', 0xc2, 0x41, 0x24, 0x35}),
              {Slice::FromContainer({'a', 'b', 'c'}),
               Slice::FromContainer({'a', 'b', 'c'})}}));

}  // namespace unreliable_framer_test
}  // namespace overnet

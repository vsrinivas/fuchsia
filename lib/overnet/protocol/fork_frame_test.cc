// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fork_frame.h"
#include <vector>
#include "gtest/gtest.h"

namespace overnet {
namespace fork_frame_test {

void RoundTrip(const ForkFrame& h, const std::vector<uint8_t>& expect) {
  auto e = h.Write(Border::None());
  EXPECT_EQ(Slice::FromCopiedBuffer(expect.data(), expect.size()), e);
  auto p = ForkFrame::Parse(e);
  ASSERT_TRUE(p.is_ok());
  EXPECT_EQ(h, *p.get());
}

TEST(ForkFrame, SomeFrame) {
  RoundTrip(ForkFrame(StreamId(1), ReliabilityAndOrdering::ReliableOrdered,
                      Introduction()),
            std::vector<uint8_t>{1, 1});
}

}  // namespace fork_frame_test
}  // namespace overnet
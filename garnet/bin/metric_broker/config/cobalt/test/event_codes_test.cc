// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace broker_service::cobalt {
namespace {

TEST(EventCodesTest, SparseConstruction) {
  std::array<EventCodes::CodeEntry, 2> entries = {EventCodes::CodeEntry(0, 1),
                                                  EventCodes::CodeEntry(3, 2)};
  EventCodes event_codes(entries.data(), entries.size());

  EXPECT_EQ(event_codes.codes[0], 1);
  EXPECT_EQ(event_codes.codes[1], std::nullopt);
  EXPECT_EQ(event_codes.codes[2], std::nullopt);
  EXPECT_EQ(event_codes.codes[3], 2);
  EXPECT_EQ(event_codes.codes[4], std::nullopt);
}

}  // namespace
}  // namespace broker_service::cobalt

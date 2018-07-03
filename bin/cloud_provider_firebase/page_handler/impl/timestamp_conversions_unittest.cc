// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/timestamp_conversions.h"

#include <lib/fxl/time/time_delta.h>

#include "gtest/gtest.h"

namespace cloud_provider_firebase {

TEST(TimestampConversions, BackAndForth) {
  int64_t timestamp = 0;
  EXPECT_EQ(timestamp,
            BytesToServerTimestamp(ServerTimestampToBytes(timestamp)));

  timestamp = 42;
  EXPECT_EQ(timestamp,
            BytesToServerTimestamp(ServerTimestampToBytes(timestamp)));

  timestamp = fxl::TimeDelta::FromSeconds(42).ToMilliseconds();
  EXPECT_EQ(timestamp,
            BytesToServerTimestamp(ServerTimestampToBytes(timestamp)));

  timestamp = fxl::TimeDelta::FromSeconds(42 * 60).ToMilliseconds();
  EXPECT_EQ(timestamp,
            BytesToServerTimestamp(ServerTimestampToBytes(timestamp)));

  timestamp = fxl::TimeDelta::FromSeconds(42 * 60 * 60).ToMilliseconds();
  EXPECT_EQ(timestamp,
            BytesToServerTimestamp(ServerTimestampToBytes(timestamp)));

  timestamp = fxl::TimeDelta::FromSeconds(42 * 60 * 60 * 24).ToMilliseconds();
  EXPECT_EQ(timestamp,
            BytesToServerTimestamp(ServerTimestampToBytes(timestamp)));

  timestamp =
      fxl::TimeDelta::FromSeconds(42 * 60 * 60 * 24 * 365).ToMilliseconds();
  EXPECT_EQ(timestamp,
            BytesToServerTimestamp(ServerTimestampToBytes(timestamp)));
}

}  // namespace cloud_provider_firebase

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/inspect/snapshot_config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace broker_service::inspect {
namespace {

TEST(SnapshotConfigTest, RequireConsistencyCheck) {
  SnapshotConfig config(true);

  ASSERT_TRUE(config.ShouldRequireConsistenfencyCheck());
}

TEST(SnapshotConfigTest, DontRequireConsistencyCheck) {
  SnapshotConfig config(false);

  ASSERT_FALSE(config.ShouldRequireConsistenfencyCheck());
}

}  // namespace
}  // namespace broker_service::inspect

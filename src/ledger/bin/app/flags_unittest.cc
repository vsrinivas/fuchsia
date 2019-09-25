// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/flags.h"

#include <string>
#include <vector>

#include "fuchsia/sys/cpp/fidl.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/command_line.h"

namespace ledger {
namespace {

// Check that FromFlags and ToFlags round-trip.
TEST(Flags, GarbageCollectionFromTo) {
  fuchsia::sys::LaunchInfo launch_info;
  AppendGarbageCollectionPolicyFlags(storage::GarbageCollectionPolicy::EAGER_LIVE_REFERENCES,
                                     &launch_info);
  fxl::CommandLine command_line = fxl::CommandLineFromIteratorsWithArgv0(
      "ledger", launch_info.arguments->begin(), launch_info.arguments->end());
  EXPECT_EQ(GarbageCollectionPolicyFromFlags(command_line),
            storage::GarbageCollectionPolicy::EAGER_LIVE_REFERENCES);

  // Do not reset |launch_info|: flags should be appended, and the latest value should win.
  AppendGarbageCollectionPolicyFlags(storage::GarbageCollectionPolicy::NEVER, &launch_info);
  command_line = fxl::CommandLineFromIteratorsWithArgv0("ledger", launch_info.arguments->begin(),
                                                        launch_info.arguments->end());
  EXPECT_EQ(GarbageCollectionPolicyFromFlags(command_line),
            storage::GarbageCollectionPolicy::NEVER);
}

// Check that no flag returns the default policy.
TEST(Flags, GarbageCollectionDefault) {
  EXPECT_EQ(GarbageCollectionPolicyFromFlags(fxl::CommandLine()), kDefaultGarbageCollectionPolicy);
}

}  // namespace
}  // namespace ledger

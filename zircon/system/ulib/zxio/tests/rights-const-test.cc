// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>

#include <zxtest/zxtest.h>

namespace {

namespace fio2 = fuchsia_io2;

// Since fidl does not support operations for constants this test ensures
// the pre-calculated constants in fuchsia.io2/rights-abilities.fidl match the
// expected set of rights.
TEST(RightsConstTest, VerifyConstCalculations) {
  using fio2::wire::Operations;
  static_assert(fio2::wire::kRStarDir ==
                    static_cast<uint64_t>(Operations::kConnect | Operations::kEnumerate |
                                          Operations::kTraverse | Operations::kReadBytes |
                                          Operations::kGetAttributes),
                "Operations::R_STAR does not match expected value");
  static_assert(
      fio2::wire::kRwStarDir ==
          static_cast<uint64_t>(Operations::kConnect | Operations::kEnumerate |
                                Operations::kTraverse | Operations::kReadBytes |
                                Operations::kWriteBytes | Operations::kModifyDirectory |
                                Operations::kGetAttributes | Operations::kUpdateAttributes),
      "Operations::RW_STAR_DIR does not match expected value");
  static_assert(fio2::wire::kRxStarDir ==
                    static_cast<uint64_t>(Operations::kConnect | Operations::kEnumerate |
                                          Operations::kTraverse | Operations::kReadBytes |
                                          Operations::kGetAttributes | Operations::kExecute),
                "Operations::RX_STAR_DIR does not match expected value");
  static_assert(
      fio2::wire::kWStarDir ==
          static_cast<uint64_t>(Operations::kConnect | Operations::kEnumerate |
                                Operations::kTraverse | Operations::kWriteBytes |
                                Operations::kModifyDirectory | Operations::kUpdateAttributes),
      "Operations::W_STAR_DIR does not match expected value");
  static_assert(
      fio2::wire::kXStarDir == static_cast<uint64_t>(Operations::kConnect | Operations::kEnumerate |
                                                     Operations::kTraverse | Operations::kExecute),
      "Operations::X_STAR_DIR does not match expected value");
}

}  // namespace

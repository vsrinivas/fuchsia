// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>

#include <zxtest/zxtest.h>

namespace {

namespace fio = fuchsia_io;

// Since fidl does not support operations for constants this test ensures
// the pre-calculated constants in fuchsia.io/rights-abilities.fidl match the
// expected set of rights.
TEST(RightsConstTest, VerifyConstCalculations) {
  using fio::wire::Operations;
  static_assert(fio::wire::kRStarDir ==
                    (Operations::kConnect | Operations::kEnumerate | Operations::kTraverse |
                     Operations::kReadBytes | Operations::kGetAttributes),
                "Operations::R_STAR does not match expected value");
  static_assert(
      fio::wire::kRwStarDir ==
          (Operations::kConnect | Operations::kEnumerate | Operations::kTraverse |
           Operations::kReadBytes | Operations::kWriteBytes | Operations::kModifyDirectory |
           Operations::kGetAttributes | Operations::kUpdateAttributes),
      "Operations::RW_STAR_DIR does not match expected value");
  static_assert(fio::wire::kRxStarDir ==
                    (Operations::kConnect | Operations::kEnumerate | Operations::kTraverse |
                     Operations::kReadBytes | Operations::kGetAttributes | Operations::kExecute),
                "Operations::RX_STAR_DIR does not match expected value");
  static_assert(
      fio::wire::kWStarDir ==
          (Operations::kConnect | Operations::kEnumerate | Operations::kTraverse |
           Operations::kWriteBytes | Operations::kModifyDirectory | Operations::kUpdateAttributes),
      "Operations::W_STAR_DIR does not match expected value");
  static_assert(fio::wire::kXStarDir == (Operations::kConnect | Operations::kEnumerate |
                                         Operations::kTraverse | Operations::kExecute),
                "Operations::X_STAR_DIR does not match expected value");
}

}  // namespace

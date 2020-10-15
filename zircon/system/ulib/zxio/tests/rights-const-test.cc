// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>

#include <zxtest/zxtest.h>

namespace {

namespace fio2 = ::llcpp::fuchsia::io2;

// Since fidl does not support operations for constants this test ensures
// the pre-calculated constants in fuchsia.io2/rights-abilities.fidl match the
// expected set of rights.
TEST(RightsConstTest, VerifyConstCalculations) {
  static_assert(fio2::R_STAR_DIR == static_cast<uint64_t>(
                                        fio2::Operations::CONNECT | fio2::Operations::ENUMERATE |
                                        fio2::Operations::TRAVERSE | fio2::Operations::READ_BYTES |
                                        fio2::Operations::GET_ATTRIBUTES),
                "fio2::Operations::R_STAR does not match expected value");
  static_assert(fio2::RW_STAR_DIR ==
                    static_cast<uint64_t>(
                        fio2::Operations::CONNECT | fio2::Operations::ENUMERATE |
                        fio2::Operations::TRAVERSE | fio2::Operations::READ_BYTES |
                        fio2::Operations::WRITE_BYTES | fio2::Operations::MODIFY_DIRECTORY |
                        fio2::Operations::GET_ATTRIBUTES | fio2::Operations::UPDATE_ATTRIBUTES),
                "fio2::Operations::RW_STAR_DIR does not match expected value");
  static_assert(
      fio2::RX_STAR_DIR ==
          static_cast<uint64_t>(fio2::Operations::CONNECT | fio2::Operations::ENUMERATE |
                                fio2::Operations::TRAVERSE | fio2::Operations::READ_BYTES |
                                fio2::Operations::GET_ATTRIBUTES | fio2::Operations::EXECUTE),
      "fio2::Operations::RX_STAR_DIR does not match expected value");
  static_assert(fio2::W_STAR_DIR ==
                    static_cast<uint64_t>(
                        fio2::Operations::CONNECT | fio2::Operations::ENUMERATE |
                        fio2::Operations::TRAVERSE | fio2::Operations::WRITE_BYTES |
                        fio2::Operations::MODIFY_DIRECTORY | fio2::Operations::UPDATE_ATTRIBUTES),
                "fio2::Operations::W_STAR_DIR does not match expected value");
  static_assert(fio2::X_STAR_DIR ==
                    static_cast<uint64_t>(fio2::Operations::CONNECT | fio2::Operations::ENUMERATE |
                                          fio2::Operations::TRAVERSE | fio2::Operations::EXECUTE),
                "fio2::Operations::X_STAR_DIR does not match expected value");
}

}  // namespace

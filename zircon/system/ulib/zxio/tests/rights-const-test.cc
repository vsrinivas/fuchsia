// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>

#include <zxtest/zxtest.h>

namespace {

namespace fio2 = ::fuchsia_io2;

// Since fidl does not support operations for constants this test ensures
// the pre-calculated constants in fuchsia.io2/rights-abilities.fidl match the
// expected set of rights.
TEST(RightsConstTest, VerifyConstCalculations) {
  using fio2::wire::Operations;
  static_assert(
      fio2::wire::R_STAR_DIR ==
          static_cast<uint64_t>(Operations::CONNECT | Operations::ENUMERATE | Operations::TRAVERSE |
                                Operations::READ_BYTES | Operations::GET_ATTRIBUTES),
      "Operations::R_STAR does not match expected value");
  static_assert(
      fio2::wire::RW_STAR_DIR ==
          static_cast<uint64_t>(Operations::CONNECT | Operations::ENUMERATE | Operations::TRAVERSE |
                                Operations::READ_BYTES | Operations::WRITE_BYTES |
                                Operations::MODIFY_DIRECTORY | Operations::GET_ATTRIBUTES |
                                Operations::UPDATE_ATTRIBUTES),
      "Operations::RW_STAR_DIR does not match expected value");
  static_assert(fio2::wire::RX_STAR_DIR ==
                    static_cast<uint64_t>(Operations::CONNECT | Operations::ENUMERATE |
                                          Operations::TRAVERSE | Operations::READ_BYTES |
                                          Operations::GET_ATTRIBUTES | Operations::EXECUTE),
                "Operations::RX_STAR_DIR does not match expected value");
  static_assert(
      fio2::wire::W_STAR_DIR ==
          static_cast<uint64_t>(Operations::CONNECT | Operations::ENUMERATE | Operations::TRAVERSE |
                                Operations::WRITE_BYTES | Operations::MODIFY_DIRECTORY |
                                Operations::UPDATE_ATTRIBUTES),
      "Operations::W_STAR_DIR does not match expected value");
  static_assert(
      fio2::wire::X_STAR_DIR == static_cast<uint64_t>(Operations::CONNECT | Operations::ENUMERATE |
                                                      Operations::TRAVERSE | Operations::EXECUTE),
      "Operations::X_STAR_DIR does not match expected value");
}

}  // namespace

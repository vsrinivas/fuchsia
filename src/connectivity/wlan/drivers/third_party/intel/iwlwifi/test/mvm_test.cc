// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "gtest/gtest.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan {
namespace testing {
namespace {

class MvmTest : public SingleApTest {
 public:
  MvmTest() {}
  ~MvmTest() {}
};

TEST_F(MvmTest, GetMvm) { EXPECT_NE(iwl_trans_get_mvm(&trans_), nullptr); }

}  // namespace
}  // namespace testing
}  // namespace wlan

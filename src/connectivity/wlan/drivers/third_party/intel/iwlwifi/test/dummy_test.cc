// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Feel free to clone this file to work on a new test.

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

// To test helper functions only.
//
// This is used when you only need to test some hardware-independent functions.
// Easy to use. Just call the function you want to test.
//
class DummyTest : public ::zxtest::Test {
 public:
  DummyTest() {}
  ~DummyTest() {}
};

TEST_F(DummyTest, DummyTestFunction) {}

// For more complicated cases that requires the simulated firmware/hardware/
// environment.
//
// This example code uses the SimSingleAp class, which creates an virtual
// AP in the environment so that we can test the client driver.
//
class MvmTest : public SingleApTest {
 public:
  MvmTest() {}
  ~MvmTest() {}
};

TEST_F(MvmTest, MvmTestFunction) {}

}  // namespace
}  // namespace wlan::testing

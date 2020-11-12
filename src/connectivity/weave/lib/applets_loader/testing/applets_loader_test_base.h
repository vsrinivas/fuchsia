// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_TESTING_APPLETS_LOADER_TEST_BASE_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_TESTING_APPLETS_LOADER_TEST_BASE_H_

#include <gtest/gtest.h>

#include "src/connectivity/weave/applets/test_applets/test_applets.h"
#include "src/connectivity/weave/lib/applets_loader/applets_loader.h"
#include "src/connectivity/weave/lib/applets_loader/testing/test_applets.h"

namespace weavestack::applets::testing {

// The |AppletsLoaderTestBase| is a test fixture that enables tests using the 'test_applets.so'
// module. This module provides 2 exports; the standard Fuchsia Weave Applet ABI that allows the
// plugin to function with the Fuchsia Weave stack, and an additional 'test applets extension'
// ABI which is an ABI defined by the test_applets module to allow tests to control the behavior
// of the Fuchsia Weave Applets implementation.
class AppletsLoaderTestBase : public ::testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  const TestAppletsModule& test_applets() { return test_applets_; }
  AppletsLoader* applets_loader() { return applets_loader_.get(); }

  void RecreateLoader();

 private:
  std::unique_ptr<AppletsLoader> applets_loader_;
  TestAppletsModule test_applets_ = TestAppletsModule::Open();
};

}  // namespace weavestack::applets::testing

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_TESTING_APPLETS_LOADER_TEST_BASE_H_

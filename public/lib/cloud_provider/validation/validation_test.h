// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_
#define LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/cloud_provider/validation/types.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"

namespace cloud_provider {

class ValidationTest : public ::gtest::TestLoopFixture {
 public:
  ValidationTest();
  ~ValidationTest() override;

  void SetUp() override;

 protected:
  CloudProviderSyncPtr cloud_provider_;

 private:
  std::unique_ptr<component::StartupContext> startup_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ValidationTest);
};

}  // namespace cloud_provider

#endif  // LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_

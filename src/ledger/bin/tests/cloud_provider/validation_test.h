// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_VALIDATION_TEST_H_
#define SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_VALIDATION_TEST_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include "src/ledger/bin/tests/cloud_provider/types.h"
#include "src/lib/fxl/macros.h"

namespace cloud_provider {

class ValidationTest : public ::gtest::TestLoopFixture {
 public:
  ValidationTest();
  ~ValidationTest() override;

  void SetUp() override;

 protected:
  CloudProviderSyncPtr cloud_provider_;

 private:
  std::unique_ptr<sys::ComponentContext> component_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ValidationTest);
};

}  // namespace cloud_provider

#endif  // SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_VALIDATION_TEST_H_

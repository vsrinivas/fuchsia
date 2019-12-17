// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_VALIDATION_TEST_H_
#define SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_VALIDATION_TEST_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <gtest/gtest.h>

#include "src/ledger/bin/tests/cloud_provider/types.h"
#include "src/ledger/lib/loop_fixture/test_loop_fixture.h"
#include "src/ledger/lib/rng/test_random.h"

namespace cloud_provider {

class ValidationTest : public ledger::TestLoopFixture {
 public:
  ValidationTest();
  ValidationTest(const ValidationTest&) = delete;
  ValidationTest& operator=(const ValidationTest&) = delete;
  ~ValidationTest() override;

  void SetUp() override;

 protected:
  CloudProviderSyncPtr cloud_provider_;
  std::vector<uint8_t> GetUniqueRandomId();

 private:
  std::unique_ptr<sys::ComponentContext> component_context_;
  ledger::TestRandom random_;
};

}  // namespace cloud_provider

#endif  // SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_VALIDATION_TEST_H_

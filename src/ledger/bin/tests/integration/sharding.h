// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_INTEGRATION_SHARDING_H_
#define SRC_LEDGER_BIN_TESTS_INTEGRATION_SHARDING_H_

namespace ledger {

enum class IntegrationTestShard {
  // Run all tests except the tests for diff compatibility.
  ALL_EXCEPT_DIFF_COMPATIBILITY,
  // Run the tests for diff compatibility.
  DIFF_COMPATIBILITY,
};

// This symbol is provided by one of the sharding_XXX.cc files that should be linked together with
// the integration tests.
IntegrationTestShard GetIntegrationTestShard();

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTS_INTEGRATION_SHARDING_H_

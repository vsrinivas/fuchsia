// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_TESTING_INTEGRATION_TEST_H_
#define EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_TESTING_INTEGRATION_TEST_H_

#include <fuchsia/examples/inspect/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/realm_builder.h>

namespace codelab::testing {

class IntegrationTest : public gtest::RealLoopFixture {
 public:
  // Options for each test.
  struct TestOptions {
    // When true, a real fizzbuzz component is started.
    // When false, a mocked fizzbuzz component that closes requests is started.
    bool include_fizzbuzz;
  };

  // Creates the test topology with reverser and fizzbuzz and returns a
  // connection to the Reverser protocol.
  fuchsia::examples::inspect::ReverserPtr ConnectToReverser(TestOptions options);

  // Returns the moniker of the reverser components. This moniker is escaped
  // properly for use in selectors.
  std::string ReverserMonikerForSelectors() const;

 private:
  std::optional<sys::testing::Realm> realm_;
};

};  // namespace codelab::testing

#endif  // EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_TESTING_INTEGRATION_TEST_H_

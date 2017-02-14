// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple Fuchsia program that connects to the test_runner process,
// starts a test and exits with success or failure based on the success or
// failure of the test.

#include "apps/modular/src/test_runner/test_runner_client.h"

constexpr char kModularTestsJson[] =
    "/system/apps/modular_tests/modular_tests.json";

int main(int argc, char** argv) {
  modular::testing::TestRunnerClient client;

  return client.RunTests(kModularTestsJson) ? 0 : 1;
}

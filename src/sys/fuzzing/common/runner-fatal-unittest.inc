// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file allows derived runners to quickly specialize the generic |Runner| unit tests.
// See src/sys/fuzzing/common/runner-unittest.h for details.

#ifndef RUNNER_TYPE
#error "RUNNER_TYPE must be defined before including this file."
#endif

#ifndef RUNNER_TEST
#error "RUNNER_TEST must be defined before including this file."
#endif

// These tests include intentional crashes that produce FATAL logs. They must be run in a test
// environment that does not interpret those logs as test failures.
// See also runner-unittest.inc.

TEST_F(RUNNER_TEST, MinimizeOneByte) {
  RUNNER_TYPE runner;
  MinimizeOneByte(&runner);
}

TEST_F(RUNNER_TEST, MinimizeReduceByTwo) {
  RUNNER_TYPE runner;
  MinimizeReduceByTwo(&runner);
}

TEST_F(RUNNER_TEST, MinimizeNewError) {
  RUNNER_TYPE runner;
  MinimizeNewError(&runner);
}

TEST_F(RUNNER_TEST, CleanseTwoBytes) {
  RUNNER_TYPE runner;
  CleanseTwoBytes(&runner);
}

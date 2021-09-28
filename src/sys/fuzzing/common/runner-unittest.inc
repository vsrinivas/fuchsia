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

// These tests do not include any intentional crashes and may be run in a normal test environment.
// See also runner-fatal-unittest.inc.

TEST_F(RUNNER_TEST, ExecuteNoError) {
  RUNNER_TYPE runner;
  ExecuteNoError(&runner);
}

TEST_F(RUNNER_TEST, ExecuteWithError) {
  RUNNER_TYPE runner;
  ExecuteWithError(&runner);
}

TEST_F(RUNNER_TEST, ExecuteWithLeak) {
  RUNNER_TYPE runner;
  ExecuteWithLeak(&runner);
}

TEST_F(RUNNER_TEST, MinimizeNoError) {
  RUNNER_TYPE runner;
  MinimizeNoError(&runner);
}

TEST_F(RUNNER_TEST, CleanseNoError) {
  RUNNER_TYPE runner;
  CleanseNoError(&runner);
}

TEST_F(RUNNER_TEST, CleanseNoReplacement) {
  RUNNER_TYPE runner;
  CleanseNoReplacement(&runner);
}

TEST_F(RUNNER_TEST, CleanseAlreadyClean) {
  RUNNER_TYPE runner;
  CleanseAlreadyClean(&runner);
}

TEST_F(RUNNER_TEST, FuzzUntilError) {
  RUNNER_TYPE runner;
  FuzzUntilError(&runner);
}

TEST_F(RUNNER_TEST, FuzzUntilRuns) {
  RUNNER_TYPE runner;
  FuzzUntilRuns(&runner);
}

TEST_F(RUNNER_TEST, FuzzUntilTime) {
  RUNNER_TYPE runner;
  FuzzUntilTime(&runner);
}

TEST_F(RUNNER_TEST, MergeSeedError) {
  RUNNER_TYPE runner;
  MergeSeedError(&runner);
}

TEST_F(RUNNER_TEST, Merge) {
  RUNNER_TYPE runner;
  Merge(&runner);
}

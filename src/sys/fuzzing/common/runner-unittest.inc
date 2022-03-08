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
  auto runner = RUNNER_TYPE::MakePtr(executor());
  ExecuteNoError(runner);
}

TEST_F(RUNNER_TEST, ExecuteWithError) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  ExecuteWithError(runner);
}

TEST_F(RUNNER_TEST, ExecuteWithLeak) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  ExecuteWithLeak(runner);
}

TEST_F(RUNNER_TEST, MinimizeNoError) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  MinimizeNoError(runner);
}

TEST_F(RUNNER_TEST, CleanseNoReplacement) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  CleanseNoReplacement(runner);
}

TEST_F(RUNNER_TEST, CleanseAlreadyClean) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  CleanseAlreadyClean(runner);
}

TEST_F(RUNNER_TEST, FuzzUntilError) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  FuzzUntilError(runner);
}

TEST_F(RUNNER_TEST, FuzzUntilRuns) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  FuzzUntilRuns(runner);
}

TEST_F(RUNNER_TEST, FuzzUntilTime) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  FuzzUntilTime(runner);
}

TEST_F(RUNNER_TEST, Stop) {
  auto runner = RUNNER_TYPE::MakePtr(executor());
  Stop(runner);
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/process.h"

#include <string>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixtures.

const char* kEcho = "/pkg/bin/libfuzzer_test_echo";

class ProcessTest : public AsyncTest {};

// Unit tests.

TEST_F(ProcessTest, SpawnFailure) {
  Process process(executor());
  process.set_verbose(true);
  FUZZING_EXPECT_ERROR(process.Spawn({"bogus"}), ZX_ERR_NOT_FOUND);
  RunUntilIdle();

  // Spawn failure leaves process in a "killed" state.
  FUZZING_EXPECT_ERROR(process.Spawn({kEcho}), ZX_ERR_BAD_STATE);
  RunUntilIdle();

  // Can respawn after reset.
  process.Reset();
  FUZZING_EXPECT_OK(process.Spawn({kEcho}));
  RunUntilIdle();

  // Cannot spawn when spawned.
  FUZZING_EXPECT_ERROR(process.Spawn({kEcho}), ZX_ERR_BAD_STATE);
  RunUntilIdle();
}

TEST_F(ProcessTest, ReadFromStdout) {
  Process process(executor());
  process.SetStderrSpawnAction(kClone);

  std::string hello("hello");
  std::string world("world");
  std::string input = hello + "\n" + world;

  FUZZING_EXPECT_OK(process.ReadFromStdout(), hello);
  FUZZING_EXPECT_OK(process.ReadFromStdout(), world);
  FUZZING_EXPECT_OK(process.WriteAndCloseStdin(input.data(), input.size()), input.size());
  FUZZING_EXPECT_OK(process.Spawn({kEcho, "--stdout"}));
  RunUntilIdle();
}

TEST_F(ProcessTest, ReadFromStderr) {
  Process process(executor());
  process.SetStdoutSpawnAction(kClone);

  std::string hello("hello");
  std::string world("world");
  std::string input = hello + "\n" + world;

  FUZZING_EXPECT_OK(process.ReadFromStderr(), hello);
  FUZZING_EXPECT_OK(process.ReadFromStderr(), world);
  FUZZING_EXPECT_OK(process.WriteAndCloseStdin(input.data(), input.size()), input.size());
  FUZZING_EXPECT_OK(process.Spawn({kEcho, "--stderr"}));
  RunUntilIdle();
}

TEST_F(ProcessTest, Kill) {
  Process process(executor());
  FUZZING_EXPECT_OK(process.Spawn({kEcho, "--stdout", "--stderr"}));

  std::string input("hello\nworld");
  FUZZING_EXPECT_OK(process.WriteToStdin(input.data(), input.size()), input.size());
  RunUntilIdle();

  FUZZING_EXPECT_OK(process.Kill());
  RunUntilIdle();

  // Cannot respawn until reset.
  FUZZING_EXPECT_ERROR(process.Spawn({kEcho}), ZX_ERR_BAD_STATE);
  FUZZING_EXPECT_ERROR(process.WriteToStdin(input.data(), input.size()));
  FUZZING_EXPECT_ERROR(process.ReadFromStdout());
  FUZZING_EXPECT_ERROR(process.ReadFromStderr());
  RunUntilIdle();

  // Can respawn after reset.
  process.Reset();
  FUZZING_EXPECT_OK(process.Spawn({kEcho}));
  RunUntilIdle();
}

}  // namespace fuzzing

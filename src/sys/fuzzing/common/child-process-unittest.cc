// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/child-process.h"

#include <string>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixtures.

const char* kEcho = "bin/fuzzing_echo_for_test";

class ChildProcessTest : public AsyncTest {};

// Unit tests.

TEST_F(ChildProcessTest, Spawn) {
  ChildProcess process(executor());
  process.AddArg("bogus");
  FUZZING_EXPECT_ERROR(process.SpawnAsync(), ZX_ERR_NOT_FOUND);
  RunUntilIdle();

  // Can respawn after reset.
  process.Reset();
  process.AddArg(kEcho);
  FUZZING_EXPECT_OK(process.SpawnAsync());
  RunUntilIdle();

  // Cannot spawn when spawned.
  FUZZING_EXPECT_ERROR(process.SpawnAsync(), ZX_ERR_BAD_STATE);
  RunUntilIdle();
}

TEST_F(ChildProcessTest, Wait) {
  ChildProcess process(executor());
  process.AddArg(kEcho);
  FUZZING_EXPECT_OK(process.SpawnAsync());
  RunUntilIdle();

  process.CloseStdin();
  FUZZING_EXPECT_OK(process.Wait(), 0);
  RunUntilIdle();
}

TEST_F(ChildProcessTest, ReadFromStdout) {
  ChildProcess process(executor());
  std::string hello("hello");
  std::string world("world");
  std::string input = hello + "\n" + world;

  FUZZING_EXPECT_OK(process.ReadFromStdout(), hello);
  FUZZING_EXPECT_OK(process.ReadFromStdout(), world);
  FUZZING_EXPECT_OK(process.WriteAndCloseStdin(input.data(), input.size()), input.size());
  process.AddArgs({kEcho, "--stdout"});
  FUZZING_EXPECT_OK(process.SpawnAsync());
  RunUntilIdle();
}

TEST_F(ChildProcessTest, ReadFromStderr) {
  ChildProcess process(executor());
  std::string hello("hello");
  std::string world("world");
  std::string input = hello + "\n" + world;

  FUZZING_EXPECT_OK(process.ReadFromStderr(), hello);
  FUZZING_EXPECT_OK(process.ReadFromStderr(), world);
  FUZZING_EXPECT_OK(process.WriteAndCloseStdin(input.data(), input.size()), input.size());
  process.AddArgs({kEcho, "--stderr"});
  FUZZING_EXPECT_OK(process.SpawnAsync());
  RunUntilIdle();
}

TEST_F(ChildProcessTest, SetEnvVar) {
  ChildProcess process(executor());
  process.AddArg(kEcho);
  process.SetEnvVar("FUZZING_COMMON_TESTING_ECHO_EXITCODE", "1");
  process.SetEnvVar("FUZZING_COMMON_TESTING_ECHO_EXITCODE", "2");
  FUZZING_EXPECT_OK(process.SpawnAsync());
  RunUntilIdle();

  process.CloseStdin();
  FUZZING_EXPECT_OK(process.Wait(), 2);
  RunUntilIdle();
}

TEST_F(ChildProcessTest, Kill) {
  ChildProcess process(executor());
  process.AddArgs({kEcho, "--stdout", "--stderr"});
  FUZZING_EXPECT_OK(process.SpawnAsync());

  std::string input("hello\nworld");
  FUZZING_EXPECT_OK(process.WriteToStdin(input.data(), input.size()), input.size());
  RunUntilIdle();

  FUZZING_EXPECT_OK(process.Kill());
  RunUntilIdle();

  // Cannot respawn until reset.
  FUZZING_EXPECT_ERROR(process.SpawnAsync(), ZX_ERR_BAD_STATE);
  FUZZING_EXPECT_ERROR(process.WriteToStdin(input.data(), input.size()));
  FUZZING_EXPECT_ERROR(process.ReadFromStdout());
  FUZZING_EXPECT_ERROR(process.ReadFromStderr());
  RunUntilIdle();

  // Can respawn after reset.
  process.Reset();
  process.AddArg(kEcho);
  FUZZING_EXPECT_OK(process.SpawnAsync());
  RunUntilIdle();
}

}  // namespace fuzzing

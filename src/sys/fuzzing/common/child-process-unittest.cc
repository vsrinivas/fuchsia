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
  EXPECT_EQ(process.Spawn(), ZX_ERR_NOT_FOUND);
  RunUntilIdle();

  // Can respawn after reset.
  process.Reset();
  process.AddArg(kEcho);
  EXPECT_EQ(process.Spawn(), ZX_OK);

  // Cannot spawn when spawned.
  EXPECT_EQ(process.Spawn(), ZX_ERR_BAD_STATE);
}

TEST_F(ChildProcessTest, Wait) {
  ChildProcess process(executor());
  process.AddArg(kEcho);
  EXPECT_EQ(process.Spawn(), ZX_OK);

  process.CloseStdin();
  FUZZING_EXPECT_OK(process.Wait(), 0);
  RunUntilIdle();
}

TEST_F(ChildProcessTest, ReadFromStdout) {
  ChildProcess process(executor());
  std::string hello("hello");
  std::string world("world");
  std::string input = hello + "\n" + world;

  process.AddArgs({kEcho, "--stdout"});
  EXPECT_EQ(process.AddStdinPipe(), ZX_OK);
  EXPECT_EQ(process.AddStdoutPipe(), ZX_OK);
  EXPECT_EQ(process.Spawn(), ZX_OK);

  FUZZING_EXPECT_OK(process.ReadFromStdout(), hello);
  FUZZING_EXPECT_OK(process.ReadFromStdout(), world);
  EXPECT_EQ(process.WriteToStdin(input), ZX_OK);
  process.CloseStdin();
  RunUntilIdle();
}

TEST_F(ChildProcessTest, ReadFromStderr) {
  ChildProcess process(executor());
  std::string hello("hello");
  std::string world("world");
  std::string input = hello + "\n" + world;

  process.AddArgs({kEcho, "--stderr"});
  EXPECT_EQ(process.AddStdinPipe(), ZX_OK);
  EXPECT_EQ(process.AddStderrPipe(), ZX_OK);
  EXPECT_EQ(process.Spawn(), ZX_OK);

  FUZZING_EXPECT_OK(process.ReadFromStderr(), hello);
  FUZZING_EXPECT_OK(process.ReadFromStderr(), world);
  EXPECT_EQ(process.WriteToStdin(input), ZX_OK);
  process.CloseStdin();
  RunUntilIdle();
}

TEST_F(ChildProcessTest, SetEnvVar) {
  ChildProcess process(executor());
  process.AddArg(kEcho);
  process.SetEnvVar("FUZZING_COMMON_TESTING_ECHO_EXITCODE", "1");
  process.SetEnvVar("FUZZING_COMMON_TESTING_ECHO_EXITCODE", "2");
  EXPECT_EQ(process.Spawn(), ZX_OK);
  RunUntilIdle();

  process.CloseStdin();
  FUZZING_EXPECT_OK(process.Wait(), 2);
  RunUntilIdle();
}

TEST_F(ChildProcessTest, Kill) {
  ChildProcess process(executor());
  process.AddArgs({kEcho, "--stdout", "--stderr"});
  EXPECT_EQ(process.AddStdinPipe(), ZX_OK);
  EXPECT_EQ(process.AddStdoutPipe(), ZX_OK);
  EXPECT_EQ(process.AddStderrPipe(), ZX_OK);
  EXPECT_EQ(process.Spawn(), ZX_OK);

  std::string input("hello\nworld");
  EXPECT_EQ(process.WriteToStdin(input), ZX_OK);
  process.CloseStdin();
  FUZZING_EXPECT_OK(process.Kill());
  RunUntilIdle();

  // Cannot respawn until reset.
  EXPECT_EQ(process.Spawn(), ZX_ERR_BAD_STATE);
  EXPECT_EQ(process.WriteToStdin(input), ZX_ERR_BAD_STATE);
  FUZZING_EXPECT_ERROR(process.ReadFromStdout());
  FUZZING_EXPECT_ERROR(process.ReadFromStderr());
  RunUntilIdle();

  // Can respawn after reset.
  process.Reset();
  process.AddArg(kEcho);
  EXPECT_EQ(process.Spawn(), ZX_OK);
}

}  // namespace fuzzing

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for fdio_spawn's #!resolve directive support

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "util.h"

namespace {

static constexpr char kTestUtilBin[] = "/pkg/bin/return_arg_test_util";
static constexpr char kResolveOnceBin[] = "/pkg/bin/resolve_once";
static constexpr char kResolveTwiceBin[] = "/pkg/bin/resolve_twice";
static constexpr char kResolveInfiniteLoopBin[] = "/pkg/bin/resolve_infinite_loop";
static constexpr char kResolveToNotFound[] = "/pkg/bin/resolve_to_not_found";
static constexpr char kUseShebangFromResolveBin[] = "/pkg/bin/use_shebang_from_resolve";

// Check that the test util works without involving #!resolve.
TEST(ResolveTest, SpawnUtilWithoutResolve) {
  const char* path = kTestUtilBin;
  const char* argv[] = {path, "42", nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  int64_t return_code;
  wait_for_process_exit(process, &return_code);
  EXPECT_EQ(return_code, 42);
}

// Single #!resolve directive hop to load test util
TEST(ResolveTest, SpawnResolveOneHop) {
  const char* path = kResolveOnceBin;
  const char* argv[] = {path, "53", nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  int64_t return_code;
  wait_for_process_exit(process, &return_code);
  EXPECT_EQ(return_code, 53);
}

// Two #!resolve directive hops to load test util
TEST(ResolveTest, SpawnResolveTwoHops) {
  const char* path = kResolveTwiceBin;
  const char* argv[] = {path, "64", nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  int64_t return_code;
  wait_for_process_exit(process, &return_code);
  EXPECT_EQ(return_code, 64);
}

// #!resolve that results in ZX_ERR_NOT_FOUND from the resolver, results in
// ZX_ERR_INTERNAL. This behavior addresses cases such as a shell treating a
// failed resolve as a "there was not binary at this path".
TEST(ResolveTest, SpawnResolveToNotFoundIsInternal) {
  const char* path = kResolveToNotFound;
  const char* argv[] = {path, "75", nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_ERR_INTERNAL);
  EXPECT_FALSE(process.is_valid());
}

// Infinite #!resolve loop (the executable references itself) should fail after hitting the limit
TEST(ResolveTest, SpawnResolveInfiniteLoopFails) {
  const char* path = kResolveInfiniteLoopBin;
  const char* argv[] = {path, "75", nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_ERR_IO_INVALID);
  EXPECT_FALSE(process.is_valid());
}

// Using #!resolve to load a file that uses a shebang should fail; mixing the two is unsupported.
TEST(ResolveTest, SpawnFailsIfResolveUsesShebang) {
  const char* path = kUseShebangFromResolveBin;
  const char* argv[] = {path, nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(process.is_valid());
}

}  // namespace

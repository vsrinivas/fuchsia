// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include "gtest/gtest.h"

static constexpr char kRunPath[] = "/system/bin/run";
static constexpr char kExiter[] = "run_test_exiter";

void test_case(const char* value) {
  // Spawn "run run_test_exiter <value>"
  uint32_t flags = FDIO_SPAWN_CLONE_ALL;
  const char* argv[] = {kRunPath, kExiter, value, NULL};
  zx_handle_t process = ZX_HANDLE_INVALID;
  zx_status_t status =
      fdio_spawn(ZX_HANDLE_INVALID, flags, kRunPath, argv, &process);
  ASSERT_EQ(ZX_OK, status);

  // Wait for `run` to terminate
  status =
      zx_object_wait_one(process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL);
  ASSERT_EQ(ZX_OK, status);

  // Verify `run` return code
  zx_info_process_t proc_info;
  status = zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), NULL, NULL);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(strtoll(value, NULL, 0), proc_info.return_code);
}

TEST(RunReturnValueTest, Zero) { test_case("0"); }
TEST(RunReturnValueTest, OneTwoThree) { test_case("123"); }
TEST(RunReturnValueTest, Negative) { test_case("-99999"); }
TEST(RunReturnValueTest, LongValue) { test_case("1152921504606846976"); }

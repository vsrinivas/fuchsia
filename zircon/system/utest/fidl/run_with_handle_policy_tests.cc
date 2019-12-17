// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <limits.h>
#include <stdio.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <unittest/unittest.h>

namespace {

// Launches handle_policy_test_app with a strict ZX_POL_BAD_HANDLE policy, such that the app would
// crash if an invalid handle was closed (e.g. double-closing).

bool LaunchHelper(const char* argv[]) {
  BEGIN_HELPER;

  const char* path = argv[0];
  fbl::unique_fd fds[2];
  int temp_fds[2] = {-1, -1};
  ASSERT_EQ(pipe(temp_fds), 0);
  fds[0].reset(temp_fds[0]);
  fds[1].reset(temp_fds[1]);

  fdio_spawn_action_t fdio_actions[3] = {
      fdio_spawn_action{.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = path}},
      fdio_spawn_action{.action = FDIO_SPAWN_ACTION_CLONE_FD,
                        .fd = {.local_fd = fds[1].get(), .target_fd = STDOUT_FILENO}},
      fdio_spawn_action{.action = FDIO_SPAWN_ACTION_TRANSFER_FD,
                        .fd = {.local_fd = fds[1].get(), .target_fd = STDERR_FILENO}},
  };

  zx_status_t status;
  zx::job test_job;
  status = zx::job::create(*zx::job::default_job(), 0, &test_job);
  ASSERT_EQ(status, ZX_OK);
  auto auto_call_kill_job = fbl::MakeAutoCall([&test_job]() { test_job.kill(); });
  const char name[] = "handle-policy-test-app";
  status = test_job.set_property(ZX_PROP_NAME, name, sizeof(name));
  ASSERT_EQ(status, ZX_OK);
  static const zx_policy_basic_v2_t policy[] = {
      zx_policy_basic_v2_t{.condition = ZX_POL_BAD_HANDLE,
                           .action = ZX_POL_ACTION_ALLOW_EXCEPTION,
                           .flags = ZX_POL_OVERRIDE_DENY},
  };
  status =
      test_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, &policy, fbl::count_of(policy));
  ASSERT_EQ(status, ZX_OK);

  fds[1].release();  // To avoid double close since fdio_spawn_etc() closes it.
  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status = fdio_spawn_etc(test_job.get(), FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr,
                          fbl::count_of(fdio_actions), fdio_actions,
                          process.reset_and_get_address(), err_msg);
  ASSERT_EQ(status, ZX_OK);
  // Pipe through output.
  char buf[1024];
  ssize_t bytes_read = 0;
  while ((bytes_read = read(fds[0].get(), buf, sizeof(buf))) > 0) {
    fwrite(buf, 1, bytes_read, stdout);
  }
  status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  ASSERT_EQ(status, ZX_OK);

  // Read the return code.
  zx_info_process_t proc_info;
  status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(proc_info.return_code, 0);

  END_HELPER;
}

bool TestWithStrictHandlePolicy() {
  BEGIN_TEST;

  // This test app contains a subset of fidl-tests; refer to BUILD.gn
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (root_dir == nullptr) {
    root_dir = "";
  }
  const std::string test_app = std::string(root_dir) + "/bin/fidl-handle-policy-test-app";
  const char* args[] = {test_app.c_str(), nullptr};
  ASSERT_TRUE(LaunchHelper(args));

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(handle_policy)
RUN_TEST(TestWithStrictHandlePolicy)
END_TEST_CASE(handle_policy)

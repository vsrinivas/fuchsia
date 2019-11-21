// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <stdlib.h>
#include <zxtest/zxtest.h>

#include <string>

static int64_t join(const zx::process& process) {
  zx_status_t status = process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr);
  EXPECT_OK(status);
  zx_info_process_t proc_info{};
  status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
  EXPECT_OK(status);
  return proc_info.return_code;
}

TEST(NullNamespaceTest, NullNamespace) {
  zx::process process;
  zx_status_t status;

  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (root_dir == nullptr) {
    root_dir = "";
  }
  const std::string path = std::string(root_dir) + "/bin/null-namespace-child";
  const char* argv[] = {path.c_str(), nullptr};
  status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_STDIO | FDIO_SPAWN_DEFAULT_LDSVC, argv[0],
                      argv, process.reset_and_get_address());
  ASSERT_OK(status);
  EXPECT_EQ(0, join(process));
}

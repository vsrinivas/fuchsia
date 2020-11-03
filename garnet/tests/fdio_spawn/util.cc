// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <zxtest/zxtest.h>

// This must return void in order to use gtest ASSERTs.
void wait_for_process_exit(const zx::process& process, int64_t* return_code) {
  zx_status_t status = process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr);
  ASSERT_OK(status);

  zx_info_process_t proc_info{};
  status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
  ASSERT_OK(status);

  *return_code = proc_info.return_code;
}

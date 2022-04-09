// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_DUMP_TESTS_H_
#define SRC_LIB_ZXDUMP_DUMP_TESTS_H_

#include <lib/fdio/spawn.h>
#include <lib/fitx/result.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zxdump/dump.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace zxdump::testing {

// A simple test program starts up and waits.
class TestProcess {
 public:
  zx::unowned_process borrow() const { return zx::unowned_process{process_}; }

  ~TestProcess() {
    if (process_) {
      EXPECT_EQ(ZX_OK, process_.kill());
    }
  }

  void StartChild(std::vector<const char*> args = {}) {
    args.insert(args.begin(), kChildProgram);
    args.push_back(nullptr);
    ASSERT_FALSE(*std::prev(args.end()));
    ASSERT_EQ(ZX_OK, fdio_spawn(zx::job::default_job()->get(), FDIO_SPAWN_CLONE_ALL, kChildProgram,
                                args.data(), process_.reset_and_get_address()));
  }

  const zx::process& process() const { return process_; }

  zx_koid_t koid() const {
    zx_info_handle_basic_t info = {.koid = ZX_KOID_INVALID};
    EXPECT_EQ(ZX_OK,
              process_.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    return info.koid;
  }

 private:
  static constexpr const char* kChildProgram = "/pkg/bin/zxdump-test-child";

  zx::process process_;
};

}  // namespace zxdump::testing

#endif  // SRC_LIB_ZXDUMP_DUMP_TESTS_H_

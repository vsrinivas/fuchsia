// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <string_view>

#include <zxtest/zxtest.h>

extern "C" zx_handle_t get_root_resource(void);

namespace {

zx_status_t DebugCommand(std::string_view command) {
  return zx_debug_send_command(get_root_resource(), command.data(), command.size());
}

// Ask the kernel to run its unit tests.
TEST(KernelUnittests, RunKernelUnittests) { ASSERT_OK(DebugCommand("ut all")); }

// Run certain unit tests in loops, to shake out flakes.
TEST(KernelUnittests, RepeatedRunCertainUnittests) {
  constexpr int kLoops = 10;
  for (int i = 0; i < kLoops; i++) {
    for (auto command : {"ut timer", "ut pi"}) {
      ASSERT_OK(DebugCommand(command));
    }
  }
}

}  // namespace

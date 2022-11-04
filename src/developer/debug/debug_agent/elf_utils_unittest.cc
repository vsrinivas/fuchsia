// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/elf_utils.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

namespace {

void ValidateModules(const std::vector<debug_ipc::Module>& modules) {
  // It should contain at least libc, libsyslog, libfdio, vdso and the main executable.
  EXPECT_GT(modules.size(), 5u);

  bool has_libc = false;
  bool has_syslog = false;
  for (const auto& module : modules) {
    if (module.name == "libc.so") {
      has_libc = true;
      EXPECT_FALSE(module.build_id.empty());
    }
    if (module.name == "libsyslog.so") {
      has_syslog = true;
      EXPECT_FALSE(module.build_id.empty());
    }
  }
  EXPECT_TRUE(has_libc);
  EXPECT_TRUE(has_syslog);
}

TEST(ElfUtils, GetElfModulesForProcess) {
  zx::process handle;
  zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
  ZirconProcessHandle self(std::move(handle));

  uintptr_t dl_debug_addr;
  ASSERT_EQ(ZX_OK, zx::process::self()->get_property(ZX_PROP_PROCESS_DEBUG_ADDR, &dl_debug_addr,
                                                     sizeof(dl_debug_addr)));

  ValidateModules(GetElfModulesForProcess(self, dl_debug_addr));
}

TEST(ElfUtils, GetElfModulesForProcessNoDebugAddr) {
  zx::process handle;
  zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
  ZirconProcessHandle self(std::move(handle));

  ValidateModules(GetElfModulesForProcess(self, 0));
}

}  // namespace

}  // namespace debug_agent

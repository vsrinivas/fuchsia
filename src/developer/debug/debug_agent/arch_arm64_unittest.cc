// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/arch_arm64_helpers_unittest.h"
#include "src/developer/debug/debug_agent/mock_arch_provider.h"
#include "src/developer/debug/debug_agent/test_utils.h"

namespace debug_agent {
namespace arch {
namespace {

class TestArchProvider : public MockArchProvider {
 public:
  TestArchProvider() { watchpoint_count_ = 4; }

  zx_status_t ReadDebugState(const zx::thread&, zx_thread_state_debug_regs_t* regs) const override {
    *regs = regs_;
    return ZX_OK;
  }

  zx_status_t WriteDebugState(const zx::thread&,
                              const zx_thread_state_debug_regs_t& regs) override {
    regs_ = regs;
    return ZX_OK;
  }

  arch::WatchpointInstallationResult InstallWatchpoint(
      const zx::thread& thread, const debug_ipc::AddressRange& range) override {
    return ArchProvider::InstallWatchpoint(thread, range);
  }

  zx_status_t UninstallWatchpoint(const zx::thread& thread,
                                  const debug_ipc::AddressRange& range) override {
    return ArchProvider::UninstallWatchpoint(thread, range);
  }

  const zx_thread_state_debug_regs_t& regs() const { return regs_; }

 private:
  zx_thread_state_debug_regs_t regs_ = {};
};

TEST(ArchArm64, ReadTPIDR) {
  zx_thread_state_general_regs_t regs_in;
  regs_in.tpidr = 0xdeadbeeff00dbabe;
  std::vector<debug_ipc::Register> regs_out;
  ArchProvider::SaveGeneralRegs(regs_in, &regs_out);

  const debug_ipc::Register* found = nullptr;
  for (const auto& reg : regs_out) {
    if (reg.id == debug_ipc::RegisterID::kARMv8_tpidr) {
      found = &reg;
      break;
    }
  }

  ASSERT_NE(nullptr, found);
  ASSERT_GE(8u, found->data.size());
  EXPECT_EQ(8u, found->data.size());

  EXPECT_EQ(0xbe, found->data[0]);
  EXPECT_EQ(0xba, found->data[1]);
  EXPECT_EQ(0x0d, found->data[2]);
  EXPECT_EQ(0xf0, found->data[3]);
  EXPECT_EQ(0xef, found->data[4]);
  EXPECT_EQ(0xbe, found->data[5]);
  EXPECT_EQ(0xad, found->data[6]);
  EXPECT_EQ(0xde, found->data[7]);
}

TEST(ArchArm64, SetupWatchpoint) {
  TestArchProvider arch_provider;
  auto& regs = arch_provider.regs();

  zx::thread thread;

  const debug_ipc::AddressRange kRange1 = {0x100, 0x101};
  const debug_ipc::AddressRange kRange2 = {0x100, 0x102};
  const debug_ipc::AddressRange kRange3 = {0x100, 0x104};
  const debug_ipc::AddressRange kRange4 = {0x100, 0x108};
  const debug_ipc::AddressRange kRange5 = {0x100, 0x105};
  const debug_ipc::AddressRange kRange6 = {0x200, 0x201};

  auto install = arch_provider.InstallWatchpoint(thread, kRange1);
  ASSERT_ZX_EQ(install.status, ZX_OK);
  EXPECT_EQ(install.installed_range, kRange1);
  EXPECT_EQ(install.slot, 0);

  install = arch_provider.InstallWatchpoint(thread, kRange2);
  ASSERT_ZX_EQ(install.status, ZX_OK);
  EXPECT_EQ(install.installed_range, kRange2);
  EXPECT_EQ(install.slot, 1);
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {1, 1, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));
  ASSERT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  install = arch_provider.InstallWatchpoint(thread, kRange2);
  ASSERT_ZX_EQ(install.status, ZX_ERR_ALREADY_BOUND);

  install = arch_provider.InstallWatchpoint(thread, kRange5);
  ASSERT_ZX_EQ(install.status, ZX_ERR_OUT_OF_RANGE);

  install = arch_provider.InstallWatchpoint(thread, kRange3);
  ASSERT_ZX_EQ(install.status, ZX_OK);
  EXPECT_EQ(install.installed_range, kRange3);
  EXPECT_EQ(install.slot, 2);
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {1, 1, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));
  ASSERT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  install = arch_provider.InstallWatchpoint(thread, kRange4);
  ASSERT_ZX_EQ(install.status, ZX_OK);
  EXPECT_EQ(install.installed_range, kRange4);
  EXPECT_EQ(install.slot, 3);
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0x100}));
  ASSERT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  ASSERT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  install = arch_provider.InstallWatchpoint(thread, kRange6);
  ASSERT_ZX_EQ(install.status, ZX_ERR_NO_RESOURCES);

  // Removing.
  ASSERT_ZX_EQ(arch_provider.UninstallWatchpoint(thread, kRange1), ZX_OK);
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0x100, 0x100}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 4, 8}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, kWrite, kWrite}));

  ASSERT_ZX_EQ(arch_provider.UninstallWatchpoint(thread, kRange1), ZX_ERR_NOT_FOUND);
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0x100, 0x100}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 4, 8}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, kWrite, kWrite}));

  ASSERT_ZX_EQ(arch_provider.UninstallWatchpoint(thread, kRange4), ZX_OK);
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0x100, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 4, 0}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, kWrite, 0}));

  ASSERT_ZX_EQ(arch_provider.UninstallWatchpoint(thread, kRange3), ZX_OK);
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 0, 0}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, 0, 0}));

  ASSERT_ZX_EQ(arch_provider.UninstallWatchpoint(thread, kRange2), ZX_OK);
  ASSERT_TRUE(CheckAddresses(regs, {0, 0, 0, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 0, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 0, 0, 0}));
  ASSERT_TRUE(CheckTypes(regs, {0, 0, 0, 0}));
}

}  // namespace
}  // namespace arch
}  // namespace debug_agent

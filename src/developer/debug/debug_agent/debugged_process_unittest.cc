// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_process.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/mock_object_provider.h"

namespace debug_agent {
namespace {

class MockArchProvider : public arch::ArchProvider {
 public:
  zx_status_t InstallHWBreakpoint(zx::thread* thread, uint64_t address) override {
    installs_.push_back({thread->get(), address});
    return ZX_OK;
  }

  zx_status_t UninstallHWBreakpoint(zx::thread* thread, uint64_t address) override {
    uninstalls_.push_back({thread->get(), address});
    return ZX_OK;
  }

  const std::vector<std::pair<zx_koid_t, uint64_t>>& installs() const { return installs_; }
  const std::vector<std::pair<zx_koid_t, uint64_t>>& uninstalls() const { return uninstalls_; }

 private:
  std::vector<std::pair<zx_koid_t, uint64_t>> installs_;
  std::vector<std::pair<zx_koid_t, uint64_t>> uninstalls_;
};

class MockProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                 uint64_t address) override {
    return ZX_OK;
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override {}
};

class MockProcessMemoryAccessor : public ProcessMemoryAccessor {
  zx_status_t ReadProcessMemory(uintptr_t address, void* buffer, size_t len,
                                size_t* actual) override {
    // In this test the logic expects to have written a breakpoint.
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    *ptr = arch::kBreakInstruction;
    *actual = len;
    return ZX_OK;
  }
  zx_status_t WriteProcessMemory(uintptr_t address, const void* buffer, size_t len,
                                 size_t* actual) override {
    *actual = len;
    return ZX_OK;
  }
};

DebuggedProcess CreateProcess(zx_koid_t koid, std::string name) {
  DebuggedProcessCreateInfo create_info = {};
  create_info.koid = koid;
  create_info.name = std::move(name);
  create_info.arch_provider = std::make_unique<MockArchProvider>();
  create_info.object_provider = CreateDefaultMockObjectProvider();
  create_info.memory_accessor = std::make_unique<MockProcessMemoryAccessor>();

  return DebuggedProcess(nullptr, std::move(create_info));
}

// If |thread| is null, it means a process-wide breakpoint.
debug_ipc::ProcessBreakpointSettings CreateLocation(zx_koid_t process_koid, zx_koid_t thread_koid,
                                                    uint64_t address) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.process_koid = process_koid;
  location.thread_koid = thread_koid;
  location.address = address;

  return location;
}

#define ZX_ASSERT_EQ(stmt, expected)                                                          \
  {                                                                                           \
    zx_status_t status = (stmt);                                                              \
    ASSERT_EQ(status, expected) << "Expected " << zx_status_get_string(expected) << std::endl \
                                << "Got: " << zx_status_get_string(status);                   \
  }

// Tests -------------------------------------------------------------------------------------------

constexpr zx_koid_t kProcessKoid = 0x1;
const std::string kProcessName = "process-name";
constexpr uint64_t kAddress1 = 0x1234;
constexpr uint64_t kAddress2 = 0x5678;
constexpr uint64_t kAddress3 = 0x9abc;

TEST(DebuggedProcess, RegisterBreakpoints) {
  MockProcessDelegate process_delegate;
  auto process = CreateProcess(kProcessKoid, kProcessName);

  debug_ipc::BreakpointSettings settings;
  settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddress1));
  settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddress2));
  settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddress3));
  Breakpoint breakpoint(&process_delegate);
  breakpoint.SetSettings(debug_ipc::BreakpointType::kSoftware, settings);

  ZX_ASSERT_EQ(process.RegisterBreakpoint(&breakpoint, kAddress1), ZX_OK);

  ASSERT_EQ(process.software_breakpoints().size(), 1u);
  auto it = process.software_breakpoints().begin();
  EXPECT_EQ(it++->first, kAddress1);
  EXPECT_EQ(it, process.software_breakpoints().end());

  // Add 2 other breakpoints.
  ZX_ASSERT_EQ(process.RegisterBreakpoint(&breakpoint, kAddress2), ZX_OK);
  ZX_ASSERT_EQ(process.RegisterBreakpoint(&breakpoint, kAddress3), ZX_OK);
  ASSERT_EQ(process.software_breakpoints().size(), 3u);
  it = process.software_breakpoints().begin();
  EXPECT_EQ(it++->first, kAddress1);
  EXPECT_EQ(it++->first, kAddress2);
  EXPECT_EQ(it++->first, kAddress3);
  EXPECT_EQ(it, process.software_breakpoints().end());

  // Unregister a breakpoint.
  process.UnregisterBreakpoint(&breakpoint, kAddress1);
  ASSERT_EQ(process.software_breakpoints().size(), 2u);
  it = process.software_breakpoints().begin();
  EXPECT_EQ(it++->first, kAddress2);
  EXPECT_EQ(it++->first, kAddress3);
  EXPECT_EQ(it, process.software_breakpoints().end());
}

}  // namespace
}  // namespace debug_agent

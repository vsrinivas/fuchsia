// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_process.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/test_utils.h"

namespace debug_agent {
namespace {

class MockProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  debug::Status RegisterBreakpoint(Breakpoint*, zx_koid_t, uint64_t) override {
    return debug::Status();
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override {}

  debug::Status RegisterWatchpoint(Breakpoint*, zx_koid_t, const debug::AddressRange&) override {
    return debug::Status();
  }
  void UnregisterWatchpoint(Breakpoint*, zx_koid_t, const debug::AddressRange&) override {}
};

// Fills a vector with the breakpoint instruction for the current architecture. The size will vary
// by architecture.
std::vector<uint8_t> GetBreakpointMemory() {
  std::vector<uint8_t> result;
  result.resize(arch::kBreakInstructionSize);
  memcpy(result.data(), &arch::kBreakInstruction, arch::kBreakInstructionSize);
  return result;
}

// If |thread| is null, it means a process-wide breakpoint.
debug_ipc::ProcessBreakpointSettings CreateLocation(zx_koid_t process_koid, zx_koid_t thread_koid,
                                                    uint64_t address) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.id = {.process = process_koid, .thread = thread_koid};
  location.address = address;

  return location;
}

debug_ipc::ProcessBreakpointSettings CreateLocation(zx_koid_t process_koid, zx_koid_t thread_koid,
                                                    const debug::AddressRange& range) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.id = {.process = process_koid, .thread = thread_koid};
  location.address_range = range;

  return location;
}

debug::AddressRange SetLocation(Breakpoint* breakpoint, zx_koid_t koid,
                                const debug::AddressRange& range) {
  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kWrite;
  settings.locations.push_back(CreateLocation(koid, 0, range));
  breakpoint->SetSettings(settings);

  return range;
}

// Tests -------------------------------------------------------------------------------------------

constexpr zx_koid_t kProcessKoid = 0x1;
const std::string kProcessName = "process-name";
constexpr uint64_t kAddress1 = 0x1234;
constexpr uint64_t kAddress2 = 0x5678;
constexpr uint64_t kAddress3 = 0x9abc;
constexpr uint64_t kAddress4 = 0xdef0;

constexpr debug::AddressRange kAddressRange1 = {0x1, 0x2};

TEST(DebuggedProcess, RegisterBreakpoints) {
  MockProcessDelegate process_delegate;
  MockProcess process(nullptr, kProcessKoid, kProcessName);

  // There needs to be memory backing the breakpoints so set some memory here. When the breakpoint
  // is uninstalled, the breakpoint expects that it will be the debug break instruction. So we
  // handle both cases and always report a breakpoint at these addresses.
  process.mock_process_handle().mock_memory().AddMemory(kAddress1, GetBreakpointMemory());
  process.mock_process_handle().mock_memory().AddMemory(kAddress2, GetBreakpointMemory());
  process.mock_process_handle().mock_memory().AddMemory(kAddress3, GetBreakpointMemory());
  process.mock_process_handle().mock_memory().AddMemory(kAddress4, GetBreakpointMemory());

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kSoftware;
  settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddress1));
  settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddress2));
  settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddress3));
  Breakpoint breakpoint(&process_delegate);
  breakpoint.SetSettings(settings);

  ASSERT_TRUE(process.RegisterBreakpoint(&breakpoint, kAddress1).ok());

  ASSERT_EQ(process.software_breakpoints().size(), 1u);
  auto it = process.software_breakpoints().begin();
  EXPECT_EQ(it++->first, kAddress1);
  EXPECT_EQ(it, process.software_breakpoints().end());

  // Add 2 other breakpoints.
  ASSERT_TRUE(process.RegisterBreakpoint(&breakpoint, kAddress2).ok());
  ASSERT_TRUE(process.RegisterBreakpoint(&breakpoint, kAddress3).ok());
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

  // Register a hardware breakpoint.
  Breakpoint hw_breakpoint(&process_delegate);
  debug_ipc::BreakpointSettings hw_settings;
  hw_settings.type = debug_ipc::BreakpointType::kHardware;
  hw_settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddress4));
  hw_breakpoint.SetSettings(hw_settings);

  ASSERT_TRUE(process.RegisterBreakpoint(&hw_breakpoint, kAddress3).ok());
  ASSERT_TRUE(process.RegisterBreakpoint(&hw_breakpoint, kAddress4).ok());

  // Should've inserted 2 HW breakpoint.
  ASSERT_EQ(process.software_breakpoints().size(), 2u);
  ASSERT_EQ(process.hardware_breakpoints().size(), 2u);
  auto hw_it = process.hardware_breakpoints().begin();
  EXPECT_EQ(hw_it++->first, kAddress3);
  EXPECT_EQ(hw_it++->first, kAddress4);
  EXPECT_EQ(hw_it, process.hardware_breakpoints().end());

  // Remove a hardware breakpoint.
  process.UnregisterBreakpoint(&hw_breakpoint, kAddress3);
  ASSERT_EQ(process.software_breakpoints().size(), 2u);
  ASSERT_EQ(process.hardware_breakpoints().size(), 1u);
  hw_it = process.hardware_breakpoints().begin();
  EXPECT_EQ(hw_it++->first, kAddress4);
  EXPECT_EQ(hw_it, process.hardware_breakpoints().end());

  // Add a watchpoint.
  Breakpoint wp_breakpoint(&process_delegate);
  debug_ipc::BreakpointSettings wp_settings;
  wp_settings.type = debug_ipc::BreakpointType::kWrite;
  wp_settings.locations.push_back(CreateLocation(kProcessKoid, 0, kAddressRange1));
  wp_breakpoint.SetSettings(wp_settings);

  ASSERT_TRUE(process.RegisterWatchpoint(&wp_breakpoint, kAddressRange1).ok());
  ASSERT_EQ(process.software_breakpoints().size(), 2u);
  ASSERT_EQ(process.hardware_breakpoints().size(), 1u);
  ASSERT_EQ(process.watchpoints().size(), 1u);
  auto wp_it = process.watchpoints().begin();
  EXPECT_EQ(wp_it++->first, kAddressRange1);
  EXPECT_EQ(wp_it, process.watchpoints().end());
}

TEST(DebuggedProcess, WatchpointRegistration) {
  MockProcessDelegate process_delegate;
  Breakpoint breakpoint(&process_delegate);

  MockProcess process(nullptr, kProcessKoid, kProcessName);

  // 1 byte.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 1};
    SetLocation(&breakpoint, process.koid(), range);
    ASSERT_TRUE(process.RegisterWatchpoint(&breakpoint, range).ok());
  }

  // 2 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 2};
    SetLocation(&breakpoint, process.koid(), range);

    // Only aligned values should work.
    bool expected_ok = (range.begin() & 0b1) == 0;
    SCOPED_TRACE(range.ToString());
    ASSERT_EQ(process.RegisterWatchpoint(&breakpoint, range).ok(), expected_ok);
  }

  // 3 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 3};
    SetLocation(&breakpoint, process.koid(), range);
    ASSERT_TRUE(process.RegisterWatchpoint(&breakpoint, range).has_error());
  }

  // 4 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 4};
    SetLocation(&breakpoint, process.koid(), range);

    // Only aligned values should work.
    bool expected_ok = (range.begin() & 0b11) == 0;
    SCOPED_TRACE(range.ToString());
    ASSERT_EQ(process.RegisterWatchpoint(&breakpoint, range).ok(), expected_ok);
  }

  // 5 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 5};
    SetLocation(&breakpoint, process.koid(), range);
    ASSERT_TRUE(process.RegisterWatchpoint(&breakpoint, range).has_error());
  }

  // 6 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 6};
    SetLocation(&breakpoint, process.koid(), range);
    ASSERT_TRUE(process.RegisterWatchpoint(&breakpoint, range).has_error());
  }

  // 6 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 6};
    SetLocation(&breakpoint, process.koid(), range);
    ASSERT_TRUE(process.RegisterWatchpoint(&breakpoint, range).has_error());
  }

  // 7 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 7};
    SetLocation(&breakpoint, process.koid(), range);
    ASSERT_TRUE(process.RegisterWatchpoint(&breakpoint, range).has_error());
  }

  // 8 bytes.
  for (uint32_t i = 0; i < 16; i++) {
    debug::AddressRange range = {0x10 + i, 0x10 + i + 8};
    SetLocation(&breakpoint, process.koid(), range);

    // Only aligned values should work.
    bool expected_ok = (range.begin() & 0b111) == 0;
    SCOPED_TRACE(range.ToString());
    ASSERT_EQ(process.RegisterWatchpoint(&breakpoint, range).ok(), expected_ok);
  }
}

}  // namespace
}  // namespace debug_agent

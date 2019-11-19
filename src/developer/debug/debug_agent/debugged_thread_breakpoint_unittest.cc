// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/mock_object_provider.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_process_breakpoint.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"
#include "src/developer/debug/debug_agent/watchpoint.h"

namespace debug_agent {

namespace {

// Dependencies -----------------------------------------------------------------------------------

class MockArchProvider : public arch::ArchProvider {
 public:
  zx_status_t ReadGeneralState(const zx::thread&, zx_thread_state_general_regs*) override {
    return ZX_OK;
  }

  zx_status_t WriteGeneralState(const zx::thread&,
                                const zx_thread_state_general_regs& regs) override {
    return ZX_OK;
  }

  zx_status_t GetInfo(const zx::thread&, zx_object_info_topic_t topic, void* buffer,
                      size_t buffer_size, size_t* actual, size_t* avail) override {
    zx_info_thread* info = reinterpret_cast<zx_info_thread*>(buffer);
    info->state = ZX_THREAD_STATE_BLOCKED_EXCEPTION;

    return ZX_OK;
  }

  debug_ipc::ExceptionType DecodeExceptionType(const DebuggedThread&,
                                               uint32_t exception_type) override {
    return exception_type_;
  }

  uint64_t* IPInRegs(zx_thread_state_general_regs* regs) override { return &exception_addr_; }

  bool IsBreakpointInstruction(zx::process& process, uint64_t address) override {
    for (uint64_t addr : breakpoints_) {
      if (addr == address)
        return true;
    }

    return false;
  }

  void AppendBreakpoint(uint64_t addr) { breakpoints_.push_back(addr); }

  uint64_t BreakpointInstructionForSoftwareExceptionAddress(uint64_t exception_addr) override {
    return exception_addr;
  }

  std::pair<uint64_t, int> InstructionForWatchpointHit(const DebuggedThread&) const override {
    return {exception_addr_, slot_};
  }

  void set_exception_addr(uint64_t addr) { exception_addr_ = addr; }
  void set_exception_type(debug_ipc::ExceptionType e) { exception_type_ = e; }
  void set_slot(int slot) { slot_ = slot; }

 private:
  uint64_t exception_addr_ = 0;
  int slot_ = -1;

  std::vector<uint64_t> breakpoints_;
  debug_ipc::ExceptionType exception_type_ = debug_ipc::ExceptionType::kLast;
};

class TestProcess : public MockProcess {
 public:
  TestProcess(DebugAgent* debug_agent, zx_koid_t koid, std::string name,
              std::shared_ptr<arch::ArchProvider> arch_provider,
              std::shared_ptr<ObjectProvider> object_provider)
      : MockProcess(debug_agent, koid, std::move(name), std::move(arch_provider),
                    std::move(object_provider)) {}

  SoftwareBreakpoint* FindSoftwareBreakpoint(uint64_t address) const override {
    auto it = software_breakpoints_.find(address);
    if (it == software_breakpoints_.end())
      return nullptr;
    return it->second.get();
  }

  HardwareBreakpoint* FindHardwareBreakpoint(uint64_t address) const override {
    auto it = hardware_breakpoints_.find(address);
    if (it == hardware_breakpoints_.end())
      return nullptr;
    return it->second.get();
  }

  Watchpoint* FindWatchpoint(const debug_ipc::AddressRange& range) const override {
    for (auto& [r, watchpoint] : watchpoints_) {
      if (r.Contains(range))
        return watchpoint.get();
    }

    return nullptr;
  }

  void AppendSofwareBreakpoint(Breakpoint* breakpoint, uint64_t address) {
    software_breakpoints_[address] =
        std::make_unique<MockSoftwareBreakpoint>(breakpoint, this, nullptr, address);
  }

  void AppendHardwareBreakpoint(Breakpoint* breakpoint, uint64_t address,
                                std::shared_ptr<arch::ArchProvider> arch_provider) {
    hardware_breakpoints_[address] = std::make_unique<MockHardwareBreakpoint>(
        breakpoint, this, address, std::move(arch_provider));
  }

  void AppendWatchpoint(Breakpoint* breakpoint, debug_ipc::AddressRange range,
                        std::shared_ptr<arch::ArchProvider> arch_provider) {
    watchpoints_[range] =
        std::make_unique<Watchpoint>(breakpoint, this, std::move(arch_provider), range);
  }

 private:
  std::map<uint64_t, std::unique_ptr<MockSoftwareBreakpoint>> software_breakpoints_;
  std::map<uint64_t, std::unique_ptr<MockHardwareBreakpoint>> hardware_breakpoints_;
  WatchpointMap watchpoints_;
};

class TestStreamBackend : public LocalStreamBackend {
 public:
  void HandleNotifyException(debug_ipc::NotifyException exception) override {
    exceptions_.push_back(std::move(exception));
  }

  const std::vector<debug_ipc::NotifyException>& exceptions() const { return exceptions_; }

 private:
  std::vector<debug_ipc::NotifyException> exceptions_;
};

class MockProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                 uint64_t address) override {
    return ZX_OK;
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override {}
};

// Helpers -----------------------------------------------------------------------------------------

struct TestContext {
  std::shared_ptr<MockArchProvider> arch_provider;
  std::shared_ptr<LimboProvider> limbo_provider;
  std::shared_ptr<MockObjectProvider> object_provider;

  std::unique_ptr<DebugAgent> debug_agent;
  std::unique_ptr<TestStreamBackend> backend;
};

TestContext CreateTestContext() {
  TestContext context;

  // Mock the system.
  context.arch_provider = std::make_shared<MockArchProvider>();
  context.limbo_provider = std::make_shared<LimboProvider>(nullptr);
  context.object_provider = CreateDefaultMockObjectProvider();

  // Create the debug agent.
  SystemProviders providers;
  providers.arch_provider = context.arch_provider;
  providers.limbo_provider = context.limbo_provider;
  providers.object_provider = context.object_provider;
  context.debug_agent = std::make_unique<DebugAgent>(nullptr, std::move(providers));

  // Create the connection to the debug agent.
  context.backend = std::make_unique<TestStreamBackend>();
  context.debug_agent->Connect(&context.backend->stream());

  return context;
}

std::pair<const MockProcessObject*, const MockThreadObject*> GetProcessThread(
    const MockObjectProvider& object_provider, const std::string& process_name,
    const std::string& thread_name) {
  auto* process = object_provider.ProcessByName(process_name);
  FXL_DCHECK(process);
  auto* thread = process->GetThread(thread_name);
  FXL_DCHECK(thread);

  return {process, thread};
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

// If |thread| is null, it means a process-wide breakpoint.
debug_ipc::ProcessBreakpointSettings CreateLocation(zx_koid_t process_koid, zx_koid_t thread_koid,
                                                    debug_ipc::AddressRange range) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.process_koid = process_koid;
  location.thread_koid = thread_koid;
  location.address_range = range;

  return location;
}

// Tests -------------------------------------------------------------------------------------------

TEST(DebuggedThreadBreakpoint, NormalException) {
  TestContext context = CreateTestContext();

  // Create a process from our mocked object hierarchy.
  auto [proc_object, thread_object] =
      GetProcessThread(*context.object_provider, "job121-p2", "second-thread");
  TestProcess process(context.debug_agent.get(), proc_object->koid, proc_object->name,
                      context.arch_provider, context.object_provider);

  // Create the thread that will be on an exception.
  DebuggedThread::CreateInfo create_info = {};
  create_info.process = &process;
  create_info.koid = thread_object->koid;
  create_info.handle = thread_object->GetHandle();
  create_info.arch_provider = context.arch_provider;
  create_info.object_provider = context.object_provider;
  DebuggedThread thread(context.debug_agent.get(), std::move(create_info));

  // Set the exception information the arch provider is going to return.
  constexpr uint64_t kAddress = 0xdeadbeef;
  context.arch_provider->set_exception_addr(kAddress);
  context.arch_provider->set_exception_type(debug_ipc::ExceptionType::kPageFault);

  // Trigger the exception.
  zx_exception_info exception_info = {};
  exception_info.pid = proc_object->koid;
  exception_info.tid = thread_object->koid;
  exception_info.type = ZX_EXCP_FATAL_PAGE_FAULT;
  thread.OnException(zx::exception(), exception_info);

  // We should've received an exception notification.
  ASSERT_EQ(context.backend->exceptions().size(), 1u);
  {
    EXPECT_EQ(context.backend->exceptions()[0].type, debug_ipc::ExceptionType::kPageFault);
    EXPECT_EQ(context.backend->exceptions()[0].hit_breakpoints.size(), 0u);

    auto& thread_record = context.backend->exceptions()[0].thread;
    EXPECT_EQ(thread_record.process_koid, proc_object->koid);
    EXPECT_EQ(thread_record.thread_koid, thread_object->koid);
    EXPECT_EQ(thread_record.state, debug_ipc::ThreadRecord::State::kBlocked);
    EXPECT_EQ(thread_record.blocked_reason, debug_ipc::ThreadRecord::BlockedReason::kException);
    EXPECT_EQ(thread_record.stack_amount, debug_ipc::ThreadRecord::StackAmount::kMinimal);
  }
}

TEST(DebuggedThreadBreakpoint, SWBreakpoint) {
  TestContext context = CreateTestContext();

  // Create a process from our mocked object hierarchy.
  auto [proc_object, thread_object] =
      GetProcessThread(*context.object_provider, "job121-p2", "second-thread");
  TestProcess process(context.debug_agent.get(), proc_object->koid, proc_object->name,
                      context.arch_provider, context.object_provider);

  // Create the thread that will be on an exception.
  DebuggedThread::CreateInfo create_info = {};
  create_info.process = &process;
  create_info.koid = thread_object->koid;
  create_info.handle = thread_object->GetHandle();
  create_info.arch_provider = context.arch_provider;
  create_info.object_provider = context.object_provider;
  DebuggedThread thread(context.debug_agent.get(), std::move(create_info));

  // Set the exception information the arch provider is going to return.
  constexpr uint64_t kAddress = 0xdeadbeef;
  context.arch_provider->set_exception_addr(kAddress);
  context.arch_provider->set_exception_type(debug_ipc::ExceptionType::kSoftware);

  // Trigger the exception.
  zx_exception_info exception_info = {};
  exception_info.pid = proc_object->koid;
  exception_info.tid = thread_object->koid;
  exception_info.type = ZX_EXCP_SW_BREAKPOINT;
  thread.OnException(zx::exception(), exception_info);

  // We should've received an exception notification.
  ASSERT_EQ(context.backend->exceptions().size(), 1u);
  {
    auto& exception = context.backend->exceptions()[0];

    EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kSoftware)
        << debug_ipc::ExceptionTypeToString(exception.type);
    EXPECT_EQ(exception.hit_breakpoints.size(), 0u);

    auto& thread_record = exception.thread;
    EXPECT_EQ(thread_record.process_koid, proc_object->koid);
    EXPECT_EQ(thread_record.thread_koid, thread_object->koid);
    EXPECT_EQ(thread_record.state, debug_ipc::ThreadRecord::State::kBlocked);
    EXPECT_EQ(thread_record.blocked_reason, debug_ipc::ThreadRecord::BlockedReason::kException);
    EXPECT_EQ(thread_record.stack_amount, debug_ipc::ThreadRecord::StackAmount::kMinimal);
  }

  // Add a breakpoint on that address.
  constexpr uint32_t kBreakpointId = 1000;
  MockProcessDelegate process_delegate;
  auto breakpoint = std::make_unique<Breakpoint>(&process_delegate);
  debug_ipc::BreakpointSettings settings = {};
  settings.id = kBreakpointId;
  settings.locations.push_back(CreateLocation(proc_object->koid, 0, kAddress));
  breakpoint->SetSettings(debug_ipc::BreakpointType::kSoftware, settings);

  process.AppendSofwareBreakpoint(breakpoint.get(), kAddress);
  context.arch_provider->AppendBreakpoint(kAddress);

  // Throw the same breakpoint exception.
  thread.OnException(zx::exception(), exception_info);

  // We should've received an exception notification with hit breakpoints.
  ASSERT_EQ(context.backend->exceptions().size(), 2u);
  {
    auto& exception = context.backend->exceptions()[1];

    EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kSoftware)
        << debug_ipc::ExceptionTypeToString(exception.type);
    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);
    EXPECT_EQ(exception.hit_breakpoints[0].id, breakpoint->stats().id);
    EXPECT_EQ(breakpoint->stats().hit_count, 1u);

    auto& thread_record = exception.thread;
    EXPECT_EQ(thread_record.process_koid, proc_object->koid);
    EXPECT_EQ(thread_record.thread_koid, thread_object->koid);
    EXPECT_EQ(thread_record.state, debug_ipc::ThreadRecord::State::kBlocked);
    EXPECT_EQ(thread_record.blocked_reason, debug_ipc::ThreadRecord::BlockedReason::kException);
    EXPECT_EQ(thread_record.stack_amount, debug_ipc::ThreadRecord::StackAmount::kMinimal);
  }
}

TEST(DebuggedThreadBreakpoint, HWBreakpoint) {
  TestContext context = CreateTestContext();

  // Create a process from our mocked object hierarchy.
  auto [proc_object, thread_object] =
      GetProcessThread(*context.object_provider, "job121-p2", "second-thread");
  TestProcess process(context.debug_agent.get(), proc_object->koid, proc_object->name,
                      context.arch_provider, context.object_provider);

  // Create the thread that will be on an exception.
  DebuggedThread::CreateInfo create_info = {};
  create_info.process = &process;
  create_info.koid = thread_object->koid;
  create_info.handle = thread_object->GetHandle();
  create_info.arch_provider = context.arch_provider;
  create_info.object_provider = context.object_provider;
  DebuggedThread thread(context.debug_agent.get(), std::move(create_info));

  // Set the exception information the arch provider is going to return.
  constexpr uint64_t kAddress = 0xdeadbeef;
  context.arch_provider->set_exception_addr(kAddress);
  context.arch_provider->set_exception_type(debug_ipc::ExceptionType::kHardware);

  // Add a breakpoint on that address.
  constexpr uint32_t kBreakpointId = 1000;
  MockProcessDelegate process_delegate;
  auto breakpoint = std::make_unique<Breakpoint>(&process_delegate);
  debug_ipc::BreakpointSettings settings = {};
  settings.id = kBreakpointId;
  settings.locations.push_back(CreateLocation(proc_object->koid, 0, kAddress));
  breakpoint->SetSettings(debug_ipc::BreakpointType::kHardware, settings);

  process.AppendHardwareBreakpoint(breakpoint.get(), kAddress, context.arch_provider);

  // Trigger the exception.
  zx_exception_info exception_info = {};
  exception_info.pid = proc_object->koid;
  exception_info.tid = thread_object->koid;
  exception_info.type = ZX_EXCP_HW_BREAKPOINT;
  thread.OnException(zx::exception(), exception_info);

  // We should've received an exception notification.
  ASSERT_EQ(context.backend->exceptions().size(), 1u);
  {
    auto& exception = context.backend->exceptions()[0];

    EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kHardware)
        << debug_ipc::ExceptionTypeToString(exception.type);
    EXPECT_EQ(exception.hit_breakpoints.size(), 1u);
    EXPECT_EQ(exception.hit_breakpoints[0].id, breakpoint->stats().id);
    EXPECT_EQ(breakpoint->stats().hit_count, 1u);

    auto& thread_record = exception.thread;
    EXPECT_EQ(thread_record.process_koid, proc_object->koid);
    EXPECT_EQ(thread_record.thread_koid, thread_object->koid);
    EXPECT_EQ(thread_record.state, debug_ipc::ThreadRecord::State::kBlocked);
    EXPECT_EQ(thread_record.blocked_reason, debug_ipc::ThreadRecord::BlockedReason::kException);
    EXPECT_EQ(thread_record.stack_amount, debug_ipc::ThreadRecord::StackAmount::kMinimal);
  }
}

TEST(DebuggedThreadBreakpoint, Watchpoint) {
  TestContext context = CreateTestContext();

  // Create a process from our mocked object hierarchy.
  auto [proc_object, thread_object] =
      GetProcessThread(*context.object_provider, "job121-p2", "second-thread");
  TestProcess process(context.debug_agent.get(), proc_object->koid, proc_object->name,
                      context.arch_provider, context.object_provider);

  // Create the thread that will be on an exception.
  DebuggedThread::CreateInfo create_info = {};
  create_info.process = &process;
  create_info.koid = thread_object->koid;
  create_info.handle = thread_object->GetHandle();
  create_info.arch_provider = context.arch_provider;
  create_info.object_provider = context.object_provider;
  DebuggedThread thread(context.debug_agent.get(), std::move(create_info));

  // Add a watchpoint.
  const debug_ipc::AddressRange kRange = {0x1000, 0x1008};
  MockProcessDelegate process_delegate;
  Breakpoint breakpoint(&process_delegate);

  constexpr uint32_t kBreakpointId = 1000;
  debug_ipc::BreakpointSettings settings = {};
  settings.id = kBreakpointId;
  settings.locations.push_back(CreateLocation(proc_object->koid, 0, kRange));
  breakpoint.SetSettings(debug_ipc::BreakpointType::kWatchpoint, settings);

  process.AppendWatchpoint(&breakpoint, kRange, context.arch_provider);

  // Set the exception information the arch provider is going to return.
  const uint64_t kAddress = kRange.begin();
  constexpr uint64_t kSlot = 0;
  context.arch_provider->set_exception_type(debug_ipc::ExceptionType::kWatchpoint);
  context.arch_provider->set_exception_addr(kAddress);
  context.arch_provider->set_slot(kSlot);

  // Trigger the exception.
  zx_exception_info exception_info = {};
  exception_info.pid = proc_object->koid;
  exception_info.tid = thread_object->koid;
  exception_info.type = ZX_EXCP_HW_BREAKPOINT;
  thread.OnException(zx::exception(), exception_info);

  // We should've received an exception notification.
  {
    auto& exception = context.backend->exceptions()[0];

    EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kWatchpoint)
        << debug_ipc::ExceptionTypeToString(exception.type);
    EXPECT_EQ(exception.hit_breakpoints.size(), 1u);
    EXPECT_EQ(exception.hit_breakpoints[0].id, breakpoint.stats().id);
    EXPECT_EQ(breakpoint.stats().hit_count, 1u);

    auto& thread_record = exception.thread;
    EXPECT_EQ(thread_record.process_koid, proc_object->koid);
    EXPECT_EQ(thread_record.thread_koid, thread_object->koid);
    EXPECT_EQ(thread_record.state, debug_ipc::ThreadRecord::State::kBlocked);
    EXPECT_EQ(thread_record.blocked_reason, debug_ipc::ThreadRecord::BlockedReason::kException);
    EXPECT_EQ(thread_record.stack_amount, debug_ipc::ThreadRecord::StackAmount::kMinimal);
  }
}

}  // namespace
}  // namespace debug_agent

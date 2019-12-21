// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_thread.h"

#include <lib/zx/event.h>

#include <thread>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/mock_arch_provider.h"
#include "src/developer/debug/debug_agent/object_provider.h"

namespace debug_agent {

using namespace debug_ipc;

namespace {

// TODO(donosoc): These helpers are replicated throughout many tests.
//                Collapse them into one place.
std::vector<uint8_t> CreateData(size_t length) {
  std::vector<uint8_t> data;
  data.reserve(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = length;
  for (size_t i = 0; i < length; i++) {
    data.emplace_back(base - i);
  }
  return data;
}

debug_ipc::Register CreateRegister(RegisterID id, size_t length) {
  debug_ipc::Register reg;
  reg.id = id;
  reg.data = CreateData(length);
  return reg;
}

bool FindRegister(const std::vector<Register>& regs, RegisterID id) {
  for (const auto& reg : regs) {
    if (reg.id == id)
      return true;
  }
  return false;
}

// Sets the given register in the vector, overwriting an existing one of the same ID, or adding a
// new one othewise.
void SetRegister(const Register& reg, std::vector<Register>* regs) {
  for (auto& cur : *regs) {
    if (cur.id == reg.id) {
      cur = reg;
      return;
    }
  }
  regs->push_back(reg);
}

class FakeArchProvider : public MockArchProvider {
 public:
  zx_status_t ReadRegisters(const debug_ipc::RegisterCategory& type, const zx::thread&,
                            std::vector<debug_ipc::Register>* out) override {
    auto it = to_read_.find(type);
    if (it == to_read_.end())
      return ZX_ERR_INVALID_ARGS;

    out->insert(out->end(), it->second.begin(), it->second.end());
    return ZX_OK;
  }

  // This also updates the "to_read" so the value will be updated next time it is read.
  zx_status_t WriteRegisters(const debug_ipc::RegisterCategory& cat,
                             const std::vector<debug_ipc::Register>& registers,
                             zx::thread*) override {
    auto& written_cat = regs_written_[cat];
    for (const Register& reg : registers) {
      written_cat.push_back(reg);
      SetRegister(reg, &to_read_[cat]);
    }

    return ZX_OK;
  }

  void AddCategory(RegisterCategory type, size_t reg_count) {
    auto& cat = to_read_[type];
    cat.reserve(reg_count);
    for (uint32_t i = 0; i < reg_count; i++) {
      auto& reg = cat.emplace_back();
      reg.id = static_cast<debug_ipc::RegisterID>(i);
      // No data for now.
    }
  }

  const std::map<RegisterCategory, std::vector<Register>>& regs_written() const {
    return regs_written_;
  }

 private:
  std::map<RegisterCategory, std::vector<Register>> to_read_;
  std::map<RegisterCategory, std::vector<Register>> regs_written_;
};

class FakeProcess : public DebuggedProcess {
 public:
  FakeProcess(zx_koid_t koid, std::shared_ptr<FakeArchProvider> arch_provider)
      : DebuggedProcess(nullptr, {koid, "", zx::process(), std::move(arch_provider),
                                  std::make_unique<ObjectProvider>()}) {}
  ~FakeProcess() = default;

  DebuggedThread* CreateThread(zx_koid_t tid) {
    if (!thread_) {
      DebuggedThread::CreateInfo create_info = {};
      create_info.koid = tid;
      create_info.creation_option = ThreadCreationOption::kSuspendedKeepSuspended;
      create_info.arch_provider = arch_provider_;
      create_info.object_provider = std::make_unique<ObjectProvider>();
      thread_ = std::make_unique<DebuggedThread>(nullptr, std::move(create_info));
    }
    return thread_.get();
  }

 private:
  std::unique_ptr<DebuggedThread> thread_;
};

TEST(DebuggedThread, ReadRegisters) {
  auto arch_provider = std::make_shared<FakeArchProvider>();

  constexpr size_t kGeneralCount = 12u;
  arch_provider->AddCategory(RegisterCategory::kGeneral, kGeneralCount);

  FakeProcess fake_process(1u, arch_provider);
  DebuggedThread* thread = fake_process.CreateThread(1u);

  std::vector<RegisterCategory> cats_to_get = {RegisterCategory::kGeneral};

  std::vector<Register> registers;
  thread->ReadRegisters(cats_to_get, &registers);
  EXPECT_EQ(registers.size(), kGeneralCount);
}

TEST(DebuggedThread, ReadRegistersGettingErrorShouldStillReturnTheRest) {
  auto arch_provider = std::make_shared<FakeArchProvider>();

  FakeProcess fake_process(1u, arch_provider);
  DebuggedThread* thread = fake_process.CreateThread(1u);

  constexpr size_t kGeneralCount = 12u;
  constexpr size_t kDebugCount = 33u;
  arch_provider->AddCategory(RegisterCategory::kGeneral, kGeneralCount);
  arch_provider->AddCategory(RegisterCategory::kDebug, kDebugCount);

  std::vector<RegisterCategory> cats_to_get = {RegisterCategory::kGeneral,
                                               RegisterCategory::kVector, RegisterCategory::kDebug};

  std::vector<Register> registers;
  thread->ReadRegisters(cats_to_get, &registers);
  EXPECT_EQ(registers.size(), kGeneralCount + kDebugCount);
}

TEST(DebuggedThread, WriteRegisters) {
  auto arch_provider = std::make_shared<FakeArchProvider>();

  FakeProcess fake_process(1u, arch_provider);
  DebuggedThread* thread = fake_process.CreateThread(1u);

  std::vector<Register> regs_to_write;
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_rax, 16));
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_rip, 16));
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_rsp, 16));

  regs_to_write.push_back(CreateRegister(RegisterID::kX64_fcw, 16));
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_st0, 16));

  regs_to_write.push_back(CreateRegister(RegisterID::kX64_mxcsr, 16));
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_ymm1, 16));
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_ymm2, 16));
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_ymm3, 16));

  regs_to_write.push_back(CreateRegister(RegisterID::kX64_dr1, 16));
  regs_to_write.push_back(CreateRegister(RegisterID::kX64_dr7, 16));

  // The registers retrieved from the "system" after writing.
  std::vector<debug_ipc::Register> reported_written;

  thread->WriteRegisters(regs_to_write, &reported_written);

  // The registers the mock told us it wrote.
  const auto& regs_written = arch_provider->regs_written();
  ASSERT_EQ(regs_written.size(), 4u);
  EXPECT_EQ(regs_written.count(RegisterCategory::kNone), 0u);

  // Make sure the API echoed back the registers we asked it to write.
  EXPECT_TRUE(FindRegister(reported_written, RegisterID::kX64_rax));
  EXPECT_TRUE(FindRegister(reported_written, RegisterID::kX64_rip));
  EXPECT_TRUE(FindRegister(reported_written, RegisterID::kX64_rsp));

  auto it = regs_written.find(RegisterCategory::kGeneral);
  ASSERT_NE(it, regs_written.end());
  ASSERT_EQ(it->second.size(), 3u);
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_rax));
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_rip));
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_rsp));

  it = regs_written.find(RegisterCategory::kFloatingPoint);
  ASSERT_NE(it, regs_written.end());
  ASSERT_EQ(it->second.size(), 2u);
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_fcw));
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_st0));

  it = regs_written.find(RegisterCategory::kVector);
  ASSERT_NE(it, regs_written.end());
  ASSERT_EQ(it->second.size(), 4u);
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_mxcsr));
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_ymm1));
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_ymm2));
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_ymm3));

  it = regs_written.find(RegisterCategory::kDebug);
  ASSERT_NE(it, regs_written.end());
  ASSERT_EQ(it->second.size(), 2u);
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_dr1));
  EXPECT_TRUE(FindRegister(it->second, RegisterID::kX64_dr7));
}

TEST(DebuggedThread, FillThreadRecord) {
  auto arch_provider = std::make_shared<FakeArchProvider>();
  auto object_provider = std::make_shared<ObjectProvider>();

  constexpr zx_koid_t kProcessKoid = 0x8723456;
  FakeProcess fake_process(kProcessKoid, arch_provider);

  zx::thread current_thread;
  zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &current_thread);

  zx_koid_t current_thread_koid = object_provider->KoidForObject(current_thread);

  // Set the name of the current thread so we can find it.
  const std::string thread_name("ProcessInfo test thread name");
  std::string old_name = object_provider->NameForObject(current_thread);
  current_thread.set_property(ZX_PROP_NAME, thread_name.c_str(), thread_name.size());
  EXPECT_EQ(thread_name, object_provider->NameForObject(current_thread));

  DebuggedThread::CreateInfo create_info = {};
  create_info.process = &fake_process;
  create_info.koid = current_thread_koid;
  create_info.handle = std::move(current_thread);
  create_info.arch_provider = arch_provider;
  create_info.object_provider = object_provider;
  auto thread = std::make_unique<DebuggedThread>(nullptr, std::move(create_info));

  // Request no stack since this thread should be running.
  debug_ipc::ThreadRecord record;
  thread->FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kNone, nullptr, &record);

  // Put back the old thread name for hygiene.
  zx::thread::self()->set_property(ZX_PROP_NAME, old_name.c_str(), old_name.size());

  // Validate the results.
  EXPECT_EQ(kProcessKoid, record.process_koid);
  EXPECT_EQ(current_thread_koid, record.thread_koid);
  EXPECT_EQ(thread_name, record.name);
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kRunning, record.state);
  EXPECT_EQ(debug_ipc::ThreadRecord::StackAmount::kNone, record.stack_amount);
  EXPECT_TRUE(record.frames.empty());
}

// Ref-counted Suspension --------------------------------------------------------------------------

TEST(DebuggedThread, NormalSuspension) {
  auto arch_provider = std::make_shared<FakeArchProvider>();
  auto object_provider = std::make_shared<ObjectProvider>();

  constexpr zx_koid_t kProcessKoid = 0x8723456;
  FakeProcess fake_process(kProcessKoid, arch_provider);

  // Create the event for coordination.
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  std::unique_ptr<DebuggedThread> debugged_thread;

  std::thread other_thread([&]() mutable {
    // Create a debugged thread for this other thread.
    zx::thread current_thread;
    zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &current_thread);
    zx_koid_t current_thread_koid = object_provider->KoidForObject(current_thread);

    DebuggedThread::CreateInfo create_info = {};
    create_info.process = &fake_process;
    create_info.koid = current_thread_koid;
    create_info.handle = std::move(current_thread);
    create_info.arch_provider = arch_provider;
    create_info.object_provider = object_provider;
    debugged_thread = std::make_unique<DebuggedThread>(nullptr, std::move(create_info));

    // Let the test know it can continue.
    ASSERT_EQ(event.signal(0, ZX_USER_SIGNAL_0), ZX_OK);

    // Wait until the test tells us we're ready.
    ASSERT_EQ(event.wait_one(ZX_USER_SIGNAL_1, zx::time::infinite(), nullptr), ZX_OK);
  });

  // Wait until the test is ready.
  ASSERT_EQ(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr), ZX_OK);

  ASSERT_FALSE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 0);

  // First suspension should be marked as such.
  ASSERT_TRUE(debugged_thread->Suspend(true));
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 1);

  // Suspending again should not add suspension.
  ASSERT_FALSE(debugged_thread->Suspend(true));
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 1);

  debugged_thread->ResumeSuspension();
  ASSERT_FALSE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 0);

  // Another firwst suspension should be marked as such.
  ASSERT_TRUE(debugged_thread->Suspend(true));
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 1);

  debugged_thread->ResumeSuspension();
  ASSERT_FALSE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 0);

  // Tell the other thread we're done.
  ASSERT_EQ(event.signal(0, ZX_USER_SIGNAL_1), ZX_OK);
  other_thread.join();
}

TEST(DebuggedThread, RefCountedSuspension) {
  auto arch_provider = std::make_shared<FakeArchProvider>();
  auto object_provider = std::make_shared<ObjectProvider>();

  constexpr zx_koid_t kProcessKoid = 0x8723456;
  FakeProcess fake_process(kProcessKoid, arch_provider);

  // Create the event for coordination.
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  std::unique_ptr<DebuggedThread> debugged_thread;

  std::thread other_thread([&]() mutable {
    // Create a debugged thread for this other thread.
    zx::thread current_thread;
    zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &current_thread);
    zx_koid_t current_thread_koid = object_provider->KoidForObject(current_thread);

    DebuggedThread::CreateInfo create_info = {};
    create_info.process = &fake_process;
    create_info.koid = current_thread_koid;
    create_info.handle = std::move(current_thread);
    create_info.arch_provider = arch_provider;
    create_info.object_provider = object_provider;
    debugged_thread = std::make_unique<DebuggedThread>(nullptr, std::move(create_info));

    // Let the test know it can continue.
    ASSERT_EQ(event.signal(0, ZX_USER_SIGNAL_0), ZX_OK);

    // Wait until the test tells us we're ready.
    ASSERT_EQ(event.wait_one(ZX_USER_SIGNAL_1, zx::time::infinite(), nullptr), ZX_OK);
  });

  // Wait until the test is ready.
  ASSERT_EQ(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr), ZX_OK);

  ASSERT_FALSE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 0);

  auto token1 = debugged_thread->RefCountedSuspend();
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 1);

  token1.reset();
  ASSERT_FALSE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 0);

  token1 = debugged_thread->RefCountedSuspend();
  auto token2 = debugged_thread->RefCountedSuspend();
  auto token3 = debugged_thread->RefCountedSuspend();
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 3);

  token3.reset();
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 2);

  token2.reset();
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 1);

  // Adding a normal suspension should add a ref counted token.
  ASSERT_FALSE(debugged_thread->Suspend(true));
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 2);

  // Suspending again should not add a token.
  ASSERT_FALSE(debugged_thread->Suspend(true));
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 2);

  debugged_thread->ResumeSuspension();
  ASSERT_TRUE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 1);

  token1.reset();
  ASSERT_FALSE(debugged_thread->IsSuspended());
  ASSERT_EQ(debugged_thread->ref_counted_suspend_count(), 0);

  // Tell the other thread we're done.
  ASSERT_EQ(event.signal(0, ZX_USER_SIGNAL_1), ZX_OK);
  other_thread.join();
}

}  // namespace

}  // namespace debug_agent

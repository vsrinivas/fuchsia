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
#include "src/developer/debug/debug_agent/mock_thread_handle.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/debug_agent/zircon_thread_handle.h"

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

// Creates a thread with no process backed by a MockThreadHandle and returns the thread and the
// handle.
std::pair<std::unique_ptr<DebuggedThread>, MockThreadHandle*> CreateThread(zx_koid_t process_koid,
                                                                           zx_koid_t thread_koid) {
  auto owning_handle = std::make_unique<MockThreadHandle>(process_koid, thread_koid);
  MockThreadHandle* handle = owning_handle.get();

  DebuggedThread::CreateInfo create_info = {};
  create_info.process = nullptr;
  create_info.koid = thread_koid;
  create_info.handle = std::move(owning_handle);
  create_info.creation_option = ThreadCreationOption::kSuspendedKeepSuspended;
  create_info.arch_provider = std::make_shared<MockArchProvider>();
  create_info.object_provider = std::make_unique<ObjectProvider>();
  return {std::make_unique<DebuggedThread>(nullptr, std::move(create_info)), handle};
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
      create_info.process = this;
      create_info.koid = tid;
      create_info.handle = std::make_unique<MockThreadHandle>(koid(), tid);
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
    // TODO(brettw) this should use a MockThreadHandle but the suspensions are not yet hooked up
    // with that in a way that will make the DebuggedThread happy.
    create_info.handle = std::make_unique<ZirconThreadHandle>(
        arch_provider, kProcessKoid, current_thread_koid, std::move(current_thread));
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
    // TODO(brettw) this should use a MockThreadHandle but the suspensions are not yet hooked up
    // with that in a way that will make the DebuggedThread happy.
    create_info.handle = std::make_unique<ZirconThreadHandle>(
        arch_provider, kProcessKoid, current_thread_koid, std::move(current_thread));
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

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debugged_thread.h"
#include "garnet/bin/debug_agent/arch.h"
#include "garnet/bin/debug_agent/debugged_process.h"
#include "gtest/gtest.h"

namespace debug_agent {

using debug_ipc::RegisterCategory;

namespace {

class FakeArchProvider : public arch::ArchProvider {
 public:
  bool GetRegisters(const debug_ipc::RegisterCategory::Type& type,
                    const zx::thread&,
                    std::vector<debug_ipc::Register>* out) override {
    auto it = categories_.find(type);
    if (it == categories_.end())
      return false;
    *out = it->second.registers;
    return true;
  }

  void AddCategory(RegisterCategory::Type type, size_t reg_count) {
    auto& cat = categories_[type];
    cat.type = type;
    cat.registers.reserve(reg_count);
    for (uint32_t i = 0; i < reg_count; i++) {
      auto& reg = cat.registers.emplace_back();
      reg.id = static_cast<debug_ipc::RegisterID>(i);
      // No data for now.
    }
  }

 private:
  std::map<RegisterCategory::Type, RegisterCategory> categories_;
};

class ScopedFakeArchProvider {
 public:
  ScopedFakeArchProvider() {
    auto fake_arch = std::make_unique<FakeArchProvider>();
    fake_arch_ = fake_arch.get();
    arch::ArchProvider::Set(std::move(fake_arch));
  }

  ~ScopedFakeArchProvider() { arch::ArchProvider::Set(nullptr); }

  FakeArchProvider* get() const { return fake_arch_; }

 private:
  FakeArchProvider* fake_arch_;
};

class FakeProcess : public DebuggedProcess {
 public:
  FakeProcess(zx_koid_t koid)
      : DebuggedProcess(nullptr, koid, zx::process(), true) {}
  ~FakeProcess() = default;

  DebuggedThread* CreateThread(zx_koid_t tid) {
    if (!thread_) {
      thread_ = std::make_unique<DebuggedThread>(this, zx::thread(), 1, false);
    }
    return thread_.get();
  }

 private:
  std::unique_ptr<DebuggedThread> thread_;
};

TEST(DebuggedThread, GetsRegisters) {
  ScopedFakeArchProvider scoped_arch_provider;
  FakeArchProvider* arch = scoped_arch_provider.get();

  constexpr size_t kGeneralCount = 12u;
  arch->AddCategory(RegisterCategory::Type::kGeneral, kGeneralCount);

  FakeProcess fake_process(1u);
  DebuggedThread* thread = fake_process.CreateThread(1u);

  std::vector<RegisterCategory::Type> cats_to_get = {
      RegisterCategory::Type::kGeneral};

  std::vector<RegisterCategory> categories;
  thread->GetRegisters(cats_to_get, &categories);

  ASSERT_EQ(categories.size(), 1u);
  auto& cat = categories.front();
  EXPECT_EQ(cat.type, RegisterCategory::Type::kGeneral);
  EXPECT_EQ(cat.registers.size(), kGeneralCount);
}

TEST(DebuggedThread, GettingErrorShouldStillReturnTheRest) {
  ScopedFakeArchProvider scoped_arch_provider;
  FakeArchProvider* arch = scoped_arch_provider.get();

  FakeProcess fake_process(1u);
  DebuggedThread* thread = fake_process.CreateThread(1u);

  constexpr size_t kGeneralCount = 12u;
  constexpr size_t kDebugCount = 33u;
  arch->AddCategory(RegisterCategory::Type::kGeneral, kGeneralCount);
  arch->AddCategory(RegisterCategory::Type::kDebug, kDebugCount);

  std::vector<RegisterCategory::Type> cats_to_get = {
      RegisterCategory::Type::kGeneral, RegisterCategory::Type::kVector,
      RegisterCategory::Type::kDebug};

  std::vector<RegisterCategory> categories;
  thread->GetRegisters(cats_to_get, &categories);

  ASSERT_EQ(categories.size(), 2u);
  EXPECT_EQ(categories[0].type, RegisterCategory::Type::kGeneral);
  EXPECT_EQ(categories[0].registers.size(), kGeneralCount);
  EXPECT_EQ(categories[1].type, RegisterCategory::Type::kDebug);
  EXPECT_EQ(categories[1].registers.size(), kDebugCount);
}

}  // namespace

}  // namespace debug_agent

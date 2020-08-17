// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_thread.h"

#include <lib/zx/event.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/mock_debug_agent_harness.h"
#include "src/developer/debug/debug_agent/mock_exception_handle.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_process_handle.h"
#include "src/developer/debug/debug_agent/mock_thread.h"
#include "src/developer/debug/debug_agent/mock_thread_handle.h"
#include "src/developer/debug/debug_agent/remote_api.h"
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

TEST(DebuggedThread, Resume) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcessKoid = 0x8723456;
  MockProcess process(harness.debug_agent(), kProcessKoid);

  constexpr zx_koid_t kThreadKoid = 0x8723457;
  MockThread* thread = process.AddThread(kThreadKoid);
  EXPECT_FALSE(thread->in_exception());

  uint32_t exception_state = 0u;
  debug_ipc::ExceptionStrategy exception_strategy = debug_ipc::ExceptionStrategy::kNone;
  auto exception = std::make_unique<MockExceptionHandle>(
      [&exception_state](uint32_t new_state) { exception_state = new_state; },
      [&exception_strategy](debug_ipc::ExceptionStrategy new_strategy) {
        exception_strategy = new_strategy;
      });
  thread->set_exception_handle(std::move(exception));
  EXPECT_TRUE(thread->in_exception());
  thread->ClientResume(
      debug_ipc::ResumeRequest{.how = debug_ipc::ResumeRequest::How::kResolveAndContinue});
  EXPECT_FALSE(thread->in_exception());
  EXPECT_EQ(exception_state, ZX_EXCEPTION_STATE_HANDLED);
  EXPECT_EQ(exception_strategy, debug_ipc::ExceptionStrategy::kNone);

  exception_state = 0u;
  exception_strategy = debug_ipc::ExceptionStrategy::kNone;
  exception = std::make_unique<MockExceptionHandle>(
      [&exception_state](uint32_t new_state) { exception_state = new_state; },
      [&exception_strategy](debug_ipc::ExceptionStrategy new_strategy) {
        exception_strategy = new_strategy;
      });
  thread->set_exception_handle(std::move(exception));
  EXPECT_TRUE(thread->in_exception());
  thread->ClientResume(
      debug_ipc::ResumeRequest{.how = debug_ipc::ResumeRequest::How::kForwardAndContinue});
  EXPECT_FALSE(thread->in_exception());
  EXPECT_EQ(exception_state, 0u);
  EXPECT_EQ(exception_strategy, debug_ipc::ExceptionStrategy::kSecondChance);
}

TEST(DebuggedThread, OnException) {
  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  constexpr zx_koid_t kProcessKoid = 0x8723456;
  MockProcess process(harness.debug_agent(), kProcessKoid);

  constexpr zx_koid_t kThreadKoid = 0x8723457;
  MockThread* thread = process.AddThread(kThreadKoid);
  EXPECT_FALSE(thread->in_exception());

  // Policy: general exceptions initially handled as first-chance.
  // Exception: general, first-chance.
  // Expected: no applied strategy.
  {
    debug_ipc::ExceptionStrategy applied_strategy = debug_ipc::ExceptionStrategy::kNone;
    auto exception = std::make_unique<MockExceptionHandle>(
        [](uint32_t new_state) {},
        [&applied_strategy](debug_ipc::ExceptionStrategy new_strategy) {
          applied_strategy = new_strategy;
        });
    exception->set_type(debug_ipc::ExceptionType::kGeneral);
    exception->SetStrategy(debug_ipc::ExceptionStrategy::kFirstChance);
    applied_strategy = debug_ipc::ExceptionStrategy::kNone;  // Clear previously set.

    thread->OnException(std::move(exception));

    EXPECT_EQ(debug_ipc::ExceptionStrategy::kNone, applied_strategy);
  }

  // Policy: general exceptions initially handled as first-chance.
  // Exception: general, second-chance.
  // Expected: no applied strategy (as this isn't our initial handling).
  {
    debug_ipc::ExceptionStrategy applied_strategy = debug_ipc::ExceptionStrategy::kNone;
    auto exception = std::make_unique<MockExceptionHandle>(
        [](uint32_t new_state) {},
        [&applied_strategy](debug_ipc::ExceptionStrategy new_strategy) {
          applied_strategy = new_strategy;
        });
    exception->set_type(debug_ipc::ExceptionType::kGeneral);
    exception->SetStrategy(debug_ipc::ExceptionStrategy::kSecondChance);
    applied_strategy = debug_ipc::ExceptionStrategy::kNone;  // Clear previously set.

    thread->OnException(std::move(exception));

    EXPECT_EQ(debug_ipc::ExceptionStrategy::kNone, applied_strategy);
  }

  // Update policy so that general exceptions are handled initially as
  // second-chance.
  const debug_ipc::UpdateGlobalSettingsRequest request = {
      .exception_strategies =
          {
              {
                  .type = debug_ipc::ExceptionType::kGeneral,
                  .value = debug_ipc::ExceptionStrategy::kSecondChance,
              },
          },
  };
  debug_ipc::UpdateGlobalSettingsReply reply;
  remote_api->OnUpdateGlobalSettings(request, &reply);
  EXPECT_EQ(ZX_OK, reply.status);

  // Policy: general exceptions initially handled as second-chance.
  // Exception: general, first-chance.
  // Expected: applied strategy of second-chance.
  {
    debug_ipc::ExceptionStrategy applied_strategy = debug_ipc::ExceptionStrategy::kNone;
    auto exception = std::make_unique<MockExceptionHandle>(
        [](uint32_t new_state) {},
        [&applied_strategy](debug_ipc::ExceptionStrategy new_strategy) {
          applied_strategy = new_strategy;
        });
    exception->set_type(debug_ipc::ExceptionType::kGeneral);
    exception->SetStrategy(debug_ipc::ExceptionStrategy::kFirstChance);
    applied_strategy = debug_ipc::ExceptionStrategy::kNone;  // Clear previously set.

    thread->OnException(std::move(exception));

    EXPECT_EQ(debug_ipc::ExceptionStrategy::kSecondChance, applied_strategy);
  }

  // Policy: general exceptions initially handled as second-chance.
  // Exception: general, second-chance.
  // Expected: no applied strategy.
  {
    debug_ipc::ExceptionStrategy applied_strategy = debug_ipc::ExceptionStrategy::kNone;
    auto exception = std::make_unique<MockExceptionHandle>(
        [](uint32_t new_state) {},
        [&applied_strategy](debug_ipc::ExceptionStrategy new_strategy) {
          applied_strategy = new_strategy;
        });
    exception->set_type(debug_ipc::ExceptionType::kGeneral);
    exception->SetStrategy(debug_ipc::ExceptionStrategy::kSecondChance);
    applied_strategy = debug_ipc::ExceptionStrategy::kNone;  // Clear previously set.

    thread->OnException(std::move(exception));

    EXPECT_EQ(debug_ipc::ExceptionStrategy::kNone, applied_strategy);
  }
}

}  // namespace

}  // namespace debug_agent

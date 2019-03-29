// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debugger_utils/threads.h"

#include <algorithm>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/util.h"

namespace debugger_utils {

uint32_t GetThreadOsState(zx_handle_t thread) {
    zx_info_thread_t info;
    zx_status_t status =
      zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info),
                         nullptr, nullptr);
    FXL_CHECK(status == ZX_OK) << status;
    return info.state;
}

uint32_t GetThreadOsState(const zx::thread& thread) {
  return GetThreadOsState(thread.get());
}

const char* ThreadOsStateName(uint32_t state) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (state) {
    CASE_TO_STR(ZX_THREAD_STATE_NEW);
    CASE_TO_STR(ZX_THREAD_STATE_RUNNING);
    CASE_TO_STR(ZX_THREAD_STATE_SUSPENDED);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED);
    CASE_TO_STR(ZX_THREAD_STATE_DYING);
    CASE_TO_STR(ZX_THREAD_STATE_DEAD);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_EXCEPTION);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_SLEEPING);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_FUTEX);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_PORT);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_CHANNEL);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_WAIT_ONE);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_WAIT_MANY);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_INTERRUPT);
    CASE_TO_STR(ZX_THREAD_STATE_BLOCKED_PAGER);
    default:
      return nullptr;
  }
#undef CASE_TO_STR
}

const std::string ThreadOsStateNameAsString(uint32_t state) {
  const char* name = ThreadOsStateName(state);
  if (name) {
    return std::string(name);
  }
  return fxl::StringPrintf("UNKNOWN(%u)", state);
}

zx_status_t WithThreadSuspended(
    const zx::thread& thread, zx::duration thread_suspend_timeout,
    const WithThreadSuspendedFunction& function) {
  zx::suspend_token suspend_token;
  auto status = thread.suspend(&suspend_token);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "unable to suspend thread "
                   << GetKoid(thread)
                   << ": " << ZxErrorString(status);
    return status;
  }

  zx_signals_t pending;
  status = thread.wait_one(ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED,
                           zx::deadline_after(thread_suspend_timeout),
                           &pending);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "error waiting for thread "
                   << GetKoid(thread)
                   << " to suspend: "
                   << ZxErrorString(status);
    return status;
  }
  if (pending & ZX_THREAD_TERMINATED) {
    FXL_LOG(WARNING) << "thread "
                     << GetKoid(thread)
                     << " terminated";
    return ZX_ERR_NOT_FOUND;
  }

  return function(thread);
}

zx_status_t WithAllThreadsSuspended(
    const std::vector<zx::thread>& threads,
    zx::duration thread_suspend_timeout,
    const WithThreadSuspendedFunction& function) {
  std::vector<zx::suspend_token> suspend_tokens;
  for (const auto& thread : threads) {
    zx::suspend_token suspend_token;
    auto status = thread.suspend(&suspend_token);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "unable to suspend thread "
                       << GetKoid(thread)
                       << ": " << ZxErrorString(status);
      suspend_tokens.emplace_back(zx::suspend_token{});
    } else {
      suspend_tokens.emplace_back(std::move(suspend_token));
    }
  }

  for (size_t i = 0; i < threads.size(); ++i) {
    if (!suspend_tokens[i].is_valid())
      continue;
    zx_signals_t pending;
    const zx::thread& thread{threads[i]};
    auto status = thread.wait_one(ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED,
                                  zx::deadline_after(thread_suspend_timeout),
                                  &pending);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "error waiting for thread "
                     << GetKoid(thread)
                     << " to suspend: "
                     << ZxErrorString(status);
      return status;
    }
    if (pending & ZX_THREAD_TERMINATED) {
      FXL_LOG(WARNING) << "thread "
                       << GetKoid(thread)
                       << " terminated";
      suspend_tokens[i].reset();
      continue;
    }
  }

  for (size_t i = 0; i < threads.size(); ++i) {
    if (suspend_tokens[i].is_valid()) {
      const zx::thread& thread{threads[i]};
      auto status = function(thread);
      if (status != ZX_OK)
        return status;
    }
  }

  return ZX_OK;
}

}  // namespace debugger_utils

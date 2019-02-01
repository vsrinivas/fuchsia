// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debugger_utils/threads.h"

#include <algorithm>
#include <lib/fxl/logging.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/util.h"

namespace debugger_utils {

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

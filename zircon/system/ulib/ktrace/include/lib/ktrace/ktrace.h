// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_KTRACE_KTRACE_H_
#define LIB_KTRACE_KTRACE_H_
#include <lib/svc/service.h>

#include <functional>

const zx_service_provider_t* ktrace_get_service_provider(void);

// Exposed for testing.
namespace internal {

struct KTraceSysCalls {
  std::function<zx_status_t(zx_handle_t, uint32_t, uint32_t, void*)> ktrace_control;
  std::function<zx_status_t(zx_handle_t, void*, uint32_t, size_t, size_t*)> ktrace_read;
};

zx_status_t OverrideKTraceSysCall(void* ctx, KTraceSysCalls sys_calls);

}  // namespace internal

#endif  // LIB_KTRACE_KTRACE_H_

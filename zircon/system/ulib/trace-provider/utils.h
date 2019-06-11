// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_UTILS_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_UTILS_H_

#include <zircon/types.h>

namespace trace {
namespace internal {

zx_koid_t GetPid();

} // namespace internal
} // namespace trace

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_UTILS_H_

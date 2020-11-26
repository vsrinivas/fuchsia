// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRACE_H_
#define FS_TRACE_H_

#include <stdio.h>

#ifdef __Fuchsia__
#include <lib/trace/event.h>
#else
// TODO(fxbug.dev/31305): If ulib/trace defines a no-op
// version of these macros, we won't need to.
//
// Redefine tracing macros as no-ops for host-side tools
#define TRACE_DURATION(args...)
#define TRACE_FLOW_BEGIN(args...)
#define TRACE_FLOW_STEP(args...)
#define TRACE_FLOW_END(args...)
#endif

// Enable trace printf()s

namespace fs {

constexpr bool trace_debug_enabled() {
#ifdef FS_TRACE_DEBUG_ENABLED
  return true;
#else
  return false;
#endif
}

}  // namespace fs

#endif  // FS_TRACE_H_

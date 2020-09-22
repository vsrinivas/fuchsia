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

#ifdef FS_TRACE_DEBUG_ENABLED
#define FS_TRACE_DEBUG(fmt...) fprintf(stderr, fmt)
#else
#define FS_TRACE_DEBUG(fmt...)
#endif
#define FS_TRACE_INFO(fmt...) fprintf(stderr, fmt)
#define FS_TRACE_WARN(fmt...) fprintf(stderr, fmt)
#define FS_TRACE_ERROR(fmt...) fprintf(stderr, fmt)

#endif  // FS_TRACE_H_

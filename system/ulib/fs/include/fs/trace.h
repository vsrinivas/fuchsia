// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#ifdef __Fuchsia__
#include <trace/event.h>
#else
// TODO(ZX-1407): If ulib/trace defines a no-op
// version of these macros, we won't need to.
//
// Redefine tracing macros as no-ops for host-side tools
#define TRACE_DURATION(args...)
#define TRACE_FLOW_BEGIN(args...)
#define TRACE_FLOW_STEP(args...)
#define TRACE_FLOW_END(args...)
#endif

// Enable trace printf()s

#define FS_TRACE_ERROR(fmt...) fprintf(stderr, fmt)
#define FS_TRACE_WARN(fmt...) fprintf(stderr, fmt)

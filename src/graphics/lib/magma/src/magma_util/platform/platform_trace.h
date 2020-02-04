// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_TRACE_H
#define PLATFORM_TRACE_H

#ifdef __Fuchsia__
#include <lib/fit/function.h>
#endif

#include <functional>

#if MAGMA_ENABLE_TRACING
#include <trace/event.h>

#include "trace-vthread/event_vthread.h"
#define TRACE_NONCE_DECLARE(x) uint64_t x = TRACE_NONCE()
#else
#define TRACE_COUNTER(args...)
#define TRACE_NONCE() 0
#define TRACE_NONCE_DECLARE(x)
#define TRACE_ASYNC_BEGIN(category, name, id, args...)
#define TRACE_ASYNC_END(category, name, id, args...)
#define TRACE_SCOPE_GLOBAL 0
#define TRACE_INSTANT(category, name, id, args...)
#define TRACE_DURATION(category, name, args...)
#define TRACE_DURATION_BEGIN(category, name, args...)
#define TRACE_DURATION_END(category, name, args...)
#define TRACE_FLOW_BEGIN(category, name, id, args...)
#define TRACE_FLOW_STEP(category, name, id, args...)
#define TRACE_FLOW_END(category, name, id, args...)
#define TRACE_VTHREAD_DURATION_BEGIN(category, name, vthread_name, vthread_id, timestamp, args...)
#define TRACE_VTHREAD_DURATION_END(category, name, vthread_name, vthread_id, timestamp, args...)
#define TRACE_VTHREAD_FLOW_BEGIN(category, name, vthread_name, vthread_id, flow_id, timestamp, \
                                 args...)
#define TRACE_VTHREAD_FLOW_STEP(category, name, vthread_name, vthread_id, flow_id, timestamp, \
                                args...)
#define TRACE_VTHREAD_FLOW_END(category, name, vthread_name, vthread_id, flow_id, timestamp, \
                               args...)
#endif

namespace magma {

class PlatformTrace {
 public:
  // Returns the current time in ticks.
  static uint64_t GetCurrentTicks();
};

class PlatformTraceObserver {
 public:
  virtual ~PlatformTraceObserver() {}

  static std::unique_ptr<PlatformTraceObserver> Create();

  virtual bool Initialize() = 0;

#ifdef __Fuchsia__
  // Invokes the given |callback| (on a different thread) when the tracing state changes.
  virtual void SetObserver(fit::function<void(bool trace_enabled)> callback) = 0;
#endif
};

}  // namespace magma

#endif  // PLATFORM_TRACE_H

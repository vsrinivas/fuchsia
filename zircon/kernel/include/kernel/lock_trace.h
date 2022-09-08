// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_LOCK_TRACE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_LOCK_TRACE_H_

#include <lib/ktrace.h>

#ifndef LOCK_TRACING_ENABLED
#define LOCK_TRACING_ENABLED false
#endif

using LockTraceEnabled = TraceEnabled<LOCK_TRACING_ENABLED>;

#define INTERNAL_LOCK_TRACE_NAME_CAT2(x, y) x##y
#define INTERNAL_LOCK_TRACE_NAME_CAT(x, y) INTERNAL_LOCK_TRACE_NAME_CAT2(x, y)
#define LOCK_TRACE_VARIABLE_NAME(name) INTERNAL_LOCK_TRACE_NAME_CAT(name, __COUNTER__)

#define LOCK_TRACE_DURATION(label, args...)                                   \
  TraceDuration<LockTraceEnabled, KTRACE_GRP_SCHEDULER, TraceContext::Thread> \
  LOCK_TRACE_VARIABLE_NAME(duration_) {                                       \
    KTRACE_STRING_REF(label), ##args                                          \
  }

#define LOCK_TRACE_DURATION_BEGIN(label, args...)                                       \
  ktrace_begin_duration(LockTraceEnabled{}, TraceContext::Thread, KTRACE_GRP_SCHEDULER, \
                        KTRACE_STRING_REF(label), ##args)

#define LOCK_TRACE_DURATION_END(label, args...)                                       \
  ktrace_end_duration(LockTraceEnabled{}, TraceContext::Thread, KTRACE_GRP_SCHEDULER, \
                      KTRACE_STRING_REF(label), ##args)

#define LOCK_TRACE_FLOW_BEGIN(label, args...)                                       \
  ktrace_flow_begin(LockTraceEnabled{}, TraceContext::Thread, KTRACE_GRP_SCHEDULER, \
                    KTRACE_STRING_REF(label), ##args)

#define LOCK_TRACE_FLOW_STEP(label, args...)                                       \
  ktrace_flow_step(LockTraceEnabled{}, TraceContext::Thread, KTRACE_GRP_SCHEDULER, \
                   KTRACE_STRING_REF(label), ##args)

#define LOCK_TRACE_FLOW_END(label, args...)                                       \
  ktrace_flow_end(LockTraceEnabled{}, TraceContext::Thread, KTRACE_GRP_SCHEDULER, \
                  KTRACE_STRING_REF(label), ##args)

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_LOCK_TRACE_H_

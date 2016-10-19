// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_GLUE_KTRACE_TRACE_EVENT_H_
#define APPS_MOZART_GLUE_KTRACE_TRACE_EVENT_H_

// A slightly hacky way to route trace events to the Magenta ktrace mechanism
// for visualization with TraceViz.
// TODO(jeffbrown): This should be removed once user-space tracing is ready
// and we have built tools to aggregate user-space and kernel trace data.

namespace ktrace {

void TraceEventDurationBegin(const char* cat, const char* name);
void TraceEventDurationEnd(const char* cat, const char* name);
void TraceEventAsyncBegin(const char* cat, const char* name, int id);
void TraceEventAsyncEnd(const char* cat, const char* name, int id);
void TraceEventInstant(const char* cat, const char* name);

class ScopedTraceEvent {
 public:
  ScopedTraceEvent(const char* cat, const char* name) : cat_(cat), name_(name) {
    TraceEventDurationBegin(cat, name);
  }

  ~ScopedTraceEvent() { TraceEventDurationEnd(cat_, name_); }

 private:
  const char* const cat_;
  const char* const name_;
};

}  // namespace ktrace

#define __TRACE_SCOPE_LABEL __TRACE_SCOPE_LABEL_(__COUNTER__)
#define __TRACE_SCOPE_LABEL_(counter) __ktrace_scope_##counter

#define TRACE_EVENT0(cat, name) \
  ::ktrace::ScopedTraceEvent TRACE_SCOPE_LABEL(cat, name)
#define TRACE_EVENT1(cat, name, k0, v0)                    \
  ::ktrace::ScopedTraceEvent TRACE_SCOPE_LABEL(cat, name); \
  (void)v0
#define TRACE_EVENT2(cat, name, k0, v0, k1, v1)            \
  ::ktrace::ScopedTraceEvent TRACE_SCOPE_LABEL(cat, name); \
  (void)v0, (void)v1

#define TRACE_EVENT_ASYNC_BEGIN0(cat, name, id) \
  ::ktrace::TraceEventAsyncBegin(cat, name, id)
#define TRACE_EVENT_ASYNC_END0(cat, name, id) \
  ::ktrace::TraceEventAsyncEnd(cat, name, id)
#define TRACE_EVENT_ASYNC_BEGIN1(cat, name, id, k0, v0) \
  ::ktrace::TraceEventAsyncBegin(cat, name, id), (void)v0
#define TRACE_EVENT_ASYNC_END1(cat, name, id, k0, v0) \
  ::ktrace::TraceEventAsyncEnd(cat, name, id), (void)v0

#define TRACE_EVENT_INSTANT0(cat, name, scope) \
  ::ktrace::TraceEventInstant(cat, name), (void)scope
#define TRACE_EVENT_INSTANT1(cat, name, scope, k0, v0) \
  ::ktrace::TraceEventInstant(cat, name), (void)scope, (void)v0
#define TRACE_EVENT_INSTANT2(cat, name, scope, k0, v0, k1, v1) \
  ::ktrace::TraceEventInstant(cat, name), (void)scope, (void)v0, (void)v1
#define TRACE_EVENT_SCOPE_GLOBAL 0
#define TRACE_EVENT_SCOPE_PROCESS 1
#define TRACE_EVENT_SCOPE_THREAD 2

// Other things which were previously offered by //base/trace_event which
// we may want to have again.
#define TRACE_EVENT_FLOW_BEGIN0(a, b, c) (void)c
#define TRACE_EVENT_FLOW_END0(a, b, c) (void)c
#define TRACE_EVENT_FLOW_BEGIN1(a, b, c, d, e) (void)c, (void)e
#define TRACE_EVENT_FLOW_END1(a, b, c, d, e) (void)c, (void)e

#endif  // APPS_MOZART_GLUE_KTRACE_TRACE_EVENT_H_

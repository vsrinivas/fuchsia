// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace/handler.h>

namespace trace {

const trace_handler_ops_t TraceHandler::kOps =
    {.is_category_enabled = &TraceHandler::CallIsCategoryEnabled,
     .trace_started = &TraceHandler::CallTraceStarted,
     .trace_stopped = &TraceHandler::CallTraceStopped,
     .notify_buffer_full = &TraceHandler::CallNotifyBufferFull};

TraceHandler::TraceHandler()
    : trace_handler{.ops = &kOps} {}

TraceHandler::~TraceHandler() = default;

bool TraceHandler::CallIsCategoryEnabled(trace_handler_t* handler, const char* category) {
    return static_cast<TraceHandler*>(handler)->IsCategoryEnabled(category);
}

void TraceHandler::CallTraceStarted(trace_handler_t* handler) {
    static_cast<TraceHandler*>(handler)->TraceStarted();
}

void TraceHandler::CallTraceStopped(trace_handler_t* handler, async_dispatcher_t* dispatcher,
                                    zx_status_t disposition, size_t buffer_bytes_written) {
    static_cast<TraceHandler*>(handler)->TraceStopped(dispatcher,
                                                      disposition, buffer_bytes_written);
}

void TraceHandler::CallNotifyBufferFull(trace_handler_t* handler) {
    static_cast<TraceHandler*>(handler)->NotifyBufferFull();
}

} // namespace trace

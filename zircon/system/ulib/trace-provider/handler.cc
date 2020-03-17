// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trace-provider/handler.h>

namespace trace {

const trace_handler_ops_t TraceHandler::kOps = {
    .is_category_enabled = &TraceHandler::CallIsCategoryEnabled,
    .trace_started = &TraceHandler::CallTraceStarted,
    .trace_stopped = &TraceHandler::CallTraceStopped,
    .trace_terminated = &TraceHandler::CallTraceTerminated,
    .notify_buffer_full = &TraceHandler::CallNotifyBufferFull,
    .send_trigger = &TraceHandler::CallSendTrigger};

TraceHandler::TraceHandler() : trace_handler{.ops = &kOps} {}

TraceHandler::~TraceHandler() = default;

bool TraceHandler::CallIsCategoryEnabled(trace_handler_t* handler, const char* category) {
  return static_cast<TraceHandler*>(handler)->IsCategoryEnabled(category);
}

void TraceHandler::CallTraceStarted(trace_handler_t* handler) {
  static_cast<TraceHandler*>(handler)->TraceStarted();
}

void TraceHandler::CallTraceStopped(trace_handler_t* handler, zx_status_t disposition) {
  static_cast<TraceHandler*>(handler)->TraceStopped(disposition);
}

void TraceHandler::CallTraceTerminated(trace_handler_t* handler) {
  static_cast<TraceHandler*>(handler)->TraceTerminated();
}

void TraceHandler::CallNotifyBufferFull(trace_handler_t* handler, uint32_t wrapped_count,
                                        uint64_t durable_data_end) {
  static_cast<TraceHandler*>(handler)->NotifyBufferFull(wrapped_count, durable_data_end);
}

void TraceHandler::CallSendTrigger(trace_handler_t* handler, const char* trigger_name) {
  static_cast<TraceHandler*>(handler)->SendTrigger(trigger_name);
}

}  // namespace trace

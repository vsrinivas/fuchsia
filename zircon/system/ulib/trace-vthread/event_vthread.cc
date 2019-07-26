// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace-vthread/event_vthread.h"

#include <trace/event.h>
#include <zircon/syscalls.h>

namespace {

struct VThreadEventHelper {
  VThreadEventHelper(trace_context_t* context, const char* name_literal,
                     const char* vthread_literal, trace_vthread_id_t vthread_id,
                     trace_ticks_t timestamp)
      : ticks(timestamp) {
    trace_context_register_vthread(context, ZX_KOID_INVALID, vthread_literal, vthread_id,
                                   &thread_ref);
    trace_context_register_string_literal(context, name_literal, &name_ref);
  }

  trace_ticks_t const ticks;
  trace_thread_ref_t thread_ref;
  trace_string_ref_t name_ref;
};

}  // namespace

static void trace_internal_vthread_complete_args(trace_context_t* context, trace_arg_t* args,
                                                 size_t num_args) {
  TRACE_COMPLETE_ARGS(context, args, num_args);
}

void trace_internal_write_vthread_duration_begin_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_ticks_t timestamp,
    trace_arg_t* args, size_t num_args) {
  VThreadEventHelper helper(context, name_literal, vthread_literal, vthread_id, timestamp);
  trace_internal_vthread_complete_args(context, args, num_args);
  trace_context_write_duration_begin_event_record(context, helper.ticks, &helper.thread_ref,
                                                  category_ref, &helper.name_ref, args, num_args);
  trace_release_context(context);
}

void trace_internal_write_vthread_duration_end_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_ticks_t timestamp,
    trace_arg_t* args, size_t num_args) {
  VThreadEventHelper helper(context, name_literal, vthread_literal, vthread_id, timestamp);
  trace_internal_vthread_complete_args(context, args, num_args);
  trace_context_write_duration_end_event_record(context, helper.ticks, &helper.thread_ref,
                                                category_ref, &helper.name_ref, args, num_args);
  trace_release_context(context);
}

void trace_internal_write_vthread_flow_begin_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_flow_id_t flow_id,
    trace_ticks_t timestamp, trace_arg_t* args, size_t num_args) {
  VThreadEventHelper helper(context, name_literal, vthread_literal, vthread_id, timestamp);
  trace_internal_vthread_complete_args(context, args, num_args);
  trace_context_write_flow_begin_event_record(context, helper.ticks, &helper.thread_ref,
                                              category_ref, &helper.name_ref, flow_id, args,
                                              num_args);
  trace_release_context(context);
}

void trace_internal_write_vthread_flow_step_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_flow_id_t flow_id,
    trace_ticks_t timestamp, trace_arg_t* args, size_t num_args) {
  VThreadEventHelper helper(context, name_literal, vthread_literal, vthread_id, timestamp);
  trace_internal_vthread_complete_args(context, args, num_args);
  trace_context_write_flow_step_event_record(context, helper.ticks, &helper.thread_ref,
                                             category_ref, &helper.name_ref, flow_id, args,
                                             num_args);
  trace_release_context(context);
}

void trace_internal_write_vthread_flow_end_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_flow_id_t flow_id,
    trace_ticks_t timestamp, trace_arg_t* args, size_t num_args) {
  VThreadEventHelper helper(context, name_literal, vthread_literal, vthread_id, timestamp);
  trace_internal_vthread_complete_args(context, args, num_args);
  trace_context_write_flow_end_event_record(context, helper.ticks, &helper.thread_ref, category_ref,
                                            &helper.name_ref, flow_id, args, num_args);
  trace_release_context(context);
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace/event.h>

#include <magenta/assert.h>
#include <magenta/syscalls.h>

namespace {

struct EventHelper {
    EventHelper(trace_context_t* context, const char* name_literal)
        : ticks(mx_ticks_get()) {
        trace_context_register_current_thread(context, &thread_ref);
        trace_context_register_string_literal(context, name_literal, &name_ref);
    }

    trace_ticks_t const ticks;
    trace_thread_ref_t thread_ref;
    trace_string_ref_t name_ref;
};

} // namespace

void trace_internal_write_instant_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_scope_t scope,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_instant_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        scope, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_counter_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_counter_id_t counter_id,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_counter_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        counter_id, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_duration_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_duration_begin_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        args, num_args);
    trace_release_context(context);
}

void trace_internal_write_duration_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_duration_end_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        args, num_args);
    trace_release_context(context);
}

void trace_internal_write_async_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_async_begin_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        async_id, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_async_instant_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_async_instant_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        async_id, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_async_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_async_end_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        async_id, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_flow_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_flow_begin_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        flow_id, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_flow_step_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_flow_step_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        flow_id, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_flow_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args) {
    EventHelper helper(context, name_literal);
    trace_context_write_flow_end_event_record(
        context, helper.ticks, &helper.thread_ref, category_ref, &helper.name_ref,
        flow_id, args, num_args);
    trace_release_context(context);
}

void trace_internal_write_kernel_object_record_for_handle_and_release_context(
    trace_context_t* context,
    mx_handle_t handle,
    const trace_arg_t* args, size_t num_args) {
    trace_context_write_kernel_object_record_for_handle(
        context, handle, args, num_args);
    trace_release_context(context);
}

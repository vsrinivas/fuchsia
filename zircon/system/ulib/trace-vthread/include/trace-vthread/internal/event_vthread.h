// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRACE_VTHREAD_INTERNAL_EVENT_VTHREAD_H_
#define TRACE_VTHREAD_INTERNAL_EVENT_VTHREAD_H_

#include <trace-engine/instrumentation.h>
#include <trace/event_args.h>

#ifndef NTRACE
#define TRACE_VTHREAD_INTERNAL_EVENT_RECORD(category_literal, stmt, args...)                   \
  do {                                                                                         \
    trace_string_ref_t __trace_vthread_category_ref;                                           \
    trace_context_t* __trace_vthread_context =                                                 \
        trace_acquire_context_for_category((category_literal), &__trace_vthread_category_ref); \
    if (unlikely(__trace_vthread_context)) {                                                   \
      TRACE_DECLARE_ARGS(__trace_vthread_context, __trace_vthread_args, args);                 \
      stmt;                                                                                    \
    }                                                                                          \
  } while (0)
#else
#define TRACE_VTHREAD_INTERNAL_EVENT_RECORD(category_literal, stmt, args...)   \
  do {                                                                         \
    if (0) {                                                                   \
      trace_string_ref_t __trace_vthread_category_ref;                         \
      trace_context_t* __trace_vthread_context = 0;                            \
      TRACE_DECLARE_ARGS(__trace_vthread_context, __trace_vthread_args, args); \
      stmt;                                                                    \
    }                                                                          \
  } while (0)
#endif  // NTRACE

#define TRACE_VTHREAD_INTERNAL_DURATION_BEGIN(category_literal, name_literal, vthread_literal, \
                                              vthread_id, timestamp, args...)                  \
  do {                                                                                         \
    TRACE_VTHREAD_INTERNAL_EVENT_RECORD(                                                       \
        (category_literal),                                                                    \
        trace_internal_write_vthread_duration_begin_event_record_and_release_context(          \
            __trace_vthread_context, &__trace_vthread_category_ref, (name_literal),            \
            (vthread_literal), (vthread_id), (timestamp), __trace_vthread_args,                \
            TRACE_NUM_ARGS(__trace_vthread_args)),                                             \
        args);                                                                                 \
  } while (0)

#define TRACE_VTHREAD_INTERNAL_DURATION_END(category_literal, name_literal, vthread_literal, \
                                            vthread_id, timestamp, args...)                  \
  do {                                                                                       \
    TRACE_VTHREAD_INTERNAL_EVENT_RECORD(                                                     \
        (category_literal),                                                                  \
        trace_internal_write_vthread_duration_end_event_record_and_release_context(          \
            __trace_vthread_context, &__trace_vthread_category_ref, (name_literal),          \
            (vthread_literal), (vthread_id), (timestamp), __trace_vthread_args,              \
            TRACE_NUM_ARGS(__trace_vthread_args)),                                           \
        args);                                                                               \
  } while (0)

#define TRACE_VTHREAD_INTERNAL_FLOW_BEGIN(category_literal, name_literal, vthread_literal, \
                                          vthread_id, flow_id, timestamp, args...)         \
  do {                                                                                     \
    TRACE_VTHREAD_INTERNAL_EVENT_RECORD(                                                   \
        (category_literal),                                                                \
        trace_internal_write_vthread_flow_begin_event_record_and_release_context(          \
            __trace_vthread_context, &__trace_vthread_category_ref, (name_literal),        \
            (vthread_literal), (vthread_id), (flow_id), (timestamp), __trace_vthread_args, \
            TRACE_NUM_ARGS(__trace_vthread_args)),                                         \
        args);                                                                             \
  } while (0)

#define TRACE_VTHREAD_INTERNAL_FLOW_STEP(category_literal, name_literal, vthread_literal,  \
                                         vthread_id, flow_id, timestamp, args...)          \
  do {                                                                                     \
    TRACE_VTHREAD_INTERNAL_EVENT_RECORD(                                                   \
        (category_literal),                                                                \
        trace_internal_write_vthread_flow_step_event_record_and_release_context(           \
            __trace_vthread_context, &__trace_vthread_category_ref, (name_literal),        \
            (vthread_literal), (vthread_id), (flow_id), (timestamp), __trace_vthread_args, \
            TRACE_NUM_ARGS(__trace_vthread_args)),                                         \
        args);                                                                             \
  } while (0)

#define TRACE_VTHREAD_INTERNAL_FLOW_END(category_literal, name_literal, vthread_literal,   \
                                        vthread_id, flow_id, timestamp, args...)           \
  do {                                                                                     \
    TRACE_VTHREAD_INTERNAL_EVENT_RECORD(                                                   \
        (category_literal),                                                                \
        trace_internal_write_vthread_flow_end_event_record_and_release_context(            \
            __trace_vthread_context, &__trace_vthread_category_ref, (name_literal),        \
            (vthread_literal), (vthread_id), (flow_id), (timestamp), __trace_vthread_args, \
            TRACE_NUM_ARGS(__trace_vthread_args)),                                         \
        args);                                                                             \
  } while (0)

void trace_internal_write_vthread_duration_begin_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_ticks_t timestamp,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_vthread_duration_end_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_ticks_t timestamp,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_vthread_flow_begin_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_flow_id_t flow_id,
    trace_ticks_t timestamp, trace_arg_t* args, size_t num_args);

void trace_internal_write_vthread_flow_step_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_flow_id_t flow_id,
    trace_ticks_t timestamp, trace_arg_t* args, size_t num_args);

void trace_internal_write_vthread_flow_end_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const char* vthread_literal, trace_vthread_id_t vthread_id, trace_flow_id_t flow_id,
    trace_ticks_t timestamp, trace_arg_t* args, size_t num_args);

#endif  // TRACE_VTHREAD_INTERNAL_EVENT_VTHREAD_H_

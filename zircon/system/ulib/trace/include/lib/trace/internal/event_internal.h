// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Internal declarations used by the C tracing macros.
// This is not part of the public API: use <trace/event.h> instead.
//

#ifndef LIB_TRACE_INTERNAL_EVENT_INTERNAL_H_
#define LIB_TRACE_INTERNAL_EVENT_INTERNAL_H_

#include <lib/trace-engine/instrumentation.h>
#include <lib/trace/internal/event_args.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

__BEGIN_CDECLS

// Variable used to refer to the current trace context.
#define TRACE_INTERNAL_CONTEXT __trace_context

// Variable used to hold call-site cache state.
#define TRACE_INTERNAL_SITE_STATE __trace_site_state

// Variable used to maintain the category enabled state.
#define TRACE_INTERNAL_CATEGORY_ENABLED_STATE __trace_is_category_group_enabled

// Variable used to refer to the current trace category's string ref.
#define TRACE_INTERNAL_CATEGORY_REF __trace_category_ref

// Variable used to contain the array of arguments.
#define TRACE_INTERNAL_ARGS __trace_args

// Number of arguments recorded in |TRACE_INTERNAL_ARGS|.
#define TRACE_INTERNAL_NUM_INTERNAL_ARGS TRACE_INTERNAL_NUM_ARGS(TRACE_INTERNAL_ARGS)

// Obtains a unique identifier name within the containing scope.
#define TRACE_INTERNAL_SCOPE_LABEL() TRACE_INTERNAL_SCOPE_LABEL_(__COUNTER__)
#define TRACE_INTERNAL_SCOPE_LABEL_(token) TRACE_INTERNAL_SCOPE_LABEL__(token)
#define TRACE_INTERNAL_SCOPE_LABEL__(token) __trace_scope_##token

#define TRACE_INTERNAL_SCOPE_ARGS_LABEL(scope) TRACE_INTERNAL_SCOPE_ARGS_LABEL_(scope)
#define TRACE_INTERNAL_SCOPE_ARGS_LABEL_(scope) scope##_args

// Scaffolding for category enabled check.
#ifndef NTRACE
#define TRACE_INTERNAL_CATEGORY_ENABLED(category_literal)                                \
  ({                                                                                     \
    static trace_site_t TRACE_INTERNAL_SITE_STATE;                                       \
    trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;                                      \
    bool TRACE_INTERNAL_CATEGORY_ENABLED_STATE = false;                                  \
    trace_context_t* TRACE_INTERNAL_CONTEXT = trace_acquire_context_for_category_cached( \
        (category_literal), &TRACE_INTERNAL_SITE_STATE, &TRACE_INTERNAL_CATEGORY_REF);   \
    if (unlikely(TRACE_INTERNAL_CONTEXT)) {                                              \
      TRACE_INTERNAL_CATEGORY_ENABLED_STATE = true;                                      \
      trace_release_context(TRACE_INTERNAL_CONTEXT);                                     \
    }                                                                                    \
    TRACE_INTERNAL_CATEGORY_ENABLED_STATE;                                               \
  })
#else
#define TRACE_INTERNAL_CATEGORY_ENABLED(category_literal) ((void)(category_literal), false)
#endif  // NTRACE

// Scaffolding for a trace macro that does not have a category.
#ifndef NTRACE
#define TRACE_INTERNAL_SIMPLE_RECORD(stmt, args...)                                   \
  do {                                                                                \
    trace_context_t* TRACE_INTERNAL_CONTEXT = trace_acquire_context();                \
    if (unlikely(TRACE_INTERNAL_CONTEXT)) {                                           \
      TRACE_INTERNAL_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT, TRACE_INTERNAL_ARGS, args); \
      stmt;                                                                           \
    }                                                                                 \
  } while (0)
#else
#define TRACE_INTERNAL_SIMPLE_RECORD(stmt, args...)                                   \
  do {                                                                                \
    if (0) {                                                                          \
      trace_context_t* TRACE_INTERNAL_CONTEXT = 0;                                    \
      TRACE_INTERNAL_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT, TRACE_INTERNAL_ARGS, args); \
      stmt;                                                                           \
    }                                                                                 \
  } while (0)
#endif  // NTRACE

// Scaffolding for a trace macro that has a category (such as a trace event).
#ifndef NTRACE
#define TRACE_INTERNAL_EVENT_RECORD(category_literal, stmt, args...)                     \
  do {                                                                                   \
    static trace_site_t TRACE_INTERNAL_SITE_STATE;                                       \
    trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;                                      \
    trace_context_t* TRACE_INTERNAL_CONTEXT = trace_acquire_context_for_category_cached( \
        (category_literal), &TRACE_INTERNAL_SITE_STATE, &TRACE_INTERNAL_CATEGORY_REF);   \
    if (unlikely(TRACE_INTERNAL_CONTEXT)) {                                              \
      TRACE_INTERNAL_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT, TRACE_INTERNAL_ARGS, args);    \
      stmt;                                                                              \
    }                                                                                    \
  } while (0)
#else
#define TRACE_INTERNAL_EVENT_RECORD(category_literal, stmt, args...)                  \
  do {                                                                                \
    if (0) {                                                                          \
      trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;                                 \
      trace_context_t* TRACE_INTERNAL_CONTEXT = 0;                                    \
      TRACE_INTERNAL_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT, TRACE_INTERNAL_ARGS, args); \
      stmt;                                                                           \
    }                                                                                 \
  } while (0)
#endif  // NTRACE

#define TRACE_INTERNAL_INSTANT(category_literal, name_literal, scope, args...)             \
  do {                                                                                     \
    TRACE_INTERNAL_EVENT_RECORD(                                                           \
        (category_literal),                                                                \
        trace_internal_write_instant_event_record_and_release_context(                     \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (scope), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                        \
        args);                                                                             \
  } while (0)

#define TRACE_INTERNAL_COUNTER(category_literal, name_literal, counter_id, args...)             \
  do {                                                                                          \
    TRACE_INTERNAL_EVENT_RECORD(                                                                \
        (category_literal),                                                                     \
        trace_internal_write_counter_event_record_and_release_context(                          \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (counter_id), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                             \
        args);                                                                                  \
  } while (0)

#define TRACE_INTERNAL_DURATION_BEGIN(category_literal, name_literal, args...)    \
  do {                                                                            \
    TRACE_INTERNAL_EVENT_RECORD(                                                  \
        (category_literal),                                                       \
        trace_internal_write_duration_begin_event_record_and_release_context(     \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),               \
        args);                                                                    \
  } while (0)

#define TRACE_INTERNAL_DURATION_END(category_literal, name_literal, args...)      \
  do {                                                                            \
    TRACE_INTERNAL_EVENT_RECORD(                                                  \
        (category_literal),                                                       \
        trace_internal_write_duration_end_event_record_and_release_context(       \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),               \
        args);                                                                    \
  } while (0)

#ifndef NTRACE
// TODO(fmeawad): The generated code for this macro is too big (fxbug.dev/22950)
#define TRACE_INTERNAL_DECLARE_DURATION_SCOPE(variable, args_variable, category_literal,         \
                                              name_literal, args...)                             \
  TRACE_INTERNAL_ALLOCATE_ARGS(args_variable, args);                                             \
  __attribute__((cleanup(trace_internal_cleanup_duration_scope)))                                \
      trace_internal_duration_scope_t variable;                                                  \
  do {                                                                                           \
    static trace_site_t TRACE_INTERNAL_SITE_STATE;                                               \
    trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;                                              \
    trace_context_t* TRACE_INTERNAL_CONTEXT = trace_acquire_context_for_category_cached(         \
        (category_literal), &TRACE_INTERNAL_SITE_STATE, &TRACE_INTERNAL_CATEGORY_REF);           \
    if (unlikely(TRACE_INTERNAL_CONTEXT)) {                                                      \
      TRACE_INTERNAL_INIT_ARGS(args_variable, args);                                             \
      trace_release_context(TRACE_INTERNAL_CONTEXT);                                             \
      trace_internal_make_duration_scope(&variable, (category_literal), (name_literal),          \
                                         args_variable, TRACE_INTERNAL_NUM_ARGS(args_variable)); \
    } else {                                                                                     \
      variable.start_time = 0;                                                                   \
    }                                                                                            \
  } while (0)

#define TRACE_INTERNAL_DURATION_(scope_label, scope_category_literal, scope_name_literal, args...) \
  TRACE_INTERNAL_DECLARE_DURATION_SCOPE(scope_label, TRACE_INTERNAL_SCOPE_ARGS_LABEL(scope_label), \
                                        scope_category_literal, scope_name_literal, args)
#define TRACE_INTERNAL_DURATION(category_literal, name_literal, args...) \
  TRACE_INTERNAL_DURATION_(TRACE_INTERNAL_SCOPE_LABEL(), (category_literal), (name_literal), args)
#else
#define TRACE_INTERNAL_DURATION(category_literal, name_literal, args...) \
  TRACE_INTERNAL_DURATION_BEGIN((category_literal), (name_literal), args)
#endif  // NTRACE

#define TRACE_INTERNAL_ASYNC_BEGIN(category_literal, name_literal, async_id, args...)         \
  do {                                                                                        \
    TRACE_INTERNAL_EVENT_RECORD(                                                              \
        (category_literal),                                                                   \
        trace_internal_write_async_begin_event_record_and_release_context(                    \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (async_id), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                           \
        args);                                                                                \
  } while (0)

#define TRACE_INTERNAL_ASYNC_INSTANT(category_literal, name_literal, async_id, args...)       \
  do {                                                                                        \
    TRACE_INTERNAL_EVENT_RECORD(                                                              \
        (category_literal),                                                                   \
        trace_internal_write_async_instant_event_record_and_release_context(                  \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (async_id), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                           \
        args);                                                                                \
  } while (0)

#define TRACE_INTERNAL_ASYNC_END(category_literal, name_literal, async_id, args...)           \
  do {                                                                                        \
    TRACE_INTERNAL_EVENT_RECORD(                                                              \
        (category_literal),                                                                   \
        trace_internal_write_async_end_event_record_and_release_context(                      \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (async_id), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                           \
        args);                                                                                \
  } while (0)

#define TRACE_INTERNAL_FLOW_BEGIN(category_literal, name_literal, flow_id, args...)          \
  do {                                                                                       \
    TRACE_INTERNAL_EVENT_RECORD(                                                             \
        (category_literal),                                                                  \
        trace_internal_write_flow_begin_event_record_and_release_context(                    \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (flow_id), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                          \
        args);                                                                               \
  } while (0)

#define TRACE_INTERNAL_FLOW_STEP(category_literal, name_literal, flow_id, args...)           \
  do {                                                                                       \
    TRACE_INTERNAL_EVENT_RECORD(                                                             \
        (category_literal),                                                                  \
        trace_internal_write_flow_step_event_record_and_release_context(                     \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (flow_id), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                          \
        args);                                                                               \
  } while (0)

#define TRACE_INTERNAL_FLOW_END(category_literal, name_literal, flow_id, args...)            \
  do {                                                                                       \
    TRACE_INTERNAL_EVENT_RECORD(                                                             \
        (category_literal),                                                                  \
        trace_internal_write_flow_end_event_record_and_release_context(                      \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (flow_id), \
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),                          \
        args);                                                                               \
  } while (0)

#define TRACE_INTERNAL_BLOB_EVENT(category_literal, name_literal, blob, blob_size, args...) \
  do {                                                                                      \
    TRACE_INTERNAL_EVENT_RECORD(                                                            \
        (category_literal),                                                                 \
        trace_internal_write_blob_event_record_and_release_context(                         \
            TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), (blob),   \
            (blob_size), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_INTERNAL_ARGS),            \
        args);                                                                              \
  } while (0)

#define TRACE_INTERNAL_BLOB_ATTACHMENT(category_literal, name_literal, blob, blob_size)           \
  do {                                                                                            \
    static trace_site_t TRACE_INTERNAL_SITE_STATE;                                                \
    trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;                                               \
    trace_context_t* TRACE_INTERNAL_CONTEXT = trace_acquire_context_for_category_cached(          \
        (category_literal), &TRACE_INTERNAL_SITE_STATE, &TRACE_INTERNAL_CATEGORY_REF);            \
    if (unlikely(TRACE_INTERNAL_CONTEXT)) {                                                       \
      trace_internal_write_blob_attachment_record_and_release_context(                            \
          TRACE_INTERNAL_CONTEXT, &TRACE_INTERNAL_CATEGORY_REF, (name_literal), blob, blob_size); \
    }                                                                                             \
  } while (0)

#define TRACE_INTERNAL_KERNEL_OBJECT(handle, args...)                             \
  do {                                                                            \
    TRACE_INTERNAL_SIMPLE_RECORD(                                                 \
        trace_internal_write_kernel_object_record_for_handle_and_release_context( \
            TRACE_INTERNAL_CONTEXT, (handle), TRACE_INTERNAL_ARGS,                \
            TRACE_INTERNAL_NUM_INTERNAL_ARGS),                                    \
        args);                                                                    \
  } while (0)

#define TRACE_INTERNAL_BLOB(type, name, blob, blob_size)                                           \
  do {                                                                                             \
    trace_context_t* TRACE_INTERNAL_CONTEXT = trace_acquire_context();                             \
    if (unlikely(TRACE_INTERNAL_CONTEXT)) {                                                        \
      trace_internal_write_blob_record_and_release_context(TRACE_INTERNAL_CONTEXT, (type), (name), \
                                                           (blob), (blob_size));                   \
    }                                                                                              \
  } while (0)

#ifndef NTRACE
#define TRACE_INTERNAL_ALERT(category_literal, alert_name)                                    \
  do {                                                                                        \
    trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;                                           \
    trace_context_t* TRACE_INTERNAL_CONTEXT =                                                 \
        trace_acquire_context_for_category((category_literal), &TRACE_INTERNAL_CATEGORY_REF); \
    if (unlikely(TRACE_INTERNAL_CONTEXT)) {                                                   \
      trace_internal_send_alert_and_release_context(TRACE_INTERNAL_CONTEXT, (alert_name));    \
    }                                                                                         \
  } while (0)
#else
#define TRACE_INTERNAL_ALERT(category_literal, alert_name) \
  ((void)(alert_name), (void)(category_literal), false)
#endif  // NTRACE

///////////////////////////////////////////////////////////////////////////////

// When "destroyed" (by the cleanup attribute), writes a duration event.
typedef struct trace_internal_duration_scope {
  const char* category_literal;
  const char* name_literal;
  trace_ticks_t start_time;
  trace_arg_t* args;
  size_t num_args;
} trace_internal_duration_scope_t;

void trace_internal_write_instant_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_scope_t scope, trace_arg_t* args, size_t num_args);

void trace_internal_write_counter_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    uint64_t counter_id, trace_arg_t* args, size_t num_args);

void trace_internal_write_duration_begin_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_duration_end_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_duration_event_record(const trace_internal_duration_scope_t* scope);

void trace_internal_write_async_begin_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_async_id_t async_id, trace_arg_t* args, size_t num_args);

void trace_internal_write_async_instant_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_async_id_t async_id, trace_arg_t* args, size_t num_args);

void trace_internal_write_async_end_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_async_id_t async_id, trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_begin_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_flow_id_t flow_id, trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_step_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_flow_id_t flow_id, trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_end_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    trace_flow_id_t flow_id, trace_arg_t* args, size_t num_args);

void trace_internal_write_blob_event_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const void* blob, size_t blob_size, trace_arg_t* args, size_t num_args);

void trace_internal_write_blob_attachment_record_and_release_context(
    trace_context_t* context, const trace_string_ref_t* category_ref, const char* name_literal,
    const void* blob, size_t blob_size);

void trace_internal_write_kernel_object_record_for_handle_and_release_context(
    trace_context_t* context, zx_handle_t handle, trace_arg_t* args, size_t num_args);

void trace_internal_write_blob_record_and_release_context(trace_context_t* context,
                                                          trace_blob_type_t type,
                                                          const char* name_literal,
                                                          const void* blob, size_t blob_size);

void trace_internal_send_alert_and_release_context(trace_context_t* context,
                                                   const char* alert_name);

#ifndef NTRACE

static inline void trace_internal_make_duration_scope(trace_internal_duration_scope_t* scope,
                                                      const char* category_literal,
                                                      const char* name_literal, trace_arg_t* args,
                                                      size_t num_args) {
  scope->category_literal = category_literal;
  scope->name_literal = name_literal;
  scope->start_time = (trace_ticks_t)zx_ticks_get();
  scope->args = args;
  scope->num_args = num_args;
}

static inline void trace_internal_cleanup_duration_scope(trace_internal_duration_scope_t* scope) {
  // Check if the scope has been initialized. It can be un-initialized if
  // tracing started after the scope was created or tracing is off.
  if (likely(scope->start_time == 0))
    return;
  trace_internal_write_duration_event_record(scope);
}
#endif  // NTRACE

__END_CDECLS

#endif  // LIB_TRACE_INTERNAL_EVENT_INTERNAL_H_

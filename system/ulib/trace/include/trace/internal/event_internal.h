// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Internal declarations used by the C tracing macros.
// This is not part of the public API: use <trace/event.h> instead.
//

#ifndef TRACE_INTERNAL_EVENT_INTERNAL_H_
#define TRACE_INTERNAL_EVENT_INTERNAL_H_

#include <zircon/compiler.h>

#include <trace-engine/instrumentation.h>
#include <trace/internal/event_args.h>

__BEGIN_CDECLS

// Variable used to refer to the current trace context.
#define TRACE_INTERNAL_CONTEXT __trace_context

// Variable used to refer to the current trace category's string ref.
#define TRACE_INTERNAL_CATEGORY_REF __trace_category_ref

// Variable used to contain the array of arguments.
#define TRACE_INTERNAL_ARGS __trace_args

// Number of arguments recorded in |TRACE_INTERNAL_ARGS|.
// TODO(PT-67): Rename this, conventions says TRACE_NUM_ARGS should call this.
#define TRACE_INTERNAL_NUM_ARGS (sizeof(TRACE_INTERNAL_ARGS) / sizeof(TRACE_INTERNAL_ARGS[0]))

// BEGIN SECTION OF DEPRECATED MACROS, USED BY EXTERNAL CODE
// TODO(PT-67): These will be replaced with the NEW_ versions when all existing
// code is cleaned up to not use these.

// Makes a string literal string ref.
#define TRACE_INTERNAL_MAKE_LITERAL_STRING_REF(string_literal_value) \
    (trace_context_make_registered_string_literal(                   \
        TRACE_INTERNAL_CONTEXT, (string_literal_value)))

// Makes a trace argument.
#ifdef __cplusplus
#define TRACE_INTERNAL_HOLD_ARG(var_name, idx, name_literal, arg_value) \
    const auto& arg##idx = (arg_value);
#define TRACE_INTERNAL_MAKE_ARG(var_name, idx, name_literal, arg_value)  \
    { .name_ref = {.encoded_value = 0, .inline_string = (name_literal)}, \
      .value = ::trace::internal::MakeArgumentValue(arg##idx) }
#else
#define TRACE_INTERNAL_MAKE_ARG(var_name, idx, name_literal, arg_value)  \
    { .name_ref = {.encoded_value = 0, .inline_string = (name_literal)}, \
      .value = (arg_value) }
#endif // __cplusplus

#ifdef __cplusplus
#define TRACE_INTERNAL_DECLARE_ARGS(args...)                                       \
    TRACE_INTERNAL_APPLY_PAIRWISE(TRACE_INTERNAL_HOLD_ARG, ignore, args)           \
    trace_arg_t TRACE_INTERNAL_ARGS[] = {                                          \
        TRACE_INTERNAL_APPLY_PAIRWISE_CSV(TRACE_INTERNAL_MAKE_ARG, ignore, args)}; \
    static_assert(TRACE_INTERNAL_NUM_ARGS <= TRACE_MAX_ARGS, "too many args")
#else
#define TRACE_INTERNAL_DECLARE_ARGS(args...)                                       \
    trace_arg_t TRACE_INTERNAL_ARGS[] = {                                          \
        TRACE_INTERNAL_APPLY_PAIRWISE_CSV(TRACE_INTERNAL_MAKE_ARG, ignore, args)}; \
    static_assert(TRACE_INTERNAL_NUM_ARGS <= TRACE_MAX_ARGS, "too many args")
#endif // __cplusplus

// END SECTION OF DEPRECATED MACROS, USED BY EXTERNAL CODE

// Obtains a unique identifier name within the containing scope.
#define TRACE_INTERNAL_SCOPE_LABEL() TRACE_INTERNAL_SCOPE_LABEL_(__COUNTER__)
#define TRACE_INTERNAL_SCOPE_LABEL_(token) TRACE_INTERNAL_SCOPE_LABEL__(token)
#define TRACE_INTERNAL_SCOPE_LABEL__(token) __trace_scope_##token

// Scaffolding for a trace macro that does not have a category.
#ifndef NTRACE
#define TRACE_INTERNAL_SIMPLE_RECORD(stmt, args...)                 \
    do {                                                            \
        trace_context_t* TRACE_INTERNAL_CONTEXT =                   \
            trace_acquire_context();                                \
        if (unlikely(TRACE_INTERNAL_CONTEXT)) {                     \
            TRACE_INTERNAL_NEW_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT, \
                                            TRACE_INTERNAL_ARGS,    \
                                            args);                  \
            stmt;                                                   \
        }                                                           \
    } while (0)
#else
#define TRACE_INTERNAL_SIMPLE_RECORD(stmt, args...)                 \
    do {                                                            \
        if (0) {                                                    \
            trace_context_t* TRACE_INTERNAL_CONTEXT = 0;            \
            TRACE_INTERNAL_NEW_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT, \
                                            TRACE_INTERNAL_ARGS,    \
                                            args);                  \
            stmt;                                                   \
        }                                                           \
    } while (0)
#endif // NTRACE

// Scaffolding for a trace macro that has a category (such as a trace event).
#ifndef NTRACE
#define TRACE_INTERNAL_EVENT_RECORD(category_literal, stmt, args...) \
    do {                                                             \
        trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;              \
        trace_context_t* TRACE_INTERNAL_CONTEXT =                    \
            trace_acquire_context_for_category(                      \
                (category_literal),                                  \
                &TRACE_INTERNAL_CATEGORY_REF);                       \
        if (unlikely(TRACE_INTERNAL_CONTEXT)) {                      \
            TRACE_INTERNAL_NEW_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT,  \
                                            TRACE_INTERNAL_ARGS,     \
                                            args);                   \
            stmt;                                                    \
        }                                                            \
    } while (0)
#else
#define TRACE_INTERNAL_EVENT_RECORD(category_literal, stmt, args...) \
    do {                                                             \
        if (0) {                                                     \
            trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;          \
            trace_context_t* TRACE_INTERNAL_CONTEXT = 0;             \
            TRACE_INTERNAL_NEW_DECLARE_ARGS(TRACE_INTERNAL_CONTEXT,  \
                                            TRACE_INTERNAL_ARGS,     \
                                            args);                   \
            stmt;                                                    \
        }                                                            \
    } while (0)
#endif // NTRACE

#define TRACE_INTERNAL_INSTANT(category_literal, name_literal, scope, args...)          \
    do {                                                                                \
        TRACE_INTERNAL_EVENT_RECORD(                                                    \
            (category_literal),                                                         \
            trace_internal_write_instant_event_record_and_release_context(              \
                TRACE_INTERNAL_CONTEXT,                                                 \
                &TRACE_INTERNAL_CATEGORY_REF,                                           \
                (name_literal), (scope), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                      \
    } while (0)

#define TRACE_INTERNAL_COUNTER(category_literal, name_literal, counter_id, args...)          \
    do {                                                                                     \
        TRACE_INTERNAL_EVENT_RECORD(                                                         \
            (category_literal),                                                              \
            trace_internal_write_counter_event_record_and_release_context(                   \
                TRACE_INTERNAL_CONTEXT,                                                      \
                &TRACE_INTERNAL_CATEGORY_REF,                                                \
                (name_literal), (counter_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                           \
    } while (0)

#define TRACE_INTERNAL_DURATION_BEGIN(category_literal, name_literal, args...)    \
    do {                                                                          \
        TRACE_INTERNAL_EVENT_RECORD(                                              \
            (category_literal),                                                   \
            trace_internal_write_duration_begin_event_record_and_release_context( \
                TRACE_INTERNAL_CONTEXT,                                           \
                &TRACE_INTERNAL_CATEGORY_REF,                                     \
                (name_literal), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS),    \
            args);                                                                \
    } while (0)

#define TRACE_INTERNAL_DURATION_END(category_literal, name_literal, args...)    \
    do {                                                                        \
        TRACE_INTERNAL_EVENT_RECORD(                                            \
            (category_literal),                                                 \
            trace_internal_write_duration_end_event_record_and_release_context( \
                TRACE_INTERNAL_CONTEXT,                                         \
                &TRACE_INTERNAL_CATEGORY_REF,                                   \
                (name_literal), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS),  \
            args);                                                              \
    } while (0)

#ifndef NTRACE
#define TRACE_INTERNAL_DECLARE_DURATION_SCOPE(variable, category_literal, name_literal) \
    __attribute__((cleanup(trace_internal_cleanup_duration_scope)))                     \
        trace_internal_duration_scope_t variable;                                       \
    trace_internal_make_duration_scope(&variable, (category_literal), (name_literal))
#define TRACE_INTERNAL_DURATION_(scope_label, scope_category_literal, scope_name_literal, args...)  \
    TRACE_INTERNAL_DECLARE_DURATION_SCOPE(scope_label, scope_category_literal, scope_name_literal); \
    TRACE_INTERNAL_DURATION_BEGIN(scope_label.category_literal, scope_label.name_literal, args)
#define TRACE_INTERNAL_DURATION(category_literal, name_literal, args...) \
    TRACE_INTERNAL_DURATION_(TRACE_INTERNAL_SCOPE_LABEL(), (category_literal), (name_literal), args)
#else
#define TRACE_INTERNAL_DURATION(category_literal, name_literal, args...) \
    TRACE_INTERNAL_DURATION_BEGIN((category_literal), (name_literal), args)
#endif // NTRACE

#define TRACE_INTERNAL_ASYNC_BEGIN(category_literal, name_literal, async_id, args...)      \
    do {                                                                                   \
        TRACE_INTERNAL_EVENT_RECORD(                                                       \
            (category_literal),                                                            \
            trace_internal_write_async_begin_event_record_and_release_context(             \
                TRACE_INTERNAL_CONTEXT,                                                    \
                &TRACE_INTERNAL_CATEGORY_REF,                                              \
                (name_literal), (async_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                         \
    } while (0)

#define TRACE_INTERNAL_ASYNC_INSTANT(category_literal, name_literal, async_id, args...)    \
    do {                                                                                   \
        TRACE_INTERNAL_EVENT_RECORD(                                                       \
            (category_literal),                                                            \
            trace_internal_write_async_instant_event_record_and_release_context(           \
                TRACE_INTERNAL_CONTEXT,                                                    \
                &TRACE_INTERNAL_CATEGORY_REF,                                              \
                (name_literal), (async_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                         \
    } while (0)

#define TRACE_INTERNAL_ASYNC_END(category_literal, name_literal, async_id, args...)        \
    do {                                                                                   \
        TRACE_INTERNAL_EVENT_RECORD(                                                       \
            (category_literal),                                                            \
            trace_internal_write_async_end_event_record_and_release_context(               \
                TRACE_INTERNAL_CONTEXT,                                                    \
                &TRACE_INTERNAL_CATEGORY_REF,                                              \
                (name_literal), (async_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                         \
    } while (0)

#define TRACE_INTERNAL_FLOW_BEGIN(category_literal, name_literal, flow_id, args...)       \
    do {                                                                                  \
        TRACE_INTERNAL_EVENT_RECORD(                                                      \
            (category_literal),                                                           \
            trace_internal_write_flow_begin_event_record_and_release_context(             \
                TRACE_INTERNAL_CONTEXT,                                                   \
                &TRACE_INTERNAL_CATEGORY_REF,                                             \
                (name_literal), (flow_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                        \
    } while (0)

#define TRACE_INTERNAL_FLOW_STEP(category_literal, name_literal, flow_id, args...)        \
    do {                                                                                  \
        TRACE_INTERNAL_EVENT_RECORD(                                                      \
            (category_literal),                                                           \
            trace_internal_write_flow_step_event_record_and_release_context(              \
                TRACE_INTERNAL_CONTEXT,                                                   \
                &TRACE_INTERNAL_CATEGORY_REF,                                             \
                (name_literal), (flow_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                        \
    } while (0)

#define TRACE_INTERNAL_FLOW_END(category_literal, name_literal, flow_id, args...)         \
    do {                                                                                  \
        TRACE_INTERNAL_EVENT_RECORD(                                                      \
            (category_literal),                                                           \
            trace_internal_write_flow_end_event_record_and_release_context(               \
                TRACE_INTERNAL_CONTEXT,                                                   \
                &TRACE_INTERNAL_CATEGORY_REF,                                             \
                (name_literal), (flow_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
            args);                                                                        \
    } while (0)

#define TRACE_INTERNAL_KERNEL_OBJECT(handle, args...)                                 \
    do {                                                                              \
        TRACE_INTERNAL_SIMPLE_RECORD(                                                 \
            trace_internal_write_kernel_object_record_for_handle_and_release_context( \
                TRACE_INTERNAL_CONTEXT,                                               \
                (handle), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS),              \
            args);                                                                    \
    } while (0)

#define TRACE_INTERNAL_BLOB(type, name, blob, blob_size)          \
    do {                                                          \
        trace_context_t* TRACE_INTERNAL_CONTEXT =                 \
            trace_acquire_context();                              \
        if (unlikely(TRACE_INTERNAL_CONTEXT)) {                   \
            trace_internal_write_blob_record_and_release_context( \
                TRACE_INTERNAL_CONTEXT,                           \
                (type), (name), (blob), (blob_size));             \
        }                                                         \
    } while (0)

void trace_internal_write_instant_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_scope_t scope,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_counter_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    uint64_t counter_id,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_duration_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_duration_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_async_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_async_instant_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_async_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_step_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_kernel_object_record_for_handle_and_release_context(
    trace_context_t* context,
    zx_handle_t handle,
    trace_arg_t* args, size_t num_args);

void trace_internal_write_blob_record_and_release_context(
    trace_context_t* context,
    trace_blob_type_t type,
    const char* name_literal,
    const void* blob, size_t blob_size);

#ifndef NTRACE
// When "destroyed" (by the cleanup attribute), writes a duration end event.
typedef struct {
    const char* category_literal;
    const char* name_literal;
} trace_internal_duration_scope_t;

static inline void trace_internal_make_duration_scope(
    trace_internal_duration_scope_t* scope,
    const char* category_literal, const char* name_literal) {
    scope->category_literal = category_literal;
    scope->name_literal = name_literal;
}

static inline void trace_internal_cleanup_duration_scope(
    trace_internal_duration_scope_t* scope) {
    TRACE_INTERNAL_DURATION_END(scope->category_literal, scope->name_literal);
}
#endif // NTRACE

__END_CDECLS

#endif // TRACE_INTERNAL_EVENT_INTERNAL_H_

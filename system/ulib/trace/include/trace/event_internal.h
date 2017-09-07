// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Internal declarations used by the C tracing macros.
// This is not part of the public API: use <trace/event.h> instead.
//

#pragma once

#include <assert.h>

#include <magenta/compiler.h>

#include <trace-engine/instrumentation.h>
#include <trace/pairs_internal.h>

__BEGIN_CDECLS

// Variable used to refer to the current trace context.
#define TRACE_INTERNAL_CONTEXT __trace_context

// Variable used to refer to the current trace category's string ref.
#define TRACE_INTERNAL_CATEGORY_REF __trace_category_ref

// Makes a string literal string ref.
#define TRACE_INTERNAL_MAKE_LITERAL_STRING_REF(string_literal_value) \
    (trace_context_make_registered_string_literal(                   \
        TRACE_INTERNAL_CONTEXT, (string_literal_value)))

// Makes a trace argument.
#ifdef __cplusplus
#define TRACE_INTERNAL_MAKE_ARG(name_literal, value)                      \
    (trace_make_arg(TRACE_INTERNAL_MAKE_LITERAL_STRING_REF(name_literal), \
                    ::trace::internal::MakeArgumentValue(value)))
#else
#define TRACE_INTERNAL_MAKE_ARG(name_literal, value)                      \
    (trace_make_arg(TRACE_INTERNAL_MAKE_LITERAL_STRING_REF(name_literal), \
                    (value)))
#endif // __cplusplus

// Declares an array of arguments and initializes it.
#define TRACE_INTERNAL_ARGS __trace_args
#define TRACE_INTERNAL_NUM_ARGS (sizeof(TRACE_INTERNAL_ARGS) / sizeof(TRACE_INTERNAL_ARGS[0]))
#define TRACE_INTERNAL_DECLARE_ARGS(args...)                           \
    const trace_arg_t TRACE_INTERNAL_ARGS[] = {                        \
        TRACE_INTERNAL_APPLY_PAIRWISE(TRACE_INTERNAL_MAKE_ARG, args)}; \
    static_assert(TRACE_INTERNAL_NUM_ARGS <= TRACE_MAX_ARGS, "too many args")

// Obtains a unique identifier name within the containing scope.
#define TRACE_INTERNAL_SCOPE_LABEL() TRACE_INTERNAL_SCOPE_LABEL_(__COUNTER__)
#define TRACE_INTERNAL_SCOPE_LABEL_(token) TRACE_INTERNAL_SCOPE_LABEL__(token)
#define TRACE_INTERNAL_SCOPE_LABEL__(token) __trace_scope_##token

// Scaffolding for a trace macro that does not have a category.
#ifndef NTRACE
#define TRACE_INTERNAL_SIMPLE_RECORD(stmt, args...) \
    do {                                            \
        trace_context_t* TRACE_INTERNAL_CONTEXT =   \
            trace_acquire_context();                \
        if (unlikely(TRACE_INTERNAL_CONTEXT)) {     \
            TRACE_INTERNAL_DECLARE_ARGS(args);      \
            stmt;                                   \
        }                                           \
    } while (0)
#else
#define TRACE_INTERNAL_SIMPLE_RECORD(stmt, args...)      \
    do {                                                 \
        if (0) {                                         \
            trace_context_t* TRACE_INTERNAL_CONTEXT = 0; \
            TRACE_INTERNAL_DECLARE_ARGS(args);           \
            stmt;                                        \
        }                                                \
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
            TRACE_INTERNAL_DECLARE_ARGS(args);                       \
            stmt;                                                    \
        }                                                            \
    } while (0)
#else
#define TRACE_INTERNAL_EVENT_RECORD(category_literal, stmt, args...) \
    do {                                                             \
        if (0) {                                                     \
            trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;          \
            trace_context_t* TRACE_INTERNAL_CONTEXT = 0;             \
            TRACE_INTERNAL_DECLARE_ARGS(args);                       \
            stmt;                                                    \
        }                                                            \
    } while (0)
#endif // NTRACE

#define TRACE_INTERNAL_INSTANT(category_literal, name_literal, scope, args...)      \
    TRACE_INTERNAL_EVENT_RECORD(                                                    \
        (category_literal),                                                         \
        trace_internal_write_instant_event_record_and_release_context(              \
            TRACE_INTERNAL_CONTEXT,                                                 \
            &TRACE_INTERNAL_CATEGORY_REF,                                           \
            (name_literal), (scope), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
        args)

#define TRACE_INTERNAL_COUNTER(category_literal, name_literal, counter_id, args...)      \
    TRACE_INTERNAL_EVENT_RECORD(                                                         \
        (category_literal),                                                              \
        trace_internal_write_counter_event_record_and_release_context(                   \
            TRACE_INTERNAL_CONTEXT,                                                      \
            &TRACE_INTERNAL_CATEGORY_REF,                                                \
            (name_literal), (counter_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
        args)

#define TRACE_INTERNAL_DURATION_BEGIN(category_literal, name_literal, args...) \
    TRACE_INTERNAL_EVENT_RECORD(                                               \
        (category_literal),                                                    \
        trace_internal_write_duration_begin_event_record_and_release_context(  \
            TRACE_INTERNAL_CONTEXT,                                            \
            &TRACE_INTERNAL_CATEGORY_REF,                                      \
            (name_literal), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS),     \
        args)

#define TRACE_INTERNAL_DURATION_END(category_literal, name_literal, args...) \
    TRACE_INTERNAL_EVENT_RECORD(                                             \
        (category_literal),                                                  \
        trace_internal_write_duration_end_event_record_and_release_context(  \
            TRACE_INTERNAL_CONTEXT,                                          \
            &TRACE_INTERNAL_CATEGORY_REF,                                    \
            (name_literal), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS),   \
        args)

#ifndef NTRACE
#define TRACE_INTERNAL_DECLARE_DURATION_SCOPE(variable, category_literal, name_literal) \
    __attribute__((cleanup(trace_internal_cleanup_duration_scope)))                     \
        trace_internal_duration_scope_t variable;                                       \
    trace_internal_make_duration_scope(&variable, (category_literal), (name_literal))
#define TRACE_INTERNAL_DURATION_(scope_label, scope_category_literal, scope_name_literal, args...)  \
    TRACE_INTERNAL_DECLARE_DURATION_SCOPE(scope_label, scope_category_literal, scope_name_literal); \
    TRACE_INTERNAL_DURATION_BEGIN(scope_label.category_literal, scope_label.name_literal, args)
#define TRACE_INTERNAL_DURATION_(scope_label, scope_category_literal, scope_name_literal, args...)  \
    TRACE_INTERNAL_DECLARE_DURATION_SCOPE(scope_label, scope_category_literal, scope_name_literal); \
    TRACE_INTERNAL_DURATION_BEGIN(scope_label.category_literal, scope_label.name_literal, args)
#define TRACE_INTERNAL_DURATION(category_literal, name_literal, args...) \
    TRACE_INTERNAL_DURATION_(TRACE_INTERNAL_SCOPE_LABEL(), (category_literal), (name_literal), args)
#else
#define TRACE_INTERNAL_DURATION(category_literal, name_literal, args...) \
    TRACE_INTERNAL_DURATION_BEGIN((category_literal), (name_literal), args)
#endif // NTRACE

#define TRACE_INTERNAL_ASYNC_BEGIN(category_literal, name_literal, async_id, args...)  \
    TRACE_INTERNAL_EVENT_RECORD(                                                       \
        (category_literal),                                                            \
        trace_internal_write_async_begin_event_record_and_release_context(             \
            TRACE_INTERNAL_CONTEXT,                                                    \
            &TRACE_INTERNAL_CATEGORY_REF,                                              \
            (name_literal), (async_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
        args)

#define TRACE_INTERNAL_ASYNC_INSTANT(category_literal, name_literal, async_id, args...) \
    TRACE_INTERNAL_EVENT_RECORD(                                                        \
        (category_literal),                                                             \
        trace_internal_write_async_instant_event_record_and_release_context(            \
            TRACE_INTERNAL_CONTEXT,                                                     \
            &TRACE_INTERNAL_CATEGORY_REF,                                               \
            (name_literal), (async_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS),  \
        args)

#define TRACE_INTERNAL_ASYNC_END(category_literal, name_literal, async_id, args...)    \
    TRACE_INTERNAL_EVENT_RECORD(                                                       \
        (category_literal),                                                            \
        trace_internal_write_async_end_event_record_and_release_context(               \
            TRACE_INTERNAL_CONTEXT,                                                    \
            &TRACE_INTERNAL_CATEGORY_REF,                                              \
            (name_literal), (async_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
        args)

#define TRACE_INTERNAL_FLOW_BEGIN(category_literal, name_literal, flow_id, args...)   \
    TRACE_INTERNAL_EVENT_RECORD(                                                      \
        (category_literal),                                                           \
        trace_internal_write_flow_begin_event_record_and_release_context(             \
            TRACE_INTERNAL_CONTEXT,                                                   \
            &TRACE_INTERNAL_CATEGORY_REF,                                             \
            (name_literal), (flow_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
        args)

#define TRACE_INTERNAL_FLOW_STEP(category_literal, name_literal, flow_id, args...)    \
    TRACE_INTERNAL_EVENT_RECORD(                                                      \
        (category_literal),                                                           \
        trace_internal_write_flow_step_event_record_and_release_context(              \
            TRACE_INTERNAL_CONTEXT,                                                   \
            &TRACE_INTERNAL_CATEGORY_REF,                                             \
            (name_literal), (flow_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
        args)

#define TRACE_INTERNAL_FLOW_END(category_literal, name_literal, flow_id, args...)     \
    TRACE_INTERNAL_EVENT_RECORD(                                                      \
        (category_literal),                                                           \
        trace_internal_write_flow_end_event_record_and_release_context(               \
            TRACE_INTERNAL_CONTEXT,                                                   \
            &TRACE_INTERNAL_CATEGORY_REF,                                             \
            (name_literal), (flow_id), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS), \
        args)

#define TRACE_INTERNAL_KERNEL_OBJECT(handle, args...)                             \
    TRACE_INTERNAL_SIMPLE_RECORD(                                                 \
        trace_internal_write_kernel_object_record_for_handle_and_release_context( \
            TRACE_INTERNAL_CONTEXT,                                               \
            (handle), TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS),              \
        args)

void trace_internal_write_instant_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_scope_t scope,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_counter_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    uint64_t counter_id,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_duration_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_duration_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_async_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_async_instant_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_async_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_begin_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_step_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_flow_end_event_record_and_release_context(
    trace_context_t* context,
    const trace_string_ref_t* category_ref,
    const char* name_literal,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args);

void trace_internal_write_kernel_object_record_for_handle_and_release_context(
    trace_context_t* context,
    mx_handle_t handle,
    const trace_arg_t* args, size_t num_args);

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

#ifdef __cplusplus

#include <fbl/type_support.h>

namespace trace {
namespace internal {

// Helps construct trace argument values using SFINAE to coerce types.
template <typename T, typename Enable = void>
struct ArgumentValueMaker;

template <>
struct ArgumentValueMaker<trace_arg_value_t> {
    static trace_arg_value_t Make(trace_arg_value_t value) {
        return value;
    }
};

template <>
struct ArgumentValueMaker<decltype(nullptr)> {
    static trace_arg_value_t Make(decltype(nullptr) value) {
        return trace_make_null_arg_value();
    }
};

template <typename T>
struct ArgumentValueMaker<
    T,
    typename fbl::enable_if<fbl::is_signed_integer<T>::value &&
                             (sizeof(T) <= sizeof(int32_t))>::type> {
    static trace_arg_value_t Make(int32_t value) {
        return trace_make_int32_arg_value(value);
    }
};

template <typename T>
struct ArgumentValueMaker<
    T,
    typename fbl::enable_if<fbl::is_unsigned_integer<T>::value &&
                             (sizeof(T) <= sizeof(uint32_t))>::type> {
    static trace_arg_value_t Make(uint32_t value) {
        return trace_make_uint32_arg_value(value);
    }
};

template <typename T>
struct ArgumentValueMaker<
    T,
    typename fbl::enable_if<fbl::is_signed_integer<T>::value &&
                             (sizeof(T) > sizeof(int32_t)) &&
                             (sizeof(T) <= sizeof(int64_t))>::type> {
    static trace_arg_value_t Make(int64_t value) {
        return trace_make_int64_arg_value(value);
    }
};

template <typename T>
struct ArgumentValueMaker<
    T,
    typename fbl::enable_if<fbl::is_unsigned_integer<T>::value &&
                             (sizeof(T) > sizeof(uint32_t)) &&
                             (sizeof(T) <= sizeof(uint64_t))>::type> {
    static trace_arg_value_t Make(uint64_t value) {
        return trace_make_uint64_arg_value(value);
    }
};

template <typename T>
struct ArgumentValueMaker<T, typename fbl::enable_if<fbl::is_enum<T>::value>::type> {
    using UnderlyingType = typename fbl::underlying_type<T>::type;
    static trace_arg_value_t Make(UnderlyingType value) {
        return ArgumentValueMaker<UnderlyingType>::Make(value);
    }
};

template <typename T>
struct ArgumentValueMaker<
    T,
    typename fbl::enable_if<fbl::is_floating_point<T>::value>::type> {
    static trace_arg_value_t Make(double value) {
        return trace_make_double_arg_value(value);
    }
};

template <size_t n>
struct ArgumentValueMaker<char[n]> {
    static trace_arg_value_t Make(const char* value) {
        return trace_make_string_arg_value(
            trace_make_inline_string_ref(value, n));
    }
};

template <>
struct ArgumentValueMaker<const char*> {
    static trace_arg_value_t Make(const char* value) {
        return trace_make_string_arg_value(
            trace_make_inline_c_string_ref(value));
    }
};

// Works for the following types:
// - fbl::String
// - fbl::StringPiece
// - std::string
// - std::stringview
DECLARE_HAS_MEMBER_FN(has_data, data);
DECLARE_HAS_MEMBER_FN(has_length, length);
template <typename T>
struct ArgumentValueMaker<T,
                          typename fbl::enable_if<has_data<T>::value &&
                                                   has_length<T>::value>::type> {
    static trace_arg_value_t Make(const T& value) {
        return trace_make_string_arg_value(
            trace_make_inline_string_ref(value.data(), value.length()));
    }
};

template <typename T>
struct ArgumentValueMaker<T*> {
    static trace_arg_value_t Make(const T* pointer) {
        return trace_make_pointer_arg_value(reinterpret_cast<uintptr_t>(pointer));
    }
};

template <typename T>
trace_arg_value_t MakeArgumentValue(const T& value) {
    return ArgumentValueMaker<T>::Make(value);
}

} // namespace internal
} // namespace trace

#endif // __cplusplus

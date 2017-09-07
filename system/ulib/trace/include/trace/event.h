// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// The API for instrumenting C and C++ programs with trace events.
//
// This header defines macros which are used to record trace information during
// program execution when tracing is enabled.  Each trace event macro records
// an event of a given type together with the current time, a category, name,
// and named arguments containing additional information about the event.
//
// Where indicated, the category and name literal strings must point to
// null-terminated static string constants whose memory address can be
// cached by the string table for the lifetime of the trace session.
//
// Defining the NTRACE macro completely disables recording of trace events
// in the compilation unit.
//
// For more control over how trace events are written, see <trace-engine/context.h>.
//

#pragma once

#include <trace/event_internal.h>

// Argument type macros used when writing trace events in C.
//
// When writing trace events in C, each trace argument value must be
// individually wrapped with one of these macros to provide type information
// for the argument.
//
// When writing trace events in C++, it is not necessary to wrap each trace
// argument value with these macros since the compiler can infer the necessary
// type information, with the exception of |TA_CHAR_ARRAY|, |TA_STRING_LITERAL|,
// and |TA_KOID|.
//
// Use |TA_NULL| for null values.
// Use |TA_INT32| for signed 32-bit integer values.
// Use |TA_UINT32| for unsigned 32-bit integer values.
// Use |TA_INT64| for signed 64-bit integer values.
// Use |TA_UINT64| for unsigned 64-bit integer values.
// Use |TA_DOUBLE| for double-precision floating point values.
// Use |TA_CHAR_ARRAY| for character arrays with a length (copied rather than cached), required in C++.
// Use |TA_STRING| for null-terminated dynamic strings (copied rather than cached).
// Use |TA_STRING_LITERAL| for null-terminated static string constants (cached), required in C++.
// Use |TA_POINTER| for pointer values (records the memory address, not the target).
// Use |TA_KOID| for kernel object ids, required in C++.
//
// C or C++ Usage: (argument type macros required in C)
//
//     char* chars = ...;
//     size_t length = ...;
//     const char* c_string = ...;
//     void* ptr = ...;
//     mx_koid_t koid = ...;
//
//     TRACE_DURATION("category", "name", "arg", TA_NULL());
//     TRACE_DURATION("category", "name", "arg", TA_INT32(-10));
//     TRACE_DURATION("category", "name", "arg", TA_UINT32(10));
//     TRACE_DURATION("category", "name", "arg", TA_INT64(-10));
//     TRACE_DURATION("category", "name", "arg", TA_UINT64(10));
//     TRACE_DURATION("category", "name", "arg", TA_DOUBLE(3.14));
//     TRACE_DURATION("category", "name", "arg", TA_CHAR_ARRAY(chars, length));
//     TRACE_DURATION("category", "name", "arg", TA_STRING(c_string));
//     TRACE_DURATION("category", "name", "arg", TA_STRING_LITERAL("Hi!"));
//     TRACE_DURATION("category", "name", "arg", TA_POINTER(ptr));
//     TRACE_DURATION("category", "name", "arg", TA_KOID(koid));
//
// C++ Usage: (argument type macros only needed for certain types)
//
//     char* chars = ...;
//     size_t length = ...;
//     const char* c_string = ...;
//     fbl::String fbl_string = ...;
//     std::string std_string = ...;
//     void* ptr = ...;
//     mx_koid_t koid = ...;
//
//     TRACE_DURATION("category", "name", "arg", nullptr);
//     TRACE_DURATION("category", "name", "arg", -10);
//     TRACE_DURATION("category", "name", "arg", 10u);
//     TRACE_DURATION("category", "name", "arg", -10l);
//     TRACE_DURATION("category", "name", "arg", 10ul);
//     TRACE_DURATION("category", "name", "arg", 3.14);
//     TRACE_DURATION("category", "name", "arg", TA_CHAR_ARRAY(chars, length));
//     TRACE_DURATION("category", "name", "arg", c_string);
//     TRACE_DURATION("category", "name", "arg", fbl_string);
//     TRACE_DURATION("category", "name", "arg", std_string);
//     TRACE_DURATION("category", "name", "arg", TA_STRING_LITERAL("Hi!"));
//     TRACE_DURATION("category", "name", "arg", ptr);
//     TRACE_DURATION("category", "name", "arg", TA_KOID(koid));
//
#define TA_NULL() (trace_make_null_arg_value())
#define TA_INT32(int32_value) (trace_make_int32_arg_value(int32_value))
#define TA_UINT32(uint32_value) (trace_make_uint32_arg_value(uint32_value))
#define TA_INT64(int64_value) (trace_make_int64_arg_value(int64_value))
#define TA_UINT64(uint64_value) (trace_make_uint64_arg_value(uint64_value))
#define TA_DOUBLE(double_value) (trace_make_double_arg_value(double_value))
#define TA_CHAR_ARRAY(string_value, length) \
    (trace_make_string_arg_value(           \
        trace_make_inline_string_ref((string_value), (length))))
#define TA_STRING(string_value)   \
    (trace_make_string_arg_value( \
        trace_make_inline_c_string_ref(string_value)))
#define TA_STRING_LITERAL(string_literal_value) \
    (trace_make_string_arg_value(               \
        TRACE_INTERNAL_MAKE_LITERAL_STRING_REF(string_literal_value)))
#define TA_POINTER(pointer_value) (trace_make_pointer_arg_value((uintptr_t)pointer_value))
#define TA_KOID(koid_value) (trace_make_koid_arg_value(koid_value))

// Returns true if tracing is enabled.
//
// Usage:
//
//     if (TRACE_ENABLED()) {
//         // do something possibly expensive only when tracing is enabled
//     }
//
#ifndef NTRACE
#define TRACE_ENABLED() (trace_is_enabled())
#else
#define TRACE_ENABLED() (false)
#endif // NTRACE

// Returns true if tracing of the specified category has been enabled (which
// implies that |TRACE_ENABLED()| is also true).
//
// |category_literal| must be a null-terminated static string constant.
//
// Usage:
//
//     if (TRACE_CATEGORY_ENABLED("category")) {
//         // do something possibly expensive only when tracing this category
//     }
//
#ifndef NTRACE
#define TRACE_CATEGORY_ENABLED(category_literal) \
    (trace_is_category_enabled(category_literal))
#else
#define TRACE_CATEGORY_ENABLED(category_literal) ((void)(category_literal), false)
#endif // NTRACE

// Returns a new unique 64-bit unsigned integer (within this process).
// Each invocation returns a different non-zero value.
// Useful for generating identifiers for async and flow events.
//
// Usage:
//
//     trace_async_id_t async_id = TRACE_NONCE();
//     TRACE_ASYNC_BEGIN("category", "name", async_id);
//     // a little while later...
//     TRACE_ASYNC_END("category", "name", async_id);
//
#define TRACE_NONCE() (trace_generate_nonce())

// Writes an instant event representing a single moment in time (a probe).
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the moment with additional information.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |scope| is |TRACE_SCOPE_THREAD|, |TRACE_SCOPE_PROCESS|, or |TRACE_SCOPE_GLOBAL|.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     TRACE_INSTANT("category", "name", TRACE_SCOPE_PROCESS, "x", TA_INT32(42));
//
#define TRACE_INSTANT(category_literal, name_literal, scope, args...) \
    TRACE_INTERNAL_INSTANT((category_literal), (name_literal), (scope), args)

// Writes a counter event with the specified id.
//
// The arguments to this event are numeric samples are typically represented by
// the visualizer as a stacked area chart.  The id serves to distinguish multiple
// instances of counters which share the same category and name within the
// same process.
//
// 1 to 15 numeric arguments can be associated with the event, each of which is
// interpreted as a distinct time series.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |counter_id| is the correlation id of the counter.
//              Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_counter_id_t counter_id = 555;
//     TRACE_COUNTER("category", "name", counter_id, "x", TA_INT32(42), "y", TA_DOUBLE(2.0))
//
#define TRACE_COUNTER(category_literal, name_literal, counter_id, arg1, args...) \
    TRACE_INTERNAL_COUNTER((category_literal), (name_literal), (counter_id), arg1, ##args)

// Writes a duration event which ends when the current scope exits.
//
// Durations describe work which is happening synchronously on one thread.
// They can be nested to represent a control flow stack.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     void function(int arg) {
//         TRACE_DURATION("category", "name", "arg", TA_INT32(arg));
//         // do something useful here
//     }
//
#define TRACE_DURATION(category_literal, name_literal, args...) \
    TRACE_INTERNAL_DURATION((category_literal), (name_literal), args)

// Writes a duration begin event only.
// This event must be matched by a duration end event with the same category and name.
//
// Durations describe work which is happening synchronously on one thread.
// They can be nested to represent a control flow stack.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.  The arguments provided
// to matching duration begin and duration end events are combined together in
// the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     TRACE_DURATION_BEGIN("category", "name", "x", TA_INT32(42));
//
#define TRACE_DURATION_BEGIN(category_literal, name_literal, args...) \
    TRACE_INTERNAL_DURATION_BEGIN((category_literal), (name_literal), args)

// Writes a duration end event only.
//
// Durations describe work which is happening synchronously on one thread.
// They can be nested to represent a control flow stack.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.  The arguments provided
// to matching duration begin and duration end events are combined together in
// the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     TRACE_DURATION_END("category", "name", "x", TA_INT32(42));
//
#define TRACE_DURATION_END(category_literal, name_literal, args...) \
    TRACE_INTERNAL_DURATION_END((category_literal), (name_literal), args)

// Writes an asynchronous begin event with the specified id.
// This event may be followed by async instant events and must be matched by
// an async end event with the same category, name, and id.
//
// Asynchronous events describe work which is happening asynchronously and which
// may span multiple threads.  Asynchronous events do not nest.  The id serves
// to correlate the progress of distinct asynchronous operations which share
// the same category and name within the same process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the asynchronous operation with additional information.  The
// arguments provided to matching async begin, async instant, and async end
// events are combined together in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_async_id_t async_id = 555;
//     TRACE_ASYNC_BEGIN("category", "name", async_id, "x", TA_INT32(42));
//
#define TRACE_ASYNC_BEGIN(category_literal, name_literal, async_id, args...) \
    TRACE_INTERNAL_ASYNC_BEGIN((category_literal), (name_literal), (async_id), args)

// Writes an asynchronous instant event with the specified id.
//
// Asynchronous events describe work which is happening asynchronously and which
// may span multiple threads.  Asynchronous events do not nest.  The id serves
// to correlate the progress of distinct asynchronous operations which share
// the same category and name within the same process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the asynchronous operation with additional information.  The
// arguments provided to matching async begin, async instant, and async end
// events are combined together in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_async_id_t async_id = 555;
//     TRACE_ASYNC_INSTANT("category", "name", async_id, "x", TA_INT32(42));
//
#define TRACE_ASYNC_INSTANT(category_literal, name_literal, async_id, args...) \
    TRACE_INTERNAL_ASYNC_INSTANT((category_literal), (name_literal), (async_id), args)

// Writes an asynchronous end event with the specified id.
//
// Asynchronous events describe work which is happening asynchronously and which
// may span multiple threads.  Asynchronous events do not nest.  The id serves
// to correlate the progress of distinct asynchronous operations which share
// the same category and name within the same process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the asynchronous operation with additional information.  The
// arguments provided to matching async begin, async instant, and async end
// events are combined together in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_async_id_t async_id = 555;
//     TRACE_ASYNC_END("category", "name", async_id, "x", TA_INT32(42));
//
#define TRACE_ASYNC_END(category_literal, name_literal, async_id, args...) \
    TRACE_INTERNAL_ASYNC_END((category_literal), (name_literal), (async_id), args)

// Writes a flow begin event with the specified id.
// This event may be followed by flow steps events and must be matched by
// a flow end event with the same category, name, and id.
//
// Flow events describe control flow handoffs between threads or across processes.
// They are typically represented as arrows in a visualizer.  Flow arrows are
// from the end of the duration event which encloses the beginning of the flow
// to the beginning of the duration event which encloses the next step or the
// end of the flow.  The id serves to correlate flows which share the same
// category and name across processes.
//
// This event must be enclosed in a duration event which represents where
// the flow handoff occurs.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the flow with additional information.  The arguments provided
// to matching flow begin, flow step, and flow end events are combined together
// in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_flow_id_t flow_id = 555;
//     TRACE_FLOW_BEGIN("category", "name", flow_id, "x", TA_INT32(42));
//
#define TRACE_FLOW_BEGIN(category_literal, name_literal, flow_id, args...) \
    TRACE_INTERNAL_FLOW_BEGIN((category_literal), (name_literal), (flow_id), args)

// Writes a flow step event with the specified id.
//
// Flow events describe control flow handoffs between threads or across processes.
// They are typically represented as arrows in a visualizer.  Flow arrows are
// from the end of the duration event which encloses the beginning of the flow
// to the beginning of the duration event which encloses the next step or the
// end of the flow.  The id serves to correlate flows which share the same
// category and name across processes.
//
// This event must be enclosed in a duration event which represents where
// the flow handoff occurs.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the flow with additional information.  The arguments provided
// to matching flow begin, flow step, and flow end events are combined together
// in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_flow_id_t flow_id = 555;
//     TRACE_FLOW_STEP("category", "name", flow_id, "x", TA_INT32(42));
//
#define TRACE_FLOW_STEP(category_literal, name_literal, flow_id, args...) \
    TRACE_INTERNAL_FLOW_STEP((category_literal), (name_literal), (flow_id), args)

// Writes a flow end event with the specified id.
//
// Flow events describe control flow handoffs between threads or across processes.
// They are typically represented as arrows in a visualizer.  Flow arrows are
// from the end of the duration event which encloses the beginning of the flow
// to the beginning of the duration event which encloses the next step or the
// end of the flow.  The id serves to correlate flows which share the same
// category and name across processes.
//
// This event must be enclosed in a duration event which represents where
// the flow handoff occurs.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the flow with additional information.  The arguments provided
// to matching flow begin, flow step, and flow end events are combined together
// in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_flow_id_t id = 555;
//     TRACE_FLOW_END("category", "name", flow_id, "x", TA_INT32(42));
//
#define TRACE_FLOW_END(category_literal, name_literal, flow_id, args...) \
    TRACE_INTERNAL_FLOW_END((category_literal), (name_literal), (flow_id), args)

// Writes a description of a kernel object indicated by |handle|,
// including its koid, name, and the supplied arguments.
//
// 0 to 15 arguments can be associated with the record, each of which is used
// to annotate the handle with additional information.
//
// |handle| is the handle of the object being described.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     mx_handle_t handle = ...;
//     TRACE_KERNEL_OBJECT(handle, "description", TA_STRING("some object"));
//
#define TRACE_KERNEL_OBJECT(handle, args...) \
    TRACE_INTERNAL_KERNEL_OBJECT((handle), args)

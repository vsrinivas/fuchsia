// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains support for emitting additional arguments to trace
// events. Most trace events support adding up to 15 additional name/value
// pairs to provide additional data about the event.

#ifndef LIB_TRACE_EVENT_ARGS_H_
#define LIB_TRACE_EVENT_ARGS_H_

#include <lib/trace/internal/event_args.h>

// Argument type macros used when writing trace events in C.
//
// When writing trace events in C, each trace argument value must be
// individually wrapped with one of these macros to provide type information
// for the argument.
//
// When writing trace events in C++, it is not necessary to wrap each trace
// argument value with these macros since the compiler can infer the necessary
// type information, with the exception of |TA_CHAR_ARRAY| and |TA_KOID|.
//
// Use |TA_NULL| for null values.
// Use |TA_BOOL| for boolean values.
// Use |TA_INT32| for signed 32-bit integer values.
// Use |TA_UINT32| for unsigned 32-bit integer values.
// Use |TA_INT64| for signed 64-bit integer values.
// Use |TA_UINT64| for unsigned 64-bit integer values.
// Use |TA_DOUBLE| for double-precision floating point values.
// Use |TA_CHAR_ARRAY| for character arrays with a length (copied rather than cached), required in
// C++. Use |TA_STRING| for null-terminated dynamic strings (copied rather than cached). Use
// |TA_POINTER| for pointer values (records the memory address, not the target). Use |TA_KOID| for
// kernel object ids, required in C++.
//
// TODO(fxbug.dev/22929): Re-add |TA_STRING_LITERAL|.
//
// C or C++ Usage: (argument type macros required in C)
//
//     char* chars = ...;
//     size_t length = ...;
//     const char* c_string = ...;
//     void* ptr = ...;
//     zx_koid_t koid = ...;
//
//     TRACE_DURATION("category", "name", "arg", TA_NULL());
//     TRACE_DURATION("category", "name", "arg", TA_BOOL(true));
//     TRACE_DURATION("category", "name", "arg", TA_INT32(-10));
//     TRACE_DURATION("category", "name", "arg", TA_UINT32(10));
//     TRACE_DURATION("category", "name", "arg", TA_INT64(-10));
//     TRACE_DURATION("category", "name", "arg", TA_UINT64(10));
//     TRACE_DURATION("category", "name", "arg", TA_DOUBLE(3.14));
//     TRACE_DURATION("category", "name", "arg", TA_CHAR_ARRAY(chars, length));
//     TRACE_DURATION("category", "name", "arg", TA_STRING(c_string));
//     TRACE_DURATION("category", "name", "arg", TA_STRING("Hi!"));
//     TRACE_DURATION("category", "name", "arg", TA_POINTER(ptr));
//     TRACE_DURATION("category", "name", "arg", TA_KOID(koid));
//
// C++ Usage: (argument type macros only needed for certain types)
//
//     char* chars = ...;
//     size_t length = ...;
//     const char* c_string = ...;
//     std::string std_string = ...;
//     void* ptr = ...;
//     zx_koid_t koid = ...;
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
//     TRACE_DURATION("category", "name", "arg", "Hi!");
//     TRACE_DURATION("category", "name", "arg", ptr);
//     TRACE_DURATION("category", "name", "arg", TA_KOID(koid));
//
#define TA_NULL() (trace_make_null_arg_value())
#define TA_BOOL(bool_value) (trace_make_bool_arg_value(bool_value))
#define TA_INT32(int32_value) (trace_make_int32_arg_value(int32_value))
#define TA_UINT32(uint32_value) (trace_make_uint32_arg_value(uint32_value))
#define TA_INT64(int64_value) (trace_make_int64_arg_value(int64_value))
#define TA_UINT64(uint64_value) (trace_make_uint64_arg_value(uint64_value))
#define TA_DOUBLE(double_value) (trace_make_double_arg_value(double_value))
#define TA_CHAR_ARRAY(string_value, length) \
  (trace_make_string_arg_value(trace_make_inline_string_ref((string_value), (length))))
#define TA_STRING(string_value) \
  (trace_make_string_arg_value(trace_make_inline_c_string_ref(string_value)))
#define TA_POINTER(pointer_value) (trace_make_pointer_arg_value((uintptr_t)pointer_value))
#define TA_KOID(koid_value) (trace_make_koid_arg_value(koid_value))

// This is a helper macro for use in writing one's own TRACE_<event> wrapper
// macros.
//
// |context| is an object of type |trace_context_t*|.
// |variable_name| is a C symbol denoting the variable that will contain the
//   arguments.
// |args| is a potentially empty set of arguments of the form:
//   arg1_name_literal, arg1_value, arg2_name_literal, arg2_value, ...
//   Argument names must be C string literals, i.e., "foo".
//   For C, argument values must use the TA_*() macros to construct the value.
//   E.g., |TA_NULL()|, |TA_BOOL()|, |TA_INT32()|, |TA_UINT32()|, etc.
//   For C++, one can either use the TA_*() macros or for several types the
//   compiler can infer the type: nullptr, bool, int32_t, uint32_t, int64_t,
//   uint64_t, enum, double, const char[] array, const char*.
//
// Example:
// Suppose you want to specify the time of a counter event.
// The basic TRACE_<event> macros always use the current time for simplicity.
// You can either write to the underlying trace-engine API directly,
// or you can write your own helper macro like this:
//
// #define MY_TRACE_COUNTER_WITH_TS(category_literal, name_literal,
//                                  counter_id, timestamp, args...)
//   do {
//     trace_string_ref_t category_ref;
//     trace_context_t* context =
//         trace_acquire_context_for_category((category_literal),
//                                            &category_ref);
//     if (unlikely(context)) {
//       TRACE_DECLARE_ARGS(context, arguments, args);
//       size_t num_args = TRACE_NUM_ARGS(arguments);
//       trace_thread_ref_t thread_ref;
//       trace_string_ref_t name_ref;
//       trace_context_register_current_thread(context, &thread_ref);
//       trace_context_register_string_literal(context, (name_literal),
//                                             &name_ref);
//       TRACE_COMPLETE_ARGS(context, arguments, num_args);
//       trace_context_write_counter_event_record(
//           context, (timestamp), &thread_ref, category_ref,
//           &name_ref, (counter_id), arguments, num_args);
//       trace_release_context(context);
//     }
//   } while (0)
//
// N.B. Trailing backslashes have been intentionally elided, it avoids errors
// if this code is compiled with -Werror=comment. You will need to add them
// back of course.
//
// The above macro is written for illustration's sake. In production use one
// might improve things by using safer names for the local variables (e.g.,
// lest someone defines a macro with their name), and move some of the code
// into a helper routine to reduce code bloat.
// The tracing system recognizes "#define NTRACE" as a way of completely
// disabling tracing by not emitting any code; you may wish to have your macro
// emit zero code if NTRACE is defined.
#define TRACE_DECLARE_ARGS(context, variable_name, args...) \
  TRACE_INTERNAL_DECLARE_ARGS((context), variable_name, args)

// Before the argument list created by |TRACE_DECLARE_ARGS()| can be passed to
// the trace-engine API it must be passed through this. This is done in a
// separate pass as it can reduce the amount of generated code by calling this
// in a helper routine instead of at the TRACE_<event>() call site.
#define TRACE_COMPLETE_ARGS(context, arg_array, num_args) \
  TRACE_INTERNAL_COMPLETE_ARGS((context), (arg_array), (num_args))

// Return the number of arguments in |variable_name|.
#define TRACE_NUM_ARGS(variable_name) TRACE_INTERNAL_NUM_ARGS(variable_name)

#endif  // LIB_TRACE_EVENT_ARGS_H_

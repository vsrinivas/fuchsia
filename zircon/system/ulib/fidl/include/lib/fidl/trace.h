// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_TRACE_H_
#define LIB_FIDL_TRACE_H_

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// This in C, not C++, since the FIDL C bindings call into this.

typedef struct fidl_type fidl_type_t;

typedef enum fidl_trigger_tag {
  // Note that it's possible to split this enum into two: the type ("LLCPP
  // encode" vs "LLCPP decode") and stage ("before"/"after", or "will/did"). We
  // aggregate both those concepts into a single enum to reduce the total number
  // of arguments passed to fidl_trace (by way of the fidl_trace_t struct), to
  // try to ensure that at least the arguments are passed in registers. This is
  // arguably premature optimization, but the API surface for fidl_trace is
  // small, so the consequences of this design doesn't leak out past the
  // immediate call sites.

  // LLCPP

  kFidlTriggerWillLLCPPInPlaceEncode,
  kFidlTriggerDidLLCPPInPlaceEncode,

  kFidlTriggerWillLLCPPLinearizeAndEncode,
  kFidlTriggerDidLLCPPLinearizeAndEncode,

  kFidlTriggerWillLLCPPDecode,
  kFidlTriggerDidLLCPPDecode,

  kFidlTriggerWillLLCPPChannelWrite,
  kFidlTriggerDidLLCPPChannelWrite,

  kFidlTriggerWillLLCPPChannelCall,
  kFidlTriggerDidLLCPPChannelCall,

  // LLCPP Async

  kFidlTriggerWillLLCPPAsyncChannelRead,
  kFidlTriggerDidLLCPPAsyncChannelRead,

  // HLCPP

  kFidlTriggerWillHLCPPEncode,
  kFidlTriggerDidHLCPPEncode,

  kFidlTriggerWillHLCPPDecode,
  kFidlTriggerDidHLCPPDecode,

  kFidlTriggerWillHLCPPValidate,
  kFidlTriggerDidHLCPPValidate,

  kFidlTriggerWillHLCPPChannelWrite,
  kFidlTriggerDidHLCPPChannelWrite,

  kFidlTriggerWillHLCPPChannelRead,
  kFidlTriggerDidHLCPPChannelRead,

  kFidlTriggerWillHLCPPChannelCall,
  kFidlTriggerDidHLCPPChannelCall,

  // C

  kFidlTriggerWillCChannelRead,
  kFidlTriggerDidCChannelRead,

  kFidlTriggerWillCChannelWrite,
  kFidlTriggerDidCChannelWrite,
} fidl_trigger_t;

typedef struct fidl_trace_tag {
  fidl_trigger_t trigger;
  uint16_t handle_count;
  uint32_t size;
  const void* data;
  const fidl_type_t* type;
} fidl_trace_t;

// fidl_trace() is a convenience macro, so that call sites can call
// fidl_trace_impl() with one line, instead of needing to a build a fidl_trace_t
// at the call-site, which uses multiple lines and impedes readability. See
// <https://fuchsia-review.googlesource.com/c/fuchsia/+/405957/4/zircon/system/ulib/fidl/message.cc#60>
// for more context.
//
// The macro is "overloaded" on the number of arguments, and dispatches to
// FIDL_TRACE_n macros for n-number of arguments. See
// <https://stackoverflow.com/questions/11761703/overloading-macro-on-number-of-arguments>
// for a description of the technique.
#define FIDL_TRACE_GET_MACRO(_1, _2, _3, _4, _5, MACRO_NAME, ...) MACRO_NAME
#define fidl_trace(...)                                                  \
  FIDL_TRACE_GET_MACRO(__VA_ARGS__, FIDL_TRACE_5 /* macro for 5 args */, \
                       FIDL_TRACE_INVALID_ARGS /* macro for 4 args */,   \
                       FIDL_TRACE_INVALID_ARGS /* macro for 3 args */,   \
                       FIDL_TRACE_INVALID_ARGS /* macro for 2 args */,   \
                       FIDL_TRACE_1 /* macro for 1 arg */)               \
  (__VA_ARGS__)

#define FIDL_TRACE_INVALID_ARGS(...) INVALID_NUMBER_OF_ARGUMENTS_PASSED_TO_FIDL_TRACE_MACRO

#define FIDL_TRACE_1(trigger_name) \
  FIDL_TRACE_5(trigger_name, NULL /* type */, NULL /* data */, 0 /* size */, 0 /* handle_count */)

#define FIDL_TRACE_5(trigger_name, type_, data_, size_, handle_count_) \
  fidl_trace_impl((fidl_trace_t){                                      \
      .trigger = kFidlTrigger##trigger_name,                           \
      .handle_count = (uint16_t)handle_count_,                         \
      .size = (uint32_t)size_,                                         \
      .data = data_,                                                   \
      .type = type_,                                                   \
  })

#ifndef FIDL_TRACE_LEVEL
// The build system is generally expected to define FIDL_TRACE_LEVEL for
// anything that uses FIDL, but this may not be possible if e.g. the Fuchsia SDK
// being used with other build systems. If FIDL_TRACE_LEVEL isn't defined, it's
// reasonable to assume that tracing isn't wanted.
#define FIDL_TRACE_LEVEL 0
#endif

#if FIDL_TRACE_LEVEL == 0
static inline void fidl_trace_impl(const fidl_trace_t trace_info) {
  // This function is explicitly a no-op. Note that current compilers completely
  // elide a call to this with any level of optimization (-O1, -O2, -Os etc), for
  // both C & C++. See <https://godbolt.org/z/bEa6zG> for an example.
  //
  // Future implementations will replace this fidl_trace_impl() function with a
  // version that may record the trace (e.g. via the Fuchsia Tracing System).
}
#elif FIDL_TRACE_LEVEL == 1
static inline void fidl_trace_impl(const fidl_trace_t trace_info) {
  // We define this no-op implementation of fidl_trace_impl when
  // FIDL_TRACE_LEVEL == 1, so that we can compile the Rust bindings with `fx
  // set ... --args fidl_trace_level=1`. This no-op implementation will be
  // replaced with a call to the Fuchsia Tracing System in the future.
}
#else
#error Unknown FIDL_TRACE_LEVEL.
#endif  // FIDL_TRACE_LEVEL

__END_CDECLS

#endif  // LIB_FIDL_TRACE_H_

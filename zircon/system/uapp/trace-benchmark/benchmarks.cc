// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "benchmarks.h"

#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <stdarg.h>
#include <stdio.h>

#include <fbl/function.h>
#include <trace-engine/buffer_internal.h>
#include <trace-engine/instrumentation.h>
#include <trace-vthread/event_vthread.h>
#include <trace/event.h>

#include "handler.h"
#include "runner.h"

namespace {

using Benchmark = fbl::Function<void()>;

class Runner {
 public:
  Runner(bool enabled, const BenchmarkSpec* spec) : enabled_(enabled), spec_(spec) {}

  void Run(const char* name, Benchmark benchmark) {
    if (enabled_) {
      // The trace engine needs to run in its own thread in order to
      // process buffer full requests in streaming mode while the
      // benchmark is running. Note that records will still get lost
      // if the engine thread is not scheduled frequently enough. This
      // is a stress test so all the app is doing is filling the trace
      // buffer. :-)
      async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
      BenchmarkHandler handler(&loop, spec_->mode, spec_->buffer_size);

      loop.StartThread("trace-engine loop", nullptr);

      RunAndMeasure(
          name, spec_->name, spec_->num_iterations, benchmark, [&handler]() { handler.Start(); },
          [&handler]() { handler.Stop(); });

      loop.Quit();
      loop.JoinThreads();
    } else {
      // For the disabled benchmarks we just use the default number
      // of iterations.
      RunAndMeasure(
          name, spec_->name, benchmark, []() {}, []() {});
    }
  }

  // Utility to print a line of text in the same format as RunAndMeasure.
  void Print(const char* fmt, ...) {
    fputs(kTestOutputPrefix, stdout);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }

 private:
  const bool enabled_;
  // nullptr if |!enabled_|.
  const BenchmarkSpec* spec_;
};

}  // namespace

#define MAKE_TEST_SYMBOL_NAME(prefix, DURATION_MACRO, test_symbol_name, category) \
  prefix##DURATION_MACRO##test_symbol_name##category

// clang-format doesn't understand the leading #
// clang-format off
#define MAKE_TEST_SYMBOL_STRING(prefix, DURATION_MACRO, test_symbol_name, category) \
    #prefix #DURATION_MACRO #test_symbol_name #category
// clang-format on

// N.B. In order for this to work, this code cannot live in the anonymous
// namespace. Otherwise the compiler attaches that namespace to the section
// name which will no longer be a valid C symbol (which is what we need in
// order for the linker to generate the __{start,stop}_SECNAME symbols.
#define RUN_TEST(test_name, pretty_test_name, macro_name, enabled_prefix, category, expression) \
  do {                                                                                          \
    const char* full_test_name = #macro_name " macro with " pretty_test_name ": " #category;    \
    struct Test {                                                                               \
      static void Run()                                                                         \
          __attribute__((__section__(MAKE_TEST_SYMBOL_STRING(test_, macro_name, test_name,      \
                                                             category)))) {                     \
        expression;                                                                             \
      }                                                                                         \
    };                                                                                          \
    runner.Run(full_test_name, Test::Run);                                                      \
    extern const char MAKE_TEST_SYMBOL_NAME(__start_test_, macro_name, test_name, category)[];  \
    extern const char MAKE_TEST_SYMBOL_NAME(__stop_test_, macro_name, test_name, category)[];   \
    size_t test_size = (MAKE_TEST_SYMBOL_NAME(__stop_test_, macro_name, test_name, category) -  \
                        MAKE_TEST_SYMBOL_NAME(__start_test_, macro_name, test_name, category)); \
    runner.Print("test size: %zu bytes\n", test_size);                                          \
  } while (0)

// The niladic version is needed to portably work around the trailing comma
// problem when using __VA_ARGS__.
#define RUN_NILADIC_DURATION_TEST(test_name, pretty_test_name, DURATION_MACRO, enabled_prefix, \
                                  category)                                                    \
  RUN_TEST(test_name, pretty_test_name, DURATION_MACRO, enabled_prefix, category,              \
           DURATION_MACRO(enabled_prefix #category, "name"))

#define RUN_DURATION_TEST(test_name, pretty_test_name, DURATION_MACRO, enabled_prefix, category, \
                          ...)                                                                   \
  RUN_TEST(test_name, pretty_test_name, DURATION_MACRO, enabled_prefix, category,                \
           DURATION_MACRO(enabled_prefix #category, "name", __VA_ARGS__))

#define DURATION_TEST(DURATION_MACRO, enabled_prefix, category)                                    \
  RUN_NILADIC_DURATION_TEST(zero_args, "0 arguments", DURATION_MACRO, enabled_prefix, category);   \
                                                                                                   \
  RUN_DURATION_TEST(one_int32_arg, "1 int32 argument", DURATION_MACRO, enabled_prefix, category,   \
                    "k1", 1);                                                                      \
                                                                                                   \
  RUN_DURATION_TEST(one_double_arg, "1 double argument", DURATION_MACRO, enabled_prefix, category, \
                    "k1", 1.);                                                                     \
                                                                                                   \
  RUN_DURATION_TEST(one_string_arg, "1 string argument", DURATION_MACRO, enabled_prefix, category, \
                    "k1", "string1");                                                              \
                                                                                                   \
  RUN_DURATION_TEST(four_int32_args, "4 int32 arguments", DURATION_MACRO, enabled_prefix,          \
                    category, "k1", 1, "k2", 2, "k3", 3, "k4", 4);                                 \
                                                                                                   \
  RUN_DURATION_TEST(four_double_args, "4 double arguments", DURATION_MACRO, enabled_prefix,        \
                    category, "k1", 1., "k2", 2., "k3", 3., "k4", 4.);                             \
                                                                                                   \
  RUN_DURATION_TEST(four_string_args, "4 string arguments", DURATION_MACRO, enabled_prefix,        \
                    category, "k1", "string1", "k2", "string2", "k3", "string3", "k4", "string4"); \
                                                                                                   \
  RUN_DURATION_TEST(eight_int32_args, "8 int32 arguments", DURATION_MACRO, enabled_prefix,         \
                    category, "k1", 1, "k2", 2, "k3", 3, "k4", 4, "k5", 5, "k6", 6, "k7", 7, "k8", \
                    8);                                                                            \
                                                                                                   \
  RUN_DURATION_TEST(eight_double_args, "8 double arguments", DURATION_MACRO, enabled_prefix,       \
                    category, "k1", 1., "k2", 2., "k3", 3., "k4", 4., "k5", 4., "k6", 5., "k7",    \
                    7., "k8", 8.);                                                                 \
                                                                                                   \
  RUN_DURATION_TEST(eight_string_args, "8 string arguments", DURATION_MACRO, enabled_prefix,       \
                    category, "k1", "string1", "k2", "string2", "k3", "string3", "k4", "string4",  \
                    "k5", "string5", "k6", "string6", "k7", "string7", "k8", "string8");

// This is not in the anonymous namespace so that the namespace name
// doesn't enter into the computation of the location of Test::Run which
// is put in its own section.
static void RunBenchmarks(bool tracing_enabled, const BenchmarkSpec* spec) {
  Runner runner(tracing_enabled, spec);

  runner.Run("is enabled", [] { trace_is_enabled(); });

  runner.Run("is category enabled", [] { trace_is_category_enabled("+enabled"); });

  if (tracing_enabled) {
    runner.Run("is category enabled for disabled category",
               [] { trace_is_category_enabled("-disabled"); });
  }

  runner.Run("acquire / release context", [] {
    trace_context_t* context = trace_acquire_context();
    if (unlikely(context))
      trace_release_context(context);
  });

  runner.Run("acquire / release context for category", [] {
    trace_string_ref_t category_ref;
    trace_context_t* context = trace_acquire_context_for_category("+enabled", &category_ref);
    if (unlikely(context))
      trace_release_context(context);
  });

  if (tracing_enabled) {
    runner.Run("acquire / release context for disabled category", [] {
      trace_string_ref_t category_ref;
      trace_context_t* context = trace_acquire_context_for_category("-disabled", &category_ref);
      ZX_DEBUG_ASSERT(!context);
    });
  }

  DURATION_TEST(TRACE_DURATION_BEGIN, "+", enabled);
  DURATION_TEST(TRACE_DURATION, "+", enabled);

  // There's no real need (yet) to test vthread support with multiple
  // variations of arguments. If we did that for all macros the S/N ratio
  // of the output would drop too much.
  RUN_TEST(zero_args, "0 arguments", TRACE_VTHREAD_DURATION_BEGIN, "+", enabled,
           TRACE_VTHREAD_DURATION_BEGIN("+enabled", "name", "vthread", 1, zx_ticks_get()));

  if (tracing_enabled) {
    DURATION_TEST(TRACE_DURATION_BEGIN, "-", disabled);
    DURATION_TEST(TRACE_DURATION, "-", disabled);

    RUN_TEST(zero_args, "0 arguments", TRACE_VTHREAD_DURATION_BEGIN, "-", disabled,
             TRACE_VTHREAD_DURATION_BEGIN("+enabled", "name", "vthread", 1, zx_ticks_get()));
  }
}

void RunTracingDisabledBenchmarks() {
  static const BenchmarkSpec spec = {
      "tracing off",
      TRACE_BUFFERING_MODE_ONESHOT,  // unused
      0,
      kDefaultRunIterations,
  };
  RunBenchmarks(false, &spec);
}

void RunTracingEnabledBenchmarks(const BenchmarkSpec* spec) { RunBenchmarks(true, spec); }

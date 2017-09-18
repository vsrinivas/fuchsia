// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace/tests/cevent_unittest.h"

#include <zircon/process.h>
#include <stdio.h>

#include "garnet/lib/trace/cevent.h"
#include "garnet/lib/trace/tests/ctrace_test_harness.h"

bool cevent_test_enabled(void) {
  C_EXPECT_EQ(true, CTRACE_ENABLED());

  ctrace_stop_tracing();
  C_EXPECT_EQ(false, CTRACE_ENABLED());

  return true;
}

bool cevent_test_category_enabled(void) {
  C_EXPECT_EQ(true, CTRACE_CATEGORY_ENABLED("cat"));
  C_EXPECT_EQ(false, CTRACE_CATEGORY_ENABLED("disabled"));

  ctrace_stop_tracing();
  C_EXPECT_EQ(false, CTRACE_CATEGORY_ENABLED("cat"));
  C_EXPECT_EQ(false, CTRACE_CATEGORY_ENABLED("disabled"));

  return true;
}

bool cevent_test_trace_nonce(void) {
  // We really don't need to test correct operation of CTRACE_NONCE,
  // that is done by the c++ test. This just exercises that it's callable
  // from C.
  C_EXPECT_NE(CTRACE_NONCE(), 0u);

  return true;
}

bool cevent_test_instant(void) {
  CTRACE_INSTANT("cat", "name", CTRACE_SCOPE_GLOBAL);
  CTRACE_INSTANT("cat", "name", CTRACE_SCOPE_PROCESS);
  CTRACE_INSTANT("cat", "name", CTRACE_SCOPE_THREAD);
  CTRACE_INSTANT("cat", "name", CTRACE_SCOPE_THREAD, TA_STR("k1", "v1"));
  CTRACE_INSTANT("cat", "name", CTRACE_SCOPE_THREAD, TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_INSTANT("cat", "name", CTRACE_SCOPE_THREAD, TA_STR("k1", "v1"),
                 TA_STR("k2", "v2"), TA_STR("k3", "v3"));
  CTRACE_INSTANT("cat", "name", CTRACE_SCOPE_THREAD, TA_STR("k1", "v1"),
                 TA_STR("k2", "v2"), TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_counter(void) {
  CTRACE_COUNTER("cat", "name", 1u, TA_I32("k1", 1));
  CTRACE_COUNTER("cat", "name", 1u, TA_I32("k1", 1), TA_I32("k2", 2));
  CTRACE_COUNTER("cat", "name", 1u, TA_I32("k1", 1), TA_I32("k2", 2),
                 TA_I32("k3", 3));
  CTRACE_COUNTER("cat", "name", 1u, TA_I32("k1", 1), TA_I32("k2", 2),
                 TA_I32("k3", 3), TA_I32("k4", 4));

  return true;
}

bool cevent_test_duration(void) {
  CTRACE_DURATION("cat", "name");
  CTRACE_DURATION("cat", "name", TA_STR("k1", "v1"));
  CTRACE_DURATION("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_DURATION("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                                      TA_STR("k3", "v3"));
  CTRACE_DURATION("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                                      TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_duration_begin(void) {
  CTRACE_DURATION_BEGIN("cat", "name");
  CTRACE_DURATION_BEGIN("cat", "name", TA_STR("k1", "v1"));
  CTRACE_DURATION_BEGIN("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_DURATION_BEGIN("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                        TA_STR("k3", "v3"));
  CTRACE_DURATION_BEGIN("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                        TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_duration_end(void) {
  CTRACE_DURATION_END("cat", "name");
  CTRACE_DURATION_END("cat", "name", TA_STR("k1", "v1"));
  CTRACE_DURATION_END("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_DURATION_END("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                      TA_STR("k3", "v3"));
  CTRACE_DURATION_END("cat", "name", TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                      TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_async_begin(void) {
  CTRACE_ASYNC_BEGIN("cat", "name", 1u);
  CTRACE_ASYNC_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"));
  CTRACE_ASYNC_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_ASYNC_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                     TA_STR("k3", "v3"));
  CTRACE_ASYNC_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                     TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_async_instant(void) {
  CTRACE_ASYNC_INSTANT("cat", "name", 1u);
  CTRACE_ASYNC_INSTANT("cat", "name", 1u, TA_STR("k1", "v1"));
  CTRACE_ASYNC_INSTANT("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_ASYNC_INSTANT("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                       TA_STR("k3", "v3"));
  CTRACE_ASYNC_INSTANT("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                       TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_async_end(void) {
  CTRACE_ASYNC_END("cat", "name", 1u);
  CTRACE_ASYNC_END("cat", "name", 1u, TA_STR("k1", "v1"));
  CTRACE_ASYNC_END("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_ASYNC_END("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"), TA_STR("k3", "v3"));
  CTRACE_ASYNC_END("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"), TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_flow_begin(void) {
  CTRACE_FLOW_BEGIN("cat", "name", 1u);
  CTRACE_FLOW_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"));
  CTRACE_FLOW_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_FLOW_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                    TA_STR("k3", "v3"));
  CTRACE_FLOW_BEGIN("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                    TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_flow_step(void) {
  CTRACE_FLOW_STEP("cat", "name", 1u);
  CTRACE_FLOW_STEP("cat", "name", 1u, TA_STR("k1", "v1"));
  CTRACE_FLOW_STEP("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_FLOW_STEP("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                   TA_STR("k3", "v3"));
  CTRACE_FLOW_STEP("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                   TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_flow_end(void) {
  CTRACE_FLOW_END("cat", "name", 1u);
  CTRACE_FLOW_END("cat", "name", 1u, TA_STR("k1", "v1"));
  CTRACE_FLOW_END("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_FLOW_END("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                  TA_STR("k3", "v3"));
  CTRACE_FLOW_END("cat", "name", 1u, TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                  TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_handle(void) {
  CTRACE_HANDLE(zx_process_self());
  CTRACE_HANDLE(zx_process_self(), TA_STR("k1", "v1"));
  CTRACE_HANDLE(zx_process_self(), TA_STR("k1", "v1"), TA_STR("k2", "v2"));
  CTRACE_HANDLE(zx_process_self(), TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                TA_STR("k3", "v3"));
  CTRACE_HANDLE(zx_process_self(), TA_STR("k1", "v1"), TA_STR("k2", "v2"),
                TA_STR("k3", "v3"), TA_STR("k4", "v4"));

  return true;
}

bool cevent_test_null_arguments(void) {
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", NULL));
  CTRACE_DURATION_BEGIN("cat", "name");

  return true;
}

bool cevent_test_integral_arguments(void) {
  CTRACE_DURATION_BEGIN("cat", "name", TA_U32("key", (bool)(true)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_U32("key", (bool)(false)));

  CTRACE_DURATION_BEGIN("cat", "name", TA_I32("key", (int32_t)(INT32_MIN)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_I32("key", (int32_t)(INT32_MAX)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_I64("key", (int64_t)(INT64_MIN)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_I64("key", (int64_t)(INT64_MAX)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_U32("key", (uint32_t)(0)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_U32("key", (uint32_t)(UINT32_MAX)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_U64("key", (uint64_t)(0)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_U64("key", (uint64_t)(UINT64_MAX)));

  return true;
}

bool cevent_test_enum_arguments(void) {
  enum Int32Enum { kMinusOne = -1, kZero, kOne };
  enum UInt32Enum { kUZero, kUOne };

  CTRACE_DURATION_BEGIN("cat", "name", TA_I32("key", kOne));
  CTRACE_DURATION_BEGIN("cat", "name", TA_U32("key", kUOne));

  return true;
}

bool cevent_test_float_arguments(void) {
  CTRACE_DURATION_BEGIN("cat", "name", TA_DOUBLE("key", (float)(1.f)));
  CTRACE_DURATION_BEGIN("cat", "name", TA_DOUBLE("key", (double)(1.)));

  return true;
}

bool cevent_test_string_arguments(void) {
  const char* kNull = NULL;
  const char* kConstChar = "const char*";
  const char kCharArray[] = "char[n]";

  CTRACE_DURATION_BEGIN("cat", "name", TA_STR("key", kNull));
  CTRACE_DURATION_BEGIN("cat", "name", TA_STR("key", kConstChar));
  CTRACE_DURATION_BEGIN("cat", "name", TA_STR("key", kCharArray));

  return true;
}

bool cevent_test_pointer_arguments(void) {
  void* kNull = NULL;
  const void* kConstNull = NULL;
  volatile void* kVolatileNull = NULL;
  const volatile void* kConstVolatileNull = NULL;
  void* kPtr = &kNull;
  const void* kConstPtr = &kNull;
  volatile void* kVolatilePtr = &kNull;
  const volatile void* kConstVolatilePtr = &kNull;

  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", kNull));
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", kConstNull));
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", (const void*) kVolatileNull));
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", (const void*) kConstVolatileNull));
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", kPtr));
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", kConstPtr));
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", (const void*) kVolatilePtr));
  CTRACE_DURATION_BEGIN("cat", "name", TA_PTR("key", (const void*) kConstVolatilePtr));

  return true;
}

bool cevent_test_koid_arguments(void) {
  CTRACE_DURATION_BEGIN("cat", "name", TA_KOID("key", 42u));

  return true;
}

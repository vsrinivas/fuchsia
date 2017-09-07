// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is included from event_tests.c and event_tests.cpp to exercise
// the different API features when using the C and C++ compilers.
// Portions of some tests are only compiled in C++.
//
// This file is also compiled with and without the NTRACE macro.

#pragma once

#include <trace/event.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#ifdef __cplusplus
#include <fbl/string.h>
#include <fbl/string_piece.h>
#endif // __cplusplus

#include "fixture.h"

#define I32_ARGS1 "k1", TA_INT32(1)
#define I32_ARGS2 "k1", TA_INT32(1), "k2", TA_INT32(2)
#define I32_ARGS3 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3)
#define I32_ARGS4 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4)
#define I32_ARGS5 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                  "k5", TA_INT32(5)
#define I32_ARGS6 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                  "k5", TA_INT32(5), "k6", TA_INT32(6)
#define I32_ARGS7 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                  "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7)
#define I32_ARGS8 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                  "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8)
#define I32_ARGS9 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                  "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8), \
                  "k9", TA_INT32(9)
#define I32_ARGS10 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                   "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8), \
                   "k9", TA_INT32(9), "k10", TA_INT32(10)
#define I32_ARGS11 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                   "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8), \
                   "k9", TA_INT32(9), "k10", TA_INT32(10), "k11", TA_INT32(11)
#define I32_ARGS12 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4), \
                   "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8), \
                   "k9", TA_INT32(9), "k10", TA_INT32(10), "k11", TA_INT32(11), "k12", TA_INT32(12)
#define I32_ARGS13 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4),       \
                   "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8),       \
                   "k9", TA_INT32(9), "k10", TA_INT32(10), "k11", TA_INT32(11), "k12", TA_INT32(12), \
                   "k13", TA_INT32(13)
#define I32_ARGS14 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4),       \
                   "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8),       \
                   "k9", TA_INT32(9), "k10", TA_INT32(10), "k11", TA_INT32(11), "k12", TA_INT32(12), \
                   "k13", TA_INT32(13), "k14", TA_INT32(14)
#define I32_ARGS15 "k1", TA_INT32(1), "k2", TA_INT32(2), "k3", TA_INT32(3), "k4", TA_INT32(4),       \
                   "k5", TA_INT32(5), "k6", TA_INT32(6), "k7", TA_INT32(7), "k8", TA_INT32(8),       \
                   "k9", TA_INT32(9), "k10", TA_INT32(10), "k11", TA_INT32(11), "k12", TA_INT32(12), \
                   "k13", TA_INT32(13), "k14", TA_INT32(14), "k15", TA_INT32(15)

#define ALL15(fn)                                         \
    fn(0) fn(1) fn(2) fn(3) fn(4) fn(5) fn(6) fn(7) fn(8) \
        fn(9) fn(10) fn(11) fn(12) fn(13) fn(14) fn(15)

#define STR_ARGS1 "k1", TA_STRING("v1")
#define STR_ARGS2 "k1", TA_STRING("v1"), "k2", TA_STRING("v2")
#define STR_ARGS3 "k1", TA_STRING("v1"), "k2", TA_STRING("v2"), "k3", TA_STRING("v3")
#define STR_ARGS4 "k1", TA_STRING("v1"), "k2", TA_STRING("v2"), "k3", TA_STRING("v3"), "k4", TA_STRING("v4")

static bool test_enabled(void) {
    BEGIN_TRACE_TEST;

    EXPECT_FALSE(TRACE_ENABLED(), "");

    fixture_start_tracing();
#ifndef NTRACE
    EXPECT_TRUE(TRACE_ENABLED(), "");
#else
    EXPECT_FALSE(TRACE_ENABLED(), "");
#endif // NTRACE

    fixture_stop_tracing();
    EXPECT_FALSE(TRACE_ENABLED(), "");

    END_TRACE_TEST;
}

static bool test_category_enabled(void) {
    BEGIN_TRACE_TEST;

    EXPECT_FALSE(TRACE_CATEGORY_ENABLED("+enabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED("-disabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED(""), "");

    fixture_start_tracing();
#ifndef NTRACE
    EXPECT_TRUE(TRACE_CATEGORY_ENABLED("+enabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED("-disabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED(""), "");
#else
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED("+enabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED("-disabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED(""), "");
#endif // NTRACE

    fixture_stop_tracing();
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED("+enabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED("-disabled"), "");
    EXPECT_FALSE(TRACE_CATEGORY_ENABLED(""), "");

    END_TRACE_TEST;
}

static bool test_trace_nonce(void) {
    BEGIN_TRACE_TEST;

    // Note: TRACE_NONCE() still returns unique values when NTRACE is defined
    // since nonces are available even when tracing is disabled.
    uint64_t nonce1 = TRACE_NONCE();
    EXPECT_NE(0u, nonce1, "nonce is never 0");
    uint64_t nonce2 = TRACE_NONCE();
    EXPECT_NE(0u, nonce1, "nonce is never 0");
    EXPECT_NE(nonce1, nonce2, "nonce is unique");

    END_TRACE_TEST;
}

static bool test_instant(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_GLOBAL);
    TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_PROCESS);
    TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_THREAD);
    TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_THREAD, STR_ARGS1);
    TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_THREAD, STR_ARGS4);
    TRACE_INSTANT("-disabled", "name", TRACE_SCOPE_THREAD);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: global), {})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: process), {})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: thread), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: thread), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: thread), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_counter(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_COUNTER("+enabled", "name", 1u, I32_ARGS1);
    TRACE_COUNTER("+enabled", "name", 1u, I32_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"k1\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Counter(id: 1), {k1: int32(1)})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Counter(id: 1), {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4)})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_duration(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    {
        TRACE_DURATION("+enabled", "name");
        TRACE_DURATION("+enabled", "name", STR_ARGS1);
        TRACE_DURATION("+enabled", "name", STR_ARGS4);
    } // end events are written when the scope exits

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_duration_begin(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name");
    TRACE_DURATION_BEGIN("+enabled", "name", STR_ARGS1);
    TRACE_DURATION_BEGIN("+enabled", "name", STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_duration_end(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_END("+enabled", "name");
    TRACE_DURATION_END("+enabled", "name", STR_ARGS1);
    TRACE_DURATION_END("+enabled", "name", STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_async_begin(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_ASYNC_BEGIN("+enabled", "name", 1u);
    TRACE_ASYNC_BEGIN("+enabled", "name", 1u, STR_ARGS1);
    TRACE_ASYNC_BEGIN("+enabled", "name", 1u, STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncBegin(id: 1), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncBegin(id: 1), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncBegin(id: 1), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_async_instant(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_ASYNC_INSTANT("+enabled", "name", 1u);
    TRACE_ASYNC_INSTANT("+enabled", "name", 1u, STR_ARGS1);
    TRACE_ASYNC_INSTANT("+enabled", "name", 1u, STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncInstant(id: 1), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncInstant(id: 1), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncInstant(id: 1), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_async_end(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_ASYNC_END("+enabled", "name", 1u);
    TRACE_ASYNC_END("+enabled", "name", 1u, STR_ARGS1);
    TRACE_ASYNC_END("+enabled", "name", 1u, STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncEnd(id: 1), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncEnd(id: 1), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", AsyncEnd(id: 1), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_flow_begin(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_FLOW_BEGIN("+enabled", "name", 1u);
    TRACE_FLOW_BEGIN("+enabled", "name", 1u, STR_ARGS1);
    TRACE_FLOW_BEGIN("+enabled", "name", 1u, STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowBegin(id: 1), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowBegin(id: 1), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowBegin(id: 1), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_flow_step(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_FLOW_STEP("+enabled", "name", 1u);
    TRACE_FLOW_STEP("+enabled", "name", 1u, STR_ARGS1);
    TRACE_FLOW_STEP("+enabled", "name", 1u, STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowStep(id: 1), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowStep(id: 1), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowStep(id: 1), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_flow_end(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_FLOW_END("+enabled", "name", 1u);
    TRACE_FLOW_END("+enabled", "name", 1u, STR_ARGS1);
    TRACE_FLOW_END("+enabled", "name", 1u, STR_ARGS4);

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowEnd(id: 1), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowEnd(id: 1), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowEnd(id: 1), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_kernel_object(void) {
    BEGIN_TRACE_TEST;

    mx_handle_t event;
    mx_event_create(0u, &event);

    fixture_start_tracing();

    TRACE_KERNEL_OBJECT(event);
    TRACE_KERNEL_OBJECT(event, STR_ARGS1);
    TRACE_KERNEL_OBJECT(event, STR_ARGS4);

    ASSERT_RECORDS("\
KernelObject(koid: <>, type: event, name: \"\", {})\n\
String(index: 1, \"k1\")\n\
KernelObject(koid: <>, type: event, name: \"\", {k1: string(\"v1\")})\n\
String(index: 2, \"k2\")\n\
String(index: 3, \"k3\")\n\
String(index: 4, \"k4\")\n\
KernelObject(koid: <>, type: event, name: \"\", {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
",
                   "");

    mx_handle_close(event);

    END_TRACE_TEST;
}

static bool test_null_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_NULL());

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", nullptr);
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: null})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: null})\n\
");

    END_TRACE_TEST;
}

// TODO(MG-1033): Define a boolean argument type in the wire format.
static bool test_bool_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT32(true));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT32(false));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", true);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", false);
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(1)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(1)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
");

    END_TRACE_TEST;
}

static bool test_int32_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT32(INT32_MIN));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT32(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT32(INT32_MAX));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int8_t(INT8_MIN));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int8_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int8_t(INT8_MAX));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int16_t(INT16_MIN));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int16_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int16_t(INT16_MAX));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int32_t(INT32_MIN));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int32_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int32_t(INT32_MAX));
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-2147483648)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(2147483647)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-128)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(127)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-32768)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(32767)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-2147483648)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(2147483647)})\n\
");

    END_TRACE_TEST;
}

static bool test_uint32_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT32(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT32(UINT32_MAX));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint8_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint8_t(UINT8_MAX));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint16_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint16_t(UINT16_MAX));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint32_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint32_t(UINT32_MAX));
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(4294967295)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(255)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(65535)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(4294967295)})\n\
");

    END_TRACE_TEST;
}

static bool test_int64_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT64(INT64_MIN));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT64(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT64(INT64_MAX));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int64_t(INT64_MIN));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int64_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", int64_t(INT64_MAX));
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(-9223372036854775808)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(9223372036854775807)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(-9223372036854775808)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(9223372036854775807)})\n\
");

    END_TRACE_TEST;
}

static bool test_uint64_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT64(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT64(UINT64_MAX));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint64_t(0));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", uint64_t(UINT64_MAX));
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(18446744073709551615)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(18446744073709551615)})\n\
");

    END_TRACE_TEST;
}

static bool test_enum_arguments(void) {
    BEGIN_TRACE_TEST;

#ifdef __cplusplus
    enum Int8Enum : int8_t { kInt8_Min = INT8_MIN,
                             kInt8_Zero = 0,
                             kInt8_Max = INT8_MAX };
    enum Uint8Enum : uint8_t { kUint8_Zero = 0,
                               kUint8_Max = UINT8_MAX };
    enum Int16Enum : int16_t { kInt16_Min = INT16_MIN,
                               kInt16_Zero = 0,
                               kInt16_Max = INT16_MAX };
    enum Uint16Enum : uint16_t { kUint16_Zero = 0,
                                 kUint16_Max = UINT16_MAX };
    enum Int32Enum : int32_t { kInt32_Min = INT32_MIN,
                               kInt32_Zero = 0,
                               kInt32_Max = INT32_MAX };
    enum Uint32Enum : uint32_t { kUint32_Zero = 0,
                                 kUint32_Max = UINT32_MAX };
    enum Int64Enum : int64_t { kInt64_Min = INT64_MIN,
                               kInt64_Zero = 0,
                               kInt64_Max = INT64_MAX };
    enum Uint64Enum : uint64_t { kUint64_Zero = 0,
                                 kUint64_Max = UINT64_MAX };
#else
    enum Int32Enum { kInt32_Min = INT32_MIN,
                     kInt32_Zero = 0,
                     kInt32_Max = INT32_MAX };
    enum Uint32Enum { kUint32_Zero = 0,
                      kUint32_Max = UINT32_MAX };
    enum Int64Enum { kInt64_Min = INT64_MIN,
                     kInt64_Zero = 0,
                     kInt64_Max = INT64_MAX };
    enum Uint64Enum { kUint64_Zero = 0,
                      kUint64_Max = UINT64_MAX };
#endif // __cplusplus

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT32(kInt32_Min));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT32(kInt32_Zero));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT32(kInt32_Max));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT32(kUint32_Zero));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT32(kUint32_Max));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT64(kInt64_Min));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT64(kInt64_Zero));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_INT64(kInt64_Max));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT64(kUint64_Zero));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_UINT64(kUint64_Max));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt8_Min);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt8_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt8_Max);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint8_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint8_Max);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt16_Min);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt16_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt16_Max);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint16_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint16_Max);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt32_Min);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt32_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt32_Max);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint32_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint32_Max);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt64_Min);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt64_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kInt64_Max);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint64_Zero);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kUint64_Max);
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-2147483648)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(2147483647)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(4294967295)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(-9223372036854775808)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(9223372036854775807)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(18446744073709551615)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-128)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(127)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(255)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-32768)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(32767)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(65535)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(-2147483648)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int32(2147483647)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint32(4294967295)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(-9223372036854775808)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: int64(9223372036854775807)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: uint64(18446744073709551615)})\n\
");

    END_TRACE_TEST;
}

static bool test_double_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_DOUBLE(1.f));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_DOUBLE(1.));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", 1.f);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", 1.);
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: double(1.000000)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: double(1.000000)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: double(1.000000)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: double(1.000000)})\n\
");

    END_TRACE_TEST;
}

static bool test_char_array_arguments(void) {
    BEGIN_TRACE_TEST;

    char kCharArray[] = "char[n]...";

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_CHAR_ARRAY(NULL, 0u));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_CHAR_ARRAY("", 0u));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_CHAR_ARRAY("literal", 7u));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_CHAR_ARRAY(kCharArray, 7u));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kCharArray);
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"literal\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"char[n]\")})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"char[n]...\")})\n\
");

    END_TRACE_TEST;
}

static bool test_string_arguments(void) {
    BEGIN_TRACE_TEST;

    char string[5] = {'?', '2', '3', '4', '\0'};
    string[0] = '1';

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_STRING(NULL));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_STRING(""));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_STRING("literal"));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_STRING(string));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", (const char*)nullptr);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", "");
    TRACE_DURATION_BEGIN("+enabled", "name", "key", "literal");
    TRACE_DURATION_BEGIN("+enabled", "name", "key", string);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", fbl::String("dynamic string"));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", fbl::StringPiece("piece", 3u));
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"literal\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"1234\")})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"literal\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"1234\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"dynamic string\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"pie\")})\n\
");

    END_TRACE_TEST;
}

static bool test_string_literal_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_STRING_LITERAL(NULL));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_STRING_LITERAL(""));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_STRING_LITERAL("literal"));

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"\")})\n\
String(index: 5, \"literal\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: string(\"literal\")})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_pointer_arguments(void) {
    BEGIN_TRACE_TEST;

    void* kNull = NULL;
    const void* kConstNull = NULL;
    volatile void* kVolatileNull = NULL;
    const volatile void* kConstVolatileNull = NULL;
    void* kPtr = &kNull;
    const void* kConstPtr = &kNull;
    volatile void* kVolatilePtr = &kNull;
    const volatile void* kConstVolatilePtr = &kNull;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kNull));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kConstNull));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kVolatileNull));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kConstVolatileNull));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kPtr));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kConstPtr));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kVolatilePtr));
    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_POINTER(kConstVolatilePtr));

#ifdef __cplusplus
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kNull);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kConstNull);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kVolatileNull);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kConstVolatileNull);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kPtr);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kConstPtr);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kVolatilePtr);
    TRACE_DURATION_BEGIN("+enabled", "name", "key", kConstVolatilePtr);
#endif // __cplusplus

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
",
                   "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(0)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: pointer(<>)})\n\
");

    END_TRACE_TEST;
}

static bool test_koid_arguments(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    TRACE_DURATION_BEGIN("+enabled", "name", "key", TA_KOID(42u));

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"key\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {key: koid(<>)})\n\
",
                   "");

    END_TRACE_TEST;
}

static bool test_all_argument_counts(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

#define FN(n) TRACE_DURATION_BEGIN("+enabled", "name", I32_ARGS##n);
    ALL15(FN)
#undef FN

    ASSERT_RECORDS("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1)})\n\
String(index: 5, \"k2\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2)})\n\
String(index: 6, \"k3\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3)})\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4)})\n\
String(index: 8, \"k5\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5)})\n\
String(index: 9, \"k6\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6)})\n\
String(index: 10, \"k7\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7)})\n\
String(index: 11, \"k8\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8)})\n\
String(index: 12, \"k9\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8), k9: int32(9)})\n\
String(index: 13, \"k10\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8), k9: int32(9), k10: int32(10)})\n\
String(index: 14, \"k11\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8), k9: int32(9), k10: int32(10), k11: int32(11)})\n\
String(index: 15, \"k12\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8), k9: int32(9), k10: int32(10), k11: int32(11), k12: int32(12)})\n\
String(index: 16, \"k13\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8), k9: int32(9), k10: int32(10), k11: int32(11), k12: int32(12), k13: int32(13)})\n\
String(index: 17, \"k14\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8), k9: int32(9), k10: int32(10), k11: int32(11), k12: int32(12), k13: int32(13), k14: int32(14)})\n\
String(index: 18, \"k15\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: int32(1), k2: int32(2), k3: int32(3), k4: int32(4), k5: int32(5), k6: int32(6), k7: int32(7), k8: int32(8), k9: int32(9), k10: int32(10), k11: int32(11), k12: int32(12), k13: int32(13), k14: int32(14), k15: int32(15)})\n\
",
                   "");

    END_TRACE_TEST;
}

#ifdef __cplusplus
#ifndef NTRACE
#define _NAME event_tests_cpp
#else
#define _NAME event_tests_cpp_ntrace
#endif // NTRACE
#else
#ifndef NTRACE
#define _NAME event_tests_c
#else
#define _NAME event_tests_c_ntrace
#endif // NTRACE
#endif // __cplusplus

#define _BEGIN_TEST_CASE(name) BEGIN_TEST_CASE(name)
#define _END_TEST_CASE(name) END_TEST_CASE(name)

_BEGIN_TEST_CASE(_NAME)
RUN_TEST(test_enabled)
RUN_TEST(test_category_enabled)
RUN_TEST(test_trace_nonce)
RUN_TEST(test_instant)
RUN_TEST(test_counter)
RUN_TEST(test_duration)
RUN_TEST(test_duration_begin)
RUN_TEST(test_duration_end)
RUN_TEST(test_async_begin)
RUN_TEST(test_async_instant)
RUN_TEST(test_async_end)
RUN_TEST(test_flow_begin)
RUN_TEST(test_flow_step)
RUN_TEST(test_flow_end)
RUN_TEST(test_kernel_object)
RUN_TEST(test_null_arguments)
RUN_TEST(test_bool_arguments)
RUN_TEST(test_int32_arguments)
RUN_TEST(test_uint32_arguments)
RUN_TEST(test_int64_arguments)
RUN_TEST(test_uint64_arguments)
RUN_TEST(test_enum_arguments)
RUN_TEST(test_double_arguments)
RUN_TEST(test_char_array_arguments)
RUN_TEST(test_string_arguments)
RUN_TEST(test_string_literal_arguments)
RUN_TEST(test_pointer_arguments)
RUN_TEST(test_koid_arguments)
RUN_TEST(test_all_argument_counts)
_END_TEST_CASE(_NAME)

#undef _LANG
#undef _NAME
#undef _BEGIN_TEST_CASE
#undef _END_TEST_CASE

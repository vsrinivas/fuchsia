// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fixture.h"

#include <threads.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <mx/event.h>
#include <trace-engine/instrumentation.h>

namespace {
int RunClosure(void* arg) {
    auto closure = static_cast<fbl::Closure*>(arg);
    (*closure)();
    delete closure;
    return 0;
}

void RunThread(fbl::Closure closure) {
    thrd_t thread;
    int result = thrd_create(&thread, RunClosure,
                             new fbl::Closure(fbl::move(closure)));
    MX_ASSERT(result == thrd_success);

    result = thrd_join(thread, nullptr);
    MX_ASSERT(result == thrd_success);
}

bool test_normal_shutdown() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();
    fixture_stop_tracing();
    EXPECT_EQ(MX_OK, fixture_get_disposition());

    END_TRACE_TEST;
}

bool test_hard_shutdown() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();
    fixture_stop_tracing_hard();
    EXPECT_EQ(MX_ERR_CANCELED, fixture_get_disposition());

    END_TRACE_TEST;
}

bool test_state() {
    BEGIN_TRACE_TEST;

    EXPECT_EQ(TRACE_STOPPED, trace_state());

    fixture_start_tracing();
    EXPECT_EQ(TRACE_STARTED, trace_state());

    fixture_stop_tracing();
    EXPECT_EQ(TRACE_STOPPED, trace_state());

    END_TRACE_TEST;
}

bool test_is_enabled() {
    BEGIN_TRACE_TEST;

    EXPECT_FALSE(trace_is_enabled(), "");

    fixture_start_tracing();
    EXPECT_TRUE(trace_is_enabled(), "");

    fixture_stop_tracing();
    EXPECT_FALSE(trace_is_enabled(), "");

    END_TRACE_TEST;
}

bool test_is_category_enabled() {
    BEGIN_TRACE_TEST;

    EXPECT_FALSE(trace_is_category_enabled("+enabled"), "");
    EXPECT_FALSE(trace_is_category_enabled("-disabled"), "");
    EXPECT_FALSE(trace_is_category_enabled(""), "");

    fixture_start_tracing();
    EXPECT_TRUE(trace_is_category_enabled("+enabled"), "");
    EXPECT_FALSE(trace_is_category_enabled("-disabled"), "");
    EXPECT_FALSE(trace_is_category_enabled(""), "");

    fixture_stop_tracing();
    EXPECT_FALSE(trace_is_category_enabled("+enabled"), "");
    EXPECT_FALSE(trace_is_category_enabled("-disabled"), "");
    EXPECT_FALSE(trace_is_category_enabled(""), "");

    END_TRACE_TEST;
}

bool test_generate_nonce() {
    BEGIN_TRACE_TEST;

    uint64_t nonce1 = trace_generate_nonce();
    EXPECT_NE(0u, nonce1, "nonce is never 0");

    uint64_t nonce2 = trace_generate_nonce();
    EXPECT_NE(0u, nonce2, "nonce is never 0");

    EXPECT_NE(nonce1, nonce2, "nonce is unique");

    END_TRACE_TEST;
}

bool test_observer() {
    BEGIN_TRACE_TEST;

    mx::event event;
    EXPECT_EQ(MX_OK, mx::event::create(0u, &event));

    EXPECT_EQ(MX_OK, trace_register_observer(event.get()));
    EXPECT_EQ(MX_ERR_TIMED_OUT, event.wait_one(MX_EVENT_SIGNALED, 0u, nullptr));

    fixture_start_tracing();
    EXPECT_EQ(MX_OK, event.wait_one(MX_EVENT_SIGNALED, 0u, nullptr));

    EXPECT_EQ(MX_OK, event.signal(MX_EVENT_SIGNALED, 0u));
    EXPECT_EQ(MX_ERR_TIMED_OUT, event.wait_one(MX_EVENT_SIGNALED, 0u, nullptr));

    fixture_stop_tracing();
    EXPECT_EQ(MX_OK, event.wait_one(MX_EVENT_SIGNALED, 0u, nullptr));

    EXPECT_EQ(MX_OK, trace_unregister_observer(event.get()));

    END_TRACE_TEST;
}

bool test_observer_errors() {
    BEGIN_TRACE_TEST;

    mx::event event;
    EXPECT_EQ(MX_OK, mx::event::create(0u, &event));

    EXPECT_EQ(MX_OK, trace_register_observer(event.get()));
    EXPECT_EQ(MX_ERR_INVALID_ARGS, trace_register_observer(event.get()));

    EXPECT_EQ(MX_OK, trace_unregister_observer(event.get()));
    EXPECT_EQ(MX_ERR_NOT_FOUND, trace_unregister_observer(event.get()));

    END_TRACE_TEST;
}

bool test_register_current_thread() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_thread_ref_t t1, t2;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_current_thread(context.get(), &t1);
        trace_context_register_current_thread(context.get(), &t2);
    }

    EXPECT_TRUE(trace_is_indexed_thread_ref(&t1));
    EXPECT_TRUE(trace_is_indexed_thread_ref(&t2));
    EXPECT_EQ(t1.encoded_value, t2.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "process")
KernelObject(koid: <>, type: thread, name: "initial-thread", {process: koid(<>)})
Thread(index: 1, <>)
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_current_thread_multiple_threads() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_thread_ref_t t1;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_current_thread(context.get(), &t1);
    }

    trace_thread_ref_t t2;
    RunThread([&t2] {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_current_thread(context.get(), &t2);
    });

    EXPECT_TRUE(trace_is_indexed_thread_ref(&t1));
    EXPECT_TRUE(trace_is_indexed_thread_ref(&t2));
    EXPECT_NE(t1.encoded_value, t2.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "process")
KernelObject(koid: <>, type: thread, name: "initial-thread", {process: koid(<>)})
Thread(index: 1, <>)
String(index: 2, "process")
KernelObject(koid: <>, type: thread, name: "thrd_t:<>/TLS=<>", {process: koid(<>)})
Thread(index: 2, <>)
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_string_literal() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_string_ref_t empty;
    trace_string_ref_t null;
    trace_string_ref_t a1, a2, a3;
    trace_string_ref_t b1, b2, b3;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_string_literal(context.get(), "", &empty);

        trace_context_register_string_literal(context.get(), nullptr, &null);

        trace_context_register_string_literal(context.get(), "string1", &a1);
        trace_context_register_string_literal(context.get(), "string2", &a2);
        trace_context_register_string_literal(context.get(), "string3", &a3);

        trace_context_register_string_literal(context.get(), "string1", &b1);
        trace_context_register_string_literal(context.get(), "string2", &b2);
        trace_context_register_string_literal(context.get(), "string3", &b3);
    }

    EXPECT_TRUE(trace_is_empty_string_ref(&empty));
    EXPECT_TRUE(trace_is_empty_string_ref(&null));

    EXPECT_TRUE(trace_is_indexed_string_ref(&a1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&a2));
    EXPECT_TRUE(trace_is_indexed_string_ref(&a3));

    EXPECT_TRUE(trace_is_indexed_string_ref(&b1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&b2));
    EXPECT_TRUE(trace_is_indexed_string_ref(&b3));

    EXPECT_EQ(a1.encoded_value, b1.encoded_value);
    EXPECT_EQ(a2.encoded_value, b2.encoded_value);
    EXPECT_EQ(a3.encoded_value, b3.encoded_value);

    EXPECT_NE(a1.encoded_value, a2.encoded_value);
    EXPECT_NE(a1.encoded_value, a3.encoded_value);
    EXPECT_NE(a2.encoded_value, a3.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "string1")
String(index: 2, "string2")
String(index: 3, "string3")
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_string_literal_multiple_threads() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_string_ref_t a1;
    trace_string_ref_t a2;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_string_literal(context.get(), "string1", &a1);
        trace_context_register_string_literal(context.get(), "string2", &a2);
    }

    trace_string_ref_t b1;
    trace_string_ref_t b2;
    RunThread([&b1, &b2] {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_string_literal(context.get(), "string1", &b1);
        trace_context_register_string_literal(context.get(), "string2", &b2);
    });

    EXPECT_TRUE(trace_is_indexed_string_ref(&a1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&a2));

    EXPECT_TRUE(trace_is_indexed_string_ref(&b1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&b2));

    EXPECT_NE(a1.encoded_value, a2.encoded_value);
    EXPECT_NE(b1.encoded_value, b2.encoded_value);

    // Each thread has its own string pool.
    EXPECT_NE(a1.encoded_value, b1.encoded_value);
    EXPECT_NE(a2.encoded_value, b2.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "string1")
String(index: 2, "string2")
String(index: 3, "string1")
String(index: 4, "string2")
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_string_literal_table_overflow() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    fbl::Vector<fbl::String> strings;

    {
        auto context = trace::TraceContext::Acquire();

        unsigned n = 0;
        for (n = 0; n < TRACE_ENCODED_STRING_REF_MAX_INDEX; n++) {
            trace_string_ref_t r;
            fbl::String string = fbl::StringPrintf("string%d", n);
            strings.push_back(string);
            trace_context_register_string_literal(context.get(), string.c_str(), &r);
            if (trace_is_inline_string_ref(&r))
                break;
        }
        EXPECT_GT(n, 100); // at least 100 string can be cached per thread
    }

    END_TRACE_TEST;
}

bool test_maximum_record_length() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    {
        auto context = trace::TraceContext::Acquire();

        EXPECT_NONNULL(trace_context_alloc_record(context.get(), 0));
        EXPECT_NONNULL(trace_context_alloc_record(context.get(), 8));
        EXPECT_NONNULL(trace_context_alloc_record(context.get(), 16));
        EXPECT_NONNULL(trace_context_alloc_record(
            context.get(), TRACE_ENCODED_RECORD_MAX_LENGTH));

        EXPECT_NULL(trace_context_alloc_record(
            context.get(), TRACE_ENCODED_RECORD_MAX_LENGTH + 8));
        EXPECT_NULL(trace_context_alloc_record(
            context.get(), TRACE_ENCODED_RECORD_MAX_LENGTH + 16));
    }

    END_TRACE_TEST;
}

bool test_event_with_inline_everything() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_string_ref_t cat = trace_make_inline_c_string_ref("cat");
    trace_string_ref_t name = trace_make_inline_c_string_ref("name");
    trace_thread_ref_t thread = trace_make_inline_thread_ref(123, 456);
    trace_arg_t args[] = {
        trace_make_arg(trace_make_inline_c_string_ref("argname"),
                       trace_make_string_arg_value(trace_make_inline_c_string_ref("argvalue")))};

    {
        auto context = trace::TraceContext::Acquire();

        trace_context_write_instant_event_record(context.get(), mx_ticks_get(),
                                                 &thread, &cat, &name,
                                                 TRACE_SCOPE_GLOBAL,
                                                 args, fbl::count_of(args));
    }

    ASSERT_RECORDS(R"X(Event(ts: <>, pt: <>, category: "cat", name: "name", Instant(scope: global), {argname: string("argvalue")})
)X",
                   "");

    END_TRACE_TEST;
}

// NOTE: The functions for writing trace records are exercised by other trace tests.

} // namespace

BEGIN_TEST_CASE(engine_tests)
RUN_TEST(test_normal_shutdown)
RUN_TEST(test_hard_shutdown)
RUN_TEST(test_is_enabled)
RUN_TEST(test_is_category_enabled)
RUN_TEST(test_generate_nonce)
RUN_TEST(test_observer)
RUN_TEST(test_observer_errors)
RUN_TEST(test_register_current_thread)
RUN_TEST(test_register_current_thread_multiple_threads)
RUN_TEST(test_register_string_literal)
RUN_TEST(test_register_string_literal_multiple_threads)
RUN_TEST(test_register_string_literal_table_overflow)
RUN_TEST(test_maximum_record_length)
RUN_TEST(test_event_with_inline_everything)
END_TEST_CASE(engine_tests)

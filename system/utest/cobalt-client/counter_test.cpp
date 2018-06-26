// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/observation.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace {

constexpr uint64_t kMetricId = 1;

constexpr uint32_t kEncodingId = 2;

constexpr char kName[] = "SomeName";

constexpr uint64_t kThreads = 20;

Counter MakeCounter() {
    return Counter(kName, kMetricId, kEncodingId);
}

bool MetricIdTest() {
    BEGIN_TEST;
    Counter counter = MakeCounter();
    ASSERT_EQ(counter.metric_id(), kMetricId);
    END_TEST;
}

bool IncrementTest() {
    BEGIN_TEST;
    Counter counter = MakeCounter();
    ASSERT_EQ(counter.Load(), 0);
    counter.Increment();
    ASSERT_EQ(counter.Load(), 1);
    counter.Increment(23);
    ASSERT_EQ(counter.Load(), 24);
    END_TEST;
}

bool ExchangeTest() {
    BEGIN_TEST;
    Counter counter = MakeCounter();
    ASSERT_EQ(counter.Load(), 0);
    ASSERT_EQ(counter.Exchange(25), 0);
    ASSERT_EQ(counter.Load(), 25);
    ASSERT_EQ(counter.Exchange(34), 25);
    ASSERT_EQ(counter.Load(), 34);
    END_TEST;
}

bool GetObservationValueTest() {
    BEGIN_TEST;
    Counter counter = MakeCounter();
    counter.Exchange(24);
    ObservationValue val = counter.GetObservationValue();
    ASSERT_EQ(val.name.size, strlen(kName));
    ASSERT_NE(val.name.data, nullptr);
    ASSERT_STR_EQ(val.name.data, kName);
    ASSERT_EQ(val.encoding_id, kEncodingId);
    ASSERT_EQ(val.value.int_value, 24);
    ASSERT_EQ(counter.Load(), 24);
    END_TEST;
}

bool GetObservationValueAndExchangeTest() {
    BEGIN_TEST;
    Counter counter = MakeCounter();
    counter.Exchange(24);
    ObservationValue val = counter.GetObservationValueAndExchange();
    ASSERT_EQ(val.name.size, strlen(kName));
    ASSERT_NE(val.name.data, nullptr);
    ASSERT_STR_EQ(val.name.data, kName);
    ASSERT_EQ(val.encoding_id, kEncodingId);
    ASSERT_EQ(val.value.int_value, 24);
    ASSERT_EQ(counter.Load(), 0);
    END_TEST;
}

struct IncrementFnArgs {
    Counter* counter;
    sync_completion_t wait_for_start;
};

int IncrementFn(void* void_args) {
    IncrementFnArgs* args = static_cast<IncrementFnArgs*>(void_args);
    sync_completion_wait(&args->wait_for_start, zx::sec(20).get());

    args->counter->Increment();
    return thrd_success;
}

bool MultithreadedIncrementTest() {
    BEGIN_TEST;
    IncrementFnArgs args;
    Counter counter = MakeCounter();
    thrd_t thread_ids[kThreads];
    args.counter = &counter;

    ASSERT_EQ(counter.Load(), 0);
    for (size_t thread = 0; thread < kThreads; ++thread) {
        auto& thread_id = thread_ids[thread];
        ASSERT_EQ(thrd_create(&thread_id, IncrementFn, &args), thrd_success);
    }

    sync_completion_signal(&args.wait_for_start);

    for (auto thread_id : thread_ids) {
        int res = thrd_success;
        ASSERT_EQ(thrd_join(thread_id, &res), thrd_success);
        ASSERT_EQ(res, thrd_success);
    }

    ASSERT_EQ(counter.Load(), kThreads);
    END_TEST;
}

struct ExchangeFnArgs {
    Counter* counter;
    sync_completion_t* wait_for_start;
    fbl::atomic<uint64_t>* sum_of_exchanged;
    uint64_t set_val_to;
};

int ExchangeFn(void* void_args) {
    ExchangeFnArgs* args = static_cast<ExchangeFnArgs*>(void_args);
    sync_completion_wait(args->wait_for_start, zx::sec(20).get());

    uint64_t value = args->counter->Exchange(args->set_val_to);
    args->sum_of_exchanged->fetch_add(value, Counter::kMemoryOrder);
    return thrd_success;
}

bool MultithreadedExchangeTest() {
    BEGIN_TEST;
    ExchangeFnArgs thread_args[kThreads];
    fbl::atomic<uint64_t> sum_of_exchanged(0);
    Counter counter = MakeCounter();
    thrd_t thread_ids[kThreads];
    sync_completion_t wait_for_start;

    ASSERT_EQ(counter.Load(), 0);
    for (size_t thread = 0; thread < kThreads; ++thread) {
        auto& args = thread_args[thread];
        args.counter = &counter;
        args.wait_for_start = &wait_for_start;
        args.set_val_to = thread;
        args.sum_of_exchanged = &sum_of_exchanged;
        auto& thread_id = thread_ids[thread];
        ASSERT_EQ(thrd_create(&thread_id, ExchangeFn, &args), thrd_success);
    }

    sync_completion_signal(&wait_for_start);

    for (auto thread_id : thread_ids) {
        int res = thrd_success;
        ASSERT_EQ(thrd_join(thread_id, &res), thrd_success);
        ASSERT_EQ(res, thrd_success);
    }

    // The current value in the counter + the added value of all exhanged values
    // should add to the sum from 0...kThreads.
    ASSERT_EQ(counter.Load() + sum_of_exchanged.load(Counter::kMemoryOrder),
              (kThreads) * (kThreads - 1) / 2);
    END_TEST;
}

BEGIN_TEST_CASE(CounterTest)
RUN_TEST(MetricIdTest)
RUN_TEST(IncrementTest)
RUN_TEST(ExchangeTest)
RUN_TEST(GetObservationValueTest)
RUN_TEST(GetObservationValueAndExchangeTest)
RUN_TEST(MultithreadedIncrementTest)
RUN_TEST(MultithreadedExchangeTest)
END_TEST_CASE(CounterTest)

} // namespace
} // namespace cobalt_client

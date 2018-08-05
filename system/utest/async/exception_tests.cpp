// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <lib/async-testutils/dispatcher_stub.h>
#include <lib/async/cpp/exception.h>
#include <unittest/unittest.h>

namespace {

const zx_handle_t dummy_task = static_cast<zx_handle_t>(1);
const uint32_t dummy_options = 99;
const zx_koid_t dummy_pid = 23;
const zx_koid_t dummy_tid = 42;
const zx_port_packet_t dummy_exception{
    .key = 0,
    .type = 0,
    .status = ZX_OK,
    .exception = {
        .pid = dummy_pid,
        .tid = dummy_tid,
        .reserved0 = 0u,
        .reserved1 = 0u},
};

class MockDispatcher : public async::DispatcherStub {
public:
    enum class Op {
        NONE,
        BIND,
        UNBIND,
    };

    zx_status_t BindExceptionPort(async_exception_t* exception) override {
        printf("%s: called\n", __func__);
        last_op = Op::BIND;
        last_exception = exception;
        return next_status;
    }

    zx_status_t UnbindExceptionPort(async_exception_t* exception) override {
        printf("%s: called\n", __func__);
        last_op = Op::UNBIND;
        last_exception = exception;
        return next_status;
    }

    Op last_op = Op::NONE;
    async_exception_t* last_exception = nullptr;
    zx_status_t next_status = ZX_OK;
};

class Harness {
public:
    Harness() { Reset(); }

    void Reset() {
        handler_ran = false;
        last_exception = nullptr;
        last_status = ZX_ERR_INTERNAL;
        last_report = nullptr;
    }

    void Handler(async_dispatcher_t* dispatcher, async::ExceptionBase* exception,
                 zx_status_t status, const zx_port_packet_t* report) {
        handler_ran = true;
        last_exception = exception;
        last_status = status;
        last_report = report;
    }

    virtual async::ExceptionBase& exception() = 0;

    bool handler_ran;
    async::ExceptionBase* last_exception;
    zx_status_t last_status;
    const zx_port_packet_t* last_report;
};

class LambdaHarness : public Harness {
public:
    LambdaHarness(zx_handle_t task = ZX_HANDLE_INVALID,
                  uint32_t options = 0)
        : exception_{task, options,
                [this](async_dispatcher_t* dispatcher, async::Exception* exception,
                       zx_status_t status, const zx_port_packet_t* report) {
                    Handler(dispatcher, exception, status, report);
                }} {}

    async::ExceptionBase& exception() override { return exception_; }

private:
    async::Exception exception_;
};

class MethodHarness : public Harness {
public:
    MethodHarness(zx_handle_t task = ZX_HANDLE_INVALID,
                  uint32_t options = 0)
        : exception_{this, task, options} {}

    async::ExceptionBase& exception() override { return exception_; }

private:
    async::ExceptionMethod<Harness, &Harness::Handler> exception_;
};

template <typename Harness>
bool exception_is_bound_test() {
    BEGIN_TEST;

    MockDispatcher dispatcher;
    Harness harness;

    EXPECT_FALSE(harness.exception().is_bound());
    EXPECT_EQ(harness.exception().Bind(&dispatcher), ZX_OK);
    EXPECT_TRUE(harness.exception().is_bound());
    EXPECT_EQ(harness.exception().Unbind(), ZX_OK);
    EXPECT_FALSE(harness.exception().is_bound());

    END_TEST;
}

template <typename Harness>
bool exception_bind_test() {
    BEGIN_TEST;

    MockDispatcher dispatcher;

    {
        Harness harness(dummy_task, dummy_options);
        EXPECT_FALSE(harness.exception().is_bound());

        dispatcher.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, harness.exception().Bind(&dispatcher));
        EXPECT_TRUE(harness.exception().is_bound());
        EXPECT_EQ(MockDispatcher::Op::BIND, dispatcher.last_op);
        EXPECT_EQ(dummy_task, dispatcher.last_exception->task);
        EXPECT_EQ(dummy_options, dispatcher.last_exception->options);
        EXPECT_FALSE(harness.handler_ran);

        harness.Reset();
        dispatcher.last_op = MockDispatcher::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, harness.exception().Bind(&dispatcher));
        EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockDispatcher::Op::UNBIND, dispatcher.last_op);

    {
        Harness harness(dummy_task, dummy_options);
        EXPECT_FALSE(harness.exception().is_bound());

        dispatcher.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, harness.exception().Bind(&dispatcher));
        EXPECT_EQ(MockDispatcher::Op::BIND, dispatcher.last_op);
        EXPECT_FALSE(harness.exception().is_bound());
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockDispatcher::Op::BIND, dispatcher.last_op);

    END_TEST;
}

template <typename Harness>
bool exception_unbind_test() {
    BEGIN_TEST;

    MockDispatcher dispatcher;

    {
        Harness harness(dummy_task, dummy_options);
        EXPECT_FALSE(harness.exception().is_bound());

        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.exception().Unbind());
        EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
        EXPECT_FALSE(harness.exception().is_bound());

        EXPECT_EQ(ZX_OK, harness.exception().Bind(&dispatcher));
        EXPECT_EQ(MockDispatcher::Op::BIND, dispatcher.last_op);
        EXPECT_TRUE(harness.exception().is_bound());

        EXPECT_EQ(ZX_OK, harness.exception().Unbind());
        EXPECT_EQ(MockDispatcher::Op::UNBIND, dispatcher.last_op);
        EXPECT_FALSE(harness.exception().is_bound());

        dispatcher.last_op = MockDispatcher::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.exception().Unbind());
        EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
        EXPECT_FALSE(harness.exception().is_bound());
    }
    EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);

    END_TEST;
}

template <typename Harness>
bool exception_run_handler_test() {
    BEGIN_TEST;

    MockDispatcher dispatcher;

    {
        Harness harness(dummy_task, dummy_options);
        EXPECT_FALSE(harness.exception().is_bound());

        EXPECT_EQ(ZX_OK, harness.exception().Bind(&dispatcher));
        EXPECT_EQ(MockDispatcher::Op::BIND, dispatcher.last_op);
        EXPECT_TRUE(harness.exception().is_bound());

        harness.Reset();
        dispatcher.last_exception->handler(&dispatcher, dispatcher.last_exception, ZX_OK, &dummy_exception);
        EXPECT_TRUE(harness.handler_ran);
        EXPECT_EQ(&harness.exception(), harness.last_exception);
        EXPECT_EQ(ZX_OK, harness.last_status);
        EXPECT_EQ(&dummy_exception, harness.last_report);
        EXPECT_TRUE(harness.exception().is_bound());
    }
    EXPECT_EQ(MockDispatcher::Op::UNBIND, dispatcher.last_op);

    END_TEST;
}

bool unsupported_bind_test() {
    BEGIN_TEST;

    async::DispatcherStub dispatcher;
    async_exception_t exception{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED,
              async_bind_exception_port(&dispatcher, &exception),
              "valid args");

    END_TEST;
}

bool unsupported_unbind_test() {
    BEGIN_TEST;

    async::DispatcherStub dispatcher;
    async_exception_t exception{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED,
              async_unbind_exception_port(&dispatcher, &exception),
              "valid args");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(exception_tests)
RUN_TEST((exception_is_bound_test<LambdaHarness>))
RUN_TEST((exception_is_bound_test<MethodHarness>))
RUN_TEST((exception_bind_test<LambdaHarness>))
RUN_TEST((exception_bind_test<MethodHarness>))
RUN_TEST((exception_unbind_test<LambdaHarness>))
RUN_TEST((exception_unbind_test<MethodHarness>))
RUN_TEST((exception_run_handler_test<LambdaHarness>))
RUN_TEST((exception_run_handler_test<MethodHarness>))
RUN_TEST(unsupported_bind_test)
RUN_TEST(unsupported_unbind_test)
END_TEST_CASE(exception_tests)

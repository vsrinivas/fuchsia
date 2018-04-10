// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/async_stub.h>
#include <lib/async/cpp/wait.h>
#include <unittest/unittest.h>

namespace {

const zx_handle_t dummy_handle = static_cast<zx_handle_t>(1);
const zx_signals_t dummy_trigger = ZX_USER_SIGNAL_0;
const zx_packet_signal_t dummy_signal{
    .trigger = dummy_trigger,
    .observed = ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1,
    .count = 0u,
    .reserved0 = 0u,
    .reserved1 = 0u};

class MockAsync : public async::AsyncStub {
public:
    enum class Op {
        NONE,
        BEGIN_WAIT,
        CANCEL_WAIT,
    };

    zx_status_t BeginWait(async_wait_t* wait) override {
        last_op = Op::BEGIN_WAIT;
        last_wait = wait;
        return next_status;
    }

    zx_status_t CancelWait(async_wait_t* wait) override {
        last_op = Op::CANCEL_WAIT;
        last_wait = wait;
        return next_status;
    }

    Op last_op = Op::NONE;
    async_wait_t* last_wait = nullptr;
    zx_status_t next_status = ZX_OK;
};

class Harness {
public:
    Harness() { Reset(); }

    void Reset() {
        handler_ran = false;
        last_wait = nullptr;
        last_status = ZX_ERR_INTERNAL;
        last_signal = nullptr;
    }

    void Handler(async_t* async, async::WaitBase* wait, zx_status_t status,
                 const zx_packet_signal_t* signal) {
        handler_ran = true;
        last_wait = wait;
        last_status = status;
        last_signal = signal;
    }

    virtual async::WaitBase& wait() = 0;

    bool handler_ran;
    async::WaitBase* last_wait;
    zx_status_t last_status;
    const zx_packet_signal_t* last_signal;
};

class LambdaHarness : public Harness {
public:
    LambdaHarness(zx_handle_t object = ZX_HANDLE_INVALID,
                  zx_signals_t trigger = ZX_SIGNAL_NONE)
        : wait_{object, trigger,
                [this](async_t* async, async::Wait* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
                    Handler(async, wait, status, signal);
                }} {}

    async::WaitBase& wait() override { return wait_; }

private:
    async::Wait wait_;
};

class MethodHarness : public Harness {
public:
    MethodHarness(zx_handle_t object = ZX_HANDLE_INVALID,
                  zx_signals_t trigger = ZX_SIGNAL_NONE)
        : wait_{this, object, trigger} {}

    async::WaitBase& wait() override { return wait_; }

private:
    async::WaitMethod<Harness, &Harness::Handler> wait_;
};

bool wait_set_handler_test() {
    BEGIN_TEST;

    {
        async::Wait wait;
        EXPECT_FALSE(wait.has_handler());
        EXPECT_FALSE(wait.is_pending());

        wait.set_handler([](async_t* async, async::Wait* wait, zx_status_t status,
                            const zx_packet_signal_t* signal) {});
        EXPECT_TRUE(wait.has_handler());
    }

    {
        async::Wait wait(ZX_HANDLE_INVALID, ZX_SIGNAL_NONE,
                         [](async_t* async, async::Wait* wait, zx_status_t status,
                            const zx_packet_signal_t* signal) {});
        EXPECT_TRUE(wait.has_handler());
        EXPECT_FALSE(wait.is_pending());
    }

    END_TEST;
}

template <typename Harness>
bool wait_properties_test() {
    BEGIN_TEST;

    Harness harness;

    EXPECT_EQ(ZX_HANDLE_INVALID, harness.wait().object());
    harness.wait().set_object(dummy_handle);
    EXPECT_EQ(dummy_handle, harness.wait().object());

    EXPECT_EQ(ZX_SIGNAL_NONE, harness.wait().trigger());
    harness.wait().set_trigger(dummy_trigger);
    EXPECT_EQ(dummy_trigger, harness.wait().trigger());

    END_TEST;
}

template <typename Harness>
bool wait_begin_test() {
    BEGIN_TEST;

    MockAsync async;

    {
        Harness harness(dummy_handle, dummy_trigger);
        EXPECT_FALSE(harness.wait().is_pending());

        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, harness.wait().Begin(&async));
        EXPECT_TRUE(harness.wait().is_pending());
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op);
        EXPECT_EQ(dummy_handle, async.last_wait->object);
        EXPECT_EQ(dummy_trigger, async.last_wait->trigger);
        EXPECT_FALSE(harness.handler_ran);

        harness.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, harness.wait().Begin(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_WAIT, async.last_op);

    {
        Harness harness(dummy_handle, dummy_trigger);
        EXPECT_FALSE(harness.wait().is_pending());

        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, harness.wait().Begin(&async));
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op);
        EXPECT_FALSE(harness.wait().is_pending());
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op);

    END_TEST;
}

template <typename Harness>
bool wait_cancel_test() {
    BEGIN_TEST;

    MockAsync async;

    {
        Harness harness(dummy_handle, dummy_trigger);
        EXPECT_FALSE(harness.wait().is_pending());

        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.wait().Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.wait().is_pending());

        EXPECT_EQ(ZX_OK, harness.wait().Begin(&async));
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op);
        EXPECT_TRUE(harness.wait().is_pending());

        EXPECT_EQ(ZX_OK, harness.wait().Cancel());
        EXPECT_EQ(MockAsync::Op::CANCEL_WAIT, async.last_op);
        EXPECT_FALSE(harness.wait().is_pending());

        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.wait().Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.wait().is_pending());
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op);

    END_TEST;
}

template <typename Harness>
bool wait_run_handler_test() {
    BEGIN_TEST;

    MockAsync async;

    {
        Harness harness(dummy_handle, dummy_trigger);
        EXPECT_FALSE(harness.wait().is_pending());

        EXPECT_EQ(ZX_OK, harness.wait().Begin(&async));
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op);
        EXPECT_TRUE(harness.wait().is_pending());

        harness.Reset();
        async.last_wait->handler(&async, async.last_wait, ZX_OK, &dummy_signal);
        EXPECT_TRUE(harness.handler_ran);
        EXPECT_EQ(&harness.wait(), harness.last_wait);
        EXPECT_EQ(ZX_OK, harness.last_status);
        EXPECT_EQ(&dummy_signal, harness.last_signal);
        EXPECT_FALSE(harness.wait().is_pending());

        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.wait().Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.wait().is_pending());
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op);

    END_TEST;
}

bool unsupported_begin_wait_test() {
    BEGIN_TEST;

    async::AsyncStub async;
    async_wait_t wait{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_begin_wait(&async, &wait), "valid args");

    END_TEST;
}

bool unsupported_cancel_wait_test() {
    BEGIN_TEST;

    async::AsyncStub async;
    async_wait_t wait{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_cancel_wait(&async, &wait), "valid args");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(wait_tests)
RUN_TEST(wait_set_handler_test)
RUN_TEST((wait_properties_test<LambdaHarness>))
RUN_TEST((wait_properties_test<MethodHarness>))
RUN_TEST((wait_begin_test<LambdaHarness>))
RUN_TEST((wait_begin_test<MethodHarness>))
RUN_TEST((wait_cancel_test<LambdaHarness>))
RUN_TEST((wait_cancel_test<MethodHarness>))
RUN_TEST((wait_run_handler_test<LambdaHarness>))
RUN_TEST((wait_run_handler_test<MethodHarness>))
RUN_TEST(unsupported_begin_wait_test)
RUN_TEST(unsupported_cancel_wait_test)
END_TEST_CASE(wait_tests)

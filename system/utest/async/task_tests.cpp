// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/async_stub.h>
#include <lib/async/cpp/task.h>
#include <unittest/unittest.h>

namespace {

class MockAsync : public async::AsyncStub {
public:
    enum class Op {
        NONE,
        POST_TASK,
        CANCEL_TASK,
    };

    zx::time Now() override { return now; }

    zx_status_t PostTask(async_task_t* task) override {
        last_op = Op::POST_TASK;
        last_task = task;
        return next_status;
    }

    zx_status_t CancelTask(async_task_t* task) override {
        last_op = Op::CANCEL_TASK;
        last_task = task;
        return next_status;
    }

    zx::time now{42};
    Op last_op = Op::NONE;
    async_task_t* last_task = nullptr;
    zx_status_t next_status = ZX_OK;
};

class Harness {
public:
    Harness() { Reset(); }

    void Reset() {
        handler_ran = false;
        last_task = nullptr;
        last_status = ZX_ERR_INTERNAL;
    }

    void Handler(async_t* async, async::TaskBase* task, zx_status_t status) {
        handler_ran = true;
        last_task = task;
        last_status = status;
    }

    void ClosureHandler() {
        handler_ran = true;
        last_task = &task();
        last_status = ZX_OK;
    }

    virtual async::TaskBase& task() = 0;
    virtual bool dispatches_failures() = 0;

    bool handler_ran;
    async::TaskBase* last_task;
    zx_status_t last_status;
};

class LambdaHarness : public Harness {
public:
    async::TaskBase& task() override { return task_; }
    bool dispatches_failures() override { return true; }

private:
    async::Task task_{[this](async_t* async, async::Task* task, zx_status_t status) {
        Handler(async, task, status);
    }};
};

class MethodHarness : public Harness {
public:
    async::TaskBase& task() override { return task_; }
    bool dispatches_failures() override { return true; }

private:
    async::TaskMethod<Harness, &Harness::Handler> task_{this};
};

class ClosureLambdaHarness : public Harness {
public:
    async::TaskBase& task() override { return task_; }
    bool dispatches_failures() override { return false; }

private:
    async::TaskClosure task_{[this] {
        ClosureHandler();
    }};
};

class ClosureMethodHarness : public Harness {
public:
    async::TaskBase& task() override { return task_; }
    bool dispatches_failures() override { return false; }

private:
    async::TaskClosureMethod<Harness, &Harness::ClosureHandler> task_{this};
};

bool task_set_handler_test() {
    BEGIN_TEST;

    {
        async::Task task;
        EXPECT_FALSE(task.has_handler());
        EXPECT_FALSE(task.is_pending());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());

        task.set_handler([](async_t* async, async::Task* task, zx_status_t status) {});
        EXPECT_TRUE(task.has_handler());
    }

    {
        async::Task task([](async_t* async, async::Task* task, zx_status_t status) {});
        EXPECT_TRUE(task.has_handler());
        EXPECT_FALSE(task.is_pending());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());
    }

    END_TEST;
}

bool task_closure_set_handler_test() {
    BEGIN_TEST;

    {
        async::TaskClosure task;
        EXPECT_FALSE(task.has_handler());
        EXPECT_FALSE(task.is_pending());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());

        task.set_handler([] {});
        EXPECT_TRUE(task.has_handler());
    }

    {
        async::TaskClosure task([] {});
        EXPECT_TRUE(task.has_handler());
        EXPECT_FALSE(task.is_pending());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());
    }

    END_TEST;
}

template <typename Harness>
bool task_post_test() {
    BEGIN_TEST;

    MockAsync async;

    {
        Harness harness;
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get(), async.last_task->deadline);
        EXPECT_EQ(async.now.get(), harness.task().last_deadline().get());
        EXPECT_TRUE(harness.task().is_pending());
        EXPECT_FALSE(harness.handler_ran);

        harness.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        Harness harness;
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get(), async.last_task->deadline);
        EXPECT_EQ(async.now.get(), harness.task().last_deadline().get());
        EXPECT_FALSE(harness.task().is_pending());
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

template <typename Harness>
bool task_post_delayed_test() {
    BEGIN_TEST;

    MockAsync async;

    {
        Harness harness;
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, harness.task().PostDelayed(&async, zx::nsec(5)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get() + 5, async.last_task->deadline);
        EXPECT_EQ(async.now.get() + 5, harness.task().last_deadline().get());
        EXPECT_TRUE(harness.task().is_pending());
        EXPECT_FALSE(harness.handler_ran);

        harness.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        Harness harness;
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, harness.task().PostDelayed(&async, zx::nsec(6)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get() + 6, async.last_task->deadline);
        EXPECT_EQ(async.now.get() + 6, harness.task().last_deadline().get());
        EXPECT_FALSE(harness.task().is_pending());
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

template <typename Harness>
bool task_post_for_time_test() {
    BEGIN_TEST;

    MockAsync async;

    {
        Harness harness;
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, harness.task().PostForTime(&async, zx::time(55)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(55, async.last_task->deadline);
        EXPECT_EQ(55, harness.task().last_deadline().get());
        EXPECT_TRUE(harness.task().is_pending());
        EXPECT_FALSE(harness.handler_ran);

        harness.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        Harness harness;
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, harness.task().PostForTime(&async, zx::time(56)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(56, async.last_task->deadline);
        EXPECT_EQ(56, harness.task().last_deadline().get());
        EXPECT_FALSE(harness.task().is_pending());
        EXPECT_FALSE(harness.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

template <typename Harness>
bool task_cancel_test() {
    BEGIN_TEST;

    MockAsync async;

    {
        Harness harness;
        EXPECT_FALSE(harness.task().is_pending());

        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.task().Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.task().is_pending());

        EXPECT_EQ(ZX_OK, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_TRUE(harness.task().is_pending());

        EXPECT_EQ(ZX_OK, harness.task().Cancel());
        EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);
        EXPECT_FALSE(harness.task().is_pending());

        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.task().Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.task().is_pending());
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op);

    END_TEST;
}

template <typename Harness>
bool task_run_handler_test() {
    BEGIN_TEST;

    MockAsync async;

    // success status
    {
        Harness harness;
        EXPECT_FALSE(harness.task().is_pending());

        EXPECT_EQ(ZX_OK, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_TRUE(harness.task().is_pending());

        harness.Reset();
        async.last_task->handler(&async, async.last_task, ZX_OK);
        EXPECT_TRUE(harness.handler_ran);
        EXPECT_EQ(&harness.task(), harness.last_task);
        EXPECT_EQ(ZX_OK, harness.last_status);
        EXPECT_FALSE(harness.task().is_pending());

        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.task().Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.task().is_pending());
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op);

    // failure status
    {
        Harness harness;
        EXPECT_FALSE(harness.task().is_pending());

        EXPECT_EQ(ZX_OK, harness.task().Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_TRUE(harness.task().is_pending());

        harness.Reset();
        async.last_task->handler(&async, async.last_task, ZX_ERR_CANCELED);
        EXPECT_FALSE(harness.task().is_pending());
        if (harness.dispatches_failures()) {
            EXPECT_TRUE(harness.handler_ran);
            EXPECT_EQ(&harness.task(), harness.last_task);
            EXPECT_EQ(ZX_ERR_CANCELED, harness.last_status);
        } else {
            EXPECT_FALSE(harness.handler_ran);
        }

        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.task().Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(harness.task().is_pending());
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op);

    END_TEST;
}

bool unsupported_post_task_test() {
    BEGIN_TEST;

    async::AsyncStub async;
    async_task_t task{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_post_task(&async, &task), "valid args");

    END_TEST;
}

bool unsupported_cancel_task_test() {
    BEGIN_TEST;

    async::AsyncStub async;
    async_task_t task{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_cancel_task(&async, &task), "valid args");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(task_tests)
RUN_TEST(task_set_handler_test)
RUN_TEST(task_closure_set_handler_test)
RUN_TEST((task_post_test<LambdaHarness>))
RUN_TEST((task_post_test<MethodHarness>))
RUN_TEST((task_post_test<ClosureLambdaHarness>))
RUN_TEST((task_post_test<ClosureMethodHarness>))
RUN_TEST((task_post_delayed_test<LambdaHarness>))
RUN_TEST((task_post_delayed_test<MethodHarness>))
RUN_TEST((task_post_delayed_test<ClosureLambdaHarness>))
RUN_TEST((task_post_delayed_test<ClosureMethodHarness>))
RUN_TEST((task_post_for_time_test<LambdaHarness>))
RUN_TEST((task_post_for_time_test<MethodHarness>))
RUN_TEST((task_post_for_time_test<ClosureLambdaHarness>))
RUN_TEST((task_post_for_time_test<ClosureMethodHarness>))
RUN_TEST((task_cancel_test<LambdaHarness>))
RUN_TEST((task_cancel_test<MethodHarness>))
RUN_TEST((task_cancel_test<ClosureLambdaHarness>))
RUN_TEST((task_cancel_test<ClosureMethodHarness>))
RUN_TEST((task_run_handler_test<LambdaHarness>))
RUN_TEST((task_run_handler_test<MethodHarness>))
RUN_TEST((task_run_handler_test<ClosureLambdaHarness>))
RUN_TEST((task_run_handler_test<ClosureMethodHarness>))
RUN_TEST(unsupported_post_task_test)
RUN_TEST(unsupported_cancel_task_test)
END_TEST_CASE(task_tests)

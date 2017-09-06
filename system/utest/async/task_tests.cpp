// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/auto_task.h>
#include <async/task.h>

#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

class MockAsync : public AsyncStub {
public:
    enum class Op {
        NONE,
        POST_TASK,
        CANCEL_TASK,
    };

    Op last_op = Op::NONE;
    async_task_t* last_task = nullptr;

    mx_status_t PostTask(async_task_t* task) override {
        last_op = Op::POST_TASK;
        last_task = task;
        return MX_OK;
    }

    mx_status_t CancelTask(async_task_t* task) override {
        last_op = Op::CANCEL_TASK;
        last_task = task;
        return MX_OK;
    }
};

template <typename TTask>
struct Handler {
    Handler(TTask* task, async_task_result_t result = ASYNC_TASK_REPEAT)
        : result(result) {
        task->set_handler([this](async_t* async, mx_status_t status) {
            handler_ran = true;
            last_status = status;
            return this->result;
        });
    }

    async_task_result_t result;
    bool handler_ran = false;
    mx_status_t last_status = MX_ERR_INTERNAL;
};

bool task_test() {
    const mx_time_t dummy_deadline = 1;
    const uint32_t dummy_flags = ASYNC_FLAG_HANDLE_SHUTDOWN;

    BEGIN_TEST;

    {
        async::Task default_task;
        EXPECT_EQ(MX_TIME_INFINITE, default_task.deadline(), "default deadline");
        EXPECT_EQ(0u, default_task.flags(), "default flags");

        default_task.set_deadline(dummy_deadline);
        EXPECT_EQ(dummy_deadline, default_task.deadline(), "set deadline");
        default_task.set_flags(dummy_flags);
        EXPECT_EQ(dummy_flags, default_task.flags(), "set flags");

        EXPECT_FALSE(!!default_task.handler(), "handler");
    }

    {
        async::Task explicit_task(dummy_deadline, dummy_flags);
        EXPECT_EQ(dummy_deadline, explicit_task.deadline(), "explicit deadline");
        EXPECT_EQ(dummy_flags, explicit_task.flags(), "explicit flags");

        // begin a repeating task
        EXPECT_FALSE(!!explicit_task.handler(), "handler");
        Handler<async::Task> handler(&explicit_task, ASYNC_TASK_REPEAT);
        EXPECT_TRUE(!!explicit_task.handler(), "handler");

        MockAsync async;
        EXPECT_EQ(MX_OK, explicit_task.Post(&async), "post, valid args");
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op, "op");
        EXPECT_EQ(dummy_deadline, async.last_task->deadline, "deadline");
        EXPECT_EQ(dummy_flags, async.last_task->flags, "flags");

        EXPECT_EQ(ASYNC_TASK_REPEAT,
                  async.last_task->handler(&async, async.last_task, MX_OK),
                  "invoke handler");
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");

        // cancel the task
        EXPECT_EQ(MX_OK, explicit_task.Cancel(&async), "cancel, valid args");
        EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op, "op");
    }

    END_TEST;
}

bool auto_task_test() {
    const mx_time_t dummy_deadline = 1;
    const uint32_t dummy_flags = ASYNC_FLAG_HANDLE_SHUTDOWN;

    BEGIN_TEST;

    MockAsync async;
    {
        async::AutoTask default_task(&async);
        EXPECT_EQ(&async, default_task.async());
        EXPECT_FALSE(default_task.is_pending());
        EXPECT_EQ(MX_TIME_INFINITE, default_task.deadline(), "default deadline");
        EXPECT_EQ(0u, default_task.flags(), "default flags");

        default_task.set_deadline(dummy_deadline);
        EXPECT_EQ(dummy_deadline, default_task.deadline(), "set deadline");
        default_task.set_flags(dummy_flags);
        EXPECT_EQ(dummy_flags, default_task.flags(), "set flags");

        EXPECT_FALSE(!!default_task.handler(), "handler");
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op, "op");

    {
        async::AutoTask explicit_task(&async, dummy_deadline, dummy_flags);
        EXPECT_EQ(&async, explicit_task.async());
        EXPECT_FALSE(explicit_task.is_pending());
        EXPECT_EQ(dummy_deadline, explicit_task.deadline(), "explicit deadline");
        EXPECT_EQ(dummy_flags, explicit_task.flags(), "explicit flags");

        // post a non-repeating task
        EXPECT_FALSE(!!explicit_task.handler(), "handler");
        Handler<async::AutoTask> handler(&explicit_task, ASYNC_TASK_FINISHED);
        EXPECT_TRUE(!!explicit_task.handler(), "handler");

        EXPECT_EQ(MX_OK, explicit_task.Post(), "post, valid args");
        EXPECT_TRUE(explicit_task.is_pending());
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op, "op");
        EXPECT_EQ(dummy_deadline, async.last_task->deadline, "deadline");
        EXPECT_EQ(dummy_flags, async.last_task->flags, "flags");

        EXPECT_EQ(ASYNC_TASK_FINISHED,
                  async.last_task->handler(&async, async.last_task, MX_OK),
                  "invoke handler");
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");

        // post a repeating task
        handler.result = ASYNC_TASK_REPEAT;

        EXPECT_EQ(MX_OK, explicit_task.Post(), "post, valid args");
        EXPECT_TRUE(explicit_task.is_pending());
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op, "op");
        EXPECT_EQ(dummy_deadline, async.last_task->deadline, "deadline");
        EXPECT_EQ(dummy_flags, async.last_task->flags, "flags");

        EXPECT_EQ(ASYNC_TASK_REPEAT,
                  async.last_task->handler(&async, async.last_task, MX_OK),
                  "invoke handler");
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");

        // cancel the task
        explicit_task.Cancel();
        EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op, "op");
        EXPECT_FALSE(explicit_task.is_pending());

        // post the task again then let it go out of scope
        EXPECT_EQ(MX_OK, explicit_task.Post(), "post, valid args");
        EXPECT_TRUE(explicit_task.is_pending());
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op, "op");
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op, "op");

    END_TEST;
}

bool unsupported_post_task_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_task_t task{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_post_task(&async, &task), "valid args");

    END_TEST;
}

bool unsupported_cancel_task_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_task_t task{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_cancel_task(&async, &task), "valid args");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(task_tests)
RUN_TEST(task_test)
RUN_TEST(auto_task_test)
RUN_TEST(unsupported_post_task_test)
RUN_TEST(unsupported_cancel_task_test)
END_TEST_CASE(task_tests)

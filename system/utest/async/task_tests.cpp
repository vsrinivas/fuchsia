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

    zx::time now{42};
    Op last_op = Op::NONE;
    async_task_t* last_task = nullptr;
    zx_status_t next_status = ZX_OK;

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
};

template <typename TTask>
struct Handler {
    bool handler_ran;
    TTask* last_task;
    zx_status_t last_status;

    Handler() { Reset(); }

    void Reset() {
        handler_ran = false;
        last_task = nullptr;
        last_status = ZX_ERR_INTERNAL;
    }

    typename TTask::Handler MakeCallback() {
        return [this](async_t* async, TTask* task, zx_status_t status) {
            handler_ran = true;
            last_task = task;
            last_status = status;
        };
    }
};

bool task_constructors() {
    BEGIN_TEST;

    Handler<async::Task> handler;

    {
        async::Task task;
        EXPECT_FALSE(task.has_handler());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());

        task.set_handler(handler.MakeCallback());
        EXPECT_TRUE(task.has_handler());
    }

    {
        async::Task task(handler.MakeCallback());
        EXPECT_TRUE(task.has_handler());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());
    }

    END_TEST;
}

bool task_post_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, task.Post(&async));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get(), async.last_task->deadline);
    EXPECT_EQ(async.now.get(), task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, task.Post(&async));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get(), async.last_task->deadline);
    EXPECT_EQ(async.now.get(), task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    END_TEST;
}

bool task_post_or_report_error_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, task.PostOrReportError(&async));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get(), async.last_task->deadline);
    EXPECT_EQ(async.now.get(), task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostOrReportError(&async));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get(), async.last_task->deadline);
    EXPECT_EQ(async.now.get(), task.last_deadline().get());
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&task, handler.last_task);
    EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);

    END_TEST;
}

bool task_post_delayed_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, task.PostDelayed(&async, zx::nsec(5)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get() + 5, async.last_task->deadline);
    EXPECT_EQ(async.now.get() + 5, task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostDelayed(&async, zx::nsec(6)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get() + 6, async.last_task->deadline);
    EXPECT_EQ(async.now.get() + 6, task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    END_TEST;
}

bool task_post_delayed_or_report_error_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, task.PostDelayedOrReportError(&async, zx::nsec(7)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get() + 7, async.last_task->deadline);
    EXPECT_EQ(async.now.get() + 7, task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostDelayedOrReportError(&async, zx::nsec(8)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(async.now.get() + 8, async.last_task->deadline);
    EXPECT_EQ(async.now.get() + 8, task.last_deadline().get());
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&task, handler.last_task);
    EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);

    END_TEST;
}

bool task_post_for_time_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, task.PostForTime(&async, zx::time(55)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(55, async.last_task->deadline);
    EXPECT_EQ(55, task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostForTime(&async, zx::time(56)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(56, async.last_task->deadline);
    EXPECT_EQ(56, task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    END_TEST;
}

bool task_post_for_time_or_report_error_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, task.PostForTimeOrReportError(&async, zx::time(57)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(57, async.last_task->deadline);
    EXPECT_EQ(57, task.last_deadline().get());
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostForTimeOrReportError(&async, zx::time(58)));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
    EXPECT_EQ(58, async.last_task->deadline);
    EXPECT_EQ(58, task.last_deadline().get());
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&task, handler.last_task);
    EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);

    END_TEST;
}

bool task_cancel_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    EXPECT_EQ(ZX_OK, task.Post(&async));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    EXPECT_EQ(ZX_OK, task.Cancel(&async));
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    END_TEST;
}

bool task_run_handler_test() {
    BEGIN_TEST;

    Handler<async::Task> handler;
    MockAsync async;
    async::Task task(handler.MakeCallback());

    EXPECT_EQ(ZX_OK, task.Post(&async));
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    handler.Reset();
    async.last_task->handler(&async, async.last_task, ZX_OK);
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&task, handler.last_task);
    EXPECT_EQ(ZX_OK, handler.last_status);

    END_TEST;
}

bool auto_task_constructors() {
    BEGIN_TEST;

    MockAsync async;
    Handler<async::AutoTask> handler;

    {
        async::AutoTask task;
        EXPECT_FALSE(task.has_handler());
        EXPECT_FALSE(task.is_pending());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());

        task.set_handler(handler.MakeCallback());
        EXPECT_TRUE(task.has_handler());
    }

    {
        async::AutoTask task(handler.MakeCallback());
        EXPECT_TRUE(task.has_handler());
        EXPECT_FALSE(task.is_pending());
        EXPECT_EQ(zx::time::infinite().get(), task.last_deadline().get());
    }

    END_TEST;
}

bool auto_task_post_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get(), async.last_task->deadline);
        EXPECT_EQ(async.now.get(), task.last_deadline().get());
        EXPECT_TRUE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);

        handler.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get(), async.last_task->deadline);
        EXPECT_EQ(async.now.get(), task.last_deadline().get());
        EXPECT_FALSE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

bool auto_task_post_or_report_error_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, task.PostOrReportError(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get(), async.last_task->deadline);
        EXPECT_EQ(async.now.get(), task.last_deadline().get());
        EXPECT_TRUE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);

        handler.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostOrReportError(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get(), async.last_task->deadline);
        EXPECT_EQ(async.now.get(), task.last_deadline().get());
        EXPECT_FALSE(task.is_pending());
        EXPECT_TRUE(handler.handler_ran);
        EXPECT_EQ(&task, handler.last_task);
        EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

bool auto_task_post_delayed_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, task.PostDelayed(&async, zx::nsec(5)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get() + 5, async.last_task->deadline);
        EXPECT_EQ(async.now.get() + 5, task.last_deadline().get());
        EXPECT_TRUE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);

        handler.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostDelayed(&async, zx::nsec(6)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get() + 6, async.last_task->deadline);
        EXPECT_EQ(async.now.get() + 6, task.last_deadline().get());
        EXPECT_FALSE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

bool auto_task_post_delayed_or_report_error_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, task.PostDelayedOrReportError(&async, zx::nsec(7)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get() + 7, async.last_task->deadline);
        EXPECT_EQ(async.now.get() + 7, task.last_deadline().get());
        EXPECT_TRUE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);

        handler.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostDelayedOrReportError(&async, zx::nsec(8)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(async.now.get() + 8, async.last_task->deadline);
        EXPECT_EQ(async.now.get() + 8, task.last_deadline().get());
        EXPECT_FALSE(task.is_pending());
        EXPECT_TRUE(handler.handler_ran);
        EXPECT_EQ(&task, handler.last_task);
        EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

bool auto_task_post_for_time_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, task.PostForTime(&async, zx::time(55)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(55, async.last_task->deadline);
        EXPECT_EQ(55, task.last_deadline().get());
        EXPECT_TRUE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);

        handler.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostForTime(&async, zx::time(56)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(56, async.last_task->deadline);
        EXPECT_EQ(56, task.last_deadline().get());
        EXPECT_FALSE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

bool auto_task_post_for_time_or_report_error_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_OK;
        EXPECT_EQ(ZX_OK, task.PostForTimeOrReportError(&async, zx::time(57)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(57, async.last_task->deadline);
        EXPECT_EQ(57, task.last_deadline().get());
        EXPECT_TRUE(task.is_pending());
        EXPECT_FALSE(handler.handler_ran);

        handler.Reset();
        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(handler.handler_ran);
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);

    {
        async::AutoTask task(handler.MakeCallback());

        handler.Reset();
        async.next_status = ZX_ERR_BAD_STATE;
        EXPECT_EQ(ZX_ERR_BAD_STATE, task.PostForTimeOrReportError(&async, zx::time(58)));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_EQ(58, async.last_task->deadline);
        EXPECT_EQ(58, task.last_deadline().get());
        EXPECT_FALSE(task.is_pending());
        EXPECT_TRUE(handler.handler_ran);
        EXPECT_EQ(&task, handler.last_task);
        EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);
    }
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);

    END_TEST;
}

bool auto_task_cancel_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());
        EXPECT_FALSE(task.is_pending());

        EXPECT_EQ(ZX_ERR_NOT_FOUND, task.Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(task.is_pending());

        EXPECT_EQ(ZX_OK, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_TRUE(task.is_pending());

        EXPECT_EQ(ZX_OK, task.Cancel());
        EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op);
        EXPECT_FALSE(task.is_pending());

        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, task.Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(task.is_pending());
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op);

    END_TEST;
}

bool auto_task_run_handler_test() {
    BEGIN_TEST;

    Handler<async::AutoTask> handler;
    MockAsync async;

    {
        async::AutoTask task(handler.MakeCallback());
        EXPECT_FALSE(task.is_pending());

        EXPECT_EQ(ZX_OK, task.Post(&async));
        EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op);
        EXPECT_TRUE(task.is_pending());

        handler.Reset();
        async.last_task->handler(&async, async.last_task, ZX_OK);
        EXPECT_TRUE(handler.handler_ran);
        EXPECT_EQ(&task, handler.last_task);
        EXPECT_EQ(ZX_OK, handler.last_status);
        EXPECT_FALSE(task.is_pending());

        async.last_op = MockAsync::Op::NONE;
        EXPECT_EQ(ZX_ERR_NOT_FOUND, task.Cancel());
        EXPECT_EQ(MockAsync::Op::NONE, async.last_op);
        EXPECT_FALSE(task.is_pending());
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
RUN_TEST(task_constructors)
RUN_TEST(task_post_test)
RUN_TEST(task_post_or_report_error_test)
RUN_TEST(task_post_delayed_test)
RUN_TEST(task_post_delayed_or_report_error_test)
RUN_TEST(task_post_for_time_test)
RUN_TEST(task_post_for_time_or_report_error_test)
RUN_TEST(task_cancel_test)
RUN_TEST(task_run_handler_test)
RUN_TEST(auto_task_constructors)
RUN_TEST(auto_task_post_test)
RUN_TEST(auto_task_post_or_report_error_test)
RUN_TEST(auto_task_post_delayed_test)
RUN_TEST(auto_task_post_delayed_or_report_error_test)
RUN_TEST(auto_task_post_for_time_test)
RUN_TEST(auto_task_post_for_time_or_report_error_test)
RUN_TEST(auto_task_cancel_test)
RUN_TEST(auto_task_run_handler_test)
RUN_TEST(unsupported_post_task_test)
RUN_TEST(unsupported_cancel_task_test)
END_TEST_CASE(task_tests)

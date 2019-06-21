// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <zxtest/zxtest.h>
#include "task.h"

namespace {

// A task that counts how many times Run() and DependencyFailed() are called.
class CountingTask : public devmgr::Task {
public:
    CountingTask() : Task(async_get_default_dispatcher()) {}
    ~CountingTask() override = default;

    size_t run_calls() const { return run_calls_; }
    size_t dep_fail_calls() const { return dep_fail_calls_; }
protected:
    void Run() override {
        ++run_calls_;
    }

    void DependencyFailed(zx_status_t status) override {
        ++dep_fail_calls_;
    }
private:
    size_t run_calls_ = 0;
    size_t dep_fail_calls_ = 0;
};

// A task with no dependencies
class NoDepsTask : public CountingTask {
public:
    static fbl::RefPtr<NoDepsTask> Create(zx_status_t status) {
        auto task = fbl::MakeRefCounted<NoDepsTask>();
        task->mock_status_ = status;
        return task;
    }
private:
    void Run() override {
        CountingTask::Run();
        Complete(mock_status_);
    }

    zx_status_t mock_status_;
};

// A task with a variable number of dependencies, each of which have 0
// dependencies.
class DepsTask : public CountingTask {
public:
    static fbl::RefPtr<DepsTask> Create(size_t deps_count, zx_status_t* dep_statuses,
                                        bool fail_on_dep_failure) {
        auto task = fbl::MakeRefCounted<DepsTask>();
        task->fail_on_dep_failure_ = fail_on_dep_failure;
        for (size_t i = 0; i < deps_count; ++i) {
            task->AddDependency(NoDepsTask::Create(dep_statuses[i]));
        }
        return task;
    }
private:
    void Run() override {
        CountingTask::Run();
        Complete(ZX_OK);
    }

    void DependencyFailed(zx_status_t status) override {
        CountingTask::DependencyFailed(status);
        if (fail_on_dep_failure_) {
            Complete(status);
        }
    }

    bool fail_on_dep_failure_;
};

class SequenceTask : public devmgr::Task {
public:
    SequenceTask() : Task(async_get_default_dispatcher()) {}

    struct TaskDesc {
        size_t dependency_count;
        TaskDesc* dependencies;
        bool complete = false;
    };
    static fbl::RefPtr<SequenceTask> Create(TaskDesc* desc) {
        auto task = fbl::MakeRefCounted<SequenceTask>();
        task->desc_ = desc;
        for (size_t i = 0; i < desc->dependency_count; ++i) {
            task->AddDependency(SequenceTask::Create(&desc->dependencies[i]));
        }
        return task;
    }
private:
    void Run() override {
        for (size_t i = 0; i < desc_->dependency_count; ++i) {
            EXPECT_TRUE(desc_->dependencies[i].complete);
        }
        desc_->complete = true;
        Complete(ZX_OK);
    }

    TaskDesc* desc_;
};

class TaskTestCase : public zxtest::Test {
public:
    async::Loop& loop() { return loop_; }
private:
    async::Loop loop_{&kAsyncLoopConfigAttachToThread};
};

TEST_F(TaskTestCase, NoDependenciesDeferred) {
    auto task = NoDepsTask::Create(ZX_OK);
    ASSERT_FALSE(task->is_completed());
    EXPECT_EQ(task->status(), ZX_ERR_UNAVAILABLE);
    loop().RunUntilIdle();
}

TEST_F(TaskTestCase, NoDependenciesSuccess) {
    auto task = NoDepsTask::Create(ZX_OK);
    loop().RunUntilIdle();
    ASSERT_TRUE(task->is_completed());
    EXPECT_OK(task->status());
    EXPECT_EQ(task->run_calls(), 1);
    EXPECT_EQ(task->dep_fail_calls(), 0);
}

TEST_F(TaskTestCase, NoDependenciesFailure) {
    auto task = NoDepsTask::Create(ZX_ERR_NOT_FOUND);
    loop().RunUntilIdle();
    ASSERT_TRUE(task->is_completed());
    EXPECT_EQ(task->status(), ZX_ERR_NOT_FOUND);
    EXPECT_EQ(task->run_calls(), 1);
    EXPECT_EQ(task->dep_fail_calls(), 0);
}

TEST_F(TaskTestCase, SuccessfulDependencies) {
    zx_status_t statuses[] = { ZX_OK, ZX_OK, ZX_OK };
    auto task = DepsTask::Create(fbl::count_of(statuses), statuses, true);
    loop().RunUntilIdle();
    ASSERT_TRUE(task->is_completed());
    EXPECT_EQ(task->status(), ZX_OK);
    EXPECT_EQ(task->run_calls(), 1);
    EXPECT_EQ(task->dep_fail_calls(), 0);
}

TEST_F(TaskTestCase, FailedDependenciesIgnored) {
    zx_status_t statuses[] = { ZX_OK, ZX_ERR_NOT_FOUND, ZX_ERR_INVALID_ARGS };
    auto task = DepsTask::Create(fbl::count_of(statuses), statuses, false);
    loop().RunUntilIdle();
    ASSERT_TRUE(task->is_completed());
    EXPECT_EQ(task->status(), ZX_OK);
    EXPECT_EQ(task->run_calls(), 1);
    EXPECT_EQ(task->dep_fail_calls(), 2);
}

TEST_F(TaskTestCase, FailedDependenciesPropagate) {
    zx_status_t statuses[] = { ZX_OK, ZX_ERR_NOT_FOUND, ZX_ERR_INVALID_ARGS };
    auto task = DepsTask::Create(fbl::count_of(statuses), statuses, true);
    loop().RunUntilIdle();
    ASSERT_TRUE(task->is_completed());
    EXPECT_EQ(task->status(), ZX_ERR_NOT_FOUND);
    EXPECT_EQ(task->run_calls(), 0);
    EXPECT_EQ(task->dep_fail_calls(), 1);
}

TEST_F(TaskTestCase, DependencySequencing) {
    SequenceTask::TaskDesc child1_children[] = {
        { 0, nullptr },
    };
    SequenceTask::TaskDesc root_children[] = {
        { fbl::count_of(child1_children), child1_children },
        { 0, nullptr },
    };

    SequenceTask::TaskDesc root = { fbl::count_of(root_children), root_children };
    auto task = SequenceTask::Create(&root);
    loop().RunUntilIdle();
    ASSERT_TRUE(task->is_completed());
    EXPECT_OK(task->status());
    EXPECT_TRUE(root.complete);
    for (auto& child : root_children) {
        EXPECT_TRUE(child.complete);
    }
    for (auto& child : child1_children) {
        EXPECT_TRUE(child.complete);
    }
}

} // namespace

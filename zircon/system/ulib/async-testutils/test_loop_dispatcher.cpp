// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop_dispatcher.h>

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

namespace async {

// An element in the loop that can be activated. It is either a task or a wait.
class TestLoopDispatcher::Activated {
public:
    virtual ~Activated() {}

    // Dispatch the element, calling its handler.
    virtual void Dispatch() const = 0;
    // Cancel the element, calling its handler with a canceled status.
    virtual void Cancel() const = 0;
    // Returns whether this |Activated| corresponds to the given task or wait.
    virtual bool Matches(void* task_or_wait) const = 0;
    // Returns the due time for this |Activable|. If the |Activable| is a task,
    // this corresponds to its deadline, otherwise this is an infinite time in
    // the future.
    virtual zx::time DueTime() const = 0;
};

class TestLoopDispatcher::TaskActivated : public Activated {
public:
    TaskActivated(async_dispatcher_t* dispatcher, async_task_t* task)
        : dispatcher_(dispatcher), task_(task) {}

    void Dispatch() const override {
        task_->handler(dispatcher_, task_, ZX_OK);
    }

    void Cancel() const override {
        task_->handler(dispatcher_, task_, ZX_ERR_CANCELED);
    }

    bool Matches(void* task_or_wait) const override {
        return task_or_wait == task_;
    }

    zx::time DueTime() const override {
        return zx::time(task_->deadline);
    }

private:
    async_dispatcher_t* const dispatcher_;
    async_task_t* const task_;
};

class TestLoopDispatcher::WaitActivated : public Activated {
public:
    WaitActivated(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_port_packet_t packet)
        : dispatcher_(dispatcher), wait_(wait), packet_(std::move(packet)) {}

    void Dispatch() const override {
        wait_->handler(dispatcher_, wait_, packet_.status, &packet_.signal);
    }

    void Cancel() const override {
        wait_->handler(dispatcher_, wait_, ZX_ERR_CANCELED, nullptr);
    }

    bool Matches(void* task_or_wait) const override { return task_or_wait == wait_; }

    zx::time DueTime() const override {
        return zx::time::infinite();
    }

private:
    async_dispatcher_t* const dispatcher_;
    async_wait_t* const wait_;
    zx_port_packet_t const packet_;
};

TestLoopDispatcher::TestLoopDispatcher(TimeKeeper* time_keeper)
    : time_keeper_(time_keeper) {
    ZX_DEBUG_ASSERT(time_keeper_);
    zx_status_t status = zx::port::create(0u, &port_);
    ZX_ASSERT_MSG(status == ZX_OK,
                  "zx_port_create: %s",
                  zx_status_get_string(status));
}

TestLoopDispatcher::~TestLoopDispatcher() {
    Shutdown();
}

zx::time TestLoopDispatcher::Now() {
    return time_keeper_->Now();
}

zx_status_t TestLoopDispatcher::BeginWait(async_wait_t* wait) {
    ZX_DEBUG_ASSERT(wait);

    if (in_shutdown_) {
        return ZX_ERR_CANCELED;
    }

    zx_status_t status = zx_object_wait_async(wait->object, port_.get(),
                                              reinterpret_cast<uintptr_t>(wait),
                                              wait->trigger,
                                              ZX_WAIT_ASYNC_ONCE);
    if (status != ZX_OK) {
        return status;
    }
    pending_waits_.insert(wait);
    return ZX_OK;
}

zx_status_t TestLoopDispatcher::CancelWait(async_wait_t* wait) {
    ZX_DEBUG_ASSERT(wait);

    auto it = pending_waits_.find(wait);
    if (it != pending_waits_.end()) {
        pending_waits_.erase(it);
        return zx_port_cancel(port_.get(),
                              wait->object, reinterpret_cast<uintptr_t>(wait));
    }

    return CancelActivatedTaskOrWait(wait);
}

zx_status_t TestLoopDispatcher::PostTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);

    if (in_shutdown_) {
        return ZX_ERR_CANCELED;
    }

    if (task->deadline <= Now().get()) {
        ExtractActivated();
        activated_.push_back(std::make_unique<TaskActivated>(this, task));
        return ZX_OK;
    }

    future_tasks_.insert(task);
    return ZX_OK;
}

zx_status_t TestLoopDispatcher::CancelTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);

    auto task_it = std::find(future_tasks_.begin(), future_tasks_.end(), task);
    if (task_it != future_tasks_.end()) {
        future_tasks_.erase(task_it);
        return ZX_OK;
    }

    return CancelActivatedTaskOrWait(task);
}

zx::time TestLoopDispatcher::GetNextTaskDueTime() {
    for (const auto& activated : activated_) {
        if (activated->DueTime() < zx::time::infinite()) {
            return activated->DueTime();
        }
    }
    if (!future_tasks_.empty()) {
        return zx::time((*future_tasks_.begin())->deadline);
    }
    return zx::time::infinite();
}

bool TestLoopDispatcher::HasPendingWork() {
    ExtractActivated();
    return !activated_.empty();
}

bool TestLoopDispatcher::DispatchNextDueMessage() {
    ExtractActivated();
    if (activated_.empty()) {
        return false;
    }

    auto activated_element = std::move(activated_.front());
    activated_.erase(activated_.begin());
    activated_element->Dispatch();
    return true;
}

void TestLoopDispatcher::ExtractActivated() {
    zx_port_packet_t packet;
    while (port_.wait(zx::time(0), &packet) == ZX_OK) {
        async_wait_t* wait = reinterpret_cast<async_wait_t*>(packet.key);
        pending_waits_.erase(wait);
        activated_.push_back(std::make_unique<WaitActivated>(this, wait, std::move(packet)));
    }

    // Move all tasks that reach their deadline to the activated list.
    while (!future_tasks_.empty() && (*future_tasks_.begin())->deadline <= Now().get()) {
        activated_.push_back(std::make_unique<TaskActivated>(this, (*future_tasks_.begin())));
        future_tasks_.erase(future_tasks_.begin());
    }
}

void TestLoopDispatcher::Shutdown() {
    in_shutdown_ = true;

    while (!future_tasks_.empty()) {
        auto task = *future_tasks_.begin();
        future_tasks_.erase(future_tasks_.begin());
        task->handler(this, task, ZX_ERR_CANCELED);
    }
    while (!pending_waits_.empty()) {
        auto wait = *pending_waits_.begin();
        pending_waits_.erase(pending_waits_.begin());
        wait->handler(this, wait, ZX_ERR_CANCELED, nullptr);
    }
    while (!activated_.empty()) {
        auto activated = std::move(activated_.front());
        activated_.erase(activated_.begin());
        activated->Cancel();
    }
}

zx_status_t TestLoopDispatcher::CancelActivatedTaskOrWait(void* task_or_wait) {
    auto activated_it = std::find_if(
        activated_.begin(),
        activated_.end(),
        [&](const auto& activated) { return activated->Matches(task_or_wait); });
    if (activated_it != activated_.end()) {
        activated_.erase(activated_it);
        return ZX_OK;
    }

    return ZX_ERR_NOT_FOUND;
}

} // namespace async

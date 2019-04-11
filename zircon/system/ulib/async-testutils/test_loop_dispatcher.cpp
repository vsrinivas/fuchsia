// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop_dispatcher.h>

#include <fbl/unique_ptr.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#define TO_NODE(type, ptr) ((list_node_t*)&ptr->state)
#define FROM_NODE(type, ptr) ((type*)((char*)(ptr)-offsetof(type, state)))

namespace async {

// An element in the loop that can be activated. It is either a task or a wait.
class TestLoopDispatcher::Activable {
public:
    virtual ~Activable() {}

    // Dispatch the element, calling its handler.
    virtual void Dispatch(zx_port_packet_t* packet) const = 0;
    // Cancel the element, calling its handler with a canceled status.
    virtual void Cancel() const = 0;
    // Enqueue the element in a port, to check if is is activated.
    virtual void Enqueue(zx::port* port) const = 0;
    // Returns whether this |Activable| corresponds to the given task or wait.
    virtual bool Matches(void* task_or_wait) const = 0;
    // Returns the due time for this |Activable|. If the |Activable| is a task,
    // this corresponds to its deadline, otherwise this is an infinite time in
    // the future.
    virtual zx::time DueTime() const = 0;
};

class TestLoopDispatcher::TaskActivable : public Activable {
public:
    TaskActivable(async_dispatcher_t* dispatcher, async_task_t* task)
        : dispatcher_(dispatcher), task_(task) {}

    void Dispatch(zx_port_packet_t* packet) const override {
        task_->handler(dispatcher_, task_, packet->status);
    }

    void Cancel() const override {
        task_->handler(dispatcher_, task_, ZX_ERR_CANCELED);
    }

    void Enqueue(zx::port* port) const override {
        zx_port_packet_t timer_packet{};
        timer_packet.type = ZX_PKT_TYPE_USER;
        zx_status_t status = port->queue(&timer_packet);
        ZX_ASSERT_MSG(status == ZX_OK,
                      "zx_port_queue: %s",
                      zx_status_get_string(status));
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

class TestLoopDispatcher::WaitActivable : public Activable {
public:
    WaitActivable(async_dispatcher_t* dispatcher, async_wait_t* wait)
        : dispatcher_(dispatcher), wait_(wait) {}

    void Dispatch(zx_port_packet_t* packet) const override {
        wait_->handler(dispatcher_, wait_, packet->status, &packet->signal);
    }

    void Cancel() const override {
        wait_->handler(dispatcher_, wait_, ZX_ERR_CANCELED, nullptr);
    }

    void Enqueue(zx::port* port) const override {
        zx_status_t status = zx_object_wait_async(wait_->object, port->get(),
                                                  0,
                                                  wait_->trigger,
                                                  ZX_WAIT_ASYNC_ONCE);
        ZX_ASSERT_MSG(status == ZX_OK,
                      "zx_object_wait_async: %s",
                      zx_status_get_string(status));
    }

    bool Matches(void* task_or_wait) const override { return task_or_wait == wait_; }

    zx::time DueTime() const override {
        return zx::time::infinite();
    }

private:
    async_dispatcher_t* const dispatcher_;
    async_wait_t* const wait_;
};

TestLoopDispatcher::TestLoopDispatcher(TimeKeeper* time_keeper)
    : time_keeper_(time_keeper) {
    ZX_DEBUG_ASSERT(time_keeper_);
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

    activables_.push_back(std::make_unique<WaitActivable>(this, wait));
    return ZX_OK;
}

zx_status_t TestLoopDispatcher::CancelWait(async_wait_t* wait) {
    ZX_DEBUG_ASSERT(wait);

    return CancelTaskOrWait(wait);
}

zx_status_t TestLoopDispatcher::PostTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);

    if (in_shutdown_) {
      return ZX_ERR_CANCELED;
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

    return CancelTaskOrWait(task);
}

zx::time TestLoopDispatcher::GetNextTaskDueTime() {
    for (const auto& [activable, _] : activated_) {
        if (activable->DueTime() < zx::time::infinite()) {
            return activable->DueTime();
        }
    }
    for (const auto& activable : activables_) {
        if (activable->DueTime() < zx::time::infinite()) {
            return activable->DueTime();
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
    activated_element.first->Dispatch(&activated_element.second);
    return true;
}

void TestLoopDispatcher::ExtractActivated() {
    if (!activated_.empty()) {
        return;
    }

    // Move all tasks that reach their deadline to the activable list.
    while (!future_tasks_.empty() && (*future_tasks_.begin())->deadline <= Now().get()) {
        activables_.push_back(std::make_unique<TaskActivable>(this, (*future_tasks_.begin())));
        future_tasks_.erase(future_tasks_.begin());
    }

    for (auto activable_iterator = activables_.begin(); activable_iterator != activables_.end();) {
        zx::port port;
        zx_status_t status = zx::port::create(0u, &port);
        ZX_ASSERT_MSG(status == ZX_OK,
                      "zx_port_create: %s",
                      zx_status_get_string(status));
        (*activable_iterator)->Enqueue(&port);
        zx_port_packet_t packet;
        if (port.wait(zx::time(0), &packet) == ZX_OK) {
            activated_.emplace_back(std::move(*activable_iterator), packet);
            activable_iterator = activables_.erase(activable_iterator);
        } else {
            ++activable_iterator;
        }
    }
}

void TestLoopDispatcher::Shutdown() {
    in_shutdown_ = true;

    while (!future_tasks_.empty()) {
        auto task = *future_tasks_.begin();
        future_tasks_.erase(future_tasks_.begin());
        task->handler(this, task, ZX_ERR_CANCELED);
    }
    while (!activables_.empty()) {
        auto activable = std::move(activables_.front());
        activables_.erase(activables_.begin());
        activable->Cancel();
    }
    while (!activated_.empty()) {
        auto activated = std::move(activated_.front());
        activated_.erase(activated_.begin());
        activated.first->Cancel();
    }
}

zx_status_t TestLoopDispatcher::CancelTaskOrWait(void* task_or_wait) {
    auto activable_it = FindActivable(task_or_wait);
    if (activable_it != activables_.end()) {
        activables_.erase(activable_it);
        return ZX_OK;
    }

    auto activated_it =
        std::find_if(
            activated_.begin(),
            activated_.end(),
            [&](const auto& activated) { return activated.first->Matches(task_or_wait); });
    if (activated_it != activated_.end()) {
        activated_.erase(activated_it);
        return ZX_OK;
    }

    return ZX_ERR_NOT_FOUND;
}

TestLoopDispatcher::ActivableList::iterator TestLoopDispatcher::FindActivable(void* task_or_wait) {
    return std::find_if(
        activables_.begin(),
        activables_.end(),
        [&](const auto& activable) { return activable->Matches(task_or_wait); });
}

} // namespace async

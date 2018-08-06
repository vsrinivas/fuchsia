// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop_dispatcher.h>

#include <zircon/assert.h>
#include <zircon/syscalls.h>

namespace async {

namespace {

// The packet key used to signal timer expirations.
constexpr uint64_t kTimerExpirationKey = 0u;

// Struct to store list node state.
template <typename T>
struct Node {
  T* prev_;
  T* next_;
};

} // namespace


template <typename T>
struct TestLoopDispatcher::ListTraits {
    static Node<T>& node_state(T& obj) {
        static_assert(sizeof(Node<T>) <= sizeof(async_state_t), "");
        auto node = reinterpret_cast<Node<T>*>(&obj.state);
        return *node;
    }
};

template <typename T>
bool TestLoopDispatcher::DeleteFromList(T* obj, TestLoopDispatcher::List<T>* list) {
    auto iter = list->find_if([obj] (const T& element) {
        return obj == &element;
    });
    if (iter.IsValid()) {
        list->erase(iter);
        return true;
    }
    return false;
}

void TestLoopDispatcher::InsertTask(async_task_t* task,
                                    TestLoopDispatcher::List<async_task_t>* list) {
    auto iter = list->begin();
    for (; iter.IsValid(); ++iter) {
        if (iter->deadline > task->deadline) { break; }
    }
    if (iter.IsValid()) {
        list->insert(iter, task);
    } else {
        list->push_back(task);
    }
}

TestLoopDispatcher::TestLoopDispatcher(TimeKeeper* time_keeper)
    : time_keeper_(time_keeper) {
    ZX_DEBUG_ASSERT(time_keeper_);
    zx_status_t status = zx::port::create(0u, &port_);
    ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
}

TestLoopDispatcher::~TestLoopDispatcher() {
    Shutdown();
    time_keeper_->CancelTimers(this);
};

zx::time TestLoopDispatcher::Now() { return time_keeper_->Now(); }

// TODO(ZX-2390): Return ZX_ERR_CANCELED if dispatcher is shutting down.
zx_status_t TestLoopDispatcher::BeginWait(async_wait_t* wait) {
    ZX_DEBUG_ASSERT(wait);

    // Along with the above assertion, the following check guarantees that the
    // packet to be sent to |port_| on completion of this wait will not be
    // mistaken for a timer expiration.
    static_assert(0u == kTimerExpirationKey,
                  "Timer expirations must be signaled with a packet key of 0");

    wait_list_.push_front(wait);
    zx_status_t status = zx_object_wait_async(wait->object, port_.get(),
                                              reinterpret_cast<uintptr_t>(wait),
                                              wait->trigger,
                                              ZX_WAIT_ASYNC_ONCE);

    if (status != ZX_OK) {
        // In this rare condition, the wait failed. Since a dispatched handler will
        // never be invoked on the wait object, we remove it ourselves.
        ZX_DEBUG_ASSERT(DeleteFromList<async_wait_t>(wait, &wait_list_));
    }
    return status;
}

zx_status_t TestLoopDispatcher::CancelWait(async_wait_t* wait) {
    ZX_DEBUG_ASSERT(wait);

    if (!DeleteFromList<async_wait_t>(wait, &wait_list_)) {
        return ZX_ERR_NOT_FOUND;
    }

    // |wait| already might be encoded in |due_packet_|.
    if (due_packet_ && due_packet_->key != kTimerExpirationKey) {
        if (wait == reinterpret_cast<async_wait_t*>(due_packet_->key)) {
            due_packet_.reset();
            return ZX_OK;
        }
    }

    return port_.cancel(*zx::unowned_handle(wait->object),
                        reinterpret_cast<uintptr_t>(wait));
}

// TODO(ZX-2390): Return ZX_ERR_CANCELED if dispatcher is shutting down.
zx_status_t TestLoopDispatcher::PostTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);

    InsertTask(task, &task_list_);
    if (task == &task_list_.front()) {
        time_keeper_->RegisterTimer(GetNextTaskDueTime(), this);
    }
    return ZX_OK;
}

zx_status_t TestLoopDispatcher::CancelTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);
    if (DeleteFromList<async_task_t>(task, &task_list_) ||
        DeleteFromList<async_task_t>(task, &due_list_)) {
        return ZX_OK;
    }
    return ZX_ERR_NOT_FOUND;
}

void TestLoopDispatcher::FireTimer() {
    zx_port_packet_t timer_packet{};
    timer_packet.key = kTimerExpirationKey;
    timer_packet.type = ZX_PKT_TYPE_USER;
    ZX_DEBUG_ASSERT(ZX_OK == port_.queue(&timer_packet));
}

zx::time TestLoopDispatcher::GetNextTaskDueTime() {
    if (due_list_.is_empty()) {
          if (task_list_.is_empty()) {
              return zx::time::infinite();
          }
          return zx::time(task_list_.front().deadline);
    }
    return zx::time(due_list_.front().deadline);
}


void TestLoopDispatcher::ExtractNextDuePacket() {
    ZX_DEBUG_ASSERT(!due_packet_);
    bool tasks_are_due = GetNextTaskDueTime() <= Now();

    // If no tasks are due, flush all timer expiration packets until either
    // there are no more packets to dequeue or a wait packet is reached.
    do {
        auto packet = fbl::make_unique<zx_port_packet_t>();
        if (ZX_OK != port_.wait(zx::time(0), packet.get())) { return; }
        due_packet_.swap(packet);
    } while (!tasks_are_due && due_packet_->key == kTimerExpirationKey);
}

bool TestLoopDispatcher::HasPendingWork() {
    if (GetNextTaskDueTime() <= Now()) { return true; }
    if (!due_packet_) { ExtractNextDuePacket(); }
    return !!due_packet_;
}

void TestLoopDispatcher::DispatchNextDueTask() {
    if (due_list_.is_empty()) { return; }

    async_task_t* task = due_list_.pop_front();
    task->handler(this, task, ZX_OK);

    // If the due list is now empty and there are still pending tasks,
    // register a timer for the next due time.
    if (due_list_.is_empty() && !task_list_.is_empty()) {
        time_keeper_->RegisterTimer(GetNextTaskDueTime(), this);
    }
}

bool TestLoopDispatcher::DispatchNextDueMessage() {
    if (!due_list_.is_empty()) {
        DispatchNextDueTask();
        return true;
    }

    if (!due_packet_) { ExtractNextDuePacket(); }

    if (!due_packet_) {
        return false;
    } else if (due_packet_->key == kTimerExpirationKey) {
        ExtractDueTasks();
        DispatchNextDueTask();
        due_packet_.reset();
    } else {  // |due_packet_| encodes a finished wait.
        // Move the next due packet to the stack, as invoking the associated
        // wait's handler might try to extract another.
        zx_port_packet_t packet = *due_packet_;
        due_packet_.reset();
        async_wait_t* wait = reinterpret_cast<async_wait_t*>(packet.key);
        ZX_DEBUG_ASSERT(DeleteFromList<async_wait_t>(wait, &wait_list_));
        wait->handler(this, wait, ZX_OK, &packet.signal);
    }
    return true;
}

void TestLoopDispatcher::ExtractDueTasks() {
    zx::time current_time = time_keeper_->Now();
    while (!task_list_.is_empty() &&
           task_list_.front().deadline <= current_time.get()) {
        InsertTask(task_list_.pop_front(), &due_list_);
    }
}

void TestLoopDispatcher::Shutdown() {
    while (!wait_list_.is_empty()) {
        async_wait_t* wait = wait_list_.pop_front();
        wait->handler(this, wait, ZX_ERR_CANCELED, nullptr);
    }
    while (!due_list_.is_empty()) {
        async_task_t* task = due_list_.pop_front();
        task->handler(this, task, ZX_ERR_CANCELED);
    }
    while (!task_list_.is_empty()) {
        async_task_t* task = task_list_.pop_front();
        task->handler(this, task, ZX_ERR_CANCELED);
    }
}

} // namespace async

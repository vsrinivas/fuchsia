// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop_dispatcher.h>

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#define TO_NODE(type, ptr) ((list_node_t*)&ptr->state)
#define FROM_NODE(type, ptr) ((type*)((char*)(ptr)-offsetof(type, state)))

namespace async {

namespace {

// The packet key used to signal timer expirations.
constexpr uint64_t kTimerExpirationKey = 0u;

// Convenience functions for task, wait, and list node management.
inline list_node_t* WaitToNode(async_wait_t* wait) {
    return TO_NODE(async_wait_t, wait);
}

inline async_wait_t* NodeToWait(list_node_t* node) {
    return FROM_NODE(async_wait_t, node);
}

inline list_node_t* TaskToNode(async_task_t* task) {
    return TO_NODE(async_task_t, task);
}

inline async_task_t* NodeToTask(list_node_t* node) {
    return FROM_NODE(async_task_t, node);
}

inline void InsertTask(list_node_t* task_list, async_task_t* task) {
    list_node_t* node;
    for (node = task_list->prev; node != task_list; node = node->prev) {
        if (task->deadline >= NodeToTask(node)->deadline) {
            break;
        }
    }
    list_add_after(node, TaskToNode(task));
}
} // namespace

TestLoopDispatcher::TestLoopDispatcher(TimeKeeper* time_keeper)
    : time_keeper_(time_keeper) {
    ZX_DEBUG_ASSERT(time_keeper_);
    list_initialize(&wait_list_);
    list_initialize(&task_list_);
    list_initialize(&due_list_);
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

    list_add_head(&wait_list_, WaitToNode(wait));
    zx_status_t status = zx_object_wait_async(wait->object, port_.get(),
                                              reinterpret_cast<uintptr_t>(wait),
                                              wait->trigger,
                                              ZX_WAIT_ASYNC_ONCE);

    if (status != ZX_OK) {
        // In this rare condition, the wait failed. Since a dispatched handler will
        // never be invoked on the wait object, we remove it ourselves.
        list_delete(WaitToNode(wait));
    }
    return status;
}

zx_status_t TestLoopDispatcher::CancelWait(async_wait_t* wait) {
    ZX_DEBUG_ASSERT(wait);

    list_node_t* node = WaitToNode(wait);
    if (!list_in_list(node)) {
        return ZX_ERR_NOT_FOUND;
    }
    zx_status_t status = port_.cancel(*zx::unowned_handle(wait->object),
                                      reinterpret_cast<uintptr_t>(wait));
    if (status == ZX_OK) {
        list_delete(node);
    }
    return status;
}

// TODO(ZX-2390): Return ZX_ERR_CANCELED if dispatcher is shutting down.
zx_status_t TestLoopDispatcher::PostTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);

    InsertTask(&task_list_, task);
    if (NodeToTask(list_peek_head(&task_list_)) == task) {
        time_keeper_->RegisterTimer(GetNextTaskDueTime(), this);
    }
    return ZX_OK;
}

zx_status_t TestLoopDispatcher::CancelTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);
    list_node_t* node = TaskToNode(task);
    if (!list_in_list(node)) {
        return ZX_ERR_NOT_FOUND;
    }
    list_delete(node);
    return ZX_OK;
}

void TestLoopDispatcher::FireTimer() {
    zx_port_packet_t timer_packet{};
    timer_packet.key = kTimerExpirationKey;
    timer_packet.type = ZX_PKT_TYPE_USER;
    ZX_ASSERT(ZX_OK == port_.queue(&timer_packet));
}

zx::time TestLoopDispatcher::GetNextTaskDueTime() {
    list_node_t* node = list_is_empty(&due_list_) ?
                        list_peek_head(&task_list_) :
                        list_peek_head(&due_list_);
    if (!node) {
        return zx::time::infinite();
    }
    return zx::time(NodeToTask(node)->deadline);
}

bool TestLoopDispatcher::DispatchNextDueTask() {
    // if something is already in the due list, dispatch that.
    list_node_t* node = list_peek_head(&due_list_);
    if (node) {
        list_delete(node);
        async_task_t* task = NodeToTask(node);
        task->handler(this, task, ZX_OK);

        // If the due list is now empty and there are still pending tasks,
        // register a timer for the next due time.
        if (list_is_empty(&due_list_) && !list_is_empty(&task_list_)) {
            time_keeper_->RegisterTimer(GetNextTaskDueTime(), this);
        }
        return true;
    }
    return false;
}

bool TestLoopDispatcher::DispatchNextDueMessage() {
    if (DispatchNextDueTask()) { return true; }

    zx_port_packet_t packet;
    zx_status_t status = port_.wait(zx::time(0), &packet);

    // If the drawn packet is a timer expiration with no due tasks, drain the
    // subsequent timer expirations (an excess may have been signaled)
    while (status == ZX_OK && packet.key == kTimerExpirationKey) {
        ExtractDueTasks();
        if (DispatchNextDueTask()) { return true; }
        status = port_.wait(zx::time(0), &packet);
    }

    if (status != ZX_OK) {
        return false;
    } else {  // |packet| encodes a finished wait.
        async_wait_t* wait = reinterpret_cast<async_wait_t*>(packet.key);
        list_delete(WaitToNode(wait));
        wait->handler(this, wait, ZX_OK, &packet.signal);
        return true;
    }
}

void TestLoopDispatcher::ExtractDueTasks() {
    list_node_t* node;
    list_node_t* tail = nullptr;
    zx::time current_time = time_keeper_->Now();
    list_for_every(&task_list_, node) {
        if (NodeToTask(node)->deadline > current_time.get()) { break; }
        tail = node;
    }
    if (tail) {
        list_node_t* head = task_list_.next;
        task_list_.next = tail->next;
        tail->next->prev = &task_list_;
        due_list_.next = head;
        head->prev = &due_list_;
        due_list_.prev = tail;
        tail->next = &due_list_;
    }
}

void TestLoopDispatcher::Shutdown() {
    list_node_t* node;
    while ((node = list_remove_head(&wait_list_))) {
        async_wait_t* wait = NodeToWait(node);
        wait->handler(this, wait, ZX_ERR_CANCELED, nullptr);
    }
    while ((node = list_remove_head(&due_list_))) {
        async_task_t* task = NodeToTask(node);
        task->handler(this, task, ZX_ERR_CANCELED);
    }
    while ((node = list_remove_head(&task_list_))) {
        async_task_t* task = NodeToTask(node);
        task->handler(this, task, ZX_ERR_CANCELED);
    }
}

} // namespace async

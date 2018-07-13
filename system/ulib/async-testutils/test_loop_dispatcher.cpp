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
        if (task->deadline >= NodeToTask(node)->deadline) break;
    }
    list_add_after(node, TaskToNode(task));
}
} // namespace

TestLoopDispatcher::TestLoopDispatcher() {
    list_initialize(&wait_list_);
    list_initialize(&task_list_);
    list_initialize(&due_list_);
    zx_status_t status = zx::port::create(0u, &port_);
    ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
}

TestLoopDispatcher::~TestLoopDispatcher() {
  Shutdown();
};

zx_status_t TestLoopDispatcher::BeginWait(async_wait_t* wait) {
    ZX_DEBUG_ASSERT(wait);
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
    zx_status_t status = port_.cancel(*zx::unowned_handle(wait->object), reinterpret_cast<uintptr_t>(wait));
    if (status == ZX_OK) {
        list_delete(node);
    }
    return status;
}

zx_status_t TestLoopDispatcher::PostTask(async_task_t* task) {
    ZX_DEBUG_ASSERT(task);
    InsertTask(&task_list_, task);
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

void TestLoopDispatcher::AdvanceTimeTo(zx::time time) {
  ZX_ASSERT_MSG(!is_dispatching_, "Cannot advance time while dispatching.");
  if (current_time_ < time) {
    current_time_ = time;
  }
}

bool TestLoopDispatcher::RunUntil(zx::time deadline) {
  ZX_ASSERT(!is_dispatching_);

  bool did_work = false;
  for (;;) {
    bool ran_handler = false;
    is_dispatching_ = true;

    if (has_quit_) break;
    ran_handler |= DispatchPendingTasks();
    did_work |= ran_handler;

    if (has_quit_) break;
    ran_handler |= DispatchPendingWaits();
    did_work |= ran_handler;

    if (ran_handler) continue;
    is_dispatching_ = false;

    zx::time next_due_time = GetNextTaskDueTime();
    if (next_due_time > deadline) {
     AdvanceTimeTo(deadline);
     break;
    }
    AdvanceTimeTo(next_due_time);
  }
  is_dispatching_ = false;
  has_quit_ = false;
  return did_work;
}

zx::time TestLoopDispatcher::GetNextTaskDueTime() {
  list_node_t* node = list_peek_head(&task_list_);
  if (!node) {
    return zx::time::infinite();
  }
  return zx::time(NodeToTask(node)->deadline);
}

bool TestLoopDispatcher::DispatchPendingWaits() {
    // First, enqueue a user packet into |port_| to mark the tail of the waits
    // queued at the time of this method call.
    // Set the key of this packet to |wait_id|, to uniquely identify this packet
    // from others that might be left queued at the port from previous iterations
    // that prematurely broke due to a quit.
    zx_port_packet_t user_packet{};
    user_packet.key = wait_id_;
    user_packet.type = ZX_PKT_TYPE_USER;
    ZX_ASSERT(ZX_OK == port_.queue(&user_packet));

    bool did_work = false;
    for (;;) {
        if (has_quit_) break;

        zx_port_packet_t packet;
        // Grace of the user packet, |port_| should always have a queued wait.
        ZX_ASSERT(ZX_OK == port_.wait(zx::time(0), &packet));
        if (packet.type == ZX_PKT_TYPE_USER) {
          if (packet.key == wait_id_) {
              // The packet is one we queued at the beginning of this call:
              // all pending waits have been dispatched.
              break;
          } else {
              // The packet is a leftover from a previously quit iteration:
              // carry on.
              continue;
          }
        }

        async_wait_t* wait = reinterpret_cast<async_wait_t*>(packet.key);
        list_delete(WaitToNode(wait));
        wait->handler(this, wait, ZX_OK, &packet.signal);
        did_work = true;
    }
    ++wait_id_;
    return did_work;
}

bool TestLoopDispatcher::DispatchPendingTasks() {
  ExtractDueTasks();

  // Dequeue and dispatch due tasks one at a time.
  bool did_work = false;
  list_node_t* node;
  while ((node = list_peek_head(&due_list_))) {
      if (has_quit_) break;
      list_delete(node);
      async_task_t* task = NodeToTask(node);
      task->handler(this, task, ZX_OK);
      did_work = true;
  }
  return did_work;
}

void TestLoopDispatcher::ExtractDueTasks() {
  list_node_t* node;
  list_node_t* tail = NULL;
  list_for_every(&task_list_, node) {
      if (NodeToTask(node)->deadline > current_time_.get()) break;
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
        wait->handler(this, wait, ZX_ERR_CANCELED, NULL);
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

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/loop.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/trap.h>
#include <lib/async/wait.h>

// The port wait key associated with the dispatcher's control messages.
#define KEY_CONTROL (0u)

static zx_time_t async_loop_now(async_t* async);
static zx_status_t async_loop_begin_wait(async_t* async, async_wait_t* wait);
static zx_status_t async_loop_cancel_wait(async_t* async, async_wait_t* wait);
static zx_status_t async_loop_post_task(async_t* async, async_task_t* task);
static zx_status_t async_loop_cancel_task(async_t* async, async_task_t* task);
static zx_status_t async_loop_queue_packet(async_t* async, async_receiver_t* receiver,
                                           const zx_packet_user_t* data);
static zx_status_t async_loop_set_guest_bell_trap(
    async_t* async, async_guest_bell_trap_t* trap,
    zx_handle_t guest, zx_vaddr_t addr, size_t length);

static const async_ops_t async_loop_ops = {
    .version = ASYNC_OPS_V1,
    .reserved = 0,
    .v1 = {
        .now = async_loop_now,
        .begin_wait = async_loop_begin_wait,
        .cancel_wait = async_loop_cancel_wait,
        .post_task = async_loop_post_task,
        .cancel_task = async_loop_cancel_task,
        .queue_packet = async_loop_queue_packet,
        .set_guest_bell_trap = async_loop_set_guest_bell_trap,
    },
};

typedef struct thread_record {
    list_node_t node;
    thrd_t thread;
} thread_record_t;

const async_loop_config_t kAsyncLoopConfigMakeDefault = {
    .make_default_for_current_thread = true};

typedef struct async_loop {
    async_t async; // must be first (the loop inherits from async_t)
    async_loop_config_t config; // immutable
    zx_handle_t port; // immutable
    zx_handle_t timer; // immutable

    _Atomic async_loop_state_t state;
    atomic_uint active_threads; // number of active dispatch threads

    mtx_t lock; // guards the lists and the dispatching tasks flag
    bool dispatching_tasks; // true while the loop is busy dispatching tasks
    list_node_t wait_list; // most recently added first
    list_node_t task_list; // pending tasks, earliest deadline first
    list_node_t due_list; // due tasks, earliest deadline first
    list_node_t thread_list; // earliest created thread first
} async_loop_t;

static zx_status_t async_loop_run_once(async_loop_t* loop, zx_time_t deadline);
static zx_status_t async_loop_dispatch_wait(async_loop_t* loop, async_wait_t* wait,
                                            zx_status_t status, const zx_packet_signal_t* signal);
static zx_status_t async_loop_dispatch_tasks(async_loop_t* loop);
static void async_loop_dispatch_task(async_loop_t* loop, async_task_t* task,
                                     zx_status_t status);
static zx_status_t async_loop_dispatch_packet(async_loop_t* loop, async_receiver_t* receiver,
                                              zx_status_t status, const zx_packet_user_t* data);
static zx_status_t async_loop_dispatch_guest_bell_trap(async_loop_t* loop,
                                                       async_guest_bell_trap_t* trap,
                                                       zx_status_t status,
                                                       const zx_packet_guest_bell_t* bell);
static void async_loop_wake_threads(async_loop_t* loop);
static void async_loop_insert_task_locked(async_loop_t* loop, async_task_t* task);
static void async_loop_restart_timer_locked(async_loop_t* loop);
static void async_loop_invoke_prologue(async_loop_t* loop);
static void async_loop_invoke_epilogue(async_loop_t* loop);

static_assert(sizeof(list_node_t) <= sizeof(async_state_t),
              "async_state_t too small");

#define TO_NODE(type, ptr) ((list_node_t*)&ptr->state)
#define FROM_NODE(type, ptr) ((type*)((char*)(ptr)-offsetof(type, state)))

static inline list_node_t* wait_to_node(async_wait_t* wait) {
    return TO_NODE(async_wait_t, wait);
}

static inline async_wait_t* node_to_wait(list_node_t* node) {
    return FROM_NODE(async_wait_t, node);
}

static inline list_node_t* task_to_node(async_task_t* task) {
    return TO_NODE(async_task_t, task);
}

static inline async_task_t* node_to_task(list_node_t* node) {
    return FROM_NODE(async_task_t, node);
}

zx_status_t async_loop_create(const async_loop_config_t* config, async_loop_t** out_loop) {
    ZX_DEBUG_ASSERT(out_loop);

    async_loop_t* loop = calloc(1u, sizeof(async_loop_t));
    if (!loop)
        return ZX_ERR_NO_MEMORY;
    atomic_init(&loop->state, ASYNC_LOOP_RUNNABLE);
    atomic_init(&loop->active_threads, 0u);

    loop->async.ops = &async_loop_ops;
    if (config)
        loop->config = *config;
    mtx_init(&loop->lock, mtx_plain);
    list_initialize(&loop->wait_list);
    list_initialize(&loop->task_list);
    list_initialize(&loop->due_list);
    list_initialize(&loop->thread_list);

    zx_status_t status = zx_port_create(0u, &loop->port);
    if (status == ZX_OK)
        status = zx_timer_create(0u, ZX_CLOCK_MONOTONIC, &loop->timer);
    if (status == ZX_OK) {
        status = zx_object_wait_async(loop->timer, loop->port, KEY_CONTROL,
                                      ZX_TIMER_SIGNALED,
                                      ZX_WAIT_ASYNC_REPEATING);
    }
    if (status == ZX_OK) {
        *out_loop = loop;
        if (loop->config.make_default_for_current_thread) {
            ZX_DEBUG_ASSERT(async_get_default() == NULL);
            async_set_default(&loop->async);
        }
    } else {
        loop->config.make_default_for_current_thread = false;
        async_loop_destroy(loop);
    }
    return status;
}

void async_loop_destroy(async_loop_t* loop) {
    ZX_DEBUG_ASSERT(loop);

    async_loop_shutdown(loop);

    zx_handle_close(loop->port);
    zx_handle_close(loop->timer);
    mtx_destroy(&loop->lock);
    free(loop);
}

void async_loop_shutdown(async_loop_t* loop) {
    ZX_DEBUG_ASSERT(loop);

    async_loop_state_t prior_state =
        atomic_exchange_explicit(&loop->state, ASYNC_LOOP_SHUTDOWN,
                                 memory_order_acq_rel);
    if (prior_state == ASYNC_LOOP_SHUTDOWN)
        return;

    async_loop_wake_threads(loop);
    async_loop_join_threads(loop);

    list_node_t* node;
    while ((node = list_remove_head(&loop->wait_list))) {
        async_wait_t* wait = node_to_wait(node);
        async_loop_dispatch_wait(loop, wait, ZX_ERR_CANCELED, NULL);
    }
    while ((node = list_remove_head(&loop->due_list))) {
        async_task_t* task = node_to_task(node);
        async_loop_dispatch_task(loop, task, ZX_ERR_CANCELED);
    }
    while ((node = list_remove_head(&loop->task_list))) {
        async_task_t* task = node_to_task(node);
        async_loop_dispatch_task(loop, task, ZX_ERR_CANCELED);
    }

    if (loop->config.make_default_for_current_thread) {
        ZX_DEBUG_ASSERT(async_get_default() == &loop->async);
        async_set_default(NULL);
    }
}

zx_status_t async_loop_run(async_loop_t* loop, zx_time_t deadline, bool once) {
    ZX_DEBUG_ASSERT(loop);

    zx_status_t status;
    atomic_fetch_add_explicit(&loop->active_threads, 1u, memory_order_acq_rel);
    do {
        status = async_loop_run_once(loop, deadline);
    } while (status == ZX_OK && !once);
    atomic_fetch_sub_explicit(&loop->active_threads, 1u, memory_order_acq_rel);
    return status;
}

zx_status_t async_loop_run_until_idle(async_loop_t* loop) {
    zx_status_t status = async_loop_run(loop, 0, false);
    if (status == ZX_ERR_TIMED_OUT) {
        status = ZX_OK;
    }
    return status;
}

static zx_status_t async_loop_run_once(async_loop_t* loop, zx_time_t deadline) {
    async_loop_state_t state = atomic_load_explicit(&loop->state, memory_order_acquire);
    if (state == ASYNC_LOOP_SHUTDOWN)
        return ZX_ERR_BAD_STATE;
    if (state != ASYNC_LOOP_RUNNABLE)
        return ZX_ERR_CANCELED;

    zx_port_packet_t packet;
    zx_status_t status = zx_port_wait(loop->port, deadline, &packet);
    if (status != ZX_OK)
        return status;

    if (packet.key == KEY_CONTROL) {
        // Handle wake-up packets.
        if (packet.type == ZX_PKT_TYPE_USER)
            return ZX_OK;

        // Handle task timer expirations.
        if (packet.type == ZX_PKT_TYPE_SIGNAL_REP &&
            packet.signal.observed & ZX_TIMER_SIGNALED) {
            return async_loop_dispatch_tasks(loop);
        }
    } else {
        // Handle wait completion packets.
        if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE) {
            async_wait_t* wait = (void*)(uintptr_t)packet.key;
            mtx_lock(&loop->lock);
            list_delete(wait_to_node(wait));
            mtx_unlock(&loop->lock);
            return async_loop_dispatch_wait(loop, wait, packet.status, &packet.signal);
        }

        // Handle queued user packets.
        if (packet.type == ZX_PKT_TYPE_USER) {
            async_receiver_t* receiver = (void*)(uintptr_t)packet.key;
            return async_loop_dispatch_packet(loop, receiver, packet.status, &packet.user);
        }

        // Handle guest bell trap packets.
        if (packet.type == ZX_PKT_TYPE_GUEST_BELL) {
            async_guest_bell_trap_t* trap = (void*)(uintptr_t)packet.key;
            return async_loop_dispatch_guest_bell_trap(
                loop, trap, packet.status, &packet.guest_bell);
        }
    }

    ZX_DEBUG_ASSERT(false);
    return ZX_ERR_INTERNAL;
}

static zx_status_t async_loop_dispatch_guest_bell_trap(async_loop_t* loop,
                                                       async_guest_bell_trap_t* trap,
                                                       zx_status_t status,
                                                       const zx_packet_guest_bell_t* bell) {
    async_loop_invoke_prologue(loop);
    trap->handler((async_t*)loop, trap, status, bell);
    async_loop_invoke_epilogue(loop);
    return ZX_OK;
}

static zx_status_t async_loop_dispatch_wait(async_loop_t* loop, async_wait_t* wait,
                                            zx_status_t status, const zx_packet_signal_t* signal) {
    async_loop_invoke_prologue(loop);
    wait->handler((async_t*)loop, wait, status, signal);
    async_loop_invoke_epilogue(loop);
    return ZX_OK;
}

static zx_status_t async_loop_dispatch_tasks(async_loop_t* loop) {
    // Dequeue and dispatch one task at a time in case an earlier task wants
    // to cancel a later task which has also come due.  At most one thread
    // can dispatch tasks at any given moment (to preserve serial ordering).
    // Timer restarts are suppressed until we run out of tasks to dispatch.
    mtx_lock(&loop->lock);
    if (!loop->dispatching_tasks) {
        loop->dispatching_tasks = true;

        // Extract all of the tasks that are due into |due_list| for dispatch
        // unless we already have some waiting from a previous iteration which
        // we would like to process in order.
        list_node_t* node;
        if (list_is_empty(&loop->due_list)) {
            zx_time_t due_time = async_loop_now((async_t*)loop);
            list_node_t* tail = NULL;
            list_for_every(&loop->task_list, node) {
                if (node_to_task(node)->deadline > due_time)
                    break;
                tail = node;
            }
            if (tail) {
                list_node_t* head = loop->task_list.next;
                loop->task_list.next = tail->next;
                tail->next->prev = &loop->task_list;
                loop->due_list.next = head;
                head->prev = &loop->due_list;
                loop->due_list.prev = tail;
                tail->next = &loop->due_list;
            }
        }

        // Dispatch all due tasks.  Note that they might be canceled concurrently
        // so we need to grab the lock during each iteration to fetch the next
        // item from the list.
        while ((node = list_remove_head(&loop->due_list))) {
            mtx_unlock(&loop->lock);

            // Invoke the handler.  Note that it might destroy itself.
            async_task_t* task = node_to_task(node);
            async_loop_dispatch_task(loop, task, ZX_OK);

            mtx_lock(&loop->lock);
            async_loop_state_t state = atomic_load_explicit(&loop->state, memory_order_acquire);
            if (state != ASYNC_LOOP_RUNNABLE)
                break;
        }

        loop->dispatching_tasks = false;
        async_loop_restart_timer_locked(loop);
    }
    mtx_unlock(&loop->lock);
    return ZX_OK;
}

static void async_loop_dispatch_task(async_loop_t* loop,
                                     async_task_t* task,
                                     zx_status_t status) {
    // Invoke the handler.  Note that it might destroy itself.
    async_loop_invoke_prologue(loop);
    task->handler((async_t*)loop, task, status);
    async_loop_invoke_epilogue(loop);
}

static zx_status_t async_loop_dispatch_packet(async_loop_t* loop, async_receiver_t* receiver,
                                              zx_status_t status, const zx_packet_user_t* data) {
    // Invoke the handler.  Note that it might destroy itself.
    async_loop_invoke_prologue(loop);
    receiver->handler((async_t*)loop, receiver, status, data);
    async_loop_invoke_epilogue(loop);
    return ZX_OK;
}

void async_loop_quit(async_loop_t* loop) {
    ZX_DEBUG_ASSERT(loop);

    async_loop_state_t expected_state = ASYNC_LOOP_RUNNABLE;
    if (!atomic_compare_exchange_strong_explicit(&loop->state, &expected_state,
                                                 ASYNC_LOOP_QUIT,
                                                 memory_order_acq_rel, memory_order_acquire))
        return;

    async_loop_wake_threads(loop);
}

static void async_loop_wake_threads(async_loop_t* loop) {
    // Queue enough packets to awaken all active threads.
    // This is safe because any new threads which join the pool first increment the
    // active thread count then check the loop state, so the count we observe here
    // cannot be less than the number of threads which might be blocked in |port_wait|.
    // Issuing too many packets is also harmless.
    uint32_t n = atomic_load_explicit(&loop->active_threads, memory_order_acquire);
    for (uint32_t i = 0u; i < n; i++) {
        zx_port_packet_t packet = {
            .key = KEY_CONTROL,
            .type = ZX_PKT_TYPE_USER,
            .status = ZX_OK};
        zx_status_t status = zx_port_queue(loop->port, &packet);
        ZX_ASSERT_MSG(status == ZX_OK, "zx_port_queue: status=%d", status);
    }
}

zx_status_t async_loop_reset_quit(async_loop_t* loop) {
    ZX_DEBUG_ASSERT(loop);

    // Ensure that there are no active threads before resetting the quit state.
    // This check is inherently racy but not dangerously so.  It's mainly a
    // sanity check for client code so we can make a stronger statement about
    // how |async_loop_reset_quit()| is supposed to be used.
    uint32_t n = atomic_load_explicit(&loop->active_threads, memory_order_acquire);
    if (n != 0)
        return ZX_ERR_BAD_STATE;

    async_loop_state_t expected_state = ASYNC_LOOP_QUIT;
    if (atomic_compare_exchange_strong_explicit(&loop->state, &expected_state,
                                                ASYNC_LOOP_RUNNABLE,
                                                memory_order_acq_rel, memory_order_acquire)) {
        return ZX_OK;
    }

    async_loop_state_t state = atomic_load_explicit(&loop->state, memory_order_acquire);
    if (state == ASYNC_LOOP_RUNNABLE)
        return ZX_OK;
    return ZX_ERR_BAD_STATE;
}

async_loop_state_t async_loop_get_state(async_loop_t* loop) {
    ZX_DEBUG_ASSERT(loop);

    return atomic_load_explicit(&loop->state, memory_order_acquire);
}

zx_time_t async_loop_now(async_t* async) {
    return zx_clock_get_monotonic();
}

static zx_status_t async_loop_begin_wait(async_t* async, async_wait_t* wait) {
    async_loop_t* loop = (async_loop_t*)async;
    ZX_DEBUG_ASSERT(loop);
    ZX_DEBUG_ASSERT(wait);

    if (atomic_load_explicit(&loop->state, memory_order_acquire) == ASYNC_LOOP_SHUTDOWN)
        return ZX_ERR_BAD_STATE;

    mtx_lock(&loop->lock);

    zx_status_t status = zx_object_wait_async(
        wait->object, loop->port, (uintptr_t)wait, wait->trigger, ZX_WAIT_ASYNC_ONCE);
    if (status == ZX_OK) {
        list_add_head(&loop->wait_list, wait_to_node(wait));
    } else {
        ZX_ASSERT_MSG(status == ZX_ERR_ACCESS_DENIED,
                      "zx_object_wait_async: status=%d", status);
    }

    mtx_unlock(&loop->lock);
    return status;
}

static zx_status_t async_loop_cancel_wait(async_t* async, async_wait_t* wait) {
    async_loop_t* loop = (async_loop_t*)async;
    ZX_DEBUG_ASSERT(loop);
    ZX_DEBUG_ASSERT(wait);

    // Note: We need to process cancelations even while the loop is being
    // destroyed in case the client is counting on the handler not being
    // invoked again past this point.

    mtx_lock(&loop->lock);

    // First, confirm that the wait is actually pending.
    list_node_t* node = wait_to_node(wait);
    if (!list_in_list(node)) {
        mtx_unlock(&loop->lock);
        return ZX_ERR_NOT_FOUND;
    }

    // Next, cancel the wait.  This may be racing with another thread that
    // has read the wait's packet but not yet dispatched it.  So if we fail
    // to cancel then we assume we lost the race.
    zx_status_t status = zx_port_cancel(loop->port, wait->object,
                                        (uintptr_t)wait);
    if (status == ZX_OK) {
        list_delete(node);
    } else {
        ZX_ASSERT_MSG(status == ZX_ERR_NOT_FOUND,
                      "zx_port_cancel: status=%d", status);
    }

    mtx_unlock(&loop->lock);
    return status;
}

static zx_status_t async_loop_post_task(async_t* async, async_task_t* task) {
    async_loop_t* loop = (async_loop_t*)async;
    ZX_DEBUG_ASSERT(loop);
    ZX_DEBUG_ASSERT(task);

    if (atomic_load_explicit(&loop->state, memory_order_acquire) == ASYNC_LOOP_SHUTDOWN)
        return ZX_ERR_BAD_STATE;

    mtx_lock(&loop->lock);

    async_loop_insert_task_locked(loop, task);
    if (!loop->dispatching_tasks &&
        task_to_node(task)->prev == &loop->task_list) {
        // Task inserted at head.  Earliest deadline changed.
        async_loop_restart_timer_locked(loop);
    }

    mtx_unlock(&loop->lock);
    return ZX_OK;
}

static zx_status_t async_loop_cancel_task(async_t* async, async_task_t* task) {
    async_loop_t* loop = (async_loop_t*)async;
    ZX_DEBUG_ASSERT(loop);
    ZX_DEBUG_ASSERT(task);

    // Note: We need to process cancelations even while the loop is being
    // destroyed in case the client is counting on the handler not being
    // invoked again past this point.  Also, the task we're removing here
    // might be present in the dispatcher's |due_list| if it is pending
    // dispatch instead of in the loop's |task_list| as usual.  The same
    // logic works in both cases.

    mtx_lock(&loop->lock);
    list_node_t* node = task_to_node(task);
    if (!list_in_list(node)) {
        mtx_unlock(&loop->lock);
        return ZX_ERR_NOT_FOUND;
    }

    // Determine whether the head task was canceled and following task has
    // a later deadline.  If so, we will bump the timer along to that deadline.
    bool must_restart = !loop->dispatching_tasks &&
                        node->prev == &loop->task_list &&
                        node->next != &loop->task_list &&
                        node_to_task(node->next)->deadline > task->deadline;
    list_delete(node);
    if (must_restart)
        async_loop_restart_timer_locked(loop);

    mtx_unlock(&loop->lock);
    return ZX_OK;
}

static zx_status_t async_loop_queue_packet(async_t* async, async_receiver_t* receiver,
                                           const zx_packet_user_t* data) {
    async_loop_t* loop = (async_loop_t*)async;
    ZX_DEBUG_ASSERT(loop);
    ZX_DEBUG_ASSERT(receiver);

    if (atomic_load_explicit(&loop->state, memory_order_acquire) == ASYNC_LOOP_SHUTDOWN)
        return ZX_ERR_BAD_STATE;

    zx_port_packet_t packet = {
        .key = (uintptr_t)receiver,
        .type = ZX_PKT_TYPE_USER,
        .status = ZX_OK};
    if (data)
        packet.user = *data;
    return zx_port_queue(loop->port, &packet);
}

static zx_status_t async_loop_set_guest_bell_trap(
    async_t* async, async_guest_bell_trap_t* trap,
    zx_handle_t guest, zx_vaddr_t addr, size_t length) {
    async_loop_t* loop = (async_loop_t*)async;
    ZX_DEBUG_ASSERT(loop);
    ZX_DEBUG_ASSERT(trap);

    if (atomic_load_explicit(&loop->state, memory_order_acquire) == ASYNC_LOOP_SHUTDOWN)
        return ZX_ERR_BAD_STATE;

    zx_status_t status = zx_guest_set_trap(guest, ZX_GUEST_TRAP_BELL, addr,
                                           length, loop->port, (uintptr_t)trap);
    if (status != ZX_OK) {
        ZX_ASSERT_MSG(status == ZX_ERR_ACCESS_DENIED ||
                          status == ZX_ERR_ALREADY_EXISTS ||
                          status == ZX_ERR_INVALID_ARGS ||
                          status == ZX_ERR_OUT_OF_RANGE ||
                          status == ZX_ERR_WRONG_TYPE,
                      "zx_guest_set_trap: status=%d", status);
    }
    return status;
}

static void async_loop_insert_task_locked(async_loop_t* loop, async_task_t* task) {
    // TODO(ZX-976): We assume that tasks are inserted in quasi-monotonic order and
    // that insertion into the task queue will typically take no more than a few steps.
    // If this assumption proves false and the cost of insertion becomes a problem, we
    // should consider using a more efficient representation for maintaining order.
    list_node_t* node;
    for (node = loop->task_list.prev; node != &loop->task_list; node = node->prev) {
        if (task->deadline >= node_to_task(node)->deadline)
            break;
    }
    list_add_after(node, task_to_node(task));
}

static void async_loop_restart_timer_locked(async_loop_t* loop) {
    zx_time_t deadline;
    if (list_is_empty(&loop->due_list)) {
        list_node_t* head = list_peek_head(&loop->task_list);
        if (!head)
            return;
        async_task_t* task = node_to_task(head);
        deadline = task->deadline;
        if (deadline == ZX_TIME_INFINITE)
            return;
    } else {
        // Fire now.
        deadline = 0ULL;
    }

    zx_status_t status = zx_timer_set(loop->timer, deadline, 0);
    ZX_ASSERT_MSG(status == ZX_OK, "zx_timer_set: status=%d", status);
}

static void async_loop_invoke_prologue(async_loop_t* loop) {
    if (loop->config.prologue)
        loop->config.prologue(loop, loop->config.data);
}

static void async_loop_invoke_epilogue(async_loop_t* loop) {
    if (loop->config.epilogue)
        loop->config.epilogue(loop, loop->config.data);
}

static int async_loop_run_thread(void* data) {
    async_loop_t* loop = (async_loop_t*)data;
    async_set_default(&loop->async);
    async_loop_run(loop, ZX_TIME_INFINITE, false);
    return 0;
}

zx_status_t async_loop_start_thread(async_loop_t* loop, const char* name, thrd_t* out_thread) {
    ZX_DEBUG_ASSERT(loop);

    // This check is inherently racy.  The client should not be racing shutdown
    // with attemps to start new threads.  This is mainly a sanity check.
    async_loop_state_t state = atomic_load_explicit(&loop->state, memory_order_acquire);
    if (state == ASYNC_LOOP_SHUTDOWN)
        return ZX_ERR_BAD_STATE;

    thread_record_t* rec = calloc(1u, sizeof(thread_record_t));
    if (!rec)
        return ZX_ERR_NO_MEMORY;

    if (thrd_create_with_name(&rec->thread, async_loop_run_thread, loop, name) != thrd_success) {
        free(rec);
        return ZX_ERR_NO_MEMORY;
    }

    mtx_lock(&loop->lock);
    list_add_tail(&loop->thread_list, &rec->node);
    mtx_unlock(&loop->lock);

    if (out_thread)
        *out_thread = rec->thread;
    return ZX_OK;
}

void async_loop_join_threads(async_loop_t* loop) {
    ZX_DEBUG_ASSERT(loop);

    mtx_lock(&loop->lock);
    for (;;) {
        thread_record_t* rec = (thread_record_t*)list_remove_head(&loop->thread_list);
        if (!rec)
            break;

        mtx_unlock(&loop->lock);
        thrd_t thread = rec->thread;
        free(rec);
        int result = thrd_join(thread, NULL);
        ZX_DEBUG_ASSERT(result == thrd_success);
        mtx_lock(&loop->lock);
    }
    mtx_unlock(&loop->lock);
}

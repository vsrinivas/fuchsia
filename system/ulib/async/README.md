# libasync

This library defines a C language interface for dispatching the results of
asynchronous operations and tasks.  This library decouples libraries
which require use of an asynchronous dispatch interface (such as FIDL bindings)
from the dispatcher's implementation.

This library also provides a simple thread-safe message loop implementation
which can be used out of the box.

Clients can also provide their own asynchronous dispatcher implementations
tied to their own task runner, message loop, thread pool, etc.

## Using the asynchronous dispatcher

### Waiting for signals

To asynchronously wait for signals, the client prepares an `async_wait_t`
structure then calls `async_begin_wait()` to register it with the dispatcher.
When the wait completes, the dispatcher invokes the handler.  If the handler
returns |ASYNC_WAIT_AGAIN| and no error occurred then the wait is automatically
restarted, otherwise the wait ends.

The client can register handlers from any thread but dispatch will occur
on a thread of the dispatcher's choosing depending on its implementation.

The client is responsible for ensuring that the wait structure remains in
memory until the wait's handler runs or the wait is successfully canceled using
`async_cancel_wait()`.

```c
#include <async/async.h>

async_wait_result_t handler(async_t* async, async_wait_t* wait,
                            mx_status_t status, const mx_packet_signal_t* signal) {
    printf("signal received: status=%d, observed=%d", status, signal ? signal->observed : 0);
    free(wait);
    return ASYNC_WAIT_FINISHED;
}

mx_status_t await(mx_handle_t object, mx_signals_t trigger, void* data) {
    async_wait_t* wait = calloc(1, sizeof(async_wait_t));
    wait->handler = handler;
    wait->object = object;
    wait->trigger = trigger;
    wait->flags = ASYNC_HANDLE_SHUTDOWN;
    return async_begin_wait(async_get_default(), handle, wait);
}
```

### Posting tasks

To schedule asynchronous tasks, the client prepares an `async_task_t`
structure then calls `async_post_task()` to register it with the dispatcher.
When the task comes due, the dispatcher invokes the handler.

The client can post tasks from any thread but dispatch will occur
on a thread of the dispatcher's choosing depending on its implementation.

The client is responsible for ensuring that the task structure remains in
memory until the task's handler runs or the task is successfully canceled using
`async_cancel_task()`.

```c
#include <async/async.h>

typedef struct {
    async_task_t task;
    void* data;
} task_data_t;

async_task_result_t handler(async_t* async, async_task_t* task, mx_status_t status) {
    task_data_t* task_data = (task_data_t*)task;
    printf("task deadline elapsed: status=%d, data=%p", status, task_data->data);
    free(task_data);
    return ASYNC_TASK_FINISHED;
}

mx_status_t schedule_work(void* data) {
    task_data_t* task_data = calloc(1, sizeof(task_data_t));
    task_data->task.handler = handler;
    task_data->task.deadline = mx_deadline_after(MX_SEC(2));
    task_data->task.flags = ASYNC_HANDLE_SHUTDOWN;
    task_data->data = data;
    return async_post_task(async_get_default(), &task_data->task);
}
```

### Delivering packets to a receiver

Occasionally it may be useful to register a receiver which will be the
recipient of multiple data packets instead of allocating a separate task
structure for each one.  The Magenta port takes care of storing the queued
packet data contents until it is delivered.

The client can queue packets from any thread but dispatch will occur
on a thread of the dispatcher's choosing depending on its implementation.

The client is responsible for ensuring that the receiver structure remains in
memory until all queued packets have been delivered.

```c
#include <async/async.h>

void handler(async_t* async, async_receiver_t* receiver, mx_status_t status,
             const mx_packet_user_t* data) {
    printf("packet received: status=%d, data.u32[0]=%d", status, data ? data.u32[0] : 0);
}

const async_receiver_t receiver = {
    .handler = handler;
}

mx_status_t send(const mx_packet_user_t* data) {
    return async_queue_packet(async_get_default(), &receiver, data);
}
```

## Using the message loop

This library includes a thread-safe message loop implementation of an
asynchronous dispatcher which you can use out of box instead of writing
your own.

```c
#include <async/loop.h>

int main(int argc, char** argv) {
    async_t* async;
    async_loop_create(NULL, &async);
    async_set_default(async);

    do_stuff();

    async_loop_run(async, MX_TIME_INFINITE, false);
    async_loop_destroy(async);
    async_set_default(NULL);  // optional since we're exiting right away
    return 0;
}

async_task_result_t handler(async_t* async, async_task_t* task, mx_status_t status) {
    printf("task deadline elapsed: status=%d", status);
    free(task);

    // This example doesn't have much to do, so just quit here.
    async_loop_quit(async_get_default());
    return ASYNC_TASK_FINISHED;
}

mx_status_t do_stuff() {
    async_task_t* task = calloc(1, sizeof(async_task_t));
    task->handler = handler;
    task->deadline = mx_deadline_after(MX_SEC(2));
    return async_post_task(async_get_default(), task);
}
```

## Using the C++ helpers

This library includes a few helper classes for writing handlers in C++ in
[async/async.h](include/async/async.h).  To use them, subclass `Wait`, `Task`,
or `Receiver` and implement the `Handle` function.

There is also a special `WaitWithTimeout` helper in
[async/timeouts.h](include/async/timeouts.h) which combines a wait operation
with a pending task which invokes the handler when the specified deadline has been
exceeded.  To use it, subclass `WaitWithTimeout` and implement the `Handle` function.

## Implementing a dispatcher

The `async_ops_t` interface is a low-level abstraction for asynchronous
dispatchers.  You can make custom implementations of this interface to
integrate clients of this library with your own dispatcher.

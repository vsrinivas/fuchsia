# libasync and friends

This set of libraries defines a C and C++ language interface for initiating
asynchronous operations and dispatching their results to callback functions.

The purpose of these libraries is to decouple clients which want to perform
asynchronous operations from the message loop implementations which dispatch
the results of those operations.  This makes it an important building block
for other abstractions such as the FIDL bindings.

## Libraries

The async package consists of three libraries:

- `libasync.a` provides the client API which includes all of the functions
and structures declared in [async/dispatcher.h](include/async/dispatcher.h),
[async/wait.h](include/async/wait.h), [async/wait_with_timeout.h](include/async/wait_with_timeout.h),
[async/task.h](include/async/task.h), [async/receiver.h](include/async/receiver.h),
[async/auto_wait.h](include/async/auto_wait.h), and [async/auto_task.h](include/async/auto_task.h).
This library must be statically linked into clients.

- `libasync-loop.a` provides a general-purpose thread-safe message loop
implementation declared in [async/loop.h](include/async/loop.h).  This library
must be statically linked into clients that want to use this particular message
loop implementation.  Note that clients can implement their own asynchronous
dispatchers tied if they have more specialized needs.

- `libasync-default.so` provides functions for getting or setting a thread-local
default asynchronous dispatcher as declared in [async/default.h](include/async/default.h).
This library must be dynamically linked into clients that use `libasync-loop.a`
or that want access to the default asynchronous dispatcher.

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

See [async/wait.h](include/async/wait.h) for details.

```c
#include <async/wait.h>

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
    wait->flags = ASYNC_FLAG_HANDLE_SHUTDOWN;
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

See [async/task.h](include/async/task.h) for details.

```c
#include <async/task.h>

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
    task_data->task.flags = ASYNC_FLAG_HANDLE_SHUTDOWN;
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

See [async/receiver.h](include/async/receiver.h) for details.

```c
#include <async/receiver.h>

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

`libasync-loop.a` provides a general-purpose thread-safe message loop
implementation of an asynchronous dispatcher which you can use out of box
unless you need something more specialized.

See [async/loop.h](include/async/loop.h) for details.

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

## The default async dispatcher

As a client of the async dispatcher, where should you get your `async_t*` from?

The ideal answer is for the `async_t*` to be passed into your code when it is
initialized.  However sometimes this becomes burdensome or isn't practical.

For this reason, the `libasync-default.so` shared library provides functions
for getting or setting a thread-local default `async_t*` using
`async_get_default()` or `async_set_default()`.

You can set the default yourself, or have `async_loop_create()` do it
for you automatically by setting the `make_default_for_current_thread`
configuration option.

See [async/default.h](include/async/default.h) for details.

## Using the C++ helpers

`libasync.a` includes `Wait`, `Task`, and `Receiver` helper classes which wrap
the C API with a more convenient fbl::Function<> callback based interface
for use in C++.

`AutoWait` in [async/auto_wait.h](include/async/auto_wait.h) is an RAII helper
which cancels the wait when it goes out of scope.

`AutoTask` in [async/auto_task.h](include/async/auto_task.h) is an RAII helper
which cancels the task when it goes out of scope.

There is also a special `WaitWithTimeout` helper defined in
[async/wait_with_timeout.h](include/async/wait_with_timeout.h)
which combines a wait operation together with a pending task that invokes the
handler when the specified deadline has been exceeded.

## Implementing a dispatcher

The `async_ops_t` interface is a low-level abstraction for asynchronous
dispatchers.  You can make custom implementations of this interface to
integrate clients of this library with your own dispatcher.

It is possible to implement only some of the operations but this may cause
incompatibilities with certain clients.

See [async/dispatcher.h](include/async/dispatcher.h) for details.

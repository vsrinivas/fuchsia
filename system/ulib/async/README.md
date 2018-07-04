# libasync and friends

This set of libraries defines a C and C++ language interface for initiating
asynchronous operations and dispatching their results to callback functions.

The purpose of these libraries is to decouple clients which want to perform
asynchronous operations from the message loop implementations which dispatch
the results of those operations.  This makes it an important building block
for other abstractions such as the FIDL bindings.

## Libraries

The async package consists of three libraries:

- `libasync.a` provides the C client API which includes all of the function
and structures declared in the following headers:
    - [async/dispatcher.h](include/lib/async/dispatcher.h)
    - [async/receiver.h](include/lib/async/receiver.h)
    - [async/task.h](include/lib/async/task.h)
    - [async/time.h](include/lib/async/time.h)
    - [async/trap.h](include/lib/async/trap.h)
    - [async/wait.h](include/lib/async/wait.h)

- `libasync-cpp.a` provides C++ wrappers:
    - [async/cpp/receiver.h](include/lib/async/cpp/receiver.h)
    - [async/cpp/task.h](include/lib/async/cpp/task.h)
    - [async/cpp/time.h](include/lib/async/cpp/time.h)
    - [async/cpp/trap.h](include/lib/async/cpp/trap.h)
    - [async/cpp/wait.h](include/lib/async/cpp/wait.h)

- `libasync-default.so` provides functions for getting or setting a thread-local
default asynchronous dispatcher as declared in [async/default.h](include/lib/async/default.h).

See also [libasync-loop.a](../async-loop/README.md) which provides a general-purpose
implementation of `async_dispatcher_t`.

## Using the asynchronous dispatcher

### Waiting for signals

To asynchronously wait for signals, the client prepares an `async_wait_t`
structure then calls `async_begin_wait()` to register it with the dispatcher.
When the wait completes, the dispatcher invokes the handler.

The client can register handlers from any thread but dispatch will occur
on a thread of the dispatcher's choosing depending on its implementation.

The client is responsible for ensuring that the wait structure remains in
memory until the wait's handler runs or the wait is successfully canceled using
`async_cancel_wait()`.

See [async/wait.h](include/lib/async/wait.h) for details.

```c
#include <lib/async/wait.h>     // for async_begin_wait()
#include <lib/async/default.h>  // for async_get_default_dispatcher()

void handler(async_dispatcher_t* async, async_wait_t* wait,
             zx_status_t status, const zx_packet_signal_t* signal) {
    printf("signal received: status=%d, observed=%d", status, signal ? signal->observed : 0);
    free(wait);
}

zx_status_t await(zx_handle_t object, zx_signals_t trigger, void* data) {
    async_dispatcher_t* async = async_get_default_dispatcher();
    async_wait_t* wait = calloc(1, sizeof(async_wait_t));
    wait->handler = handler;
    wait->object = object;
    wait->trigger = trigger;
    return async_begin_wait(async, wait);
}
```

### Getting the current time

The dispatcher represents time in the form of a `zx_time_t`.  In normal
operation, values of this type represent a moment in the `ZX_CLOCK_MONOTONIC`
time base.  However for unit testing purposes, dispatchers may use a synthetic
time base instead.

To make unit testing easier, prefer using `async_now()` to get the current
time according the dispatcher's time base.

See [async/time.h](include/lib/async/time.h) for details.

### Posting tasks and getting the current time

To schedule asynchronous tasks, the client prepares an `async_task_t`
structure then calls `async_post_task()` to register it with the dispatcher.
When the task comes due, the dispatcher invokes the handler.

The client can post tasks from any thread but dispatch will occur
on a thread of the dispatcher's choosing depending on its implementation.

The client is responsible for ensuring that the task structure remains in
memory until the task's handler runs or the task is successfully canceled using
`async_cancel_task()`.

See [async/task.h](include/lib/async/task.h) for details.

```c
#include <lib/async/task.h>     // for async_post_task()
#include <lib/async/time.h>     // for async_now()
#include <lib/async/default.h>  // for async_get_default_dispatcher()

typedef struct {
    async_task_t task;
    void* data;
} task_data_t;

void handler(async_dispatcher_t* async, async_task_t* task, zx_status_t status) {
    task_data_t* task_data = (task_data_t*)task;
    printf("task deadline elapsed: status=%d, data=%p", status, task_data->data);
    free(task_data);
}

zx_status_t schedule_work(void* data) {
    async_dispatcher_t* async = async_get_default_dispatcher();
    task_data_t* task_data = calloc(1, sizeof(task_data_t));
    task_data->task.handler = handler;
    task_data->task.deadline = async_now(async) + ZX_SEC(2);
    task_data->data = data;
    return async_post_task(async, &task_data->task);
}
```

### Delivering packets to a receiver

Occasionally it may be useful to register a receiver which will be the
recipient of multiple data packets instead of allocating a separate task
structure for each one.  The Zircon port takes care of storing the queued
packet data contents until it is delivered.

The client can queue packets from any thread but dispatch will occur
on a thread of the dispatcher's choosing depending on its implementation.

The client is responsible for ensuring that the receiver structure remains in
memory until all queued packets have been delivered.

See [async/receiver.h](include/lib/async/receiver.h) for details.

```c
#include <lib/async/receiver.h>  // for async_queue_packet()
#include <lib/async/default.h>   // for async_get_default_dispatcher()

void handler(async_dispatcher_t* async, async_receiver_t* receiver, zx_status_t status,
             const zx_packet_user_t* data) {
    printf("packet received: status=%d, data.u32[0]=%d", status, data ? data.u32[0] : 0);
}

const async_receiver_t receiver = {
    .state = ASYNC_STATE_INIT,
    .handler = handler;
}

zx_status_t send(const zx_packet_user_t* data) {
    async_dispatcher_t* async = async_get_default_dispatcher();
    return async_queue_packet(async, &receiver, data);
}
```

## The default async dispatcher

As a client of the async dispatcher, where should you get your `async_dispatcher_t*` from?

The ideal answer is for the `async_dispatcher_t*` to be passed into your code when it is
initialized.  However sometimes this becomes burdensome or isn't practical.

For this reason, the `libasync-default.so` shared library provides functions
for getting or setting a thread-local default `async_dispatcher_t*` using
`async_get_default_dispatcher()` or `async_set_default_dispatcher()`.

This makes it easy to retrieve the `async_dispatcher_t*` from the ambient environment
by calling `async_get_default_dispatcher()`, which is used by many libraries.

Message loop implementations should register themselves as the default
dispatcher any threads they service.

See [async/default.h](include/lib/async/default.h) for details.

## Using the C++ helpers

`libasync-cpp.a` includes helper classes such as `Wait`, `Task`, and `Receiver`
which wrap the C API with a more convenient type safe interface for use
in C++.

Note that the C API can of course be used directly from C++ for special
situations which may not be well addressed by the wrappers.

## Implementing a dispatcher

The `async_ops_t` interface is a low-level abstraction for asynchronous
dispatchers.  You can make custom implementations of this interface to
integrate clients of this library with your own dispatcher.

It is possible to implement only some of the operations but this may cause
incompatibilities with certain clients.

See [async/dispatcher.h](include/lib/async/dispatcher.h) for details.

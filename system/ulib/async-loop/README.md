# libasync-loop and libasync-loop-cpp

This library provides a general-purpose thread-safe message loop
implementation declared in [async/loop.h](include/async/loop.h).
It must be statically linked into clients that want to use this particular
message loop implementation.  Note that clients can implement their own
asynchronous dispatchers instead if they have more specialized needs.

## Libraries

- `libasync-loop.a` provides the loop implementation itself as declared in
the following headers:
    - [async-loop/loop.h](include/lib/async-loop/loop.h)

- `libasync-loop-cpp.a` provides C++ wrappers:
    - [async-loop/cpp/loop.h](include/lib/async-loop/cpp/loop.h)

## Using the message loop

`libasync-loop.a` provides a general-purpose thread-safe message loop
implementation of an asynchronous dispatcher which you can use out of box
unless you need something more specialized.

See [async/loop.h](include/async/loop.h) for details.

```c
#include <lib/async-loop/loop.h>
#include <lib/async/task.h>      // for async_post_task()
#include <lib/async/time.h>      // for async_now()
#include <lib/async/default.h>   // for async_get_default()

static async_loop_t* g_loop;

int main(int argc, char** argv) {
    async_loop_create(&kAsyncLoopConfigMakeDefault, &g_loop);

    do_stuff();

    async_loop_run(g_loop, ZX_TIME_INFINITE, false);
    async_loop_destroy(g_loop);
    return 0;
}

void handler(async_t* async, async_task_t* task, zx_status_t status) {
    printf("task deadline elapsed: status=%d", status);
    free(task);

    // This example doesn't have much to do, so just quit here.
    async_loop_quit(g_loop);
}

zx_status_t do_stuff() {
    async_t* async = async_get_default();
    async_task_t* task = calloc(1, sizeof(async_task_t));
    task->handler = handler;
    task->deadline = async_now(async) + ZX_SEC(2);
    return async_post_task(async, task);
}
```

## Setting as the default async dispatcher

If the `make_default_for_current_thread` configuration option is set to true
and `libasync-default.so` is linked into the executable, the message loop
will automatically register itself as the default dispatcher for the thread on
which it is created.

New threads started with `async_loop_state_thread()` will automatically have
their default dispatcher set to the message loop regardless of the value of
`make_default_for_current_thread`.

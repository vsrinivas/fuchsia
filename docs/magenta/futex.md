# Futexes

## What is a futex?

A **futex** is a Fast Userspace muTEX. It is a low level
synchronization primitive which is a building block for higher level
APIs such as `pthread_mutex_t` and `pthread_cond_t`.

Futexes are designed to not enter the kernel or allocate kernel
resources in the uncontested case.

## Papers about futexes

- [Fuss, Futexes and Furwocks: Fast Userlevel Locking in Linux](https://www.kernel.org/doc/ols/2002/ols2002-pages-479-495.pdf), Hubertus Franke and Rusty Russell

    This is the original white paper describing the Linux futex. It
    documents the history and design of the original implementation,
    prior (failed) attempts at creating a fast userspace
    synchronization primitive, and performance measurements.

- [Futexes Are Tricky](https://www.akkadia.org/drepper/futex.pdf), Ulrich Drepper
    This paper describes some gotchas and implementation details of
    futexes in Linux. It discusses the kernel implementation, and goes
    into more detail about correct and efficient userspace
    implementations of mutexes, condition variables, and so on.

## API

The magenta futex implementation currently supports three operations:

```C
    uint32_t sys_futex_wait(int* value_ptr, int current_value, uint32_t timeout)
    uint32_t sys_futex_wake(int* value_ptr, uint32_t wake_count)
    uint32_t sys_futex_requeue(int* value_ptr, uint32_t wake_count, int current_value, int* requeue_ptr, uint32_t requeue_count);
```

All of these share a `value_ptr` parameter, which is the virtual
address of an aligned userspace integer. This virtual address is the
information used in kernel to track what futex given threads are
waiting on. The kernel does not currently modify the value of
`*value_ptr` (but see below for future operations which might do
so). It is up to userspace code to correctly atomically modify this
value across threads in order to build mutexes and so on.

### `sys_futex_wait`

Waiting on a futex (or acquiring it) causes a thread to sleep until
the futex is made available by a call to `sys_futex_wake`. Optionally,
the thread can also be woken up after the timeout argument expires.

### `sys_futex_wake`

Waking a futex causes `wake_count` threads waiting on that futex to be
woken up.

### `sys_futex_requeue`

Requeuing is a generalization of waking. After waking `wake_count`
threads, `requeue_count` threads are moved from the original futex's
wait queue to the wait queue corresponding to `requeue_ptr`, another
futex.

This requeueing behavior is used to avoid thundering herds on wake.

## Differences from Linux futexes

Note that all of the magenta futex operations key off of the virtual
address of an userspace pointer. This differs from the Linux
implementation, which distinguishes private futex operations (which
correspond to our in-process-only ones) from ones shared across
address spaces.

As noted above, all of our futex operations leave the value of the
futex unmodified from the kernel. Other potential operations, such as
Linux's `FUTEX_WAKE_OP`, requires atomic manipulation of the value
from the kernel, which our current implementation does not require.

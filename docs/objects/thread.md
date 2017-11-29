# Thread

## NAME

thread - runnable / computation entity

## SYNOPSIS

TODO

## DESCRIPTION

The thread object is the construct that represents a time-shared CPU execution
context. Thread objects live associated to a particular
[Process Object](process.md) which provides the memory and the handles to other
objects necessary for I/O and computation.

### Lifetime
Threads are created by calling `zx_thread_create()`, but only start executing
when either `zx_thread_start()` or `zx_process_start()` are called. Both syscalls
take as an argument the entrypoint of the initial routine to execute.

The thread passed to `zx_process_start()` should be the first thread to start execution
on a process.

A thread terminates execution:
+ by calling `zx_thread_exit()`
+ by calling `zx_vmar_unmap_handle_close_thread_exit()`
+ by calling `zx_futex_wake_handle_close_thread_exit()`
+ when the parent process terminates
+ by calling `zx_task_kill()` with the thread's handle
+ after generating an exception for which there is no handler or the handler
decides to terminate the thread.

Returning from the entrypoint routine does not terminate execution. The last
action of the entrypoint should be to call `zx_thread_exit()` or one of the
above mentioned `_exit()` variants.

Closing the last handle to a thread does not terminate execution. In order to
forcefully kill a thread for which there is no available handle, use
`zx_object_get_child()` to obtain a handle to the thread. This method is strongly
discouraged. Killing a thread that is executing might leave the process in a
corrupt state.

Fuchsia native threads are always *detached*. That is, there is no *join()* operation
needed to do a clean termination. However, some runtimes above the kernel, such as
C11 or POSIX might require threads to be joined.

## SYSCALLS

+ [thread_create](../syscalls/thread_create.md) - create a new thread within a process
+ [thread_exit](../syscalls/thread_exit.md) - exit the current thread
+ [thread_read_state](../syscalls/thread_read_state.md) - read register state from a thread
+ [thread_start](../syscalls/thread_start.md) - cause a new thread to start executing
+ [thread_write_state](../syscalls/thread_write_state.md) - modify register state of a thread

<br>

+ [task_resume](../syscalls/task_resume.md) - cause a suspended task to continue running
+ [task_bind_exception_port](../syscalls/task_bind_exception_port.md) - attach an exception
port to a task
+ [task_kill](../syscalls/task_kill.md) - cause a task to stop running

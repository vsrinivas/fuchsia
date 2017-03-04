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
A thread can be created implicitly by calling `sys_process_start()`, in which
case the new thread is the "main thread" and the thread entrypoint is defined by
the previously loaded binary. Or it can be created by calling
`sys_thread_create()` which takes the entrypoint as a parameter.

A thread terminates when it `return`s from executing the routine specified as
the entrypoint or by calling `sys_thread_exit()`.

## SYSCALLS

+ [thread_create](../syscalls/thread_create.md) - create a new thread within a process
+ [thread_exit](../syscalls/thread_exit.md) - exit the current thread
+ [thread_read_state](../syscalls/thread_read_state.md) - read register state from a thread
+ [thread_start](../syscalls/thread_start.md) - cause a new thread to start executing
+ [thread_write_state](../syscalls/thread_write_state.md) - modify register state of a thread

<br>

+ [task_resume](../syscalls/task_resume.md) - cause a suspended task to continue running
+ [task_bind_exception_port](../syscalls/task_bind_exception_port.md) - attach an exception port to a task
+ [task_kill](../syscalls/task_kill.md) - cause a task to stop running

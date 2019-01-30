# Task

## NAME

Task - "Runnable" subclass of kernel objects (threads, processes, and jobs)

## SYNOPSIS

[Threads](thread.md), [processes](process.md), and [jobs](job.md) objects
are all tasks. They share the ability to be suspended, resumed, and
killed.

## DESCRIPTION

TODO

## SYSCALLS

 - [`zx_task_bind_exception_port()`] - attach an exception port to a task
 - [`zx_task_kill()`] - cause a task to stop running

[`zx_task_bind_exception_port()`]: ../syscalls/task_bind_exception_port.md
[`zx_task_kill()`]: ../syscalls/task_kill.md

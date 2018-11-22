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

+ [task_bind_exception_port](../syscalls/task_bind_exception_port.md) - attach an exception port to a task
+ [task_kill](../syscalls/task_kill.md) - cause a task to stop running

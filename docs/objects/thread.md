# Thread Object

## NAME

thread - Thread abstraction

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

## SEE ALSO

TODO (add links to thread syscall docs)

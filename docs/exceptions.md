# Exception handling

## Introduction

Exception handling support in Zircon was inspired by similar support in Mach.

Exceptions are mainly used for debugging. Outside of debugging
one generally uses ["signals"](signals.md).
Signals are the core Zircon mechanism for observing state changes on
kernel Objects (a Channel becoming readable, a Process terminating,
an Event becoming signaled, etc).
See [Signals](#signals) below.

The reader is assumed to have a basic understanding of what exceptions like
segmentation faults, etc. are, as well as Posix signals.
This document does not explain what a segfault is, nor what "exception
handling" is at a high level (though it certainly can if there is a need).

## The basics

Exceptions are handled from userspace by binding a Zircon Port to the
Exception Port of the desired object: thread, process, or job.
This is done with the
[**task_bind_exception_port**() system call](syscalls/task_bind_exception_port.md).

Example:

```cpp
  zx_handle_t eport;
  auto status = zx_port_create(0, &eport);
  // ... check status ...
  uint32_t options = 0;
  // The key is anything that is useful to the code handling the exception.
  uint64_t child_key = 0;
  // Assume |child| is a process handle.
  status = zx_task_bind_exception_port(child, eport, child_key, options);
  // ... check status ...
```

When an exception occurs a report is sent to the port,
after which the receiver must reply with either "exception handled"
or "exception not handled".
The thread stays paused until then, or until the port is unbound,
either explicitly or by the port being closed (say because the handler
process exited). If the port is unbound, for whatever reason, the
exception is processed as if the reply was "exception not handled".

Here is a simple exception handling loop.
The main components of it are the call to the
[**port_wait**() system call](syscalls/port_wait.md)
to wait for an exception, or anything else that's interesting, to happen,
and the call to the
[**thread_resume_from_exception**() system call]
(syscalls/thread_resume_from_exception.md)
to indicate the handler is finished processing the exception.

```cpp
  while (true) {
    zx_port_packet_t packet;
    auto status = zx_port_wait(eport, ZX_TIME_INFINITE, packet);
    // ... check status ...
    if (packet.key != child_key) {
      // ... do something else, depending on what else the port is used for ...
      continue;
    }
    if (!ZX_PKT_IS_EXCEPTION(packet.type)) {
      // ... probably a signal, process it ...
      continue;
    }
    zx_koid_t packet_tid = packet.exception.tid;
    zx_handle_t thread;
    status = zx_object_get_child(child, packet_tid, ZX_RIGHT_SAME_RIGHTS,
                                 &thread);
    // ... check status ...
    bool handled = process_exception(child, thread, &packet);
    uint32_t resume_flags = 0;
    if (!handled)
      resume_flags |= ZX_RESUME_TRY_NEXT;
    status = zx_task_resume_from_exception(thread, eport, resume_flags);
    // ... check status ...
    status = zx_handle_close(thread);
    assert(status == ZX_OK);
  }
```

To unbind an exception port, pass **ZX_HANDLE_INVALID** for the
exception port:

```cpp
  uint32_t options = 0;
  status = zx_task_bind_exception_port(child, ZX_HANDLE_INVALID,
                                       key, options);
  // ... check status ...
```

## Exception processing details

When a thread gets an exception it is paused while the kernel processes
the exception. The kernel looks for bound exception ports in a specific order
and if it finds one an "exception report" is sent to the bound port.

Exception reports are messages sent through the port with a specific format
defined by the port message protocol. The packet contents are defined by
the *zx_packet_exception_t* type defined in
[`<zircon/syscalls/port.h>`](../system/public/zircon/syscalls/port.h).

The exception handler is expected to read the message, decide how it
wants to process the exception, and then resume the thread that got the
exception with the [**task_resume_from_exception**() system call]
(syscalls/task_resume_from_exception.md).

Resuming the thread can be done in either of two ways:

- Resume execution of the thread as if the exception has been resolved.
If the thread gets another exception then exception processing begins
again anew. An example of when one would do this is when resuming after a
debugger breakpoint.

```cpp
  auto status = zx_task_resume_from_exception(thread, eport, 0);
  // ... check status ...
```

- Resume exception processing, marking the exception as "unhandled" by the
current handler, thus giving the next exception port in the search order a
chance to process the exception. An example of when one would do this is
when the exception is not one the handler intends to process.

```cpp
  auto status = zx_task_resume_from_exception(thread, eport,
      ZX_RESUME_TRY_NEXT);
  // ... check status ...
```

If there are no remaining exception ports to try the kernel terminates
the process, as if *zx_task_kill(process)* was called.
The return code of a process terminated by an exception is an
unspecified non-zero value.
The return code can be obtained with *zx_object_get_info(ZX_INFO_PROCESS)*.
Example:

```cpp
    zx_info_process_t info;
    auto status = zx_object_get_info(process, ZX_INFO_PROCESS, &info,
                                     sizeof(info), nullptr, nullptr);
    // ... check status ...
    int return_code = info.return_code;
```

Resuming the thread requires a handle of the thread, which the handler
may not yet have. The handle is obtained with the
[**object_get_child**() system call](syscalls/object_get_child.md).
The pid,tid necessary to look up the thread are contained in the
exception report. See the above trivial exception handler example.

## Exception search order

Exception ports are searched in the following order:

- *Debugger* - The debugger exception port is associated with processes, and
is for things like zxdb and gdb. To bind to the debugger exception port
pass **ZX_EXCEPTION_PORT_DEBUGGER** in *options* when binding an
exception port to the process.
There is only one debugger exception port per process.

- *Thread* - This is for exception ports bound directly to the thread.
There is only one thread exception port per thread.

- *Process* - This is for exception ports bound directly to the process.
There is only one process exception port per process.

- *Job* - This is for exception ports bound to the process's job. Note that
jobs have a hierarchy. First the process's job is searched. If it has a bound
exception port then the exception is delivered to that port. If it does not
have a bound exception port, or if the handler returns **ZX_RESUME_TRY_NEXT**,
then that job's parent job is searched, and so on right up to the root job.
There is only one job exception port per job.

If no exception port handles the exception then the kernel finishes
exception processing by killing the process.

Notes:

- The search order is different than that of Mach. In Zircon the
debugger exception port is tried first, before all other ports.
This is useful for at least a few reasons:

    - Allows "fix and continue" debugging. E.g., if a thread gets a segfault,
      the debugger user can fix the segfault and resume the thread before the
      thread even knows it got a segfault.
    - Makes debugger breakpoints easier to reason about.

## Types of exceptions

At a high level there are two types of exceptions: architectural and
synthetic.
Architectural exceptions are things like a segment fault (e.g., dereferencing
the NULL pointer) or executing an undefined instruction. Synthetic exceptions
are things like thread start and exit notifications.

Exception types are enumerated in the *zx_excp_type_t* enum defined
in [`<zircon/syscalls/exception.h>`](../system/public/zircon/syscalls/exception.h).

Some exceptions are debugger specific, and are only sent to the
debugger exception port. These exceptions are:

- **ZX_EXCP_THREAD_STARTING**
- **ZX_EXCP_THREAD_EXITING**

## Interaction with thread suspension

Exceptions and thread suspensions are treated separately.
In other words, a thread can be both in an exception and be suspended.
This can happen if the thread is suspended while waiting for a response
from an exception handler. The thread stays paused until it is resumed
for both the exception and the suspension:

```cpp
  auto status = zx_task_resume_from_exception(thread, eport, 0);
  // ... check status ...
```

and one for the suspension:

```cpp
  // suspend_token was obtained by an earlier call to zx_task_suspend().
  auto status = zx_handle_close(suspend_token);
  // ... check status ...
```

The order does not matter.

## Signals

Signals are the core Zircon mechanism for observing state changes on
kernel Objects (a Channel becoming readable, a Process terminating,
an Event becoming signaled, etc). See ["signals"](signals.md).

Unlike exceptions, signals do not require a response from an exception handler.
On the other hand signals are sent to whomever is waiting on the thread's
handle, instead of being sent to the exception port that could be
bound to the thread's process.
This is generally not a problem for exception handlers because they generally
keep track of thread handles anyway. For example, they need the thread handle
to resume the thread after an exception.

It does, however, mean that an exception handler must wait on the
port *and* every thread handle that it wishes to monitor.
Fortunately, one can reduce this to continuing to just have to wait
on the port by using the
[**object_wait_async**() system call](syscalls/object_wait_async.md)
to have signals regarding each thread sent to the port.
In other words, there is still just one system call involved to wait
for something interesting to happen.

```cpp
  uint64_t key = some_key_denoting_the_thread;
  bool is_suspended = thread_is_suspended(thread);
  zx_signals_t signals = ZX_THREAD_TERMINATED;
  if (is_suspended)
    signals |= ZX_THREAD_RUNNING;
  else
    signals |= ZX_THREAD_SUSPENDED;
  uint32_t options = ZX_WAIT_ASYNC_ONCE;
  auto status = zx_object_wait_async(thread, eport, key, signals, options);
  // ... check status ...
```

When the thread gets any of the specified signals a **ZX_PKT_TYPE_SIGNAL_ONE**
packet will be sent to the port. After processing the signal the above
call to **zx_object_wait_async**() must be done again, that is the nature
of **ZX_WAIT_ASYNC_ONCE**.

*Note:* There is both an exception and a signal for thread termination.
The **ZX_EXCP_THREAD_EXITING** exception is sent first. When the thread
is finally terminated the **ZX_THREAD_TERMINATED** signal is sent.

The following signals are relevant to exception handlers:

- **ZX_THREAD_TERMINATED**
- **ZX_THREAD_SUSPENDED**
- **ZX_THREAD_RUNNING**

When a thread is started **ZX_THREAD_RUNNING** is asserted.
When it is suspended **ZX_THREAD_RUNNING** is deasserted, and
**ZX_THREAD_SUSPENDED** is asserted. When the thread is resumed
**ZX_THREAD_SUSPENDED** is deasserted and **ZX_THREAD_RUNNING** is
asserted. When a thread terminates both **ZX_THREAD_RUNNING** and
**ZX_THREAD_SUSPENDED** are deasserted and **ZX_THREAD_TERMINATED**
is asserted. However, signals are OR'd into the state maintained by
the port thus you may see any combination of requested signals
when **zx_port_wait**() returns.

## Comparison with Posix (and Linux)

This table shows equivalent terms, types, and function calls between
Zircon and Posix/Linux for exceptions and the kinds of things exception
handlers generally do.

```
Zircon                       Posix/Linux
------                       -----------
Exception/Signal             Signal
ZX_EXCP_*                    SIG*
task_bind_exception_port()   ptrace(ATTACH,DETACH)
task_resume()                kill(SIGCONT),ptrace(CONT)
task_suspend()               kill(SIGSTOP),ptrace(KILL(SIGSTOP))
N/A                          kill(everything_other_than_SIGKILL)
task_kill()                  kill(SIGKILL)
TBD                          signal()/sigaction()
port_wait()                  wait*()
TBD                          W*() macros from sys/wait.h
zx_packet_exception_t        siginfo_t
zx_exception_context_t       siginfo_t
thread_read_state            ptrace(GETREGS,GETREGSET)
thread_write_state           ptrace(SETREGS,SETREGSET)
process_read_memory          ptrace(PEEKTEXT)
process_write_memory         ptrace(POKETEXT)
```

Zircon does not have asynchronous signals like SIGINT, SIGQUIT, SIGTERM,
SIGUSR1, SIGUSR2, and so on.

Another significant different from Posix is that the exception handler
is always run on a separate thread.

## Example programs

There are three good example programs in the Zircon tree to use to
further one's understanding of exceptions and signals in Zircon.

- `system/core/crashsvc`, `system/core/crashanalyzer`

`crashsvc` is the main crash logging service. It delegates the
processing of the crash to various programs.
One of those programs is `crashanalyzer` which prints a backtrace
of the crashing thread.

- `system/utest/exception`

The basic exception handling testcase.

- `system/utest/debugger`

Testcase for the rest of the system calls a debugger would use, beyond
those exercised by system/utest/exception.
There are tests for segfault recovery, reading/writing thread registers,
reading/writing process memory, as well as various other tests.

## Todo

There are a few outstanding issues:

- signal()/sigaction() replacement

In Posix one is able to specify handlers for particular signals,
whereas in Zircon there is currently just the exception port,
and the handler is expected to understand all possible exceptions.
This is tracked as ZX-560.

- W*() macros from sys/wait.h

When a process exits because of an exception, no information is provided
on which exception the process got (e.g., segfault). At present only a
non-specific non-zero exit code is returned.
This is tracked as ZX-1974.

- more selectiveness in which exceptions to see

In addition to ZX-560 IWBN to be able to specify to the kernel
when binding the exception port that one is only interested in
seeing a particular subset of exceptions.
This is tracked as ZX-990.

- ability to say exception ports unbind quietly when closed

The default behaviour when a port is unbound implicitly due to
the port being closed is to resume exception processing, i.e.,
given the next exception port in the search order a try.
In debugging sessions it is useful to change the default behavior
and have the port unbound "quietly", in other words leave things as
is, with the thread still waiting for an exception response.
This is because debuggers can crash, and obliterating an active debugging
session is counterproductive.
This is tracked as ZX-988.

- rights for binding exception ports and getting debuggable thread handles

In Zircon rights can, in general, only be taken away, they can't be added.
However, one doesn't want to have "debuggability" a default right:
debuggers are privileged processes. Thus we need a way to obtain handles
with sufficient rights for debugging.
This is tracked as ZX-509, ZX-911, and ZX-923.

- strace?

There is currently no way to trace syscalls like there is in Linux.
The typical way this would be implemented is with syscall start/end
synthetic exceptions.
It's a nice feature, but it's not necessary. Plus while useful,
strace operates at the syscall layer and thus is confusing when
trying to trace things like fork, which is no longer implemented
as a syscall. Since every syscall in Zircon is via the vdso, it
makes more sense to implement this by having breakpoints on all
the relevant vdso entry points.
This is tracked as ZX-567.

- restrictions on **zx_task_resume**()

This is tracked as ZX-562. The basic discussion is about only allowing
appropriate processes to resume a thread in an exception.

- no way to obtain currently bound port or to chain handlers

Currently, there's no way to get the currently bound exception port.
Possible use-cases are for debugging purposes (e.g, to see what's going on
in the system).
Another possible use-case is to allow chaining exception handlers, though for
the case of in-process chaining it's likely better to use a
signal()/sigaction() replacement (see ZX-560).
This is tracked as ZX-1216.

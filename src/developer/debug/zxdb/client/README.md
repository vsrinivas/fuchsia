# Zxdb client library

This is a library for writing debugger frontends. The client library provides a
way to connect to a remote Fuchsia system's running debug agent. It maintains
the objects that represent running processes, threads, and stack frames, and
provides a way to query and control these things.

## Main objects

From largest to smallest.

### Session

This is the main global object. It represents the connection to the remote
system and provides functions to `Connect` and `Disconnect`.

The basic debugger initialization is to make a `Session` object and run the
message loop (in `src/developer/debug/shared`).

The IPC layer is represented by the `RemoteAPI` which the Session maintains. The
`RemoteAPI` is often mocked for testing.

### System

This is the client representation of the remote system. Its main job is to
maintain the list of `Target` objects. It also maintains breakpoints and
filters. There is a 1:1 correspondence between Session and System (they could be
the same object but we split them between "connection-related" and
"OS-state-related" to keep things smaller).

### Target

A target is a holder for a process. The process itself may or may not be
running. Requests to start or kill a process are on the target and there is
always at least one in the `System`. The target also maintains state that needs
to survive across process launches like command line arguments.

When running, the `Target` will have a `Process` object.

### Process

A running process on the remote system. It holds the running threads and
provides an API to read and write memory. There are also functions to do
process-wide suspend and resume.

In the console frontend, a "process" noun actually represents a target in the
client library. The console frontend process may or may not be running or
attached, while a client `Process` object always represents a running process.

### Thread

A running thread in a process. It provides a nonempty `Stack` when paused, and
the main stepping logic is here (see "Thread Control" below).

### Stack

The backtrace of a `Thread` when the thread is paused. At its simplest it's a
list of stack frames, but there is a lot of extra complexity around handling of
inline stack frames.

In our system when a thread stops at an exception only the top two stack frames
are sent from the `debug_agent` running on the remote system. This makes things
faster since many exceptions are internally handled to implement stepping and
unwinding the whole stack can take time. If you need the whole stack you will
need to asynchronously `SyncFrames`.

### Frame

One stack frame. Some stack frames are real and some represent inline function
calls without any real information on the stack. Usually when you want to get a
variable or evaluate an expression you do it in the context of a stack frame.
You do this by getting the frame's `EvalContext`.

## Other objects

### Breakpoint

Breakpoints are global and are maintained by the System. You can have process-
and thread-specific ones but those states are a property of the breakpoint. The
breakpoint objects themselves are still global.

### Job

Job is used to watch for process launches in Zircon.

### Filter

A filter is a way to watch for processes on jobs and automatically attach when
the name matches.

## Thread control

Stepping is implemented by `ThreadController` objects. To do a certain kind of
step operation on a thread, you instantiate the type of `ThreadController` that
implements the operation you want and call `Thread::ContinueWith` to run that
controller. The thread controller will watch for exceptions and resume as
necessary to implement its operation.

The main ones are:

  * `FinishThreadController`
  * `StepOverThreadController`
  * `StepThreadController` (implements various kinds of "step into")
  * `UntilThreadController`

Thread controllers are nested so that you can do a "step over", hit an
exception, complete some other steppings, and have the thread still stop
when the original "step over" has completed.

## Expressions

Expression and variable evaluation is provided by the `expr` library. To run
this you give it an `EvalContext` in which to do it's job. This context is most
commonly provided by a `Frame` which provides access to the local variables and
all process state.

# input_pipeline > Coding

Reviewed on: 2022-03-22

## Shared-mutable data structures

Rust provides multiple options for how to manage ownership of shared data structures
(e.g. `Rc`, `Arc`), and multiple options for how to ensure memory safety when mutating
shared data structures (e.g. `RefCell`, `std::sync::Mutex`).

This section describes the recommended options for code within this library.
If these options seem unsuitable for your use case, please ask a reviewer _before_
writing code that uses some other option.

1. `InputHandler`s _should_ be wrapped in `Rc` instead of `Arc`. This is primarily
   for semantic consistency with the fact that `InputHandler`s are not guaranteed
   to be `Send`, but may have some performance benefits as well.

1. Implementations of `InputHandler` _should_ wrap mutable state in a `RefCell`,
   rather than a `std::sync::Mutex`, `futures::lock::Mutex`, or `parking_lot::Mutex`.
   * The primary benefit of `RefCell` over the mutexes is that, if a program
     tries to concurrently access the data in incompatible ways
     (`borrow()` + `borrow_mut()`), the program will panic instead of deadlocking.
   * A secondary benefit is that there's no confusion over which wrapper is
     being used. (`Mutex` is very confusing given that there are three commonly
     used implementations.)
   * A teritary benefit, largely speculative, is that performance may be better.
     As with `Rc` vs. `Arc`, `RefCell` doesn't require cross-core synchronization.

## Background tasks

In some cases, a pipeline stage wants to do work independently of newly arriving
input events. For example, the `autorepeater` wants to periodically send new key
events when a key has been pressed but _not_ released.

Because the work is independent of newly arriving events, there's no natural way
to integrate the autorepeat logic (wait for a timer, send an event) with
`InputHandler::handle_input_event()`.

For this reason, the autorepeat logic uses the Fuchsia Async library's `Task`
facility.

This section describes the recommended way to use background tasks within this
library. If this seems unsuitable for your use case, please ask a reviewer
_before_ writing code that uses some other option.

1. Use `fuchsia_async::Task::local()`, rather than `fuchsia_async::Task::spawn()`.
   Given that the input pipeline code [runs on a `LocalExecutor`](parallelism.md),
   the two functions for creating a task will behave identically. However,
   `Task::local()` better documents the fact that the code was not written, reviewed,
   or tested with multithreaded use in mind.

1. Store the `Task` within a data structure (e.g. the `struct` that created the `Task`),
   rather than `detach()`-ing the `Task`. Having the `Task` within the data structure
   is a convenient and low-effort way of documenting that the struct is active, rather
   than inert. Said differently: the `Task` field in the struct makes it obvious that
   the struct may act spontaneously, rather than, e.g. only responding to function calls.

Note: some existing code does not follow these recommendations. Such code will be
migrated over time.

## Re-entrancy

Some `InputHandler`s need to maintain state which depends on the order in which
events are received. If `handle_input_event()` could be invoked re-entrantly,
a handler would probably need to buffer the re-entrant calls internally, to
avoid corrupting mutable state.

Instead, the `handle_input_event()` API is documented as _not_ being reentrant-safe,
and the `InputPipeline` struct always waits for an `InputHandler` to complete
processing an `InputEvent`, before invoking `handle_input_event()` again on the
same handler.

Consequently, `InputHandler`s _should_ assume that `handle_input_event()`
is not invoked concurrently.

In addition to avoiding the need for internal buffering, this may also simplify
reasoning about the correctness of the use of `RefCell::borrow()` and
`RefCell::borrow_mut()`.

For example, for some handlers, code in `handle_input_event()` and its callees
_may_ be able to assume that data read while holding a `RefCell::borrow()` or
`RefCell::borrow_mut()` guard remains unchanged even after releasing the guard
and yielding (via the `await` operator).

> NOTE: Such an assumption is _only_ appropriate if
> 1. The handler does _not_ create `fuchsia_async::Task`s which can modify the
>    mutable state behind the `RefCell`, OR
> 2. It is semantically correct for the `handle_input_event()` `Future`
>    to use the old value.
>
> Otherwise, the other `Task`s may execute while `handle_input_event()` is
> suspended, and the state cached in `handle_input_event()` `Future` will be
> (incorrectly) used instead of the value updated by the other `Task`s.

## Error handling (implementation code)

In the `workstation` product, the input pipeline library runs in the same
process (`scene_manager`) as the code that sets up Scenic. Hence, if an
`InputHandler` calls `panic!()` (or methods such as `unwrap()` or `expect()` on
`None`/`Err` variants), all graphical programs will terminate.

Hence, implementation code should avoid aborting the program, and should,
instead, propagate errors to its caller (e.g. for callees of `handle_input_event()`),
or log errors messages (for `Task`s that run independently of `handle_input_event()`).

Note that keeping the program running allows the user has the opportunity to
continue their work using other input modalities, which don't depend on the
erroneous handler.

Some existing handlers use `panic!()`, `unwrap()`, and `expect()`. Such code
will be migrated over time.

## Error handling (test code)

When a test aborts, the test runner outputs a backtrace, which is helpful
for debugging the test. When a test returns an `Err`, however, the test
runner does not print a backtrace.

Hence, test code, should _prefer_ to abort, rather than propagating errors.

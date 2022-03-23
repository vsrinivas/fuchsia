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

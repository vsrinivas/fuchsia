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
events are received. If [`handle_input_event()`](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/input_handler.rs;drc=736d1cff60799806705e26b3473457acbfb31bb7;l=30) could be invoked re-entrantly, a handler would probably need to buffer the re-entrant
calls internally, to avoid corrupting mutable state.

Instead, the `handle_input_event()` API is [documented as _not_ being reentrant-safe](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/input_handler.rs?q=%22should%20not%20be%20invoked%20concurrently%22),
and the `InputPipeline` struct [always waits for an `InputHandler` to complete
processing](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/input_pipeline.rs?q=handle_input_event%5C(event%5C).await&ss=fuchsia%2Ffuchsia) an `InputEvent`,
before invoking `handle_input_event()` again on the same handler.

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

## Blocking

Implementations of [`InputHandler::handle_input_event()`][handle-input-event]
should avoid blocking unnecessarily, as that prevents [`InputEvent`s][input-event]
from propagating to downstream handlers in a timely manner.

For example, consider [`MediaButtonsHandler`][media-buttons-handler], which
handles [`ConsumerControlsEvent`][consumer-controls-event]s by sending events to
[`fuchsia.ui.policy.MediaButtonsListener`][media-buttons-listener] channels.

After https://fxbug.dev/106843 is resolved, we will consider a [`ConsumerControlsEvent`][consumer-controls-event]
handled when the [`MediaButtonsHandler`][media-buttons-handler]
has invoked the [`OnEvent()`][on-event-fidl] method on all listeners, even if the
listeners have not yet acknowledged the call.

By doing so, we will unblock handling of events intended for handlers downstream
of [`MediaButtonsHandler`][media-buttons-handler]. In particular, [`TouchInjectorHandler`][touch-injector-handler]
will be able to process [`TouchScreenEvent`s][touchscreen-event]
before the [`MediaButtonsListener`s][media-buttons-listener] have acknowledged
the media buttons event.

Handlers _should_ still listen for acknowledgements, to prevent unread ACKs from
accumulating in the associated Zircon channel. However, that work can be done off
of the critical path of `handle_input_event()`.

## Unresponsive clients in general

Beyond the advice about blocking above, we don't have any advice about how to deal with
unresponsive clients. For example, we do not yet have enough experience to recommend
that handlers should, or should not, time out a client that fails to acknowledge a
message after a set period of time.

[consumer-controls-event]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/consumer_controls_binding.rs;l=17
[handle-input-event]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/input_handler.rs;l=30
[input-event]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/input_device.rs;l=31
[media-buttons-handler]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/media_buttons_handler.rs;l=20
[media-buttons-listener]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.policy/device_listener.fidl;l=32
[on-event-fidl]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.policy/device_listener.fidl;l=40
[touch-injector-handler]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/touch_injector_handler.rs;l=25
[touchscreen-event]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/touch_binding.rs;l=25
# Applications

The application structure is central to the implementation of Carnelian, but has very little
interface.

Instead, the
[AppAssistant](https://fuchsia-docs.firebaseapp.com/rust/carnelian/app/trait.AppAssistant.html)
trait is the point where a Carnelian component developer changes the default behavior of the
framework.

At the heart of every Carnelian app is an [asynchronous, multi-producer, single-consumer
queue](https://docs.rs/futures/latest/futures/channel/mpsc/index.html) that is used to funnel all
asynchronous events to a single point for processing.

The sending side of this queue is encapsulated in the [`AppSender`][1] struct. Applications are
passed a [`AppSender`][1] at creation.

Carnelian apps are inherently single-threaded. For those apps that wish to use additional threads,
the [`AppSender`][1] struct can create a thread-safe multi-producer, single-consumer queue to allow
communication from additional threads back to the main thread.

## Notes

When starting up, Carnelian attempts to open the [display
controller](./glossary.md#display-controller) to decide whether it should use the display
controller or Scenic.

Support for the GFX flavor of Scenic should be removed as soon as possible.

## Concerns

### AppSender

Passing an [`AppSender`][1] everywhere one might eventually need requires a lot of boilerplate.
There's nothing about [`AppSender`][1] that prevents it from being globally available using a
thread-local variable.

### Unneeded Flexibility

Compiled Carnelian apps currently can run either with our without Scenic. It's not clear this
runtime flexibility is worth the code size cost. See
[this abandoned CL](https://fuchsia-review.googlesource.com/c/fuchsia/+/577065)
for an approach to make the capability decided at compile-time.

[1]: https://fuchsia-docs.firebaseapp.com/rust/carnelian/app/struct.AppSender.html

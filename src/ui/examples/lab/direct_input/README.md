# DirectInput

## What is it?

This is a program that exercises Scenic's input subsystem. It directly talks to
Scenic, and runs without the help of a root presenter. Other presenters should
be disabled while this program runs.

## What should I expect to see?

You should see two Views, one vended by direct_input, and an inset View vended by
direct_input_child. The functionality is the same: display a finger tracker for
each finger on the touchscreen, and "blink" when a keyboard input is received by
one of the Views.

## How do I run it?

Run it like this:

```
$ run direct_input [--verbose=1]
```

## How is it structured?

There are two packages, each containing an independent binary.

### DirectInput

Some high-level functionality provided by DirectInput:

* Receive low-level input reports from Zircon, via an
[InputReader](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/ui/lib/input_report_reader/input_reader.h).
* Manage input devices and associated state, via
[InputDeviceImpl](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/lib/ui/input/input_device_impl.h)
and
[DeviceState](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/lib/ui/input/device_state.h).
* Transform low-level input report into a FIDL
[InputEvent](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.ui.input/input_events.fidl)
struct, via DeviceState.

It forwards InputEvents **to** Scenic to be routed to the correct destination
based on hit testing. Scenic will send an InputEvent to a
[View](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/ui/scenic/lib/gfx/resources/view.h)
over its
[SessionListener](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.ui.scenic/session.fidl),
as a
[session event](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.ui.scenic/events.fidl).

To receive InputEvents **from** Scenic, DirectInput implements the following
callback, given to its session as the event handler:

```C++
void App::OnSessionEvents(fidl::VectorPtr<fuchsia::ui::scenic::Event> events);
```

On receipt of an InputEvent from Scenic, we switch based on its type: Focus,
Pointer, or Keyboard.

*   Focus events are sent first, and we respond to it by displaying a "focus
    frame" as a visual aid to the user. The focus frame is removed on a defocus
    event.
*   Pointer events are sent after, and we respond by displaying a per-finger
    circle to indicate active tracking. The finger tracker is removed on a touch
    pointer's `UP` event.
*   Keyboard events are sent to the focused View, and we respond to it by
    briefly "blinking" the focus frame.

To exercise the multi-View case, DirectInput requests a child program to be
launched, and sets up a ViewHolder into which the child can vend its View.
DirectInput requests the child to *create* a View into the provided ViewHolder
through the
[ViewProvider](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.ui.app/view_provider.fidl)
interface, implemented by the child.

### DirectInputChild

DirectInputChild represents a typical user application &mdash; e.g., there is no
logic to manage input devices and input reports. Instead, it implements the
ViewProvider interface (allowing other programs to request we create a View) and
sets up its UI logic into its View.

The specifics of how DirectInputChild handles InputEvents are identical to
DirectInputChild. However, we deliberately keep the code independent and duplicated so
that you can play around with each View independently of the other.

# Display ownership in [Fuchsia][fx]'s input pipeline

[fx]: https://fuchsia.dev

> **Summary.** This document explains the design of the display ownership.

## Problem

Fuchsia's input is sometimes multiplexed between two subsystems: the graphics
subsystem, which is managed by [Scenic][scenic], and the [virtcon][vc], the
virtual console.

Without special care, that would lead to events being processed by both
`virtcon`, and the input pipeline. That would manifest, e.g., as having
characters that were typed into `virtcon` also appear in a text box
in Chrome.

## Solution 

Scenic sets aside a kernel [Event][ev] that it uses to signal whether
it owns the display or not.  This Event can be obtained by calling
[`fuchsia.ui.scenic/Scenic.GetDisplayOwnershipEvent`][doe].

The [DisplayOwnership struct][disp-own] watches this event to learn when
Scenic loses and gains ownership of the display. When `DisplayOwnership` detects
that Scenic has lost ownership, `DisplayOwnership` marks all keyboard events that
it subsequently sees as handled.  This makes all the input pipeline stages downstream
see, but ignore the event. This way we ensure that effectively the keyboard events
are forwarded either to the clients of the input pipeline, or to the virtcon,
but not both.

Note that `DisplayOwnership` deals only in keyboard events. Other event
types are handled as follows:
* Mouse, touchpad, and touchscreen: these events are always forwarded to
  Scenic. This is okay, because Scenic already knows when it has lost ownership
  of the display, and can discard these events accordingly. 
* `ConsumerControls`: these events are always forwarded to clients of the
  input pipeline, as:
  * they [have no meaning to `virtcon`][virtcon-no-cc]
  * although [the recovery UI uses these events][recovery-cc], input-pipeline
    does not run alongside the recovery UI

## Limitations

The [DisplayOwnership struct][disp-own] does not obtain the kernel Event that
signals display ownership from Scenic itself.  Rather, the client code for the
input pipeline library is expected to obtain this Event first, and pass it down
to the constructor function `DisplayOwnership::new`.

This means that the display ownership does not tolerate Scenic restarts
as written today.

[ev]: https://fuchsia.dev/fuchsia-src/reference/kernel_objects/event
[scenic]: https://fuchsia.dev/fuchsia-src/development/graphics/scenic
[vc]: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0094_carnelian_virtcon
[doe]: https://fuchsia.dev/reference/fidl/fuchsia.ui.scenic#Scenic.GetDisplayOwnershipEvent
[disp-own]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/display_ownership.rs?q=%22struct%20DisplayOwnership%22
[virtcon-no-cc]: https://cs.opensource.google/search?q=input::&sq=&ss=fuchsia%2Ffuchsia:src%2Fbringup%2Fbin%2Fvirtcon%2Fsrc%2F
[recovery-cc]: https://cs.opensource.google/search?q=file:recovery%20consumercontrol&sq=&ss=fuchsia%2Ffuchsia
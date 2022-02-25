# Display ownership handler in [Fuchsia][fx]'s input pipeline

[fx]: https://fuchsia.dev

> **Summary.** This document explains the design of the display ownership
> handler.

## Introduction

Fuchsia's input is sometimes multiplexed between two subsystems: the graphics
subsystem, which is managed by [Scenic][scenic], and the [virtcon][vc], the
virtual console. Scenic sets aside a kernel [Event][ev] that it uses to signal whether
it owns the display or not.  This Event can be obtained by calling
[`fuchsia.ui.scenic/Scenic.GetDisplayOwnershipEvent`][doe].

## Display ownership handler

The display ownership handler does not obtain the display ownership Event from
Scenic itself. Rather, the client code for the input pipeline library is
expected to obtain this Event first, and pass it down to the constructor
function `DisplayOwnershipHandler::new`.

This means that the display ownership handler does not tolerate Scenic restarts
as written today.

[ev]: https://fuchsia.dev/fuchsia-src/reference/kernel_objects/event
[scenic]: https://fuchsia.dev/fuchsia-src/development/graphics/scenic
[vc]: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0094_carnelian_virtcon
[doe]: https://fuchsia.dev/reference/fidl/fuchsia.ui.scenic?hl=en#Scenic.GetDisplayOwnershipEvent

When the display ownership handler detects that the display ownership has been
lost, it marks all events that it subsequently sees as handled.  This makes
all the input pipeline stages downstream of this handler see, but ignore the
event. This way we ensure that effectively the input events are forwarded either
to the clients of the input pipeline, or to the virtcon, but not both.


# Life of Mouse Event

This document explains how mouse events are processed on the workstation
product. The first section describes drivers, and subsequent sections work up
the software stack.

## Driver

[`src/ui/input/lib/hid-input-report/mouse.cc`][1] parses
[USB Mouse HID input reports][6] and translates them to FIDL
[`sdk/fidl/fuchsia.input.report/mouse`][2]. [More details][23].

## Input Pipeline

Input Pipeline ([`src/ui/lib/input_pipeline/`][3]) [`watch_for_devices`][4] and
[add bindings based on device type][5] to receive input report events and
translate to Input Pipeline internal events, then more handlers in Input
Pipeline.

[`MouseBinding`][7] receives [`sdk/fidl/fuchsia.input.report/`][8] and convert
them to `mouse_binding::MouseEvent`, then send to
[handlers in Input Pipeline][24].

Handlers in Input Pipeline process events in order:

1.  [`ClickDragHandler`][9] translate events to `RelativeMouseEvent` if the
    event belongs to a click and drag sequence. It improves the disambiguation
    of click vs. drag events. We may need to revisit this handler when touchpad
    support is done.
1.  [`PointerMotionScaleHandler`][10] scale the mouse movement based on scale
    factor. Scale factor is passed to `PointerMotionScaleHandler` when created.
1.  [`MouseInjectHandler`][11] [`update_cursor_renderer`][22] (eg. position,
    visibility) and converts `mouse_binding::MouseEvent` to
    [`fuchsia.ui.pointerinjector.PointerSample`][12] and send to `Scenic`.

## Scenic

[`MouseInjector`][13] converts `fuchsia.ui.pointerinjector.PointerSample` to
[`scenic_impl::input::InternalMouseEvent`][14].

[`MouseSourceBase`][15] converts `scenic_impl::input::InternalMouseEvent` to
[`fuchsia.ui.pointer.MouseEvent`][16] and send to clients in
[`MouseSourceBase::UpdateStream`][21].

## Clients

Most clients listen to `fuchsia.ui.pointer.MouseEvent` events. A special case is
Carnelian Terminal.

### Carnelian Terminal

-   When running under the graphical shell (Ermine), it uses
    [`fuchsia.ui.pointer`][17], like other clients.
-   In modes that have [minimal component topologies][25] (e.g.,
    `virtcon`/`recovery`), it runs directly on the display driver and reads
    [`fuchsia.input.report`][18] from the input driver.

### Links

-   Chromium handles Fuchsia input events in [`ui/events/fuchsia/`][19].
-   Flutter handles Fuchsia input events in
    [`shell/platform/fuchsia/flutter/pointer_delegate.cc`][20].
-   Carnelian handles Fuchsia input events in
    [`src/lib/ui/carnelian/src/input/flatland.rs`][17] and
    [`src/lib/ui/carnelian/src/input/report.rs`][18].

<!-- xrefs -->

[1]: /src/ui/input/lib/hid-input-report/mouse.cc
[2]: /sdk/fidl/fuchsia.input.report/mouse.fidl
[3]: /src/ui/lib/input_pipeline/
[4]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/input_pipeline.rs?q=%22fn%20watch_for_devices%22
[5]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/input_device.rs?q=%22fn%20get_device_binding%22
[6]: https://www.usb.org/hid
[7]: /src/ui/lib/input_pipeline/src/mouse_binding.rs
[8]: /sdk/fidl/fuchsia.input.report/
[9]: /src/ui/lib/input_pipeline/src/click_drag_handler.rs
[10]: /src/ui/lib/input_pipeline/src/pointer_motion_scale_handler.rs
[11]: /src/ui/lib/input_pipeline/src/mouse_injector_handler.rs
[12]: /sdk/fidl/fuchsia.ui.pointerinjector/
[13]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/scenic/lib/input/mouse_injector.cc?q=%22InternalMouseEvent%20MouseInjector::PointerInjectorEventToInternalMouseEvent%22
[14]: /src/ui/scenic/lib/input/internal_pointer_event.h
[15]: https://cs.opensource.google/search?q=%22fuchsia::ui::pointer::MousePointerSample%20MouseSourceBase::NewPointerSample%22
[16]: /sdk/fidl/fuchsia.ui.pointer/mouse.fidl
[17]: /src/lib/ui/carnelian/src/input/flatland.rs
[18]: /src/lib/ui/carnelian/src/input/report.rs
[19]: https://source.chromium.org/chromium/chromium/src/+/main:ui/events/fuchsia
[20]: https://github.com/flutter/engine/blob/main/shell/platform/fuchsia/flutter/pointer_delegate.cc
[21]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/scenic/lib/input/mouse_source_base.cc?q=%22MouseSourceBase::UpdateStream%22
[22]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/mouse_injector_handler.rs?q=%22fn%20update_cursor_renderer%22
[23]: /doc/development/drivers/concepts/driver_architectures/input_drivers/input.md
[24]: /doc/contribute/governance/rfcs/0096_user_input_arch#input-pipeline.md
[25]: /doc/contribute/governance/rfcs/0094_carnelian_virtcon.md

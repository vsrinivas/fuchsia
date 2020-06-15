# Fuchsia Input Report

Note: This is a low-level input API and it should only be used by trusted system
programs.  Most applications should use the real input stack (either
[`RootPresenter`](/docs/concepts/graphics/scenic/input.md) or
[`SessionFramework`](/docs/concepts/session/input.md))
to get things like IME, localization, and proper input focus.

`fuchsia.input.report` is the lowest level of structured input in the Fuchsia
system. This API maps as closely as possible to the hardware of input devices
while still being easy to use and configure. It is heavily inspired by the HID
standard.

For more information see the
[Fuchsia Input Drivers](/docs/concepts/drivers/input.md) document.

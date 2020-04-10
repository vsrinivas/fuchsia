# measure-tape

> Note: As of right now, this tool is specifically designed for use by Scenic.

`measure-tape` is a code generator creating a function meant to measure
the size of a specific target type, both in terms of bytes and handles. We
call this function a "measuring tape".

This tool is currently limited to creating measuring tapes for the HLCPP
bindings only.

## Build and Run

    fx set core.x64 --with //tools/fidl/measure-tape:host
    fx build

Then

    ./out/default/host_x64/measure-tape \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.images/fuchsia.images.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.gfx/fuchsia.ui.gfx.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.input/fuchsia.ui.input.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.scenic/fuchsia.ui.scenic.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.views/fuchsia.ui.views.fidl.json \
        --measure fuchsia.ui.scenic/Command \
        --out-cc sdk/lib/ui/scenic/cpp/commands_sizing.cc

## Testing

**tl;dr** See `commands_sizing_test.cc`.

Currently, the tool has been designed to measure `fuchsia.ui.scenic/Command`
unions. While the tool is general purpose, and with some small tweaks will be
able to be reused for other similar uses, it is currently single focused.

Testing of the tool relies on Scenic tests, and in particular

    fx test scenic_cpp_unittests

To which `commands_sizing_test.cc` has been added.

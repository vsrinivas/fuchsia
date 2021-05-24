# Introduction

Carnelian is a prototype framework for writing Fuchsia modules in Rust.

To build the included samples, use the fx set line below to build the
core version of Fuchsia with the necessary additional packages to run either
directly on the frame buffer or with scenic using the tiles tool. See
[these configuration](https://fuchsia.dev/fuchsia-src/getting_started#configure-and-build-fuchsia)
instructions for more details.

    fx set core.x64 \
        --with //src/lib/ui/carnelian:examples \
        --with //src/lib/ui/carnelian:carnelian-integration-test \
        --with //src/lib/ui/carnelian:carnelian-fb-integration-test \
        --with //src/lib/ui/carnelian:carnelian-tests \
        --with //src/lib/ui/carnelian:carnelian-layout-tests
        --release \
        --auto-dir \
        --args=rust_cap_lints='"warn"' \
        --cargo-toml-gen

To disable virtcon, add

        --args='dev_bootfs_labels=["//products/kernel_cmdline:virtcon.disable--true"]'

To run an example in virtcon mode, add `:virtcon_config` to `additional_deps` for that
example in `BUILD.gn`

# Examples

The examples directory contains a set of Carnelian example programs.

## Layout-based Examples

These examples demonstrate using scenes, facets and groups to layout user-interface.

### button

This example implements a single button which, when pressed, toggles a rectangle from red to green.

The 'M' key on an attached keyboard will cycle the main axis alignment of the row containing the
indicators. Pressing 'C' will cycle the cross axis alignment of that row. Pressing 'R' will cycle
the main alignment of the column containing both the button and the indicators.

### layout_gallery

This example is a gallery of the existing group layouts as well as housing tests for those layouts.

The 'M' key on an attached keyboard will cycle between layouts modes of stack, flex and button,
where the button layout is a test version of the layout used by the button example.

For stack, hitting the '1' key cycles the alignment of the stack.

For flex, hitting the '1', '2' and '3' keys cycles the various parameters of the flex layout.

Pressing the 'D' key will dump the bounds of the scene's facets for use in test validation.

### spinning_square

The original example, now improved to use layout and demonstrate facet draw order.

Press the space bar on an attached keyboard to toggle the square between sharp and rounded corners.

Press the 'B' key to move the square backwards in facet draw order, or 'F' to move it forwards.

## Scene-based Examples

These example demonstrate scenes, but rather than using layout they take responsbility for
positioning the facets.

### clockface

This example draws a clock face.

### font_metrics

This example displays metrics about certain fonts.

### gamma

This example demonstrate how Carnelian can be used to produce gamma correct output.

### rive

[Rive](https://rive.app) is a file format for animations. This example loads the example from file
and displays it. By default it will load `juice.riv` but the `--file` command line option can be
used to select a different `.riv` file.

### shapes

This example shows how to use pointer input. Press and hold anywhere in the running app to create a
random shape. Drag the pointer to move this shape around. Release the pointer to let the shape fall
towards the bottom of the screen.

### svg

This example loads a vector file in `shed` format and displays it. Press and hold to drag the image
around the screen.

## Render-based Examples

These examples use the render mechanism directly, instead of using scenes and facets.

### ink

This example demonstrate efficient drawing on devices with stylus support.

### png

This example loads a PNG file from disk and displays it. The scene system does not support the post-copy
mechanism used to display pixel, rather than vector, data. Eventually pixel data will be supported
more directly and this sample can be converted to use scenes.

# Tentative Roadmap

1. Flutter-style flex-box layout

## Flutter-style flex-box layout

Currently, layout in Carnelian only supports fixed size facets. The next step will be to allow some
kinds of facets to be flexibly sized.

# Future Areas

## Command Handling

Mature application frameworks usually have some mechanism for commands that might apply to
multiple items in the view hierarchy to be handled by the most specific first and proceeding
to less specific items. This command handling structure can also be used to show/enable menu
items if Fuchsia ever has such a menu.

## Animation

Design and implement a simple animation facility.

# Frequently Asked Questions

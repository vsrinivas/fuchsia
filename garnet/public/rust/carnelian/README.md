# Introduction

Carnelian is a prototype framework for writing Fuchsia modules in Rust.

To build the included samples, add `--available garnet/packages/examples/carnelian` to
your fx set line. `--product terminal` is sufficient to support Carnelian samples.

# Tentative Roadmap

1. Mouse, touch and keyboard input
1. Text rendering with RustType
1. Flutter-style flex-box layout
1. Single-line text editor

## Mouse, touch and keyboard input

Design and implement a way to pass input events from Scenic to app and view assistants.

## Flutter-style flex-box layout

Implement the basics of flex box layout, similar to the way it is done in
[Druid](https://docs.rs/druid/0.1.1/druid/).

# Future Areas

## Command Handling

Mature application frameworks usually have some mechanism for commands that might apply to
multiple items in the view hierarchy to be handled by the most specific first and proceeding
to less specific items. This command handling structure can also be used to show/enable menu
items if Fuchsia ever has such a menu.

## Complex Rendering

Scenic currently allows easy rendering of shapes with colors or textures. One area of exploration
is using the [Lyon path tessellation tool](https://github.com/nical/lyon) to turn arbitrary paths
into triangle meshes for use with Scenic's mesh drawing commands.

## Animation

Design and implement a simple animation facility.

# Frequently Asked Questions

## Nested Calls to App::with()

`App::with` is implemented with a thread-local `RefCell`. After calling the function provided
to `App::with`, any messages queued with `App::queue_message` are sent. If the sending of these
messages results in a call to `App::with` the Carnelian app will be aborted.

This restriction will be removed soon.

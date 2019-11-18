# Introduction

Carnelian is a prototype framework for writing Fuchsia modules in Rust.

To build the included samples, add `--with garnet/packages/examples:carnelian` to
your fx set line. `terminal.x86` is sufficient to support Carnelian samples. See
[these fx set](/docs/getting_started.md#build_fuchsia)
instructions for more details.

# Tentative Roadmap

1. Software Framebuffer and Scenic modes
1. Flutter-style flex-box layout

## Software Framebuffer and Scenic modes

In order to support the UI for software recovery, Carnelian is going to
be able to run some subset of its features in the software-only mode
exposed by the `fuchsia-framebuffer` library. The `drawing` example
runs in this mode, and this task is to modify Carnelian to provide an
abstraction across this and Scenic.

## Flutter-style flex-box layout

Implement the basics of flex box layout, similar to the way it is done in
[Druid](https://docs.rs/druid/0.1.1/druid/).

# Future Areas

## Command Handling

Mature application frameworks usually have some mechanism for commands that might apply to
multiple items in the view hierarchy to be handled by the most specific first and proceeding
to less specific items. This command handling structure can also be used to show/enable menu
items if Fuchsia ever has such a menu.

## Animation

Design and implement a simple animation facility.

# Frequently Asked Questions

## Nested Calls to App::with()

`App::with` is implemented with a thread-local `RefCell`. After calling the function provided
to `App::with`, any messages queued with `App::queue_message` are sent. If the sending of these
messages results in a call to `App::with` the Carnelian app will be aborted.

This restriction will be removed soon.

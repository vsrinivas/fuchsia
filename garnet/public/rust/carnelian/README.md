# Introduction

Carnelian is a prototype framework for writing Fuchsia modules in Rust. It is primarily
intended to be used as the basis of the Ermine session shell.

# Tentative Roadmap

1. Mouse, touch and keyboard input
1. Text rendering with RustType
1. Flutter-style flex-box layout
1. Single-line text editor

## Mouse, touch and keyboard input

Design and implement a way to pass input events from Scenic to app and view assistants.

## Text rendering with RustType

Design and implement a way to render text with RustType and display it with scenic.

## Flutter-style flex-box layout

Implement the basics of flex box layout, similar to the way it is done in
[Druid](https://docs.rs/druid/0.1.1/druid/).

## Single-line text editor

Design and implement a single-line text editor in Rust so that Ermine does not have to
launch a Flutter mod for inputs to use to generate suggestions.

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

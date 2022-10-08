# README

This directory contains all of the example code for the "canvas" case study
series of FIDL examples. In sum, this case study series is meant to serve as a
tour of some of FIDL's most popular patterns around data flow, including topics
like improving protocol performance and flow control.

The canvas store case study features a "baseline" case implementing a simple
canvas that "draws" an (unrendered) set of lines, returning the position of the
bounding box of all things drawn to the canvas when it has done so. This is then
follow by a number of examples demonstrating various possible "extensions" to
that baseline case, including:

* Implementing flow control on messages sent by the client.
* Implementing flow control on messages sent by the server.
* Various changes that improve IPC performance.

Each example should include the following:

* The [FIDL][impl-fidl] definition.
* The [CML][impl-cml] definitions for the client and server.
* The [realm][impl-realm] definition for performing end-to-end testing.
* An [implementation][impl-cpp-natural] of the client and server using the C++ (Natural) bindings.
* An [implementation][impl-cpp-wire] of the client and server using the C++ (Wire) bindings.
* An [implementation][impl-dart] of the client and server using the Dart bindings.
* An [implementation][impl-hlcpp] of the client and server using the HLCPP bindings.
* An [implementation][impl-rust] of the client and server using the Rust bindings.

[impl-cml]: baseline/meta
[impl-cpp-natural]: baseline/cpp-natural
[impl-cpp-wire]: baseline/cpp-wire
[impl-dart]: baseline/dart
[impl-fidl]: baseline/fidl
[impl-hlcpp]: baseline/hlcpp
[impl-rust]: baseline/rust
[impl-realm]: baseline/realm

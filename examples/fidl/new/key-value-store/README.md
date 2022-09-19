# README

This directory contains all of the example code for the "key-value store" case
study series of FIDL examples. In sum, this case study series is meant to serve
as a tour of all of FIDL's non-[resource][rfc-0057] data types.

The key-value store case study features a "baseline" case implementing a simple
write-only key-value store, plus a number of examples demonstrating various
possible "extensions" to that baseline case, including:

* Adding the ability to read an item from the store.
* Adding the ability to do paginated reads of "directories" in the store.
* Adding the ability to look up statistics on items in the store.

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
[rfc-0057]: /docs/contribute/governance/rfcs/0057_default_no_handles.md

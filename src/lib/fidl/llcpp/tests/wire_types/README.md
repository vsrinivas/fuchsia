# Wire types tests

These cover the construction and manipulation of LLCPP domain objects
(wire types). Tests generally should not require running client/server bindings,
such that they are more likely to work on both Fuchsia and host.

See //src/lib/fidl/llcpp/tests/conformance for testing the
encoding and decoding of these domain objects.

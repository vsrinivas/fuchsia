# Protocol test libraries

These are modular building blocks each designed to exercise a particular feature
in FIDL. For example, the `test.unknown.interactions` library contains protocols
that exercise the [unknown interaction][rfc-0138] handling functionality.

Binding tests should be written using these protocols. Sharing the protocol
definitions makes it easier to test all the edge cases, such as different
binding API shapes for zero-arg and single-arg methods. Binding tests may add
binding-specific concerns via additional binding-specific FIDL test libraries.

## Relations to the compatibility test

Parts of the [compatibility test][compatibility-test] may be factorized into
separate libraries here. Individual binding unit tests may then be written on
top, breaking up the compatibility test into facets (https://fxbug.dev/94910).

## Relations to the dynamic test suite

Dynamic test suite project: https://fxbug.dev/102339.

Binding specific unit tests focus on testing the API of generated bindings. The
dynamic test suite focuses on testing the ABI across different bindings. The
FIDL schema behind both tend to have some overlaps. We'll need to wait for both
to grow before evaluating whether sharing the FIDL schema is a good idea.

[rfc-0138]: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0138_handling_unknown_interactions?hl=en
[compatibility-test]: /src/tests/fidl/compatibility/README.md

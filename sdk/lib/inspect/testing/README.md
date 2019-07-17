# Inspect Test Matchers

This document was last reviewed for accuracy on: 2019-07-16

InspectTesting is an extension to the gmock library to support matching
against [Inspect](https://fuchsia.dev/fuchsia-src/development/inspect)
output. You can find examples for using it in the
[Quickstart Guide](https://fuchsia.dev/fuchsia-src/development/inspect/quickstart.md).

## Using

InspectTesting can be used by depending on the `//sdk/lib/inspect/testing/cpp`
GN target and adding `#include <lib/inspect/testing/cpp/inspect.h>` to your C++ source file.

InspectTesting is *not* available in the SDK.

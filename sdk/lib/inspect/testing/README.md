# Inspect Test Matchers

This document was last reviewed for accuracy on: 2020-12-11

InspectTesting is an extension to the gmock library to support matching
against [Inspect](https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect)
output. You can find examples for using it in the
[Quickstart Guide](https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect/quickstart.md).

## Using

* For gtest: InspectTesting can be used with gtest framework by depending on the
`//sdk/lib/inspect/testing/cpp` GN target and adding `#include <lib/inspect/testing/cpp/inspect.h>`
 to your C++ source file.

* For zxtest: InspectTestHelper can be used with zxtest framework by depending on the
`//sdk/lib/inspect/testing/cpp:zxtest` GN target and adding
`#include <lib/inspect/testing/cpp/zxtest/inspect.h>` to your C++ source file. The feature set is
limited when compared to gtest InspectTesting due to limited macros in zxtest.


InspectTesting is *not* available in the SDK

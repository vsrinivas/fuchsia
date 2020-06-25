# Compiling FIDL

## Prerequisites

This tutorial builds on the [Compiling FIDL][fidl-intro] tutorial. For the
full set of FIDL tutorials, refer to the [overview][overview]

## Overview

This tutorial covers how to include the HLCPP FIDL bindings into
code you write by creating a unit test that will serve as a "playground" for
exploring the the HLCPP bindings.

This document covers how to complete the following tasks:

* Write a C++ host test.
* Add the HLCPP bindings of a FIDL library as a build dependency.
* Include the HLCPP bindings into your C++ code.
* Inspect and using the generated bindings code

If you'd like to follow along and write the code yourself, feel free to delete
the example code:

    rm -r examples/fidl/hlcpp/unittests/*

## Write a host test

Add a gtest stub to `examples/fidl/hlcpp/unittests/main.cc`:

```c++
#include <gtest/gtest.h>

namespace {

} // namespace
```

## Define a build target for the host test

Next, create a target that will make it possible to run the test on host, by
defining a `test` for it, then depending on it through the `$host_toolchain`.

This is done by adding the following to `examples/fidl/hlcpp/unittests/BUILD.gn`.

```gn
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/unittests/BUILD.gn" region_tag="first" %}

test("example-cpp-host-test") {
  sources = [ "main.cc" ]
  deps = [ "//third_party/googletest:gtest_main" ]
}
```

## Run the host test

You can now run the empty test suite:

    fx set core.x64 --with //examples/fidl/hlcpp/unittests
    fx test -vo example-cpp-host-test

You should see test output indicating that zero tests have run, since no tests
have been added yet.

## Add the HLCPP FIDL bindings as a dependency.

Add a dependency on the HLCPP bindings by referencing the FIDL target
directly. The new `test` target should look like:

```gn
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/unittests/BUILD.gn" region_tag="test" %}
```

(Optional) To view the newly generated bindings:

1. Rebuild using `fx build`.
2. Change to the generated files directory:
   `out/default/fidling/gen/examples/fidl/fuchsia.examples/fuchsia/examples`.
   You may need to change `out/default` if you have set a different build output
   directory. You can check your build output directory by running `cat .fx-build-dir`.

For more information on how to find generated bindings code, see
[Viewing Generated Bindings Code][generated-code].

Include the bindings, by adding the following include statement to the top of
`examples/fidl/hlcpp/unittests/main.cc`

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/unittests/main.cc" region_tag="include" %}
```

Now, feel free to write some tests and play around with the bindings by
referring to the generated code or the [HLCPP Bindings Reference][bindings-ref].

Here's some example code to get started. You can add this inside the
anonymous namespace in `main.cc`:

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/unittests/main.cc" region_tag="tests" %}
```

Rebuild and rerun the tests by running:

    fx test -vo example-cpp-host-test


<!-- xrefs -->
[generated-code]: /docs/development/languages/fidl/guides/generated-code.md#c-family
[bindings-ref]: /docs/reference/fidl/bindings/hlcpp-bindings.md
[fidl-intro]: /docs/development/languages/fidl/tutorials/fidl.md
[overview]: /docs/development/languages/fidl/tutorials/overview.md

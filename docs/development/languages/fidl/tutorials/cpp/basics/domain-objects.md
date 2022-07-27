# Using natural and wire domain objects

## Prerequisites

This tutorial builds on the [Compiling FIDL][fidl-intro] tutorial.
For more information on other FIDL tutorials, see the [overview][overview].

## Overview

This tutorial details how to use the natural and wire domain objects by creating
a unit test exercising those data types.

This document covers how to complete the following tasks:

* [Add the C++ bindings of a FIDL library as a build dependency](#add-dep).
* [Include the bindings header into your code](#include-cpp-bindings).
* [Using natural domain objects](#using-natural).
* [Using wire domain objects](#using-wire).

## Using the domain objects example code

The example code accompanying this tutorial is located in your Fuchsia checkout
at `//examples/fidl/cpp/domain_objects`. It consists of a unit test component
and its containing package. For more information about building unit test
components, see [Build components][build-components].

You may build and run the example on a running instance of Fuchsia emulator via
the following:

```posix-terminal
# Add the domain objects unit test to the build.
# This only needs to be done once.
fx set core.x64 --with //examples/fidl/cpp/domain_objects

# Run the domain objects unit test.
fx test -vo fidl-examples-domain-objects-cpp-test
```

## Add the C++ bindings of a FIDL library as a build dependency {#add-dep}

<!-- TODO(fxbug.dev/98989): Talk about Bazel targets -->

For each FIDL library declaration, such as the one in
[Compiling FIDL][fidl-intro], the C++ bindings code for that library is
generated under the original target name suffixed with `_cpp`:

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/BUILD.gn" region_tag="binding-dep" adjust_indentation="auto" %}
```

The `test` target looks like:

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/BUILD.gn" region_tag="test" adjust_indentation="auto" highlight="6" %}
```

Note the line which adds the dependency on the C++ bindings by referencing that
`_cpp` target.

(Optional) To view the generated bindings:

1. Build using `fx build`.
2. Change to the generated files directory:
   `out/default/fidling/gen/examples/fidl/fuchsia.examples/fuchsia/examples`,
   where the generated files are located. You may need to change `out/default`
   if you have set a different build output directory. You can check your build
   output directory with `cat .fx-build-dir`.

For more information on how to find generated bindings code, see
[Viewing generated bindings code][generated-code].

## Include the bindings header into your code {#include-cpp-bindings}

After adding the build dependency, you may include the bindings header. The
include pattern is `#include <fidl/my.library.name/cpp/fidl.h>`.

The following include statement at the top of `domain_objects/main.cc` includes
the bindings and makes the generated APIs available to the source code:

```cpp
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="include" %}
```

## Using natural domain objects {#using-natural}

At a high level the natural types embrace `std::` containers and concepts. For
example, a table is represented as a collection of `std::optional<Field>`s. A
vector is `std::vector<T>`, etc. They also implement idiomatic C++ moves and
copies and equality. For example, a non-resource type will be copyable, while a
resource type is only movable. Moving a table doesn't make it empty (it just
recursively moves the fields), similar to `std::optional`.

<!-- TODO(fxbug.dev/103483): Complete the domain object tutorials -->

```cpp
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="natural-bits" %}

{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="natural-enums" %}

{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="natural-structs" %}

{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="natural-unions" %}

{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="natural-tables" %}
```

## Using wire domain objects {#using-wire}

Because bits and enums have a very simple memory layout and do not have any
out-of-line children, the wire types for FIDL bits and enums are the same as
their natural type counterparts. To stay coherent with the overall namespace
naming profiles, bits and enums are aliased into the `fuchsia_my_library::wire`
nested namespace, appearing alongside wire structs, unions, and tables.

Using the `fuchsia.examples/FileMode` FIDL bits as example,
`fuchsia_examples::wire::FileMode` is a type alias of
`fuchsia_examples::FileMode`.

```cpp
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="wire-bits" %}

{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="wire-enums" %}
```

<!-- TODO(fxbug.dev/103483): Write more about the domain objects -->

```cpp
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="wire-structs" %}

{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="wire-unions" %}

{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/cpp/domain_objects/main.cc" region_tag="wire-tables" %}
```

For more information on the bindings, see the
[bindings reference][bindings-ref].

<!-- xrefs -->
[build-components]: /docs/development/components/build.md#unit-tests
[generated-code]: /docs/development/languages/fidl/guides/generated-code.md#rust
[bindings-ref]: /docs/reference/fidl/bindings/cpp-bindings.md
[fidl-intro]: /docs/development/languages/fidl/tutorials/fidl.md
[overview]: /docs/development/languages/fidl/tutorials/overview.md
[server-tut]: /docs/development/languages/fidl/tutorials/cpp/basics/server.md

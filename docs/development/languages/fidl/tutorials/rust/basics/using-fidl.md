# FIDL Rust crates

## Prerequisites

In this tutorial, you will be using the `fuchsia.examples` FIDL library from the
[Creating a FIDL library][fidl-intro] tutorial. The code for this FIDL library
is available in [//examples/fidl/fuchsia.examples][example-lib]. Take a minute to
review the code before moving on.

## Overview

This tutorial details how to use FIDL from Rust
by creating a unit test that you can use  as a "playground" for
exploring the Rust bindings.

This document covers how to complete the following tasks:

* [Write a "hello world" Rust program](#hello-world).
* [Add the Rust bindings of a FIDL library as a build
  dependency](#add-dependency).
* [Import the Rust bindings crate into your code](#include-rust-bindings).
* [Inspect and use the generated bindings
  code](#inspect-user-generated-bindings).

The example code is located in your Fuchsia checkout in
`//examples/fidl/rust/unittests/`. If you want to write all the code
as you follow this tutorial, you can remove the example code:

```
rm -r examples/fidl/rust/unittests/*
```

## Write a "hello world" program {#hello-world}

1. Add the main function to `examples/fidl/rust/fidl_crates/src/main.rs`:

   ```rust
   fn main() {
      println!("Hello, world!");
   }
   ```

1. Define a `rustc_binary` and then create a depencency on the test through the `$host_toolchain`, which will build the binary for the host.
   To do this, add the following to `examples/fidl/rust/fidl_crates/BUILD.gn`:

   ```gn
   {%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/BUILD.gn" region_tag="imports" %}

   rustc_binary("fidl_crates_bin") {
     edition = "2018"
     sources = [ "src/main.rs" ]
   }

   {%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/BUILD.gn" region_tag="group" %}
   ```

   Note: `rustc_binary` will look for a `src/main.rs` file by default as the crate root. It is possible
   to place the test code in a different file (e.g. `hello_world.rs`) instead, and then specify the
   crate root explicity in the `rustc_test` declaration (e.g. `source_root = "hello_world.rs"`).

1. Build the file

   ```
   fx set core.x64 --with //examples/fidl/rust/fidl_crates
   ```

1. Run the binary

   ```
   out/default/host_x64/fidl_crates_bin`
   ```

   You should see the hello world message printed.

   Note: the directory inside `out/default` will depend on your machine and
   configuration. For example, if you're running on an ARM machine with ASan,
   the directory will be `out/default/host_arm64-asan` instead.

## Add the Rust FIDL bindings as a dependency {#add-dependency}

For each FIDL library declaration, including the one in [Compiling FIDL][fidl-intro],
a FIDL crate containing Rust bindings code for that library is generated under the original target
name appended with `-rustc`.

Add a dependency on the Rust bindings by referencing this generated crate. The new `rustc_test`
target should look like:

```gn
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/BUILD.gn" region_tag="test" %}
```

(Optional) To view the newly generated bindings:

1. Rebuild using `fx build`.
2. Change to the generated files directory:
   `out/default/fidling/gen/examples/fidl/fuchsia.examples`. The generated code is in
   `fidl_fuchsia_examples.rs`.
   You may need to change `out/default` if you have set a different build output
   directory. You can check your build output directory with `cat .fx-build-dir`.

Note: The generated FIDL bindings are part of the build output and are not checked in.

For more information on how to find generated bindings code, see
[Viewing generated bindings code][generated-code].

## Import the FIDL Rust crate into your project {#include-rust-bindings}

To import the crate, add the following to the top of the `tests` module in
`examples/fidl/rust/fidl_crates/src/main.rs`. In the Fuchsia tree, FIDL crates are often aliased to
shorter names for brevity.

```rust
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/src/main.rs" region_tag="import" adjust_indentation="auto" %}
```

## Use the generated bindings code {#inspect-user-generated-bindings}

You can now write some tests by referring to the generated code. For more
information on the bindings, see [Rust Bindings Reference][bindings-ref].

To get started, you can also use some example code. You can add this inside the
`tests` module:

```rust
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/src/main.rs" region_tag="bits" adjust_indentation="auto" %}

{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/src/main.rs" region_tag="enums" adjust_indentation="auto" %}

{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/src/main.rs" region_tag="structs" adjust_indentation="auto" %}

{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/src/main.rs" region_tag="unions" adjust_indentation="auto" %}

{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/rust/fidl_crates/src/main.rs" region_tag="tables" adjust_indentation="auto" %}
```

To rebuild and rerun the tests, run:

```
fx test -vo fidl_crates_test
```

<!-- xrefs -->
[generated-code]: /docs/development/languages/fidl/guides/generated-code.md#rust
[bindings-ref]: /docs/reference/fidl/bindings/rust-bindings.md
[fidl-intro]: /docs/development/languages/fidl/tutorials/fidl.md
[overview]: /docs/development/languages/fidl/tutorials/overview.md
[example-lib]: /examples/fidl/fuchsia.examples/echo.test.fidl

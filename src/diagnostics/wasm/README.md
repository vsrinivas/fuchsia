# Diagnostics WASM

* Reviewed on: 2020-03-25
* Author: crjohns@google.com

Diagnostics WASM is a set of build rules and libraries to build
rustc\_library targets as .wasm libraries, with generated bindings.

WARNING: These build templates are for use only under
//src/diagnostics. All other uses are unsupported and may break without
warning.

## Building

This project can be added to builds by including `--with //src/diagnostics/wasm` to the `fx
set` invocation.

## Using

Diagnostics WASM can be used by using the following import in BUILD.gn:

```
import("//src/diagnostics/wasm/rustc_wasm.gni")
```

For an example, see [//src/diagnostics/wasm/example](example).

Diagnostics WASM is not available in the SDK.

## Source layout

The main .gni and helpers for the build rule exist in this top-level directory.

Our in-tree implementation of the wasm-bindgen CLI is under "bindgen."

The example library exists under "example."

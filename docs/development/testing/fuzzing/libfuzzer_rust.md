# Fuzz testing Rust in Fuchsia

Fuchsia's Rust toolchain supports fuzzing Rust crates using LLVM's [libFuzzer].
Most of the information in [C/C++ fuzzing guide](libfuzzer.md) still applies.
This document only focuses on the details specific to Rust.

## Write a Rust fuzzer

You need to implement a fuzz target function that accepts a slice
of bytes and does something interesting with these bytes using the API under
test.

For example:

```
#[cfg(fuzz)]
fn fuzz_something(data: &[u8]) {
    match std::str::from_utf8(data) {
        Err(_e) => {}
        Ok(s) => do_something_interesting_with_my_api(s);
    }
}
```

This function should typically be located near the code that it tests. It is
directly analogous to a [fuzz target] function in C or C++, but an
additional step is needed to export that symbol to libFuzzer:

```
#[cfg(fuzz)]
mod fuzz {
    use super::*;
    use fuchsia_fuzzing::declare_fuzzers;
    declare_fuzzers!(fuzz_something);
}

```

Using the `declare_fuzzers!` macro from the [`fuchsia-fuzzing`][fuchsia-fuzzing]
crate includes one or more named functions into the symbol used by libFuzzer to
searches for inputs that cause panics or other errors.

## Build a Rust fuzzer

The `rustc_fuzzer` GN template generates a GN target that compiles the Rust fuzz
target function into a C object file that it then links with libFuzzer.

To build a Rust fuzzer:
1. Add a call to `declare_fuzzers!` to a crate root, e.g. src/lib.rs.
   Alternatively, you may create new Rust crate if no existing crate is a
   good fit, and the fuzz target functions are visible.
1. Add a [`rustc_fuzzer`][rustc_fuzzer] GN target to the crate's BUILD.gn. This
   should minimally include `edition = "2018"`; in many cases the default values
   for the remaining parameters will be correct.
1. Add the fuzzer to a new or existing [`fuzzers_package`][fuzzers_package] GN
   target to bundle it into a deployable package.

After this, you can continue following [Fuzz testing in Fuchsia with
LibFuzzer](libfuzzer.md)'s generic instructions. Currently, the only supported
sanitizer runtime is [AddressSanitizer].

Note: There are two different versions of each supported sanitizer runtime.
The fuzzer will behave differently depending on which is selected!
 * To fuzz the (possibly unsafe) C/C++ code reachable from some Rust code, use
   `--fuzz-with asan`.
 * To fuzz the Rust code itself, use `--fuzz-with rust-asan`.

It is not possible to instrument and fuzz both the C/C++ and Rust code
simultaneously. This is fundamentally a result of the clang compiler and rustc
compiler being built from different forks of LLVM and using different version
of compiler runtimes.

## Run a rust fuzzer

To run the fuzzer, see the [Quick-start guide] for how to use the `fx fuzz`
commands.

## Example

For a complete example, see the example Rust fuzzer in
[//examples/fuzzer/rust](/examples/fuzzer/rust).

[addresssanitizer]: https://clang.llvm.org/docs/AddressSanitizer.html
[fuchsia-fuzzing]: /src/lib/fuzzing/rust/src/lib.rs
[fuzz target]: https://llvm.org/docs/LibFuzzer.html#fuzz-target
[fuzzers_package]: libfuzzer.md#the-fuzzers-package-gn-template
[libFuzzer]: https://llvm.org/docs/LibFuzzer.html
[rustc_fuzzer]: /build/rust/rustc_fuzzer.gni
[quick-start guide]: libfuzzer.md#quick-start-guide

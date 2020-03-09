# Fuzz testing Rust in Fuchsia

Fuchsia's Rust toolchain supports fuzzing Rust crates using LLVM's [libFuzzer].
Most of the information in [C/C++ fuzzing guide](libfuzzer.md) still applies.
This document only focuses on the details specific to Rust.

## Write a Rust fuzzer {#write-a-rust-fuzzer}

You need to implement a fuzz target function and annotate it with the `[#fuzz]`
attribute from the [`fuchsia-fuzzing`][fuchsia-fuzzing] crate.

This function should typically be located near the code that it tests. It is
analogous to a [fuzz target] function in C or C++, and is used by libFuzzer to
search for inputs that cause panics or other errors.

The function should use its inputs to exercise an API to be tested. There are
two ways to write the inputs to the function. For each of the examples below,
assume you want to test code like the following:
```
struct ToyStruct {
    n: u8,
    s: String,
}

fn toy_example(input: ToyStruct) -> Result<u8, &'static str>;
```

### Automatically transform arbitrary inputs {#automatic-transform}

You can create a fuzz target function that takes one or more inputs with the
`Arbitrary` trait from the [`arbitrary`][arbitrary] crate. This is the
recommended approach.

To write a fuzz target function that automatically transforms arbitrary inputs:
1. If needed, implement the `Arbitrary` trait for the types used by your
   test code. This can be done "by hand" following the crate's
   [instructions][arbitrary], but the recommended way is to automatically derive the trait.

   For example, in your `src/lib.rs`:
   ```
   use arbitrary:Arbitrary;

   #[derive(Arbitrary)]
   struct ToyStruct { ... }
   ```

1. Create a function with the [`#[fuzz]`][fuchsia-fuzzing] attribute that passes
   the necessary parameters to the code you wish to test.

   For example, in your `src/lib.rs`:
   ```
   use fuchsia_fuzzing::fuzz;

   #[fuzz]
   fn toy_example_arbitrary(input: ToyStruct) {
       let _ = toy_example(input);
   }
   ```

### Manually transform a byte slice to API inputs {#manual-transform}

If the code you wish to test already operates on bytes, or if it's not possible
to implement the `Arbitrary` trait for your inputs, you can create a fuzz target
function that uses the bytes provided by the fuzzer engine directly. As before,
this function needs the [`#[fuzz]`][fuchsia-fuzzing] attribute. It should take a
reference to byte slice as its single parameter, i.e. `&[u8]`.

For example, in your `src/lib.rs`:
```
use fuchsia_fuzzing::fuzz;

#[fuzz]
fn toy_example_u8(input: &[u8]) {
    if input.len() == 0 {
        return
    }
    let n = input[0];
    if let Ok(s) = std::str::from_utf8(input) {
        let _ = toy_example(ToyStruct{n, s: s.to_string(),});
    }
}
```

## Build a Rust fuzzer

The `rustc_fuzzer` GN template generates a GN target that compiles the Rust fuzz
target function into a C object file that it then links with libFuzzer.

To build a Rust fuzzer:
1. Add a [`rustc_fuzzer`][rustc_fuzzer] GN target to the crate's BUILD.gn.

   When choosing where and how to add this target, consider the following:
   * It is recommended to have the fuzzer name match the fuzz target function
     name, and to include the fuzz target function in a Rust library, i.e. in
     `src/lib.rs`. You may leave the body of the template empty when following
     these recommendations.

     For example, using the `toy_example_arbitrary` example
     [above](#automatic-transform), you would add the following to your
     `BUILD.gn`:
     ```
     import("//build/rust/rustc_fuzzer.gni")

     rustc_fuzzer("toy_example_arbitrary") {
     }
     ```

   * If the fuzz target function name differs from the fuzzer name, you must
     provide it with the `rustfunction` parameter.

     For example, using the `toy_example_u8` example [above](#manual-transform),
     you would add the following to your `BUILD.gn`:
     ```
     import("//build/rust/rustc_fuzzer.gni")

     rustc_fuzzer("toy_example_raw_bytes") {
         rustfunction = "toy_example_u8"
     }
     ```

   * If the [code to be tested](#write-a-rust-fuzzer) cannot be easily factored
     into a library, a Rust binary can be used with two implications:
     * You must exclude the `main` function from compilation, along with any
       items not used when fuzzing, e.g. imports only used in `main`.

       For example:
       ```
       #[cfg(not(fuzz))]
       use only::used::in::main;

       #[cfg(not(fuzz))]
       fn main() { ... }
       ```

     * You must explicitly provide the fuzz target function to the
       `rustc_fuzzer` with the `source_root` parameter.

       For example, in your `BUILD.gn`:
       ```
       import("//build/rust/rustc_fuzzer.gni")

       rustc_fuzzer("toy_example_with_main") {
           source_root = "src/main.rs"
       }
       ```

1. Add the fuzzer to a new or existing [`fuzzers_package`][fuzzers_package] GN
   target to bundle it into a deployable package. You'll want to add it to a
   `fuzzer_profile` that tells the build to add Rust instrumentation.

   For example:
   ```
   fuzzers_package("example_fuzzers") {
     fuzzer_profiles = [
       {
         fuzzers = [
           "rust:toy_example_arbitrary",
           "rust:toy_example_u8",
         ]
         sanitizers = [ "rust-asan" ]
       },
     ]
   }
   ```

After this, you can continue following the instructions in
[Fuzz testing in Fuchsia with LibFuzzer](libfuzzer.md).


Note: There are different versions of each supported sanitizer runtime for
different compilers (e.g. `clang`, `rustc`, etc.). Supported variants are
those with the prefix of "rust-" in the list of [known_variants], e.g.
"rust-asan-fuzzer".

To use the `rust-asan-fuzzer` variant, [configure your build][fx_set] with the
following:
```
fx set [other-args...] --fuzz-with rust-asan`
```

## Run a rust fuzzer

To run the fuzzer, see the [Quick-start guide] for how to use the `fx fuzz`
commands.

## Example

The code in this document is taken from the complete Rust fuzzer example in
[//examples/fuzzer/rust](/examples/fuzzer/rust).

[addresssanitizer]: https://clang.llvm.org/docs/AddressSanitizer.html
[arbitrary]: https://docs.rs/arbitrary/0.4.0/arbitrary
[fuchsia-fuzzing]: /src/lib/fuzzing/rust/src/lib.rs
[fuzz target]: https://llvm.org/docs/LibFuzzer.html#fuzz-target
[fuzzers_package]: libfuzzer.md#the-fuzzers-package-gn-template
[fx_set]: /docs/development/build/fx.md#configure-a-build
[libFuzzer]: https://llvm.org/docs/LibFuzzer.html
[known_variants]: /docs/gen/build_arguments.md#known_variants
[rustc_fuzzer]: /build/rust/rustc_fuzzer.gni
[quick-start guide]: libfuzzer.md#quick-start-guide

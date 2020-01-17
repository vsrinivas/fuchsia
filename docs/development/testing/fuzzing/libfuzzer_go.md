# Fuzz testing Go in Fuchsia

Fuchsia's Go toolchain supports fuzzing Go packages using LLVM's
[libFuzzer](https://llvm.org/docs/LibFuzzer.html). Most of the information in
[C/C++ fuzzing guide](libfuzzer.md) still applies; this document only focuses on
the details specific to Go.

## Writing a Go fuzzer

Go fuzzer authors need to implement a fuzz target function that accepts a slice
of bytes and does something interesting with these bytes using the API under
test. libFuzzer then searches for inputs that cause the function to panic.

Example:

```
func Fuzz(s []byte) {
	DoSomethingInterestingWithMyAPI(s)
}
```

This is directly analogous to a [fuzz target function in
C](https://llvm.org/docs/LibFuzzer.html#fuzz-target).

## Building a Go fuzzer

The `go_fuzzer` GN template generates a GN target that compiles the Go fuzz
target function into a C object file that it then links with libFuzzer.

1. Add a function `func Fuzz(s []byte)` to a Go package and export
   it. Alternatively, you may create new Go package if no existing package is a
   good fit.
1. Ensure the Go package in the previous step is available as a `go_library` GN
   target.
1. Write a `go_fuzzer` (`//build/go/go_fuzzer.gni`) GN target to build the
   package containing the fuzz target function. Make sure to include the
   `go_library` in [`deps`](gn deps).
1. Write a `fuzzers_package` (`//build/fuzzing/fuzzer.gni`) GN target that
   bundles the fuzzer into a deployable package. This is explained further in
   the [fuzzers_package documentation](fuzzers_package docs).

After this, you can continue following [Fuzz testing in Fuchsia with
LibFuzzer](libfuzzer.md)'s generic instructions. For example, see its
[Quick-start guide](libfuzzer.md#quick-start-guide) for how to use the `fx fuzz`
commands.

For a complete example, see the example Go fuzzer in
[//examples/fuzzer/go/BUILD.gn](/examples/fuzzer/go/BUILD.gn).

[gn deps]: https://gn.googlesource.com/gn/+/master/docs/reference.md#var_deps
[fuzzers_package docs]: libfuzzer.md#the-fuzzers-package-gn-template

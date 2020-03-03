// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the "library code" tested in this example.
// Compare with https://llvm.org/docs/LibFuzzer.html#toy-example
fn toy_example(input: String) {
    let mut chars = input.chars();
    if chars.next() == Some('H') {
        if chars.next() == Some('I') {
            if chars.next() == Some('!') {
                panic!("it works!");
            }
        }
    }
}

// This can typically be placed alongside your code, wherever it may be in your crate.
#[cfg(fuzz)]
fn fuzz_toy_example(data: &[u8]) {
    match std::str::from_utf8(data) {
        Err(_e) => {}
        Ok(s) => toy_example(s.to_string()),
    }
}

// Wrapping the `declare_fuzzers!` invocation in a mod prevents the `LLVMFuzzerTestOneInput` symbol
// to only be emitted when using the `rustc_fuzzer` template. This mod will typically be in
// src/lib.rs and collect
#[cfg(fuzz)]
mod fuzz {
    use super::*;
    use fuchsia_fuzzing::declare_fuzzers;
    declare_fuzzers!(fuzz_toy_example);
}

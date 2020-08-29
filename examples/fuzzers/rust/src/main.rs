// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the "library code" tested in this example.
// Compare with https://llvm.org/docs/LibFuzzer.html#toy-example
use {
    fuzz::fuzz,
    helper::{add_bang, toy_example},
};

// Imports that are only used by `main` needs to be conditional included.
#[cfg(not(fuzz))]
use std::io::{self, Read};

// Conditionally include `main`, allowing it to be omitted when provided by a compiler runtime.
#[cfg(not(fuzz))]
fn main() -> io::Result<()> {
    let mut s = String::new();
    io::stdin().read_to_string(&mut s)?;
    let _ = toy_example(s);
    Ok(())
}

// This is the "fuzzer code" for the binary code above.
// It can typically be placed alongside that code and will only be compiled when fuzzing.
#[fuzz]
fn toy_example_with_main(input: String) {
    let _ = toy_example(add_bang(input));
}

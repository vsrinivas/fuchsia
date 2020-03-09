// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {arbitrary::Arbitrary, fuzz::fuzz};

// This is the "library code" tested in this example.
#[derive(Arbitrary)]
struct ToyStruct {
    n: u8,
    s: String,
}

fn toy_example(input: ToyStruct) -> Result<u8, &'static str> {
    if input.n != 42 {
        return Err("n must be 42");
    }
    let mut chars = input.s.chars();
    if chars.next() == Some('H') {
        if chars.next() == Some('I') {
            if chars.next() == Some('!') {
                panic!("it works!");
            }
        }
    }
    return Ok(0);
}

// This is the "fuzzer code" for the library code above.
// It can typically be placed alongside that code and will only be compiled when fuzzing.

// "Automatic" transform: Use one or more immutable types that implement `Arbitrary` as inputs.
#[fuzz]
fn toy_example_arbitrary(input1: ToyStruct, input2: ToyStruct) -> Result<u8, &'static str> {
    let _ = toy_example(input1);
    toy_example(input2) // Arbitrary return values are okay.
}

// "Manual" transform: convert a reference to an immutable byte slice to inputs.
#[fuzz]
fn toy_example_u8(input: &[u8]) {
    if input.len() == 0 {
        return; // Early returns are okay.
    }
    let n = input[0];
    if let Ok(s) = std::str::from_utf8(&input[1..]) {
        let _ = toy_example(ToyStruct { n, s: s.to_string() });
    }
}

// You can reuse the same name with `[#fuzz]` in different modules. Compare this function with
// `helper::toy_example_same_name`.
#[fuzz]
fn toy_example_same_name(input: String) {
    use helper;
    helper::toy_example(helper::add_bang(input));
}

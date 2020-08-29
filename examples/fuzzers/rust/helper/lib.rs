// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuzz::fuzz;

pub fn add_bang(input: String) -> String {
    let mut result = input;
    result.push('!');
    result
}

pub fn toy_example(input: String) {
    let mut chars = input.chars();
    if chars.next() == Some('H') {
        if chars.next() == Some('I') {
            if chars.next() == Some('!') {
                panic!("it works!");
            }
        }
    }
}

// Compare with `toy_example_same_name` in ../src/lib.rs.
#[fuzz]
fn toy_example_same_name(input: String) {
    toy_example(input)
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_bitsstrictflexible as fidl_lib;

// [START contents]
fn use_bits(bits: &fidl_lib::Flags) -> fidl_lib::Flags {
    let mut result = fidl_lib::Flags::empty();
    if bits.contains(fidl_lib::Flags::OPTION_A) {
        result.set(fidl_lib::Flags::all(), true);
    }
    return result;
}
// [END contents]

fn main() {}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_bitsmemberadd as fidl_lib;

// [START contents]
fn use_bits(bits: &fidl_lib::Flags) {
    if bits.contains(fidl_lib::Flags::OPTION_A) {
        println!("option A is set");
    }
    if bits.contains(fidl_lib::Flags::OPTION_B) {
        println!("option B is set");
    }
    if bits.has_unknown_bits() {
        println!("unknown options: {:x}", bits.get_unknown_bits());
    }
}
// [END contents]

fn main() {}

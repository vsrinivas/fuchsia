// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_enumflexiblestrict as fidl_lib;

// [START contents]
fn complement(color: &fidl_lib::Color) -> fidl_lib::Color {
    match color {
        fidl_lib::Color::Red => fidl_lib::Color::Blue,
        fidl_lib::Color::Blue => fidl_lib::Color::Red,
    }
}

// [END contents]

fn main() {}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_enumflexiblestrict as fidl_lib;

// [START contents]
fn complement(color: &fidl_lib::Color) -> Option<fidl_lib::Color> {
    #[allow(unreachable_patterns)]
    match color {
        fidl_lib::Color::Red => Some(fidl_lib::Color::Blue),
        fidl_lib::Color::Blue => Some(fidl_lib::Color::Red),
        _ => None,
    }
}

// [END contents]

fn main() {}

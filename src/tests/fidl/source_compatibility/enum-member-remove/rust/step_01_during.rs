// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_enummemberremove as fidl_lib;

// [START contents]
fn writer(s: &str) -> fidl_lib::Color {
    match s {
        "red" => fidl_lib::Color::Red,
        "blue" => fidl_lib::Color::Blue,
        _ => fidl_lib::Color::unknown(),
    }
}

fn reader(color: fidl_lib::Color) -> &'static str {
    match color {
        fidl_lib::Color::Red => "red",
        fidl_lib::Color::Blue => "blue",
        _ => "unknown",
    }
}
// [END contents]

fn main() {}

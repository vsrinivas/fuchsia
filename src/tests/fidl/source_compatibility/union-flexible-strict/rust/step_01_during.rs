// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_unionflexiblestrict as fidl_lib;

// [START contents]
fn use_union(value: &fidl_lib::JsonValue) {
    #[allow(unreachable_patterns)]
    match value {
        fidl_lib::JsonValue::IntValue(n) => println!("int value: {}", n),
        fidl_lib::JsonValue::StringValue(s) => println!("string: {}", s),
        _ => {}
    };
}
// [END contents]

fn main() {}

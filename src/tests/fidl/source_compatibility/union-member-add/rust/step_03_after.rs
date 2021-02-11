// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_unionmemberadd as fidl_lib;

// [START contents]
fn writer(s: &str) -> fidl_lib::JsonValue {
    let as_float = s.parse::<f32>();
    if let Ok(val) = as_float {
        return fidl_lib::JsonValue::FloatValue(val);
    }
    let as_int = s.parse::<i32>();
    as_int
        .map(|n| fidl_lib::JsonValue::IntValue(n))
        .unwrap_or(fidl_lib::JsonValue::StringValue(s.to_string()))
}

fn reader(value: fidl_lib::JsonValue) -> String {
    match value {
        fidl_lib::JsonValue::IntValue(n) => format!("{}", n),
        fidl_lib::JsonValue::StringValue(s) => s,
        fidl_lib::JsonValue::FloatValue(v) => format!("{:.2}", v),
        fidl_lib::JsonValueUnknown!() => "<unknown>".to_string(),
    }
}
// [END contents]

fn main() {}

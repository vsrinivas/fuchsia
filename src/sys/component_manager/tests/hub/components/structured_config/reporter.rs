// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hub_report::*;

#[fuchsia::component]
async fn main() {
    let config_path = "/hub/resolved/config";

    let expected_fields = vec![
        ("my_flag", "true"),
        ("my_int16", "-32766"),
        ("my_int32", "-2000000000"),
        ("my_int64", "-4000000000"),
        ("my_int8", "-127"),
        ("my_string", "\"hello, world!\""),
        ("my_uint16", "65535"),
        ("my_uint32", "4000000000"),
        ("my_uint64", "8000000000"),
        ("my_uint8", "255"),
        ("my_vector_of_flag", "[true, false]"),
        ("my_vector_of_int16", "[-2, -3, 4]"),
        ("my_vector_of_int32", "[-3, -4, 5]"),
        ("my_vector_of_int64", "[-4, -5, 6]"),
        ("my_vector_of_int8", "[-1, -2, 3]"),
        ("my_vector_of_string", "[\"hello, world!\", \"hello, again!\"]"),
        ("my_vector_of_uint16", "[2, 3, 4]"),
        ("my_vector_of_uint32", "[3, 4, 5]"),
        ("my_vector_of_uint64", "[4, 5, 6]"),
        ("my_vector_of_uint8", "[1, 2, 3]"),
    ];

    let expected_files: Vec<&str> = expected_fields.iter().map(|field| field.0).collect();

    expect_dir_listing(config_path, expected_files).await;

    for (key, expected_value) in expected_fields {
        let config_value_path = format!("{}/{}", config_path, key);
        expect_file_content(&config_value_path, expected_value).await;
    }
}

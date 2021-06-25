// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_examples as fex;

#[test]
fn simple_method() -> Result<(), fidl::Error> {
    // [START simple_method_encode]
    let mut original_value = fex::Color { id: 0, name: "red".to_string() };
    let bytes = fidl::encoding::encode_persistent(&mut original_value)?;
    // [END simple_method_encode]

    // [START simple_method_decode]
    let decoded_value = fidl::encoding::decode_persistent::<fex::Color>(&bytes)?;
    assert_eq!(original_value, decoded_value);
    // [END simple_method_decode]

    Ok(())
}

#[test]
fn separate_header() -> Result<(), fidl::Error> {
    // [START separate_header_encode]
    let mut original_json = fex::JsonValue::StringValue("hello".to_string());
    let mut original_user = fex::User { age: Some(20), ..fex::User::EMPTY };

    let mut header = fidl::encoding::create_persistent_header();
    let header_bytes = fidl::encoding::encode_persistent_header(&mut header)?;
    let json_bytes = fidl::encoding::encode_persistent_body(&mut original_json, &header)?;
    let user_bytes = fidl::encoding::encode_persistent_body(&mut original_user, &header)?;
    // [END separate_header_encode]

    // [START separate_header_decode]
    let header = fidl::encoding::decode_persistent_header(&header_bytes)?;
    let decoded_json = fidl::encoding::decode_persistent_body(&json_bytes, &header)?;
    let decoded_user = fidl::encoding::decode_persistent_body(&user_bytes, &header)?;
    assert_eq!(original_json, decoded_json);
    assert_eq!(original_user, decoded_user);
    // [END separate_header_decode]

    Ok(())
}

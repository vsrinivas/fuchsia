// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file tests FIDL's persistent encoding/decoding API.

use fidl_fidl_rust_test_external::{Coordinate, FlexibleValueThing, StrictValueThing, ValueRecord};

#[test]
fn encode_decode_persistent_combined() {
    let mut body = Coordinate { x: 1, y: 2 };
    let buf = encode_persistent(&mut body).expect("encoding failed");
    let body_out = decode_persistent::<Coordinate>(&buf).expect("decoding failed");
    assert_eq!(body, body_out);
}

#[test]
fn encode_decode_persistent_separate() {
    let mut body1 = StrictValueThing::Number(42);
    let mut body2 = FlexibleValueThing::Name("foo".into_string());
    let mut body3 = ValueRecord { age: 30, ..ValueRecord::EMPTY };

    let mut header = create_persistent_header();
    let buf_header = encode_persistent_header(&mut header).expect("header encoding failed");
    let buf_body1 = encode_persistent_body(&mut body1, &header).expect("body1 encoding failed");
    let buf_body2 = encode_persistent_body(&mut body2, &header).expect("body2 encoding failed");
    let buf_body3 = encode_persistent_body(&mut body3, &header).expect("body3 encoding failed");

    let header_out = decode_persistent_header(&buf_header).expect("header decoding failed");
    assert_eq!(header, decode_persistent_header(&buf_header).expect("header decoding failed"));
    let body1_out = decode_persistent_body::<StrictValueThing>(&buf_body1, &header)
        .expect("body1 decoding failed");
    assert_eq!(body1, body1_out);
    let body2_out = decode_persistent_body::<FlexibleValueThing>(&buf_body2, &header)
        .expect("body2 decoding failed");
    assert_eq!(body2, body2_out);
    let body3_out = decode_persistent_body::<ValueRecord>(&buf_body3, &header)
        .expect("Another body decoding failed");
    assert_eq!(body3, body3_out);
}

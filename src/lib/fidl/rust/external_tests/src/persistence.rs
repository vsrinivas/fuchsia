// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file tests FIDL's persistent encoding/decoding API.

use fidl::encoding::{
    create_persistent_header, decode_persistent, decode_persistent_body, decode_persistent_header,
    encode_persistent, encode_persistent_body, encode_persistent_header,
};
use fidl_fidl_rust_test_external::{Coordinate, FlexibleValueThing, StrictValueThing, ValueRecord};
use test_case::test_case;

// TODO(fxbug.dev/45252): Remove these.
fn keep_new_header(_buf: &mut Vec<u8>) {}
fn transform_new_to_old_header(buf: &mut Vec<u8>) {
    //       disambiguator
    //            | magic
    //            |  | flags
    //            |  |  / \  ( reserved )
    //     new:  00 MA FL FL  00 00 00 00
    //     idx:  0  1  2  3   4  5  6  7
    //     old:  00 00 00 00  FL FL FL MA  00 00 00 00  00 00 00 00
    //          ( txid gap )   \ | /   |  (      ordinal gap      )
    //                         flags  magic
    let magic = buf[1];
    let flag1 = buf[2];
    let flag2 = buf[3];
    let new_header: [u8; 16] = [0, 0, 0, 0, flag1, flag2, 0, magic, 0, 0, 0, 0, 0, 0, 0, 0];
    buf.splice(0..8, new_header);
}

#[test_case(keep_new_header)]
#[test_case(transform_new_to_old_header)]
fn encode_decode_persistent_combined(transform: impl Fn(&mut Vec<u8>)) {
    let mut body = Coordinate { x: 1, y: 2 };
    let mut buf = encode_persistent(&mut body).expect("encoding failed");
    transform(&mut buf);
    let body_out = decode_persistent::<Coordinate>(&buf).expect("decoding failed");
    assert_eq!(body, body_out);
}

#[test_case(keep_new_header)]
#[test_case(transform_new_to_old_header)]
fn encode_decode_persistent_separate(transform: impl Fn(&mut Vec<u8>)) {
    let mut body1 = StrictValueThing::Number(42);
    let mut body2 = FlexibleValueThing::Name("foo".to_string());
    let mut body3 = ValueRecord { age: Some(30), ..ValueRecord::EMPTY };

    let mut header = create_persistent_header();
    let mut buf_header = encode_persistent_header(&mut header).expect("header encoding failed");
    transform(&mut buf_header);
    let buf_body1 = encode_persistent_body(&mut body1, &header).expect("body1 encoding failed");
    let buf_body2 = encode_persistent_body(&mut body2, &header).expect("body2 encoding failed");
    let buf_body3 = encode_persistent_body(&mut body3, &header).expect("body3 encoding failed");

    let header_out = decode_persistent_header(&buf_header).expect("header decoding failed");
    assert_eq!(header, header_out);
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

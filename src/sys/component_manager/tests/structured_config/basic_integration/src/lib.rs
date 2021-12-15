// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cm_rust::FidlIntoNative;
use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_config::{ListValue, SingleValue, Value, ValuesData};
use fidl_test_structuredconfig_receiver::ReceiverConfig;
use std::path::Path;

#[fuchsia::test]
fn manually_resolve_structured_config() {
    // read the config declaration
    let manifest_raw = std::fs::read("/pkg/meta/basic_config_receiver.cm").unwrap();
    let manifest: fidl_fuchsia_component_decl::Component =
        decode_persistent(&manifest_raw[..]).unwrap();
    let manifest = manifest.fidl_into_native();
    let config = manifest.config.as_ref().unwrap();

    // read the value file
    let value_file_raw = std::fs::read("/pkg/meta/basic_config_receiver.cvf").unwrap();
    let value_file: ValuesData = decode_persistent(&value_file_raw[..]).unwrap();

    // resolve
    let resolved_fields =
        config_encoder::ConfigFields::resolve(config, value_file.clone()).unwrap();

    // check the checksums
    let value_checksum = value_file.declaration_checksum.as_ref().unwrap();
    let resolved_checksum = &resolved_fields.declaration_checksum;
    assert_eq!(
        &config.declaration_checksum, value_checksum,
        "decl and value file checksums must match"
    );
    assert_eq!(value_checksum, resolved_checksum, "decl and resolved checksums must match");

    // collect the resolved fields ignoring their order
    let observed_values =
        resolved_fields.fields.iter().map(|field| field.value.clone()).collect::<Vec<_>>();

    // there's an empty file at this path if the puppet has been packaged with my_flag==true
    let expected_my_flag = Path::new("/pkg/data/my_flag_is_true").exists();

    // NOTE: these are sorted to match the key-alpha-sorted order cmc creates
    let expected_values = vec![
        Value::Single(SingleValue::Flag(expected_my_flag)),
        Value::Single(SingleValue::Signed16(-32766)),
        Value::Single(SingleValue::Signed32(-2_000_000_000)),
        Value::Single(SingleValue::Signed64(-4_000_000_000)),
        Value::Single(SingleValue::Signed8(-127)),
        Value::Single(SingleValue::Text("hello, world!".into())),
        Value::Single(SingleValue::Unsigned16(65535)),
        Value::Single(SingleValue::Unsigned32(4_000_000_000)),
        Value::Single(SingleValue::Unsigned64(8_000_000_000)),
        Value::Single(SingleValue::Unsigned8(255)),
        Value::List(ListValue::FlagList(vec![true, false])),
        Value::List(ListValue::Signed16List(vec![-2, -3, 4])),
        Value::List(ListValue::Signed32List(vec![-3, -4, 5])),
        Value::List(ListValue::Signed64List(vec![-4, -5, 6])),
        Value::List(ListValue::Signed8List(vec![-1, -2, 3])),
        Value::List(ListValue::TextList(vec![
            "hello, world!".to_string(),
            "hello, again!".to_string(),
        ])),
        Value::List(ListValue::Unsigned16List(vec![2, 3, 4])),
        Value::List(ListValue::Unsigned32List(vec![3, 4, 5])),
        Value::List(ListValue::Unsigned64List(vec![4, 5, 6])),
        Value::List(ListValue::Unsigned8List(vec![1, 2, 3])),
    ];

    assert_eq!(
        observed_values.len(),
        expected_values.len(),
        "must have the right number of values resolved",
    );

    for (observed_field, expected_field) in observed_values.iter().zip(expected_values.iter()) {
        assert_eq!(observed_field, expected_field, "each observed field must match expected");
    }

    assert_eq!(observed_values.len(), config.fields.len(), "must have the right number of fields");

    // encode as a fidl struct and make sure the bindings can parse it back
    let encoded = resolved_fields.encode_as_fidl_struct();
    let checksum_length = u16::from_le_bytes([encoded[0], encoded[1]]) as usize;
    let fidl_start = 2 + checksum_length;
    let encoded_checksum = &encoded[2..fidl_start];
    assert_eq!(value_checksum, encoded_checksum);
    let decoded: ReceiverConfig = decode_persistent(&encoded[fidl_start..]).unwrap();

    assert_eq!(
        decoded,
        ReceiverConfig {
            my_flag: expected_my_flag,
            my_uint8: 255,
            my_uint16: 65535,
            my_uint32: 4_000_000_000,
            my_uint64: 8_000_000_000,
            my_int8: -127,
            my_int16: -32766,
            my_int32: -2_000_000_000,
            my_int64: -4_000_000_000,
            my_string: "hello, world!".into(),
            my_vector_of_flag: vec![true, false],
            my_vector_of_uint8: vec![1, 2, 3],
            my_vector_of_uint16: vec![2, 3, 4],
            my_vector_of_uint32: vec![3, 4, 5],
            my_vector_of_uint64: vec![4, 5, 6],
            my_vector_of_int8: vec![-1, -2, 3],
            my_vector_of_int16: vec![-2, -3, 4],
            my_vector_of_int32: vec![-3, -4, 5],
            my_vector_of_int64: vec![-4, -5, 6],
            my_vector_of_string: vec!["hello, world!".into(), "hello, again!".into()],
        }
    );
}

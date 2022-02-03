// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cm_rust::{ConfigChecksum, FidlIntoNative, ListValue, SingleValue, Value};
use diagnostics_reader::{ArchiveReader, Inspect};
use fidl::encoding::decode_persistent;
use fidl_test_structuredconfig_receiver::{ConfigReceiverPuppetMarker, ReceiverConfig};
use fuchsia_inspect::assert_data_tree;
use std::fs::{read_dir, read_to_string};
use std::path::Path;

fn expected_my_flag() -> bool {
    // there's an empty file at this path if the puppet has been packaged with my_flag==true
    Path::new("/pkg/data/my_flag_is_true").exists()
}

fn expected_config() -> ReceiverConfig {
    ReceiverConfig {
        my_flag: expected_my_flag(),
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
}

#[fuchsia::test]
async fn resolve_structured_config_in_child() {
    // this is routed from our child component
    let puppet =
        fuchsia_component::client::connect_to_protocol::<ConfigReceiverPuppetMarker>().unwrap();
    assert_eq!(
        puppet.get_config().await.unwrap(),
        expected_config(),
        "child must receive expected configuration"
    );

    // Now that the receiver has been resolved, check that the hub also has the matching
    // configuration fields.
    let config_path = Path::new("/hub/children/receiver/resolved/config");
    let files = read_dir(config_path).unwrap();
    let mut files: Vec<String> =
        files.map(|d| d.unwrap().file_name().into_string().unwrap()).collect();
    files.sort();

    let expected_fields = vec![
        ("my_flag", if expected_my_flag() { "true" } else { "false" }),
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

    assert_eq!(files, expected_files);

    for (key, expected_value) in expected_fields {
        let file = config_path.join(key);
        let value = read_to_string(file).unwrap();
        assert_eq!(value, expected_value);
    }

    let inspector = ArchiveReader::new()
        .add_selector("receiver:root")
        .snapshot::<Inspect>()
        .await
        .unwrap()
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .unwrap();

    assert_data_tree!(inspector, root: {
        config: {
            my_flag: expected_my_flag(),
            my_uint8: 255u64,
            my_uint16: 65535u64,
            my_uint32: 4000000000u64,
            my_uint64: 8000000000u64,
            my_int8: -127i64,
            my_int16: -32766i64,
            my_int32: -2000000000i64,
            my_int64: -4000000000i64,
            my_string: "hello, world!",
            my_vector_of_flag: vec![
                1i64,
                0i64
            ],
            my_vector_of_uint8: vec![
                1i64,
                2i64,
                3i64,
            ],
            my_vector_of_uint16: vec![
                2i64,
                3i64,
                4i64,
            ],
            my_vector_of_uint32: vec![
                3i64,
                4i64,
                5i64,
            ],
            my_vector_of_uint64: vec![
                4i64,
                5i64,
                6i64,
            ],
            my_vector_of_int8: vec![
                -1i64,
                -2i64,
                3i64,
            ],
            my_vector_of_int16: vec![
                -2i64,
                -3i64,
                4i64,
            ],
            my_vector_of_int32: vec![
                -3i64,
                -4i64,
                5i64,
            ],
            my_vector_of_int64: vec![
                -4i64,
                -5i64,
                6i64,
            ],
            my_vector_of_string: vec![
                "hello, world!",
                "hello, again!"
            ]
        }
    })
}

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
    let value_file: fidl_fuchsia_component_config::ValuesData =
        decode_persistent(&value_file_raw[..]).unwrap();
    let value_file = value_file.fidl_into_native();

    // resolve
    let resolved_fields =
        config_encoder::ConfigFields::resolve(config, value_file.clone()).unwrap();

    // check the checksums
    assert_eq!(config.checksum, value_file.checksum, "decl and value file checksums must match");
    assert_eq!(
        value_file.checksum, resolved_fields.checksum,
        "decl and resolved checksums must match"
    );

    // collect the resolved fields ignoring their order
    let observed_values =
        resolved_fields.fields.iter().map(|field| field.value.clone()).collect::<Vec<_>>();

    // NOTE: these are sorted to match the key-alpha-sorted order cmc creates
    let expected_values = vec![
        Value::Single(SingleValue::Flag(expected_my_flag())),
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
    let ConfigChecksum::Sha256(value_checksum) = &value_file.checksum;
    assert_eq!(value_checksum, encoded_checksum);
    let decoded: ReceiverConfig = decode_persistent(&encoded[fidl_start..]).unwrap();
    assert_eq!(decoded, expected_config());
}

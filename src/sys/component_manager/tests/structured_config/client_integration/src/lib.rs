// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_reader::{ArchiveReader, Inspect};
use fidl_test_structuredconfig_receiver::{ConfigReceiverPuppetMarker, ReceiverConfig};
use fuchsia_inspect::assert_data_tree;

fn expected_config() -> ReceiverConfig {
    ReceiverConfig {
        my_flag: true,
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

async fn assert_inspect_config(selector: &str) {
    let inspector = ArchiveReader::new()
        .add_selector(selector)
        .snapshot::<Inspect>()
        .await
        .unwrap()
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .unwrap();

    assert_eq!(inspector.children.len(), 1, "selector must return exactly one child");

    assert_data_tree!(inspector, root: {
        config: {
            my_flag: true,
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

// TODO(http://fxbug.dev/93830): Find a way to use the same test case with different manifests.
#[fuchsia::test]
async fn rust() {
    let puppet =
        fuchsia_component::client::connect_to_protocol_at_path::<ConfigReceiverPuppetMarker>(
            "/svc/test.structuredconfig.receiver.ConfigReceiverPuppet.rust",
        )
        .unwrap();
    assert_eq!(
        puppet.get_config().await.unwrap(),
        expected_config(),
        "child must receive expected configuration"
    );

    assert_inspect_config("rust_receiver:root").await;
}

#[fuchsia::test]
async fn cpp_elf() {
    let puppet =
        fuchsia_component::client::connect_to_protocol_at_path::<ConfigReceiverPuppetMarker>(
            "/svc/test.structuredconfig.receiver.ConfigReceiverPuppet.cpp_elf",
        )
        .unwrap();
    assert_eq!(
        puppet.get_config().await.unwrap(),
        expected_config(),
        "child must receive expected configuration"
    );

    assert_inspect_config("cpp_elf_receiver:root").await;
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_test_structuredconfig_receiver::{ConfigReceiverPuppetMarker, ReceiverConfig};
use std::path::Path;

#[fuchsia::test]
async fn receiver() {
    let puppet =
        fuchsia_component::client::connect_to_protocol::<ConfigReceiverPuppetMarker>().unwrap();

    let expected_config = ReceiverConfig {
        // there's an empty file at this path if the puppet has been packaged with my_flag==true
        my_flag: Path::new("/pkg/data/my_flag_is_true").exists(),
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
    };

    assert_eq!(
        puppet.get_config().await.unwrap(),
        expected_config,
        "child must receive expected configuration"
    );
}

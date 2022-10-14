// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fs_management::partition::{open_partition, PartitionMatcher},
    fuchsia_async as fasync,
    fuchsia_zircon::Duration,
    ramdevice_client::RamdiskClient,
};

const RAMDISK_PATH: &str = "/dev/sys/platform/00:00:2d/ramctl";

#[fasync::run_singlethreaded(test)]
async fn open_partition_test() {
    ramdevice_client::wait_for_device(RAMDISK_PATH, std::time::Duration::from_secs(60)).unwrap();
    let _ramdisk = RamdiskClient::create(1024, 1 << 16).unwrap();
    let matcher = PartitionMatcher {
        parent_device: Some(String::from("/dev/sys/platform")),
        ..Default::default()
    };

    assert_eq!(
        open_partition(matcher, Duration::from_seconds(10)).await.unwrap(),
        RAMDISK_PATH.to_string() + "/ramdisk-0/block"
    );
}

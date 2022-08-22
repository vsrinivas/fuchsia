// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    blackout_target::{dev, find_partition, set_up_partition},
    device_watcher::recursive_wait_and_open_node,
    fuchsia_async as fasync,
    ramdevice_client::RamdiskClient,
};

#[fasync::run_singlethreaded(test)]
async fn setup_then_find_with_path() {
    recursive_wait_and_open_node(&dev(), "sys/platform/00:00:2d/ramctl")
        .await
        .expect("failed to wait");
    let ramdisk = RamdiskClient::create(8192, 128).unwrap();
    let ramdisk_path = ramdisk.get_path();

    let setup_device_path = set_up_partition("test-label", Some(ramdisk_path), false)
        .await
        .expect("failed to set up device");
    assert_eq!(setup_device_path, format!("{}/fvm/test-label-p-1/block", ramdisk_path));

    let find_device_path =
        find_partition("test-label", Some(ramdisk_path)).await.expect("failed to find device");
    assert_eq!(setup_device_path, find_device_path);
}

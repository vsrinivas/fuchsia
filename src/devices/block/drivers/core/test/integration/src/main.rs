// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ramdevice_client::RamdiskClient,
    remote_block_device::{BlockClient, BufferSlice, MutableBufferSlice, RemoteBlockClient},
};

#[fuchsia::test]
async fn test_multiple_sessions() {
    ramdevice_client::wait_for_device(
        "/dev/sys/platform/00:00:2d/ramctl",
        std::time::Duration::from_secs(60),
    )
    .unwrap();
    let ramdisk = RamdiskClient::create(512, 1 << 16).unwrap();
    let device_channel = ramdisk.open().expect("open failed");
    let device_proxy = device_channel.into_proxy().expect("into_proxy failed");
    let block_client1 = RemoteBlockClient::new(device_proxy).await.expect("new failed");
    let device_channel = ramdisk.open().expect("open failed");
    let device_proxy = device_channel.into_proxy().expect("into_proxy failed");
    let block_client2 = RemoteBlockClient::new(device_proxy).await.expect("new failed");

    let data1 = [1; 512];
    block_client1.write_at(BufferSlice::Memory(&data1), 1024).await.expect("write_at failed");

    let mut data2 = [0; 512];
    block_client2
        .read_at(MutableBufferSlice::Memory(&mut data2), 1024)
        .await
        .expect("read_at failed");

    assert_eq!(data1, data2);
}

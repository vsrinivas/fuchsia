// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    log::info,
    test_utils_lib::{
        events::{Event, EventMatcher, Handler, Started},
        opaque_test::OpaqueTest,
    },
};

pub const ONE_MB: u64 = 1048576;
pub const FOUR_MB: u64 = 4 * ONE_MB;
pub const BLOCK_SIZE: u64 = fuchsia_merkle::BLOCK_SIZE as u64;
pub const HASH_SIZE: u64 = fuchsia_hash::HASH_SIZE as u64;

// Sets up an OpaqueTest which creates the v2 components needed for blobfs to be mounted.
// Waits until blobfs is ready to be used. Returns the OpaqueTest object and a path to the
// blobfs root.
pub async fn init_blobfs() -> (OpaqueTest, Directory) {
    let test: OpaqueTest =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/blobfs-stress-test#meta/root.cm")
            .await
            .unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();
    event_source.start_component_tree().await;

    // Expect 4 components to be started
    // (root, isolated-devmgr, driver_manager_test and disk-create)
    let mut expected_monikers = vec![
        "fuchsia-pkg://fuchsia.com/blobfs-stress-test#meta/root.cm",
        "fuchsia-pkg://fuchsia.com/blobfs-stress-test#meta/mounter.cm",
        "fuchsia-pkg://fuchsia.com/isolated-devmgr#meta/isolated_devmgr.cm",
        "fuchsia-pkg://fuchsia.com/isolated-devmgr#meta/driver_manager_test.cm",
    ];

    for _ in 1..=4 {
        let event = event_stream.expect_match::<Started>(EventMatcher::ok()).await;
        let component_url = event.component_url();
        info!("{} has started", component_url);

        // If the moniker is in the list, remove it so it cannot be matched against again
        if let Some(position) = expected_monikers.iter().position(|url| url == &component_url) {
            expected_monikers.remove(position);
        } else {
            panic!("Could not find moniker {} in expected moniker list", component_url);
        }

        event.resume().await.unwrap();
    }

    let blobfs_root_path = test.get_hub_v2_path().join("children/mounter/exec/out/blobfs/root");

    // Wait for blobfs to be mounted
    assert!(blobfs_root_path.exists());

    let root_dir = Directory::from_namespace(blobfs_root_path);

    (test, root_dir)
}

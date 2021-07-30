// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_cobalt::CobaltSender,
    futures::channel::mpsc,
    fuzz::fuzz,
    std::path::Path,
    tempfile::TempDir,
    wlan_common::assert_variant,
    wlancfg_lib::config_management::{
        network_config::{Credential, NetworkIdentifier},
        SavedNetworksManager, SavedNetworksManagerApi,
    },
};

#[fuzz]
async fn fuzz_saved_networks_manager_store(id: NetworkIdentifier, credential: Credential) {
    // Test with fuzzed inputs that if a network is stored, we can look it up again and if it is
    // loaded from stash when SavedNetworksManager is initialized from stash the values of the
    // saved network are correct.
    let stash_id = "store_and_lookup";
    let temp_dir = TempDir::new().expect("failed to create temporary directory");
    let path = temp_dir.path().join("networks.json");
    let tmp_path = temp_dir.path().join("tmp.json");

    // Expect the store to be constructed successfully even if the file doesn't
    // exist yet
    let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

    assert!(saved_networks.lookup(id.clone()).await.is_empty());
    assert_eq!(0, saved_networks.known_network_count().await);

    // Store a fuzzed network identifier and credential.
    assert!(saved_networks
        .store(id.clone(), credential.clone())
        .await
        .expect("storing network failed")
        .is_none());
    assert_variant!(saved_networks.lookup(id.clone()).await.as_slice(),
        [network_config] => {
            assert_eq!(network_config.ssid, id.ssid);
            assert_eq!(network_config.security_type, id.security_type);
            assert_eq!(network_config.credential, credential);
        }
    );
    assert_eq!(1, saved_networks.known_network_count().await);

    // Saved networks should persist when we create a saved networks manager with the same ID.
    let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
        stash_id,
        &path,
        tmp_path,
        create_mock_cobalt_sender(),
    )
    .await
    .expect("failed to create saved networks store");
    assert_variant!(saved_networks.lookup(id.clone()).await.as_slice(),
        [network_config] => {
            assert_eq!(network_config.ssid, id.ssid);
            assert_eq!(network_config.security_type, id.security_type);
            assert_eq!(network_config.credential, credential);
        }
    );
    assert_eq!(1, saved_networks.known_network_count().await);
}

/// Create a saved networks manager and clear the contents. Stash ID should be different for
/// each test so that they don't interfere.
async fn create_saved_networks(
    stash_id: impl AsRef<str>,
    path: impl AsRef<Path>,
    tmp_path: impl AsRef<Path>,
) -> SavedNetworksManager {
    let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
        stash_id,
        &path,
        &tmp_path,
        create_mock_cobalt_sender(),
    )
    .await
    .expect("Failed to create SavedNetworksManager");
    // saved_networks.clear().await.expect("Failed to clear new SavedNetworksManager");
    saved_networks
}

pub fn create_mock_cobalt_sender() -> CobaltSender {
    // Arbitrary number that is (much) larger than the count of metrics we might send to it
    const MOCK_COBALT_MSG_BUFFER: usize = 100;
    let (cobalt_mpsc_sender, _cobalt_mpsc_receiver) = mpsc::channel(MOCK_COBALT_MSG_BUFFER);
    CobaltSender::new(cobalt_mpsc_sender)
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        network_config::{
            Credential, FailureReason, NetworkConfig, NetworkConfigError, NetworkIdentifier,
            SecurityType,
        },
        stash_conversion::*,
    },
    crate::legacy::known_ess_store::{self, EssJsonRead, KnownEss, KnownEssStore},
    anyhow::format_err,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_cobalt::CobaltSender,
    futures::lock::Mutex,
    log::{error, info},
    std::{
        clone::Clone,
        collections::{hash_map::Entry, HashMap},
        fs, io,
        path::Path,
    },
    wlan_metrics_registry::{
        SavedConfigurationsForSavedNetworkMetricDimensionSavedConfigurations,
        SavedNetworksMetricDimensionSavedNetworks,
        SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_METRIC_ID, SAVED_NETWORKS_METRIC_ID,
    },
    wlan_stash::policy::{PolicyStash as Stash, POLICY_STASH_ID},
};

/// The Saved Network Manager keeps track of saved networks and provides thread-safe access to
/// saved networks. Networks are saved by NetworkConfig and accessed by their NetworkIdentifier
/// (SSID and security protocol). Network configs are saved in-memory, and part of each network
/// data is saved persistently. Futures aware locks are used in order to wait for the stash flush
/// operations to complete when data changes.
pub struct SavedNetworksManager {
    saved_networks: Mutex<NetworkConfigMap>,
    stash: Mutex<Stash>,
    legacy_store: KnownEssStore,
    cobalt_api: Mutex<CobaltSender>,
}

/// Save multiple network configs per SSID in able to store multiple connections with different
/// credentials, for different authentication credentials on the same network or for different
/// networks with the same name.
type NetworkConfigMap = HashMap<NetworkIdentifier, Vec<NetworkConfig>>;

const MAX_CONFIGS_PER_SSID: usize = 1;

impl SavedNetworksManager {
    /// Initializes a new Saved Network Manager by reading saved networks from a secure storage
    /// (stash) or from a legacy storage file (from KnownEssStore) if stash is empty. In either
    /// case it initializes in-memory storage and persistent storage with stash to remember
    /// networks.
    pub async fn new(cobalt_api: CobaltSender) -> Result<Self, anyhow::Error> {
        let path = known_ess_store::KNOWN_NETWORKS_PATH;
        let tmp_path = known_ess_store::TMP_KNOWN_NETWORKS_PATH;
        Self::new_with_stash_or_paths(
            POLICY_STASH_ID,
            Path::new(path),
            Path::new(tmp_path),
            cobalt_api,
        )
        .await
    }

    /// Load from persistent data from 1 of 2 places: stash or the file created by KnownEssStore.
    /// For now we need to support reading from the the legacy version (KnownEssStore) as well
    /// from stash. And we need to keep the legacy version temporarily so we will decide where to
    /// read based on whether stash has been used.
    /// TODO(44184) Eventually delete logic for handling legacy storage from KnownEssStore and
    /// update comments once all users have migrated.
    pub async fn new_with_stash_or_paths(
        stash_id: impl AsRef<str>,
        legacy_path: impl AsRef<Path>,
        legacy_tmp_path: impl AsRef<Path>,
        cobalt_api: CobaltSender,
    ) -> Result<Self, anyhow::Error> {
        let mut stash = Stash::new_with_id(stash_id.as_ref())?;
        let stashed_networks = stash.load().await?;
        let mut saved_networks: HashMap<NetworkIdentifier, Vec<NetworkConfig>> = stashed_networks
            .iter()
            .map(|(network_id, persistent_data)| {
                (
                    NetworkIdentifier::from(network_id.clone()),
                    persistent_data
                        .iter()
                        .filter_map(|data| {
                            NetworkConfig::new(
                                NetworkIdentifier::from(network_id.clone()),
                                data.credential.clone().into(),
                                data.has_ever_connected,
                                false,
                            )
                            .ok()
                        })
                        .collect(),
                )
            })
            .collect();

        // Don't read legacy if stash is not empty; we have already migrated.
        if saved_networks.is_empty() {
            Self::migrate_legacy(legacy_path.as_ref(), &mut stash, &mut saved_networks).await?;
        }
        // KnownEssStore will internally load from the correct path.
        let legacy_store = KnownEssStore::new_with_paths(
            legacy_path.as_ref().to_path_buf(),
            legacy_tmp_path.as_ref().to_path_buf(),
        )?;

        Ok(Self {
            saved_networks: Mutex::new(saved_networks),
            stash: Mutex::new(stash),
            legacy_store,
            cobalt_api: Mutex::new(cobalt_api),
        })
    }

    /// Creates a new config at a random path, ensuring a clean environment for an individual test
    #[cfg(test)]
    pub async fn new_for_test() -> Result<Self, anyhow::Error> {
        use crate::util::cobalt::create_mock_cobalt_sender;
        use rand::{distributions::Alphanumeric, thread_rng, Rng};

        let stash_id: String = thread_rng().sample_iter(&Alphanumeric).take(20).collect();
        let path: String = thread_rng().sample_iter(&Alphanumeric).take(20).collect();
        let tmp_path: String = thread_rng().sample_iter(&Alphanumeric).take(20).collect();
        Self::new_with_stash_or_paths(
            stash_id,
            Path::new(&path),
            Path::new(&tmp_path),
            create_mock_cobalt_sender(),
        )
        .await
    }

    /// Creates a new SavedNetworksManager and hands back the other end of the stash proxy used.
    /// This should be used when a test does something that will interact with stash and uses the
    /// executor to step through futures.
    #[cfg(test)]
    pub async fn new_and_stash_server(
        legacy_path: impl AsRef<Path>,
        legacy_tmp_path: impl AsRef<Path>,
    ) -> (Self, fidl_fuchsia_stash::StoreAccessorRequestStream) {
        use crate::util::cobalt::create_mock_cobalt_sender;
        use rand::{distributions::Alphanumeric, thread_rng, Rng};

        let id: String = thread_rng().sample_iter(&Alphanumeric).take(20).collect();
        use fidl::endpoints::create_proxy;
        let (store_client, _stash_server) = create_proxy::<fidl_fuchsia_stash::StoreMarker>()
            .expect("failed to create stash proxy");
        store_client.identify(id.as_ref()).expect("failed to identify client to store");
        let (store, accessor_server) = create_proxy::<fidl_fuchsia_stash::StoreAccessorMarker>()
            .expect("failed to create accessor proxy");
        let stash = Stash::new_with_stash(store);
        let legacy_store = KnownEssStore::new_with_paths(
            legacy_path.as_ref().to_path_buf(),
            legacy_tmp_path.as_ref().to_path_buf(),
        )
        .expect("failed to create legacy store");

        (
            Self {
                saved_networks: Mutex::new(NetworkConfigMap::new()),
                stash: Mutex::new(stash),
                legacy_store,
                cobalt_api: Mutex::new(create_mock_cobalt_sender()),
            },
            accessor_server.into_stream().expect("failed to create stash request stream"),
        )
    }

    /// Read from the old persistent storage of network configs, then write them into the new
    /// storage, both in the hashmap and the stash that stores them persistently.
    async fn migrate_legacy(
        legacy_storage_path: impl AsRef<Path>,
        stash: &mut Stash,
        saved_networks: &mut NetworkConfigMap,
    ) -> Result<(), anyhow::Error> {
        info!("Attempting to migrate saved networks from legacy implementation to stash");
        match Self::load_from_path(&legacy_storage_path) {
            Ok(legacy_saved_networks) => {
                for (net_id, configs) in legacy_saved_networks {
                    stash
                        .write(
                            &net_id.clone().into(),
                            &network_config_vec_to_persistent_data(&configs),
                        )
                        .await?;
                    saved_networks.insert(net_id, configs);
                }
            }
            Err(e) => {
                format_err!("Failed to load legacy networks: {}", e);
            }
        }
        Ok(())
    }

    /// Handles reading networks persisted by the previous version of network storage
    /// (KnownEssStore) for the new store.
    fn load_from_path(storage_path: impl AsRef<Path>) -> Result<NetworkConfigMap, anyhow::Error> {
        // Temporarily read from memory the same way EssStore does.
        let config_list: Vec<EssJsonRead> = match fs::File::open(&storage_path) {
            Ok(file) => match serde_json::from_reader(io::BufReader::new(file)) {
                Ok(list) => list,
                Err(e) => {
                    error!(
                        "Failed to parse the list of known wireless networks from JSONin {}: {}. \
                         Starting with an empty list.",
                        storage_path.as_ref().display(),
                        e
                    );
                    fs::remove_file(&storage_path).map_err(|e| {
                        format_err!("Failed to delete {}: {}", storage_path.as_ref().display(), e)
                    })?;
                    Vec::new()
                }
            },
            Err(e) => match e.kind() {
                io::ErrorKind::NotFound => Vec::new(),
                _ => {
                    return Err(format_err!(
                        "Failed to open {}: {}",
                        storage_path.as_ref().display(),
                        e
                    ))
                }
            },
        };
        let mut saved_networks = HashMap::<NetworkIdentifier, Vec<NetworkConfig>>::new();
        for config in config_list {
            // Choose appropriate unknown values based on password and how known ESS have been used
            // for connections. Credential cannot be read in as PSK - PSK's were not supported by
            // before the new storage was in place.
            let credential = Credential::from_bytes(config.password);
            let network_id =
                NetworkIdentifier::new(config.ssid, credential.derived_security_type());
            if let Ok(network_config) =
                NetworkConfig::new(network_id.clone(), credential, false, false)
            {
                saved_networks.entry(network_id).or_default().push(network_config);
            } else {
                error!(
                    "Error creating network config from loaded data for SSID {}",
                    String::from_utf8_lossy(&network_id.ssid.clone())
                );
            }
        }
        Ok(saved_networks)
    }

    /// Attempt to remove the NetworkConfig described by the specified NetworkIdentifier and
    /// Credential. Return true if a NetworkConfig is remove and false otherwise.
    pub async fn remove(
        &self,
        network_id: NetworkIdentifier,
        credential: Credential,
    ) -> Result<bool, NetworkConfigError> {
        // Find any matching NetworkConfig and remove it.
        let mut saved_networks = self.saved_networks.lock().await;
        if let Some(network_configs) = saved_networks.get_mut(&network_id) {
            let original_len = network_configs.len();
            // Keep the configs that don't match provided NetworkIdentifier and Credential.
            network_configs.retain(|cfg| cfg.credential != credential);
            // Update stash and legacy storage if there was a change
            if original_len != network_configs.len() {
                self.stash
                    .lock()
                    .await
                    .write(
                        &network_id.clone().into(),
                        &network_config_vec_to_persistent_data(&network_configs),
                    )
                    .await
                    .map_err(|_| NetworkConfigError::StashWriteError)?;
                self.legacy_store
                    .remove(network_id.ssid, credential.into_bytes())
                    .map_err(|_| NetworkConfigError::LegacyWriteError)?;
                return Ok(true);
            }
        }
        Ok(false)
    }

    /// Clear the in memory storage and the persistent storage. Also clear the legacy storage.
    pub async fn clear(&self) -> Result<(), anyhow::Error> {
        self.saved_networks.lock().await.clear();
        self.stash.lock().await.clear().await?;
        self.legacy_store.clear()
    }

    /// Get the count of networks in store, including multiple values with same SSID
    pub async fn known_network_count(&self) -> usize {
        self.saved_networks.lock().await.values().into_iter().flatten().count()
    }

    /// Return a list of network configs that match the given SSID.
    pub async fn lookup(&self, id: NetworkIdentifier) -> Vec<NetworkConfig> {
        self.saved_networks.lock().await.entry(id).or_default().iter().map(Clone::clone).collect()
    }

    /// Save a network by SSID and password. If the SSID and password have been saved together
    /// before, do not modify the saved config. Update the legacy storage to keep it consistent
    /// with what it did before the new version.
    pub async fn store(
        &self,
        network_id: NetworkIdentifier,
        credential: Credential,
    ) -> Result<(), NetworkConfigError> {
        let mut saved_networks = self.saved_networks.lock().await;
        let network_entry = saved_networks.entry(network_id.clone());

        if let Entry::Occupied(network_configs) = &network_entry {
            if network_configs.get().iter().any(|cfg| cfg.credential == credential) {
                info!(
                    "Saving a previously saved network with same password: {}",
                    String::from_utf8_lossy(&network_id.ssid)
                );
                return Ok(());
            }
        }
        let network_config =
            NetworkConfig::new(network_id.clone(), credential.clone(), false, false)?;
        let network_configs = network_entry.or_default();
        evict_if_needed(network_configs);
        network_configs.push(network_config);

        self.stash
            .lock()
            .await
            .write(
                &network_id.clone().into(),
                &network_config_vec_to_persistent_data(&network_configs),
            )
            .await
            .map_err(|_| NetworkConfigError::StashWriteError)?;

        // Write saved networks to the legacy store only if they are WPA2 or Open, as legacy store
        // does not support more types. Do not write PSK to legacy storage.
        if let Credential::Psk(_) = credential {
            return Ok(());
        }
        if network_id.security_type == SecurityType::Wpa2
            || network_id.security_type == SecurityType::None
        {
            let ess = KnownEss { password: credential.into_bytes() };
            self.legacy_store
                .store(network_id.ssid, ess)
                .map_err(|_| NetworkConfigError::LegacyWriteError)?;
        }
        Ok(())
    }

    /// Update the specified saved network with the result of an attempted connect.  If the
    /// specified network is not saved, this function does not save it.
    pub async fn record_connect_result(
        &self,
        id: NetworkIdentifier,
        credential: &Credential,
        connect_result: fidl_sme::ConnectResultCode,
    ) {
        let mut saved_networks = self.saved_networks.lock().await;
        if let Some(networks) = saved_networks.get_mut(&id) {
            for network in networks.iter_mut() {
                if &network.credential == credential {
                    match connect_result {
                        fidl_sme::ConnectResultCode::Success => {
                            if !network.has_ever_connected {
                                network.has_ever_connected = true;
                                // Update persistent storage since a config has changed.
                                self.stash
                                    .lock()
                                    .await
                                    .write(
                                        &id.into(),
                                        &network_config_vec_to_persistent_data(&networks),
                                    )
                                    .await
                                    .unwrap_or_else(|_| {
                                        info!(
                                        "Failed recording successful connect in persistent storage"
                                    );
                                    });
                            }
                        }
                        fidl_sme::ConnectResultCode::CredentialRejected => {
                            network.perf_stats.failure_list.add(FailureReason::CredentialRejected);
                        }
                        fidl_sme::ConnectResultCode::WrongCredentialType => {
                            network.perf_stats.failure_list.add(FailureReason::WrongCredentialType);
                        }
                        fidl_sme::ConnectResultCode::Failed => {
                            network.perf_stats.failure_list.add(FailureReason::GeneralFailure);
                        }
                        fidl_sme::ConnectResultCode::Canceled => {}
                    }
                    return;
                }
            }
        }

        // Will not reach here if we find the saved network with matching SSID and credential.
        info!(
            "Failed finding network ({},{:?}) to record result of connect attempt.",
            String::from_utf8_lossy(&id.ssid),
            id.security_type
        );
    }

    pub async fn record_periodic_metrics(&self) {
        let saved_networks = self.saved_networks.lock().await;
        let mut cobalt_api = self.cobalt_api.lock().await;
        log_cobalt_metrics(&*saved_networks, &mut cobalt_api);
    }

    // Return a list of every network config that has been saved.
    pub async fn get_networks(&self) -> Vec<NetworkConfig> {
        self.saved_networks
            .lock()
            .await
            .values()
            .into_iter()
            .map(|cfgs| cfgs.clone())
            .flatten()
            .collect()
    }
}

/// If the list of configs is at capacity for the number of saved configs per SSID,
/// remove a saved network that has never been successfully connected to. If all have
/// been successfully connected to, remove any. If a network config is evicted, that connection
/// is forgotten for future connections.
/// TODO(41232) - when network configs record information about successful connections,
/// use this to make a better decision what to forget if all networks have connected before.
/// TODO(41626) - make sure that we disconnect from the network if we evict a network config
/// for a network we are currently connected to.
fn evict_if_needed(configs: &mut Vec<NetworkConfig>) {
    if configs.len() < MAX_CONFIGS_PER_SSID {
        return;
    }

    for i in 0..configs.len() {
        if let Some(config) = configs.get(i) {
            if !config.has_ever_connected {
                configs.remove(i);
                return;
            }
        }
    }
    // If all saved networks have connected, remove the first network
    configs.remove(0);
}

/// Record Cobalt metrics related to Saved Networks
fn log_cobalt_metrics(saved_networks: &NetworkConfigMap, cobalt_api: &mut CobaltSender) {
    // Count the total number of saved networks
    let num_networks = match saved_networks.len() {
        0 => SavedNetworksMetricDimensionSavedNetworks::Zero,
        1 => SavedNetworksMetricDimensionSavedNetworks::One,
        2..=4 => SavedNetworksMetricDimensionSavedNetworks::TwoToFour,
        5..=40 => SavedNetworksMetricDimensionSavedNetworks::FiveToForty,
        41..=500 => SavedNetworksMetricDimensionSavedNetworks::FortyToFiveHundred,
        501..=usize::MAX => SavedNetworksMetricDimensionSavedNetworks::FiveHundredAndOneOrMore,
        _ => unreachable!(),
    };
    cobalt_api.log_event(SAVED_NETWORKS_METRIC_ID, num_networks);

    // Count the number of configs for each saved network
    for saved_network in saved_networks {
        let configs = saved_network.1;
        use SavedConfigurationsForSavedNetworkMetricDimensionSavedConfigurations as ConfigCountDimension;
        let num_configs = match configs.len() {
            0 => ConfigCountDimension::Zero,
            1 => ConfigCountDimension::One,
            2..=4 => ConfigCountDimension::TwoToFour,
            5..=40 => ConfigCountDimension::FiveToForty,
            41..=500 => ConfigCountDimension::FortyToFiveHundred,
            501..=usize::MAX => ConfigCountDimension::FiveHundredAndOneOrMore,
            _ => unreachable!(),
        };
        cobalt_api.log_event(SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_METRIC_ID, num_configs);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            config_management::PerformanceStats,
            util::cobalt::{create_mock_cobalt_sender, create_mock_cobalt_sender_and_receiver},
        },
        cobalt_client::traits::AsEventCode,
        fidl_fuchsia_cobalt::CobaltEvent,
        fidl_fuchsia_stash as fidl_stash, fuchsia_async as fasync,
        fuchsia_cobalt::cobalt_event_builder::CobaltEventExt,
        futures::{task::Poll, TryStreamExt},
        pin_utils::pin_mut,
        rand::{distributions::Alphanumeric, thread_rng, Rng},
        std::{io::Write, mem, time::SystemTime},
        tempfile::TempDir,
        wlan_common::assert_variant,
    };

    #[fasync::run_singlethreaded(test)]
    async fn store_and_lookup() {
        let stash_id = "store_and_lookup";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id_foo = NetworkIdentifier::new("foo", SecurityType::Wpa2);

        assert!(saved_networks.lookup(network_id_foo.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed");
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(network_id_foo.clone()).await
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        // Store another network with the same SSID.
        saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"12345678".to_vec()))
            .await
            .expect("storing 'foo' a second time failed");

        // There should only be one saved "foo" network because MAX_CONFIGS_PER_SSID is 1.
        // When this constant becomes greater than 1, both network configs should be found
        assert_eq!(
            vec![network_config("foo", "12345678")],
            saved_networks.lookup(network_id_foo.clone()).await
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        // Store another network and verify.
        let network_id_baz = NetworkIdentifier::new("baz", SecurityType::Wpa2);
        let psk = Credential::Psk(vec![1; 32]);
        let config_baz = NetworkConfig::new(network_id_baz.clone(), psk.clone(), false, false)
            .expect("failed to create network config");
        saved_networks
            .store(network_id_baz.clone(), psk)
            .await
            .expect("storing 'baz' with PSK failed");
        assert_eq!(vec![config_baz.clone()], saved_networks.lookup(network_id_baz.clone()).await);
        assert_eq!(2, saved_networks.known_network_count().await);

        // Saved networks should persist when we create a saved networks manager with the same ID.
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");
        assert_eq!(
            vec![network_config("foo", "12345678")],
            saved_networks.lookup(network_id_foo.clone()).await
        );
        assert_eq!(vec![config_baz], saved_networks.lookup(network_id_baz).await);
        assert_eq!(2, saved_networks.known_network_count().await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn store_twice() {
        let stash_id = "store_twice";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);

        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed");
        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' a second time failed");
        let expected_cfgs = vec![network_config("foo", "qwertyuio")];
        assert_eq!(expected_cfgs, saved_networks.lookup(network_id).await);
        assert_eq!(1, saved_networks.known_network_count().await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn store_many_same_ssid() {
        let stash_id = "store_many_same_ssid";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let network_id = NetworkIdentifier::new("foo", SecurityType::Wpa2);
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // save max + 1 networks with same SSID and different credentials
        for i in 0..MAX_CONFIGS_PER_SSID + 1 {
            let mut password = b"password".to_vec();
            password.push(i as u8);
            saved_networks
                .store(network_id.clone(), Credential::Password(password))
                .await
                .expect("Failed to saved network");
        }

        // since none have been connected to yet, we don't care which config was removed
        assert_eq!(MAX_CONFIGS_PER_SSID, saved_networks.lookup(network_id).await.len());
    }

    #[fasync::run_singlethreaded(test)]
    async fn store_and_remove() {
        let stash_id = "store_and_remove";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        let network_id = NetworkIdentifier::new("foo", SecurityType::Wpa2);
        let credential = Credential::Password(b"qwertyuio".to_vec());
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("storing 'foo' failed");
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(network_id.clone()).await
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        // Remove a network with the same NetworkIdentifier but differenct credential and verify
        // that the saved network is unaffected.
        assert_eq!(
            false,
            saved_networks
                .remove(network_id.clone(), Credential::Password(b"diff-password".to_vec()))
                .await
                .expect("removing 'foo' failed")
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        // Remove the network and check it is gone
        assert_eq!(
            true,
            saved_networks
                .remove(network_id.clone(), credential.clone())
                .await
                .expect("removing 'foo' failed")
        );
        assert_eq!(0, saved_networks.known_network_count().await);

        // If we try to remove the network again, we won't get an error and nothing happens
        assert_eq!(
            false,
            saved_networks
                .remove(network_id.clone(), credential)
                .await
                .expect("removing 'foo' failed")
        );

        // Check that removal persists.
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("Failed to create SavedNetworksManager");
        assert_eq!(0, saved_networks.known_network_count().await);
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect_network() {
        let stash_id = "connect_network";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        let network_id = NetworkIdentifier::new("bar", SecurityType::Wpa2);
        let credential = Credential::Password(b"password".to_vec());

        // If connect and network hasn't been saved, we should not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                fidl_sme::ConnectResultCode::Success,
            )
            .await;
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Save the network and record a successful connection.
        saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("Failed save network");

        let config = network_config("bar", "password");
        assert_eq!(vec![config], saved_networks.lookup(network_id.clone()).await);

        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                fidl_sme::ConnectResultCode::Success,
            )
            .await;

        // The network should be saved with the connection recorded.
        let mut config = network_config("bar", "password");
        config.has_ever_connected = true;
        assert_eq!(vec![config], saved_networks.lookup(network_id).await);
        assert_eq!(1, saved_networks.known_network_count().await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_record_connect_failure() {
        let stash_id = "test_record_connect_failure";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id = NetworkIdentifier::new("foo", SecurityType::None);
        let credential = Credential::None;
        let before_recording = SystemTime::now();

        // Verify that recording connect result does not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                fidl_sme::ConnectResultCode::Failed,
            )
            .await;
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Record that the connect failed.
        saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("Failed save network");
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                fidl_sme::ConnectResultCode::CredentialRejected,
            )
            .await;

        // Check that the failure was recorded correctly.
        assert_eq!(1, saved_networks.known_network_count().await);
        let saved_config = saved_networks
            .lookup(network_id)
            .await
            .pop()
            .expect("Failed to get saved network config");
        let mut connect_failures =
            saved_config.perf_stats.failure_list.get_recent(before_recording);
        assert_eq!(1, connect_failures.len());
        let connect_failure = connect_failures.pop().expect("Failed to get a connect failure");
        assert_eq!(FailureReason::CredentialRejected, connect_failure.reason);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_record_connect_cancelled_ignored() {
        let stash_id = "test_record_connect_failure";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id = NetworkIdentifier::new("foo", SecurityType::None);
        let credential = Credential::None;
        let before_recording = SystemTime::now();

        // Verify that recording connect result does not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                fidl_sme::ConnectResultCode::Canceled,
            )
            .await;
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Record that the connect was canceled.
        saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("Failed save network");
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                fidl_sme::ConnectResultCode::Canceled,
            )
            .await;

        // Check that there are no failures recorded for this saved network.
        assert_eq!(1, saved_networks.known_network_count().await);
        let saved_config = saved_networks
            .lookup(network_id)
            .await
            .pop()
            .expect("Failed to get saved network config");
        let connect_failures = saved_config.perf_stats.failure_list.get_recent(before_recording);
        assert_eq!(0, connect_failures.len());
    }

    #[test]
    fn evict_if_needed_removes_unconnected() {
        // this test is less meaningful when MAX_CONFIGS_PER_SSID is greater than 1, otherwise
        // the only saved configs should be removed when the max capacity is met, regardless of
        // whether it has been connected to.
        let unconnected_config = network_config("foo", "password");
        let mut connected_config = unconnected_config.clone();
        connected_config.has_ever_connected = false;
        let mut network_configs = vec![connected_config; MAX_CONFIGS_PER_SSID - 1];
        network_configs.insert(MAX_CONFIGS_PER_SSID / 2, unconnected_config);

        evict_if_needed(&mut network_configs);
        assert_eq!(MAX_CONFIGS_PER_SSID - 1, network_configs.len());
        // check that everything left has been connected to before, only one removed is
        // the one that has never been connected to
        for config in network_configs.iter() {
            assert_eq!(true, config.has_ever_connected);
        }
    }

    #[test]
    fn evict_if_needed_already_has_space() {
        let mut configs = vec![];
        evict_if_needed(&mut configs);
        let expected_cfgs: Vec<NetworkConfig> = vec![];
        assert_eq!(expected_cfgs, configs);

        if MAX_CONFIGS_PER_SSID > 1 {
            let mut configs = vec![network_config("foo", "password")];
            evict_if_needed(&mut configs);
            // if MAX_CONFIGS_PER_SSID is 1, this wouldn't be true
            assert_eq!(vec![network_config("foo", "password")], configs);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn clear() {
        let stash_id = "clear";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed");
        assert!(path.exists());
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(network_id).await
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        saved_networks.clear().await.expect("clearing store failed");
        assert_eq!(0, saved_networks.known_network_count().await);

        // Load store from stash to verify it is also gone from persistent storage
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks manager");

        assert_eq!(0, saved_networks.known_network_count().await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn ignore_legacy_file_bad_format() {
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");
        // Write invalid JSON and close the file
        file.write(b"{").expect("failed to write broken json into file");
        mem::drop(file);
        assert!(path.exists());
        // Constructing a saved network config store should still succeed,
        // but the invalid file should be gone now
        let stash_id = "ignore_legacy_file_bad_format";
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");
        // KnownEssStore deletes the file if it can't read it, as in this case.
        assert!(!path.exists());
        // Writing an entry should not create the file yet because networks configs don't persist.
        assert_eq!(0, saved_networks.known_network_count().await);
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed");

        // There should be a file here again since we stored a network, so one will be created.
        assert!(path.exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_network_from_legacy_storage() {
        // Possible contents of a file generated from KnownEssStore, with networks foo and bar with
        // passwords foobar and password respecitively. Network foo should not be read into new
        // saved network manager because the password is too short for a valid network password.
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        let stash_id = "read_network_from_legacy_storage";
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // We should not delete the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Network bar should have been read into the saved networks manager because it is valid
        assert_eq!(1, saved_networks.known_network_count().await);
        let bar_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        let bar_config = NetworkConfig::new(
            bar_id.clone(),
            Credential::Password(b"password".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![bar_config], saved_networks.lookup(bar_id).await);

        // Network foo should not have been read into saved networks manager because it is invalid.
        let foo_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        assert!(saved_networks.lookup(foo_id).await.is_empty());

        assert!(path.exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn do_not_migrate_networks_twice() {
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        let stash_id = "do_not_migrate_networks_twice";
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // We should not have deleted the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Verify the network config loaded from legacy storage
        let net_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        let net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"password".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![net_config], saved_networks.lookup(net_id.clone()).await);

        // Replace the network 'bar' that was read from legacy version storage
        saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .await
            .expect("failed to store network");
        let new_net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"foobarbaz".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![new_net_config.clone()], saved_networks.lookup(net_id.clone()).await);

        // Recreate the SavedNetworksManager again, as would happen when the device restasts
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // We should not have deleted the file while creating SavedNetworksManager the first time.
        assert!(path.exists());
        // Expect to see the replaced network 'bar'
        assert_eq!(1, saved_networks.known_network_count().await);
        assert_eq!(vec![new_net_config], saved_networks.lookup(net_id).await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn ignore_legacy_if_stash_exists() {
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        let stash_id = "ignore_legacy_if_stash_exists";
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // We should not delete the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Verify the network config loaded from legacy storage
        let net_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        let net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"password".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![net_config], saved_networks.lookup(net_id.clone()).await);

        // Replace the network 'bar' that was read from legacy version storage
        saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .await
            .expect("failed to store network");
        let new_net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"foobarbaz".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![new_net_config.clone()], saved_networks.lookup(net_id.clone()).await);

        // Add legacy store file again as if we had failed to delete it
        let mut file = fs::File::create(&path).expect("failed to open file for writing");
        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        // Recreate the SavedNetworksManager again, as would happen when the device restasts
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // We should ignore the legacy file since there is something in the stash.
        assert_eq!(1, saved_networks.known_network_count().await);
        assert_eq!(vec![new_net_config], saved_networks.lookup(net_id).await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_and_load_legacy() {
        let stash_id = "write_and_load_legacy";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // Save a network, which should write to the legacy store
        let net_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .await
            .expect("failed to store network");

        // Explicitly clear just the stash
        saved_networks.stash.lock().await.clear().await.expect("failed to clear the stash");

        // Create the saved networks manager again to trigger reading from persistent storage
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // Verify that the network was read in from legacy store.
        let net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"foobarbaz".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![net_config.clone()], saved_networks.lookup(net_id.clone()).await);
    }

    #[test]
    fn test_store_waits_for_stash() {
        let mut exec = fasync::Executor::new().expect("failed to create executor");
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join(rand_string());
        let tmp_path = temp_dir.path().join(rand_string());
        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server(path, tmp_path));

        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::None);
        let save_fut = saved_networks.store(network_id, Credential::None);
        pin_mut!(save_fut);

        // Verify that storing the network does not complete until stash responds.
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::SetValue{..})))
        );
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::Flush{responder}))) => {
                responder.send(&mut Ok(())).expect("failed to send stash response");
            }
        );
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(Ok(())));
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
        saved_networks.clear().await.expect("Failed to clear new SavedNetworksManager");
        saved_networks
    }

    /// Convience function for creating network configs with default values as they would be
    /// initialized when read from KnownEssStore. Credential is password or none, and security
    /// type is WPA2 or none.
    fn network_config(ssid: impl Into<Vec<u8>>, password: impl Into<Vec<u8>>) -> NetworkConfig {
        let credential = Credential::from_bytes(password.into());
        NetworkConfig {
            ssid: ssid.into(),
            security_type: credential.derived_security_type(),
            credential,
            has_ever_connected: false,
            seen_in_passive_scan_results: false,
            perf_stats: PerformanceStats::new(),
        }
    }

    fn rand_string() -> String {
        thread_rng().sample_iter(&Alphanumeric).take(20).collect()
    }

    #[fasync::run_singlethreaded(test)]
    async fn record_metrics_when_called_on_class() {
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let (cobalt_api, mut cobalt_events) = create_mock_cobalt_sender_and_receiver();

        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(&stash_id, &path, &tmp_path, cobalt_api)
                .await
                .unwrap();
        let network_id_foo = NetworkIdentifier::new("foo", SecurityType::Wpa2);
        let network_id_baz = NetworkIdentifier::new("baz", SecurityType::Wpa2);

        assert!(saved_networks.lookup(network_id_foo.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed");
        assert_eq!(1, saved_networks.known_network_count().await);

        // Store another network and verify.
        saved_networks
            .store(network_id_baz.clone(), Credential::Psk(vec![1; 32]))
            .await
            .expect("storing 'baz' with PSK failed");
        assert_eq!(2, saved_networks.known_network_count().await);

        // Record metrics
        saved_networks.record_periodic_metrics().await;

        // Two saved networks
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SAVED_NETWORKS_METRIC_ID)
                    .with_event_code(
                        SavedNetworksMetricDimensionSavedNetworks::TwoToFour.as_event_code()
                    )
                    .as_event()
            )
        );

        // One config for each network
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_METRIC_ID)
                    .with_event_code(
                        SavedConfigurationsForSavedNetworkMetricDimensionSavedConfigurations::One
                            .as_event_code()
                    )
                    .as_event()
            )
        );
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_METRIC_ID)
                    .with_event_code(
                        SavedConfigurationsForSavedNetworkMetricDimensionSavedConfigurations::One
                            .as_event_code()
                    )
                    .as_event()
            )
        );

        // No more metrics
        assert!(cobalt_events.try_next().is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn metrics_count_configs() {
        let (mut cobalt_api, mut cobalt_events) = create_mock_cobalt_sender_and_receiver();

        let network_id_foo = NetworkIdentifier::new("foo", SecurityType::Wpa2);
        let network_id_baz = NetworkIdentifier::new("baz", SecurityType::Wpa2);

        let networks: NetworkConfigMap = [
            (network_id_foo, vec![]),
            (
                network_id_baz.clone(),
                vec![
                    NetworkConfig::new(
                        network_id_baz.clone(),
                        Credential::Password(b"qwertyuio".to_vec()),
                        false,
                        false,
                    )
                    .unwrap(),
                    NetworkConfig::new(
                        network_id_baz,
                        Credential::Password(b"asdfasdfasdf".to_vec()),
                        false,
                        false,
                    )
                    .unwrap(),
                ],
            ),
        ]
        .iter()
        .cloned()
        .collect();

        log_cobalt_metrics(&networks, &mut cobalt_api);

        // Two saved networks
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SAVED_NETWORKS_METRIC_ID)
                    .with_event_code(
                        SavedNetworksMetricDimensionSavedNetworks::TwoToFour.as_event_code()
                    )
                    .as_event()
            )
        );

        // Extract the next two events, their order is not guaranteed
        let cobalt_metrics = vec![
            cobalt_events.try_next().unwrap().unwrap(),
            cobalt_events.try_next().unwrap().unwrap(),
        ];
        // Zero configs for one network
        assert!(cobalt_metrics.iter().any(|metric| metric
            == &CobaltEvent::builder(SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_METRIC_ID)
                .with_event_code(
                    SavedConfigurationsForSavedNetworkMetricDimensionSavedConfigurations::Zero
                        .as_event_code()
                )
                .as_event()));
        // Two configs for the other network
        assert!(cobalt_metrics.iter().any(|metric| metric
            == &CobaltEvent::builder(SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_METRIC_ID)
                .with_event_code(
                    SavedConfigurationsForSavedNetworkMetricDimensionSavedConfigurations::TwoToFour
                        .as_event_code()
                )
                .as_event()));

        // No more metrics
        assert!(cobalt_events.try_next().is_err());
    }
}

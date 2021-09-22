// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        network_config::{
            Credential, FailureReason, HiddenProbEvent, NetworkConfig, NetworkConfigError,
            NetworkIdentifier, RssiData, SecurityType,
        },
        stash_conversion::*,
    },
    crate::{
        client::types,
        legacy::known_ess_store::{self, EssJsonRead, KnownEss, KnownEssStore},
    },
    anyhow::format_err,
    async_trait::async_trait,
    fidl_fuchsia_wlan_common::ScanType,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_cobalt::CobaltSender,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    log::{error, info, warn},
    rand::Rng,
    std::{
        clone::Clone,
        collections::{hash_map::Entry, HashMap},
        convert::TryInto,
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

const MAX_CONFIGS_PER_SSID: usize = 1;
// Number of previous RSSI measurements to exponentially weigh into average.
// TODO(fxbug.dev/84870): Tune smoothing factor.
const EWMA_SMOOTHING_FACTOR: u8 = 25;

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

pub enum ScanResultType {
    #[allow(dead_code)]
    Undirected,
    Directed(Vec<types::NetworkIdentifier>), // Contains list of target SSIDs
}

/// Calculates the expontentially weighted moving average RSSI value after a new RSSI measurement,
/// using the previous average and a smoothing factor, which is the number of previous measurements
/// to weigh into the average.
fn calculate_ewma_rssi(previous_ewma: f32, measured_rssi: f32, smoothing_factor: u8) -> f32 {
    (2.0 / (1.0 + smoothing_factor as f32) * measured_rssi)
        + ((1.0 - (2.0 / (1.0 + smoothing_factor as f32))) * previous_ewma)
}

/// Calculates RSSI velocity across vector of RSSI measurements by determining
/// the slope of the line of best fit using least squares regression.
fn calculate_rssi_velocity(historical_rssis: Vec<f32>) -> f32 {
    let n = historical_rssis.len() as f32;
    let mut sum_x = 0.0;
    let mut sum_y = 0.0;
    let mut sum_xy = 0.0;
    let mut sum_x2 = 0.0;

    for (i, y) in historical_rssis.iter().enumerate() {
        let x = i as f32;
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x.powf(2.0);
    }
    (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x.powf(2.0))
}

#[async_trait]
pub trait SavedNetworksManagerApi: Send + Sync {
    /// Attempt to remove the NetworkConfig described by the specified NetworkIdentifier and
    /// Credential. Return true if a NetworkConfig is remove and false otherwise.
    async fn remove(
        &self,
        network_id: NetworkIdentifier,
        credential: Credential,
    ) -> Result<bool, NetworkConfigError>;

    /// Get the count of networks in store, including multiple values with same SSID
    async fn known_network_count(&self) -> usize;

    /// Return a list of network configs that match the given SSID.
    async fn lookup(&self, id: NetworkIdentifier) -> Vec<NetworkConfig>;

    /// Return a list of network configs that could be used with the security type seen in a scan.
    /// This includes configs that have a lower security type that can be upgraded to match the
    /// provided detailed security type.
    async fn lookup_compatible(
        &self,
        ssid: &types::Ssid,
        scan_security: types::SecurityTypeDetailed,
    ) -> Vec<NetworkConfig>;

    /// Save a network by SSID and password. If the SSID and password have been saved together
    /// before, do not modify the saved config. Update the legacy storage to keep it consistent
    /// with what it did before the new version. If a network is pushed out because of the newly
    /// saved network, this will return the removed config.
    async fn store(
        &self,
        network_id: NetworkIdentifier,
        credential: Credential,
    ) -> Result<Option<NetworkConfig>, NetworkConfigError>;

    /// Update the specified saved network with the result of an attempted connect.  If the
    /// specified network could have been connected to with a different security type and we
    /// do not find the specified config, we will check the other possible security type. For
    /// example if a WPA3 network is specified, we will check WPA2 if it isn't found. If the
    /// specified network is not saved, this function does not save it.
    async fn record_connect_result(
        &self,
        id: NetworkIdentifier,
        credential: &Credential,
        bssid: types::Bssid,
        connect_result: fidl_sme::ConnectResultCode,
        discovered_in_scan: Option<ScanType>,
    );

    /// Record the disconnect from a network, to be used for things such as avoiding connections
    /// that drop soon after starting.
    async fn record_disconnect(
        &self,
        id: &NetworkIdentifier,
        credential: &Credential,
        bssid: types::Bssid,
        uptime: zx::Duration,
        curr_time: zx::Time,
    );

    async fn record_periodic_metrics(&self);

    /// Update hidden networks probabilities based on scan results. Record either results of a
    /// passive scan or a directed active scan.
    async fn record_scan_result(
        &self,
        scan_type: ScanResultType,
        results: Vec<types::NetworkIdentifierDetailed>,
    );

    // Return a list of every network config that has been saved.
    async fn get_networks(&self) -> Vec<NetworkConfig>;

    // Updates connection quality information for a given network id. Runs filtering and
    // calculations, stores the values, and returns them.
    // TODO(fxbug.dev/84867): Replace connection_data (f32), which is currently just rssi, with
    // struct when integrating connection quality retrieval.
    async fn record_connection_quality_data(
        &self,
        id: &NetworkIdentifier,
        credential: &Credential,
        bssid: types::Bssid,
        connection_data: f32,
    ) -> Option<RssiData>;
}

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
    /// TODO(fxbug.dev/44184) Eventually delete logic for handling legacy storage from KnownEssStore and
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
        use crate::util::testing::cobalt::create_mock_cobalt_sender;
        use rand::{distributions::Alphanumeric, thread_rng};

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
        use crate::util::testing::cobalt::create_mock_cobalt_sender;
        use rand::{distributions::Alphanumeric, thread_rng};

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
                    if let Some(_) = saved_networks.insert(net_id, configs) {
                        warn!("Overwriting saved network with one from legacy config file");
                    };
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
                NetworkIdentifier::new(config.ssid.try_into()?, credential.derived_security_type());
            if let Ok(network_config) = NetworkConfig::new(network_id.clone(), credential, false) {
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

    /// Clear the in memory storage and the persistent storage. Also clear the legacy storage.
    #[cfg(test)]
    pub async fn clear(&self) -> Result<(), anyhow::Error> {
        self.saved_networks.lock().await.clear();
        self.stash.lock().await.clear().await?;
        self.legacy_store.clear()
    }
}

#[async_trait]
impl SavedNetworksManagerApi for SavedNetworksManager {
    async fn remove(
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
                    .remove(network_id.ssid.to_vec(), credential.into_bytes())
                    .map_err(|_| NetworkConfigError::LegacyWriteError)?;
                return Ok(true);
            } else {
                info!("No matching network with the provided credential was found to remove.");
            }
        } else {
            info!("No network was found to remove with the provided SSID and security.");
        }
        Ok(false)
    }

    async fn known_network_count(&self) -> usize {
        self.saved_networks.lock().await.values().into_iter().flatten().count()
    }

    async fn lookup(&self, id: NetworkIdentifier) -> Vec<NetworkConfig> {
        self.saved_networks.lock().await.entry(id).or_default().iter().map(Clone::clone).collect()
    }

    async fn lookup_compatible(
        &self,
        ssid: &types::Ssid,
        scan_security: types::SecurityTypeDetailed,
    ) -> Vec<NetworkConfig> {
        let mut saved_networks_guard = self.saved_networks.lock().await;
        let mut matching_configs = Vec::new();
        for security in compatible_policy_securities(&scan_security) {
            let id = NetworkIdentifier::new(ssid.clone(), security.into());
            // Check for conflicts; PSKs can't be used to connect to WPA3 networks.
            let configs = saved_networks_guard
                .entry(id)
                .or_default()
                .iter()
                .filter(|config| security_is_compatible(&scan_security, &config.credential))
                .into_iter()
                .map(Clone::clone);
            matching_configs.extend(configs);
        }
        matching_configs
    }

    async fn store(
        &self,
        network_id: NetworkIdentifier,
        credential: Credential,
    ) -> Result<Option<NetworkConfig>, NetworkConfigError> {
        let mut saved_networks = self.saved_networks.lock().await;
        let network_entry = saved_networks.entry(network_id.clone());

        if let Entry::Occupied(network_configs) = &network_entry {
            if network_configs.get().iter().any(|cfg| cfg.credential == credential) {
                info!("Saving a previously saved network with same password.");
                return Ok(None);
            }
        }
        let network_config = NetworkConfig::new(network_id.clone(), credential.clone(), false)?;
        let network_configs = network_entry.or_default();
        let evicted_config = evict_if_needed(network_configs);
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
            return Ok(evicted_config);
        }
        if network_id.security_type == SecurityType::Wpa2
            || network_id.security_type == SecurityType::None
        {
            let ess = KnownEss { password: credential.into_bytes() };
            self.legacy_store
                .store(network_id.ssid.clone(), ess)
                .map_err(|_| NetworkConfigError::LegacyWriteError)?;
        }
        Ok(evicted_config)
    }

    async fn record_connect_result(
        &self,
        id: NetworkIdentifier,
        credential: &Credential,
        bssid: types::Bssid,
        connect_result: fidl_sme::ConnectResultCode,
        discovered_in_scan: Option<ScanType>,
    ) {
        let mut saved_networks = self.saved_networks.lock().await;
        let networks = match saved_networks.get_mut(&id) {
            Some(networks) => networks,
            None => {
                error!("Failed to find network to record result of connect attempt.");
                return;
            }
        };
        for network in networks.iter_mut() {
            if &network.credential == credential {
                match connect_result {
                    fidl_sme::ConnectResultCode::Success => {
                        let mut has_change = false;
                        if !network.has_ever_connected {
                            network.has_ever_connected = true;
                            has_change = true;
                        }
                        if let Some(scan_type) = discovered_in_scan {
                            let connect_event = match scan_type {
                                ScanType::Passive => HiddenProbEvent::ConnectPassive,
                                ScanType::Active => HiddenProbEvent::ConnectActive,
                            };
                            network.update_hidden_prob(connect_event);
                            // TODO(60619): Update the stash with new probability if it has changed
                        }
                        if has_change {
                            // Update persistent storage since a config has changed.
                            let data = network_config_vec_to_persistent_data(&networks);
                            if let Err(e) = self.stash.lock().await.write(&id.into(), &data).await {
                                info!("Failed to record successful connect in stash: {}", e);
                            }
                        }
                    }
                    fidl_sme::ConnectResultCode::CredentialRejected => {
                        network
                            .perf_stats
                            .failure_list
                            .add(bssid, FailureReason::CredentialRejected);
                    }
                    fidl_sme::ConnectResultCode::Failed => {
                        network.perf_stats.failure_list.add(bssid, FailureReason::GeneralFailure);
                    }
                    fidl_sme::ConnectResultCode::Canceled => {}
                }
                return;
            }
        }
        // Will not reach here if we find the saved network with matching SSID and credential.
        error!("Failed to find matching network to record result of connect attempt.");
    }

    async fn record_disconnect(
        &self,
        id: &NetworkIdentifier,
        credential: &Credential,
        bssid: types::Bssid,
        uptime: zx::Duration,
        curr_time: zx::Time,
    ) {
        let mut saved_networks = self.saved_networks.lock().await;
        let networks = match saved_networks.get_mut(&id) {
            Some(networks) => networks,
            None => {
                info!("Failed to find network to record disconnect stats");
                return;
            }
        };
        for network in networks.iter_mut() {
            if &network.credential == credential {
                network.perf_stats.disconnect_list.add(bssid, uptime, curr_time);
            }
        }
    }

    async fn record_periodic_metrics(&self) {
        let saved_networks = self.saved_networks.lock().await;
        let mut cobalt_api = self.cobalt_api.lock().await;
        log_cobalt_metrics(&*saved_networks, &mut cobalt_api);
    }

    async fn record_scan_result(
        &self,
        scan_type: ScanResultType,
        results: Vec<types::NetworkIdentifierDetailed>,
    ) {
        let mut saved_networks = self.saved_networks.lock().await;
        match scan_type {
            ScanResultType::Undirected => {
                // For each network we have seen, look for compatible configs and record results.
                for scan_id in results {
                    for security in compatible_policy_securities(&scan_id.security_type) {
                        let configs = match saved_networks
                            .get_mut(&NetworkIdentifier::new(scan_id.ssid.clone(), security))
                        {
                            Some(configs) => configs,
                            None => continue,
                        };
                        // Check that the credential is compatible with the actual security type of
                        // the scan result.
                        let compatible_configs = configs.iter_mut().filter(|config| {
                            security_is_compatible(&scan_id.security_type, &config.credential)
                        });
                        for config in compatible_configs {
                            config.update_hidden_prob(HiddenProbEvent::SeenPassive)
                        }
                        // TODO(60619): Update the stash with new probability if it has changed
                    }
                }
            }
            ScanResultType::Directed(target_ids) => {
                // For each config of each targeted ID, check whether there is a scan result that
                // could be used to connect. If not, update the hidden probability.
                for target_id in target_ids.into_iter() {
                    let configs = match saved_networks.get_mut(&target_id.clone().into()) {
                        Some(configs) => configs,
                        None => continue,
                    };
                    let potential_scans = results
                        .iter()
                        .filter(|scan_id| scan_id.ssid == target_id.ssid)
                        .collect::<Vec<_>>();
                    for config in configs {
                        if let None = potential_scans.iter().find(|scan_id| {
                            compatible_policy_securities(&scan_id.security_type)
                                .contains(&config.security_type)
                                && security_is_compatible(
                                    &scan_id.security_type,
                                    &config.credential,
                                )
                        }) {
                            config.update_hidden_prob(HiddenProbEvent::NotSeenActive);
                        }
                        // TODO(60619): Update the stash with new probability if it has changed
                    }
                }
            }
        }
    }

    async fn get_networks(&self) -> Vec<NetworkConfig> {
        self.saved_networks
            .lock()
            .await
            .values()
            .into_iter()
            .map(|cfgs| cfgs.clone())
            .flatten()
            .collect()
    }

    async fn record_connection_quality_data(
        &self,
        id: &NetworkIdentifier,
        credential: &Credential,
        bssid: types::Bssid,
        connection_data: f32,
    ) -> Option<RssiData> {
        // Find saved networks matching network id.
        let mut saved_networks = self.saved_networks.lock().await;
        let networks = match saved_networks.get_mut(&id) {
            Some(networks) => networks,
            None => {
                error!("Failed to find network to record connection quality data.");
                return None;
            }
        };
        // Find network matching credential
        for network in networks.iter_mut() {
            if &network.credential == credential {
                let rssi_data = match network.perf_stats.rssi_data_by_bssid.get(&bssid) {
                    Some(rssi_data) => {
                        let ewma_rssi = calculate_ewma_rssi(
                            rssi_data.rssi,
                            connection_data,
                            EWMA_SMOOTHING_FACTOR,
                        );
                        // TODO(fxbug.dev/84872): Use historical RSSI values to calculate smoothed
                        // velocity.
                        let velocity = calculate_rssi_velocity(vec![rssi_data.rssi, ewma_rssi]);
                        RssiData { rssi: ewma_rssi, velocity: velocity }
                    }
                    None => RssiData { rssi: connection_data, velocity: 0.0 },
                };
                let _ = network.perf_stats.rssi_data_by_bssid.insert(bssid, rssi_data.clone());
                return Some(rssi_data);
            }
        }
        error!("Failed to find matching network to record connection quality data.");
        return None;
    }
}

/// Returns a subset of potentially hidden saved networks, filtering probabilistically based
/// on how certain they are to be hidden.
pub fn select_subset_potentially_hidden_networks(
    saved_networks: Vec<NetworkConfig>,
) -> Vec<types::NetworkIdentifier> {
    saved_networks
        .into_iter()
        .filter(|saved_network| {
            // Roll a dice to see if we should scan for it. The function gen_range(low, high)
            // has an inclusive lower bound and exclusive upper bound, so using it as
            // `hidden_probability > gen_range(0, 1)` means that:
            // - hidden_probability of 1 will _always_ be selected
            // - hidden_probability of 0 will _never_ be selected
            saved_network.hidden_probability > rand::thread_rng().gen_range(0.0, 1.0)
        })
        .map(|network| types::NetworkIdentifier {
            ssid: network.ssid,
            security_type: network.security_type.into(),
        })
        .collect()
}

/// Returns a list of security types that could be optionally upgraded to match with this detailed
/// security type. For example, a WPA2/WPA3 could be connected to using a WPA2 config or a WPA3
/// config.
pub fn compatible_policy_securities(
    detailed_security: &types::SecurityTypeDetailed,
) -> Vec<SecurityType> {
    use fidl_sme::Protection::*;
    match detailed_security {
        Wpa3Enterprise | Wpa3Personal | Wpa2Wpa3Personal => {
            vec![SecurityType::Wpa2, SecurityType::Wpa3]
        }
        Wpa2Enterprise
        | Wpa2Personal
        | Wpa1Wpa2Personal
        | Wpa2PersonalTkipOnly
        | Wpa1Wpa2PersonalTkipOnly => vec![SecurityType::Wpa, SecurityType::Wpa2],
        Wpa1 => vec![SecurityType::Wpa],
        Wep => vec![SecurityType::Wep],
        Open => vec![SecurityType::None],
        Unknown => vec![],
    }
}

pub fn security_is_compatible(
    scan_security: &types::SecurityTypeDetailed,
    credential: &Credential,
) -> bool {
    if scan_security == &types::SecurityTypeDetailed::Wpa3Personal
        || scan_security == &types::SecurityTypeDetailed::Wpa3Enterprise
    {
        if let Credential::Psk(_) = credential {
            return false;
        }
    }
    true
}

/// If the list of configs is at capacity for the number of saved configs per SSID,
/// remove a saved network that has never been successfully connected to. If all have
/// been successfully connected to, remove any. If a network config is evicted, that connection
/// is forgotten for future connections.
/// TODO(fxbug.dev/41232) - when network configs record information about successful connections,
/// use this to make a better decision what to forget if all networks have connected before.
/// TODO(fxbug.dev/41626) - make sure that we disconnect from the network if we evict a network config
/// for a network we are currently connected to.
fn evict_if_needed(configs: &mut Vec<NetworkConfig>) -> Option<NetworkConfig> {
    if configs.len() < MAX_CONFIGS_PER_SSID {
        return None;
    }

    for i in 0..configs.len() {
        if let Some(config) = configs.get(i) {
            if !config.has_ever_connected {
                return Some(configs.remove(i));
            }
        }
    }
    // If all saved networks have connected, remove the first network
    return Some(configs.remove(0));
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
            config_management::{
                Disconnect, PROB_HIDDEN_DEFAULT, PROB_HIDDEN_IF_CONNECT_ACTIVE,
                PROB_HIDDEN_IF_CONNECT_PASSIVE, PROB_HIDDEN_IF_SEEN_PASSIVE,
            },
            util::testing::cobalt::{
                create_mock_cobalt_sender, create_mock_cobalt_sender_and_receiver,
            },
        },
        cobalt_client::traits::AsEventCode,
        fidl_fuchsia_cobalt::CobaltEvent,
        fidl_fuchsia_stash as fidl_stash, fuchsia_async as fasync,
        fuchsia_cobalt::cobalt_event_builder::CobaltEventExt,
        futures::{task::Poll, TryStreamExt},
        pin_utils::pin_mut,
        rand::{distributions::Alphanumeric, thread_rng, Rng},
        std::{convert::TryFrom, io::Write, mem},
        tempfile::TempDir,
        test_case::test_case,
        test_util::{assert_gt, assert_lt},
        wlan_common::assert_variant,
    };

    #[fuchsia::test]
    async fn store_and_lookup() {
        let stash_id = "store_and_lookup";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id_foo = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();

        assert!(saved_networks.lookup(network_id_foo.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        assert!(saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed")
            .is_none());
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(network_id_foo.clone()).await
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        // Store another network with the same SSID.
        let popped_network = saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"12345678".to_vec()))
            .await
            .expect("storing 'foo' a second time failed");
        assert_eq!(popped_network, Some(network_config("foo", "qwertyuio")));

        // There should only be one saved "foo" network because MAX_CONFIGS_PER_SSID is 1.
        // When this constant becomes greater than 1, both network configs should be found
        assert_eq!(
            vec![network_config("foo", "12345678")],
            saved_networks.lookup(network_id_foo.clone()).await
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        // Store another network and verify.
        let network_id_baz = NetworkIdentifier::try_from("baz", SecurityType::Wpa2).unwrap();
        let psk = Credential::Psk(vec![1; 32]);
        let config_baz = NetworkConfig::new(network_id_baz.clone(), psk.clone(), false)
            .expect("failed to create network config");
        assert!(saved_networks
            .store(network_id_baz.clone(), psk)
            .await
            .expect("storing 'baz' with PSK failed")
            .is_none());
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

    #[fuchsia::test]
    async fn store_twice() {
        let stash_id = "store_twice";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();

        assert!(saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed")
            .is_none());
        let popped_network = saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' a second time failed");
        // Because the same network was stored twice, nothing was evicted, so popped_network == None
        assert_eq!(popped_network, None);
        let expected_cfgs = vec![network_config("foo", "qwertyuio")];
        assert_eq!(expected_cfgs, saved_networks.lookup(network_id).await);
        assert_eq!(1, saved_networks.known_network_count().await);
    }

    #[fuchsia::test]
    async fn store_many_same_ssid() {
        let stash_id = "store_many_same_ssid";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // save max + 1 networks with same SSID and different credentials
        for i in 0..MAX_CONFIGS_PER_SSID + 1 {
            let mut password = b"password".to_vec();
            password.push(i as u8);
            let popped_network = saved_networks
                .store(network_id.clone(), Credential::Password(password))
                .await
                .expect("Failed to saved network");
            if i >= MAX_CONFIGS_PER_SSID {
                assert!(popped_network.is_some());
            } else {
                assert!(popped_network.is_none());
            }
        }

        // since none have been connected to yet, we don't care which config was removed
        assert_eq!(MAX_CONFIGS_PER_SSID, saved_networks.lookup(network_id).await.len());
    }

    #[fuchsia::test]
    async fn store_and_remove() {
        let stash_id = "store_and_remove";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Password(b"qwertyuio".to_vec());
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        assert!(saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("storing 'foo' failed")
            .is_none());
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

    #[fuchsia::test]
    fn sme_protection_converts_to_lower_compatible() {
        use fidl_sme::Protection::*;
        let lower_compatible_pairs = vec![
            (Wpa3Enterprise, vec![SecurityType::Wpa2, SecurityType::Wpa3]),
            (Wpa3Personal, vec![SecurityType::Wpa2, SecurityType::Wpa3]),
            (Wpa2Wpa3Personal, vec![SecurityType::Wpa2, SecurityType::Wpa3]),
            (Wpa2Enterprise, vec![SecurityType::Wpa, SecurityType::Wpa2]),
            (Wpa2Personal, vec![SecurityType::Wpa, SecurityType::Wpa2]),
            (Wpa1Wpa2Personal, vec![SecurityType::Wpa, SecurityType::Wpa2]),
            (Wpa2PersonalTkipOnly, vec![SecurityType::Wpa, SecurityType::Wpa2]),
            (Wpa1Wpa2PersonalTkipOnly, vec![SecurityType::Wpa, SecurityType::Wpa2]),
            (Wpa1, vec![SecurityType::Wpa]),
            (Wep, vec![SecurityType::Wep]),
            (Open, vec![SecurityType::None]),
            (Unknown, vec![]),
        ];
        for (detailed_security, security) in lower_compatible_pairs {
            assert_eq!(compatible_policy_securities(&detailed_security), security);
        }
    }

    #[fuchsia::test]
    async fn lookup_compatible_returns_both_compatible_configs() {
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // Store a couple of network configs that could both be use to connect to a WPA2/WPA3
        // network.
        let ssid = types::Ssid::try_from("foo").unwrap();
        let network_id_wpa2 = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa2);
        let network_id_wpa3 = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa3);
        let credential_wpa2 = Credential::Password(b"password".to_vec());
        let credential_wpa3 = Credential::Password(b"wpa3-password".to_vec());
        assert!(saved_networks
            .store(network_id_wpa2.clone(), credential_wpa2.clone())
            .await
            .expect("Failed to store network")
            .is_none());
        assert!(saved_networks
            .store(network_id_wpa3.clone(), credential_wpa3.clone())
            .await
            .expect("Failed to store network")
            .is_none());
        // Store a network with the same SSID but a not-compatible security type.
        let network_id_wep = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa);
        assert!(saved_networks
            .store(network_id_wep.clone(), Credential::Password(b"abcdefgh".to_vec()))
            .await
            .expect("Failed to store network")
            .is_none());

        let results = saved_networks
            .lookup_compatible(&ssid, types::SecurityTypeDetailed::Wpa2Wpa3Personal)
            .await;
        let expected_config_wpa2 = NetworkConfig::new(network_id_wpa2, credential_wpa2, false)
            .expect("Failed to create config");
        let expected_config_wpa3 = NetworkConfig::new(network_id_wpa3, credential_wpa3, false)
            .expect("Failed to create config");
        assert_eq!(results.len(), 2);
        assert!(results.contains(&expected_config_wpa2));
        assert!(results.contains(&expected_config_wpa3));
    }

    #[test_case(types::SecurityTypeDetailed::Wpa3Personal)]
    #[test_case(types::SecurityTypeDetailed::Wpa3Enterprise)]
    #[fuchsia::test(add_test_attr = false)]
    fn lookup_compatible_does_not_return_wpa3_psk(
        wpa3_detailed_security: types::SecurityTypeDetailed,
    ) {
        let mut exec = fasync::TestExecutor::new().expect("failed to create executor");
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        let saved_networks =
            exec.run_singlethreaded(create_saved_networks(stash_id, &path, &tmp_path));

        // Store a WPA3 config with a password that will match and a PSK config that won't match
        // to a WPA3 network.
        let ssid = types::Ssid::try_from("foo").unwrap();
        let network_id_psk = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa2);
        let network_id_password = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa3);
        let credential_psk = Credential::Psk(vec![5; 32]);
        let credential_password = Credential::Password(b"mypassword".to_vec());
        assert!(exec
            .run_singlethreaded(
                saved_networks.store(network_id_psk.clone(), credential_psk.clone()),
            )
            .expect("Failed to store network")
            .is_none());
        assert!(exec
            .run_singlethreaded(
                saved_networks.store(network_id_password.clone(), credential_password.clone()),
            )
            .expect("Failed to store network")
            .is_none());

        // Only the WPA3 config with a credential should be returned.
        let expected_config_wpa3 =
            NetworkConfig::new(network_id_password, credential_password, false)
                .expect("Failed to create configc");
        let results = exec
            .run_singlethreaded(saved_networks.lookup_compatible(&ssid, wpa3_detailed_security));
        assert_eq!(results, vec![expected_config_wpa3]);
    }

    #[fuchsia::test]
    async fn connect_network() {
        let stash_id = "connect_network";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        let network_id = NetworkIdentifier::try_from("bar", SecurityType::Wpa2).unwrap();
        let credential = Credential::Password(b"password".to_vec());
        let bssid = types::Bssid([4; 6]);

        // If connect and network hasn't been saved, we should not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Success,
                None,
            )
            .await;
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Save the network and record a successful connection.
        assert!(saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());

        let config = network_config("bar", "password");
        assert_eq!(vec![config], saved_networks.lookup(network_id.clone()).await);

        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Success,
                None,
            )
            .await;

        // The network should be saved with the connection recorded. We should not have recorded
        // that the network was connected to passively or actively.
        assert_variant!(saved_networks.lookup(network_id.clone()).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
            assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
        });

        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Success,
                Some(ScanType::Active),
            )
            .await;
        // We should now see that we connected to the network after an active scan.
        assert_variant!(saved_networks.lookup(network_id.clone()).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_CONNECT_ACTIVE);
        });

        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Success,
                Some(ScanType::Passive),
            )
            .await;
        // The config should have a lower hidden probability after connecting after a passive scan.
        assert_variant!(saved_networks.lookup(network_id.clone()).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_CONNECT_PASSIVE);
        });

        // Success connects should be saved as persistent data.
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("Failed to create SavedNetworksManager");
        assert_variant!(saved_networks.lookup(network_id).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
        });
    }

    #[fuchsia::test]
    async fn test_record_connect_updates_one() {
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let net_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let net_id_also_valid = NetworkIdentifier::try_from("foo", SecurityType::Wpa).unwrap();
        let credential = Credential::Password(b"some_password".to_vec());
        let bssid = types::Bssid([2; 6]);

        // Save the networks and record a successful connection.
        assert!(saved_networks
            .store(net_id.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());
        assert!(saved_networks
            .store(net_id_also_valid.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());
        saved_networks
            .record_connect_result(
                net_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Success,
                None,
            )
            .await;

        assert_variant!(saved_networks.lookup(net_id).await.as_slice(), [config] => {
            assert!(config.has_ever_connected);
        });
        // If the specified network identifier is found, record_conenct_result should not mark
        // another config even if it could also have been used for the connect attempt.
        assert_variant!(saved_networks.lookup(net_id_also_valid).await.as_slice(), [config] => {
            assert!(!config.has_ever_connected);
        });
    }

    #[fuchsia::test]
    async fn test_record_connect_failure() {
        let stash_id = "test_record_connect_failure";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::None).unwrap();
        let credential = Credential::None;
        let bssid = types::Bssid([1; 6]);
        let before_recording = zx::Time::get_monotonic();

        // Verify that recording connect result does not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Failed,
                None,
            )
            .await;
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Record that the connect failed.
        assert!(saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Failed,
                None,
            )
            .await;
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::CredentialRejected,
                None,
            )
            .await;

        // Check that the failures were recorded correctly.
        assert_eq!(1, saved_networks.known_network_count().await);
        let saved_config = saved_networks
            .lookup(network_id)
            .await
            .pop()
            .expect("Failed to get saved network config");
        let connect_failures = saved_config.perf_stats.failure_list.get_recent(before_recording);
        assert_variant!(connect_failures, failures => {
            // There are 2 failures. One is a general failure and one rejected credentials failure.
            assert_eq!(failures.len(), 2);
            assert!(failures.iter().any(|failure| failure.reason == FailureReason::GeneralFailure));
            assert!(failures.iter().any(|failure| failure.reason == FailureReason::CredentialRejected));
            // Both failures have the correct BSSID
            for failure in failures.iter() {
                assert_eq!(failure.bssid, bssid);
                assert_eq!(failure.bssid, bssid);
            }
        });
    }

    #[fuchsia::test]
    async fn test_record_connect_cancelled_ignored() {
        let stash_id = "test_record_connect_failure";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::None).unwrap();
        let credential = Credential::None;
        let bssid = types::Bssid([0; 6]);
        let before_recording = zx::Time::get_monotonic();

        // Verify that recording connect result does not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid.clone(),
                fidl_sme::ConnectResultCode::Canceled,
                None,
            )
            .await;
        assert!(saved_networks.lookup(network_id.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Record that the connect was canceled.
        assert!(saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResultCode::Canceled,
                None,
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

    #[fuchsia::test]
    async fn test_record_disconnect() {
        let stash_id = "test_record_connect_failure";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Psk(vec![1; 32]);
        let bssid = types::Bssid([1; 6]);
        let recording_time = zx::Time::get_monotonic();
        let uptime = zx::Duration::from_seconds(1);

        saved_networks.record_disconnect(&id, &credential, bssid, uptime, recording_time).await;
        // Verify that nothing happens if the network was not already saved.
        assert_eq!(saved_networks.known_network_count().await, 0);

        // Save the network and record a disconnect.
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        saved_networks.record_disconnect(&id, &credential, bssid, uptime, recording_time).await;

        // Check that a disconnect was recorded for the network
        let disconnects = saved_networks
            .lookup(id)
            .await
            .pop()
            .expect("Failed to get saved network")
            .perf_stats
            .disconnect_list
            .get_recent(zx::Time::ZERO);
        assert_variant!(disconnects.as_slice(), [disconnect] => {
            assert_eq!(disconnect, &Disconnect {uptime, bssid, time: recording_time});
        })
    }

    #[fuchsia::test]
    async fn test_record_passive_scan() {
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let saved_seen_id = NetworkIdentifier::try_from("foo", SecurityType::None).unwrap();
        let saved_seen_network = types::NetworkIdentifierDetailed {
            ssid: saved_seen_id.ssid.clone(),
            security_type: types::SecurityTypeDetailed::Open,
        };
        let unsaved_id = NetworkIdentifier::try_from("bar", SecurityType::Wpa2).unwrap();
        let unsaved_network = types::NetworkIdentifierDetailed {
            ssid: unsaved_id.ssid.clone(),
            security_type: types::SecurityTypeDetailed::Wpa2Personal,
        };
        let saved_unseen_id = NetworkIdentifier::try_from("baz", SecurityType::Wpa2).unwrap();
        let seen_credential = Credential::None;
        let unseen_credential = Credential::Password(b"password".to_vec());

        // Save the networks
        assert!(saved_networks
            .store(saved_seen_id.clone(), seen_credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        assert!(saved_networks
            .store(saved_unseen_id.clone(), unseen_credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());

        // Record passive scan results, including the saved network and another network.
        let seen_networks = vec![saved_seen_network, unsaved_network];
        saved_networks.record_scan_result(ScanResultType::Undirected, seen_networks).await;

        assert_variant!(saved_networks.lookup(saved_seen_id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_SEEN_PASSIVE);
        });
        assert_variant!(saved_networks.lookup(saved_unseen_id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
        });
    }

    #[fuchsia::test]
    async fn test_record_undirected_scan_with_upgraded_security() {
        // Test that if we see a different compatible (higher) scan result for a saved network that
        // could be used to connect, recording the scan results will change the hidden probability.
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let id = NetworkIdentifier::try_from("foobar", SecurityType::Wpa2).unwrap();
        let credential = Credential::Password(b"credential".to_vec());

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());

        // Record passive scan results
        let seen_networks = vec![types::NetworkIdentifierDetailed {
            ssid: id.ssid.clone(),
            security_type: types::SecurityTypeDetailed::Wpa3Personal,
        }];
        saved_networks.record_scan_result(ScanResultType::Undirected, seen_networks).await;
        // The network was seen in a passive scan, so hidden probability should be updated.
        assert_variant!(saved_networks.lookup(id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_SEEN_PASSIVE);
        });
    }

    #[fuchsia::test]
    async fn test_record_undirected_scan_incompatible_credential() {
        // Test that if we see a different compatible (higher) scan result for a saved network that
        // could be used to connect, recording the scan results will change the hidden probability.
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let id = NetworkIdentifier::try_from("foobar", SecurityType::Wpa2).unwrap();
        let credential = Credential::Psk(vec![8; 32]);

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());

        // Record passive scan results, including the saved network and another network.
        let seen_networks = vec![types::NetworkIdentifierDetailed {
            ssid: id.ssid.clone(),
            security_type: types::SecurityTypeDetailed::Wpa3Personal,
        }];
        saved_networks.record_scan_result(ScanResultType::Undirected, seen_networks).await;
        // The network in the passive scan results was not compatible, so hidden probability should
        // not have been updated.
        assert_variant!(saved_networks.lookup(id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
        });
    }

    #[fuchsia::test]
    async fn test_record_directed_scan_for_upgraded_security() {
        // Test that if we see a different compatible (higher) scan result for a saved network that
        // could be used to connect in a directed scan, the hidden probability will not be lowered.
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let id = NetworkIdentifier::try_from("foobar", SecurityType::Wpa).unwrap();
        let credential = Credential::Password(b"credential".to_vec());

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config =
            saved_networks.lookup(id.clone()).await.pop().expect("failed to lookup config");
        assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);

        // Record directed scan results. The config's probability hidden should not be lowered
        // since we did not fail to see it in a directed scan.
        let seen_networks = vec![types::NetworkIdentifierDetailed {
            ssid: id.ssid.clone(),
            security_type: types::SecurityTypeDetailed::Wpa2Personal,
        }];
        let target = vec![types::NetworkIdentifier {
            ssid: id.ssid.clone(),
            security_type: types::SecurityType::Wpa,
        }];
        saved_networks.record_scan_result(ScanResultType::Directed(target), seen_networks).await;

        let config =
            saved_networks.lookup(id.clone()).await.pop().expect("failed to lookup config");
        assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
    }

    #[fuchsia::test]
    async fn test_record_directed_scan_for_incompatible_credential() {
        // Test that if we see a network that is not compatible because of the saved credential
        // (but is otherwise compatible), the directed scan is not considered successful and the
        // hidden probability of the config is lowered.
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Psk(vec![11; 32]);

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config =
            saved_networks.lookup(id.clone()).await.pop().expect("failed to lookup config");
        assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);

        // Record directed scan results. The seen network does not match the saved network even
        // though security is compatible, since the security type is not compatible with the PSK.
        let target = vec![types::NetworkIdentifier {
            ssid: id.ssid.clone(),
            security_type: types::SecurityType::Wpa2,
        }];
        let seen_networks = vec![types::NetworkIdentifierDetailed {
            ssid: id.ssid.clone(),
            security_type: types::SecurityTypeDetailed::Wpa3Personal,
        }];
        saved_networks.record_scan_result(ScanResultType::Directed(target), seen_networks).await;
        // The hidden probability should have been lowered because a directed scan failed to find
        // the network.
        let config =
            saved_networks.lookup(id.clone()).await.pop().expect("failed to lookup config");
        assert!(config.hidden_probability < PROB_HIDDEN_DEFAULT);
    }

    #[fuchsia::test]
    async fn test_record_directed_scan_no_ssid_match() {
        // Test that recording directed active scan results does not mistakenly match a config with
        // a network with a different SSID.
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Psk(vec![11; 32]);
        let diff_ssid = types::Ssid::try_from("other-ssid").unwrap();

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config =
            saved_networks.lookup(id.clone()).await.pop().expect("failed to lookup config");
        assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);

        // Record directed scan results. We target the saved network but see a different one.
        let target = vec![types::NetworkIdentifier {
            ssid: id.ssid.clone(),
            security_type: types::SecurityType::Wpa2,
        }];
        let seen_networks = vec![types::NetworkIdentifierDetailed {
            ssid: diff_ssid,
            security_type: types::SecurityTypeDetailed::Wpa2Personal,
        }];
        saved_networks.record_scan_result(ScanResultType::Directed(target), seen_networks).await;

        let config = saved_networks.lookup(id).await.pop().expect("failed to lookup config");
        assert!(config.hidden_probability < PROB_HIDDEN_DEFAULT);
    }

    #[fuchsia::test]
    async fn test_record_directed_one_not_compatible_one_compatible() {
        // Test that if we see two networks with the same SSID but only one is compatible, the scan
        // is recorded as successful for the config. In other words it isn't mistakenly recorded as
        // a failure because of the config that isn't compatible.
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Password(b"foo-pass".to_vec());

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config =
            saved_networks.lookup(id.clone()).await.pop().expect("failed to lookup config");
        assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);

        // Record directed scan results. We see one network with the same SSID that doesn't match,
        // and one that does match.
        let target = vec![types::NetworkIdentifier {
            ssid: id.ssid.clone(),
            security_type: types::SecurityType::Wpa2,
        }];
        let seen_networks = vec![
            types::NetworkIdentifierDetailed {
                ssid: id.ssid.clone(),
                security_type: types::SecurityTypeDetailed::Wpa1,
            },
            types::NetworkIdentifierDetailed {
                ssid: id.ssid.clone(),
                security_type: types::SecurityTypeDetailed::Wpa2Personal,
            },
        ];
        saved_networks.record_scan_result(ScanResultType::Directed(target), seen_networks).await;
        // Since the directed scan found a matching network, the hidden probability should not
        // have been lowered.
        let config = saved_networks.lookup(id).await.pop().expect("failed to lookup config");
        assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
    }

    #[fuchsia::test]
    fn evict_if_needed_removes_unconnected() {
        // this test is less meaningful when MAX_CONFIGS_PER_SSID is greater than 1, otherwise
        // the only saved configs should be removed when the max capacity is met, regardless of
        // whether it has been connected to.
        let unconnected_config = network_config("foo", "password");
        let mut connected_config = unconnected_config.clone();
        connected_config.has_ever_connected = false;
        let mut network_configs = vec![connected_config; MAX_CONFIGS_PER_SSID - 1];
        network_configs.insert(MAX_CONFIGS_PER_SSID / 2, unconnected_config.clone());

        assert_eq!(evict_if_needed(&mut network_configs), Some(unconnected_config));
        assert_eq!(MAX_CONFIGS_PER_SSID - 1, network_configs.len());
        // check that everything left has been connected to before, only one removed is
        // the one that has never been connected to
        for config in network_configs.iter() {
            assert_eq!(true, config.has_ever_connected);
        }
    }

    #[fuchsia::test]
    fn evict_if_needed_already_has_space() {
        let mut configs = vec![];
        assert_eq!(evict_if_needed(&mut configs), None);
        let expected_cfgs: Vec<NetworkConfig> = vec![];
        assert_eq!(expected_cfgs, configs);

        if MAX_CONFIGS_PER_SSID > 1 {
            let mut configs = vec![network_config("foo", "password")];
            assert_eq!(evict_if_needed(&mut configs), None);
            // if MAX_CONFIGS_PER_SSID is 1, this wouldn't be true
            assert_eq!(vec![network_config("foo", "password")], configs);
        }
    }

    #[fuchsia::test]
    async fn clear() {
        let stash_id = "clear";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        assert!(saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed")
            .is_none());
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

    #[fuchsia::test]
    async fn ignore_legacy_file_bad_format() {
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");
        // Write invalid JSON and close the file
        assert_eq!(file.write(b"{").expect("failed to write broken json into file"), 1);
        mem::drop(file);
        assert!(path.exists());
        // Constructing a saved network config store should still succeed,
        // but the invalid file should be gone now
        let stash_id = rand_string();
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            &stash_id,
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
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        assert!(saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed")
            .is_none());

        // There should be a file here again since we stored a network, so one will be created.
        assert!(path.exists());
    }

    #[fuchsia::test]
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

        assert_eq!(file.write(contents).expect("Failed to write to file"), contents.len());
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
        let bar_id = NetworkIdentifier::try_from("bar", SecurityType::Wpa2).unwrap();
        let bar_config =
            NetworkConfig::new(bar_id.clone(), Credential::Password(b"password".to_vec()), false)
                .expect("failed to create network config");
        assert_eq!(vec![bar_config], saved_networks.lookup(bar_id).await);

        // Network foo should not have been read into saved networks manager because it is invalid.
        let foo_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        assert!(saved_networks.lookup(foo_id).await.is_empty());

        assert!(path.exists());
    }

    #[fuchsia::test]
    async fn do_not_migrate_networks_twice() {
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        assert_eq!(file.write(contents).expect("Failed to write to file"), contents.len());
        file.flush().expect("failed to flush contents of file");

        let stash_id = rand_string();
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            &stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // We should not have deleted the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Verify the network config loaded from legacy storage
        let net_id = NetworkIdentifier::try_from("bar", SecurityType::Wpa2).unwrap();
        let net_config =
            NetworkConfig::new(net_id.clone(), Credential::Password(b"password".to_vec()), false)
                .expect("failed to create network config");
        assert_eq!(vec![net_config.clone()], saved_networks.lookup(net_id.clone()).await);

        // Replace the network 'bar' that was read from legacy version storage
        let popped_network = saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .await
            .expect("failed to store network");
        assert_eq!(popped_network, Some(net_config));
        let new_net_config =
            NetworkConfig::new(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()), false)
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

    #[fuchsia::test]
    async fn ignore_legacy_if_stash_exists() {
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        assert_eq!(file.write(contents).expect("Failed to write to file"), contents.len());
        file.flush().expect("failed to flush contents of file");

        let stash_id = rand_string();
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            &stash_id,
            &path,
            &tmp_path,
            create_mock_cobalt_sender(),
        )
        .await
        .expect("failed to create saved networks store");

        // We should not delete the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Verify the network config loaded from legacy storage
        let net_id = NetworkIdentifier::try_from("bar", SecurityType::Wpa2).unwrap();
        let net_config =
            NetworkConfig::new(net_id.clone(), Credential::Password(b"password".to_vec()), false)
                .expect("failed to create network config");
        assert_eq!(vec![net_config.clone()], saved_networks.lookup(net_id.clone()).await);

        // Replace the network 'bar' that was read from legacy version storage
        let popped_network = saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .await
            .expect("failed to store network");
        assert_eq!(popped_network, Some(net_config));
        let new_net_config =
            NetworkConfig::new(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()), false)
                .expect("failed to create network config");
        assert_eq!(vec![new_net_config.clone()], saved_networks.lookup(net_id.clone()).await);

        // Add legacy store file again as if we had failed to delete it
        let mut file = fs::File::create(&path).expect("failed to open file for writing");
        assert_eq!(file.write(contents).expect("Failed to write to file"), contents.len());
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

    #[fuchsia::test]
    async fn write_and_load_legacy() {
        let stash_id = "write_and_load_legacy";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // Save a network, which should write to the legacy store
        let net_id = NetworkIdentifier::try_from("bar", SecurityType::Wpa2).unwrap();
        assert!(saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .await
            .expect("failed to store network")
            .is_none());

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
        let net_config =
            NetworkConfig::new(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()), false)
                .expect("failed to create network config");
        assert_eq!(vec![net_config.clone()], saved_networks.lookup(net_id.clone()).await);
    }

    #[fuchsia::test]
    fn test_store_waits_for_stash() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create executor");
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join(rand_string());
        let tmp_path = temp_dir.path().join(rand_string());
        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server(path, tmp_path));

        let network_id = NetworkIdentifier::try_from("foo", SecurityType::None).unwrap();
        let save_fut = saved_networks.store(network_id, Credential::None);
        pin_mut!(save_fut);

        // Verify that storing the network does not complete until stash responds.
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::SetValue { .. })))
        );
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::Flush{responder}))) => {
                responder.send(&mut Ok(())).expect("failed to send stash response");
            }
        );
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(Ok(None)));
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
    fn network_config(ssid: &str, password: impl Into<Vec<u8>>) -> NetworkConfig {
        let credential = Credential::from_bytes(password.into());
        let id = NetworkIdentifier::try_from(ssid, credential.derived_security_type()).unwrap();
        let has_ever_connected = false;
        NetworkConfig::new(id, credential, has_ever_connected).unwrap()
    }

    fn rand_string() -> String {
        thread_rng().sample_iter(&Alphanumeric).take(20).collect()
    }

    #[fuchsia::test]
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
        let network_id_foo = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let network_id_baz = NetworkIdentifier::try_from("baz", SecurityType::Wpa2).unwrap();

        assert!(saved_networks.lookup(network_id_foo.clone()).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        assert!(saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed")
            .is_none());
        assert_eq!(1, saved_networks.known_network_count().await);

        // Store another network and verify.
        assert!(saved_networks
            .store(network_id_baz.clone(), Credential::Psk(vec![1; 32]))
            .await
            .expect("storing 'baz' with PSK failed")
            .is_none());
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

    #[fuchsia::test]
    async fn metrics_count_configs() {
        let (mut cobalt_api, mut cobalt_events) = create_mock_cobalt_sender_and_receiver();

        let network_id_foo = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let network_id_baz = NetworkIdentifier::try_from("baz", SecurityType::Wpa2).unwrap();

        let networks: NetworkConfigMap = [
            (network_id_foo, vec![]),
            (
                network_id_baz.clone(),
                vec![
                    NetworkConfig::new(
                        network_id_baz.clone(),
                        Credential::Password(b"qwertyuio".to_vec()),
                        false,
                    )
                    .unwrap(),
                    NetworkConfig::new(
                        network_id_baz,
                        Credential::Password(b"asdfasdfasdf".to_vec()),
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

    #[fuchsia::test]
    async fn probabilistic_choosing_of_hidden_networks() {
        // Create three networks with 1, 0, 0.5 hidden probability
        let id_hidden = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("hidden").unwrap(),
            security_type: types::SecurityType::Wpa2,
        };
        let mut net_config_hidden = NetworkConfig::new(
            id_hidden.clone().into(),
            Credential::Password(b"password".to_vec()),
            false,
        )
        .expect("failed to create network config");
        net_config_hidden.hidden_probability = 1.0;

        let id_not_hidden = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("not_hidden").unwrap(),
            security_type: types::SecurityType::Wpa2,
        };
        let mut net_config_not_hidden = NetworkConfig::new(
            id_not_hidden.clone().into(),
            Credential::Password(b"password".to_vec()),
            false,
        )
        .expect("failed to create network config");
        net_config_not_hidden.hidden_probability = 0.0;

        let id_maybe_hidden = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("maybe_hidden").unwrap(),
            security_type: types::SecurityType::Wpa2,
        };
        let mut net_config_maybe_hidden = NetworkConfig::new(
            id_maybe_hidden.clone().into(),
            Credential::Password(b"password".to_vec()),
            false,
        )
        .expect("failed to create network config");
        net_config_maybe_hidden.hidden_probability = 0.5;

        let mut maybe_hidden_selection_count = 0;
        let mut hidden_selection_count = 0;

        // Run selection many times, to ensure the probability is working as expected.
        for _ in 1..100 {
            let selected_networks = select_subset_potentially_hidden_networks(vec![
                net_config_hidden.clone(),
                net_config_not_hidden.clone(),
                net_config_maybe_hidden.clone(),
            ]);
            // The 1.0 probability should always be picked
            assert!(selected_networks.contains(&id_hidden));
            // The 0 probability should never be picked
            assert!(!selected_networks.contains(&id_not_hidden));

            // Keep track of how often the networks were selected
            if selected_networks.contains(&id_maybe_hidden) {
                maybe_hidden_selection_count += 1;
            }
            if selected_networks.contains(&id_hidden) {
                hidden_selection_count += 1;
            }
        }

        // The 0.5 probability network should be picked at least once, but not every time. With 100
        // runs, the chances of either of these assertions flaking is 1 / (0.5^100), i.e. 1 in 1e30.
        // Even with a hypothetical 1,000,000 test runs per day, there would be an average of 1e24
        // days between flakes due to this test.
        assert!(maybe_hidden_selection_count > 0);
        assert!(maybe_hidden_selection_count < hidden_selection_count);
    }

    #[fuchsia::test]
    async fn test_record_not_seen_active_scan() {
        // Test that if we update that we haven't seen a couple of networks in active scans, their
        // hidden probability is updated.
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // Seen in active scans
        let id_1 = NetworkIdentifier::try_from("foo", SecurityType::Wpa).unwrap();
        let credential_1 = Credential::Password(b"some_password".to_vec());
        let id_2 = NetworkIdentifier::try_from("bar", SecurityType::Wpa3).unwrap();
        let credential_2 = Credential::Password(b"another_password".to_vec());
        // Seen in active scan but not saved
        let id_3 = NetworkIdentifier::try_from("baz", SecurityType::None).unwrap();
        // Saved and targeted in active scan but not seen
        let id_4 = NetworkIdentifier::try_from("foobar", SecurityType::None).unwrap();
        let credential_4 = Credential::None;

        // Save 3 of the 4 networks
        assert!(saved_networks
            .store(id_1.clone(), credential_1)
            .await
            .expect("failed to store network")
            .is_none());
        assert!(saved_networks
            .store(id_2.clone(), credential_2)
            .await
            .expect("failed to store network")
            .is_none());
        assert!(saved_networks
            .store(id_4.clone(), credential_4)
            .await
            .expect("failed to store network")
            .is_none());
        // Check that the saved networks have the default hidden probability so later we can just
        // check that the probability has changed.
        let config_1 = saved_networks.lookup(id_1.clone()).await.pop().expect("failed to lookup");
        assert_eq!(config_1.hidden_probability, PROB_HIDDEN_DEFAULT);
        let config_2 = saved_networks.lookup(id_2.clone()).await.pop().expect("failed to lookup");
        assert_eq!(config_2.hidden_probability, PROB_HIDDEN_DEFAULT);
        let config_4 = saved_networks.lookup(id_4.clone()).await.pop().expect("failed to lookup");
        assert_eq!(config_4.hidden_probability, PROB_HIDDEN_DEFAULT);

        let seen_ids = vec![];
        let not_seen_ids = vec![id_1.clone().into(), id_2.clone().into(), id_3.clone().into()];
        saved_networks.record_scan_result(ScanResultType::Directed(not_seen_ids), seen_ids).await;

        // Check that the configs' hidden probability has decreased
        let config_1 = saved_networks.lookup(id_1).await.pop().expect("failed to lookup");
        assert!(config_1.hidden_probability < PROB_HIDDEN_DEFAULT);
        let config_2 = saved_networks.lookup(id_2).await.pop().expect("failed to lookup");
        assert!(config_2.hidden_probability < PROB_HIDDEN_DEFAULT);

        // Check that for the network that was target but not seen in the active scan, its hidden
        // probability isn't lowered.
        let config_4 = saved_networks.lookup(id_4.clone()).await.pop().expect("failed to lookup");
        assert_eq!(config_4.hidden_probability, PROB_HIDDEN_DEFAULT);

        // Check that a config was not saved for the identifier that was not saved before.
        assert!(saved_networks.lookup(id_3).await.is_empty());
    }

    #[fuchsia::test]
    fn test_ewma_rssi_calculations() {
        assert_eq!(calculate_ewma_rssi(-25.0, -35.0, 25), -25.769232);
        assert_eq!(calculate_ewma_rssi(-90.0, -65.0, 12), -86.153846);
        assert_eq!(calculate_ewma_rssi(-50.4524, -41.2564, 45), -50.052578);
    }

    #[fuchsia::test]
    fn test_rssi_velocity_calculations() {
        assert_eq!(calculate_rssi_velocity(vec![-45.0, -48.0]), -3.0);
        assert_eq!(calculate_rssi_velocity(vec![-48.0, -45.0]), 3.0);
        assert_eq!(calculate_rssi_velocity(vec![-60.256, -55.128, -57.512, -60.256]), -0.23840332);
        assert_eq!(calculate_rssi_velocity(vec![-30.0, -32.0, -30.0, -30.0]), 0.2);
        assert_eq!(calculate_rssi_velocity(vec![-25.0, -25.0, -25.0, -25.0, -25.0, -25.0]), 0.0);
    }

    #[fuchsia::test]
    async fn test_record_connection_quality_data() {
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let net_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let net_id_also_valid = NetworkIdentifier::try_from("foo", SecurityType::Wpa).unwrap();
        let credential = Credential::Password(b"some_password".to_vec());
        let bssid = types::Bssid([2; 6]);

        // Save the network.
        assert!(saved_networks
            .store(net_id.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());
        assert!(saved_networks
            .store(net_id_also_valid.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());

        // Record connection quality data
        let response = saved_networks
            .record_connection_quality_data(&net_id, &credential, bssid, -50.0)
            .await
            .expect("failed to get RSSI data response.");

        // Verify connection quality data we recorded. Values should be the initial
        // values, since its the first recording.
        let saved_config = saved_networks
            .lookup(net_id.clone())
            .await
            .pop()
            .expect("Failed to get saved network config");
        let rssi_data = saved_config
            .perf_stats
            .rssi_data_by_bssid
            .get(&bssid)
            .expect("failed to get rssi data.");
        assert_eq!(response, *rssi_data);
        assert_eq!(rssi_data.rssi, -50.0);
        assert_eq!(rssi_data.velocity, 0.0);

        // Record second quality connection data
        let response = saved_networks
            .record_connection_quality_data(&net_id, &credential, bssid, -51.0)
            .await
            .expect("failed to get RSSI data response.");

        // Verify connection quality data was updated. This is non-determinstic and depends on the
        // weighting parameters. But the RSSI should be smoothed to between the two values, and the
        // velocity should be negative.
        let saved_config =
            saved_networks.lookup(net_id).await.pop().expect("Failed to get saved network config");
        let rssi_data = saved_config
            .perf_stats
            .rssi_data_by_bssid
            .get(&bssid)
            .expect("failed to get rssi data.");
        assert_eq!(response, *rssi_data);
        assert_lt!(rssi_data.rssi, -50.0);
        assert_gt!(rssi_data.rssi, -51.0);
        assert_lt!(rssi_data.velocity, 0.0);
    }
}

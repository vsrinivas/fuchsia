// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        network_config::{
            ConnectFailure, Credential, FailureReason, HiddenProbEvent, NetworkConfig,
            NetworkConfigError, NetworkIdentifier, PastConnectionData, PastConnectionList,
            SecurityType,
        },
        stash_conversion::*,
    },
    crate::{
        client::types,
        telemetry::{TelemetryEvent, TelemetrySender},
    },
    anyhow::format_err,
    async_trait::async_trait,
    fidl_fuchsia_metrics::{MetricEvent, MetricEventPayload},
    fidl_fuchsia_wlan_common::ScanType,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_async as fasync,
    futures::lock::Mutex,
    log::{error, info},
    rand::Rng,
    std::{
        clone::Clone,
        collections::{hash_map::Entry, HashMap, HashSet},
        fs,
        path::Path,
    },
    wlan_metrics_registry::{
        SavedConfigurationsForSavedNetworkMigratedMetricDimensionSavedConfigurations,
        SavedNetworksMigratedMetricDimensionSavedNetworks,
        SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_MIGRATED_METRIC_ID,
        SAVED_NETWORKS_MIGRATED_METRIC_ID,
    },
    wlan_stash::policy::{PolicyStash as Stash, POLICY_STASH_ID},
};

const MAX_CONFIGS_PER_SSID: usize = 1;

pub const LEGACY_KNOWN_NETWORKS_PATH: &str = "/data/known_networks.json";

/// The Saved Network Manager keeps track of saved networks and provides thread-safe access to
/// saved networks. Networks are saved by NetworkConfig and accessed by their NetworkIdentifier
/// (SSID and security protocol). Network configs are saved in-memory, and part of each network
/// data is saved persistently. Futures aware locks are used in order to wait for the stash flush
/// operations to complete when data changes.
pub struct SavedNetworksManager {
    saved_networks: Mutex<NetworkConfigMap>,
    stash: Mutex<Stash>,
    telemetry_sender: TelemetrySender,
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
    async fn lookup(&self, id: &NetworkIdentifier) -> Vec<NetworkConfig>;

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
        connect_result: fidl_sme::ConnectResult,
        discovered_in_scan: Option<ScanType>,
    );

    /// Record the disconnect from a network, to be used for things such as avoiding connections
    /// that drop soon after starting.
    async fn record_disconnect(
        &self,
        id: &NetworkIdentifier,
        credential: &Credential,
        data: PastConnectionData,
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

    // Get the list of past connections for a specific BSS
    async fn get_past_connections(
        &self,
        id: &NetworkIdentifier,
        credential: &Credential,
        bssid: &types::Bssid,
    ) -> PastConnectionList;
}

impl SavedNetworksManager {
    /// Initializes a new Saved Network Manager by reading saved networks from a secure storage
    /// (stash). It initializes in-memory storage and persistent storage with stash.
    pub async fn new(telemetry_sender: TelemetrySender) -> Result<Self, anyhow::Error> {
        let path = LEGACY_KNOWN_NETWORKS_PATH;
        Self::new_with_stash_or_paths(POLICY_STASH_ID, Path::new(path), telemetry_sender).await
    }

    /// Load from persistent data from stash. The path for the legacy storage is used to remove the
    /// legacy storage if it exists.
    /// TODO(fxbug.dev/85337) Eventually delete logic for deleting legacy storage
    pub async fn new_with_stash_or_paths(
        stash_id: impl AsRef<str>,
        legacy_path: impl AsRef<Path>,
        telemetry_sender: TelemetrySender,
    ) -> Result<Self, anyhow::Error> {
        let stash = Stash::new_with_id(stash_id.as_ref())?;
        let stashed_networks = stash.load().await?;
        let saved_networks: HashMap<NetworkIdentifier, Vec<NetworkConfig>> = stashed_networks
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

        // Clean up the legacy storage file since it is no longer used.
        if let Err(e) = Self::delete_from_path(legacy_path.as_ref()) {
            info!("Failed to delete legacy storage file: {}", e);
        }

        Ok(Self {
            saved_networks: Mutex::new(saved_networks),
            stash: Mutex::new(stash),
            telemetry_sender,
        })
    }

    /// Creates a new config with a random stash ID, ensuring a clean environment for an individual
    /// test
    #[cfg(test)]
    pub async fn new_for_test() -> Result<Self, anyhow::Error> {
        use {
            futures::channel::mpsc,
            rand::{
                distributions::{Alphanumeric, DistString as _},
                thread_rng,
            },
        };

        let stash_id = Alphanumeric.sample_string(&mut thread_rng(), 20);
        let path = Alphanumeric.sample_string(&mut thread_rng(), 20);
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);
        Self::new_with_stash_or_paths(stash_id, Path::new(&path), telemetry_sender).await
    }

    /// Creates a new SavedNetworksManager and hands back the other end of the stash proxy used.
    /// This should be used when a test does something that will interact with stash and uses the
    /// executor to step through futures.
    #[cfg(test)]
    pub async fn new_and_stash_server() -> (Self, fidl_fuchsia_stash::StoreAccessorRequestStream) {
        use {
            futures::channel::mpsc,
            rand::{
                distributions::{Alphanumeric, DistString as _},
                thread_rng,
            },
        };

        let id = Alphanumeric.sample_string(&mut thread_rng(), 20);
        use fidl::endpoints::create_proxy;
        let (store_client, _stash_server) = create_proxy::<fidl_fuchsia_stash::StoreMarker>()
            .expect("failed to create stash proxy");
        store_client.identify(id.as_ref()).expect("failed to identify client to store");
        let (store, accessor_server) = create_proxy::<fidl_fuchsia_stash::StoreAccessorMarker>()
            .expect("failed to create accessor proxy");
        let stash = Stash::new_with_stash(store);
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);

        (
            Self {
                saved_networks: Mutex::new(NetworkConfigMap::new()),
                stash: Mutex::new(stash),
                telemetry_sender,
            },
            accessor_server.into_stream().expect("failed to create stash request stream"),
        )
    }

    /// Delete the legacy storage file at the specified path
    fn delete_from_path(storage_path: impl AsRef<Path>) -> Result<(), anyhow::Error> {
        if storage_path.as_ref().exists() {
            fs::remove_file(storage_path)
                .map_err(|e| format_err!("Failed to delete legacy storage: {}", e))
        } else {
            Ok(())
        }
    }

    /// Clear the in memory storage and the persistent storage. Also clear the legacy storage.
    #[cfg(test)]
    pub async fn clear(&self) -> Result<(), anyhow::Error> {
        self.saved_networks.lock().await.clear();
        self.stash.lock().await.clear().await
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
                // If there was only one config with this ID before removing it, remove the ID.
                if network_configs.is_empty() {
                    _ = saved_networks.remove(&network_id);
                }
                return Ok(true);
            } else {
                // Log whether there were any matching credential types without logging specific
                // network data
                let credential_types = network_configs
                    .iter()
                    .map(|nc| nc.credential.type_str())
                    .collect::<HashSet<_>>();
                if credential_types.contains(credential.type_str()) {
                    info!("No matching network with the provided credential was found to remove.");
                } else {
                    info!(
                        "No credential matching type {:?} found to remove for this network identifier. Help: found credential type(s): {:?}",
                        credential.type_str(), credential_types
                    );
                }
            }
        } else {
            // Check whether there is another network with the same SSID but different security
            // type to remove.
            let mut found_securities = SecurityType::list_variants();
            found_securities.retain(|security| {
                let id = NetworkIdentifier::new(network_id.ssid.clone(), *security);
                saved_networks.contains_key(&id)
            });
            if found_securities.is_empty() {
                info!("No network was found to remove with the provided SSID.");
            } else {
                info!(
                    "No config to remove with security type {:?}. Help: found different config(s) for this SSID with security {:?}",
                    network_id.security_type, found_securities
                );
            }
        }
        Ok(false)
    }

    /// Get the count of networks in store, including multiple values with same SSID
    async fn known_network_count(&self) -> usize {
        self.saved_networks.lock().await.values().into_iter().flatten().count()
    }

    async fn lookup(&self, id: &NetworkIdentifier) -> Vec<NetworkConfig> {
        self.saved_networks.lock().await.get(id).cloned().unwrap_or_default()
    }

    async fn lookup_compatible(
        &self,
        ssid: &types::Ssid,
        scan_security: types::SecurityTypeDetailed,
    ) -> Vec<NetworkConfig> {
        let saved_networks_guard = self.saved_networks.lock().await;
        let mut matching_configs = Vec::new();
        for security in compatible_policy_securities(&scan_security) {
            let id = NetworkIdentifier::new(ssid.clone(), security);
            let saved_configs = saved_networks_guard.get(&id);
            if let Some(configs) = saved_configs {
                matching_configs.extend(
                    configs
                        .iter()
                        // Check for conflicts; PSKs can't be used to connect to WPA3 networks.
                        .filter(|config| security_is_compatible(&scan_security, &config.credential))
                        .into_iter()
                        .map(Clone::clone),
                );
            }
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

        Ok(evicted_config)
    }

    async fn record_connect_result(
        &self,
        id: NetworkIdentifier,
        credential: &Credential,
        bssid: types::Bssid,
        connect_result: fidl_sme::ConnectResult,
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
                match (connect_result.code, connect_result.is_credential_rejected) {
                    (fidl_ieee80211::StatusCode::Success, _) => {
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
                    (fidl_ieee80211::StatusCode::Canceled, _) => {}
                    (_, true) => {
                        network.perf_stats.connect_failures.add(
                            bssid,
                            ConnectFailure {
                                time: fasync::Time::now(),
                                reason: FailureReason::CredentialRejected,
                                bssid: bssid,
                            },
                        );
                    }
                    (_, _) => {
                        network.perf_stats.connect_failures.add(
                            bssid,
                            ConnectFailure {
                                time: fasync::Time::now(),
                                reason: FailureReason::GeneralFailure,
                                bssid: bssid,
                            },
                        );
                    }
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
        data: PastConnectionData,
    ) {
        let bssid = data.bssid;
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
                network.perf_stats.past_connections.add(bssid, data);
                return;
            }
        }
    }

    async fn record_periodic_metrics(&self) {
        let saved_networks = self.saved_networks.lock().await;
        log_cobalt_metrics(&*saved_networks, &self.telemetry_sender);
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
                    let configs = match saved_networks.get_mut(&target_id.clone()) {
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
            .flat_map(|cfgs| cfgs.clone())
            .collect()
    }

    async fn get_past_connections(
        &self,
        id: &NetworkIdentifier,
        credential: &Credential,
        bssid: &types::Bssid,
    ) -> PastConnectionList {
        self.saved_networks
            .lock()
            .await
            .get(id)
            .and_then(|configs| configs.iter().find(|config| &config.credential == credential))
            .map(|config| config.perf_stats.past_connections.get_list_for_bss(bssid))
            .unwrap_or_default()
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
            // Roll a dice to see if we should scan for it. The function gen_range(low..high)
            // has an inclusive lower bound and exclusive upper bound, so using it as
            // `hidden_probability > gen_range(0..1)` means that:
            // - hidden_probability of 1 will _always_ be selected
            // - hidden_probability of 0 will _never_ be selected
            saved_network.hidden_probability > rand::thread_rng().gen_range(0.0..1.0)
        })
        .map(|network| types::NetworkIdentifier {
            ssid: network.ssid,
            security_type: network.security_type,
        })
        .collect()
}

/// Gets compatible `SecurityType`s for network candidates.
///
/// This function returns a sequence of `SecurityType`s that may be used to connect to a network
/// configured as described by the given `SecurityTypeDetailed`. If there is no compatible
/// `SecurityType`, then the sequence will be empty.
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
fn log_cobalt_metrics(saved_networks: &NetworkConfigMap, telemetry_sender: &TelemetrySender) {
    // Count the total number of saved networks
    let mut metric_events = vec![];

    let num_networks = match saved_networks.len() {
        0 => SavedNetworksMigratedMetricDimensionSavedNetworks::Zero,
        1 => SavedNetworksMigratedMetricDimensionSavedNetworks::One,
        2..=4 => SavedNetworksMigratedMetricDimensionSavedNetworks::TwoToFour,
        5..=40 => SavedNetworksMigratedMetricDimensionSavedNetworks::FiveToForty,
        41..=500 => SavedNetworksMigratedMetricDimensionSavedNetworks::FortyToFiveHundred,
        501..=usize::MAX => {
            SavedNetworksMigratedMetricDimensionSavedNetworks::FiveHundredAndOneOrMore
        }
        _ => unreachable!(),
    };
    metric_events.push(MetricEvent {
        metric_id: SAVED_NETWORKS_MIGRATED_METRIC_ID,
        event_codes: vec![num_networks as u32],
        payload: MetricEventPayload::Count(1),
    });

    // Count the number of configs for each saved network
    for saved_network in saved_networks {
        let configs = saved_network.1;
        use SavedConfigurationsForSavedNetworkMigratedMetricDimensionSavedConfigurations as ConfigCountDimension;
        let num_configs = match configs.len() {
            0 => ConfigCountDimension::Zero,
            1 => ConfigCountDimension::One,
            2..=4 => ConfigCountDimension::TwoToFour,
            5..=40 => ConfigCountDimension::FiveToForty,
            41..=500 => ConfigCountDimension::FortyToFiveHundred,
            501..=usize::MAX => ConfigCountDimension::FiveHundredAndOneOrMore,
            _ => unreachable!(),
        };
        metric_events.push(MetricEvent {
            metric_id: SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_MIGRATED_METRIC_ID,
            event_codes: vec![num_configs as u32],
            payload: MetricEventPayload::Count(1),
        });
    }

    telemetry_sender.send(TelemetryEvent::LogMetricEvents {
        events: metric_events,
        ctx: "SavedNetworksManager::log_cobalt_metrics",
    });
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            config_management::{
                network_config::AddAndGetRecent, PastConnectionsByBssid, PROB_HIDDEN_DEFAULT,
                PROB_HIDDEN_IF_CONNECT_ACTIVE, PROB_HIDDEN_IF_CONNECT_PASSIVE,
                PROB_HIDDEN_IF_SEEN_PASSIVE,
            },
            util::testing::random_connection_data,
        },
        fidl_fuchsia_stash as fidl_stash,
        futures::{channel::mpsc, task::Poll, TryStreamExt},
        pin_utils::pin_mut,
        rand::{
            distributions::{Alphanumeric, DistString as _},
            thread_rng,
        },
        std::{convert::TryFrom, io::Write},
        tempfile::TempDir,
        test_case::test_case,
        wlan_common::assert_variant,
    };

    #[fuchsia::test]
    async fn store_and_lookup() {
        let stash_id = "store_and_lookup";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let saved_networks = create_saved_networks(stash_id, &path).await;
        let network_id_foo = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();

        assert!(saved_networks.lookup(&network_id_foo).await.is_empty());
        assert_eq!(0, saved_networks.saved_networks.lock().await.len());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        assert!(saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed")
            .is_none());
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(&network_id_foo).await
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
            saved_networks.lookup(&network_id_foo).await
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
        assert_eq!(vec![config_baz.clone()], saved_networks.lookup(&network_id_baz).await);
        assert_eq!(2, saved_networks.known_network_count().await);

        // Saved networks should persist when we create a saved networks manager with the same ID.
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);

        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            TelemetrySender::new(telemetry_sender),
        )
        .await
        .expect("failed to create saved networks store");
        assert_eq!(
            vec![network_config("foo", "12345678")],
            saved_networks.lookup(&network_id_foo).await
        );
        assert_eq!(vec![config_baz], saved_networks.lookup(&network_id_baz).await);
        assert_eq!(2, saved_networks.known_network_count().await);
    }

    #[fuchsia::test]
    async fn store_twice() {
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
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
        assert_eq!(expected_cfgs, saved_networks.lookup(&network_id).await);
        assert_eq!(1, saved_networks.known_network_count().await);
    }

    #[fuchsia::test]
    async fn store_many_same_ssid() {
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");

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
        assert_eq!(MAX_CONFIGS_PER_SSID, saved_networks.lookup(&network_id).await.len());
    }

    #[fuchsia::test]
    async fn store_and_remove() {
        let stash_id = "store_and_remove";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let saved_networks = create_saved_networks(stash_id, &path).await;

        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Password(b"qwertyuio".to_vec());
        assert!(saved_networks.lookup(&network_id).await.is_empty());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Store a network and verify it was stored.
        assert!(saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("storing 'foo' failed")
            .is_none());
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(&network_id).await
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
        // Check that the key in the saved networks manager's internal hashmap was removed.
        assert!(saved_networks.saved_networks.lock().await.get(&network_id).is_none());

        // If we try to remove the network again, we won't get an error and nothing happens
        assert_eq!(
            false,
            saved_networks
                .remove(network_id.clone(), credential)
                .await
                .expect("removing 'foo' failed")
        );

        // Check that removal persists.
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            TelemetrySender::new(telemetry_sender),
        )
        .await
        .expect("Failed to create SavedNetworksManager");
        assert_eq!(0, saved_networks.known_network_count().await);
        assert!(saved_networks.lookup(&network_id).await.is_empty());
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
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let ssid = types::Ssid::try_from("foo").unwrap();
        let network_id_wpa2 = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa2);
        let network_id_wpa3 = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa3);
        let credential_wpa2 = Credential::Password(b"password".to_vec());
        let credential_wpa3 = Credential::Password(b"wpa3-password".to_vec());

        // Check that lookup_compatible does not modify the SavedNetworksManager and returns an
        // empty vector if there is no matching config.
        let results = saved_networks
            .lookup_compatible(&ssid, types::SecurityTypeDetailed::Wpa2Wpa3Personal)
            .await;
        assert!(results.is_empty());
        assert_eq!(saved_networks.known_network_count().await, 0);

        // Store a couple of network configs that could both be use to connect to a WPA2/WPA3
        // network.
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
        let saved_networks = exec
            .run_singlethreaded(SavedNetworksManager::new_for_test())
            .expect("Failed to create SavedNetworksManager");

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

        let saved_networks = create_saved_networks(stash_id, &path).await;

        let network_id = NetworkIdentifier::try_from("bar", SecurityType::Wpa2).unwrap();
        let credential = Credential::Password(b"password".to_vec());
        let bssid = types::Bssid([4; 6]);

        // If connect and network hasn't been saved, we should not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fake_successful_connect_result(),
                None,
            )
            .await;
        assert!(saved_networks.lookup(&network_id).await.is_empty());
        assert_eq!(saved_networks.saved_networks.lock().await.len(), 0);
        assert_eq!(0, saved_networks.known_network_count().await);

        // Save the network and record a successful connection.
        assert!(saved_networks
            .store(network_id.clone(), credential.clone())
            .await
            .expect("Failed save network")
            .is_none());

        let config = network_config("bar", "password");
        assert_eq!(vec![config], saved_networks.lookup(&network_id).await);

        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fake_successful_connect_result(),
                None,
            )
            .await;

        // The network should be saved with the connection recorded. We should not have recorded
        // that the network was connected to passively or actively.
        assert_variant!(saved_networks.lookup(&network_id).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
            assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
        });

        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fake_successful_connect_result(),
                Some(ScanType::Active),
            )
            .await;
        // We should now see that we connected to the network after an active scan.
        assert_variant!(saved_networks.lookup(&network_id).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_CONNECT_ACTIVE);
        });

        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fake_successful_connect_result(),
                Some(ScanType::Passive),
            )
            .await;
        // The config should have a lower hidden probability after connecting after a passive scan.
        assert_variant!(saved_networks.lookup(&network_id).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_CONNECT_PASSIVE);
        });

        // Success connects should be saved as persistent data.
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            TelemetrySender::new(telemetry_sender),
        )
        .await
        .expect("Failed to create SavedNetworksManager");
        assert_variant!(saved_networks.lookup(&network_id).await.as_slice(), [config] => {
            assert_eq!(config.has_ever_connected, true);
        });
    }

    #[fuchsia::test]
    async fn test_record_connect_updates_one() {
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
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
                fake_successful_connect_result(),
                None,
            )
            .await;

        assert_variant!(saved_networks.lookup(&net_id).await.as_slice(), [config] => {
            assert!(config.has_ever_connected);
        });
        // If the specified network identifier is found, record_conenct_result should not mark
        // another config even if it could also have been used for the connect attempt.
        assert_variant!(saved_networks.lookup(&net_id_also_valid).await.as_slice(), [config] => {
            assert!(!config.has_ever_connected);
        });
    }

    #[fuchsia::test]
    async fn test_record_connect_failure() {
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::None).unwrap();
        let credential = Credential::None;
        let bssid = types::Bssid([1; 6]);
        let before_recording = fasync::Time::now();

        // Verify that recording connect result does not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResult {
                    code: fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
                    ..fake_successful_connect_result()
                },
                None,
            )
            .await;
        assert!(saved_networks.lookup(&network_id).await.is_empty());
        assert_eq!(0, saved_networks.saved_networks.lock().await.len());
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
                fidl_sme::ConnectResult {
                    code: fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
                    ..fake_successful_connect_result()
                },
                None,
            )
            .await;
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResult {
                    code: fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
                    is_credential_rejected: true,
                    ..fake_successful_connect_result()
                },
                None,
            )
            .await;

        // Check that the failures were recorded correctly.
        assert_eq!(1, saved_networks.known_network_count().await);
        let saved_config = saved_networks
            .lookup(&network_id)
            .await
            .pop()
            .expect("Failed to get saved network config");
        let connect_failures =
            saved_config.perf_stats.connect_failures.get_recent_for_network(before_recording);
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
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::None).unwrap();
        let credential = Credential::None;
        let bssid = types::Bssid([0; 6]);
        let before_recording = fasync::Time::now();

        // Verify that recording connect result does not save the network.
        saved_networks
            .record_connect_result(
                network_id.clone(),
                &credential,
                bssid,
                fidl_sme::ConnectResult {
                    code: fidl_ieee80211::StatusCode::Canceled,
                    ..fake_successful_connect_result()
                },
                None,
            )
            .await;
        assert!(saved_networks.lookup(&network_id).await.is_empty());
        assert_eq!(saved_networks.saved_networks.lock().await.len(), 0);
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
                fidl_sme::ConnectResult {
                    code: fidl_ieee80211::StatusCode::Canceled,
                    ..fake_successful_connect_result()
                },
                None,
            )
            .await;

        // Check that there are no failures recorded for this saved network.
        assert_eq!(1, saved_networks.known_network_count().await);
        let saved_config = saved_networks
            .lookup(&network_id)
            .await
            .pop()
            .expect("Failed to get saved network config");
        let connect_failures =
            saved_config.perf_stats.connect_failures.get_recent_for_network(before_recording);
        assert_eq!(0, connect_failures.len());
    }

    #[fuchsia::test]
    async fn test_record_disconnect() {
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Psk(vec![1; 32]);
        let data = random_connection_data();

        saved_networks.record_disconnect(&id, &credential, data).await;
        // Verify that nothing happens if the network was not already saved.
        assert_eq!(saved_networks.saved_networks.lock().await.len(), 0);
        assert_eq!(saved_networks.known_network_count().await, 0);

        // Save the network and record a disconnect.
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        saved_networks.record_disconnect(&id, &credential, data).await;

        // Check that a data was recorded about the connection that just ended.
        let recent_connections = saved_networks
            .lookup(&id)
            .await
            .pop()
            .expect("Failed to get saved network")
            .perf_stats
            .past_connections
            .get_recent_for_network(fasync::Time::INFINITE_PAST);
        assert_variant!(recent_connections.as_slice(), [connection_data] => {
            assert_eq!(connection_data, &data);
        })
    }

    #[fuchsia::test]
    async fn test_record_passive_scan() {
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
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

        assert_variant!(saved_networks.lookup(&saved_seen_id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_SEEN_PASSIVE);
        });
        assert_variant!(saved_networks.lookup(&saved_unseen_id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
        });
    }

    #[fuchsia::test]
    async fn test_record_undirected_scan_with_upgraded_security() {
        // Test that if we see a different compatible (higher) scan result for a saved network that
        // could be used to connect, recording the scan results will change the hidden probability.
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
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
        assert_variant!(saved_networks.lookup(&id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_IF_SEEN_PASSIVE);
        });
    }

    #[fuchsia::test]
    async fn test_record_undirected_scan_incompatible_credential() {
        // Test that if we see a different compatible (higher) scan result for a saved network that
        // could be used to connect, recording the scan results will change the hidden probability.
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
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
        assert_variant!(saved_networks.lookup(&id).await.as_slice(), [config] => {
            assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
        });
    }

    #[fuchsia::test]
    async fn test_record_directed_scan_for_upgraded_security() {
        // Test that if we see a different compatible (higher) scan result for a saved network that
        // could be used to connect in a directed scan, the hidden probability will not be lowered.
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let id = NetworkIdentifier::try_from("foobar", SecurityType::Wpa).unwrap();
        let credential = Credential::Password(b"credential".to_vec());

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
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

        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
        assert_eq!(config.hidden_probability, PROB_HIDDEN_DEFAULT);
    }

    #[fuchsia::test]
    async fn test_record_directed_scan_for_incompatible_credential() {
        // Test that if we see a network that is not compatible because of the saved credential
        // (but is otherwise compatible), the directed scan is not considered successful and the
        // hidden probability of the config is lowered.
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Psk(vec![11; 32]);

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
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
        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
        assert!(config.hidden_probability < PROB_HIDDEN_DEFAULT);
    }

    #[fuchsia::test]
    async fn test_record_directed_scan_no_ssid_match() {
        // Test that recording directed active scan results does not mistakenly match a config with
        // a network with a different SSID.

        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Psk(vec![11; 32]);
        let diff_ssid = types::Ssid::try_from("other-ssid").unwrap();

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
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

        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
        assert!(config.hidden_probability < PROB_HIDDEN_DEFAULT);
    }

    #[fuchsia::test]
    async fn test_record_directed_one_not_compatible_one_compatible() {
        // Test that if we see two networks with the same SSID but only one is compatible, the scan
        // is recorded as successful for the config. In other words it isn't mistakenly recorded as
        // a failure because of the config that isn't compatible.
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");
        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let credential = Credential::Password(b"foo-pass".to_vec());

        // Save the networks
        assert!(saved_networks
            .store(id.clone(), credential.clone())
            .await
            .expect("Failed to save network")
            .is_none());
        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
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
        let config = saved_networks.lookup(&id).await.pop().expect("failed to lookup config");
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
        let network_id = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let saved_networks = create_saved_networks(stash_id, &path).await;

        assert!(saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .await
            .expect("storing 'foo' failed")
            .is_none());
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(&network_id).await
        );
        assert_eq!(1, saved_networks.known_network_count().await);

        saved_networks.clear().await.expect("clearing store failed");
        assert_eq!(0, saved_networks.saved_networks.lock().await.len());
        assert_eq!(0, saved_networks.known_network_count().await);

        // Load store from stash to verify it is also gone from persistent storage
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            TelemetrySender::new(telemetry_sender),
        )
        .await
        .expect("failed to create saved networks manager");

        assert_eq!(0, saved_networks.known_network_count().await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn do_not_read_network_from_legacy_storage_and_delete_file() {
        // Possible contents of a file generated from KnownEssStore, with networks foo and bar with
        // passwords foobar and password respecitively. Network foo should not be read into new
        // saved network manager because the password is too short for a valid network password.
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        assert_eq!(file.write(contents).expect("Failed to write to file"), contents.len());
        file.flush().expect("failed to flush contents of file");

        let stash_id = "read_network_from_legacy_storage";
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            TelemetrySender::new(telemetry_sender),
        )
        .await
        .expect("failed to create saved networks store");

        // Network should not be read. The backing file should be deleted.
        assert_eq!(0, saved_networks.known_network_count().await);
        assert!(!path.exists());
    }

    #[fuchsia::test]
    fn test_store_waits_for_stash() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create executor");
        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());

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
    ) -> SavedNetworksManager {
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let saved_networks = SavedNetworksManager::new_with_stash_or_paths(
            stash_id,
            &path,
            TelemetrySender::new(telemetry_sender),
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
        Alphanumeric.sample_string(&mut thread_rng(), 20)
    }

    #[fuchsia::test]
    async fn record_metrics_when_called_on_class() {
        let stash_id = rand_string();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let (telemetry_sender, mut telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);

        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(&stash_id, &path, telemetry_sender)
                .await
                .unwrap();
        let network_id_foo = NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap();
        let network_id_baz = NetworkIdentifier::try_from("baz", SecurityType::Wpa2).unwrap();

        assert!(saved_networks.lookup(&network_id_foo).await.is_empty());
        assert_eq!(0, saved_networks.saved_networks.lock().await.len());
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

        // Verify three metrics are logged
        let metric_events = assert_variant!(telemetry_receiver.try_next(), Ok(Some(TelemetryEvent::LogMetricEvents { events, .. })) => events);
        assert_eq!(metric_events.len(), 3);

        // Two saved networks
        assert_eq!(
            metric_events[0],
            MetricEvent {
                metric_id: SAVED_NETWORKS_MIGRATED_METRIC_ID,
                event_codes: vec![
                    SavedNetworksMigratedMetricDimensionSavedNetworks::TwoToFour as u32
                ],
                payload: MetricEventPayload::Count(1),
            }
        );

        // One config for each network
        assert_eq!(metric_events[1], MetricEvent {
            metric_id: SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_MIGRATED_METRIC_ID,
            event_codes: vec![SavedConfigurationsForSavedNetworkMigratedMetricDimensionSavedConfigurations::One as u32],
            payload: MetricEventPayload::Count(1),
        });
        assert_eq!(metric_events[1], MetricEvent {
            metric_id: SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_MIGRATED_METRIC_ID,
            event_codes: vec![SavedConfigurationsForSavedNetworkMigratedMetricDimensionSavedConfigurations::One as u32],
            payload: MetricEventPayload::Count(1),
        });
    }

    #[fuchsia::test]
    async fn metrics_count_configs() {
        let (telemetry_sender, mut telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);

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

        log_cobalt_metrics(&networks, &telemetry_sender);

        let metric_events = assert_variant!(telemetry_receiver.try_next(), Ok(Some(TelemetryEvent::LogMetricEvents { events, .. })) => events);
        assert_eq!(metric_events.len(), 3);

        // Two saved networks
        assert_eq!(
            metric_events[0],
            MetricEvent {
                metric_id: SAVED_NETWORKS_MIGRATED_METRIC_ID,
                event_codes: vec![
                    SavedNetworksMigratedMetricDimensionSavedNetworks::TwoToFour as u32
                ],
                payload: MetricEventPayload::Count(1),
            }
        );

        // For the next two events, the order is not guaranteed
        // Zero configs for one network
        assert!(metric_events[1..]
            .iter()
            .find(|event| **event == MetricEvent {
                metric_id: SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_MIGRATED_METRIC_ID,
                event_codes: vec![SavedConfigurationsForSavedNetworkMigratedMetricDimensionSavedConfigurations::Zero
                     as u32],
                payload: MetricEventPayload::Count(1),
            })
            .is_some());
        // Two configs for the other network
        assert!(metric_events[1..]
            .iter()
            .find(|event| **event == MetricEvent {
                metric_id: SAVED_CONFIGURATIONS_FOR_SAVED_NETWORK_MIGRATED_METRIC_ID,
                event_codes: vec![SavedConfigurationsForSavedNetworkMigratedMetricDimensionSavedConfigurations::TwoToFour
                     as u32],
                payload: MetricEventPayload::Count(1),
            })
            .is_some());
    }

    #[fuchsia::test]
    async fn probabilistic_choosing_of_hidden_networks() {
        // Create three networks with 1, 0, 0.5 hidden probability
        let id_hidden = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("hidden").unwrap(),
            security_type: types::SecurityType::Wpa2,
        };
        let mut net_config_hidden = NetworkConfig::new(
            id_hidden.clone(),
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
            id_not_hidden.clone(),
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
            id_maybe_hidden.clone(),
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
        let saved_networks = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");

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
        let config_1 = saved_networks.lookup(&id_1).await.pop().expect("failed to lookup");
        assert_eq!(config_1.hidden_probability, PROB_HIDDEN_DEFAULT);
        let config_2 = saved_networks.lookup(&id_2).await.pop().expect("failed to lookup");
        assert_eq!(config_2.hidden_probability, PROB_HIDDEN_DEFAULT);
        let config_4 = saved_networks.lookup(&id_4).await.pop().expect("failed to lookup");
        assert_eq!(config_4.hidden_probability, PROB_HIDDEN_DEFAULT);

        let seen_ids = vec![];
        let not_seen_ids = vec![id_1.clone(), id_2.clone(), id_3.clone()];
        saved_networks.record_scan_result(ScanResultType::Directed(not_seen_ids), seen_ids).await;

        // Check that the configs' hidden probability has decreased
        let config_1 = saved_networks.lookup(&id_1).await.pop().expect("failed to lookup");
        assert!(config_1.hidden_probability < PROB_HIDDEN_DEFAULT);
        let config_2 = saved_networks.lookup(&id_2).await.pop().expect("failed to lookup");
        assert!(config_2.hidden_probability < PROB_HIDDEN_DEFAULT);

        // Check that for the network that was target but not seen in the active scan, its hidden
        // probability isn't lowered.
        let config_4 = saved_networks.lookup(&id_4).await.pop().expect("failed to lookup");
        assert_eq!(config_4.hidden_probability, PROB_HIDDEN_DEFAULT);

        // Check that a config was not saved for the identifier that was not saved before.
        assert!(saved_networks.lookup(&id_3).await.is_empty());
    }

    #[fuchsia::test]
    async fn test_get_past_connections() {
        let saved_networks_manager = SavedNetworksManager::new_for_test()
            .await
            .expect("Failed to create SavedNetworksManager");

        let id = NetworkIdentifier::try_from("foo", SecurityType::Wpa).unwrap();
        let credential = Credential::Password(b"some_password".to_vec());
        let mut config = NetworkConfig::new(id.clone(), credential.clone(), true)
            .expect("failed to create config");
        let mut past_connections = PastConnectionsByBssid::new();

        // Add two past connections with the same bssid
        let data_1 = random_connection_data();
        let bssid_1 = data_1.bssid;
        let mut data_2 = random_connection_data();
        data_2.bssid = bssid_1;
        past_connections.add(bssid_1, data_1);
        past_connections.add(bssid_1, data_2);

        // Add a past connection with different bssid
        let data_3 = random_connection_data();
        let bssid_2 = data_3.bssid;
        past_connections.add(bssid_2, data_3);
        config.perf_stats.past_connections = past_connections;

        // Create SavedNetworksManager with configs that have past connections
        assert!(saved_networks_manager
            .saved_networks
            .lock()
            .await
            .insert(id.clone(), vec![config])
            .is_none());

        // Check that get_past_connections gets the two PastConnectionLists for the BSSIDs.
        let mut expected_past_connections = PastConnectionList::new();
        expected_past_connections.add(data_1);
        expected_past_connections.add(data_2);
        let actual_past_connections =
            saved_networks_manager.get_past_connections(&id, &credential, &bssid_1).await;
        assert_eq!(actual_past_connections, expected_past_connections);

        let mut expected_past_connections = PastConnectionList::new();
        expected_past_connections.add(data_3);
        let actual_past_connections =
            saved_networks_manager.get_past_connections(&id, &credential, &bssid_2).await;
        assert_eq!(actual_past_connections, expected_past_connections);

        // Check that get_past_connections will not get the PastConnectionLists if the specified
        // Credential is different.
        let actual_past_connections = saved_networks_manager
            .get_past_connections(&id, &Credential::Password(b"other-password".to_vec()), &bssid_1)
            .await;
        assert_eq!(actual_past_connections, PastConnectionList::new());
    }

    fn fake_successful_connect_result() -> fidl_sme::ConnectResult {
        fidl_sme::ConnectResult {
            code: fidl_ieee80211::StatusCode::Success,
            is_credential_rejected: false,
            is_reconnect: false,
        }
    }
}

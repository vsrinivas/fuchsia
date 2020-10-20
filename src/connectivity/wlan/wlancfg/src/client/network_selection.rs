// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{
            scan::{self, ScanResultUpdate},
            types,
        },
        config_management::{self, Credential, SavedNetworksManager},
        mode_management::iface_manager_api::IfaceManagerApi,
    },
    async_trait::async_trait,
    fuchsia_cobalt::CobaltSender,
    futures::lock::Mutex,
    log::{error, info, trace},
    std::{
        cmp::Ordering,
        collections::HashMap,
        convert::TryInto,
        sync::Arc,
        time::{Duration, SystemTime},
    },
    wlan_metrics_registry::{
        SavedNetworkInScanResultMetricDimensionBssCount,
        ScanResultsReceivedMetricDimensionSavedNetworksCount,
        LAST_SCAN_AGE_WHEN_SCAN_REQUESTED_METRIC_ID, SAVED_NETWORK_IN_SCAN_RESULT_METRIC_ID,
        SCAN_RESULTS_RECEIVED_METRIC_ID,
    },
};

const RECENT_FAILURE_WINDOW: Duration = Duration::from_secs(60 * 5); // 5 minutes
const STALE_SCAN_AGE: Duration = Duration::from_secs(15); // TODO(61992) Optimize this value

pub struct NetworkSelector {
    saved_network_manager: Arc<SavedNetworksManager>,
    scan_result_cache: Arc<Mutex<ScanResultCache>>,
    cobalt_api: Arc<Mutex<CobaltSender>>,
}

struct ScanResultCache {
    updated_at: SystemTime,
    results: Vec<types::ScanResult>,
}

#[derive(Debug, PartialEq)]
struct InternalNetworkData {
    credential: Credential,
    has_ever_connected: bool,
    rssi: Option<i8>,
    compatible: bool,
    recent_failure_count: u8,
}

#[derive(Debug, PartialEq)]
pub struct NetworkMetadata {}

impl NetworkSelector {
    pub fn new(saved_network_manager: Arc<SavedNetworksManager>, cobalt_api: CobaltSender) -> Self {
        Self {
            saved_network_manager,
            scan_result_cache: Arc::new(Mutex::new(ScanResultCache {
                updated_at: SystemTime::UNIX_EPOCH,
                results: Vec::new(),
            })),
            cobalt_api: Arc::new(Mutex::new(cobalt_api)),
        }
    }

    pub fn generate_scan_result_updater(&self) -> NetworkSelectorScanUpdater {
        NetworkSelectorScanUpdater {
            scan_result_cache: Arc::clone(&self.scan_result_cache),
            saved_network_manager: Arc::clone(&self.saved_network_manager),
            cobalt_api: Arc::clone(&self.cobalt_api),
        }
    }

    async fn perform_scan(&self, iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>) {
        // Get the scan age. On error (which indicates the system clock moved backwards) fallback
        // to a number that will trigger a new scan, since we have no idea how old the scan is.
        let scan_result_guard = self.scan_result_cache.lock().await;
        let last_scan_result_time = scan_result_guard.updated_at;
        drop(scan_result_guard);
        let scan_age = last_scan_result_time.elapsed().unwrap_or(STALE_SCAN_AGE);

        // Log a metric for scan age, to help us optimize the STALE_SCAN_AGE
        if last_scan_result_time != SystemTime::UNIX_EPOCH {
            let mut cobalt_api_guard = self.cobalt_api.lock().await;
            let cobalt_api = &mut *cobalt_api_guard;
            cobalt_api.log_elapsed_time(
                LAST_SCAN_AGE_WHEN_SCAN_REQUESTED_METRIC_ID,
                Vec::<u32>::new(),
                scan_age.as_micros().try_into().unwrap_or(i64::MAX),
            );
            drop(cobalt_api_guard);
        }

        // Determine if a new scan is warranted
        if scan_age >= STALE_SCAN_AGE {
            info!("Scan results are {:?} old, triggering a scan", scan_age);

            scan::perform_scan(
                iface_manager,
                None,
                self.generate_scan_result_updater(),
                scan::LocationSensorUpdater {},
                |_| None, // TODO(35920): include potentially hidden networks
            )
            .await;
        } else {
            info!("Using cached scan results from {:?} ago", scan_age);
        }
    }

    /// Augment the networks hash map with data from scan results
    async fn augment_networks_with_scan_data(
        &self,
        mut networks: HashMap<types::NetworkIdentifier, InternalNetworkData>,
    ) -> HashMap<types::NetworkIdentifier, InternalNetworkData> {
        let scan_result_guard = self.scan_result_cache.lock().await;
        for scan_result in &*scan_result_guard.results {
            if let Some(hashmap_entry) = networks.get_mut(&scan_result.id) {
                // Extract the max RSSI from all the BSS in scan_result.entries
                if let Some(max_rssi) =
                    scan_result.entries.iter().map(|bss| bss.rssi).max_by(|a, b| a.cmp(b))
                {
                    let compatibility =
                        scan_result.compatibility == types::Compatibility::Supported;
                    trace!(
                        "Augmenting network with RSSI {} and compatibility {}",
                        max_rssi,
                        compatibility
                    );
                    hashmap_entry.rssi = Some(max_rssi);
                    hashmap_entry.compatible = compatibility;
                }
            }
        }
        networks
    }

    /// Select the best available network, based on the current saved networks and the most
    /// recent scan results provided to this module.
    /// Only networks that are both saved and visible in the most recent scan results are eligible
    /// for consideration. Among those, the "best" network based on compatibility and quality (e.g.
    /// RSSI, recent failures) is selected.
    pub(crate) async fn get_best_network(
        &self,
        iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
        ignore_list: &Vec<types::NetworkIdentifier>,
    ) -> Option<(types::NetworkIdentifier, Credential, NetworkMetadata)> {
        self.perform_scan(iface_manager).await;
        let saved_networks = load_saved_networks(Arc::clone(&self.saved_network_manager)).await;
        let networks = self.augment_networks_with_scan_data(saved_networks).await;
        find_best_network(&networks, ignore_list)
    }
}

/// Insert all saved networks into a hashmap with this module's internal data representation
async fn load_saved_networks(
    saved_network_manager: Arc<SavedNetworksManager>,
) -> HashMap<types::NetworkIdentifier, InternalNetworkData> {
    let mut networks: HashMap<types::NetworkIdentifier, InternalNetworkData> = HashMap::new();
    for saved_network in saved_network_manager.get_networks().await.into_iter() {
        let recent_failure_count = saved_network
            .perf_stats
            .failure_list
            .get_recent(SystemTime::now() - RECENT_FAILURE_WINDOW)
            .len()
            .try_into()
            .unwrap_or_else(|e| {
                error!("Failed to convert failure count: {:?}", e);
                u8::MAX
            });

        trace!(
            "Adding saved network to hashmap{}",
            if recent_failure_count > 0 { " with some failures" } else { "" }
        );
        // We allow networks saved as WPA to be also used as WPA2 or WPA2 to be used for WPA3
        if let Some(security_type) = upgrade_security(&saved_network.security_type) {
            networks.insert(
                types::NetworkIdentifier { ssid: saved_network.ssid.clone(), type_: security_type },
                InternalNetworkData {
                    credential: saved_network.credential.clone(),
                    has_ever_connected: saved_network.has_ever_connected,
                    recent_failure_count: recent_failure_count,
                    rssi: None,
                    compatible: false,
                },
            );
        };
        networks.insert(
            types::NetworkIdentifier {
                ssid: saved_network.ssid,
                type_: saved_network.security_type.into(),
            },
            InternalNetworkData {
                credential: saved_network.credential,
                has_ever_connected: saved_network.has_ever_connected,
                recent_failure_count: recent_failure_count,
                rssi: None,
                compatible: false,
            },
        );
    }
    networks
}

fn upgrade_security(security: &config_management::SecurityType) -> Option<types::SecurityType> {
    match security {
        config_management::SecurityType::Wpa => Some(types::SecurityType::Wpa2),
        config_management::SecurityType::Wpa2 => Some(types::SecurityType::Wpa3),
        _ => None,
    }
}

pub struct NetworkSelectorScanUpdater {
    scan_result_cache: Arc<Mutex<ScanResultCache>>,
    saved_network_manager: Arc<SavedNetworksManager>,
    cobalt_api: Arc<Mutex<CobaltSender>>,
}
#[async_trait]
impl ScanResultUpdate for NetworkSelectorScanUpdater {
    async fn update_scan_results(&mut self, scan_results: &Vec<types::ScanResult>) {
        // Update internal scan result cache
        let scan_results_clone = scan_results.clone();
        let mut scan_result_guard = self.scan_result_cache.lock().await;
        scan_result_guard.results = scan_results_clone;
        scan_result_guard.updated_at = SystemTime::now();
        drop(scan_result_guard);

        // Record metrics for this scan
        let saved_networks = load_saved_networks(Arc::clone(&self.saved_network_manager)).await;
        let mut cobalt_api_guard = self.cobalt_api.lock().await;
        let cobalt_api = &mut *cobalt_api_guard;
        record_metrics_on_scan(scan_results, saved_networks, cobalt_api);
        drop(cobalt_api_guard);
    }
}

/// Find the best network in the given hashmap
fn find_best_network(
    networks: &HashMap<types::NetworkIdentifier, InternalNetworkData>,
    ignore_list: &Vec<types::NetworkIdentifier>,
) -> Option<(types::NetworkIdentifier, Credential, NetworkMetadata)> {
    networks
        .iter()
        .filter(|(id, data)| {
            // Filter out networks that are incompatible
            if !data.compatible {
                trace!("Network is incompatible, filtering");
                return false;
            };
            // Filter out networks not present in scan results
            if data.rssi.is_none() {
                trace!("RSSI not present, filtering");
                return false;
            };
            // Filter out networks we've been told to ignore
            if ignore_list.contains(id) {
                trace!("Network is ignored, filtering");
                return false;
            }
            true
        })
        .max_by(|(_, data_a), (_, data_b)| {
            // If only one network has failures, prefer the other one
            if data_a.recent_failure_count > 0 && data_b.recent_failure_count == 0 {
                return Ordering::Less;
            }
            if data_a.recent_failure_count == 0 && data_b.recent_failure_count > 0 {
                return Ordering::Greater;
            }

            // Both networks have failures, sort by RSSI
            let rssi_a = data_a.rssi.unwrap();
            let rssi_b = data_b.rssi.unwrap();
            rssi_a.partial_cmp(&rssi_b).unwrap()
        })
        .map(|(id, data)| (id.clone(), data.credential.clone(), NetworkMetadata {}))
}

fn record_metrics_on_scan(
    scan_results: &Vec<types::ScanResult>,
    saved_networks: HashMap<types::NetworkIdentifier, InternalNetworkData>,
    cobalt_api: &mut CobaltSender,
) {
    let mut num_saved_networks_observed = 0;

    for scan_result in scan_results {
        if let Some(_) = saved_networks.get(&scan_result.id) {
            // This saved network was present in scan results;
            num_saved_networks_observed += 1;

            // Record how many BSSs are visible in the scan results for this saved network.
            let num_bss = match scan_result.entries.len() {
                0 => unreachable!(), // The ::Zero enum exists, but we shouldn't get a scan result with no BSS
                1 => SavedNetworkInScanResultMetricDimensionBssCount::One,
                2..=4 => SavedNetworkInScanResultMetricDimensionBssCount::TwoToFour,
                5..=10 => SavedNetworkInScanResultMetricDimensionBssCount::FiveToTen,
                11..=20 => SavedNetworkInScanResultMetricDimensionBssCount::ElevenToTwenty,
                21..=usize::MAX => SavedNetworkInScanResultMetricDimensionBssCount::TwentyOneOrMore,
                _ => unreachable!(),
            };
            cobalt_api.log_event(SAVED_NETWORK_IN_SCAN_RESULT_METRIC_ID, num_bss);
        }
    }

    let saved_network_count_metric = match num_saved_networks_observed {
        0 => ScanResultsReceivedMetricDimensionSavedNetworksCount::Zero,
        1 => ScanResultsReceivedMetricDimensionSavedNetworksCount::One,
        2..=4 => ScanResultsReceivedMetricDimensionSavedNetworksCount::TwoToFour,
        5..=20 => ScanResultsReceivedMetricDimensionSavedNetworksCount::FiveToTwenty,
        21..=40 => ScanResultsReceivedMetricDimensionSavedNetworksCount::TwentyOneToForty,
        41..=usize::MAX => ScanResultsReceivedMetricDimensionSavedNetworksCount::FortyOneOrMore,
        _ => unreachable!(),
    };
    cobalt_api.log_event(SCAN_RESULTS_RECEIVED_METRIC_ID, saved_network_count_metric);
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            access_point::state_machine as ap_fsm,
            client::state_machine as client_fsm,
            util::{cobalt::create_mock_cobalt_sender_and_receiver, logger::set_logger_for_test},
        },
        anyhow::Error,
        cobalt_client::traits::AsEventCode,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_cobalt::CobaltEvent,
        fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync,
        fuchsia_cobalt::cobalt_event_builder::CobaltEventExt,
        futures::{
            channel::{mpsc, oneshot},
            prelude::*,
            task::Poll,
        },
        pin_utils::pin_mut,
        rand::Rng,
        std::sync::Arc,
        wlan_common::assert_variant,
    };

    struct TestValues {
        network_selector: Arc<NetworkSelector>,
        saved_network_manager: Arc<SavedNetworksManager>,
        cobalt_events: mpsc::Receiver<CobaltEvent>,
        iface_manager: Arc<Mutex<FakeIfaceManager>>,
        sme_stream: fidl_sme::ClientSmeRequestStream,
    }

    async fn test_setup() -> TestValues {
        set_logger_for_test();

        // setup modules
        let (cobalt_api, cobalt_events) = create_mock_cobalt_sender_and_receiver();
        let saved_network_manager = Arc::new(SavedNetworksManager::new_for_test().await.unwrap());
        let network_selector =
            Arc::new(NetworkSelector::new(Arc::clone(&saved_network_manager), cobalt_api));
        let (client_sme, remote) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("error creating proxy");
        let iface_manager = Arc::new(Mutex::new(FakeIfaceManager::new(client_sme)));

        TestValues {
            network_selector,
            saved_network_manager,
            cobalt_events,
            iface_manager,
            sme_stream: remote.into_stream().expect("failed to create stream"),
        }
    }

    struct FakeIfaceManager {
        pub sme_proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy,
    }

    impl FakeIfaceManager {
        pub fn new(proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy) -> Self {
            FakeIfaceManager { sme_proxy: proxy }
        }
    }

    #[async_trait]
    impl IfaceManagerApi for FakeIfaceManager {
        async fn disconnect(
            &mut self,
            _network_id: fidl_fuchsia_wlan_policy::NetworkIdentifier,
        ) -> Result<(), Error> {
            unimplemented!()
        }

        async fn connect(
            &mut self,
            _connect_req: client_fsm::ConnectRequest,
        ) -> Result<oneshot::Receiver<()>, Error> {
            unimplemented!()
        }

        async fn record_idle_client(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!()
        }

        async fn has_idle_client(&mut self) -> Result<bool, Error> {
            unimplemented!()
        }

        async fn handle_added_iface(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!()
        }

        async fn handle_removed_iface(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!()
        }

        async fn scan(
            &mut self,
            mut scan_request: fidl_sme::ScanRequest,
        ) -> Result<fidl_fuchsia_wlan_sme::ScanTransactionProxy, Error> {
            let (local, remote) = fidl::endpoints::create_proxy()?;
            let _ = self.sme_proxy.scan(&mut scan_request, remote);
            Ok(local)
        }

        async fn stop_client_connections(&mut self) -> Result<(), Error> {
            unimplemented!()
        }

        async fn start_client_connections(&mut self) -> Result<(), Error> {
            unimplemented!()
        }

        async fn start_ap(
            &mut self,
            _config: ap_fsm::ApConfig,
        ) -> Result<oneshot::Receiver<fidl_fuchsia_wlan_sme::StartApResultCode>, Error> {
            unimplemented!()
        }

        async fn stop_ap(&mut self, _ssid: Vec<u8>, _password: Vec<u8>) -> Result<(), Error> {
            unimplemented!()
        }

        async fn stop_all_aps(&mut self) -> Result<(), Error> {
            unimplemented!()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn saved_networks_are_loaded() {
        let test_values = test_setup().await;

        // check there are 0 saved networks to start with
        let networks = load_saved_networks(Arc::clone(&test_values.saved_network_manager)).await;
        assert_eq!(networks.len(), 0);

        // create some identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let ssid_2 = "bar".as_bytes().to_vec();
        let test_id_2 =
            types::NetworkIdentifier { ssid: ssid_2.clone(), type_: types::SecurityType::Wpa };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        // insert some new saved networks
        test_values
            .saved_network_manager
            .store(test_id_1.clone().into(), credential_1.clone())
            .await
            .unwrap();

        test_values
            .saved_network_manager
            .store(test_id_2.clone().into(), credential_2.clone())
            .await
            .unwrap();

        // mark the first one as having connected
        test_values
            .saved_network_manager
            .record_connect_result(
                test_id_1.clone().into(),
                &credential_1.clone(),
                fidl_sme::ConnectResultCode::Success,
                true,
            )
            .await;

        // mark the second one as having a failure
        test_values
            .saved_network_manager
            .record_connect_result(
                test_id_2.clone().into(),
                &credential_2.clone(),
                fidl_sme::ConnectResultCode::CredentialRejected,
                true,
            )
            .await;

        // check these networks were loaded
        let mut expected_hashmap = HashMap::new();
        expected_hashmap.insert(
            test_id_1,
            InternalNetworkData {
                credential: credential_1,
                has_ever_connected: true,
                rssi: None,
                compatible: false,
                recent_failure_count: 0,
            },
        );
        expected_hashmap.insert(
            test_id_2,
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: false,
                rssi: None,
                compatible: false,
                recent_failure_count: 1,
            },
        );
        // Networks saved as WPA can be used to auto connect to WPA2 networks
        expected_hashmap.insert(
            types::NetworkIdentifier { ssid: ssid_2, type_: types::SecurityType::Wpa2 },
            InternalNetworkData {
                credential: credential_2,
                has_ever_connected: false,
                rssi: None,
                compatible: false,
                recent_failure_count: 1,
            },
        );
        let networks = load_saved_networks(Arc::clone(&test_values.saved_network_manager)).await;
        assert_eq!(networks, expected_hashmap);
    }

    #[fasync::run_singlethreaded(test)]
    async fn scan_results_are_stored() {
        let test_values = test_setup().await;
        let network_selector = test_values.network_selector;

        // check there are 0 scan results to start with
        let guard = network_selector.scan_result_cache.lock().await;
        assert_eq!(guard.results.len(), 0);
        drop(guard);

        // create some identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };

        // provide some new scan results
        let mock_scan_results = vec![
            types::ScanResult {
                id: test_id_1.clone(),
                entries: vec![
                    types::Bss {
                        bssid: [0, 1, 2, 3, 4, 5],
                        rssi: -14,
                        frequency: 2400,
                        timestamp_nanos: 0,
                        snr_db: 1,
                        observed_in_passive_scan: true,
                        compatible: true,
                    },
                    types::Bss {
                        bssid: [6, 7, 8, 9, 10, 11],
                        rssi: -10,
                        frequency: 2410,
                        timestamp_nanos: 1,
                        snr_db: 2,
                        observed_in_passive_scan: true,
                        compatible: true,
                    },
                    types::Bss {
                        bssid: [0, 1, 2, 3, 4, 5],
                        rssi: -20,
                        frequency: 2400,
                        timestamp_nanos: 0,
                        snr_db: 3,
                        observed_in_passive_scan: false,
                        compatible: false,
                    },
                ],
                compatibility: types::Compatibility::Supported,
            },
            types::ScanResult {
                id: test_id_2.clone(),
                entries: vec![types::Bss {
                    bssid: [20, 30, 40, 50, 60, 70],
                    rssi: -15,
                    frequency: 2400,
                    timestamp_nanos: 0,
                    snr_db: 4,
                    observed_in_passive_scan: false,
                    compatible: false,
                }],
                compatibility: types::Compatibility::DisallowedNotSupported,
            },
        ];
        let mut updater = network_selector.generate_scan_result_updater();
        updater.update_scan_results(&mock_scan_results).await;

        // check that the scan results are stored
        let guard = network_selector.scan_result_cache.lock().await;
        assert_eq!(guard.results, mock_scan_results);
    }

    #[fasync::run_singlethreaded(test)]
    async fn scan_results_used_to_augment_hashmap_and_metrics() {
        let mut test_values = test_setup().await;
        let network_selector = test_values.network_selector;

        // create some identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        // create the saved networks hashmap
        let mut saved_networks = HashMap::new();
        saved_networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: None,
                compatible: false,
                recent_failure_count: 0,
            },
        );
        saved_networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: false,
                rssi: None,
                compatible: false,
                recent_failure_count: 0,
            },
        );

        // store some scan results
        let mock_scan_results = vec![
            types::ScanResult {
                id: test_id_1.clone(),
                entries: vec![
                    types::Bss {
                        bssid: [0, 1, 2, 3, 4, 5],
                        rssi: -14,
                        frequency: 2400,
                        timestamp_nanos: 0,
                        snr_db: 1,
                        observed_in_passive_scan: true,
                        compatible: true,
                    },
                    types::Bss {
                        bssid: [6, 7, 8, 9, 10, 11],
                        rssi: -10,
                        frequency: 2410,
                        timestamp_nanos: 1,
                        snr_db: 2,
                        observed_in_passive_scan: true,
                        compatible: false,
                    },
                    types::Bss {
                        bssid: [0, 1, 2, 3, 4, 5],
                        rssi: -20,
                        frequency: 2400,
                        timestamp_nanos: 0,
                        snr_db: 3,
                        observed_in_passive_scan: false,
                        compatible: true,
                    },
                ],
                compatibility: types::Compatibility::Supported,
            },
            types::ScanResult {
                id: test_id_2.clone(),
                entries: vec![types::Bss {
                    bssid: [20, 30, 40, 50, 60, 70],
                    rssi: -15,
                    frequency: 2400,
                    timestamp_nanos: 0,
                    snr_db: 4,
                    observed_in_passive_scan: true,
                    compatible: false,
                }],
                compatibility: types::Compatibility::DisallowedNotSupported,
            },
        ];
        let mut updater = network_selector.generate_scan_result_updater();
        updater.update_scan_results(&mock_scan_results).await;

        // build our expected result
        let mut expected_result = HashMap::new();
        expected_result.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: Some(-10),  // strongest RSSI
                compatible: true, // compatible
                recent_failure_count: 0,
            },
        );
        expected_result.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: false,
                rssi: Some(-15),
                compatible: false, // DisallowedNotSupported
                recent_failure_count: 0,
            },
        );

        // validate the function works
        let result = network_selector.augment_networks_with_scan_data(saved_networks).await;
        assert_eq!(result, expected_result);

        // check there are some metric events
        // note: the actual metrics are checked in unit tests for the metric recording function
        assert!(test_values.cobalt_events.try_next().unwrap().is_some());
    }

    #[test]
    fn find_best_network_sorts_by_rssi() {
        // build network hashmap
        let mut networks = HashMap::new();
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());
        networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: Some(-10),
                compatible: true,
                recent_failure_count: 0,
            },
        );
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(-15),
                compatible: true,
                recent_failure_count: 0,
            },
        );

        // stronger network returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_1.clone(), credential_1.clone(), NetworkMetadata {})
        );

        // make the other network stronger
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(-5),
                compatible: true,
                recent_failure_count: 0,
            },
        );

        // stronger network returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_2.clone(), credential_2.clone(), NetworkMetadata {})
        );
    }

    #[test]
    fn find_best_network_sorts_by_failure_count() {
        // build network hashmap
        let mut networks = HashMap::new();
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());
        networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: Some(-10),
                compatible: true,
                recent_failure_count: 0,
            },
        );
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(-15),
                compatible: true,
                recent_failure_count: 0,
            },
        );

        // stronger network returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_1.clone(), credential_1.clone(), NetworkMetadata {})
        );

        // mark the stronger network as having a failure
        networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: Some(-10),
                compatible: true,
                recent_failure_count: 2,
            },
        );

        // weaker network (with no failures) returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_2.clone(), credential_2.clone(), NetworkMetadata {})
        );

        // give them both the same number of failures
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(-15),
                compatible: true,
                recent_failure_count: 1,
            },
        );

        // stronger network returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_1.clone(), credential_1.clone(), NetworkMetadata {})
        );
    }

    #[test]
    fn find_best_network_incompatible() {
        // build network hashmap
        let mut networks = HashMap::new();
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());
        networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: Some(1),
                compatible: true,
                recent_failure_count: 0,
            },
        );
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(2),
                compatible: true,
                recent_failure_count: 0,
            },
        );

        // stronger network returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_2.clone(), credential_2.clone(), NetworkMetadata {})
        );

        // mark it as incompatible
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(2),
                compatible: false,
                recent_failure_count: 0,
            },
        );

        // other network returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_1.clone(), credential_1.clone(), NetworkMetadata {})
        );
    }

    #[test]
    fn find_best_network_no_rssi() {
        // build network hashmap
        let mut networks = HashMap::new();
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());
        networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: None, // No RSSI
                compatible: true,
                recent_failure_count: 0,
            },
        );
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: None, // No RSSI
                compatible: true,
                recent_failure_count: 0,
            },
        );

        // no network returned
        assert!(find_best_network(&networks, &vec![]).is_none());

        // add RSSI
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(20),
                compatible: true,
                recent_failure_count: 0,
            },
        );
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_2.clone(), credential_2.clone(), NetworkMetadata {})
        );
    }

    #[test]
    fn find_best_network_ignore_list() {
        // build network hashmap
        let mut networks = HashMap::new();
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());
        networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: credential_1.clone(),
                has_ever_connected: true,
                rssi: Some(1),
                compatible: true,
                recent_failure_count: 0,
            },
        );
        networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: credential_2.clone(),
                has_ever_connected: true,
                rssi: Some(2),
                compatible: true,
                recent_failure_count: 0,
            },
        );

        // stronger network returned
        assert_eq!(
            find_best_network(&networks, &vec![]).unwrap(),
            (test_id_2.clone(), credential_2.clone(), NetworkMetadata {})
        );

        // ignore the stronger network, other network returned
        assert_eq!(
            find_best_network(&networks, &vec![test_id_2.clone()]).unwrap(),
            (test_id_1.clone(), credential_1.clone(), NetworkMetadata {})
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn perform_scan_cache_is_fresh() {
        let mut test_values = test_setup().await;
        let network_selector = test_values.network_selector;

        // Set the scan result cache to be fresher than STALE_SCAN_AGE
        let mut scan_result_guard = network_selector.scan_result_cache.lock().await;
        let last_scan_age = Duration::from_secs(1);
        assert!(last_scan_age < STALE_SCAN_AGE);
        scan_result_guard.updated_at = SystemTime::now() - last_scan_age;
        drop(scan_result_guard);

        network_selector.perform_scan(test_values.iface_manager).await;

        // Metric logged for scan age
        let metric = test_values.cobalt_events.try_next().unwrap().unwrap();
        let expected_metric =
            CobaltEvent::builder(LAST_SCAN_AGE_WHEN_SCAN_REQUESTED_METRIC_ID).as_elapsed_time(0);
        // We need to individually check each field, since the elapsed time is non-deterministic
        assert_eq!(metric.metric_id, expected_metric.metric_id);
        assert_eq!(metric.event_codes, expected_metric.event_codes);
        assert_eq!(metric.component, expected_metric.component);
        assert_variant!(metric.payload, fidl_fuchsia_cobalt::EventPayload::ElapsedMicros(elapsed_micros) => {
            let elapsed_time = Duration::from_micros(elapsed_micros.try_into().unwrap());
            assert!(elapsed_time < STALE_SCAN_AGE);
        });

        // No scan performed
        assert!(test_values.sme_stream.next().await.is_none());
    }

    #[test]
    fn perform_scan_cache_is_stale() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());
        let network_selector = test_values.network_selector;
        let test_start_time = SystemTime::now();

        // Set the scan result cache to be older than STALE_SCAN_AGE
        let mut scan_result_guard =
            exec.run_singlethreaded(network_selector.scan_result_cache.lock());
        scan_result_guard.updated_at =
            SystemTime::now() - (STALE_SCAN_AGE + Duration::from_secs(1));
        drop(scan_result_guard);

        // Kick off scan
        let scan_fut = network_selector.perform_scan(test_values.iface_manager);
        pin_mut!(scan_fut);
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Metric logged for scan age
        let metric = test_values.cobalt_events.try_next().unwrap().unwrap();
        let expected_metric =
            CobaltEvent::builder(LAST_SCAN_AGE_WHEN_SCAN_REQUESTED_METRIC_ID).as_elapsed_time(0);
        assert_eq!(metric.metric_id, expected_metric.metric_id);
        assert_eq!(metric.event_codes, expected_metric.event_codes);
        assert_eq!(metric.component, expected_metric.component);
        assert_variant!(metric.payload, fidl_fuchsia_cobalt::EventPayload::ElapsedMicros(elapsed_micros) => {
            let elapsed_time = Duration::from_micros(elapsed_micros.try_into().unwrap());
            assert!(elapsed_time > STALE_SCAN_AGE);
        });

        // Check that a scan request was sent to the sme and send back results
        let expected_scan_request = fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {});
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, req, control_handle: _
            }))) => {
                // Validate the request
                assert_eq!(req, expected_scan_request);
                // Send all the APs
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_result(&mut vec![].iter_mut())
                    .expect("failed to send scan data");

                // Send the end of data
                ctrl.send_on_finished()
                    .expect("failed to send scan data");
            }
        );

        // Process scan
        exec.run_singlethreaded(&mut scan_fut);

        // Check scan results were updated
        let scan_result_guard = exec.run_singlethreaded(network_selector.scan_result_cache.lock());
        assert!(scan_result_guard.updated_at > test_start_time);
        assert!(scan_result_guard.updated_at < SystemTime::now());
        drop(scan_result_guard);
    }

    #[test]
    fn get_best_network_end_to_end() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());
        let network_selector = test_values.network_selector;

        // create some identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        // insert some new saved networks
        exec.run_singlethreaded(
            test_values.saved_network_manager.store(test_id_1.clone().into(), credential_1.clone()),
        )
        .unwrap();
        exec.run_singlethreaded(
            test_values.saved_network_manager.store(test_id_2.clone().into(), credential_2.clone()),
        )
        .unwrap();

        // mark them as having connected
        exec.run_singlethreaded(test_values.saved_network_manager.record_connect_result(
            test_id_1.clone().into(),
            &credential_1.clone(),
            fidl_sme::ConnectResultCode::Success,
            true,
        ));
        exec.run_singlethreaded(test_values.saved_network_manager.record_connect_result(
            test_id_2.clone().into(),
            &credential_2.clone(),
            fidl_sme::ConnectResultCode::Success,
            true,
        ));

        // Kick off network selection
        let ignore_list = vec![];
        let network_selection_fut =
            network_selector.get_best_network(test_values.iface_manager.clone(), &ignore_list);
        pin_mut!(network_selection_fut);
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back results
        let expected_scan_request = fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {});
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, req, control_handle: _
            }))) => {
                // Validate the request
                assert_eq!(req, expected_scan_request);

                let mut mock_scan_results = vec![
                    fidl_sme::BssInfo {
                        bssid: [0, 0, 0, 0, 0, 0],
                        ssid: test_id_1.ssid.clone(),
                        rx_dbm: 10,
                        snr_db: 10,
                        channel: 0,
                        protection: fidl_sme::Protection::Wpa3Enterprise,
                        compatible: true,
                    },
                    fidl_sme::BssInfo {
                        bssid: [0, 0, 0, 0, 0, 0],
                        ssid: test_id_2.ssid.clone(),
                        rx_dbm: 0,
                        snr_db: 0,
                        channel: 0,
                        protection: fidl_sme::Protection::Wpa1,
                        compatible: true,
                    },
                ];
                // Send all the APs
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_result(&mut mock_scan_results.iter_mut())
                    .expect("failed to send scan data");

                // Send the end of data
                ctrl.send_on_finished()
                    .expect("failed to send scan data");
            }
        );

        // Check that we pick a network
        let results = exec.run_singlethreaded(&mut network_selection_fut);
        assert_eq!(results, Some((test_id_1.clone(), credential_1.clone(), NetworkMetadata {})));

        // Ignore that network, check that we pick the other one
        let results = exec.run_singlethreaded(
            network_selector.get_best_network(test_values.iface_manager, &vec![test_id_1.clone()]),
        );
        assert_eq!(results, Some((test_id_2.clone(), credential_2.clone(), NetworkMetadata {})));
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_best_network_wpa_wpa2() {
        // Check that if we see a WPA2 network and have WPA and WPA3 credentials saved for it, we
        // could choose the WPA credential but not the WPA3 credential. In other words we can
        // upgrade saved networks to higher security but not downgrade.
        let test_values = test_setup().await;
        let network_selector = test_values.network_selector;

        // Save networks with WPA and WPA3 security, same SSIDs, and different passwords.
        let ssid = "foo".as_bytes().to_vec();
        let wpa_network_id =
            types::NetworkIdentifier { ssid: ssid.clone(), type_: types::SecurityType::Wpa };
        let credential = Credential::Password("foo_password".as_bytes().to_vec());
        test_values
            .saved_network_manager
            .store(wpa_network_id.clone().into(), credential.clone())
            .await
            .expect("Failed to save network");
        let wpa3_network_id =
            types::NetworkIdentifier { ssid: ssid.clone(), type_: types::SecurityType::Wpa3 };
        let wpa3_credential = Credential::Password("wpa3_only_password".as_bytes().to_vec());
        test_values
            .saved_network_manager
            .store(wpa3_network_id.into(), wpa3_credential)
            .await
            .expect("Failed to save network");

        // Feed scans with WPA2 and WPA3 results to network selector, as we should get if a
        // WPA2/WPA3 network was seen.
        let id = types::NetworkIdentifier { ssid: ssid, type_: types::SecurityType::Wpa2 };
        let mixed_scan_results = vec![types::ScanResult {
            id: id.clone(),
            entries: vec![types::Bss {
                bssid: [10, 9, 8, 7, 6, 5],
                rssi: -70,
                frequency: 2400,
                timestamp_nanos: 0,
                snr_db: 10,
                observed_in_passive_scan: true,
                compatible: true,
            }],
            compatibility: types::Compatibility::Supported,
        }];
        let mut updater = network_selector.generate_scan_result_updater();
        updater.update_scan_results(&mixed_scan_results).await;

        // Check that we choose the config saved as WPA2
        assert_eq!(
            network_selector.get_best_network(test_values.iface_manager.clone(), &vec![]).await,
            Some((id.clone(), credential, NetworkMetadata {}))
        );
        assert_eq!(
            network_selector.get_best_network(test_values.iface_manager, &vec![id]).await,
            None
        );
    }

    fn generate_random_scan_result() -> types::ScanResult {
        let mut rng = rand::thread_rng();
        let bss = (0..6).map(|_| rng.gen::<u8>()).collect::<Vec<u8>>();
        types::ScanResult {
            id: types::NetworkIdentifier {
                ssid: format!("scan result rand {}", rng.gen::<i32>()).as_bytes().to_vec(),
                type_: types::SecurityType::Wpa,
            },
            entries: vec![types::Bss {
                bssid: bss.as_slice().try_into().unwrap(),
                rssi: rng.gen_range(-100, 0),
                frequency: rng.gen_range(2000, 6000),
                timestamp_nanos: 0,
                snr_db: rng.gen_range(0, 50),
                observed_in_passive_scan: rng.gen_bool(1.0 / 2.0),
                compatible: rng.gen_bool(1.0 / 2.0),
            }],
            compatibility: types::Compatibility::Supported,
        }
    }

    fn generate_random_saved_network() -> (types::NetworkIdentifier, InternalNetworkData) {
        let mut rng = rand::thread_rng();
        (
            types::NetworkIdentifier {
                ssid: format!("saved network rand {}", rng.gen::<i32>()).as_bytes().to_vec(),
                type_: types::SecurityType::Wpa,
            },
            InternalNetworkData {
                credential: Credential::Password(
                    format!("password {}", rng.gen::<i32>()).as_bytes().to_vec(),
                ),
                has_ever_connected: false,
                rssi: None,
                compatible: false,
                recent_failure_count: 0,
            },
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn recorded_metrics_on_scan() {
        let (mut cobalt_api, mut cobalt_events) = create_mock_cobalt_sender_and_receiver();

        // create some identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa3,
        };
        let test_id_2 = types::NetworkIdentifier {
            ssid: "bar".as_bytes().to_vec(),
            type_: types::SecurityType::Wpa,
        };

        let mock_scan_results = vec![
            types::ScanResult {
                id: test_id_1.clone(),
                entries: vec![
                    types::Bss {
                        bssid: [0, 1, 2, 3, 4, 5],
                        rssi: -14,
                        frequency: 2400,
                        timestamp_nanos: 0,
                        snr_db: 1,
                        observed_in_passive_scan: true,
                        compatible: true,
                    },
                    types::Bss {
                        bssid: [6, 7, 8, 9, 10, 11],
                        rssi: -10,
                        frequency: 2410,
                        timestamp_nanos: 1,
                        snr_db: 2,
                        observed_in_passive_scan: true,
                        compatible: true,
                    },
                    types::Bss {
                        bssid: [0, 1, 2, 3, 4, 5],
                        rssi: -20,
                        frequency: 2400,
                        timestamp_nanos: 0,
                        snr_db: 3,
                        observed_in_passive_scan: false,
                        compatible: true,
                    },
                ],
                compatibility: types::Compatibility::Supported,
            },
            types::ScanResult {
                id: test_id_2.clone(),
                entries: vec![types::Bss {
                    bssid: [20, 30, 40, 50, 60, 70],
                    rssi: -15,
                    frequency: 2400,
                    timestamp_nanos: 0,
                    snr_db: 4,
                    observed_in_passive_scan: true,
                    compatible: true,
                }],
                compatibility: types::Compatibility::Supported,
            },
            generate_random_scan_result(),
            generate_random_scan_result(),
            generate_random_scan_result(),
            generate_random_scan_result(),
            generate_random_scan_result(),
        ];

        let mut mock_saved_networks = HashMap::new();
        mock_saved_networks.insert(
            test_id_1.clone(),
            InternalNetworkData {
                credential: Credential::Password("foo_pass".as_bytes().to_vec()),
                has_ever_connected: false,
                rssi: None,
                compatible: false,
                recent_failure_count: 0,
            },
        );
        mock_saved_networks.insert(
            test_id_2.clone(),
            InternalNetworkData {
                credential: Credential::Password("bar_pass".as_bytes().to_vec()),
                has_ever_connected: false,
                rssi: None,
                compatible: false,
                recent_failure_count: 0,
            },
        );
        let random_saved_net = generate_random_saved_network();
        mock_saved_networks.insert(random_saved_net.0, random_saved_net.1);
        let random_saved_net = generate_random_saved_network();
        mock_saved_networks.insert(random_saved_net.0, random_saved_net.1);
        let random_saved_net = generate_random_saved_network();
        mock_saved_networks.insert(random_saved_net.0, random_saved_net.1);

        record_metrics_on_scan(&mock_scan_results, mock_saved_networks, &mut cobalt_api);

        // Three BSSs present for network 1 in scan results
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SAVED_NETWORK_IN_SCAN_RESULT_METRIC_ID)
                    .with_event_code(
                        SavedNetworkInScanResultMetricDimensionBssCount::TwoToFour.as_event_code()
                    )
                    .as_event()
            )
        );
        // One BSS present for network 2 in scan results
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SAVED_NETWORK_IN_SCAN_RESULT_METRIC_ID)
                    .with_event_code(
                        SavedNetworkInScanResultMetricDimensionBssCount::One.as_event_code()
                    )
                    .as_event()
            )
        );
        // Total of two saved networks in the scan results
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SCAN_RESULTS_RECEIVED_METRIC_ID)
                    .with_event_code(
                        ScanResultsReceivedMetricDimensionSavedNetworksCount::TwoToFour
                            .as_event_code()
                    )
                    .as_event()
            )
        );
        // No more metrics
        assert!(cobalt_events.try_next().is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn recorded_metrics_on_scan_no_saved_networks() {
        let (mut cobalt_api, mut cobalt_events) = create_mock_cobalt_sender_and_receiver();

        let mock_scan_results = vec![
            generate_random_scan_result(),
            generate_random_scan_result(),
            generate_random_scan_result(),
            generate_random_scan_result(),
            generate_random_scan_result(),
        ];

        let mock_saved_networks = HashMap::new();

        record_metrics_on_scan(&mock_scan_results, mock_saved_networks, &mut cobalt_api);

        // No saved networks in scan results
        assert_eq!(
            cobalt_events.try_next().unwrap(),
            Some(
                CobaltEvent::builder(SCAN_RESULTS_RECEIVED_METRIC_ID)
                    .with_event_code(
                        ScanResultsReceivedMetricDimensionSavedNetworksCount::Zero.as_event_code()
                    )
                    .as_event()
            )
        );
        // No more metrics
        assert!(cobalt_events.try_next().is_err());
    }
}

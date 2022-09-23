// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{
            scan::{self, ScanReason::NetworkSelection as NetworkSelectionScan},
            state_machine::PeriodicConnectionStats,
            types,
        },
        config_management::{
            network_config::{AddAndGetRecent, PastConnectionsByBssid},
            ConnectFailure, Credential, FailureReason, SavedNetworksManagerApi,
        },
        mode_management::iface_manager_api::IfaceManagerApi,
        telemetry::{self, TelemetryEvent, TelemetrySender},
    },
    fidl_fuchsia_metrics::{MetricEvent, MetricEventPayload},
    fidl_fuchsia_wlan_internal as fidl_internal, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_async as fasync,
    fuchsia_inspect::{Node as InspectNode, StringReference},
    fuchsia_inspect_contrib::{
        auto_persist::{self, AutoPersist},
        inspect_insert, inspect_log,
        log::{InspectList, WriteInspect},
        nodes::BoundedListNode as InspectBoundedListNode,
    },
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    log::{debug, error, info, trace, warn},
    std::{collections::HashMap, convert::TryInto as _, sync::Arc},
    wlan_common::{self, hasher::WlanHasher},
    wlan_inspect::wrappers::InspectWlanChan,
    wlan_metrics_registry::{
        SavedNetworkInScanResultMigratedMetricDimensionBssCount,
        SavedNetworkInScanResultWithActiveScanMigratedMetricDimensionActiveScanSsidsObserved as ActiveScanSsidsObserved,
        ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount,
        LAST_SCAN_AGE_WHEN_SCAN_REQUESTED_MIGRATED_METRIC_ID,
        SAVED_NETWORK_IN_SCAN_RESULT_MIGRATED_METRIC_ID,
        SAVED_NETWORK_IN_SCAN_RESULT_WITH_ACTIVE_SCAN_MIGRATED_METRIC_ID,
        SCAN_RESULTS_RECEIVED_MIGRATED_METRIC_ID,
    },
};

const RECENT_FAILURE_WINDOW: zx::Duration = zx::Duration::from_seconds(60 * 5); // 5 minutes
const RECENT_DISCONNECT_WINDOW: zx::Duration = zx::Duration::from_seconds(60 * 15); // 15 minutes

/// Above or at this RSSI, we'll give 5G networks a preference
const RSSI_CUTOFF_5G_PREFERENCE: i16 = -64;
/// The score boost for 5G networks that we are giving preference to.
const RSSI_5G_PREFERENCE_BOOST: i16 = 20;
/// The amount to decrease the score by for each failed connection attempt.
const SCORE_PENALTY_FOR_RECENT_FAILURE: i16 = 5;
/// This penalty is much higher than for a general failure because we are not likely to succeed
/// on a retry.
const SCORE_PENALTY_FOR_RECENT_CREDENTIAL_REJECTED: i16 = 30;
/// The amount to decrease the score for each time we are connected for only a short amount
/// of time before disconncting. This amount is the same as the penalty for 4 failed connect
/// attempts to a BSS.
const SCORE_PENALTY_FOR_SHORT_CONNECTION: i16 = 20;
// Threshold for what we consider a short time to be connected
const SHORT_CONNECT_DURATION: zx::Duration = zx::Duration::from_seconds(7 * 60);

const INSPECT_EVENT_LIMIT_FOR_NETWORK_SELECTIONS: usize = 10;

pub struct NetworkSelector {
    saved_network_manager: Arc<dyn SavedNetworksManagerApi>,
    last_scan_result_time: Arc<Mutex<zx::Time>>,
    hasher: WlanHasher,
    _inspect_node_root: Arc<Mutex<InspectNode>>,
    inspect_node_for_network_selections: Arc<Mutex<AutoPersist<InspectBoundedListNode>>>,
    telemetry_sender: TelemetrySender,
}
#[derive(Debug, PartialEq, Clone)]
struct InternalSavedNetworkData {
    network_id: types::NetworkIdentifier,
    credential: Credential,
    has_ever_connected: bool,
    recent_failures: Vec<ConnectFailure>,
    past_connections: PastConnectionsByBssid,
}

#[derive(Debug, Clone, PartialEq)]
struct InternalBss<'a> {
    saved_network_info: InternalSavedNetworkData,
    scanned_bss: &'a types::Bss,
    security_type_detailed: types::SecurityTypeDetailed,
    multiple_bss_candidates: bool,
    hasher: WlanHasher,
}

impl InternalBss<'_> {
    /// This function scores a BSS based on 3 factors: (1) RSSI (2) whether the BSS is 2.4 or 5 GHz
    /// and (3) recent failures to connect to this BSS. No single factor is enough to decide which
    /// BSS to connect to.
    fn score(&self) -> i16 {
        let mut score = self.scanned_bss.rssi as i16;
        let channel = types::WlanChan::from(self.scanned_bss.channel);

        // If the network is 5G and has a strong enough RSSI, give it a bonus
        if channel.is_5ghz() && score >= RSSI_CUTOFF_5G_PREFERENCE {
            score = score.saturating_add(RSSI_5G_PREFERENCE_BOOST);
        }

        // Penalize APs with recent failures to connect
        let matching_failures = self
            .saved_network_info
            .recent_failures
            .iter()
            .filter(|failure| failure.bssid == self.scanned_bss.bssid);
        for failure in matching_failures {
            // Count failures for rejected credentials higher since we probably won't succeed
            // another try with the same credentials.
            if failure.reason == FailureReason::CredentialRejected {
                score = score.saturating_sub(SCORE_PENALTY_FOR_RECENT_CREDENTIAL_REJECTED);
            } else {
                score = score.saturating_sub(SCORE_PENALTY_FOR_RECENT_FAILURE);
            }
        }
        // Penalize APs with recent short connections before disconnecting.
        let short_connection_score: i16 = self
            .recent_short_connections()
            .try_into()
            .unwrap_or_else(|_| i16::MAX)
            .saturating_mul(SCORE_PENALTY_FOR_SHORT_CONNECTION);

        return score.saturating_sub(short_connection_score);
    }

    fn recent_failure_count(&self) -> u64 {
        self.saved_network_info
            .recent_failures
            .iter()
            .filter(|failure| failure.bssid == self.scanned_bss.bssid)
            .count()
            .try_into()
            .unwrap_or_else(|e| {
                error!("{}", e);
                u64::MAX
            })
    }

    fn recent_short_connections(&self) -> usize {
        self.saved_network_info
            .past_connections
            .get_list_for_bss(&self.scanned_bss.bssid)
            .get_recent(fasync::Time::now() - RECENT_DISCONNECT_WINDOW)
            .iter()
            .filter(|d| d.connection_uptime < SHORT_CONNECT_DURATION)
            .collect::<Vec<_>>()
            .len()
    }

    fn saved_security_type_to_string(&self) -> String {
        match self.saved_network_info.network_id.security_type {
            types::SecurityType::None => "open",
            types::SecurityType::Wep => "WEP",
            types::SecurityType::Wpa => "WPA1",
            types::SecurityType::Wpa2 => "WPA2",
            types::SecurityType::Wpa3 => "WPA3",
        }
        .to_string()
    }

    fn scanned_security_type_to_string(&self) -> String {
        match self.security_type_detailed {
            types::SecurityTypeDetailed::Unknown => "unknown",
            types::SecurityTypeDetailed::Open => "open",
            types::SecurityTypeDetailed::Wep => "WEP",
            types::SecurityTypeDetailed::Wpa1 => "WPA1",
            types::SecurityTypeDetailed::Wpa1Wpa2PersonalTkipOnly => "WPA1/2Tk",
            types::SecurityTypeDetailed::Wpa2PersonalTkipOnly => "WPA2Tk",
            types::SecurityTypeDetailed::Wpa1Wpa2Personal => "WPA1/2",
            types::SecurityTypeDetailed::Wpa2Personal => "WPA2",
            types::SecurityTypeDetailed::Wpa2Wpa3Personal => "WPA2/3",
            types::SecurityTypeDetailed::Wpa3Personal => "WPA3",
            types::SecurityTypeDetailed::Wpa2Enterprise => "WPA2Ent",
            types::SecurityTypeDetailed::Wpa3Enterprise => "WPA3Ent",
        }
        .to_string()
    }

    fn to_string_without_pii(&self) -> String {
        let channel = types::WlanChan::from(self.scanned_bss.channel);
        let rssi = self.scanned_bss.rssi;
        let recent_failure_count = self.recent_failure_count();
        let recent_short_connection_count = self.recent_short_connections();
        format!(
            "{}({:4}), {}({:6}), {:>4}dBm, channel {:8}, score {:4}{}{}{}{}",
            self.hasher.hash_ssid(&self.saved_network_info.network_id.ssid),
            self.saved_security_type_to_string(),
            self.hasher.hash_mac_addr(&self.scanned_bss.bssid.0),
            self.scanned_security_type_to_string(),
            rssi,
            channel,
            self.score(),
            if !self.scanned_bss.compatible { ", NOT compatible" } else { "" },
            if recent_failure_count > 0 {
                format!(", {} recent failures", recent_failure_count)
            } else {
                "".to_string()
            },
            if recent_short_connection_count > 0 {
                format!(", {} recent short disconnects", recent_short_connection_count)
            } else {
                "".to_string()
            },
            if !self.saved_network_info.has_ever_connected { ", never used yet" } else { "" },
        )
    }
}
impl<'a> WriteInspect for InternalBss<'a> {
    fn write_inspect<'b>(&self, writer: &InspectNode, key: impl Into<StringReference<'b>>) {
        inspect_insert!(writer, var key: {
            ssid_hash: self.hasher.hash_ssid(&self.saved_network_info.network_id.ssid),
            bssid_hash: self.hasher.hash_mac_addr(&self.scanned_bss.bssid.0),
            rssi: self.scanned_bss.rssi,
            score: self.score(),
            security_type_saved: self.saved_security_type_to_string(),
            security_type_scanned: format!("{}", wlan_common::bss::Protection::from(self.security_type_detailed)),
            channel: InspectWlanChan(&self.scanned_bss.channel.into()),
            compatible: self.scanned_bss.compatible,
            recent_failure_count: self.recent_failure_count(),
            saved_network_has_ever_connected: self.saved_network_info.has_ever_connected,
        });
    }
}

impl NetworkSelector {
    pub fn new(
        saved_network_manager: Arc<dyn SavedNetworksManagerApi>,
        hasher: WlanHasher,
        inspect_node: InspectNode,
        persistence_req_sender: auto_persist::PersistenceReqSender,
        telemetry_sender: TelemetrySender,
    ) -> Self {
        let inspect_node_for_network_selection = InspectBoundedListNode::new(
            inspect_node.create_child("network_selection"),
            INSPECT_EVENT_LIMIT_FOR_NETWORK_SELECTIONS,
        );
        let inspect_node_for_network_selection = AutoPersist::new(
            inspect_node_for_network_selection,
            "wlancfg-network-selection",
            persistence_req_sender.clone(),
        );
        Self {
            saved_network_manager,
            last_scan_result_time: Arc::new(Mutex::new(zx::Time::ZERO)),
            hasher,
            _inspect_node_root: Arc::new(Mutex::new(inspect_node)),
            inspect_node_for_network_selections: Arc::new(Mutex::new(
                inspect_node_for_network_selection,
            )),
            telemetry_sender,
        }
    }

    /// Select the best available network, based on the current saved networks and the most
    /// recent scan results provided to this module.
    /// Only networks that are both saved and visible in the most recent scan results are eligible
    /// for consideration. Among those, the "best" network based on compatibility and quality (e.g.
    /// RSSI, recent failures) is selected.
    pub(crate) async fn find_best_connection_candidate(
        &self,
        iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
        ignore_list: &Vec<types::NetworkIdentifier>,
    ) -> Option<types::ConnectionCandidate> {
        // Log a metric for scan age
        let last_scan_result_time = *self.last_scan_result_time.lock().await;
        let scan_age = zx::Time::get_monotonic() - last_scan_result_time;
        if last_scan_result_time != zx::Time::ZERO {
            info!("Scan results are {}s old, triggering a scan", scan_age.into_seconds());
            self.telemetry_sender.send(TelemetryEvent::LogMetricEvents {
                events: vec![MetricEvent {
                    metric_id: LAST_SCAN_AGE_WHEN_SCAN_REQUESTED_MIGRATED_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::IntegerValue(scan_age.into_micros()),
                }],
                ctx: "NetworkSelector::perform_scan",
            });
        }

        let wpa3_supported =
            iface_manager.lock().await.has_wpa3_capable_client().await.unwrap_or_else(|e| {
                error!("Failed to determine WPA3 support. Assuming no WPA3 support. {}", e);
                false
            });

        let scan_results = scan::perform_scan(
            iface_manager.clone(),
            self.saved_network_manager.clone(),
            scan::LocationSensorUpdater { wpa3_supported },
            NetworkSelectionScan,
            Some(self.telemetry_sender.clone()),
        )
        .await;

        let (candidate_networks, num_candidates) = match scan_results {
            Err(e) => {
                warn!("Failed to get scan results for network selection, {:?}", e);
                (vec![], Err(()))
            }
            Ok(ref scan_results) => {
                let candidate_networks = merge_saved_networks_and_scan_data(
                    &self.saved_network_manager,
                    scan_results,
                    &self.hasher,
                )
                .await;

                *self.last_scan_result_time.lock().await = zx::Time::get_monotonic();
                record_metrics_on_scan(candidate_networks.clone(), &self.telemetry_sender);
                let candidate_count = candidate_networks.len();
                (candidate_networks, Ok(candidate_count))
            }
        };

        let mut inspect_node = self.inspect_node_for_network_selections.lock().await;
        let result = match select_best_connection_candidate(
            candidate_networks,
            ignore_list,
            &mut inspect_node,
        ) {
            Some((selected, channel, bssid)) => Some(
                augment_bss_with_active_scan(
                    selected,
                    channel,
                    bssid,
                    iface_manager,
                    self.telemetry_sender.clone(),
                )
                .await,
            ),
            None => None,
        };

        self.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: telemetry::NetworkSelectionType::Undirected,
            num_candidates,
            selected_any: result.is_some(),
        });
        result
    }

    /// Find a suitable BSS for the given network.
    pub(crate) async fn find_connection_candidate_for_network(
        &self,
        sme_proxy: fidl_sme::ClientSmeProxy,
        network: types::NetworkIdentifier,
    ) -> Option<types::ConnectionCandidate> {
        // TODO: check if we have recent enough scan results that we can pull from instead?
        let scan_results = scan::perform_directed_active_scan(
            &sme_proxy,
            &network.ssid,
            None,
            Some(self.telemetry_sender.clone()),
        )
        .await;

        let (result, num_candidates) = match scan_results {
            Err(_) => (None, Err(())),
            Ok(scan_results) => {
                let networks = merge_saved_networks_and_scan_data(
                    &self.saved_network_manager,
                    &scan_results,
                    &self.hasher,
                )
                .await;
                let num_candidates = Ok(networks.len());
                let ignore_list = vec![];
                let mut inspect_node = self.inspect_node_for_network_selections.lock().await;
                let result =
                    select_best_connection_candidate(networks, &ignore_list, &mut inspect_node)
                        .map(|(mut candidate, _, _)| {
                            // Strip out the information about passive vs active scan, because we can't know
                            // if this network would have been observed in a passive scan (since we never
                            // performed a passive scan).
                            if let Some(types::ScannedCandidate { ref mut observation, .. }) =
                                candidate.scanned
                            {
                                *observation = types::ScanObservation::Unknown;
                            }
                            candidate
                        });
                (result, num_candidates)
            }
        };

        self.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: telemetry::NetworkSelectionType::Directed,
            num_candidates,
            selected_any: result.is_some(),
        });
        result
    }
}

/// Merge the saved networks and scan results into a vector of BSSs that correspond to a saved
/// network.
async fn merge_saved_networks_and_scan_data<'a>(
    saved_network_manager: &Arc<dyn SavedNetworksManagerApi>,
    scan_results: &'a Vec<types::ScanResult>,
    hasher: &WlanHasher,
) -> Vec<InternalBss<'a>> {
    let mut merged_networks = vec![];
    for scan_result in scan_results {
        for saved_config in saved_network_manager
            .lookup_compatible(&scan_result.ssid, scan_result.security_type_detailed)
            .await
        {
            let multiple_bss_candidates = scan_result.entries.len() > 1;
            for bss in &scan_result.entries {
                merged_networks.push(InternalBss {
                    scanned_bss: bss,
                    multiple_bss_candidates,
                    security_type_detailed: scan_result.security_type_detailed,
                    saved_network_info: InternalSavedNetworkData {
                        network_id: types::NetworkIdentifier {
                            ssid: saved_config.ssid.clone(),
                            security_type: saved_config.security_type.into(),
                        },
                        credential: saved_config.credential.clone(),
                        has_ever_connected: saved_config.has_ever_connected,
                        recent_failures: saved_config
                            .perf_stats
                            .connect_failures
                            .get_recent_for_network(fasync::Time::now() - RECENT_FAILURE_WINDOW),
                        past_connections: saved_config.perf_stats.past_connections.clone(),
                    },
                    hasher: hasher.clone(),
                })
            }
        }
    }
    merged_networks
}

fn select_best_connection_candidate<'a>(
    bss_list: Vec<InternalBss<'a>>,
    ignore_list: &Vec<types::NetworkIdentifier>,
    inspect_node: &mut AutoPersist<InspectBoundedListNode>,
) -> Option<(types::ConnectionCandidate, types::WlanChan, types::Bssid)> {
    if bss_list.is_empty() {
        info!("No saved networks to connect to");
    } else {
        info!("Selecting from {} BSSs found for saved networks", bss_list.len());
    }

    let selected = bss_list
        .iter()
        .inspect(|bss| {
            info!("{}", bss.to_string_without_pii());
        })
        .filter(|bss| {
            // Filter out incompatible BSSs
            if !bss.scanned_bss.compatible {
                trace!("BSS is incompatible, filtering: {:?}", bss);
                return false;
            };
            // Filter out networks we've been told to ignore
            if ignore_list.contains(&bss.saved_network_info.network_id) {
                trace!("Network is ignored, filtering: {:?}", bss);
                return false;
            }
            true
        })
        .max_by_key(|bss| bss.score());

    // Log the candidates into Inspect
    inspect_log!(inspect_node.get_mut(), candidates: InspectList(&bss_list), selected?: selected);

    selected.map(|bss| {
        info!("Selected BSS:");
        info!("{}", bss.to_string_without_pii());
        (
            types::ConnectionCandidate {
                network: bss.saved_network_info.network_id.clone(),
                credential: bss.saved_network_info.credential.clone(),
                scanned: Some(types::ScannedCandidate {
                    bss_description: bss.scanned_bss.bss_description.clone(),
                    observation: bss.scanned_bss.observation,
                    has_multiple_bss_candidates: bss.multiple_bss_candidates,
                    security_type_detailed: bss.security_type_detailed,
                }),
            },
            bss.scanned_bss.channel,
            bss.scanned_bss.bssid,
        )
    })
}

/// If a BSS was discovered via a passive scan, we need to perform an active scan on it to discover
/// all the information potentially needed by the SME layer.
async fn augment_bss_with_active_scan(
    mut selected_network: types::ConnectionCandidate,
    channel: types::WlanChan,
    bssid: types::Bssid,
    iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    telemetry_sender: TelemetrySender,
) -> types::ConnectionCandidate {
    // This internal function encapsulates all the logic and has a Result<> return type, allowing us
    // to use the `?` operator inside it to reduce nesting.
    async fn get_enhanced_bss_description(
        selected_network: &types::ConnectionCandidate,
        channel: types::WlanChan,
        bssid: types::Bssid,
        iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
        telemetry_sender: TelemetrySender,
    ) -> Result<fidl_internal::BssDescription, ()> {
        // Ensure that a scan is necessary. If this expression returns `Unknown`, then either the
        // network has been scanned but the observation is unknown or the network has not be
        // scanned at all.
        let observation = selected_network
            .scanned
            .as_ref()
            .map_or(types::ScanObservation::Unknown, |scanned| scanned.observation);
        match observation {
            types::ScanObservation::Passive => {
                info!("Performing directed active scan on selected network")
            }
            types::ScanObservation::Active => {
                debug!("Network already discovered via active scan.");
                return Err(());
            }
            types::ScanObservation::Unknown => {
                error!("Unexpected `Unknown` variant of network `observation`.");
                return Err(());
            }
        }

        // Get an SME proxy
        let mut iface_manager_guard = iface_manager.lock().await;
        let sme_proxy = iface_manager_guard.get_sme_proxy_for_scan().await.map_err(|e| {
            info!("Failed to get an SME proxy for scan: {:?}", e);
        })?;
        drop(iface_manager_guard);

        // Perform the scan
        let mut directed_scan_result = scan::perform_directed_active_scan(
            &sme_proxy,
            &selected_network.network.ssid,
            Some(vec![channel.primary]),
            Some(telemetry_sender),
        )
        .await
        .map_err(|_| {
            info!("Failed to perform active scan to augment BSS info.");
        })?;

        // Find the bss in the results
        let bss_description = directed_scan_result
            .drain(..)
            .find_map(|mut network| {
                if network.ssid == selected_network.network.ssid {
                    for bss in network.entries.drain(..) {
                        if bss.bssid == bssid {
                            return Some(bss.bss_description);
                        }
                    }
                }
                None
            })
            .ok_or_else(|| {
                info!("BSS info will lack active scan augmentation, proceeding anyway.");
            })?;

        Ok(bss_description)
    }

    match get_enhanced_bss_description(
        &selected_network,
        channel,
        bssid,
        iface_manager,
        telemetry_sender,
    )
    .await
    {
        Ok(new_bss_description) => {
            let combined_scanned = selected_network.scanned.take().map(|original_scanned| {
                types::ScannedCandidate { bss_description: new_bss_description, ..original_scanned }
            });
            types::ConnectionCandidate { scanned: combined_scanned, ..selected_network }
        }
        Err(()) => selected_network,
    }
}

/// Give a numerical score to the connection quality in order to decide whether to look for a new
/// network and to ultimately decide whether to switch to a new network or stay on the same one.
/// score should be between 0 and 1, where 0 is an unusable connection and 1 is a great connection.
pub fn score_connection_quality(_connection_stats: &PeriodicConnectionStats) -> f32 {
    // TODO(fxbug.dev/84551) Actually implement the connection quality scoring and the threshold
    // for a bad connection
    return 1.0;
}

fn record_metrics_on_scan(
    mut merged_networks: Vec<InternalBss<'_>>,
    telemetry_sender: &TelemetrySender,
) {
    let mut metric_events = vec![];
    let mut merged_network_map: HashMap<types::NetworkIdentifier, Vec<InternalBss<'_>>> =
        HashMap::new();
    for bss in merged_networks.drain(..) {
        merged_network_map.entry(bss.saved_network_info.network_id.clone()).or_default().push(bss);
    }

    let num_saved_networks_observed = merged_network_map.len();
    let mut num_actively_scanned_networks = 0;
    for (_network_id, bsss) in merged_network_map {
        // Record how many BSSs are visible in the scan results for this saved network.
        let num_bss = match bsss.len() {
            0 => unreachable!(), // The ::Zero enum exists, but we shouldn't get a scan result with no BSS
            1 => SavedNetworkInScanResultMigratedMetricDimensionBssCount::One,
            2..=4 => SavedNetworkInScanResultMigratedMetricDimensionBssCount::TwoToFour,
            5..=10 => SavedNetworkInScanResultMigratedMetricDimensionBssCount::FiveToTen,
            11..=20 => SavedNetworkInScanResultMigratedMetricDimensionBssCount::ElevenToTwenty,
            21..=usize::MAX => {
                SavedNetworkInScanResultMigratedMetricDimensionBssCount::TwentyOneOrMore
            }
            _ => unreachable!(),
        };
        metric_events.push(MetricEvent {
            metric_id: SAVED_NETWORK_IN_SCAN_RESULT_MIGRATED_METRIC_ID,
            event_codes: vec![num_bss as u32],
            payload: MetricEventPayload::Count(1),
        });

        // Check if the network was found via active scan.
        if bsss
            .iter()
            .any(|bss| matches!(bss.scanned_bss.observation, types::ScanObservation::Active))
        {
            num_actively_scanned_networks += 1;
        };
    }

    let saved_network_count_metric = match num_saved_networks_observed {
        0 => ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::Zero,
        1 => ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::One,
        2..=4 => ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::TwoToFour,
        5..=20 => ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::FiveToTwenty,
        21..=40 => ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::TwentyOneToForty,
        41..=usize::MAX => {
            ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::FortyOneOrMore
        }
        _ => unreachable!(),
    };
    metric_events.push(MetricEvent {
        metric_id: SCAN_RESULTS_RECEIVED_MIGRATED_METRIC_ID,
        event_codes: vec![saved_network_count_metric as u32],
        payload: MetricEventPayload::Count(1),
    });

    let actively_scanned_networks_metrics = match num_actively_scanned_networks {
        0 => ActiveScanSsidsObserved::Zero,
        1 => ActiveScanSsidsObserved::One,
        2..=4 => ActiveScanSsidsObserved::TwoToFour,
        5..=10 => ActiveScanSsidsObserved::FiveToTen,
        11..=20 => ActiveScanSsidsObserved::ElevenToTwenty,
        21..=50 => ActiveScanSsidsObserved::TwentyOneToFifty,
        51..=100 => ActiveScanSsidsObserved::FiftyOneToOneHundred,
        101..=usize::MAX => ActiveScanSsidsObserved::OneHundredAndOneOrMore,
        _ => unreachable!(),
    };
    metric_events.push(MetricEvent {
        metric_id: SAVED_NETWORK_IN_SCAN_RESULT_WITH_ACTIVE_SCAN_MIGRATED_METRIC_ID,
        event_codes: vec![actively_scanned_networks_metrics as u32],
        payload: MetricEventPayload::Count(1),
    });

    telemetry_sender.send(TelemetryEvent::LogMetricEvents {
        events: metric_events,
        ctx: "network_selection::record_metrics_on_scan",
    });
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            access_point::state_machine as ap_fsm,
            config_management::{
                network_config::{PastConnectionData, PastConnectionsByBssid},
                SavedNetworksManager,
            },
            telemetry::ScanIssue,
            util::testing::{
                create_inspect_persistence_channel, create_wlan_hasher, generate_channel,
                generate_random_bss, poll_for_and_validate_sme_scan_request_and_send_results,
                random_connection_data, validate_sme_scan_request_and_send_results,
            },
        },
        anyhow::Error,
        async_trait::async_trait,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_common as fidl_common,
        fidl_fuchsia_wlan_common_security as fidl_security,
        fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_sme as fidl_sme,
        fuchsia_async as fasync,
        fuchsia_inspect::{self as inspect, assert_data_tree},
        futures::{
            channel::{mpsc, oneshot},
            prelude::*,
            task::Poll,
        },
        pin_utils::pin_mut,
        rand::Rng,
        std::{convert::TryFrom, sync::Arc},
        test_case::test_case,
        wlan_common::{assert_variant, random_fidl_bss_description},
    };

    struct TestValues {
        network_selector: Arc<NetworkSelector>,
        saved_network_manager: Arc<dyn SavedNetworksManagerApi>,
        iface_manager: Arc<Mutex<FakeIfaceManager>>,
        sme_stream: fidl_sme::ClientSmeRequestStream,
        inspector: inspect::Inspector,
        telemetry_receiver: mpsc::Receiver<TelemetryEvent>,
    }

    async fn test_setup() -> TestValues {
        let saved_network_manager = Arc::new(SavedNetworksManager::new_for_test().await.unwrap());
        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("net_select_test");
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);

        let network_selector = Arc::new(NetworkSelector::new(
            saved_network_manager.clone(),
            create_wlan_hasher(),
            inspect_node,
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));
        let (client_sme, remote) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("error creating proxy");
        let iface_manager = Arc::new(Mutex::new(FakeIfaceManager::new(client_sme)));

        TestValues {
            network_selector,
            saved_network_manager,
            iface_manager,
            sme_stream: remote.into_stream().expect("failed to create stream"),
            inspector,
            telemetry_receiver,
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
            _network_id: types::NetworkIdentifier,
            _reason: types::DisconnectReason,
        ) -> Result<(), Error> {
            unimplemented!()
        }

        async fn connect(&mut self, _connect_req: types::ConnectRequest) -> Result<(), Error> {
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

        async fn get_sme_proxy_for_scan(
            &mut self,
        ) -> Result<fidl_fuchsia_wlan_sme::ClientSmeProxy, Error> {
            Ok(self.sme_proxy.clone())
        }

        async fn stop_client_connections(
            &mut self,
            _reason: types::DisconnectReason,
        ) -> Result<(), Error> {
            unimplemented!()
        }

        async fn start_client_connections(&mut self) -> Result<(), Error> {
            unimplemented!()
        }

        async fn start_ap(
            &mut self,
            _config: ap_fsm::ApConfig,
        ) -> Result<oneshot::Receiver<()>, Error> {
            unimplemented!()
        }

        async fn stop_ap(&mut self, _ssid: types::Ssid, _password: Vec<u8>) -> Result<(), Error> {
            unimplemented!()
        }

        async fn stop_all_aps(&mut self) -> Result<(), Error> {
            unimplemented!()
        }

        // Many tests use wpa3 networks expecting them to be used normally, so by default this
        // is true.
        async fn has_wpa3_capable_client(&mut self) -> Result<bool, Error> {
            Ok(true)
        }

        async fn set_country(
            &mut self,
            _country_code: Option<[u8; types::REGION_CODE_LEN]>,
        ) -> Result<(), Error> {
            unimplemented!()
        }
    }

    fn generate_random_saved_network() -> (types::NetworkIdentifier, InternalSavedNetworkData) {
        let mut rng = rand::thread_rng();
        let net_id = types::NetworkIdentifier {
            ssid: types::Ssid::try_from(format!("saved network rand {}", rng.gen::<i32>()))
                .expect("Failed to create random SSID from String"),
            security_type: types::SecurityType::Wpa,
        };
        (
            net_id.clone(),
            InternalSavedNetworkData {
                network_id: net_id,
                credential: Credential::Password(
                    format!("password {}", rng.gen::<i32>()).as_bytes().to_vec(),
                ),
                has_ever_connected: false,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
        )
    }

    #[fuchsia::test]
    async fn scan_results_merged_with_saved_networks() {
        let test_values = test_setup().await;

        // create some identifiers
        let test_ssid_1 = types::Ssid::try_from("foo").unwrap();
        let test_security_1 = types::SecurityTypeDetailed::Wpa3Personal;
        let test_id_1 = types::NetworkIdentifier {
            ssid: test_ssid_1.clone(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_ssid_2 = types::Ssid::try_from("bar").unwrap();
        let test_security_2 = types::SecurityTypeDetailed::Wpa1;
        let test_id_2 = types::NetworkIdentifier {
            ssid: test_ssid_2.clone(),
            security_type: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        // insert the saved networks
        assert!(test_values
            .saved_network_manager
            .store(test_id_1.clone().into(), credential_1.clone())
            .await
            .unwrap()
            .is_none());

        assert!(test_values
            .saved_network_manager
            .store(test_id_2.clone().into(), credential_2.clone())
            .await
            .unwrap()
            .is_none());

        // build some scan results
        let mock_scan_results = vec![
            types::ScanResult {
                ssid: test_ssid_1.clone(),
                security_type_detailed: test_security_1.clone(),
                entries: vec![generate_random_bss(), generate_random_bss(), generate_random_bss()],
                compatibility: types::Compatibility::Supported,
            },
            types::ScanResult {
                ssid: test_ssid_2.clone(),
                security_type_detailed: test_security_2.clone(),
                entries: vec![generate_random_bss()],
                compatibility: types::Compatibility::DisallowedNotSupported,
            },
        ];

        let bssid_1 = mock_scan_results[0].entries[0].bssid;
        let bssid_2 = mock_scan_results[0].entries[1].bssid;

        // mark the first one as having connected
        test_values
            .saved_network_manager
            .record_connect_result(
                test_id_1.clone().into(),
                &credential_1.clone(),
                bssid_1,
                fake_successful_connect_result(),
                None,
            )
            .await;

        // mark the second one as having a failure
        test_values
            .saved_network_manager
            .record_connect_result(
                test_id_1.clone().into(),
                &credential_1.clone(),
                bssid_2,
                fidl_sme::ConnectResult {
                    code: fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
                    is_credential_rejected: true,
                    ..fake_successful_connect_result()
                },
                None,
            )
            .await;

        // build our expected result
        let failure_time = test_values
            .saved_network_manager
            .lookup(&test_id_1.clone().into())
            .await
            .get(0)
            .expect("failed to get config")
            .perf_stats
            .connect_failures
            .get_recent_for_network(fasync::Time::now() - RECENT_FAILURE_WINDOW)
            .get(0)
            .expect("failed to get recent failure")
            .time;
        let recent_failures = vec![ConnectFailure {
            bssid: bssid_2,
            time: failure_time,
            reason: FailureReason::CredentialRejected,
        }];
        let hasher = WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes());
        let expected_internal_data_1 = InternalSavedNetworkData {
            network_id: test_id_1.clone(),
            credential: credential_1.clone(),
            has_ever_connected: true,
            recent_failures: recent_failures.clone(),
            past_connections: PastConnectionsByBssid::new(),
        };
        let expected_result = vec![
            InternalBss {
                security_type_detailed: test_security_1,
                saved_network_info: expected_internal_data_1.clone(),
                scanned_bss: &mock_scan_results[0].entries[0],
                multiple_bss_candidates: true,
                hasher: hasher.clone(),
            },
            InternalBss {
                security_type_detailed: test_security_1,
                saved_network_info: expected_internal_data_1.clone(),
                scanned_bss: &mock_scan_results[0].entries[1],
                multiple_bss_candidates: true,
                hasher: hasher.clone(),
            },
            InternalBss {
                security_type_detailed: test_security_1,
                saved_network_info: expected_internal_data_1,
                scanned_bss: &mock_scan_results[0].entries[2],
                multiple_bss_candidates: true,
                hasher: hasher.clone(),
            },
            InternalBss {
                security_type_detailed: test_security_2,
                saved_network_info: InternalSavedNetworkData {
                    network_id: test_id_2.clone(),
                    credential: credential_2.clone(),
                    has_ever_connected: false,
                    recent_failures: Vec::new(),
                    past_connections: PastConnectionsByBssid::new(),
                },
                scanned_bss: &mock_scan_results[1].entries[0],
                multiple_bss_candidates: false,
                hasher: hasher.clone(),
            },
        ];

        // validate the function works
        let result = merge_saved_networks_and_scan_data(
            &test_values.saved_network_manager,
            &mock_scan_results,
            &hasher,
        )
        .await;
        assert_eq!(result, expected_result);
    }

    #[test_case(types::Bss {
            rssi: -8,
            channel: generate_channel(1),
            ..generate_random_bss()
        },
        -8; "2.4GHz BSS score is RSSI")]
    #[test_case(types::Bss {
            rssi: -49,
            channel: generate_channel(36),
            ..generate_random_bss()
        },
        -29; "5GHz score is (RSSI + mod), when above threshold")]
    #[test_case(types::Bss {
            rssi: -71,
            channel: generate_channel(36),
            ..generate_random_bss()
        },
        -71; "5GHz score is RSSI, when below threshold")]
    #[fuchsia::test(add_test_attr = false)]
    fn scoring_test(bss: types::Bss, expected_score: i16) {
        let _exec =
            fasync::TestExecutor::new_with_fake_time().expect("failed to create an executor");
        let mut rng = rand::thread_rng();

        let network_id = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("test").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let internal_bss = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: network_id,
                credential: Credential::None,
                has_ever_connected: rng.gen::<bool>(),
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };

        assert_eq!(internal_bss.score(), expected_score)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_score_bss_prefers_less_short_connections() {
        let bss_worse =
            types::Bss { rssi: -60, channel: generate_channel(3), ..generate_random_bss() };
        let bss_better =
            types::Bss { rssi: -60, channel: generate_channel(3), ..generate_random_bss() };
        let (_test_id, mut internal_data) = generate_random_saved_network();
        let short_uptime = zx::Duration::from_seconds(30);
        let okay_uptime = zx::Duration::from_minutes(100);
        // Record a short uptime for the worse network and a long enough uptime for the better one.
        let short_uptime_data = past_connection_with_bssid_uptime(bss_worse.bssid, short_uptime);
        let okay_uptime_data = past_connection_with_bssid_uptime(bss_better.bssid, okay_uptime);
        internal_data.past_connections.add(bss_worse.bssid, short_uptime_data);
        internal_data.past_connections.add(bss_better.bssid, okay_uptime_data);
        let bss_worse = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data.clone(),
            scanned_bss: &bss_worse,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };
        let bss_better = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data,
            scanned_bss: &bss_better,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };
        // Check that the better BSS has a higher score than the worse BSS.
        assert!(bss_better.score() > bss_worse.score());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_score_bss_prefers_less_failures() {
        let bss_worse =
            types::Bss { rssi: -60, channel: generate_channel(3), ..generate_random_bss() };
        let bss_better =
            types::Bss { rssi: -60, channel: generate_channel(3), ..generate_random_bss() };
        let (_test_id, mut internal_data) = generate_random_saved_network();
        // Add many test failures for the worse BSS and one for the better BSS
        let mut failures = vec![connect_failure_with_bssid(bss_worse.bssid); 12];
        failures.push(connect_failure_with_bssid(bss_better.bssid));
        internal_data.recent_failures = failures;
        let bss_worse = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data.clone(),
            scanned_bss: &bss_worse,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };
        let bss_better = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data,
            scanned_bss: &bss_better,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };
        // Check that the better BSS has a higher score than the worse BSS.
        assert!(bss_better.score() > bss_worse.score());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_score_bss_prefers_stronger_with_failures() {
        // Test test that if one network has a few network failures but is 5 Ghz instead of 2.4,
        // the 5 GHz network has a higher score.
        let bss_worse =
            types::Bss { rssi: -35, channel: generate_channel(3), ..generate_random_bss() };
        let bss_better =
            types::Bss { rssi: -35, channel: generate_channel(36), ..generate_random_bss() };
        let (_test_id, mut internal_data) = generate_random_saved_network();
        // Set the failure list to have 0 failures for the worse BSS and 4 failures for the
        // stronger BSS.
        internal_data.recent_failures = vec![connect_failure_with_bssid(bss_better.bssid); 2];
        let bss_worse = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data.clone(),
            scanned_bss: &bss_worse,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };
        let bss_better = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data,
            scanned_bss: &bss_better,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };
        assert!(bss_better.score() > bss_worse.score());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_score_credentials_rejected_worse() {
        // If two BSS are identical other than one failed to connect with wrong credentials and
        // the other failed with a few connect failurs, the one with wrong credentials has a lower
        // score.
        let bss_worse =
            types::Bss { rssi: -30, channel: generate_channel(44), ..generate_random_bss() };
        let bss_better =
            types::Bss { rssi: -30, channel: generate_channel(44), ..generate_random_bss() };
        let (_test_id, mut internal_data) = generate_random_saved_network();
        // Add many test failures for the worse BSS and one for the better BSS
        let mut failures = vec![connect_failure_with_bssid(bss_better.bssid); 4];
        failures.push(ConnectFailure {
            bssid: bss_worse.bssid,
            time: fasync::Time::now(),
            reason: FailureReason::CredentialRejected,
        });
        internal_data.recent_failures = failures;

        let bss_worse = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data.clone(),
            scanned_bss: &bss_worse,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };
        let bss_better = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data,
            scanned_bss: &bss_better,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };

        assert!(bss_better.score() > bss_worse.score());
    }

    #[fasync::run_singlethreaded(test)]
    async fn score_many_penalties_do_not_cause_panic() {
        let bss = types::Bss { rssi: -80, channel: generate_channel(1), ..generate_random_bss() };
        let (_test_id, mut internal_data) = generate_random_saved_network();
        // Add 10 general failures and 10 rejected credentials failures
        internal_data.recent_failures = vec![connect_failure_with_bssid(bss.bssid); 10];
        for _ in 0..1200 {
            internal_data.recent_failures.push(ConnectFailure {
                bssid: bss.bssid,
                time: fasync::Time::now(),
                reason: FailureReason::CredentialRejected,
            });
        }
        let short_uptime = zx::Duration::from_seconds(30);
        let data = past_connection_with_bssid_uptime(bss.bssid, short_uptime);
        for _ in 0..10 {
            internal_data.past_connections.add(bss.bssid, data);
        }
        let internal_bss = InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: internal_data.clone(),
            scanned_bss: &bss,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        };

        assert_eq!(internal_bss.score(), i16::MIN);
    }

    #[fuchsia::test]
    fn select_best_connection_candidate_sorts_by_score() {
        let _executor = fasync::TestExecutor::new();
        // generate Inspect nodes
        let inspector = inspect::Inspector::new();
        let mut inspect_node = AutoPersist::new(
            InspectBoundedListNode::new(inspector.root().create_child("test"), 10),
            "sample-persistence-tag",
            create_inspect_persistence_channel().0,
        );
        // build networks list
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("bar").unwrap(),
            security_type: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        let mut networks = vec![];

        let bss_1 = types::Bss {
            compatible: true,
            rssi: -14,
            channel: generate_channel(36),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_1,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_2 = types::Bss {
            compatible: true,
            rssi: -10,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_2,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_3 = types::Bss {
            compatible: true,
            rssi: -8,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_2.clone(),
                credential: credential_2.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_3,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        // there's a network on 5G, it should get a boost and be selected
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_1.clone(),
                    credential: credential_1.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_1.bss_description.clone(),
                        observation: bss_1.observation,
                        has_multiple_bss_candidates: true,
                        security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
                    }),
                },
                bss_1.channel,
                bss_1.bssid
            ))
        );

        // make the 5GHz network into a 2.4GHz network
        let mut modified_network = networks[0].clone();
        let modified_bss =
            types::Bss { channel: generate_channel(6), ..modified_network.scanned_bss.clone() };
        modified_network.scanned_bss = &modified_bss;
        networks[0] = modified_network;

        // all networks are 2.4GHz, strongest RSSI network returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_2.clone(),
                    credential: credential_2.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: networks[2].scanned_bss.bss_description.clone(),
                        observation: networks[2].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
                    }),
                },
                networks[2].scanned_bss.channel,
                networks[2].scanned_bss.bssid
            ))
        );
    }

    #[fuchsia::test]
    fn select_best_connection_candidate_sorts_by_failure_count() {
        let _executor = fasync::TestExecutor::new();
        // generate Inspect nodes
        let inspector = inspect::Inspector::new();
        let mut inspect_node = AutoPersist::new(
            InspectBoundedListNode::new(inspector.root().create_child("test"), 10),
            "sample-persistence-tag",
            create_inspect_persistence_channel().0,
        );
        // build networks list
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("bar").unwrap(),
            security_type: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        let mut networks = vec![];

        let bss_1 = types::Bss {
            compatible: true,
            rssi: -34,
            channel: generate_channel(3),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa1Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_1,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_2 = types::Bss {
            compatible: true,
            rssi: -50,
            channel: generate_channel(3),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa1Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_2.clone(),
                credential: credential_2.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_2,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        // stronger network returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_1.clone(),
                    credential: credential_1.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_1.bss_description.clone(),
                        observation: networks[0].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed:
                            types::SecurityTypeDetailed::Wpa1Wpa2PersonalTkipOnly,
                    }),
                },
                bss_1.channel,
                bss_1.bssid
            ))
        );

        // mark the stronger network as having some failures
        let num_failures = 4;
        networks[0].saved_network_info.recent_failures =
            vec![connect_failure_with_bssid(bss_1.bssid); num_failures];
        networks[1].saved_network_info.recent_failures =
            vec![connect_failure_with_bssid(bss_1.bssid); num_failures];

        // weaker network (with no failures) returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_2.clone(),
                    credential: credential_2.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_2.bss_description.clone(),
                        observation: networks[1].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed:
                            types::SecurityTypeDetailed::Wpa1Wpa2PersonalTkipOnly,
                    }),
                },
                bss_2.channel,
                bss_2.bssid
            ))
        );

        // give them both the same number of failures
        networks[1].saved_network_info.recent_failures =
            vec![connect_failure_with_bssid(bss_2.bssid.clone()); num_failures];

        // stronger network returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_1.clone(),
                    credential: credential_1.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_1.bss_description.clone(),
                        observation: networks[0].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed:
                            types::SecurityTypeDetailed::Wpa1Wpa2PersonalTkipOnly,
                    }),
                },
                bss_1.channel,
                bss_1.bssid
            ))
        );
    }

    #[fuchsia::test]
    fn select_best_connection_candidate_incompatible() {
        let _executor = fasync::TestExecutor::new();
        // generate Inspect nodes
        let inspector = inspect::Inspector::new();
        let mut inspect_node = AutoPersist::new(
            InspectBoundedListNode::new(inspector.root().create_child("test"), 10),
            "sample-persistence-tag",
            create_inspect_persistence_channel().0,
        );
        // build networks list
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("bar").unwrap(),
            security_type: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        let mut networks = vec![];

        let bss_1 = types::Bss {
            compatible: true,
            rssi: -14,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa3Personal,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_1,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_2 = types::Bss {
            compatible: false,
            rssi: -10,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_2,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_3 = types::Bss {
            compatible: true,
            rssi: -12,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa1,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_2.clone(),
                credential: credential_2.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_3,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        // stronger network returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_2.clone(),
                    credential: credential_2.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_3.bss_description.clone(),
                        observation: networks[2].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed: types::SecurityTypeDetailed::Wpa1,
                    }),
                },
                bss_3.channel,
                bss_3.bssid
            ))
        );

        // mark the stronger network as incompatible
        let mut modified_network = networks[2].clone();
        let modified_bss = types::Bss { compatible: false, ..modified_network.scanned_bss.clone() };
        modified_network.scanned_bss = &modified_bss;
        networks[2] = modified_network;

        // other network returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_1.clone(),
                    credential: credential_1.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: networks[0].scanned_bss.bss_description.clone(),
                        observation: networks[0].scanned_bss.observation,
                        has_multiple_bss_candidates: true,
                        security_type_detailed: types::SecurityTypeDetailed::Wpa3Personal,
                    }),
                },
                networks[0].scanned_bss.channel,
                networks[0].scanned_bss.bssid
            ))
        );
    }

    #[fuchsia::test]
    fn select_best_connection_candidate_ignore_list() {
        let _executor = fasync::TestExecutor::new();
        // generate Inspect nodes
        let inspector = inspect::Inspector::new();
        let mut inspect_node = AutoPersist::new(
            InspectBoundedListNode::new(inspector.root().create_child("test"), 10),
            "sample-persistence-tag",
            create_inspect_persistence_channel().0,
        );
        // build networks list
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("bar").unwrap(),
            security_type: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        let mut networks = vec![];

        let bss_1 = types::Bss { compatible: true, rssi: -100, ..generate_random_bss() };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_1,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_2 = types::Bss { compatible: true, rssi: -12, ..generate_random_bss() };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_2.clone(),
                credential: credential_2.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_2,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        // stronger network returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_2.clone(),
                    credential: credential_2.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_2.bss_description.clone(),
                        observation: networks[1].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
                    }),
                },
                bss_2.channel,
                bss_2.bssid
            ))
        );

        // ignore the stronger network, other network returned
        assert_eq!(
            select_best_connection_candidate(
                networks.clone(),
                &vec![test_id_2.clone()],
                &mut inspect_node
            ),
            Some((
                types::ConnectionCandidate {
                    network: test_id_1.clone(),
                    credential: credential_1.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_1.bss_description.clone(),
                        observation: networks[0].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
                    }),
                },
                bss_1.channel,
                bss_1.bssid
            ))
        );
    }

    #[fuchsia::test]
    fn select_best_connection_candidate_logs_to_inspect() {
        let _executor = fasync::TestExecutor::new();
        // generate Inspect nodes
        let inspector = inspect::Inspector::new();
        let mut inspect_node = AutoPersist::new(
            InspectBoundedListNode::new(inspector.root().create_child("test"), 10),
            "sample-persistence-tag",
            create_inspect_persistence_channel().0,
        );
        // build networks list
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let test_id_2 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("bar").unwrap(),
            security_type: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());

        let mut networks = vec![];

        let bss_1 = types::Bss {
            compatible: true,
            rssi: -14,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_1,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_2 = types::Bss {
            compatible: false,
            rssi: -10,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_1.clone(),
                credential: credential_1.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_2,
            multiple_bss_candidates: true,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        let bss_3 = types::Bss {
            compatible: true,
            rssi: -12,
            channel: generate_channel(1),
            ..generate_random_bss()
        };
        networks.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_2.clone(),
                credential: credential_2.clone(),
                has_ever_connected: true,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &bss_3,
            multiple_bss_candidates: false,
            hasher: WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes()),
        });

        // stronger network returned
        assert_eq!(
            select_best_connection_candidate(networks.clone(), &vec![], &mut inspect_node),
            Some((
                types::ConnectionCandidate {
                    network: test_id_2.clone(),
                    credential: credential_2.clone(),
                    scanned: Some(types::ScannedCandidate {
                        bss_description: bss_3.bss_description.clone(),
                        observation: networks[2].scanned_bss.observation,
                        has_multiple_bss_candidates: false,
                        security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
                    }),
                },
                bss_3.channel,
                bss_3.bssid
            ))
        );

        let fidl_channel = fidl_common::WlanChannel::from(networks[2].scanned_bss.channel);
        assert_data_tree!(inspector, root: {
            test: {
                "0": {
                    "@time": inspect::testing::AnyProperty,
                    "candidates": {
                        "0": contains {
                            score: inspect::testing::AnyProperty,
                        },
                        "1": contains {
                            score: inspect::testing::AnyProperty,
                        },
                        "2": contains {
                            score: inspect::testing::AnyProperty,
                        },
                    },
                    "selected": {
                        ssid_hash: networks[2].hasher.hash_ssid(&networks[2].saved_network_info.network_id.ssid),
                        bssid_hash: networks[2].hasher.hash_mac_addr(&networks[2].scanned_bss.bssid.0),
                        rssi: i64::from(networks[2].scanned_bss.rssi),
                        score: i64::from(networks[2].score()),
                        security_type_saved: networks[2].saved_security_type_to_string(),
                        security_type_scanned: format!("{}", wlan_common::bss::Protection::from(networks[2].security_type_detailed)),
                        channel: {
                            cbw: inspect::testing::AnyProperty,
                            primary: u64::from(fidl_channel.primary),
                            secondary80: u64::from(fidl_channel.secondary80),
                        },
                        compatible: networks[2].scanned_bss.compatible,
                        recent_failure_count: networks[2].recent_failure_count(),
                        saved_network_has_ever_connected: networks[2].saved_network_info.has_ever_connected,
                    },
                }
            },
        });
    }

    #[fuchsia::test]
    fn augment_bss_with_active_scan_doesnt_run_on_actively_found_networks() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let bss_1 = types::Bss {
            compatible: true,
            rssi: -14,
            channel: generate_channel(36),
            ..generate_random_bss()
        };
        let connect_req = types::ConnectionCandidate {
            network: test_id_1.clone(),
            credential: credential_1.clone(),
            scanned: Some(types::ScannedCandidate {
                bss_description: bss_1.bss_description.clone(),
                observation: types::ScanObservation::Active,
                has_multiple_bss_candidates: false,
                security_type_detailed: types::SecurityTypeDetailed::Wpa3Personal,
            }),
        };

        let fut = augment_bss_with_active_scan(
            connect_req.clone(),
            bss_1.channel,
            bss_1.bssid,
            test_values.iface_manager.clone(),
            test_values.network_selector.telemetry_sender.clone(),
        );
        pin_mut!(fut);

        // The connect_req comes out the other side with no change
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(req) => {
            assert_eq!(req, connect_req)}
        );
    }

    #[fuchsia::test]
    fn augment_bss_with_active_scan_runs_on_passively_found_networks() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let bss_1 = types::Bss {
            compatible: true,
            rssi: -14,
            channel: generate_channel(36),
            ..generate_random_bss()
        };
        let connect_req = types::ConnectionCandidate {
            network: test_id_1.clone(),
            credential: credential_1.clone(),
            scanned: Some(types::ScannedCandidate {
                bss_description: bss_1.bss_description.clone(),
                observation: types::ScanObservation::Passive,
                has_multiple_bss_candidates: true,
                security_type_detailed: types::SecurityTypeDetailed::Wpa2Personal,
            }),
        };

        let fut = augment_bss_with_active_scan(
            connect_req.clone(),
            bss_1.channel,
            bss_1.bssid,
            test_values.iface_manager.clone(),
            test_values.network_selector.telemetry_sender.clone(),
        );
        pin_mut!(fut);

        // Progress the future until a scan request is sent
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back results
        let expected_scan_request = fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![test_id_1.ssid.to_vec()],
            channels: vec![36],
        });
        let new_bss_desc = random_fidl_bss_description!(
            Wpa3,
            bssid: bss_1.bssid.0,
            ssid: test_id_1.ssid.clone(),
            rssi_dbm: 0,
            snr_db: 0,
            channel: types::WlanChan::new(1, types::Cbw::Cbw20),
        );
        let new_scan_result = fidl_sme::ScanResult {
            compatibility: Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa3Personal],
            })),
            timestamp_nanos: 0, // Must be monotonic; set this field adjacent to its usage.
            bss_description: new_bss_desc.clone(),
        };

        let mock_scan_results = vec![
            fidl_sme::ScanResult {
                compatibility: Some(Box::new(fidl_sme::Compatibility {
                    mutual_security_protocols: vec![fidl_security::Protocol::Wpa3Personal],
                })),
                timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
                bss_description: random_fidl_bss_description!(
                    Wpa3,
                    bssid: [0, 0, 0, 0, 0, 0], // Not the same BSSID
                    ssid: test_id_1.ssid.clone(),
                    rssi_dbm: 10,
                    snr_db: 10,
                    channel: types::WlanChan::new(1, types::Cbw::Cbw20),
                ),
            },
            fidl_sme::ScanResult {
                timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
                ..new_scan_result
            },
        ];
        validate_sme_scan_request_and_send_results(
            &mut exec,
            &mut test_values.sme_stream,
            &expected_scan_request,
            mock_scan_results,
        );

        // The connect_req comes out the other side with the new bss_description
        assert_eq!(
            exec.run_singlethreaded(fut),
            types::ConnectionCandidate {
                scanned: Some(types::ScannedCandidate {
                    bss_description: new_bss_desc,
                    // The network was observed in a passive scan prior to the directed active
                    // scan, so this should remain `Passive`.
                    observation: types::ScanObservation::Passive,
                    // Multiple BSSes were observed prior to the directed active scan, so this
                    // field should remain `true`.
                    has_multiple_bss_candidates: true,
                    security_type_detailed: types::SecurityTypeDetailed::Wpa2Personal,
                }),
                ..connect_req
            }
        );
    }

    #[fuchsia::test]
    fn find_best_connection_candidate_scan_error() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());
        let network_selector = test_values.network_selector;
        let mut telemetry_receiver = test_values.telemetry_receiver;

        // Kick off network selection
        let ignore_list = vec![];
        let network_selection_fut = network_selector
            .find_best_connection_candidate(test_values.iface_manager.clone(), &ignore_list);
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
                // Send all the APs
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_error(&mut fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::InternalError,
                    message: "Failed to scan".to_string()
                })
                    .expect("failed to send scan data");
            }
        );

        // Process scan results
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Ready(None));

        // Check the network selections were logged
        assert_data_tree!(test_values.inspector, root: {
            net_select_test: {
                network_selection: {
                    "0": {
                        "@time": inspect::testing::AnyProperty,
                        "candidates": {},
                    },
                }
            },
        });

        // Verify two sets of TelemetryEvent for network selection were sent
        assert_variant!(
            telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ScanDefect(ScanIssue::ScanFailure)))
        );
        assert_variant!(telemetry_receiver.try_next(), Ok(Some(event)) => {
            assert_variant!(event, TelemetryEvent::NetworkSelectionDecision {
                network_selection_type: telemetry::NetworkSelectionType::Undirected,
                num_candidates: Err(()),
                selected_any: false,
            });
        });
    }

    #[fuchsia::test]
    fn find_best_connection_candidate_end_to_end() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());
        let network_selector = test_values.network_selector;
        let mut telemetry_receiver = test_values.telemetry_receiver;

        // create some identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());
        let scan_result1 = fidl_sme::ScanResult {
            compatibility: Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa3Personal],
            })),
            timestamp_nanos: 0, // Must be monotonic; set this field adjacent to its usage.
            bss_description: random_fidl_bss_description!(
                Wpa3,
                bssid: [0, 0, 0, 0, 0, 0],
                ssid: test_id_1.ssid.clone(),
                rssi_dbm: 10,
                snr_db: 10,
                channel: types::WlanChan::new(1, types::Cbw::Cbw20),
            ),
        };
        let bss_desc1_active = random_fidl_bss_description!(
            Wpa3,
            bssid: [0, 0, 0, 0, 0, 0],
            ssid: test_id_1.ssid.clone(),
            rssi_dbm: 10,
            snr_db: 10,
            channel: types::WlanChan::new(1, types::Cbw::Cbw20),
        );
        let scan_result1_active = fidl_sme::ScanResult {
            compatibility: Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa3Personal],
            })),
            timestamp_nanos: 0, // Must be monotonic; set this field adjacent to its usage.
            bss_description: bss_desc1_active.clone(),
        };
        let test_id_2 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("bar").unwrap(),
            security_type: types::SecurityType::Wpa,
        };
        let credential_2 = Credential::Password("bar_pass".as_bytes().to_vec());
        let scan_result2 = fidl_sme::ScanResult {
            compatibility: Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa1],
            })),
            timestamp_nanos: 0, // Must be monotonic; set this field adjacent to its usage.
            bss_description: random_fidl_bss_description!(
                Wpa1,
                bssid: [0, 0, 0, 0, 0, 0],
                ssid: test_id_2.ssid.clone(),
                rssi_dbm: 0,
                snr_db: 0,
                channel: types::WlanChan::new(1, types::Cbw::Cbw20),
            ),
        };
        let bss_desc2_active = random_fidl_bss_description!(
            Wpa1,
            bssid: [0, 0, 0, 0, 0, 0],
            ssid: test_id_2.ssid.clone(),
            rssi_dbm: 10,
            snr_db: 10,
            channel: types::WlanChan::new(1, types::Cbw::Cbw20),
        );
        let scan_result2_active = fidl_sme::ScanResult {
            compatibility: Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa1],
            })),
            timestamp_nanos: 0, // Must be monotonic; set this field adjacent to its usage.
            bss_description: bss_desc2_active.clone(),
        };

        // insert some new saved networks
        assert!(exec
            .run_singlethreaded(
                test_values
                    .saved_network_manager
                    .store(test_id_1.clone().into(), credential_1.clone()),
            )
            .unwrap()
            .is_none());
        assert!(exec
            .run_singlethreaded(
                test_values
                    .saved_network_manager
                    .store(test_id_2.clone().into(), credential_2.clone()),
            )
            .unwrap()
            .is_none());

        // Mark them as having connected. Make connection passive so that no active scans are made.
        exec.run_singlethreaded(test_values.saved_network_manager.record_connect_result(
            test_id_1.clone().into(),
            &credential_1.clone(),
            types::Bssid([0, 0, 0, 0, 0, 0]),
            fake_successful_connect_result(),
            Some(fidl_common::ScanType::Passive),
        ));
        exec.run_singlethreaded(test_values.saved_network_manager.record_connect_result(
            test_id_2.clone().into(),
            &credential_2.clone(),
            types::Bssid([0, 0, 0, 0, 0, 0]),
            fake_successful_connect_result(),
            Some(fidl_common::ScanType::Passive),
        ));

        // Kick off network selection
        let ignore_list = vec![];
        let network_selection_fut = network_selector
            .find_best_connection_candidate(test_values.iface_manager.clone(), &ignore_list);
        pin_mut!(network_selection_fut);
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back results
        let expected_scan_request = fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {});
        let mock_scan_results = vec![
            fidl_sme::ScanResult {
                timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
                ..scan_result1
            },
            fidl_sme::ScanResult {
                timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
                ..scan_result2
            },
        ];
        validate_sme_scan_request_and_send_results(
            &mut exec,
            &mut test_values.sme_stream,
            &expected_scan_request,
            mock_scan_results.clone(),
        );

        // Process scan results
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Pending);

        // An additional directed active scan should be made for the selected network
        let expected_scan_request = fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![test_id_1.ssid.to_vec()],
            channels: vec![1],
        });
        let mock_active_scan_results = vec![fidl_sme::ScanResult {
            timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
            ..scan_result1_active
        }];
        poll_for_and_validate_sme_scan_request_and_send_results(
            &mut exec,
            &mut network_selection_fut,
            &mut test_values.sme_stream,
            &expected_scan_request,
            mock_active_scan_results,
        );

        // Check that we pick a network
        let results = exec.run_singlethreaded(&mut network_selection_fut);
        assert_eq!(
            results,
            Some(types::ConnectionCandidate {
                network: test_id_1.clone(),
                credential: credential_1.clone(),
                scanned: Some(types::ScannedCandidate {
                    bss_description: bss_desc1_active,
                    observation: types::ScanObservation::Passive,
                    has_multiple_bss_candidates: false,
                    security_type_detailed: types::SecurityTypeDetailed::Wpa3Personal,
                }),
            })
        );

        // Ignore that network, check that we pick the other one
        let ignore_list = vec![test_id_1.clone()];
        let network_selection_fut = network_selector
            .find_best_connection_candidate(test_values.iface_manager.clone(), &ignore_list);
        pin_mut!(network_selection_fut);
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back results
        let expected_scan_request = fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {});
        validate_sme_scan_request_and_send_results(
            &mut exec,
            &mut test_values.sme_stream,
            &expected_scan_request,
            mock_scan_results,
        );

        // Process scan results
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Pending);

        // An additional directed active scan should be made for the selected network
        let expected_scan_request = fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![test_id_2.ssid.to_vec()],
            channels: vec![1],
        });
        let mock_active_scan_results = vec![fidl_sme::ScanResult {
            timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
            ..scan_result2_active
        }];
        poll_for_and_validate_sme_scan_request_and_send_results(
            &mut exec,
            &mut network_selection_fut,
            &mut test_values.sme_stream,
            &expected_scan_request,
            mock_active_scan_results,
        );

        let results = exec.run_singlethreaded(&mut network_selection_fut);
        assert_eq!(
            results,
            Some(types::ConnectionCandidate {
                network: test_id_2.clone(),
                credential: credential_2.clone(),
                scanned: Some(types::ScannedCandidate {
                    bss_description: bss_desc2_active,
                    observation: types::ScanObservation::Passive,
                    has_multiple_bss_candidates: false,
                    security_type_detailed: types::SecurityTypeDetailed::Wpa1,
                }),
            })
        );

        // Check the network selections were logged
        assert_data_tree!(test_values.inspector, root: {
            net_select_test: {
                network_selection: {
                    "0": {
                        "@time": inspect::testing::AnyProperty,
                        "candidates": {
                            "0": contains {
                                bssid_hash: inspect::testing::AnyProperty,
                                score: inspect::testing::AnyProperty,
                            },
                            "1": contains {
                                bssid_hash: inspect::testing::AnyProperty,
                                score: inspect::testing::AnyProperty,
                            },
                        },
                        "selected": contains {
                            bssid_hash: inspect::testing::AnyProperty,
                            score: inspect::testing::AnyProperty,
                        },
                    },
                    "1": {
                        "@time": inspect::testing::AnyProperty,
                        "candidates": {
                            "0": contains {
                                bssid_hash: inspect::testing::AnyProperty,
                                score: inspect::testing::AnyProperty,
                            },
                            "1": contains {
                                bssid_hash: inspect::testing::AnyProperty,
                                score: inspect::testing::AnyProperty,
                            },
                        },
                        "selected": contains {
                            bssid_hash: inspect::testing::AnyProperty,
                            score: inspect::testing::AnyProperty,
                        },
                    }
                }
            },
        });

        // Verify two sets of TelemetryEvent for network selection were sent
        assert_variant!(
            telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ActiveScanRequested { .. }))
        );
        assert_variant!(
            telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::LogMetricEvents {
                ctx: "network_selection::record_metrics_on_scan",
                ..
            }))
        );
        assert_variant!(telemetry_receiver.try_next(), Ok(Some(event)) => {
            assert_variant!(event, TelemetryEvent::NetworkSelectionDecision {
                network_selection_type: telemetry::NetworkSelectionType::Undirected,
                num_candidates: Ok(2),
                selected_any: true,
            });
        });

        assert_variant!(telemetry_receiver.try_next(), Ok(Some(TelemetryEvent::LogMetricEvents { events, ctx: "NetworkSelector::perform_scan" })) => {
            assert_eq!(events.len(), 1);
            assert_eq!(events[0].metric_id, LAST_SCAN_AGE_WHEN_SCAN_REQUESTED_MIGRATED_METRIC_ID);
        });
        assert_variant!(
            telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ActiveScanRequested { .. }))
        );
        assert_variant!(
            telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::LogMetricEvents { .. }))
        );
        assert_variant!(telemetry_receiver.try_next(), Ok(Some(event)) => {
            assert_variant!(event, TelemetryEvent::NetworkSelectionDecision {
                network_selection_type: telemetry::NetworkSelectionType::Undirected,
                num_candidates: Ok(2),
                selected_any: true,
            });
        });
    }

    #[fuchsia::test]
    fn find_connection_candidate_for_network_end_to_end() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());
        let network_selector = test_values.network_selector;
        let mut telemetry_receiver = test_values.telemetry_receiver;

        // create identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };
        let credential_1 = Credential::Password("foo_pass".as_bytes().to_vec());

        // insert saved networks
        assert!(exec
            .run_singlethreaded(
                test_values
                    .saved_network_manager
                    .store(test_id_1.clone().into(), credential_1.clone()),
            )
            .unwrap()
            .is_none());

        // get the sme proxy
        let mut iface_manager_inner = exec.run_singlethreaded(test_values.iface_manager.lock());
        let sme_proxy =
            exec.run_singlethreaded(iface_manager_inner.get_sme_proxy_for_scan()).unwrap();
        drop(iface_manager_inner);

        // Kick off network selection
        let network_selection_fut =
            network_selector.find_connection_candidate_for_network(sme_proxy, test_id_1.clone());
        pin_mut!(network_selection_fut);
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back results
        let expected_scan_request = fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![test_id_1.ssid.to_vec()],
            channels: vec![],
        });
        let bss_desc_1 = random_fidl_bss_description!(
            // This network is WPA3, but should still match against the desired WPA2 network
            Wpa3,
            bssid: [0, 0, 0, 0, 0, 0],
            ssid: test_id_1.ssid.clone(),
            rssi_dbm: 10,
            snr_db: 10,
            channel: types::WlanChan::new(1, types::Cbw::Cbw20),
        );
        let scan_result_1 = fidl_sme::ScanResult {
            compatibility: Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa3Personal],
            })),
            timestamp_nanos: 0, // Must be monotonic; set this field adjacent to its usage.
            bss_description: bss_desc_1.clone(),
        };
        let mock_scan_results = vec![
            fidl_sme::ScanResult {
                timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
                ..scan_result_1
            },
            fidl_sme::ScanResult {
                compatibility: Some(Box::new(fidl_sme::Compatibility {
                    mutual_security_protocols: vec![fidl_security::Protocol::Wpa1],
                })),
                timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
                bss_description: random_fidl_bss_description!(
                    Wpa1,
                    bssid: [0, 0, 0, 0, 0, 0],
                    ssid: types::Ssid::try_from("other ssid").unwrap(),
                    rssi_dbm: 0,
                    snr_db: 0,
                    channel: types::WlanChan::new(1, types::Cbw::Cbw20),
                ),
            },
        ];
        validate_sme_scan_request_and_send_results(
            &mut exec,
            &mut test_values.sme_stream,
            &expected_scan_request,
            mock_scan_results,
        );

        // Check that we pick a network
        let results = exec.run_singlethreaded(&mut network_selection_fut);
        assert_eq!(
            results,
            Some(types::ConnectionCandidate {
                network: test_id_1.clone(),
                credential: credential_1.clone(),
                scanned: Some(types::ScannedCandidate {
                    bss_description: bss_desc_1,
                    // A passive scan is never performed in the tested code path, so the
                    // observation mode cannot be known and this field should be `Unknown`.
                    observation: types::ScanObservation::Unknown,
                    has_multiple_bss_candidates: false,
                    security_type_detailed: types::SecurityTypeDetailed::Wpa3Personal,
                }),
            })
        );

        // Verify that NetworkSelectionDecision telemetry event is sent
        assert_variant!(telemetry_receiver.try_next(), Ok(Some(event)) => {
            assert_variant!(event, TelemetryEvent::NetworkSelectionDecision {
                network_selection_type: telemetry::NetworkSelectionType::Directed,
                num_candidates: Ok(1),
                selected_any: true,
            });
        });
    }

    #[fuchsia::test]
    fn find_connection_candidate_for_network_end_to_end_with_failure() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());
        let network_selector = test_values.network_selector;
        let mut telemetry_receiver = test_values.telemetry_receiver;

        // create identifiers
        let test_id_1 = types::NetworkIdentifier {
            ssid: types::Ssid::try_from("foo").unwrap(),
            security_type: types::SecurityType::Wpa3,
        };

        // get the sme proxy
        let mut iface_manager_inner = exec.run_singlethreaded(test_values.iface_manager.lock());
        let sme_proxy =
            exec.run_singlethreaded(iface_manager_inner.get_sme_proxy_for_scan()).unwrap();
        drop(iface_manager_inner);

        // Kick off network selection
        let network_selection_fut =
            network_selector.find_connection_candidate_for_network(sme_proxy, test_id_1);
        pin_mut!(network_selection_fut);
        assert_variant!(exec.run_until_stalled(&mut network_selection_fut), Poll::Pending);

        // Return an error on the scan
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, req: _, control_handle: _
            }))) => {
                // Send failed scan response.
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_error(&mut fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::InternalError,
                    message: "Failed to scan".to_string()
                }).expect("failed to send scan error");
            }
        );

        // Check that nothing is returned
        let results = exec.run_singlethreaded(&mut network_selection_fut);
        assert_eq!(results, None);

        // Verify that the scan defect is sent to telemetry.
        assert_variant!(
            telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ScanDefect(ScanIssue::ScanFailure)))
        );

        // Verify that NetworkSelectionDecision telemetry event is sent
        assert_variant!(telemetry_receiver.try_next(), Ok(Some(event)) => {
            assert_variant!(event, TelemetryEvent::NetworkSelectionDecision {
                network_selection_type: telemetry::NetworkSelectionType::Directed,
                num_candidates: Err(()),
                selected_any: false,
            });
        });
    }

    #[fuchsia::test]
    async fn recorded_metrics_on_scan() {
        let (telemetry_sender, mut telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);

        // create some identifiers
        let test_ssid_1 = types::Ssid::try_from("foo").unwrap();
        let test_id_1 = types::NetworkIdentifier {
            ssid: test_ssid_1.clone(),
            security_type: types::SecurityType::Wpa3,
        };
        let test_ssid_2 = types::Ssid::try_from("bar").unwrap();
        let test_id_2 = types::NetworkIdentifier {
            ssid: test_ssid_2.clone(),
            security_type: types::SecurityType::Wpa,
        };

        let hasher = WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes());
        let mut mock_scan_results = vec![];

        let test_network_info_1 = InternalSavedNetworkData {
            network_id: test_id_1.clone(),
            credential: Credential::Password("foo_pass".as_bytes().to_vec()),
            has_ever_connected: false,
            recent_failures: Vec::new(),
            past_connections: PastConnectionsByBssid::new(),
        };
        let test_bss_1 =
            types::Bss { observation: types::ScanObservation::Passive, ..generate_random_bss() };
        mock_scan_results.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: test_network_info_1.clone(),
            scanned_bss: &test_bss_1,
            multiple_bss_candidates: true,
            hasher: hasher.clone(),
        });

        let test_bss_2 =
            types::Bss { observation: types::ScanObservation::Passive, ..generate_random_bss() };
        mock_scan_results.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: test_network_info_1.clone(),
            scanned_bss: &test_bss_2,
            multiple_bss_candidates: true,
            hasher: hasher.clone(),
        });

        // mark one BSS as found in active scan
        let test_bss_3 =
            types::Bss { observation: types::ScanObservation::Active, ..generate_random_bss() };
        mock_scan_results.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: test_network_info_1.clone(),
            scanned_bss: &test_bss_3,
            multiple_bss_candidates: true,
            hasher: hasher.clone(),
        });

        let test_bss_4 =
            types::Bss { observation: types::ScanObservation::Passive, ..generate_random_bss() };
        mock_scan_results.push(InternalBss {
            security_type_detailed: types::SecurityTypeDetailed::Wpa2PersonalTkipOnly,
            saved_network_info: InternalSavedNetworkData {
                network_id: test_id_2.clone(),
                credential: Credential::Password("bar_pass".as_bytes().to_vec()),
                has_ever_connected: false,
                recent_failures: Vec::new(),
                past_connections: PastConnectionsByBssid::new(),
            },
            scanned_bss: &test_bss_4,
            multiple_bss_candidates: false,
            hasher: hasher.clone(),
        });

        record_metrics_on_scan(mock_scan_results, &telemetry_sender);

        let metric_events = assert_variant!(telemetry_receiver.try_next(), Ok(Some(TelemetryEvent::LogMetricEvents { events, .. })) => events);
        assert_eq!(metric_events.len(), 4);

        // The order of the first two cobalt events is not deterministic
        // Three BSSs present for network 1 in scan results
        assert!(metric_events[..2]
            .iter()
            .find(|event| **event
                == MetricEvent {
                    metric_id: SAVED_NETWORK_IN_SCAN_RESULT_MIGRATED_METRIC_ID,
                    event_codes: vec![
                        SavedNetworkInScanResultMigratedMetricDimensionBssCount::TwoToFour as u32
                    ],
                    payload: MetricEventPayload::Count(1),
                })
            .is_some());

        // One BSS present for network 2 in scan results
        assert!(metric_events[..2]
            .iter()
            .find(|event| **event
                == MetricEvent {
                    metric_id: SAVED_NETWORK_IN_SCAN_RESULT_MIGRATED_METRIC_ID,
                    event_codes: vec![
                        SavedNetworkInScanResultMigratedMetricDimensionBssCount::One as u32
                    ],
                    payload: MetricEventPayload::Count(1),
                })
            .is_some());

        // Total of two saved networks in the scan results
        assert_eq!(
            metric_events[2],
            MetricEvent {
                metric_id: SCAN_RESULTS_RECEIVED_MIGRATED_METRIC_ID,
                event_codes: vec![
                    ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::TwoToFour as u32
                ],
                payload: MetricEventPayload::Count(1),
            }
        );

        // One saved networks that was discovered via active scan
        assert_eq!(
            metric_events[3],
            MetricEvent {
                metric_id: SAVED_NETWORK_IN_SCAN_RESULT_WITH_ACTIVE_SCAN_MIGRATED_METRIC_ID,
                event_codes: vec![ActiveScanSsidsObserved::One as u32],
                payload: MetricEventPayload::Count(1),
            }
        );
    }

    #[fuchsia::test]
    async fn recorded_metrics_on_scan_no_saved_networks() {
        let (telemetry_sender, mut telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);

        let mock_scan_results = vec![];

        record_metrics_on_scan(mock_scan_results, &telemetry_sender);

        let metric_events = assert_variant!(telemetry_receiver.try_next(), Ok(Some(TelemetryEvent::LogMetricEvents { events, .. })) => events);
        assert_eq!(metric_events.len(), 2);

        // No saved networks in scan results
        assert_eq!(
            metric_events[0],
            MetricEvent {
                metric_id: SCAN_RESULTS_RECEIVED_MIGRATED_METRIC_ID,
                event_codes: vec![
                    ScanResultsReceivedMigratedMetricDimensionSavedNetworksCount::Zero as u32
                ],
                payload: MetricEventPayload::Count(1),
            }
        );

        // Also no saved networks that were discovered via active scan
        assert_eq!(
            metric_events[1],
            MetricEvent {
                metric_id: SAVED_NETWORK_IN_SCAN_RESULT_WITH_ACTIVE_SCAN_MIGRATED_METRIC_ID,
                event_codes: vec![ActiveScanSsidsObserved::Zero as u32],
                payload: MetricEventPayload::Count(1),
            }
        );
    }

    fn connect_failure_with_bssid(bssid: types::Bssid) -> ConnectFailure {
        ConnectFailure {
            reason: FailureReason::GeneralFailure,
            time: fasync::Time::INFINITE,
            bssid,
        }
    }

    fn past_connection_with_bssid_uptime(
        bssid: types::Bssid,
        uptime: zx::Duration,
    ) -> PastConnectionData {
        PastConnectionData {
            bssid,
            connection_uptime: uptime,
            disconnect_time: fasync::Time::INFINITE, // disconnect will always be considered recent
            ..random_connection_data()
        }
    }

    fn fake_successful_connect_result() -> fidl_sme::ConnectResult {
        fidl_sme::ConnectResult {
            code: fidl_ieee80211::StatusCode::Success,
            is_credential_rejected: false,
            is_reconnect: false,
        }
    }
}

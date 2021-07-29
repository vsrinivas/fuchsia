// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod convert;
mod disconnect_tracker;
#[cfg(test)]
pub mod test_helper;

pub use disconnect_tracker::DisconnectTracker;

use {
    crate::{device::IfaceMap, inspect, telemetry::convert::*},
    fidl_fuchsia_cobalt::HistogramBucket,
    fidl_fuchsia_wlan_stats as fidl_stats,
    fidl_fuchsia_wlan_stats::MlmeStats::{ApMlmeStats, ClientMlmeStats},
    fuchsia_async as fasync,
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect_contrib::{inspect_log, make_inspect_loggable},
    fuchsia_zircon as zx,
    fuchsia_zircon::DurationNum,
    futures::prelude::*,
    futures::stream::FuturesUnordered,
    log::{error, warn},
    parking_lot::Mutex,
    std::cmp::PartialOrd,
    std::collections::hash_map::Entry,
    std::collections::HashMap,
    std::default::Default,
    std::ops::Sub,
    std::sync::Arc,
    wlan_common::format::{MacFmt as _, SsidFmt as _},
    wlan_metrics_registry as metrics,
    wlan_sme::client::{
        info::{
            ConnectStats, ConnectionMilestone, ConnectionPingInfo, DisconnectInfo,
            DisconnectSource, ScanStats,
        },
        AssociationFailure, ConnectFailure, ConnectResult,
    },
};

// Macro wrapper for logging simple events (occurrence, integer, histogram, string)
// and log a warning when the status is not Ok
macro_rules! log_cobalt_1dot1 {
    ($cobalt_proxy:expr, $method_name:ident, $metric_id:expr, $value:expr, $event_codes:expr $(,)?) => {{
        let status = $cobalt_proxy.$method_name($metric_id, $value, $event_codes).await;
        match status {
            Ok(fidl_fuchsia_metrics::Status::Ok) => (),
            Ok(s) => warn!("Failed logging metric: {}, status: {:?}", $metric_id, s),
            Err(e) => warn!("Failed logging metric: {}, error: {}", $metric_id, e),
        }
    }};
}

type StatsRef = Arc<Mutex<fidl_stats::IfaceStats>>;

/// How often to request RSSI stats and dispatcher packet counts from MLME.
const REPORT_PERIOD_MINUTES: i64 = 1;

struct ReconnectInfo {
    gap_time: zx::Duration,
    same_ssid: bool,
}

// Export MLME stats to Cobalt every REPORT_PERIOD_MINUTES.
pub async fn report_telemetry_periodically(
    ifaces_map: Arc<IfaceMap>,
    mut sender: CobaltSender,
    inspect_tree: Arc<inspect::WlanstackTree>,
) {
    // TODO(fxbug.dev/28800): Make this module resilient to Wlanstack2 downtime.

    let mut last_reported_stats: HashMap<u16, StatsRef> = HashMap::new();
    let mut interval_stream = fasync::Interval::new(REPORT_PERIOD_MINUTES.minutes());
    while let Some(_) = interval_stream.next().await {
        let mut futures = FuturesUnordered::new();
        for (id, iface) in ifaces_map.get_snapshot().iter() {
            let id = *id;
            let iface = Arc::clone(iface);
            let fut = iface.stats_sched.get_stats().map(move |r| (id, iface, r));
            futures.push(fut);
        }

        while let Some((id, _iface, stats_result)) = futures.next().await {
            match stats_result {
                Ok(current_stats) => match last_reported_stats.entry(id) {
                    Entry::Vacant(entry) => {
                        entry.insert(current_stats);
                    }
                    Entry::Occupied(mut value) => {
                        let last_stats = value.get_mut();
                        log_counters_to_inspect(
                            id,
                            &last_stats.lock(),
                            &current_stats.lock(),
                            inspect_tree.clone(),
                        );
                        report_stats(&last_stats.lock(), &current_stats.lock(), &mut sender);
                        let _dropped = std::mem::replace(value.get_mut(), current_stats);
                    }
                },
                Err(e) => {
                    last_reported_stats.remove(&id);
                    error!("Failed to get the stats for iface '{}': {}", id, e);
                }
            };
        }
    }
}

fn log_counters_to_inspect(
    iface_id: u16,
    last: &fidl_stats::IfaceStats,
    current: &fidl_stats::IfaceStats,
    inspect_tree: Arc<inspect::WlanstackTree>,
) {
    let (last, current) = match (&last.mlme_stats, &current.mlme_stats) {
        (Some(ref last), Some(ref current)) => match (last.as_ref(), current.as_ref()) {
            (ClientMlmeStats(last), ClientMlmeStats(current)) => (last, current),
            _ => return,
        },
        _ => return,
    };

    let overflow = current.tx_frame.in_.count < last.tx_frame.in_.count
        || current.tx_frame.drop.count < last.tx_frame.drop.count
        || current.rx_frame.in_.count < last.rx_frame.in_.count
        || current.rx_frame.drop.count < last.rx_frame.drop.count;
    if overflow {
        inspect_log!(inspect_tree.client_stats.counters.lock(), {
            iface: iface_id,
            msg: "no diff (counters have likely been reset)",
        })
    } else {
        inspect_log!(inspect_tree.client_stats.counters.lock(), {
            iface: iface_id,
            tx_total: current.tx_frame.in_.count - last.tx_frame.in_.count,
            tx_drop: current.tx_frame.drop.count - last.tx_frame.drop.count,
            rx_total: current.rx_frame.in_.count - last.rx_frame.in_.count,
            rx_drop: current.rx_frame.drop.count - last.rx_frame.drop.count,
        });
    }
}

fn report_stats(
    last_stats: &fidl_stats::IfaceStats,
    current_stats: &fidl_stats::IfaceStats,
    sender: &mut CobaltSender,
) {
    report_mlme_stats(&last_stats.mlme_stats, &current_stats.mlme_stats, sender);
}

fn report_mlme_stats(
    last: &Option<Box<fidl_stats::MlmeStats>>,
    current: &Option<Box<fidl_stats::MlmeStats>>,
    sender: &mut CobaltSender,
) {
    if let (Some(ref last), Some(ref current)) = (last, current) {
        match (last.as_ref(), current.as_ref()) {
            (ClientMlmeStats(last), ClientMlmeStats(current)) => {
                report_client_mlme_stats(&last, &current, sender)
            }
            (ApMlmeStats(_), ApMlmeStats(_)) => {}
            _ => error!("Current MLME stats type is different from the last MLME stats type"),
        };
    }
}

fn report_client_mlme_stats(
    last_stats: &fidl_stats::ClientMlmeStats,
    current_stats: &fidl_stats::ClientMlmeStats,
    sender: &mut CobaltSender,
) {
    report_rssi_stats(
        metrics::CLIENT_ASSOC_RSSI_METRIC_ID,
        &last_stats.assoc_data_rssi,
        &current_stats.assoc_data_rssi,
        sender,
    );
    report_rssi_stats(
        metrics::CLIENT_BEACON_RSSI_METRIC_ID,
        &last_stats.beacon_rssi,
        &current_stats.beacon_rssi,
        sender,
    );
}

fn report_rssi_stats(
    rssi_metric_id: u32,
    last_stats: &fidl_stats::RssiStats,
    current_stats: &fidl_stats::RssiStats,
    sender: &mut CobaltSender,
) {
    // In the internal stats histogram, hist[x] represents the number of frames
    // with RSSI -x. For the Cobalt representation, buckets from -128 to 0 are
    // used. When data is sent to Cobalt, the concept of index is utilized.
    //
    // Shortly, for Cobalt:
    // Bucket -128 -> index   0
    // Bucket -127 -> index   1
    // ...
    // Bucket    0 -> index 128
    //
    // The for loop below converts the stats internal representation to the
    // Cobalt representation and prepares the histogram that will be sent.

    let mut histogram = Vec::new();
    for bin in 0..current_stats.hist.len() {
        let diff = get_diff(last_stats.hist[bin], current_stats.hist[bin]);
        if diff > 0 {
            let entry = HistogramBucket {
                index: (fidl_stats::RSSI_BINS - (bin as u8) - 1).into(),
                count: diff.into(),
            };
            histogram.push(entry);
        }
    }

    if !histogram.is_empty() {
        sender.log_int_histogram(rssi_metric_id, (), histogram);
    }
}

fn get_diff<T>(last_stat: T, current_stat: T) -> T
where
    T: Sub<Output = T> + PartialOrd + Default,
{
    if current_stat >= last_stat {
        current_stat - last_stat
    } else {
        Default::default()
    }
}

pub fn log_scan_stats(
    sender: &mut CobaltSender,
    inspect_tree: Arc<inspect::WlanstackTree>,
    scan_stats: &ScanStats,
    is_join_scan: bool,
) {
    inspect_log!(inspect_tree.client_stats.scan.lock().get_mut(), {
        start_at: scan_stats.scan_start_at.into_nanos(),
        end_at: scan_stats.scan_end_at.into_nanos(),
        result: format!("{:?}", scan_stats.result_code),
        start_while_connected: scan_stats.scan_start_while_connected,
    });

    let (scan_result_dim, error_code_dim) = convert_scan_result(&scan_stats.result_code);
    let scan_type_dim = convert_scan_type(scan_stats.scan_type);
    let is_join_scan_dim = convert_bool_dim(is_join_scan);
    let client_state_dim = match scan_stats.scan_start_while_connected {
        true => metrics::ScanResultMetricDimensionClientState::Connected,
        false => metrics::ScanResultMetricDimensionClientState::Idle,
    };

    sender.log_event_count(
        metrics::SCAN_RESULT_METRIC_ID,
        [
            scan_result_dim as u32,
            scan_type_dim as u32,
            is_join_scan_dim as u32,
            client_state_dim as u32,
        ],
        // Elapsed time during which the count of event has been gathered. We log 0 since
        // we don't keep track of this.
        0,
        // Log one count of scan result.
        1,
    );

    if let Some(error_code_dim) = error_code_dim {
        inspect_log!(inspect_tree.client_stats.scan_failures.lock().get_mut(), {});
        sender.log_event_count(
            metrics::SCAN_FAILURE_METRIC_ID,
            [
                error_code_dim as u32,
                scan_type_dim as u32,
                is_join_scan_dim as u32,
                client_state_dim as u32,
            ],
            0,
            1,
        )
    }

    let scan_time = scan_stats.scan_time().into_micros();
    sender.log_elapsed_time(metrics::SCAN_TIME_METRIC_ID, (), scan_time);
    sender.log_elapsed_time(
        metrics::SCAN_TIME_PER_RESULT_METRIC_ID,
        scan_result_dim as u32,
        scan_time,
    );
    sender.log_elapsed_time(
        metrics::SCAN_TIME_PER_SCAN_TYPE_METRIC_ID,
        scan_type_dim as u32,
        scan_time,
    );
    sender.log_elapsed_time(
        metrics::SCAN_TIME_PER_JOIN_OR_DISCOVERY_METRIC_ID,
        is_join_scan_dim as u32,
        scan_time,
    );
    sender.log_elapsed_time(
        metrics::SCAN_TIME_PER_CLIENT_STATE_METRIC_ID,
        client_state_dim as u32,
        scan_time,
    );
}

pub async fn log_connect_stats(
    sender: &mut CobaltSender,
    cobalt_1dot1_proxy: &mut fidl_fuchsia_metrics::MetricEventLoggerProxy,
    inspect_tree: Arc<inspect::WlanstackTree>,
    connect_stats: &ConnectStats,
) {
    log_connect_attempts_stats(sender, connect_stats);
    log_connect_result_stats(sender, cobalt_1dot1_proxy, connect_stats).await;
    log_time_to_connect_stats(sender, connect_stats);
    let reconnect_info = log_connection_gap_time_stats(sender, connect_stats);

    if let ConnectResult::Success = connect_stats.result {
        inspect_log!(inspect_tree.client_stats.connect.lock().get_mut(), {
            attempts: connect_stats.attempts,
            reconnect_info?: reconnect_info.as_ref().map(|info| make_inspect_loggable!({
                gap_time: info.gap_time.into_nanos(),
                same_ssid: info.same_ssid,
            })),
        });
    }
}

fn log_connect_attempts_stats(sender: &mut CobaltSender, connect_stats: &ConnectStats) {
    // Only log attempts for successful connect. If connect is not successful, or if the expected
    // fields for successful connect attempts are not there, early return.
    match connect_stats.result {
        ConnectResult::Success => (),
        _ => return,
    }
    let (bss, is_multi_bss) = match &connect_stats.candidate_network {
        Some(network) => (&network.bss, network.multiple_bss_candidates),
        None => return,
    };

    use metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionAttempts::*;
    let attempts_dim = match connect_stats.attempts {
        0 => {
            warn!("unexpected 0 attempts in connect stats");
            return;
        }
        1 => One,
        2 => Two,
        3 => Three,
        4 => Four,
        5 => Five,
        _ => MoreThanFive,
    };
    let is_multi_bss_dim = convert_bool_dim(is_multi_bss);
    let protection_dim = convert_protection(&bss.protection());
    let channel_band_dim = convert_channel_band(bss.channel.primary);

    sender.log_event_count(
        metrics::CONNECTION_ATTEMPTS_METRIC_ID,
        (), // no dimension
        0,
        connect_stats.attempts as i64,
    );
    sender.log_event_count(
        metrics::CONNECTION_SUCCESS_WITH_ATTEMPTS_BREAKDOWN_METRIC_ID,
        [
            attempts_dim as u32,
            is_multi_bss_dim as u32,
            protection_dim as u32,
            channel_band_dim as u32,
        ],
        0,
        1,
    );
}

async fn log_connect_result_stats(
    sender: &mut CobaltSender,
    cobalt_1dot1_proxy: &mut fidl_fuchsia_metrics::MetricEventLoggerProxy,
    connect_stats: &ConnectStats,
) {
    let oui = connect_stats
        .candidate_network
        .as_ref()
        .map(|network| network.bss.bssid.to_oui_uppercase(""));
    let result_dim = convert_connection_result(&connect_stats.result);
    sender.with_component().log_event_count::<_, String, _>(
        metrics::CONNECTION_RESULT_METRIC_ID,
        result_dim as u32,
        oui.clone(),
        0,
        1,
    );

    if let ConnectResult::Failed(failure) = &connect_stats.result {
        let fail_at_dim = convert_to_fail_at_dim(failure);
        let timeout_dim = convert_bool_dim(failure.is_timeout());
        let credential_rejected_dim = convert_bool_dim(failure.likely_due_to_credential_rejected());
        sender.with_component().log_event_count::<_, String, _>(
            metrics::CONNECTION_FAILURE_METRIC_ID,
            [fail_at_dim as u32, timeout_dim as u32, credential_rejected_dim as u32],
            oui.clone(),
            0,
            1,
        );
        log_cobalt_1dot1!(
            cobalt_1dot1_proxy,
            log_string,
            metrics::CONNECTION_FAILURE_MIGRATED_METRIC_ID,
            oui.as_deref().unwrap_or(""),
            &[fail_at_dim as u32, timeout_dim as u32, credential_rejected_dim as u32],
        );

        if let ConnectFailure::SelectNetworkFailure(select_network_failure) = failure {
            let error_reason_dim = convert_select_network_failure(&select_network_failure);
            sender.with_component().log_event_count::<_, String, _>(
                metrics::NETWORK_SELECTION_FAILURE_METRIC_ID,
                error_reason_dim as u32,
                oui,
                0,
                1,
            );
        }
    }

    // For the remaining metrics, we expect the candidate network to have been found
    let (bss, is_multi_bss) = match &connect_stats.candidate_network {
        Some(network) => (&network.bss, network.multiple_bss_candidates),
        None => return,
    };
    let oui = bss.bssid.to_oui_uppercase("");

    let is_multi_bss_dim = convert_bool_dim(is_multi_bss);
    let protection_dim = convert_protection(&bss.protection());
    let channel_band_dim = convert_channel_band(bss.channel.primary);
    let rssi_dim = convert_rssi(bss.rssi_dbm);
    let snr_dim = convert_snr(bss.snr_db);
    sender.with_component().log_event_count(
        metrics::CONNECTION_RESULT_POST_NETWORK_SELECTION_METRIC_ID,
        [
            result_dim as u32,
            is_multi_bss_dim as u32,
            protection_dim as u32,
            channel_band_dim as u32,
        ],
        oui.clone(),
        0,
        1,
    );
    sender.with_component().log_event_count(
        metrics::CONNECTION_RESULT_PER_RSSI_METRIC_ID,
        [result_dim as u32, rssi_dim as u32],
        oui.clone(),
        0,
        1,
    );
    sender.with_component().log_event_count(
        metrics::CONNECTION_RESULT_PER_SNR_METRIC_ID,
        [result_dim as u32, snr_dim as u32],
        oui.clone(),
        0,
        1,
    );
    log_cobalt_1dot1!(
        cobalt_1dot1_proxy,
        log_string,
        metrics::CONNECTION_RESULT_MIGRATED_METRIC_ID,
        &oui,
        &[
            result_dim as u32,
            is_multi_bss_dim as u32,
            protection_dim as u32,
            channel_band_dim as u32,
            snr_dim as u32,
        ],
    );

    if let ConnectResult::Failed(failure) = &connect_stats.result {
        let credential_rejected_bool = failure.likely_due_to_credential_rejected();

        match failure {
            ConnectFailure::AuthenticationFailure(code) => {
                let credential_rejected_dim = convert_bool_dim(credential_rejected_bool);
                let error_code_dim = convert_auth_error_code(*code);
                sender.with_component().log_event_count(
                    metrics::AUTHENTICATION_FAILURE_METRIC_ID,
                    [
                        error_code_dim as u32,
                        is_multi_bss_dim as u32,
                        channel_band_dim as u32,
                        protection_dim as u32,
                        credential_rejected_dim as u32,
                    ],
                    oui.clone(),
                    0,
                    1,
                );

                sender.with_component().log_event_count(
                    metrics::AUTHENTICATION_FAILURE_PER_RSSI_METRIC_ID,
                    [
                        error_code_dim as u32,
                        rssi_dim as u32,
                        channel_band_dim as u32,
                        credential_rejected_dim as u32,
                    ],
                    oui,
                    0,
                    1,
                );
            }
            ConnectFailure::AssociationFailure(AssociationFailure { code, .. }) => {
                let error_code_dim = convert_assoc_error_code(*code);
                let credential_rejected_dim = convert_bool_dim(credential_rejected_bool);
                sender.with_component().log_event_count(
                    metrics::ASSOCIATION_FAILURE_METRIC_ID,
                    [error_code_dim as u32, protection_dim as u32, credential_rejected_dim as u32],
                    oui.clone(),
                    0,
                    1,
                );
                sender.with_component().log_event_count(
                    metrics::ASSOCIATION_FAILURE_PER_RSSI_METRIC_ID,
                    [
                        error_code_dim as u32,
                        rssi_dim as u32,
                        channel_band_dim as u32,
                        credential_rejected_dim as u32,
                    ],
                    oui,
                    0,
                    1,
                );
            }
            ConnectFailure::EstablishRsnaFailure(..) => {
                let credential_rejected_dim = convert_bool_dim(credential_rejected_bool);
                sender.with_component().log_event_count(
                    metrics::ESTABLISH_RSNA_FAILURE_METRIC_ID,
                    [protection_dim as u32, credential_rejected_dim as u32],
                    oui,
                    0,
                    1,
                );
            }
            // Scan failure is already logged as part of scan stats.
            // Select network failure is already logged above.
            _ => (),
        }
    }
}

fn log_time_to_connect_stats(sender: &mut CobaltSender, connect_stats: &ConnectStats) {
    let connect_result_dim = convert_connection_result(&connect_stats.result);
    let rssi_dim =
        connect_stats.candidate_network.as_ref().map(|network| convert_rssi(network.bss.rssi_dbm));

    let connect_time = connect_stats.connect_time().into_micros();
    sender.log_elapsed_time(metrics::CONNECTION_SETUP_TIME_METRIC_ID, (), connect_time);
    sender.log_elapsed_time(
        metrics::CONNECTION_SETUP_TIME_PER_RESULT_METRIC_ID,
        connect_result_dim as u32,
        connect_time,
    );

    if let Some(auth_time) = connect_stats.auth_time() {
        sender.log_elapsed_time(
            metrics::AUTHENTICATION_TIME_METRIC_ID,
            (),
            auth_time.into_micros(),
        );
        if let Some(rssi_dim) = rssi_dim {
            sender.log_elapsed_time(
                metrics::AUTHENTICATION_TIME_PER_RSSI_METRIC_ID,
                rssi_dim as u32,
                auth_time.into_micros(),
            )
        }
    }

    if let Some(assoc_time) = connect_stats.assoc_time() {
        sender.log_elapsed_time(metrics::ASSOCIATION_TIME_METRIC_ID, (), assoc_time.into_micros());
        if let Some(rssi_dim) = rssi_dim {
            sender.log_elapsed_time(
                metrics::ASSOCIATION_TIME_PER_RSSI_METRIC_ID,
                rssi_dim as u32,
                assoc_time.into_micros(),
            )
        }
    }

    if let Some(rsna_time) = connect_stats.rsna_time() {
        sender.log_elapsed_time(
            metrics::ESTABLISH_RSNA_TIME_METRIC_ID,
            (),
            rsna_time.into_micros(),
        );
        if let Some(rssi_dim) = rssi_dim {
            sender.log_elapsed_time(
                metrics::ESTABLISH_RSNA_TIME_PER_RSSI_METRIC_ID,
                rssi_dim as u32,
                rsna_time.into_micros(),
            )
        }
    }
}

/// If there was a reconnect, log connection gap time stats to Cobalt. Return the duration
/// and whether the reconnect was to the same SSID as last connected.
fn log_connection_gap_time_stats(
    sender: &mut CobaltSender,
    connect_stats: &ConnectStats,
) -> Option<ReconnectInfo> {
    if connect_stats.result != ConnectResult::Success {
        return None;
    }

    let ssid = match &connect_stats.candidate_network {
        Some(network) => network.bss.ssid(),
        None => {
            warn!("No candidate_network in successful connect stats");
            return None;
        }
    };

    let mut reconnect_info = None;
    if let Some(previous_disconnect_info) = &connect_stats.previous_disconnect_info {
        let duration = connect_stats.connect_end_at - previous_disconnect_info.disconnect_at;
        let ssids_dim = if ssid == &previous_disconnect_info.ssid {
            metrics::ConnectionGapTimeBreakdownMetricDimensionSsids::SameSsid
        } else {
            metrics::ConnectionGapTimeBreakdownMetricDimensionSsids::DifferentSsids
        };
        let previous_disconnect_cause_dim =
            convert_disconnect_source(&previous_disconnect_info.disconnect_source);

        sender.log_elapsed_time(metrics::CONNECTION_GAP_TIME_METRIC_ID, (), duration.into_micros());
        sender.log_elapsed_time(
            metrics::CONNECTION_GAP_TIME_BREAKDOWN_METRIC_ID,
            [ssids_dim as u32, previous_disconnect_cause_dim as u32],
            duration.into_micros(),
        );
        reconnect_info.replace(ReconnectInfo {
            gap_time: duration,
            same_ssid: ssid == &previous_disconnect_info.ssid,
        });
    }
    reconnect_info
}

pub fn log_connection_ping(sender: &mut CobaltSender, info: &ConnectionPingInfo) {
    for milestone in ConnectionMilestone::all().iter() {
        if info.connected_duration() >= milestone.min_duration() {
            let dur_dim = convert_connected_milestone(milestone);
            sender.log_event(metrics::CONNECTION_UPTIME_PING_METRIC_ID, dur_dim as u32);
        }
    }

    if info.reaches_new_milestone() {
        let dur_dim = convert_connected_milestone(&info.get_milestone());
        sender.log_event_count(
            metrics::CONNECTION_COUNT_BY_DURATION_METRIC_ID,
            dur_dim as u32,
            0,
            1,
        );
    }
}

pub async fn log_disconnect(
    sender: &mut CobaltSender,
    cobalt_1dot1_proxy: &mut fidl_fuchsia_metrics::MetricEventLoggerProxy,
    inspect_tree: Arc<inspect::WlanstackTree>,
    info: &DisconnectInfo,
) {
    inspect_log!(inspect_tree.client_stats.disconnect.lock().get_mut(), {
        connected_duration: info.connected_duration.into_nanos(),
        last_rssi: info.last_rssi,
        last_snr: info.last_snr,
        bssid: info.bssid.to_mac_string(),
        bssid_hash: inspect_tree.hasher.hash_mac_addr(&info.bssid),
        ssid: info.ssid.to_ssid_string(),
        ssid_hash: inspect_tree.hasher.hash_ssid(&info.ssid[..]),
        wsc?: match &info.wsc {
            None => None,
            Some(wsc) => Some(make_inspect_loggable!(
                device_name: String::from_utf8_lossy(&wsc.device_name[..]).to_string(),
                manufacturer: String::from_utf8_lossy(&wsc.manufacturer[..]).to_string(),
                model_name: String::from_utf8_lossy(&wsc.model_name[..]).to_string(),
                model_number: String::from_utf8_lossy(&wsc.model_number[..]).to_string(),
            )),
        },
        protection: format!("{:?}", info.protection),
        channel: {
            primary: info.channel.primary,
            cbw: format!("{:?}", info.channel.cbw.to_fidl().0),
            secondary80: info.channel.cbw.to_fidl().1,
        },
        disconnect_source: info.disconnect_source.inspect_string(),
        time_since_channel_switch?: info.time_since_channel_switch.map(|d| d.into_nanos()),
        // Both reason_code and locally_initiated are still consumed
        // by an upper layer and should not be removed.
        reason_code: info.disconnect_source.reason_code(),
        locally_initiated: info.disconnect_source.locally_initiated(),
    });

    if let DisconnectSource::Mlme(_) = info.disconnect_source {
        use metrics::LostConnectionCountMetricDimensionConnectedTime::*;

        let duration_dim = match &info.connected_duration {
            x if x < &1.minutes() => LessThanOneMinute,
            x if x < &10.minutes() => LessThanTenMinutes,
            x if x < &30.minutes() => LessThanThirtyMinutes,
            x if x < &1.hour() => LessThanOneHour,
            x if x < &3.hours() => LessThanThreeHours,
            x if x < &6.hours() => LessThanSixHours,
            _ => AtLeastSixHours,
        };
        let rssi_dim = convert_rssi(info.last_rssi);

        sender.with_component().log_event_count(
            metrics::LOST_CONNECTION_COUNT_METRIC_ID,
            [duration_dim as u32, rssi_dim as u32],
            info.bssid.to_oui_uppercase(""),
            0,
            1,
        );
    }

    use metrics::DisconnectCountBreakdownMetricDimensionConnectedTime::*;
    use metrics::DisconnectCountBreakdownMetricDimensionDisconnectSource::*;
    use metrics::DisconnectCountBreakdownMetricDimensionRecentChannelSwitch::*;

    let connected_time_dim = match &info.connected_duration {
        x if x < &1.minutes() => LessThanOneMinute,
        x if x < &10.minutes() => LessThanTenMinutes,
        x if x < &30.minutes() => LessThanThirtyMinutes,
        x if x < &1.hour() => LessThanOneHour,
        x if x < &3.hours() => LessThanThreeHours,
        x if x < &6.hours() => LessThanSixHours,
        _ => AtLeastSixHours,
    };
    // TODO(fxbug.dev/71138): Log disconnect MLME event name and reason
    // code to Cobalt too.
    let disconnect_source_dim = match &info.disconnect_source {
        DisconnectSource::User(_) => User,
        DisconnectSource::Mlme(_) => Mlme,
        DisconnectSource::Ap(_) => Ap,
    };
    let snr_dim = convert_snr(info.last_snr);
    let recent_channel_switch_dim = match info.time_since_channel_switch.map(|d| d < 1.minutes()) {
        Some(true) => Yes,
        _ => No,
    };
    let channel_band_dim = convert_channel_band(info.channel.primary);

    sender.log_event_count(metrics::DISCONNECT_COUNT_METRIC_ID, (), 0, 1);
    sender.with_component().log_event_count(
        metrics::DISCONNECT_COUNT_BREAKDOWN_METRIC_ID,
        [
            connected_time_dim as u32,
            disconnect_source_dim as u32,
            snr_dim as u32,
            recent_channel_switch_dim as u32,
            channel_band_dim as u32,
        ],
        info.bssid.to_oui_uppercase(""),
        0,
        1,
    );
    log_cobalt_1dot1!(
        cobalt_1dot1_proxy,
        log_string,
        metrics::DISCONNECT_COUNT_BREAKDOWN_MIGRATED_METRIC_ID,
        &info.bssid.to_oui_uppercase(""),
        &[
            connected_time_dim as u32,
            disconnect_source_dim as u32,
            snr_dim as u32,
            recent_channel_switch_dim as u32,
            channel_band_dim as u32,
        ],
    );
    log_cobalt_1dot1!(
        cobalt_1dot1_proxy,
        log_occurrence,
        metrics::DISCONNECT_REASON_METRIC_ID,
        1,
        &[info.disconnect_source.unflattened_reason_code() as u32, disconnect_source_dim as u32,],
    );
}

pub async fn log_disconnect_reason_avg_population(
    cobalt_1dot1_proxy: &mut fidl_fuchsia_metrics::MetricEventLoggerProxy,
    info: &DisconnectInfo,
) {
    use metrics::ConnectivityWlanMetricDimensionDisconnectSource::*;
    let disconnect_source_dim = match &info.disconnect_source {
        DisconnectSource::User(_) => User,
        DisconnectSource::Mlme(_) => Mlme,
        DisconnectSource::Ap(_) => Ap,
    };
    log_cobalt_1dot1!(
        cobalt_1dot1_proxy,
        log_occurrence,
        metrics::DISCONNECT_REASON_AVERAGE_POPULATION_METRIC_ID,
        1,
        &[info.disconnect_source.unflattened_reason_code() as u32, disconnect_source_dim as u32,],
    );
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            device::{self, IfaceDevice},
            mlme_query_proxy::MlmeQueryProxy,
            stats_scheduler::{self, StatsRequest},
            telemetry::test_helper::{fake_cobalt_sender, fake_disconnect_info, CobaltExt},
            test_helper::fake_inspect_tree,
        },
        fidl::endpoints::{create_proxy, create_proxy_and_stream},
        fidl_fuchsia_cobalt::{CobaltEvent, EventPayload},
        fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
        fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeMarker},
        fidl_fuchsia_wlan_sme as fidl_sme,
        fidl_fuchsia_wlan_stats::{Counter, DispatcherStats, IfaceStats, PacketCounter},
        fuchsia_inspect::{assert_data_tree, testing::AnyProperty},
        fuchsia_zircon as zx,
        futures::channel::mpsc,
        maplit::hashset,
        pin_utils::pin_mut,
        std::collections::HashSet,
        std::task::Poll,
        wlan_common::{
            assert_variant,
            bss::Protection as BssProtection,
            channel::{Cbw, Channel},
            fake_bss,
            ie::fake_ies::fake_probe_resp_wsc_ie,
        },
        wlan_sme::client::{
            info::{
                CandidateNetwork, ConnectStats, DisconnectCause, DisconnectInfo,
                DisconnectMlmeEventName, DisconnectSource, PreviousDisconnectInfo,
                SupplicantProgress,
            },
            ConnectFailure, ConnectResult, EstablishRsnaFailure, EstablishRsnaFailureReason,
            SelectNetworkFailure,
        },
    };

    const IFACE_ID: u16 = 1;
    const DURATION_SINCE_LAST_DISCONNECT: zx::Duration = zx::Duration::from_seconds(10);

    #[test]
    fn test_report_telemetry_periodically() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        let (ifaces_map, stats_requests) = fake_iface_map();
        let (cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let telemetry_fut = report_telemetry_periodically(
            Arc::new(ifaces_map),
            cobalt_sender,
            inspect_tree.clone(),
        );
        pin_mut!(telemetry_fut);

        // Schedule the first stats request
        let _ = exec.run_until_stalled(&mut telemetry_fut);
        assert!(exec.wake_next_timer().is_some());
        let _ = exec.run_until_stalled(&mut telemetry_fut);

        // Provide stats response
        let mut nth_req = 0;
        let mut stats_server = stats_requests.for_each(move |req| {
            nth_req += 1;
            future::ready(req.reply(fake_iface_stats(nth_req)))
        });
        let _ = exec.run_until_stalled(&mut stats_server);

        // TODO(fxbug.dev/29730): For some reason, telemetry skips logging the first stats response
        // Schedule the next stats request
        let _ = exec.run_until_stalled(&mut telemetry_fut);
        assert!(exec.wake_next_timer().is_some());
        let _ = exec.run_until_stalled(&mut telemetry_fut);

        // Provide stats response
        let _ = exec.run_until_stalled(&mut stats_server);

        // Verify that stats are sent to Cobalt
        let _ = exec.run_until_stalled(&mut telemetry_fut);
        let mut expected_metrics = vec![
            CobaltEvent {
                metric_id: metrics::CLIENT_ASSOC_RSSI_METRIC_ID,
                event_codes: vec![],
                component: None,
                payload: EventPayload::IntHistogram(vec![HistogramBucket { index: 128, count: 1 }]),
            },
            CobaltEvent {
                metric_id: metrics::CLIENT_BEACON_RSSI_METRIC_ID,
                event_codes: vec![],
                component: None,
                payload: EventPayload::IntHistogram(vec![HistogramBucket { index: 128, count: 1 }]),
            },
        ];
        while let Ok(Some(event)) = cobalt_receiver.try_next() {
            let index = expected_metrics.iter().position(|e| *e == event);
            assert!(index.is_some(), "unexpected event: {:?}", event);
            expected_metrics.remove(index.unwrap());
        }
        assert!(expected_metrics.is_empty(), "some metrics not logged: {:?}", expected_metrics);
    }

    #[test]
    fn test_log_scan_stats() {
        let (mut cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let now = now();
        let scan_stats = ScanStats {
            scan_start_at: now - 3.seconds(),
            scan_end_at: now,
            scan_type: fidl_mlme::ScanTypes::Active,
            scan_start_while_connected: true,
            result_code: fidl_mlme::ScanResultCode::Success,
            bss_count: 5,
        };
        log_scan_stats(&mut cobalt_sender, inspect_tree.clone(), &scan_stats, true);

        let mut expected_metrics = hashset! {
            metrics::SCAN_RESULT_METRIC_ID,
            metrics::SCAN_TIME_METRIC_ID,
            metrics::SCAN_TIME_PER_RESULT_METRIC_ID,
            metrics::SCAN_TIME_PER_SCAN_TYPE_METRIC_ID,
            metrics::SCAN_TIME_PER_JOIN_OR_DISCOVERY_METRIC_ID,
            metrics::SCAN_TIME_PER_CLIENT_STATE_METRIC_ID,
        };
        while let Ok(Some(event)) = cobalt_receiver.try_next() {
            assert!(expected_metrics.contains(&event.metric_id), "unexpected event: {:?}", event);
            expected_metrics.remove(&event.metric_id);
        }
        assert!(expected_metrics.is_empty(), "some metrics not logged: {:?}", expected_metrics);
    }

    #[test]
    fn test_log_connect_stats_success() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();
        let connect_stats = fake_connect_stats();
        let fut = log_connect_stats(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &connect_stats,
        );
        pin_mut!(fut);
        let cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        let mut expected_metrics = hashset! {
            metrics::CONNECTION_ATTEMPTS_METRIC_ID,
            metrics::CONNECTION_SUCCESS_WITH_ATTEMPTS_BREAKDOWN_METRIC_ID,
            metrics::CONNECTION_RESULT_METRIC_ID,
            metrics::CONNECTION_RESULT_POST_NETWORK_SELECTION_METRIC_ID,
            metrics::CONNECTION_RESULT_PER_RSSI_METRIC_ID,
            metrics::CONNECTION_RESULT_PER_SNR_METRIC_ID,
            metrics::CONNECTION_SETUP_TIME_METRIC_ID,
            metrics::CONNECTION_SETUP_TIME_PER_RESULT_METRIC_ID,
            metrics::AUTHENTICATION_TIME_METRIC_ID,
            metrics::AUTHENTICATION_TIME_PER_RSSI_METRIC_ID,
            metrics::ASSOCIATION_TIME_METRIC_ID,
            metrics::ASSOCIATION_TIME_PER_RSSI_METRIC_ID,
            metrics::ESTABLISH_RSNA_TIME_METRIC_ID,
            metrics::ESTABLISH_RSNA_TIME_PER_RSSI_METRIC_ID,
            metrics::CONNECTION_GAP_TIME_METRIC_ID,
            metrics::CONNECTION_GAP_TIME_BREAKDOWN_METRIC_ID,
        };
        while let Ok(Some(event)) = cobalt_receiver.try_next() {
            assert!(expected_metrics.contains(&event.metric_id), "unexpected event: {:?}", event);
            expected_metrics.remove(&event.metric_id);
        }
        assert!(expected_metrics.is_empty(), "some metrics not logged: {:?}", expected_metrics);

        let expected_cobalt_1dot1_metrics = hashset! {
            metrics::CONNECTION_RESULT_MIGRATED_METRIC_ID,
        };
        assert_eq!(
            cobalt_1dot1_events.into_iter().map(|e| e.0).collect::<HashSet<_>>(),
            expected_cobalt_1dot1_metrics
        );
    }

    #[test]
    fn test_log_connect_stats_select_network_failure() {
        let connect_stats = ConnectStats {
            result: SelectNetworkFailure::IncompatibleConnectRequest.into(),
            ..fake_connect_stats()
        };
        let expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_METRIC_ID,
            metrics::CONNECTION_FAILURE_METRIC_ID,
            metrics::NETWORK_SELECTION_FAILURE_METRIC_ID,
        };
        let cobalt_1dot1_expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_MIGRATED_METRIC_ID,
            metrics::CONNECTION_FAILURE_MIGRATED_METRIC_ID,
        };
        test_metric_subset(
            &connect_stats,
            expected_metrics_subset,
            cobalt_1dot1_expected_metrics_subset,
            hashset! {},
        );
    }

    #[test]
    fn test_log_connect_stats_auth_failure() {
        let connect_stats = ConnectStats {
            result: ConnectFailure::AuthenticationFailure(
                fidl_mlme::AuthenticateResultCode::Refused,
            )
            .into(),
            ..fake_connect_stats()
        };
        let expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_METRIC_ID,
            metrics::CONNECTION_FAILURE_METRIC_ID,
            metrics::AUTHENTICATION_FAILURE_METRIC_ID,
        };
        let cobalt_1dot1_expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_MIGRATED_METRIC_ID,
            metrics::CONNECTION_FAILURE_MIGRATED_METRIC_ID,
        };
        test_metric_subset(
            &connect_stats,
            expected_metrics_subset,
            cobalt_1dot1_expected_metrics_subset,
            hashset! {},
        );
    }

    #[test]
    fn test_log_connect_stats_assoc_failure() {
        let connect_stats = ConnectStats {
            result: ConnectFailure::AssociationFailure(AssociationFailure {
                bss_protection: BssProtection::Open,
                code: fidl_mlme::AssociateResultCode::RefusedReasonUnspecified,
            })
            .into(),
            ..fake_connect_stats()
        };
        let expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_METRIC_ID,
            metrics::CONNECTION_FAILURE_METRIC_ID,
            metrics::ASSOCIATION_FAILURE_METRIC_ID,
        };
        let cobalt_1dot1_expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_MIGRATED_METRIC_ID,
            metrics::CONNECTION_FAILURE_MIGRATED_METRIC_ID,
        };
        test_metric_subset(
            &connect_stats,
            expected_metrics_subset,
            cobalt_1dot1_expected_metrics_subset,
            hashset! {},
        );
    }

    #[test]
    fn test_log_connect_stats_establish_rsna_failure() {
        let connect_stats = ConnectStats {
            result: EstablishRsnaFailure {
                auth_method: None,
                reason: EstablishRsnaFailureReason::OverallTimeout,
            }
            .into(),
            ..fake_connect_stats()
        };
        let expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_METRIC_ID,
            metrics::CONNECTION_FAILURE_METRIC_ID,
            metrics::ESTABLISH_RSNA_FAILURE_METRIC_ID,
        };
        let cobalt_1dot1_expected_metrics_subset = hashset! {
            metrics::CONNECTION_RESULT_MIGRATED_METRIC_ID,
            metrics::CONNECTION_FAILURE_MIGRATED_METRIC_ID,
        };
        test_metric_subset(
            &connect_stats,
            expected_metrics_subset,
            cobalt_1dot1_expected_metrics_subset,
            hashset! {},
        );
    }

    #[test]
    fn test_log_connection_ping() {
        use metrics::ConnectionCountByDurationMetricDimensionConnectedTime::*;

        let (mut cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let start = now();
        let ping = ConnectionPingInfo::first_connected(start);
        log_connection_ping(&mut cobalt_sender, &ping);
        assert_ping_metrics(&mut cobalt_receiver, &[Connected as u32]);
        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::CONNECTION_COUNT_BY_DURATION_METRIC_ID);
            assert_eq!(event.event_codes, vec![Connected as u32])
        });

        let ping = ping.next_ping(start + 1.minute());
        log_connection_ping(&mut cobalt_sender, &ping);
        assert_ping_metrics(&mut cobalt_receiver, &[Connected as u32, ConnectedOneMinute as u32]);
        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::CONNECTION_COUNT_BY_DURATION_METRIC_ID);
            assert_eq!(event.event_codes, vec![ConnectedOneMinute as u32])
        });

        let ping = ping.next_ping(start + 3.minutes());
        log_connection_ping(&mut cobalt_sender, &ping);
        assert_ping_metrics(&mut cobalt_receiver, &[Connected as u32, ConnectedOneMinute as u32]);
        // check that CONNECTION_COUNT_BY_DURATION isn't logged since new milestone is not reached
        assert_variant!(cobalt_receiver.try_next(), Ok(None) | Err(..));

        let ping = ping.next_ping(start + 10.minutes());
        log_connection_ping(&mut cobalt_sender, &ping);
        assert_ping_metrics(
            &mut cobalt_receiver,
            &[Connected as u32, ConnectedOneMinute as u32, ConnectedTenMinute as u32],
        );
        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::CONNECTION_COUNT_BY_DURATION_METRIC_ID);
            assert_eq!(event.event_codes, vec![ConnectedTenMinute as u32])
        });
    }

    fn assert_ping_metrics(cobalt_receiver: &mut mpsc::Receiver<CobaltEvent>, milestones: &[u32]) {
        for milestone in milestones {
            assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
                assert_eq!(event.metric_id, metrics::CONNECTION_UPTIME_PING_METRIC_ID);
                assert_eq!(event.event_codes, vec![*milestone]);
            });
        }
    }

    #[test]
    fn test_log_disconnect_initiated_from_user() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();
        let disconnect_info = DisconnectInfo {
            disconnect_source: DisconnectSource::User(
                fidl_sme::UserDisconnectReason::FailedToConnect,
            ),
            ..fake_disconnect_info([1u8; 6])
        };
        let fut = log_disconnect(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &disconnect_info,
        );
        pin_mut!(fut);
        let cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::DISCONNECT_COUNT_METRIC_ID);
        });
        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::DISCONNECT_COUNT_BREAKDOWN_METRIC_ID);
            assert_eq!(event.event_codes, vec![
                metrics::DisconnectCountBreakdownMetricDimensionConnectedTime::LessThanOneMinute as u32,
                metrics::DisconnectCountBreakdownMetricDimensionDisconnectSource::User as u32,
                metrics::DisconnectCountBreakdownMetricDimensionSnr::From1To10 as u32,
                metrics::DisconnectCountBreakdownMetricDimensionRecentChannelSwitch::No as u32,
                metrics::DisconnectCountBreakdownMetricDimensionChannelBand::Band2Dot4Ghz as u32
            ]);
        });

        assert_eq!(cobalt_1dot1_events.len(), 2);
        assert_eq!(
            cobalt_1dot1_events[0].0,
            metrics::DISCONNECT_COUNT_BREAKDOWN_MIGRATED_METRIC_ID
        );
        assert_eq!(
            cobalt_1dot1_events[0].1,
            vec![
                metrics::DisconnectCountBreakdownMetricDimensionConnectedTime::LessThanOneMinute
                    as u32,
                metrics::DisconnectCountBreakdownMetricDimensionDisconnectSource::User as u32,
                metrics::DisconnectCountBreakdownMetricDimensionSnr::From1To10 as u32,
                metrics::DisconnectCountBreakdownMetricDimensionRecentChannelSwitch::No as u32,
                metrics::DisconnectCountBreakdownMetricDimensionChannelBand::Band2Dot4Ghz as u32
            ]
        );
        assert_eq!(cobalt_1dot1_events[1].0, metrics::DISCONNECT_REASON_METRIC_ID);
        assert_eq!(
            cobalt_1dot1_events[1].1,
            vec![
                fidl_sme::UserDisconnectReason::FailedToConnect as u32,
                metrics::ConnectivityWlanMetricDimensionDisconnectSource::User as u32,
            ]
        );
    }

    #[test]
    fn test_log_disconnect_initiated_from_ap() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();
        let disconnect_info = DisconnectInfo {
            disconnect_source: DisconnectSource::Ap(DisconnectCause {
                reason_code: fidl_ieee80211::ReasonCode::NoMoreStas,
                mlme_event_name: DisconnectMlmeEventName::DisassociateIndication,
            }),
            ..fake_disconnect_info([1u8; 6])
        };
        let fut = log_disconnect(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &disconnect_info,
        );
        pin_mut!(fut);
        let cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::DISCONNECT_COUNT_METRIC_ID);
        });
        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::DISCONNECT_COUNT_BREAKDOWN_METRIC_ID);
            assert_eq!(event.event_codes, vec![
                metrics::DisconnectCountBreakdownMetricDimensionConnectedTime::LessThanOneMinute as u32,
                metrics::DisconnectCountBreakdownMetricDimensionDisconnectSource::Ap as u32,
                metrics::DisconnectCountBreakdownMetricDimensionSnr::From1To10 as u32,
                metrics::DisconnectCountBreakdownMetricDimensionRecentChannelSwitch::No as u32,
                metrics::DisconnectCountBreakdownMetricDimensionChannelBand::Band2Dot4Ghz as u32
            ]);
        });

        assert_eq!(cobalt_1dot1_events.len(), 2);
        assert_eq!(
            cobalt_1dot1_events[0].0,
            metrics::DISCONNECT_COUNT_BREAKDOWN_MIGRATED_METRIC_ID
        );
        assert_eq!(
            cobalt_1dot1_events[0].1,
            vec![
                metrics::DisconnectCountBreakdownMetricDimensionConnectedTime::LessThanOneMinute
                    as u32,
                metrics::DisconnectCountBreakdownMetricDimensionDisconnectSource::Ap as u32,
                metrics::DisconnectCountBreakdownMetricDimensionSnr::From1To10 as u32,
                metrics::DisconnectCountBreakdownMetricDimensionRecentChannelSwitch::No as u32,
                metrics::DisconnectCountBreakdownMetricDimensionChannelBand::Band2Dot4Ghz as u32
            ]
        );
        assert_eq!(cobalt_1dot1_events[1].0, metrics::DISCONNECT_REASON_METRIC_ID);
        assert_eq!(
            cobalt_1dot1_events[1].1,
            vec![
                fidl_ieee80211::ReasonCode::NoMoreStas as u32,
                metrics::ConnectivityWlanMetricDimensionDisconnectSource::Ap as u32,
            ]
        );
    }

    #[test]
    fn test_log_disconnect_initiated_from_mlme() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();
        let disconnect_info = DisconnectInfo {
            disconnect_source: DisconnectSource::Mlme(DisconnectCause {
                reason_code: fidl_ieee80211::ReasonCode::MlmeLinkFailed,
                mlme_event_name: DisconnectMlmeEventName::DeauthenticateIndication,
            }),
            ..fake_disconnect_info([1u8; 6])
        };
        let fut = log_disconnect(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &disconnect_info,
        );
        pin_mut!(fut);
        let cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::LOST_CONNECTION_COUNT_METRIC_ID);
        });
        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::DISCONNECT_COUNT_METRIC_ID);
        });
        assert_variant!(cobalt_receiver.try_next(), Ok(Some(event)) => {
            assert_eq!(event.metric_id, metrics::DISCONNECT_COUNT_BREAKDOWN_METRIC_ID);
            assert_eq!(event.event_codes, vec![
                metrics::DisconnectCountBreakdownMetricDimensionConnectedTime::LessThanOneMinute as u32,
                metrics::DisconnectCountBreakdownMetricDimensionDisconnectSource::Mlme as u32,
                metrics::DisconnectCountBreakdownMetricDimensionSnr::From1To10 as u32,
                metrics::DisconnectCountBreakdownMetricDimensionRecentChannelSwitch::No as u32,
                metrics::DisconnectCountBreakdownMetricDimensionChannelBand::Band2Dot4Ghz as u32
            ]);
        });

        assert_eq!(cobalt_1dot1_events.len(), 2);
        assert_eq!(
            cobalt_1dot1_events[0].0,
            metrics::DISCONNECT_COUNT_BREAKDOWN_MIGRATED_METRIC_ID
        );
        assert_eq!(
            cobalt_1dot1_events[0].1,
            vec![
                metrics::DisconnectCountBreakdownMetricDimensionConnectedTime::LessThanOneMinute
                    as u32,
                metrics::DisconnectCountBreakdownMetricDimensionDisconnectSource::Mlme as u32,
                metrics::DisconnectCountBreakdownMetricDimensionSnr::From1To10 as u32,
                metrics::DisconnectCountBreakdownMetricDimensionRecentChannelSwitch::No as u32,
                metrics::DisconnectCountBreakdownMetricDimensionChannelBand::Band2Dot4Ghz as u32
            ]
        );

        assert_eq!(cobalt_1dot1_events[1].0, metrics::DISCONNECT_REASON_METRIC_ID);
        assert_eq!(
            cobalt_1dot1_events[1].1,
            vec![
                fidl_ieee80211::ReasonCode::MlmeLinkFailed as u32,
                metrics::ConnectivityWlanMetricDimensionDisconnectSource::Mlme as u32,
            ]
        );
    }

    #[test]
    fn test_inspect_log_connect_stats() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, _cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let connect_stats = fake_connect_stats();
        let fut = log_connect_stats(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &connect_stats,
        );
        pin_mut!(fut);
        // This is to execute and complete the Future
        let _cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                connect: {
                    "0": {
                        "@time": AnyProperty,
                        attempts: 1u64,
                        reconnect_info: {
                            gap_time: DURATION_SINCE_LAST_DISCONNECT.into_nanos(),
                            same_ssid: true,
                        },
                    }
                }
            }
        });
    }

    #[test]
    fn test_inspect_log_disconnect_stats_disconnect_source_user() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, _cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let disconnect_source =
            DisconnectSource::User(fidl_sme::UserDisconnectReason::FailedToConnect);
        let disconnect_info = DisconnectInfo {
            connected_duration: 30.seconds(),
            bssid: [1u8; 6],
            ssid: b"foo".to_vec(),
            wsc: Some(fake_probe_resp_wsc_ie()),
            protection: BssProtection::Open,
            channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
            last_rssi: -90,
            last_snr: 1,
            disconnect_source,
            time_since_channel_switch: Some(zx::Duration::from_nanos(1337i64)),
        };
        let fut = log_disconnect(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &disconnect_info,
        );
        pin_mut!(fut);
        // This is to execute and complete the Future
        let _cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                disconnect: {
                    "0": {
                        "@time": AnyProperty,
                        connected_duration: 30.seconds().into_nanos(),
                        bssid: "01:01:01:01:01:01",
                        bssid_hash: AnyProperty,
                        ssid: "<ssid-666f6f>",
                        ssid_hash: AnyProperty,
                        wsc: {
                            device_name: "ASUS Router",
                            manufacturer: "ASUSTek Computer Inc.",
                            model_name: "RT-AC58U",
                            model_number: "123",
                        },
                        protection: "Open",
                        channel: {
                            primary: 1u64,
                            cbw: "Cbw20",
                            secondary80: 0u64,
                        },
                        last_rssi: -90i64,
                        last_snr: 1i64,
                        locally_initiated: true,
                        reason_code: (1u64 << 16) + 1u64,
                        disconnect_source: "source: user, reason: FailedToConnect",
                        time_since_channel_switch: 1337i64,
                    }
                }
            }
        });
    }
    #[test]
    fn test_inspect_log_disconnect_stats_disconnect_source_ap() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, _cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let disconnect_source = DisconnectSource::Ap(DisconnectCause {
            reason_code: fidl_ieee80211::ReasonCode::NoMoreStas,
            mlme_event_name: DisconnectMlmeEventName::DisassociateIndication,
        });
        let disconnect_info = DisconnectInfo {
            connected_duration: 30.seconds(),
            bssid: [1u8; 6],
            ssid: b"foo".to_vec(),
            wsc: None,
            protection: BssProtection::Open,
            channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
            last_rssi: -90,
            last_snr: 1,
            disconnect_source,
            time_since_channel_switch: Some(zx::Duration::from_nanos(1337i64)),
        };
        let fut = log_disconnect(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &disconnect_info,
        );
        pin_mut!(fut);
        // This is to execute and complete the Future
        let _cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                disconnect: {
                    "0": {
                        "@time": AnyProperty,
                        connected_duration: 30.seconds().into_nanos(),
                        bssid: "01:01:01:01:01:01",
                        bssid_hash: AnyProperty,
                        ssid: "<ssid-666f6f>",
                        ssid_hash: AnyProperty,
                        protection: "Open",
                        channel: {
                            primary: 1u64,
                            cbw: "Cbw20",
                            secondary80: 0u64,
                        },
                        last_rssi: -90i64,
                        last_snr: 1i64,
                        locally_initiated: false,
                        reason_code: 5u64,
                        disconnect_source: "source: ap, reason: NoMoreStas, mlme_event_name: DisassociateIndication",
                        time_since_channel_switch: 1337i64,
                    }
                }
            }
        });
    }

    #[test]
    fn test_inspect_log_disconnect_stats_disconnect_source_mlme() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, _cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let disconnect_source = DisconnectSource::Mlme(DisconnectCause {
            reason_code: fidl_ieee80211::ReasonCode::LeavingNetworkDeauth,
            mlme_event_name: DisconnectMlmeEventName::DeauthenticateIndication,
        });
        let disconnect_info = DisconnectInfo {
            connected_duration: 30.seconds(),
            bssid: [1u8; 6],
            ssid: b"foo".to_vec(),
            wsc: None,
            protection: BssProtection::Open,
            channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
            last_rssi: -90,
            last_snr: 1,
            disconnect_source,
            time_since_channel_switch: Some(zx::Duration::from_nanos(1337i64)),
        };
        let fut = log_disconnect(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &disconnect_info,
        );
        pin_mut!(fut);
        // This is to execute and complete the Future
        let _cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                disconnect: {
                    "0": {
                        "@time": AnyProperty,
                        connected_duration: 30.seconds().into_nanos(),
                        bssid: "01:01:01:01:01:01",
                        bssid_hash: AnyProperty,
                        ssid: "<ssid-666f6f>",
                        ssid_hash: AnyProperty,
                        protection: "Open",
                        channel: {
                            primary: 1u64,
                            cbw: "Cbw20",
                            secondary80: 0u64,
                        },
                        last_rssi: -90i64,
                        last_snr: 1i64,
                        locally_initiated: true,
                        reason_code: 131072u64 + 3u64,
                        disconnect_source: "source: mlme, reason: LeavingNetworkDeauth, mlme_event_name: DeauthenticateIndication",
                        time_since_channel_switch: 1337i64,
                    }
                }
            }
        });
    }

    #[test]
    fn test_inspect_log_scan() {
        let (mut cobalt_sender, _cobalt_receiver) = fake_cobalt_sender();
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let now = now();
        let scan_stats = ScanStats {
            scan_start_at: now - 3.seconds(),
            scan_end_at: now,
            scan_type: fidl_mlme::ScanTypes::Active,
            scan_start_while_connected: true,
            result_code: fidl_mlme::ScanResultCode::Success,
            bss_count: 5,
        };
        log_scan_stats(&mut cobalt_sender, inspect_tree.clone(), &scan_stats, true);

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                scan: {
                    "0": {
                        "@time": AnyProperty,
                        start_at: scan_stats.scan_start_at.into_nanos(),
                        end_at: scan_stats.scan_end_at.into_nanos(),
                        result: "Success",
                        start_while_connected: true,
                    }
                }
            }
        });
    }

    #[test]
    fn test_inspect_log_scan_failures() {
        let (mut cobalt_sender, _cobalt_receiver) = fake_cobalt_sender();
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();

        let now = now();
        let scan_stats = ScanStats {
            scan_start_at: now - 3.seconds(),
            scan_end_at: now,
            scan_type: fidl_mlme::ScanTypes::Active,
            scan_start_while_connected: true,
            result_code: fidl_mlme::ScanResultCode::InvalidArgs,
            bss_count: 5,
        };
        log_scan_stats(&mut cobalt_sender, inspect_tree.clone(), &scan_stats, true);

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                scan_failures: {
                    "0": {
                        "@time": AnyProperty,
                    }
                }
            }
        });
    }

    #[test]
    fn test_inspect_log_counters() {
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();
        let last = fake_iface_stats(10);
        let current = fake_iface_stats(20);
        log_counters_to_inspect(1, &last, &current, inspect_tree.clone());

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                counters: {
                    "0": {
                        "@time": AnyProperty,
                        iface: 1u64,
                        tx_total: 10u64,
                        tx_drop: 30u64,
                        rx_total: 10u64,
                        rx_drop: 30u64,
                    }
                }
            }
        })
    }

    #[test]
    fn test_inspect_log_counters_detects_overflow() {
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();
        let last = fake_iface_stats(20);
        let current = fake_iface_stats(10);
        log_counters_to_inspect(1, &last, &current, inspect_tree.clone());

        assert_data_tree!(inspect_tree.inspector, root: contains {
            client_stats: contains {
                counters: {
                    "0": {
                        "@time": AnyProperty,
                        iface: 1u64,
                        msg: "no diff (counters have likely been reset)",
                    }
                }
            }
        })
    }

    fn test_metric_subset(
        connect_stats: &ConnectStats,
        mut expected_metrics_subset: HashSet<u32>,
        expected_cobalt_1dot1_metrics: HashSet<u32>,
        unexpected_metrics: HashSet<u32>,
    ) {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let (mut cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();
        let (mut cobalt_1dot1_proxy, mut cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy and stream");
        let (inspect_tree, _persistence_stream) = fake_inspect_tree();
        let fut = log_connect_stats(
            &mut cobalt_sender,
            &mut cobalt_1dot1_proxy,
            inspect_tree.clone(),
            &connect_stats,
        );
        pin_mut!(fut);
        let cobalt_1dot1_events =
            drain_cobalt_events(&mut exec, &mut fut, &mut cobalt_1dot1_stream);

        while let Ok(Some(event)) = cobalt_receiver.try_next() {
            assert!(
                !unexpected_metrics.contains(&event.metric_id),
                "unexpected event: {:?}",
                event
            );
            if expected_metrics_subset.contains(&event.metric_id) {
                expected_metrics_subset.remove(&event.metric_id);
            }
        }
        assert!(
            expected_metrics_subset.is_empty(),
            "some metrics not logged: {:?}",
            expected_metrics_subset
        );

        assert_eq!(
            cobalt_1dot1_events.into_iter().map(|e| e.0).collect::<HashSet<_>>(),
            expected_cobalt_1dot1_metrics
        );
    }

    fn now() -> zx::Time {
        zx::Time::get_monotonic()
    }

    fn fake_connect_stats() -> ConnectStats {
        let now = now();
        ConnectStats {
            connect_start_at: now,
            connect_end_at: now,
            auth_start_at: Some(now),
            auth_end_at: Some(now),
            assoc_start_at: Some(now),
            assoc_end_at: Some(now),
            rsna_start_at: Some(now),
            rsna_end_at: Some(now),
            supplicant_error: None,
            supplicant_progress: Some(SupplicantProgress {
                pmksa_established: true,
                ptksa_established: true,
                gtksa_established: true,
                esssa_established: true,
            }),
            num_rsna_key_frame_exchange_timeout: 0,
            result: ConnectResult::Success,
            candidate_network: Some(CandidateNetwork {
                bss: fake_bss!(Open),
                multiple_bss_candidates: true,
            }),
            attempts: 1,
            last_ten_failures: vec![],
            previous_disconnect_info: Some(PreviousDisconnectInfo {
                ssid: fake_bss!(Open).ssid().to_vec(),
                disconnect_source: DisconnectSource::User(
                    fidl_sme::UserDisconnectReason::WlanstackUnitTesting,
                ),
                disconnect_at: now - DURATION_SINCE_LAST_DISCONNECT,
            }),
        }
    }

    fn fake_iface_stats(nth_req: u64) -> IfaceStats {
        IfaceStats {
            dispatcher_stats: DispatcherStats {
                any_packet: fake_packet_counter(nth_req),
                mgmt_frame: fake_packet_counter(nth_req),
                ctrl_frame: fake_packet_counter(nth_req),
                data_frame: fake_packet_counter(nth_req),
            },
            mlme_stats: Some(Box::new(ClientMlmeStats(fidl_stats::ClientMlmeStats {
                svc_msg: fake_packet_counter(nth_req),
                data_frame: fake_packet_counter(nth_req),
                mgmt_frame: fake_packet_counter(nth_req),
                tx_frame: fake_packet_counter(nth_req),
                rx_frame: fake_packet_counter(nth_req),
                assoc_data_rssi: fake_rssi(nth_req),
                beacon_rssi: fake_rssi(nth_req),
                noise_floor_histograms: fake_noise_floor_histograms(),
                rssi_histograms: fake_rssi_histograms(),
                rx_rate_index_histograms: fake_rx_rate_index_histograms(),
                snr_histograms: fake_snr_histograms(),
            }))),
        }
    }

    fn fake_packet_counter(nth_req: u64) -> PacketCounter {
        PacketCounter {
            in_: Counter { count: 1 * nth_req, name: "in".to_string() },
            out: Counter { count: 2 * nth_req, name: "out".to_string() },
            drop: Counter { count: 3 * nth_req, name: "drop".to_string() },
            in_bytes: Counter { count: 4 * nth_req, name: "in_bytes".to_string() },
            out_bytes: Counter { count: 5 * nth_req, name: "out_bytes".to_string() },
            drop_bytes: Counter { count: 6 * nth_req, name: "drop_bytes".to_string() },
        }
    }

    fn fake_rssi(nth_req: u64) -> fidl_stats::RssiStats {
        fidl_stats::RssiStats { hist: vec![nth_req] }
    }

    fn fake_antenna_id() -> Option<Box<fidl_stats::AntennaId>> {
        Some(Box::new(fidl_stats::AntennaId { freq: fidl_stats::AntennaFreq::Antenna5G, index: 0 }))
    }

    fn fake_noise_floor_histograms() -> Vec<fidl_stats::NoiseFloorHistogram> {
        vec![fidl_stats::NoiseFloorHistogram {
            hist_scope: fidl_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // Noise floor bucket_index 165 indicates -90 dBm.
            noise_floor_samples: vec![fidl_stats::HistBucket {
                bucket_index: 165,
                num_samples: 10,
            }],
            invalid_samples: 0,
        }]
    }

    fn fake_rssi_histograms() -> Vec<fidl_stats::RssiHistogram> {
        vec![fidl_stats::RssiHistogram {
            hist_scope: fidl_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // RSSI bucket_index 225 indicates -30 dBm.
            rssi_samples: vec![fidl_stats::HistBucket { bucket_index: 225, num_samples: 10 }],
            invalid_samples: 0,
        }]
    }

    fn fake_rx_rate_index_histograms() -> Vec<fidl_stats::RxRateIndexHistogram> {
        vec![fidl_stats::RxRateIndexHistogram {
            hist_scope: fidl_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // Rate bucket_index 74 indicates HT BW40 MCS 14 SGI, which is 802.11n 270 Mb/s.
            // Rate bucket_index 75 indicates HT BW40 MCS 15 SGI, which is 802.11n 300 Mb/s.
            rx_rate_index_samples: vec![
                fidl_stats::HistBucket { bucket_index: 74, num_samples: 5 },
                fidl_stats::HistBucket { bucket_index: 75, num_samples: 5 },
            ],
            invalid_samples: 0,
        }]
    }

    fn fake_snr_histograms() -> Vec<fidl_stats::SnrHistogram> {
        vec![fidl_stats::SnrHistogram {
            hist_scope: fidl_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // Signal to noise ratio bucket_index 60 indicates 60 dB.
            snr_samples: vec![fidl_stats::HistBucket { bucket_index: 60, num_samples: 10 }],
            invalid_samples: 0,
        }]
    }

    fn fake_iface_map() -> (IfaceMap, impl Stream<Item = StatsRequest>) {
        let ifaces_map = IfaceMap::new();
        let (iface_device, stats_requests) = fake_iface_device();
        ifaces_map.insert(IFACE_ID, iface_device);
        (ifaces_map, stats_requests)
    }

    fn fake_iface_device() -> (IfaceDevice, impl Stream<Item = StatsRequest>) {
        let (sme_sender, _sme_receiver) = mpsc::unbounded();
        let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
        let (proxy, _server) = create_proxy::<MlmeMarker>().expect("Error creating proxy");
        let mlme_query = MlmeQueryProxy::new(proxy);
        let (shutdown_sender, _) = mpsc::channel(1);
        let device_info = fake_device_info();
        let iface_device = IfaceDevice {
            phy_ownership: device::PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
            sme_server: device::SmeServer::Client(sme_sender),
            stats_sched,
            mlme_query,
            device_info,
            shutdown_sender,
        };
        (iface_device, stats_requests)
    }

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_mlme::MacRole::Client,
            bands: vec![],
            mac_addr: [0xAC; 6],
            driver_features: vec![],
            qos_capable: false,
        }
    }

    // Continually execute the future and respond to any incoming Cobalt request with Ok.
    // Save the metric ID and event codes of each metric request into a vector and return it.
    fn drain_cobalt_events(
        exec: &mut fasync::TestExecutor,
        fut: &mut (impl Future + Unpin),
        event_stream: &mut fidl_fuchsia_metrics::MetricEventLoggerRequestStream,
    ) -> Vec<(u32, Vec<u32>)> {
        let mut metrics = vec![];
        while let Poll::Pending = exec.run_until_stalled(fut) {
            while let Poll::Ready(Some(Ok(req))) = exec.run_until_stalled(&mut event_stream.next())
            {
                metrics.push((req.metric_id(), req.event_codes().to_vec()));
                req.respond(fidl_fuchsia_metrics::Status::Ok);
            }
        }
        metrics
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_cobalt::HistogramBucket;
use fidl_fuchsia_wlan_mlme::{
    AssociateResultCodes, AuthenticateResultCodes, JoinResultCodes, ScanResultCodes,
};
use fidl_fuchsia_wlan_stats as fidl_stats;
use fidl_fuchsia_wlan_stats::MlmeStats::{ApMlmeStats, ClientMlmeStats};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use fuchsia_zircon::DurationNum;
use futures::prelude::*;
use futures::stream::FuturesUnordered;
use futures::StreamExt;
use log::error;
use parking_lot::Mutex;
use std::cmp::PartialOrd;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::default::Default;
use std::ops::Sub;
use std::sync::Arc;
use wlan_sme::client::{ConnectFailure, ConnectResult, Standard};

use crate::device::IfaceMap;
use fuchsia_cobalt::CobaltSender;

type StatsRef = Arc<Mutex<fidl_stats::IfaceStats>>;

const REPORT_PERIOD_MINUTES: i64 = 1;

// These IDs must match the Cobalt config from
// //third_party/cobalt_config/fuchsia/wlan/config.yaml
enum CobaltMetricId {
    RsnaDelay = 2,
    AssociationDelay = 3,
    ScanDelay = 4,
    DispatcherPacketCounter = 5,
    ClientAssocDataRssi = 6,
    ClientBeaconRssi = 7,
    ConnectionDelay = 8,
    RxTxFrameCount = 9,
    RxTxFrameBytes = 10,
    NeighborNetworks = 11,
    WlanStandards = 12,
    WlanChannels = 13,
}

// Export MLME stats to Cobalt every REPORT_PERIOD_MINUTES.
pub async fn report_telemetry_periodically(ifaces_map: Arc<IfaceMap>, mut sender: CobaltSender) {
    // TODO(NET-1386): Make this module resilient to Wlanstack2 downtime.

    let mut last_reported_stats: HashMap<u16, StatsRef> = HashMap::new();
    let mut interval_stream = fasync::Interval::new(REPORT_PERIOD_MINUTES.minutes());
    while let Some(_) = await!(interval_stream.next()) {
        let mut futures = FuturesUnordered::new();
        for (id, iface) in ifaces_map.get_snapshot().iter() {
            let id = *id;
            let iface = Arc::clone(iface);
            let fut = iface.stats_sched.get_stats().map(move |r| (id, iface, r));
            futures.push(fut);
        }

        while let Some((id, iface, stats_result)) = await!(futures.next()) {
            match stats_result {
                Ok(current_stats) => {
                    let last_stats_opt = last_reported_stats.get(&id);
                    if let Some(last_stats) = last_stats_opt {
                        let last_stats = last_stats.lock();
                        let current_stats = current_stats.lock();
                        report_stats(&last_stats, &current_stats, &mut sender);
                    }

                    last_reported_stats.insert(id, current_stats);
                }
                Err(e) => {
                    last_reported_stats.remove(&id);
                    error!(
                        "Failed to get the stats for iface '{}': {}",
                        iface.device.path().display(),
                        e
                    );
                }
            };
        }
    }
}

fn report_stats(
    last_stats: &fidl_stats::IfaceStats,
    current_stats: &fidl_stats::IfaceStats,
    sender: &mut CobaltSender,
) {
    report_mlme_stats(&last_stats.mlme_stats, &current_stats.mlme_stats, sender);

    report_dispatcher_stats(&last_stats.dispatcher_stats, &current_stats.dispatcher_stats, sender);
}

fn report_dispatcher_stats(
    last_stats: &fidl_stats::DispatcherStats,
    current_stats: &fidl_stats::DispatcherStats,
    sender: &mut CobaltSender,
) {
    // These indexes must match the Cobalt config from
    // //third_party/cobalt_config/fuchsia/wlan/config.yaml
    const DISPATCHER_IN_PACKET_COUNT_INDEX: u32 = 0;
    const DISPATCHER_OUT_PACKET_COUNT_INDEX: u32 = 1;
    const DISPATCHER_DROP_PACKET_COUNT_INDEX: u32 = 2;

    report_dispatcher_packets(
        DISPATCHER_IN_PACKET_COUNT_INDEX,
        get_diff(last_stats.any_packet.in_.count, current_stats.any_packet.in_.count),
        sender,
    );
    report_dispatcher_packets(
        DISPATCHER_OUT_PACKET_COUNT_INDEX,
        get_diff(last_stats.any_packet.out.count, current_stats.any_packet.out.count),
        sender,
    );
    report_dispatcher_packets(
        DISPATCHER_DROP_PACKET_COUNT_INDEX,
        get_diff(last_stats.any_packet.drop.count, current_stats.any_packet.drop.count),
        sender,
    );
}

fn report_dispatcher_packets(packet_type_index: u32, packet_count: u64, sender: &mut CobaltSender) {
    sender.log_event_count(
        CobaltMetricId::DispatcherPacketCounter as u32,
        packet_type_index,
        packet_count as i64,
    );
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
        CobaltMetricId::ClientAssocDataRssi as u32,
        &last_stats.assoc_data_rssi,
        &current_stats.assoc_data_rssi,
        sender,
    );
    report_rssi_stats(
        CobaltMetricId::ClientBeaconRssi as u32,
        &last_stats.beacon_rssi,
        &current_stats.beacon_rssi,
        sender,
    );

    report_client_mlme_rx_tx_frames(&last_stats, &current_stats, sender);
}

fn report_client_mlme_rx_tx_frames(
    last_stats: &fidl_stats::ClientMlmeStats,
    current_stats: &fidl_stats::ClientMlmeStats,
    sender: &mut CobaltSender,
) {
    // These indexes must match the Cobalt config from
    // //third_party/cobalt_config/fuchsia/wlan/config.yaml
    const CLIENT_MLME_RX_FRAME_COUNT_INDEX: u32 = 0;
    const CLIENT_MLME_TX_FRAME_COUNT_INDEX: u32 = 1;
    sender.log_event_count(
        CobaltMetricId::RxTxFrameCount as u32,
        CLIENT_MLME_RX_FRAME_COUNT_INDEX,
        get_diff(last_stats.rx_frame.in_.count, current_stats.rx_frame.in_.count) as i64,
    );
    sender.log_event_count(
        CobaltMetricId::RxTxFrameCount as u32,
        CLIENT_MLME_TX_FRAME_COUNT_INDEX,
        get_diff(last_stats.tx_frame.out.count, current_stats.tx_frame.out.count) as i64,
    );

    // These indexes must match the Cobalt config from
    // //third_party/cobalt_config/fuchsia/wlan/config.yaml
    const CLIENT_MLME_RX_FRAME_BYTES_INDEX: u32 = 0;
    const CLIENT_MLME_TX_FRAME_BYTES_INDEX: u32 = 1;
    sender.log_event_count(
        CobaltMetricId::RxTxFrameBytes as u32,
        CLIENT_MLME_RX_FRAME_BYTES_INDEX,
        get_diff(last_stats.rx_frame.in_bytes.count, current_stats.rx_frame.in_bytes.count) as i64,
    );
    sender.log_event_count(
        CobaltMetricId::RxTxFrameBytes as u32,
        CLIENT_MLME_TX_FRAME_BYTES_INDEX,
        get_diff(last_stats.tx_frame.out_bytes.count, current_stats.tx_frame.out_bytes.count)
            as i64,
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
        sender.log_int_histogram(rssi_metric_id, histogram);
    }
}

pub fn report_scan_delay(
    sender: &mut CobaltSender,
    scan_started_time: zx::Time,
    scan_finished_time: zx::Time,
) {
    let delay_micros = (scan_finished_time - scan_started_time).nanos() / 1000;
    sender.log_elapsed_time(CobaltMetricId::ScanDelay as u32, 0, delay_micros);
}

pub fn report_connection_delay(
    sender: &mut CobaltSender,
    conn_started_time: zx::Time,
    conn_finished_time: zx::Time,
    result: &ConnectResult,
    failure: &Option<ConnectFailure>,
) {
    let delay_micros = (conn_finished_time - conn_started_time).nanos() / 1000;
    let cobalt_index = match (result, failure) {
        (ConnectResult::Success, None) => Some(ConnectionResultLabel::SuccessId),
        (ConnectResult::Success, Some(_)) => None,
        (_, Some(failure)) => convert_connect_failure(failure),
        (_, None) => Some(ConnectionResultLabel::FailId),
    };

    if let Some(cobalt_index) = cobalt_index {
        sender.log_elapsed_time(
            CobaltMetricId::ConnectionDelay as u32,
            cobalt_index as u32,
            delay_micros,
        );
    }
}

pub fn report_assoc_success_delay(
    sender: &mut CobaltSender,
    assoc_started_time: zx::Time,
    assoc_finished_time: zx::Time,
) {
    let delay_micros = (assoc_finished_time - assoc_started_time).nanos() / 1000;
    sender.log_elapsed_time(CobaltMetricId::AssociationDelay as u32, 0, delay_micros);
}

pub fn report_rsna_established_delay(
    sender: &mut CobaltSender,
    rsna_started_time: zx::Time,
    rsna_finished_time: zx::Time,
) {
    let delay_micros = (rsna_finished_time - rsna_started_time).nanos() / 1000;
    sender.log_elapsed_time(CobaltMetricId::RsnaDelay as u32, 0, delay_micros);
}

pub fn report_neighbor_networks_count(
    sender: &mut CobaltSender,
    bss_count: usize,
    ess_count: usize,
) {
    const BSS_COUNT_INDEX: u32 = 0;
    const ESS_COUNT_INDEX: u32 = 1;
    sender.log_event_count(
        CobaltMetricId::NeighborNetworks as u32,
        BSS_COUNT_INDEX,
        bss_count as i64,
    );
    sender.log_event_count(
        CobaltMetricId::NeighborNetworks as u32,
        ESS_COUNT_INDEX,
        ess_count as i64,
    );
}

pub fn report_standards(
    sender: &mut CobaltSender,
    mut num_bss_by_standard: HashMap<Standard, usize>,
) {
    const ALL_STANDARDS: [(Standard, StandardLabel); 5] = [
        (Standard::B, StandardLabel::B),
        (Standard::G, StandardLabel::G),
        (Standard::A, StandardLabel::A),
        (Standard::N, StandardLabel::N),
        (Standard::Ac, StandardLabel::Ac),
    ];
    ALL_STANDARDS.into_iter().for_each(|(standard, label)| {
        let count = match num_bss_by_standard.entry(standard.clone()) {
            Entry::Vacant(_) => 0 as i64,
            Entry::Occupied(e) => *e.get() as i64,
        };
        sender.log_event_count(CobaltMetricId::WlanStandards as u32, label.clone() as u32, count)
    });
}

pub fn report_channels(sender: &mut CobaltSender, num_bss_by_channel: HashMap<u8, usize>) {
    num_bss_by_channel.into_iter().for_each(|(channel, count)| {
        sender.log_event_count(CobaltMetricId::WlanChannels as u32, channel as u32, count as i64);
    });
}

#[derive(Clone, Debug)]
enum StandardLabel {
    B = 0,
    G = 1,
    A = 2,
    N = 3,
    Ac = 4,
}

#[derive(Debug)]
enum ConnectionResultLabel {
    SuccessId = 0,
    FailId = 1,
    NoMatchingBssFoundId = 2,
    JoinFailureTimeoutId = 1000,
    AuthRefusedId = 2000,
    AuthAntiCloggingTokenRequiredId = 2001,
    AuthFiniteCyclicGroupNotSupportedId = 2002,
    AuthRejectedId = 2003,
    AuthFailureTimeoutId = 2004,
    AssocRefusedReasonUnspecifiedId = 3000,
    AssocRefusedNotAuthenticatedId = 3001,
    AssocRefusedCapabilitiesMismatchId = 3002,
    AssocRefusedExternalReasonId = 3003,
    AssocRefusedApOutOfMemoryId = 3004,
    AssocRefusedBasicRatesMismatchId = 3005,
    AssocRejectedEmergencyServicesNotSupportedId = 3006,
    AssocRefusedTemporarilyId = 3007,
    ScanNotSupportedId = 4000,
    ScanInvalidArgsId = 4001,
    ScanInternalErrorId = 4002,
    RsnaTimeout = 5000,
}

fn convert_connect_failure(result: &ConnectFailure) -> Option<ConnectionResultLabel> {
    use crate::telemetry::ConnectionResultLabel::*;
    use fidl_fuchsia_wlan_mlme::AssociateResultCodes::*;
    use fidl_fuchsia_wlan_mlme::AuthenticateResultCodes::*;
    use fidl_fuchsia_wlan_mlme::JoinResultCodes::*;
    use fidl_fuchsia_wlan_mlme::ScanResultCodes::*;

    let result = match result {
        ConnectFailure::NoMatchingBssFound => NoMatchingBssFoundId,
        ConnectFailure::ScanFailure(scan_failure) => match scan_failure {
            ScanResultCodes::Success => {
                return None;
            }
            NotSupported => ScanNotSupportedId,
            InvalidArgs => ScanInvalidArgsId,
            InternalError => ScanInternalErrorId,
        },
        ConnectFailure::JoinFailure(join_failure) => match join_failure {
            JoinResultCodes::Success => {
                return None;
            }
            JoinFailureTimeout => JoinFailureTimeoutId,
        },
        ConnectFailure::AuthenticationFailure(auth_failure) => match auth_failure {
            AuthenticateResultCodes::Success => {
                return None;
            }
            Refused => AuthRefusedId,
            AntiCloggingTokenRequired => AuthAntiCloggingTokenRequiredId,
            FiniteCyclicGroupNotSupported => AuthFiniteCyclicGroupNotSupportedId,
            AuthenticationRejected => AuthRejectedId,
            AuthFailureTimeout => AuthFailureTimeoutId,
        },
        ConnectFailure::AssociationFailure(assoc_failure) => match assoc_failure {
            AssociateResultCodes::Success => {
                return None;
            }
            RefusedReasonUnspecified => AssocRefusedReasonUnspecifiedId,
            RefusedNotAuthenticated => AssocRefusedNotAuthenticatedId,
            RefusedCapabilitiesMismatch => AssocRefusedCapabilitiesMismatchId,
            RefusedExternalReason => AssocRefusedExternalReasonId,
            RefusedApOutOfMemory => AssocRefusedApOutOfMemoryId,
            RefusedBasicRatesMismatch => AssocRefusedBasicRatesMismatchId,
            RejectedEmergencyServicesNotSupported => AssocRejectedEmergencyServicesNotSupportedId,
            RefusedTemporarily => AssocRefusedTemporarilyId,
        },
        ConnectFailure::RsnaTimeout => RsnaTimeout,
    };

    Some(result)
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

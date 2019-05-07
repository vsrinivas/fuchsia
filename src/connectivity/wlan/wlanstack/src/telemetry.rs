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
use wlan_metrics_registry as metrics;
use wlan_sme::client::{ConnectFailure, ConnectResult, Standard};

use crate::device::IfaceMap;
use fuchsia_cobalt::CobaltSender;

type StatsRef = Arc<Mutex<fidl_stats::IfaceStats>>;

const REPORT_PERIOD_MINUTES: i64 = 1;

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
                        "Failed to get the stats for iface '{:?}': {}",
                        match &iface.device {
                            Some(device) => device.path().to_string_lossy().into_owned(),
                            None => "TODO(WLAN-927)".to_string(),
                        },
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
    report_dispatcher_packets(
        metrics::DispatcherPacketCountsMetricDimensionPacketType::In,
        get_diff(last_stats.any_packet.in_.count, current_stats.any_packet.in_.count),
        sender,
    );
    report_dispatcher_packets(
        metrics::DispatcherPacketCountsMetricDimensionPacketType::Out,
        get_diff(last_stats.any_packet.out.count, current_stats.any_packet.out.count),
        sender,
    );
    report_dispatcher_packets(
        metrics::DispatcherPacketCountsMetricDimensionPacketType::Dropped,
        get_diff(last_stats.any_packet.drop.count, current_stats.any_packet.drop.count),
        sender,
    );
}

fn report_dispatcher_packets(
    packet_type: metrics::DispatcherPacketCountsMetricDimensionPacketType,
    packet_count: u64,
    sender: &mut CobaltSender,
) {
    sender.log_event_count(
        metrics::DISPATCHER_PACKET_COUNTS_METRIC_ID,
        packet_type as u32,
        0,
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

    report_client_mlme_rx_tx_frames(&last_stats, &current_stats, sender);
}

fn report_client_mlme_rx_tx_frames(
    last_stats: &fidl_stats::ClientMlmeStats,
    current_stats: &fidl_stats::ClientMlmeStats,
    sender: &mut CobaltSender,
) {
    sender.log_event_count(
        metrics::MLME_RX_TX_FRAME_COUNTS_METRIC_ID,
        metrics::MlmeRxTxFrameCountsMetricDimensionFrameType::Rx as u32,
        0,
        get_diff(last_stats.rx_frame.in_.count, current_stats.rx_frame.in_.count) as i64,
    );
    sender.log_event_count(
        metrics::MLME_RX_TX_FRAME_COUNTS_METRIC_ID,
        metrics::MlmeRxTxFrameCountsMetricDimensionFrameType::Tx as u32,
        0,
        get_diff(last_stats.tx_frame.out.count, current_stats.tx_frame.out.count) as i64,
    );

    sender.log_event_count(
        metrics::MLME_RX_TX_FRAME_BYTES_METRIC_ID,
        metrics::MlmeRxTxFrameBytesMetricDimensionFrameType::Rx as u32,
        0,
        get_diff(last_stats.rx_frame.in_bytes.count, current_stats.rx_frame.in_bytes.count) as i64,
    );
    sender.log_event_count(
        metrics::MLME_RX_TX_FRAME_BYTES_METRIC_ID,
        metrics::MlmeRxTxFrameBytesMetricDimensionFrameType::Tx as u32,
        0,
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
        sender.log_int_histogram(rssi_metric_id, (), histogram);
    }
}

pub fn report_scan_delay(
    sender: &mut CobaltSender,
    scan_started_time: zx::Time,
    scan_finished_time: zx::Time,
) {
    let delay_micros = (scan_finished_time - scan_started_time).micros();
    sender.log_elapsed_time(metrics::SCAN_DELAY_METRIC_ID, (), delay_micros);
}

pub fn report_connection_delay(
    sender: &mut CobaltSender,
    conn_started_time: zx::Time,
    conn_finished_time: zx::Time,
    result: &ConnectResult,
    failure: &Option<ConnectFailure>,
) {
    use wlan_metrics_registry::ConnectionDelayMetricDimensionConnectionResult::{Fail, Success};

    let delay_micros = (conn_finished_time - conn_started_time).micros();
    let connection_result_cobalt = match (result, failure) {
        (ConnectResult::Success, None) => Some(Success),
        (ConnectResult::Success, Some(_)) => None,
        (_, Some(failure)) => convert_connect_failure(failure),
        (_, None) => Some(Fail),
    };

    if let Some(connection_result_cobalt) = connection_result_cobalt {
        sender.log_elapsed_time(
            metrics::CONNECTION_DELAY_METRIC_ID,
            connection_result_cobalt as u32,
            delay_micros,
        );
    }
}

pub fn report_assoc_success_delay(
    sender: &mut CobaltSender,
    assoc_started_time: zx::Time,
    assoc_finished_time: zx::Time,
) {
    let delay_micros = (assoc_finished_time - assoc_started_time).micros();
    sender.log_elapsed_time(metrics::ASSOCIATION_DELAY_METRIC_ID, 0, delay_micros);
}

pub fn report_rsna_established_delay(
    sender: &mut CobaltSender,
    rsna_started_time: zx::Time,
    rsna_finished_time: zx::Time,
) {
    let delay_micros = (rsna_finished_time - rsna_started_time).micros();
    sender.log_elapsed_time(metrics::RSNA_DELAY_METRIC_ID, 0, delay_micros);
}

pub fn report_neighbor_networks_count(
    sender: &mut CobaltSender,
    bss_count: usize,
    ess_count: usize,
) {
    sender.log_event_count(
        metrics::NEIGHBOR_NETWORKS_COUNT_METRIC_ID,
        metrics::NeighborNetworksCountMetricDimensionNetworkType::Bss as u32,
        0,
        bss_count as i64,
    );
    sender.log_event_count(
        metrics::NEIGHBOR_NETWORKS_COUNT_METRIC_ID,
        metrics::NeighborNetworksCountMetricDimensionNetworkType::Ess as u32,
        0,
        ess_count as i64,
    );
}

pub fn report_standards(
    sender: &mut CobaltSender,
    mut num_bss_by_standard: HashMap<Standard, usize>,
) {
    use metrics::NeighborNetworksWlanStandardsCountMetricDimensionWlanStandardType as StandardLabel;
    const ALL_STANDARDS: [(Standard, StandardLabel); 5] = [
        (Standard::B, StandardLabel::_802_11b),
        (Standard::G, StandardLabel::_802_11g),
        (Standard::A, StandardLabel::_802_11a),
        (Standard::N, StandardLabel::_802_11n),
        (Standard::Ac, StandardLabel::_802_11ac),
    ];
    ALL_STANDARDS.into_iter().for_each(|(standard, label)| {
        let count = match num_bss_by_standard.entry(standard.clone()) {
            Entry::Vacant(_) => 0 as i64,
            Entry::Occupied(e) => *e.get() as i64,
        };
        sender.log_event_count(
            metrics::NEIGHBOR_NETWORKS_WLAN_STANDARDS_COUNT_METRIC_ID,
            label.clone() as u32,
            0,
            count,
        )
    });
}

pub fn report_channels(sender: &mut CobaltSender, num_bss_by_channel: HashMap<u8, usize>) {
    num_bss_by_channel.into_iter().for_each(|(channel, count)| {
        sender.log_event_count(
            metrics::NEIGHBOR_NETWORKS_PRIMARY_CHANNELS_COUNT_METRIC_ID,
            channel as u32,
            0,
            count as i64,
        );
    });
}

fn convert_connect_failure(
    result: &ConnectFailure,
) -> Option<metrics::ConnectionDelayMetricDimensionConnectionResult> {
    use wlan_metrics_registry::ConnectionDelayMetricDimensionConnectionResult::*;

    let result = match result {
        ConnectFailure::NoMatchingBssFound => NoMatchingBssFound,
        ConnectFailure::ScanFailure(scan_failure) => match scan_failure {
            ScanResultCodes::Success => return None,
            ScanResultCodes::NotSupported => ScanNotSupported,
            ScanResultCodes::InvalidArgs => ScanInvalidArgs,
            ScanResultCodes::InternalError => ScanInternalError,
        },
        ConnectFailure::JoinFailure(join_failure) => match join_failure {
            JoinResultCodes::Success => return None,
            JoinResultCodes::JoinFailureTimeout => JoinFailureTimeout,
        },
        ConnectFailure::AuthenticationFailure(auth_failure) => match auth_failure {
            AuthenticateResultCodes::Success => return None,
            AuthenticateResultCodes::Refused => AuthenticationRefused,
            AuthenticateResultCodes::AntiCloggingTokenRequired => {
                AuthenticationAntiCloggingTokenRequired
            }
            AuthenticateResultCodes::FiniteCyclicGroupNotSupported => {
                AuthenticationFiniteCyclicGroupNotSupported
            }
            AuthenticateResultCodes::AuthenticationRejected => AuthenticationRejected,
            AuthenticateResultCodes::AuthFailureTimeout => AuthenticationFailureTimeout,
        },
        ConnectFailure::AssociationFailure(assoc_failure) => match assoc_failure {
            AssociateResultCodes::Success => return None,
            AssociateResultCodes::RefusedReasonUnspecified => AssociationRefusedReasonUnspecified,
            AssociateResultCodes::RefusedNotAuthenticated => AssociationRefusedNotAuthenticated,
            AssociateResultCodes::RefusedCapabilitiesMismatch => {
                AssociationRefusedCapabilitiesMismatch
            }
            AssociateResultCodes::RefusedExternalReason => AssociationRefusedExternalReason,
            AssociateResultCodes::RefusedApOutOfMemory => AssociationRefusedApOutOfMemory,
            AssociateResultCodes::RefusedBasicRatesMismatch => AssociationRefusedBasicRatesMismatch,
            AssociateResultCodes::RejectedEmergencyServicesNotSupported => {
                AssociationRejectedEmergencyServicesNotSupported
            }
            AssociateResultCodes::RefusedTemporarily => AssociationRefusedTemporarily,
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

#[cfg(test)]
mod tests {
    use super::*;

    use crate::device::{self, IfaceDevice};
    use crate::mlme_query_proxy::MlmeQueryProxy;
    use crate::stats_scheduler::{self, StatsRequest};

    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload};
    use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeMarker};
    use fidl_fuchsia_wlan_stats::{Counter, DispatcherStats, IfaceStats, PacketCounter};
    use futures::channel::mpsc;
    use pin_utils::pin_mut;

    const IFACE_ID: u16 = 1;

    #[test]
    fn test_report_telemetry_periodically() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");

        let (ifaces_map, stats_requests) = fake_iface_map();
        let (cobalt_sender, mut cobalt_receiver) = fake_cobalt_sender();

        let telemetry_fut = report_telemetry_periodically(Arc::new(ifaces_map), cobalt_sender);
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

        // TODO(WLAN-1113): For some reason, telemetry skips logging the first stats response
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
                metric_id: metrics::DISPATCHER_PACKET_COUNTS_METRIC_ID,
                event_codes: vec![0], // in
                component: None,
                payload: event_count(1),
            },
            CobaltEvent {
                metric_id: metrics::DISPATCHER_PACKET_COUNTS_METRIC_ID,
                event_codes: vec![1], // out
                component: None,
                payload: event_count(2),
            },
            CobaltEvent {
                metric_id: metrics::DISPATCHER_PACKET_COUNTS_METRIC_ID,
                event_codes: vec![2], // dropped
                component: None,
                payload: event_count(3),
            },
            CobaltEvent {
                metric_id: metrics::MLME_RX_TX_FRAME_COUNTS_METRIC_ID,
                event_codes: vec![0], // rx
                component: None,
                payload: event_count(1),
            },
            CobaltEvent {
                metric_id: metrics::MLME_RX_TX_FRAME_COUNTS_METRIC_ID,
                event_codes: vec![1], // tx
                component: None,
                payload: event_count(2),
            },
            CobaltEvent {
                metric_id: metrics::MLME_RX_TX_FRAME_BYTES_METRIC_ID,
                event_codes: vec![0], // rx
                component: None,
                payload: event_count(4),
            },
            CobaltEvent {
                metric_id: metrics::MLME_RX_TX_FRAME_BYTES_METRIC_ID,
                event_codes: vec![1], // tx
                component: None,
                payload: event_count(5),
            },
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

    fn event_count(count: i64) -> EventPayload {
        EventPayload::EventCount(CountEvent { period_duration_micros: 0, count })
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

    fn fake_iface_map() -> (IfaceMap, impl Stream<Item = StatsRequest>) {
        let (ifaces_map, _watcher) = IfaceMap::new();
        let (iface_device, stats_requests) = fake_iface_device();
        ifaces_map.insert(IFACE_ID, iface_device);
        (ifaces_map, stats_requests)
    }

    fn fake_iface_device() -> (IfaceDevice, impl Stream<Item = StatsRequest>) {
        let (sme_sender, _sme_receiver) = mpsc::unbounded();
        let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
        let (proxy, _server) = create_proxy::<MlmeMarker>().expect("Error creating proxy");
        let mlme_query = MlmeQueryProxy::new(proxy);
        let device_info = fake_device_info();
        let iface_device = IfaceDevice {
            sme_server: device::SmeServer::Client(sme_sender),
            stats_sched,
            device: None,
            mlme_query,
            device_info,
        };
        (iface_device, stats_requests)
    }

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_mlme::MacRole::Client,
            bands: vec![],
            mac_addr: [0xAC; 6],
            driver_features: vec![],
        }
    }

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }
}

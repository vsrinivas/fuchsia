// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fdio;
use fidl_fuchsia_cobalt::{
    BucketDistributionEntry, EncoderFactoryMarker, EncoderProxy, ObservationValue, ProjectProfile,
    Status, Value,
};
use fidl_fuchsia_mem as fuchsia_mem;
use fidl_fuchsia_wlan_stats as fidl_stats;
use fidl_fuchsia_wlan_stats::MlmeStats::{ApMlmeStats, ClientMlmeStats};
use fuchsia_async as fasync;
use fuchsia_zircon::DurationNum;
use futures::prelude::*;
use futures::stream::FuturesUnordered;
use futures::StreamExt;
use log::{error, info, log};
use parking_lot::Mutex;
use std::cmp::PartialOrd;
use std::collections::HashMap;
use std::default::Default;
use std::fs::File;
use std::io::Seek;
use std::ops::Sub;
use std::sync::Arc;

use crate::device::{IfaceDevice, IfaceMap};

type StatsRef = Arc<Mutex<fidl_stats::IfaceStats>>;

const REPORT_PERIOD_MINUTES: i64 = 1;

const COBALT_CONFIG_PATH: &'static str = "/pkg/data/cobalt_config.binproto";

// These IDs must match the Cobalt config from //third_party/cobalt_config/fuchsia/wlan/config.yaml
enum CobaltMetricId {
    DispatcherPacketCounter = 5,
    ClientAssocDataRssi = 6,
    ClientBeaconRssi = 7,
}
enum CobaltEncodingId {
    RawEncoding = 1,
}

async fn get_cobalt_encoder() -> Result<EncoderProxy, Error> {
    let (proxy, server_end) =
        fidl::endpoints2::create_endpoints().context("Failed to create endpoints")?;
    let encoder_factory = fuchsia_app::client::connect_to_service::<EncoderFactoryMarker>()
        .context("Failed to connect to the Cobalt EncoderFactory")?;

    let mut cobalt_config = File::open(COBALT_CONFIG_PATH)?;
    let vmo = fdio::get_vmo_copy_from_file(&cobalt_config)?;
    let size = cobalt_config.seek(std::io::SeekFrom::End(0))?;

    let config = fuchsia_mem::Buffer { vmo, size };
    let resp =
        await!(encoder_factory.get_encoder_for_project(&mut ProjectProfile { config }, server_end));

    match resp {
        Ok(Status::Ok) => Ok(proxy),
        Ok(other) => Err(format_err!("Failed to obtain Encoder: {:?}", other)),
        Err(e) => Err(format_err!("Failed to obtain Encoder: {}", e)),
    }
}

// Export stats to Cobalt every REPORT_PERIOD_MINUTES.
pub async fn report_telemetry_periodically(ifaces_map: Arc<IfaceMap>) {
    // TODO(NET-1386): Make this module resilient to Wlanstack2 downtime.

    info!("Telemetry started");
    let mut last_reported_stats: HashMap<u16, StatsRef> = HashMap::new();
    let mut interval_stream = fasync::Interval::new(REPORT_PERIOD_MINUTES.minutes());
    while let Some(_) = await!(interval_stream.next()) {
        let mut futures = FuturesUnordered::new();
        for (id, iface) in ifaces_map.get_snapshot().iter() {
            let fut = handle_iface(
                *id,
                last_reported_stats.get(id).map(|r| Arc::clone(r)),
                Arc::clone(&iface),
            );
            futures.push(fut);
        }
        while let Some((id, stats_result)) = await!(futures.next()) {
            match stats_result {
                Some(reported_stats) => last_reported_stats.insert(id, reported_stats),
                None => last_reported_stats.remove(&id),
            };
        }
    }
}

async fn handle_iface(
    id: u16, last_stats_opt: Option<StatsRef>, iface: Arc<IfaceDevice>,
) -> (u16, Option<StatsRef>) {
    let r = await!(try_handle_iface(id, last_stats_opt, Arc::clone(&iface)));
    match r {
        Ok((id, reported_stats)) => (id, Some(reported_stats)),
        Err(e) => {
            error!(
                "Failed to report telemetry for iface '{}': {}",
                iface.device.path().display(),
                e
            );
            (id, None)
        }
    }
}

async fn try_handle_iface(
    id: u16, last_stats_opt: Option<StatsRef>, iface: Arc<IfaceDevice>,
) -> Result<(u16, StatsRef), Error> {
    let encoder_proxy = await!(get_cobalt_encoder())?;
    let current_stats = await!(iface.stats_sched.get_stats())?;
    if let Some(last_stats) = last_stats_opt {
        await!(report_stats(
            last_stats,
            Arc::clone(&current_stats),
            &encoder_proxy
        ));
    }
    Ok((id, current_stats))
}

async fn report_stats(last_stats: StatsRef, current_stats: StatsRef, encoder_proxy: &EncoderProxy) {
    await!(report_mlme_stats(
        Arc::clone(&last_stats),
        Arc::clone(&current_stats),
        encoder_proxy.clone()
    ));

    let last_stats_guard = last_stats.lock();
    let current_stats_guard = current_stats.lock();

    await!(report_dispatcher_stats(
        &last_stats_guard.dispatcher_stats,
        &current_stats_guard.dispatcher_stats,
        encoder_proxy.clone()
    ));
}

fn report_dispatcher_stats(
    last_stats: &fidl_stats::DispatcherStats, current_stats: &fidl_stats::DispatcherStats,
    encoder_proxy: EncoderProxy,
) -> impl Future<Output = ()> {
    // These indexes must match the Cobalt config from
    // //third_party/cobalt_config/fuchsia/wlan/config.yaml
    const DISPATCHER_IN_PACKET_COUNT_INDEX: u32 = 0;
    const DISPATCHER_OUT_PACKET_COUNT_INDEX: u32 = 1;
    const DISPATCHER_DROP_PACKET_COUNT_INDEX: u32 = 2;

    let report_in_fut = report_dispatcher_packets(
        DISPATCHER_IN_PACKET_COUNT_INDEX,
        get_diff(
            last_stats.any_packet.in_.count,
            current_stats.any_packet.in_.count,
        ),
        encoder_proxy.clone(),
    );
    let report_out_fut = report_dispatcher_packets(
        DISPATCHER_OUT_PACKET_COUNT_INDEX,
        get_diff(
            last_stats.any_packet.out.count,
            current_stats.any_packet.out.count,
        ),
        encoder_proxy.clone(),
    );
    let report_drop_fut = report_dispatcher_packets(
        DISPATCHER_DROP_PACKET_COUNT_INDEX,
        get_diff(
            last_stats.any_packet.drop.count,
            current_stats.any_packet.drop.count,
        ),
        encoder_proxy.clone(),
    );

    report_in_fut
        .join3(report_out_fut, report_drop_fut)
        .map(|_| ())
}

async fn report_dispatcher_packets(
    packet_type_index: u32, packet_count: u64, encoder_proxy: EncoderProxy,
) {
    let index_value = ObservationValue {
        name: String::from("packet_type_index"),
        value: Value::IndexValue(packet_type_index),
        encoding_id: CobaltEncodingId::RawEncoding as u32,
    };
    let count_value = ObservationValue {
        name: String::from("packet_count"),
        value: Value::IntValue(packet_count as i64),
        encoding_id: CobaltEncodingId::RawEncoding as u32,
    };
    let mut values = vec![index_value, count_value];

    let resp = await!(encoder_proxy.add_multipart_observation(
        CobaltMetricId::DispatcherPacketCounter as u32,
        &mut values.iter_mut()
    ));
    handle_cobalt_response(resp, CobaltMetricId::DispatcherPacketCounter as u32);
}

async fn report_mlme_stats(
    last_stats: StatsRef, current_stats: StatsRef, encoder_proxy: EncoderProxy,
) {
    let last_stats_guard = last_stats.lock();
    let current_stats_guard = current_stats.lock();

    let last = &last_stats_guard.mlme_stats;
    let current = &current_stats_guard.mlme_stats;
    if let (Some(ref last), Some(ref current)) = (last, current) {
        match (last.as_ref(), current.as_ref()) {
            (ClientMlmeStats(last), ClientMlmeStats(current)) => await!(
                report_client_mlme_stats(&last, &current, encoder_proxy.clone())
            ),
            (ApMlmeStats(_), ApMlmeStats(_)) => {}
            _ => error!("Current MLME stats type is different from the last MLME stats type"),
        };
    }
}

fn report_client_mlme_stats<'a>(
    last_stats: &'a fidl_stats::ClientMlmeStats, current_stats: &'a fidl_stats::ClientMlmeStats,
    encoder_proxy: EncoderProxy,
) -> impl Future<Output = ()> + 'a {
    let report_assoc_rssi_fut = report_rssi_stats(
        CobaltMetricId::ClientAssocDataRssi as u32,
        &last_stats.assoc_data_rssi,
        &current_stats.assoc_data_rssi,
        encoder_proxy.clone(),
    );
    let report_beacon_rssi_fut = report_rssi_stats(
        CobaltMetricId::ClientBeaconRssi as u32,
        &last_stats.beacon_rssi,
        &current_stats.beacon_rssi,
        encoder_proxy.clone(),
    );

    report_assoc_rssi_fut
        .join(report_beacon_rssi_fut)
        .map(|_| ())
}

fn report_rssi_stats(
    rssi_metric_id: u32, last_stats: &fidl_stats::RssiStats, current_stats: &fidl_stats::RssiStats,
    encoder_proxy: EncoderProxy,
) -> impl Future<Output = ()> {
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

    let mut distribution = Vec::new();
    for bin in 0..current_stats.hist.len() {
        let diff = get_diff(last_stats.hist[bin], current_stats.hist[bin]);
        if diff > 0 {
            let entry = BucketDistributionEntry {
                index: (fidl_stats::RSSI_BINS - (bin as u8) - 1).into(),
                count: diff.into(),
            };
            distribution.push(entry);
        }
    }

    report_int_bucket_distribution(distribution, rssi_metric_id, encoder_proxy)
}

async fn report_int_bucket_distribution(
    mut distribution: Vec<BucketDistributionEntry>, rssi_metric_id: u32,
    encoder_proxy: EncoderProxy,
) {
    if !distribution.is_empty() {
        let resp = await!(encoder_proxy.add_int_bucket_distribution(
            rssi_metric_id,
            CobaltEncodingId::RawEncoding as u32,
            &mut distribution.iter_mut()
        ));
        handle_cobalt_response(resp, rssi_metric_id);
    }
}

fn handle_cobalt_response(resp: Result<Status, fidl::Error>, metric_id: u32) {
    match resp {
        Ok(Status::Ok) => {}
        Ok(other) => error!(
            "Cobalt returned an error for metric {}: {:?}",
            metric_id, other
        ),
        Err(e) => error!(
            "Failed to send observation to Cobalt for metric {}: {}",
            metric_id, e
        ),
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

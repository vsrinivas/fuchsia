// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl_fuchsia_cobalt::{EncoderFactoryMarker, EncoderProxy, ObservationValue, Status, Value};
use fidl_fuchsia_wlan_stats as fidl_stats;
use fuchsia_async as fasync;
use fuchsia_zircon::DurationNum;
use futures::join;
use futures::stream::FuturesUnordered;
use futures::StreamExt;
use log::{error, log};
use parking_lot::Mutex;
use std::sync::Arc;

use crate::device::{IfaceDevice, IfaceMap};

const REPORT_PERIOD_MINUTES: i64 = 1;

// This ID must match the Cobalt config from //third_party/cobalt_config/projects.yaml
const COBALT_PROJECT_ID: i32 = 106;

// These IDs must match the Cobalt config from //third_party/cobalt_config/fuchsia/wlan/config.yaml
enum CobaltMetricId {
    DispatcherPacketCounter = 5,
}
enum CobaltEncodingId {
    RawEncoding = 1,
}

fn get_cobalt_encoder() -> Result<EncoderProxy, Error> {
    let (proxy, server_end) =
        fidl::endpoints2::create_endpoints().context("Failed to create endpoints")?;
    let encoder_factory = fuchsia_app::client::connect_to_service::<EncoderFactoryMarker>()
        .context("Failed to connect to the Cobalt EncoderFactory")?;
    encoder_factory
        .get_encoder(COBALT_PROJECT_ID, server_end)
        .context("Failed to get Cobalt Encoder")?;
    Ok(proxy)
}

// Export DispatcherStats to Cobalt every REPORT_PERIOD_MINUTES.
pub async fn report_telemetry_periodically(ifaces_map: Arc<IfaceMap>) {
    let mut interval_stream = fasync::Interval::new(REPORT_PERIOD_MINUTES.minutes());
    while let Some(_) = await!(interval_stream.next()) {
        let mut futures = FuturesUnordered::new();
        for iface in ifaces_map.get_snapshot().values() {
            let fut = handle_iface(Arc::clone(&iface));
            futures.push(fut);
        }
        await!(futures.collect::<()>());
    }
}

async fn handle_iface(iface: Arc<IfaceDevice>) {
    if let Err(e) = await!(try_handle_iface(Arc::clone(&iface))) {
        error!(
            "Failed to report telemetry for iface '{}': {}",
            iface.device.path().display(),
            e
        );
    }
}

async fn try_handle_iface(iface: Arc<IfaceDevice>) -> Result<(), Error> {
    let encoder_proxy = get_cobalt_encoder()?;
    let stats = await!(iface.stats_sched.get_stats())?;
    await!(report_stats(stats, &encoder_proxy));
    Ok(())
}

async fn report_stats(stats: Arc<Mutex<fidl_stats::IfaceStats>>, encoder_proxy: &EncoderProxy) {
    let fidl_stats = stats.lock();
    let report_dispatcher_stats_fut =
        report_dispatcher_stats(&fidl_stats.dispatcher_stats, encoder_proxy.clone());
    // TODO(alexandrew): Report also RSSI stats.
    join!(report_dispatcher_stats_fut);
}

async fn report_dispatcher_stats(
    dispatcher_stats: &fidl_stats::DispatcherStats, encoder_proxy: EncoderProxy,
) {
    // These indexes must match the Cobalt config from
    // //third_party/cobalt_config/fuchsia/wlan/config.yaml
    const DISPATCHER_IN_PACKET_COUNT_INDEX: u32 = 0;
    const DISPATCHER_OUT_PACKET_COUNT_INDEX: u32 = 1;
    const DISPATCHER_DROP_PACKET_COUNT_INDEX: u32 = 2;

    let report_in_fut = report_dispatcher_packets(
        DISPATCHER_IN_PACKET_COUNT_INDEX,
        dispatcher_stats.any_packet.in_.count,
        encoder_proxy.clone(),
    );
    let report_out_fut = report_dispatcher_packets(
        DISPATCHER_OUT_PACKET_COUNT_INDEX,
        dispatcher_stats.any_packet.out.count,
        encoder_proxy.clone(),
    );
    let report_drop_fut = report_dispatcher_packets(
        DISPATCHER_DROP_PACKET_COUNT_INDEX,
        dispatcher_stats.any_packet.drop.count,
        encoder_proxy.clone(),
    );

    join!(report_in_fut, report_out_fut, report_drop_fut);
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
    match resp {
        Ok(Status::Ok) => {}
        Ok(other) => error!("Cobalt returned an error: {:?}", other),
        Err(e) => error!("Failed to send packet count to Cobalt: {}", e),
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fdio;
use fidl_fuchsia_cobalt::{
    HistogramBucket, LoggerExtProxy, LoggerFactoryMarker, LoggerProxy, ProjectProfile2,
    ReleaseStage, Status2,
};
use fidl_fuchsia_mem as fuchsia_mem;
use fidl_fuchsia_wlan_stats as fidl_stats;
use fidl_fuchsia_wlan_stats::MlmeStats::{ApMlmeStats, ClientMlmeStats};
use fuchsia_async as fasync;
use fuchsia_zircon::DurationNum;
use futures::channel::mpsc;
use futures::prelude::*;
use futures::stream::FuturesUnordered;
use futures::{join, StreamExt};
use log::{error, info, log};
use parking_lot::Mutex;
use std::cmp::PartialOrd;
use std::collections::HashMap;
use std::default::Default;
use std::fs::File;
use std::io::Seek;
use std::ops::Sub;
use std::sync::Arc;

use crate::device::IfaceMap;

type StatsRef = Arc<Mutex<fidl_stats::IfaceStats>>;

const COBALT_CONFIG_PATH: &'static str = "/pkg/data/cobalt_config.binproto";

const REPORT_PERIOD_MINUTES: i64 = 1;
const COBALT_BUFFER_SIZE: usize = 100;

// These IDs must match the Cobalt config from //third_party/cobalt_config/fuchsia/wlan/config.yaml
enum CobaltMetricId {
    DispatcherPacketCounter = 5,
    ClientAssocDataRssi = 6,
    ClientBeaconRssi = 7,
    ConnectionTime = 8,
}

enum EventValue {
    Count {
        event_type_index: u32,
        count: i64,
    },
    ElapsedTime {
        event_type_index: u32,
        elapsed_micros: i64,
    },
    IntHistogram {
        values: Vec<HistogramBucket>,
    },
}

pub struct Event {
    metric_id: u32,
    value: EventValue,
}

#[derive(Clone)]
pub struct CobaltSender {
    sender: mpsc::Sender<Event>,
}

impl CobaltSender {
    fn log_event_count(&mut self, metric_id: u32, event_type_index: u32, count: i64) {
        let event_value = EventValue::Count {
            event_type_index,
            count,
        };
        self.log_event(metric_id, event_value);
    }

    fn log_elapsed_time(&mut self, metric_id: u32, event_type_index: u32, elapsed_micros: i64) {
        let event_value = EventValue::ElapsedTime {
            event_type_index,
            elapsed_micros,
        };
        self.log_event(metric_id, event_value);
    }

    fn log_int_histogram(&mut self, metric_id: u32, values: Vec<HistogramBucket>) {
        let event_value = EventValue::IntHistogram { values };
        self.log_event(metric_id, event_value);
    }

    fn log_event(&mut self, metric_id: u32, value: EventValue) {
        let event = Event { metric_id, value };
        if self.sender.try_send(event).is_err() {
            error!("Dropping a Cobalt event because the buffer is full");
        }
    }
}

pub fn serve(ifaces_map: Arc<IfaceMap>) -> (CobaltSender, impl Future<Output = ()>) {
    let (sender, receiver) = mpsc::channel(COBALT_BUFFER_SIZE);
    let sender = CobaltSender { sender };
    let fut = report_telemetry(ifaces_map.clone(), sender.clone(), receiver);
    (sender, fut)
}

async fn get_cobalt_loggers() -> Result<(LoggerProxy, LoggerExtProxy), Error> {
    let (logger_proxy, server_end) =
        fidl::endpoints2::create_endpoints().context("Failed to create endpoints")?;
    let (logger_ext_proxy, ext_server_end) =
        fidl::endpoints2::create_endpoints().context("Failed to create endpoints")?;
    let logger_factory = fuchsia_app::client::connect_to_service::<LoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt LoggerFactory")?;

    let mut cobalt_config = File::open(COBALT_CONFIG_PATH)?;
    let vmo = fdio::get_vmo_copy_from_file(&cobalt_config)?;
    let size = cobalt_config.seek(std::io::SeekFrom::End(0))?;

    let config = fuchsia_mem::Buffer { vmo, size };
    let vmo_ext = fdio::get_vmo_copy_from_file(&cobalt_config)?;
    let config_ext = fuchsia_mem::Buffer { vmo: vmo_ext, size };

    let create_logger_fut = logger_factory.create_logger(
        &mut ProjectProfile2 {
            config,
            release_stage: ReleaseStage::Ga,
        },
        server_end,
    );
    let create_logger_ext_fut = logger_factory.create_logger_ext(
        &mut ProjectProfile2 {
            config: config_ext,
            release_stage: ReleaseStage::Ga,
        },
        ext_server_end,
    );

    let (res, ext_res) = join!(create_logger_fut, create_logger_ext_fut);
    handle_cobalt_factory_result(res, "Failed to obtain Logger")?;
    handle_cobalt_factory_result(ext_res, "Failed to obtain LoggerExt")?;
    Ok((logger_proxy, logger_ext_proxy))
}

fn handle_cobalt_factory_result(
    r: Result<Status2, fidl::Error>, context: &str
) -> Result<(), failure::Error> {
    match r {
        Ok(Status2::Ok) => Ok(()),
        Ok(other) => Err(format_err!("{}: {:?}", context, other)),
        Err(e) => Err(format_err!("{}: {}", context, e)),
    }
}

async fn report_telemetry(
    ifaces_map: Arc<IfaceMap>, sender: CobaltSender, receiver: mpsc::Receiver<Event>,
) {
    info!("Telemetry started");
    let report_periodically_fut = report_telemetry_periodically(ifaces_map.clone(), sender);
    let report_events_fut = report_telemetry_events(receiver);
    join!(report_periodically_fut, report_events_fut);
}

async fn report_telemetry_events(mut receiver: mpsc::Receiver<Event>) {
    let (logger, logger_ext) = match await!(get_cobalt_loggers()) {
        Ok((logger, logger_ext)) => (logger, logger_ext),
        Err(e) => {
            error!("Error obtaining a Cobalt Logger: {}", e);
            return;
        }
    };

    let mut is_full = false;
    while let Some(event) = await!(receiver.next()) {
        match event.value {
            EventValue::Count {
                event_type_index,
                count,
            } => {
                let resp = await!(logger.log_event_count(
                    event.metric_id,
                    event_type_index,
                    "",
                    0, // TODO report a period duration once the backend supports it.
                    count
                ));
                handle_cobalt_response(resp, event.metric_id, &mut is_full);
            }
            EventValue::ElapsedTime {
                event_type_index,
                elapsed_micros,
            } => {
                let resp = await!(logger.log_elapsed_time(
                    event.metric_id,
                    event_type_index,
                    "",
                    elapsed_micros
                ));
                handle_cobalt_response(resp, event.metric_id, &mut is_full);
            }
            EventValue::IntHistogram { mut values } => {
                let resp = await!(logger_ext.log_int_histogram(
                    event.metric_id,
                    0,
                    "",
                    &mut values.iter_mut()
                ));
                handle_cobalt_response(resp, event.metric_id, &mut is_full);
            }
        }
    }
}

// Export MLME stats to Cobalt every REPORT_PERIOD_MINUTES.
async fn report_telemetry_periodically(ifaces_map: Arc<IfaceMap>, mut sender: CobaltSender) {
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
    last_stats: &fidl_stats::IfaceStats, current_stats: &fidl_stats::IfaceStats,
    sender: &mut CobaltSender,
) {
    report_mlme_stats(&last_stats.mlme_stats, &current_stats.mlme_stats, sender);

    report_dispatcher_stats(
        &last_stats.dispatcher_stats,
        &current_stats.dispatcher_stats,
        sender,
    );
}

fn report_dispatcher_stats(
    last_stats: &fidl_stats::DispatcherStats, current_stats: &fidl_stats::DispatcherStats,
    sender: &mut CobaltSender,
) {
    // These indexes must match the Cobalt config from
    // //third_party/cobalt_config/fuchsia/wlan/config.yaml
    const DISPATCHER_IN_PACKET_COUNT_INDEX: u32 = 0;
    const DISPATCHER_OUT_PACKET_COUNT_INDEX: u32 = 1;
    const DISPATCHER_DROP_PACKET_COUNT_INDEX: u32 = 2;

    report_dispatcher_packets(
        DISPATCHER_IN_PACKET_COUNT_INDEX,
        get_diff(
            last_stats.any_packet.in_.count,
            current_stats.any_packet.in_.count,
        ),
        sender,
    );
    report_dispatcher_packets(
        DISPATCHER_OUT_PACKET_COUNT_INDEX,
        get_diff(
            last_stats.any_packet.out.count,
            current_stats.any_packet.out.count,
        ),
        sender,
    );
    report_dispatcher_packets(
        DISPATCHER_DROP_PACKET_COUNT_INDEX,
        get_diff(
            last_stats.any_packet.drop.count,
            current_stats.any_packet.drop.count,
        ),
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
    last: &Option<Box<fidl_stats::MlmeStats>>, current: &Option<Box<fidl_stats::MlmeStats>>,
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
    last_stats: &fidl_stats::ClientMlmeStats, current_stats: &fidl_stats::ClientMlmeStats,
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
}

fn report_rssi_stats(
    rssi_metric_id: u32, last_stats: &fidl_stats::RssiStats, current_stats: &fidl_stats::RssiStats,
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

pub fn report_connection_time(sender: &mut CobaltSender, time: i64, result_index: u32) {
    sender.log_elapsed_time(CobaltMetricId::ConnectionTime as u32, result_index, time);
}

fn handle_cobalt_response(resp: Result<Status2, fidl::Error>, metric_id: u32, is_full: &mut bool) {
    if let Err(e) = throttle_cobalt_error(resp, metric_id, is_full) {
        error!("{}", e);
    }
}

fn throttle_cobalt_error(
    resp: Result<Status2, fidl::Error>, metric_id: u32, is_full: &mut bool,
) -> Result<(), failure::Error> {
    let was_full = *is_full;
    *is_full = resp.as_ref().ok() == Some(&Status2::BufferFull);
    match resp {
        Ok(Status2::BufferFull) => {
            if !was_full {
                Err(format_err!(
                    "Cobalt buffer became full. Cannot report the stats"
                ))
            } else {
                Ok(())
            }
        }
        Ok(Status2::Ok) => Ok(()),
        Ok(other) => Err(format_err!(
            "Cobalt returned an error for metric {}: {:?}",
            metric_id,
            other
        )),
        Err(e) => Err(format_err!(
            "Failed to send event to Cobalt for metric {}: {}",
            metric_id,
            e
        )),
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

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_cobalt::Status2;

    #[test]
    fn throttle_errors() {
        let mut is_full = false;

        let cobalt_resp = Ok(Status2::Ok);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, false);

        let cobalt_resp = Ok(Status2::InvalidArguments);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, false);

        let cobalt_resp = Ok(Status2::BufferFull);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, true);

        let cobalt_resp = Ok(Status2::BufferFull);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, true);

        let cobalt_resp = Ok(Status2::Ok);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, false);

        let cobalt_resp = Err(fidl::Error::ClientWrite(
            fuchsia_zircon::Status::PEER_CLOSED,
        ));
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, false);
    }
}

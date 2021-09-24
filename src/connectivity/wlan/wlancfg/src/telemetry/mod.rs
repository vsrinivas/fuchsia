// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod convert;
mod windowed_stats;

use {
    crate::telemetry::{convert::convert_disconnect_source, windowed_stats::WindowedStats},
    fidl_fuchsia_metrics::{MetricEvent, MetricEventPayload},
    fidl_fuchsia_wlan_sme as fidl_sme,
    fidl_fuchsia_wlan_stats::MlmeStats,
    fuchsia_async as fasync,
    fuchsia_inspect::{Inspector, Node as InspectNode, NumericProperty, UintProperty},
    fuchsia_inspect_contrib::inspect_insert,
    fuchsia_zircon::{self as zx, DurationNum},
    futures::{channel::mpsc, select, Future, FutureExt, StreamExt},
    log::{info, warn},
    num_traits::SaturatingAdd,
    parking_lot::Mutex,
    static_assertions::const_assert_eq,
    std::{
        cmp::max,
        ops::Add,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
    wlan_common::bss::{BssDescription, Protection as BssProtection},
    wlan_metrics_registry as metrics,
};

#[derive(Clone, Debug)]
pub struct TelemetrySender {
    sender: Arc<Mutex<mpsc::Sender<TelemetryEvent>>>,
    sender_is_blocked: Arc<AtomicBool>,
}

impl TelemetrySender {
    pub fn new(sender: mpsc::Sender<TelemetryEvent>) -> Self {
        Self {
            sender: Arc::new(Mutex::new(sender)),
            sender_is_blocked: Arc::new(AtomicBool::new(false)),
        }
    }

    // Send telemetry event. Log an error if it fails
    pub fn send(&self, event: TelemetryEvent) {
        match self.sender.lock().try_send(event) {
            Ok(_) => {
                // If sender has been blocked before, set bool to false and log message
                if let Ok(_) = self.sender_is_blocked.compare_exchange(
                    true,
                    false,
                    Ordering::SeqCst,
                    Ordering::SeqCst,
                ) {
                    info!("TelemetrySender recovered and resumed sending");
                }
            }
            Err(_) => {
                // If sender has not been blocked before, set bool to true and log error message
                if let Ok(_) = self.sender_is_blocked.compare_exchange(
                    false,
                    true,
                    Ordering::SeqCst,
                    Ordering::SeqCst,
                ) {
                    warn!("TelemetrySender dropped a msg: either buffer is full or no receiver is waiting");
                }
            }
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct DisconnectInfo {
    pub connected_duration: zx::Duration,
    pub is_sme_reconnecting: bool,
    pub reason_code: u16,
    pub disconnect_source: fidl_sme::DisconnectSource,
    pub latest_ap_state: BssDescription,
}

#[derive(Clone, Debug, PartialEq)]
pub enum TelemetryEvent {
    /// Notify the telemetry event loop that network selection is complete.
    NetworkSelectionDecision {
        /// Type of network selection. If it's undirected and no candidate network is found,
        /// telemetry will toggle the "no saved neighbor" flag.
        network_selection_type: NetworkSelectionType,
        /// When there's a scan error, `num_candidates` should be Err.
        /// When `num_candidates` is `Ok(0)` for an undirected network selection, telemetry
        /// will toggle the "no saved neighbor" flag.  If the event loop is tracking downtime,
        /// the subsequent downtime period will also be used to increment the,
        /// `downtime_no_saved_neighbor_duration` counter. This counter is used to
        /// adjust the raw downtime.
        num_candidates: Result<usize, ()>,
        /// Whether a network has been selected. This field is currently unused.
        selected_any: bool,
    },
    /// Notify the telemetry event loop that the client has connected.
    /// Subsequently, the telemetry event loop will increment the `connected_duration` counter
    /// periodically.
    Connected { iface_id: u16, latest_ap_state: BssDescription },
    /// Notify the telemetry event loop that the client has disconnected.
    /// Subsequently, the telemetry event loop will increment the downtime counters periodically
    /// if TelemetrySender has requested downtime to be tracked via `track_subsequent_downtime`
    /// flag.
    Disconnected {
        /// Indicates whether subsequent period should be used to increment the downtime counters.
        track_subsequent_downtime: bool,
        info: DisconnectInfo,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub enum NetworkSelectionType {
    /// Looking for the best BSS from any saved networks
    Undirected,
    /// Looking for the best BSS for a particular network
    Directed,
}

/// Capacity of "first come, first serve" slots available to clients of
/// the mpsc::Sender<TelemetryEvent>.
const TELEMETRY_EVENT_BUFFER_SIZE: usize = 100;
/// How often to request RSSI stats and dispatcher packet counts from MLME.
const TELEMETRY_QUERY_INTERVAL: zx::Duration = zx::Duration::from_seconds(15);

/// Create a struct for sending TelemetryEvent, and a future representing the telemetry loop.
///
/// Every 15 seconds, the telemetry loop will query for MLME/PHY stats and update various
/// time-interval stats. The telemetry loop also handles incoming TelemetryEvent to update
/// the appropriate stats.
pub fn serve_telemetry(
    dev_svc_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    inspect_node: InspectNode,
) -> (TelemetrySender, impl Future<Output = ()>) {
    let (sender, mut receiver) = mpsc::channel::<TelemetryEvent>(TELEMETRY_EVENT_BUFFER_SIZE);
    let fut = async move {
        let mut report_interval_stream = fasync::Interval::new(TELEMETRY_QUERY_INTERVAL);
        const ONE_HOUR: zx::Duration = zx::Duration::from_hours(1);
        const_assert_eq!(ONE_HOUR.into_nanos() % TELEMETRY_QUERY_INTERVAL.into_nanos(), 0);
        const INTERVAL_TICKS_PER_HR: u64 =
            (ONE_HOUR.into_nanos() / TELEMETRY_QUERY_INTERVAL.into_nanos()) as u64;
        const INTERVAL_TICKS_PER_DAY: u64 = INTERVAL_TICKS_PER_HR * 24;
        let mut interval_tick = 0u64;
        let mut telemetry = Telemetry::new(dev_svc_proxy, cobalt_1dot1_proxy, inspect_node);
        loop {
            select! {
                event = receiver.next() => {
                    if let Some(event) = event {
                        telemetry.handle_telemetry_event(event).await;
                    }
                }
                _ = report_interval_stream.next() => {
                    telemetry.handle_periodic_telemetry().await;

                    interval_tick += 1;
                    if interval_tick % INTERVAL_TICKS_PER_DAY == 0 {
                        telemetry.log_daily_cobalt_metrics().await;
                    }

                    // This ensures that `signal_hr_passed` is always called after
                    // `handle_periodic_telemetry` at the hour mark. This helps with
                    // ease of testing. Additionally, logging to Cobalt before sliding
                    // the window ensures that Cobalt uses the last 24 hours of data
                    // rather than 23 hours.
                    if interval_tick % INTERVAL_TICKS_PER_HR == 0 {
                        telemetry.signal_hr_passed().await;
                    }
                }
            }
        }
    };
    (TelemetrySender::new(sender), fut)
}

#[derive(Debug, Clone)]
enum ConnectionState {
    // Like disconnected, but no downtime is tracked.
    Idle,
    Connected(ConnectedState),
    Disconnected(DisconnectedState),
}

#[derive(Debug, Clone)]
struct ConnectedState {
    iface_id: u16,
    prev_counters: Option<fidl_fuchsia_wlan_stats::IfaceStats>,
}

#[derive(Debug, Clone)]
struct DisconnectedState {
    disconnected_since: fasync::Time,
    disconnect_info: DisconnectInfo,
    /// The latest time when the device's no saved neighbor duration was accounted.
    /// If this has a value, then conceptually we say that "no saved neighbor" flag
    /// is set.
    latest_no_saved_neighbor_time: Option<fasync::Time>,
    accounted_no_saved_neighbor_duration: zx::Duration,
}

fn record_inspect_counters(
    inspect_node: &InspectNode,
    child_name: &str,
    counters: Arc<Mutex<WindowedStats<StatCounters>>>,
) {
    inspect_node.record_lazy_child(child_name, move || {
        let counters = Arc::clone(&counters);
        async move {
            let inspector = Inspector::new();
            {
                let counters_mutex_guard = counters.lock();
                let counters = counters_mutex_guard.windowed_stat(None);
                inspect_insert!(inspector.root(), {
                    total_duration: counters.total_duration.into_nanos(),
                    connected_duration: counters.connected_duration.into_nanos(),
                    downtime_duration: counters.downtime_duration.into_nanos(),
                    downtime_no_saved_neighbor_duration: counters.downtime_no_saved_neighbor_duration.into_nanos(),
                    disconnect_count: counters.disconnect_count,
                    tx_high_packet_drop_duration: counters.tx_high_packet_drop_duration.into_nanos(),
                    rx_high_packet_drop_duration: counters.rx_high_packet_drop_duration.into_nanos(),
                    no_rx_duration: counters.no_rx_duration.into_nanos(),
                });
            }
            Ok(inspector)
        }
        .boxed()
    });
}

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

macro_rules! log_cobalt_1dot1_batch {
    ($cobalt_proxy:expr, $events:expr, $context:expr $(,)?) => {{
        let status = $cobalt_proxy.log_metric_events($events).await;
        match status {
            Ok(fidl_fuchsia_metrics::Status::Ok) => (),
            Ok(s) => warn!("Failed logging batch metrics, context: {}, status: {:?}", $context, s),
            Err(e) => warn!("Failed logging batch metrics, context: {}, error: {}", $context, e),
        }
    }};
}

pub struct Telemetry {
    dev_svc_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    connection_state: ConnectionState,
    last_checked_connection_state: fasync::Time,
    stats_logger: StatsLogger,
    _inspect_node: InspectNode,
    get_iface_stats_fail_count: UintProperty,
}

impl Telemetry {
    pub fn new(
        dev_svc_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
        cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
        inspect_node: InspectNode,
    ) -> Self {
        let stats_logger = StatsLogger::new(cobalt_1dot1_proxy);
        record_inspect_counters(
            &inspect_node,
            "1d_counters",
            Arc::clone(&stats_logger.last_1d_stats),
        );
        record_inspect_counters(
            &inspect_node,
            "7d_counters",
            Arc::clone(&stats_logger.last_7d_stats),
        );
        let get_iface_stats_fail_count = inspect_node.create_uint("get_iface_stats_fail_count", 0);
        Self {
            dev_svc_proxy,
            connection_state: ConnectionState::Idle,
            last_checked_connection_state: fasync::Time::now(),
            stats_logger,
            _inspect_node: inspect_node,
            get_iface_stats_fail_count,
        }
    }

    pub async fn handle_periodic_telemetry(&mut self) {
        let now = fasync::Time::now();
        let duration = now - self.last_checked_connection_state;

        self.stats_logger.log_stat(StatOp::AddTotalDuration(duration)).await;
        self.stats_logger.log_queued_stats().await;

        match &mut self.connection_state {
            ConnectionState::Idle => (),
            ConnectionState::Connected(state) => {
                self.stats_logger.log_stat(StatOp::AddConnectedDuration(duration)).await;
                match self.dev_svc_proxy.get_iface_stats(state.iface_id).await {
                    Ok((zx::sys::ZX_OK, Some(stats))) => {
                        if let Some(prev_counters) = state.prev_counters.as_ref() {
                            diff_and_log_counters(
                                &mut self.stats_logger,
                                prev_counters,
                                &stats,
                                duration,
                            )
                            .await;
                        }
                        let _prev = state.prev_counters.replace(*stats);
                    }
                    _ => {
                        self.get_iface_stats_fail_count.add(1);
                        let _ = state.prev_counters.take();
                    }
                }
            }
            ConnectionState::Disconnected(state) => {
                self.stats_logger.log_stat(StatOp::AddDowntimeDuration(duration)).await;
                if let Some(prev) = state.latest_no_saved_neighbor_time.take() {
                    let duration = now - prev;
                    state.accounted_no_saved_neighbor_duration += duration;
                    self.stats_logger
                        .log_stat(StatOp::AddDowntimeNoSavedNeighborDuration(duration))
                        .await;
                    state.latest_no_saved_neighbor_time = Some(now);
                }
            }
        }
        self.last_checked_connection_state = now;
    }

    pub async fn handle_telemetry_event(&mut self, event: TelemetryEvent) {
        let now = fasync::Time::now();
        match event {
            TelemetryEvent::NetworkSelectionDecision {
                network_selection_type,
                num_candidates,
                ..
            } => {
                match num_candidates {
                    Ok(n) if n > 0 => {
                        // Saved neighbors are seen, so clear the "no saved neighbor" flag. Account
                        // for any untracked time to the `downtime_no_saved_neighbor_duration`
                        // counter.
                        if let ConnectionState::Disconnected(state) = &mut self.connection_state {
                            if let Some(prev) = state.latest_no_saved_neighbor_time.take() {
                                let duration = now - prev;
                                state.accounted_no_saved_neighbor_duration += duration;
                                self.stats_logger.queue_stat_op(
                                    StatOp::AddDowntimeNoSavedNeighborDuration(duration),
                                );
                            }
                        }
                    }
                    Ok(0) if network_selection_type == NetworkSelectionType::Undirected => {
                        // No saved neighbor is seen. If "no saved neighbor" flag isn't set, then
                        // set it to the current time. Otherwise, do nothing because the telemetry
                        // loop will account for untracked downtime during periodic telemetry run.
                        if let ConnectionState::Disconnected(state) = &mut self.connection_state {
                            if state.latest_no_saved_neighbor_time.is_none() {
                                state.latest_no_saved_neighbor_time = Some(now);
                            }
                        }
                    }
                    _ => (),
                }
            }
            TelemetryEvent::Connected { iface_id, latest_ap_state } => {
                self.stats_logger.log_device_connected_cobalt_metrics(&latest_ap_state).await;
                if let ConnectionState::Disconnected(state) = &self.connection_state {
                    if state.latest_no_saved_neighbor_time.is_some() {
                        warn!("'No saved neighbor' flag still set even though connected");
                    }
                    self.stats_logger.queue_stat_op(StatOp::AddDowntimeDuration(
                        now - self.last_checked_connection_state,
                    ));
                    let total_downtime = now - state.disconnected_since;
                    if total_downtime < state.accounted_no_saved_neighbor_duration {
                        warn!(
                            "Total downtime is less than no-saved-neighbor duration. \
                               Total downtime: {:?}, No saved neighbor duration: {:?}",
                            total_downtime, state.accounted_no_saved_neighbor_duration
                        )
                    }
                    let adjusted_downtime = max(
                        total_downtime - state.accounted_no_saved_neighbor_duration,
                        0.seconds(),
                    );
                    self.stats_logger
                        .log_downtime_cobalt_metrics(adjusted_downtime, &state.disconnect_info)
                        .await;
                }
                self.connection_state =
                    ConnectionState::Connected(ConnectedState { iface_id, prev_counters: None });
                self.last_checked_connection_state = now;
            }
            TelemetryEvent::Disconnected { track_subsequent_downtime, info } => {
                self.stats_logger.log_stat(StatOp::AddDisconnectCount).await;
                self.stats_logger.log_disconnect_cobalt_metrics(&info).await;

                let duration = now - self.last_checked_connection_state;
                if let ConnectionState::Connected { .. } = self.connection_state {
                    self.stats_logger.queue_stat_op(StatOp::AddConnectedDuration(duration));
                }
                self.connection_state = if track_subsequent_downtime {
                    ConnectionState::Disconnected(DisconnectedState {
                        disconnected_since: now,
                        disconnect_info: info,
                        // We assume that there's a saved neighbor in vicinity until proven
                        // otherwise from scan result.
                        latest_no_saved_neighbor_time: None,
                        accounted_no_saved_neighbor_duration: 0.seconds(),
                    })
                } else {
                    ConnectionState::Idle
                };
                self.last_checked_connection_state = now;
            }
        }
    }

    pub async fn log_daily_cobalt_metrics(&mut self) {
        self.stats_logger.log_daily_cobalt_metrics().await;
    }

    pub async fn signal_hr_passed(&mut self) {
        self.stats_logger.handle_hr_passed().await;
    }
}

// Convert float to an integer in "ten thousandth" unit
// Example: 0.02f64 (i.e. 2%) -> 200 per ten thousand
fn float_to_ten_thousandth(value: f64) -> i64 {
    (value * 10000f64) as i64
}

const PACKET_DROP_RATE_THRESHOLD: f64 = 0.02;

const DEVICE_LOW_UPTIME_THRESHOLD: f64 = 0.95;
/// Threshold for high number of disconnects per day connected.
/// Example: if threshold is 12 and a device has 1 disconnect after being connected for less
///          than 2 hours, then it has high DPDC ratio.
const DEVICE_HIGH_DPDC_THRESHOLD: f64 = 12.0;
/// Note: Threshold is for "percentage of time" the device has high packet drop rate.
///       That is, if threshold is 0.10, then the device passes that threshold if more
///       than 10% of the time it has high packet drop rate.
const DEVICE_FREQUENT_HIGH_PACKET_DROP_RATE_THRESHOLD: f64 = 0.10;
/// TODO(fxbug.dev/83621): Adjust this threshold when we consider unicast frames only
const DEVICE_FREQUENT_NO_RX_THRESHOLD: f64 = 0.01;

async fn diff_and_log_counters(
    stats_logger: &mut StatsLogger,
    prev: &fidl_fuchsia_wlan_stats::IfaceStats,
    current: &fidl_fuchsia_wlan_stats::IfaceStats,
    duration: zx::Duration,
) {
    let (prev, current) = match (&prev.mlme_stats, &current.mlme_stats) {
        (Some(ref prev), Some(ref current)) => match (prev.as_ref(), current.as_ref()) {
            (MlmeStats::ClientMlmeStats(prev), MlmeStats::ClientMlmeStats(current)) => {
                (prev, current)
            }
            _ => return,
        },
        _ => return,
    };

    let tx_total = current.tx_frame.in_.count - prev.tx_frame.in_.count;
    let tx_drop = current.tx_frame.drop.count - prev.tx_frame.drop.count;
    let rx_total = current.rx_frame.in_.count - prev.rx_frame.in_.count;
    let rx_drop = current.rx_frame.drop.count - prev.rx_frame.drop.count;

    let tx_drop_rate = if tx_total > 0 { tx_drop as f64 / tx_total as f64 } else { 0f64 };
    let rx_drop_rate = if rx_total > 0 { rx_drop as f64 / rx_total as f64 } else { 0f64 };

    if tx_drop_rate > PACKET_DROP_RATE_THRESHOLD {
        stats_logger.log_stat(StatOp::AddTxHighPacketDropDuration(duration)).await;
    }
    if rx_drop_rate > PACKET_DROP_RATE_THRESHOLD {
        stats_logger.log_stat(StatOp::AddRxHighPacketDropDuration(duration)).await;
    }
    if rx_total == 0 {
        stats_logger.log_stat(StatOp::AddNoRxDuration(duration)).await;
    }
}

struct StatsLogger {
    cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    last_1d_stats: Arc<Mutex<WindowedStats<StatCounters>>>,
    last_7d_stats: Arc<Mutex<WindowedStats<StatCounters>>>,
    stat_ops: Vec<StatOp>,
    hr_tick: u32,
}

impl StatsLogger {
    pub fn new(cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy) -> Self {
        Self {
            cobalt_1dot1_proxy,
            last_1d_stats: Arc::new(Mutex::new(WindowedStats::new(24))),
            last_7d_stats: Arc::new(Mutex::new(WindowedStats::new(7))),
            stat_ops: vec![],
            hr_tick: 0,
        }
    }

    async fn log_stat(&mut self, stat_op: StatOp) {
        let zero = StatCounters::default();
        let addition = match stat_op {
            StatOp::AddTotalDuration(duration) => StatCounters { total_duration: duration, ..zero },
            StatOp::AddConnectedDuration(duration) => {
                StatCounters { connected_duration: duration, ..zero }
            }
            StatOp::AddDowntimeDuration(duration) => {
                StatCounters { downtime_duration: duration, ..zero }
            }
            StatOp::AddDowntimeNoSavedNeighborDuration(duration) => {
                StatCounters { downtime_no_saved_neighbor_duration: duration, ..zero }
            }
            StatOp::AddDisconnectCount => StatCounters { disconnect_count: 1, ..zero },
            StatOp::AddTxHighPacketDropDuration(duration) => {
                StatCounters { tx_high_packet_drop_duration: duration, ..zero }
            }
            StatOp::AddRxHighPacketDropDuration(duration) => {
                StatCounters { rx_high_packet_drop_duration: duration, ..zero }
            }
            StatOp::AddNoRxDuration(duration) => StatCounters { no_rx_duration: duration, ..zero },
        };
        self.last_1d_stats.lock().saturating_add(&addition);
        self.last_7d_stats.lock().saturating_add(&addition);
    }

    // Queue stat operation to be logged later. This allows the caller to control the timing of
    // when stats are logged. This ensures that various counters are not inconsistent with each
    // other because one is logged early and the other one later.
    fn queue_stat_op(&mut self, stat_op: StatOp) {
        self.stat_ops.push(stat_op);
    }

    async fn log_queued_stats(&mut self) {
        while let Some(stat_op) = self.stat_ops.pop() {
            self.log_stat(stat_op).await;
        }
    }

    async fn log_daily_cobalt_metrics(&mut self) {
        self.log_daily_1d_cobalt_metrics().await;
        self.log_daily_7d_cobalt_metrics().await;
    }

    async fn log_daily_1d_cobalt_metrics(&mut self) {
        let mut metric_events = vec![];

        let c = self.last_1d_stats.lock().windowed_stat(None);
        let uptime_ratio = c.connected_duration.into_seconds() as f64
            / (c.connected_duration + c.adjusted_downtime()).into_seconds() as f64;
        if uptime_ratio.is_finite() {
            metric_events.push(MetricEvent {
                metric_id: metrics::CONNECTED_UPTIME_RATIO_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::IntegerValue(float_to_ten_thousandth(uptime_ratio)),
            });

            if uptime_ratio < DEVICE_LOW_UPTIME_THRESHOLD {
                metric_events.push(MetricEvent {
                    metric_id: metrics::DEVICE_WITH_LOW_UPTIME_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }

            let uptime_ratio_dim = {
                use metrics::ConnectivityWlanMetricDimensionUptimeRatio::*;
                match uptime_ratio {
                    x if x < 0.75 => LessThan75Percent,
                    x if x < 0.90 => _75ToLessThan90Percent,
                    x if x < 0.95 => _90ToLessThan95Percent,
                    x if x < 0.98 => _95ToLessThan98Percent,
                    x if x < 0.99 => _98ToLessThan99Percent,
                    x if x < 0.995 => _99ToLessThan99_5Percent,
                    _ => _99_5To100Percent,
                }
            };
            metric_events.push(MetricEvent {
                metric_id: metrics::DEVICE_CONNECTED_UPTIME_RATIO_BREAKDOWN_METRIC_ID,
                event_codes: vec![uptime_ratio_dim as u32],
                payload: MetricEventPayload::Count(1),
            });
        }

        let connected_dur_in_day = c.connected_duration.into_seconds() as f64 / (24 * 3600) as f64;
        let dpdc_ratio = c.disconnect_count as f64 / connected_dur_in_day;
        if dpdc_ratio.is_finite() {
            metric_events.push(MetricEvent {
                metric_id: metrics::DISCONNECT_PER_DAY_CONNECTED_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::IntegerValue(float_to_ten_thousandth(dpdc_ratio)),
            });

            if dpdc_ratio > DEVICE_HIGH_DPDC_THRESHOLD {
                metric_events.push(MetricEvent {
                    metric_id: metrics::DEVICE_WITH_HIGH_DISCONNECT_RATE_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }

            let dpdc_ratio_dim = {
                use metrics::DeviceDisconnectPerDayConnectedBreakdownMetricDimensionDpdcRatio::*;
                match dpdc_ratio {
                    x if x == 0.0 => _0,
                    x if x <= 1.5 => UpTo1_5,
                    x if x <= 3.0 => UpTo3,
                    x if x <= 5.0 => UpTo5,
                    x if x <= 10.0 => UpTo10,
                    _ => MoreThan10,
                }
            };
            metric_events.push(MetricEvent {
                metric_id: metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_METRIC_ID,
                event_codes: vec![dpdc_ratio_dim as u32],
                payload: MetricEventPayload::Count(1),
            });
        }

        let high_rx_drop_time_ratio = c.rx_high_packet_drop_duration.into_seconds() as f64
            / c.connected_duration.into_seconds() as f64;
        if high_rx_drop_time_ratio.is_finite() {
            metric_events.push(MetricEvent {
                metric_id: metrics::TIME_RATIO_WITH_HIGH_RX_PACKET_DROP_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::IntegerValue(float_to_ten_thousandth(
                    high_rx_drop_time_ratio,
                )),
            });

            if high_rx_drop_time_ratio > DEVICE_FREQUENT_HIGH_PACKET_DROP_RATE_THRESHOLD {
                metric_events.push(MetricEvent {
                    metric_id: metrics::DEVICE_WITH_FREQUENT_HIGH_RX_PACKET_DROP_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }
        }

        let high_tx_drop_time_ratio = c.tx_high_packet_drop_duration.into_seconds() as f64
            / c.connected_duration.into_seconds() as f64;
        if high_tx_drop_time_ratio.is_finite() {
            metric_events.push(MetricEvent {
                metric_id: metrics::TIME_RATIO_WITH_HIGH_TX_PACKET_DROP_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::IntegerValue(float_to_ten_thousandth(
                    high_tx_drop_time_ratio,
                )),
            });

            if high_tx_drop_time_ratio > DEVICE_FREQUENT_HIGH_PACKET_DROP_RATE_THRESHOLD {
                metric_events.push(MetricEvent {
                    metric_id: metrics::DEVICE_WITH_FREQUENT_HIGH_TX_PACKET_DROP_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }
        }

        let no_rx_time_ratio =
            c.no_rx_duration.into_seconds() as f64 / c.connected_duration.into_seconds() as f64;
        if no_rx_time_ratio.is_finite() {
            metric_events.push(MetricEvent {
                metric_id: metrics::TIME_RATIO_WITH_NO_RX_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::IntegerValue(float_to_ten_thousandth(
                    no_rx_time_ratio,
                )),
            });

            if no_rx_time_ratio > DEVICE_FREQUENT_NO_RX_THRESHOLD {
                metric_events.push(MetricEvent {
                    metric_id: metrics::DEVICE_WITH_FREQUENT_NO_RX_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }
        }

        log_cobalt_1dot1_batch!(
            self.cobalt_1dot1_proxy,
            &mut metric_events.iter_mut(),
            "log_daily_1d_cobalt_metrics",
        );
    }

    async fn log_daily_7d_cobalt_metrics(&mut self) {
        let c = self.last_7d_stats.lock().windowed_stat(None);
        let connected_dur_in_day = c.connected_duration.into_seconds() as f64 / (24 * 3600) as f64;
        let dpdc_ratio = c.disconnect_count as f64 / connected_dur_in_day;
        if dpdc_ratio.is_finite() {
            let dpdc_ratio_dim = {
                use metrics::DeviceDisconnectPerDayConnectedBreakdown7dMetricDimensionDpdcRatio::*;
                match dpdc_ratio {
                    x if x == 0.0 => _0,
                    x if x <= 0.2 => UpTo0_2,
                    x if x <= 0.35 => UpTo0_35,
                    x if x <= 0.5 => UpTo0_5,
                    x if x <= 1.0 => UpTo1,
                    x if x <= 5.0 => UpTo5,
                    _ => MoreThan5,
                }
            };
            log_cobalt_1dot1!(
                self.cobalt_1dot1_proxy,
                log_occurrence,
                metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_7D_METRIC_ID,
                1,
                &[dpdc_ratio_dim as u32],
            );
        }
    }

    async fn handle_hr_passed(&mut self) {
        self.log_hourly_fleetwise_quality_cobalt_metrics().await;

        self.hr_tick = (self.hr_tick + 1) % 24;
        self.last_1d_stats.lock().slide_window();
        if self.hr_tick == 0 {
            self.last_7d_stats.lock().slide_window();
        }
    }

    async fn log_hourly_fleetwise_quality_cobalt_metrics(&mut self) {
        let mut metric_events = vec![];

        // Get stats from the last hour
        let c = self.last_1d_stats.lock().windowed_stat(Some(1));
        let total_wlan_uptime = c.connected_duration + c.adjusted_downtime();

        // Log the durations calculated in the last hour
        metric_events.push(MetricEvent {
            metric_id: metrics::TOTAL_WLAN_UPTIME_NEAR_SAVED_NETWORK_METRIC_ID,
            event_codes: vec![],
            payload: MetricEventPayload::IntegerValue(total_wlan_uptime.into_micros()),
        });
        metric_events.push(MetricEvent {
            metric_id: metrics::TOTAL_CONNECTED_UPTIME_METRIC_ID,
            event_codes: vec![],
            payload: MetricEventPayload::IntegerValue(c.connected_duration.into_micros()),
        });
        metric_events.push(MetricEvent {
            metric_id: metrics::TOTAL_TIME_WITH_HIGH_RX_PACKET_DROP_METRIC_ID,
            event_codes: vec![],
            payload: MetricEventPayload::IntegerValue(c.rx_high_packet_drop_duration.into_micros()),
        });
        metric_events.push(MetricEvent {
            metric_id: metrics::TOTAL_TIME_WITH_HIGH_TX_PACKET_DROP_METRIC_ID,
            event_codes: vec![],
            payload: MetricEventPayload::IntegerValue(c.tx_high_packet_drop_duration.into_micros()),
        });
        metric_events.push(MetricEvent {
            metric_id: metrics::TOTAL_TIME_WITH_NO_RX_METRIC_ID,
            event_codes: vec![],
            payload: MetricEventPayload::IntegerValue(c.no_rx_duration.into_micros()),
        });

        log_cobalt_1dot1_batch!(
            self.cobalt_1dot1_proxy,
            &mut metric_events.iter_mut(),
            "log_hourly_fleetwise_quality_cobalt_metrics",
        );
    }

    async fn log_disconnect_cobalt_metrics(&mut self, disconnect_info: &DisconnectInfo) {
        let mut metric_events = vec![];
        metric_events.push(MetricEvent {
            metric_id: metrics::TOTAL_DISCONNECT_COUNT_METRIC_ID,
            event_codes: vec![],
            payload: MetricEventPayload::Count(1),
        });

        let device_uptime_dim = {
            use metrics::DisconnectBreakdownByDeviceUptimeMetricDimensionDeviceUptime::*;
            match fasync::Time::now() - fasync::Time::from_nanos(0) {
                x if x < 1.hour() => LessThan1Hour,
                x if x < 3.hours() => LessThan3Hours,
                x if x < 12.hours() => LessThan12Hours,
                x if x < 24.hours() => LessThan1Day,
                x if x < 48.hours() => LessThan2Days,
                _ => AtLeast2Days,
            }
        };
        metric_events.push(MetricEvent {
            metric_id: metrics::DISCONNECT_BREAKDOWN_BY_DEVICE_UPTIME_METRIC_ID,
            event_codes: vec![device_uptime_dim as u32],
            payload: MetricEventPayload::Count(1),
        });

        let connected_duration_dim = {
            use metrics::DisconnectBreakdownByConnectedDurationMetricDimensionConnectedDuration::*;
            match disconnect_info.connected_duration {
                x if x < 30.seconds() => LessThan30Seconds,
                x if x < 5.minutes() => LessThan5Minutes,
                x if x < 1.hour() => LessThan1Hour,
                x if x < 6.hours() => LessThan6Hours,
                x if x < 24.hours() => LessThan24Hours,
                _ => AtLeast24Hours,
            }
        };
        metric_events.push(MetricEvent {
            metric_id: metrics::DISCONNECT_BREAKDOWN_BY_CONNECTED_DURATION_METRIC_ID,
            event_codes: vec![connected_duration_dim as u32],
            payload: MetricEventPayload::Count(1),
        });

        let disconnect_source_dim = convert_disconnect_source(&disconnect_info.disconnect_source);
        metric_events.push(MetricEvent {
            metric_id: metrics::DISCONNECT_BREAKDOWN_BY_REASON_CODE_METRIC_ID,
            event_codes: vec![disconnect_info.reason_code as u32, disconnect_source_dim as u32],
            payload: MetricEventPayload::Count(1),
        });

        metric_events.push(MetricEvent {
            metric_id: metrics::DISCONNECT_BREAKDOWN_BY_PRIMARY_CHANNEL_METRIC_ID,
            event_codes: vec![disconnect_info.latest_ap_state.channel.primary as u32],
            payload: MetricEventPayload::Count(1),
        });

        log_cobalt_1dot1_batch!(
            self.cobalt_1dot1_proxy,
            &mut metric_events.iter_mut(),
            "log_disconnect_cobalt_metrics",
        );
    }

    async fn log_downtime_cobalt_metrics(
        &mut self,
        downtime: zx::Duration,
        disconnect_info: &DisconnectInfo,
    ) {
        let disconnect_source_dim = convert_disconnect_source(&disconnect_info.disconnect_source);
        log_cobalt_1dot1!(
            self.cobalt_1dot1_proxy,
            log_integer,
            metrics::DOWNTIME_BREAKDOWN_BY_DISCONNECT_REASON_METRIC_ID,
            downtime.into_micros(),
            &[disconnect_info.reason_code as u32, disconnect_source_dim as u32],
        );
    }

    async fn log_device_connected_cobalt_metrics(&mut self, latest_ap_state: &BssDescription) {
        let mut metric_events = vec![];
        metric_events.push(MetricEvent {
            metric_id: metrics::NUMBER_OF_CONNECTED_DEVICES_METRIC_ID,
            event_codes: vec![],
            payload: MetricEventPayload::Count(1),
        });

        let security_type_dim = {
            use metrics::ConnectedNetworkSecurityTypeMetricDimensionSecurityType::*;
            match latest_ap_state.protection() {
                BssProtection::Unknown => Unknown,
                BssProtection::Open => Open,
                BssProtection::Wep => Wep,
                BssProtection::Wpa1 => Wpa1,
                BssProtection::Wpa1Wpa2PersonalTkipOnly => Wpa1Wpa2PersonalTkipOnly,
                BssProtection::Wpa2PersonalTkipOnly => Wpa2PersonalTkipOnly,
                BssProtection::Wpa1Wpa2Personal => Wpa1Wpa2Personal,
                BssProtection::Wpa2Personal => Wpa2Personal,
                BssProtection::Wpa2Wpa3Personal => Wpa2Wpa3Personal,
                BssProtection::Wpa3Personal => Wpa3Personal,
                BssProtection::Wpa2Enterprise => Wpa2Enterprise,
                BssProtection::Wpa3Enterprise => Wpa3Enterprise,
            }
        };
        metric_events.push(MetricEvent {
            metric_id: metrics::CONNECTED_NETWORK_SECURITY_TYPE_METRIC_ID,
            event_codes: vec![security_type_dim as u32],
            payload: MetricEventPayload::Count(1),
        });

        if latest_ap_state.supports_uapsd() {
            metric_events.push(MetricEvent {
                metric_id: metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_APSD_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::Count(1),
            });
        }

        if let Some(rm_enabled_cap) = latest_ap_state.rm_enabled_cap() {
            let rm_enabled_cap_head = rm_enabled_cap.rm_enabled_caps_head;
            if rm_enabled_cap_head.link_measurement_enabled() {
                metric_events.push(MetricEvent {
                    metric_id:
                        metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_LINK_MEASUREMENT_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }
            if rm_enabled_cap_head.neighbor_report_enabled() {
                metric_events.push(MetricEvent {
                    metric_id:
                        metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_NEIGHBOR_REPORT_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }
        }

        if latest_ap_state.supports_ft() {
            metric_events.push(MetricEvent {
                metric_id: metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_FT_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::Count(1),
            });
        }

        if let Some(cap) = latest_ap_state.ext_cap().map(|cap| cap.ext_caps_octet_3).flatten() {
            if cap.bss_transition() {
                metric_events.push(MetricEvent {
                    metric_id: metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_BSS_TRANSITION_MANAGEMENT_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Count(1),
                });
            }
        }

        log_cobalt_1dot1_batch!(
            self.cobalt_1dot1_proxy,
            &mut metric_events.iter_mut(),
            "log_device_connected_cobalt_metrics",
        );
    }
}

enum StatOp {
    AddTotalDuration(zx::Duration),
    AddConnectedDuration(zx::Duration),
    AddDowntimeDuration(zx::Duration),
    // Downtime with no saved network in vicinity
    AddDowntimeNoSavedNeighborDuration(zx::Duration),
    AddDisconnectCount,
    AddTxHighPacketDropDuration(zx::Duration),
    AddRxHighPacketDropDuration(zx::Duration),
    AddNoRxDuration(zx::Duration),
}

#[derive(Clone, Default)]
struct StatCounters {
    total_duration: zx::Duration,
    connected_duration: zx::Duration,
    downtime_duration: zx::Duration,
    downtime_no_saved_neighbor_duration: zx::Duration,
    disconnect_count: u64,
    tx_high_packet_drop_duration: zx::Duration,
    rx_high_packet_drop_duration: zx::Duration,
    no_rx_duration: zx::Duration,
}

impl StatCounters {
    fn adjusted_downtime(&self) -> zx::Duration {
        max(0.seconds(), self.downtime_duration - self.downtime_no_saved_neighbor_duration)
    }
}

// `Add` implementation is required to implement `SaturatingAdd` down below.
impl Add for StatCounters {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Self {
            total_duration: self.total_duration + other.total_duration,
            connected_duration: self.connected_duration + other.connected_duration,
            downtime_duration: self.downtime_duration + other.downtime_duration,
            downtime_no_saved_neighbor_duration: self.downtime_no_saved_neighbor_duration
                + other.downtime_no_saved_neighbor_duration,
            disconnect_count: self.disconnect_count + other.disconnect_count,
            tx_high_packet_drop_duration: self.tx_high_packet_drop_duration
                + other.tx_high_packet_drop_duration,
            rx_high_packet_drop_duration: self.rx_high_packet_drop_duration
                + other.rx_high_packet_drop_duration,
            no_rx_duration: self.no_rx_duration + other.no_rx_duration,
        }
    }
}

impl SaturatingAdd for StatCounters {
    fn saturating_add(&self, v: &Self) -> Self {
        Self {
            total_duration: zx::Duration::from_nanos(
                self.total_duration.into_nanos().saturating_add(v.total_duration.into_nanos()),
            ),
            connected_duration: zx::Duration::from_nanos(
                self.connected_duration
                    .into_nanos()
                    .saturating_add(v.connected_duration.into_nanos()),
            ),
            downtime_duration: zx::Duration::from_nanos(
                self.downtime_duration
                    .into_nanos()
                    .saturating_add(v.downtime_duration.into_nanos()),
            ),
            downtime_no_saved_neighbor_duration: zx::Duration::from_nanos(
                self.downtime_no_saved_neighbor_duration
                    .into_nanos()
                    .saturating_add(v.downtime_no_saved_neighbor_duration.into_nanos()),
            ),
            disconnect_count: self.disconnect_count.saturating_add(v.disconnect_count),
            tx_high_packet_drop_duration: zx::Duration::from_nanos(
                self.tx_high_packet_drop_duration
                    .into_nanos()
                    .saturating_add(v.tx_high_packet_drop_duration.into_nanos()),
            ),
            rx_high_packet_drop_duration: zx::Duration::from_nanos(
                self.rx_high_packet_drop_duration
                    .into_nanos()
                    .saturating_add(v.rx_high_packet_drop_duration.into_nanos()),
            ),
            no_rx_duration: zx::Duration::from_nanos(
                self.no_rx_duration.into_nanos().saturating_add(v.no_rx_duration.into_nanos()),
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_metrics::{MetricEvent, MetricEventLoggerRequest, MetricEventPayload},
        fidl_fuchsia_wlan_common as fidl_common,
        fidl_fuchsia_wlan_device_service::{
            DeviceServiceGetIfaceStatsResponder, DeviceServiceRequest,
        },
        fidl_fuchsia_wlan_stats::{self, ClientMlmeStats, Counter, PacketCounter},
        fuchsia_inspect::{assert_data_tree, testing::NonZeroUintProperty, Inspector},
        futures::{pin_mut, task::Poll, TryStreamExt},
        std::{cmp::min, pin::Pin},
        wlan_common::{ie::IeType, random_bss_description, test_utils::fake_stas::IesOverrides},
    };

    const STEP_INCREMENT: zx::Duration = zx::Duration::from_seconds(1);
    const IFACE_ID: u16 = 1;

    #[fuchsia::test]
    fn test_stat_cycles() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(24.hours() - TELEMETRY_QUERY_INTERVAL, test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    total_duration: (24.hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    connected_duration: (24.hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                },
                "7d_counters": contains {
                    total_duration: (24.hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    connected_duration: (24.hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                },
            }
        });

        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    // The first hour window is now discarded, so it only shows 23 hours
                    // of total and connected duration.
                    total_duration: 23.hours().into_nanos(),
                    connected_duration: 23.hours().into_nanos(),
                },
                "7d_counters": contains {
                    total_duration: 24.hours().into_nanos(),
                    connected_duration: 24.hours().into_nanos(),
                },
            }
        });

        test_helper.advance_by(2.hours(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    total_duration: 23.hours().into_nanos(),
                    connected_duration: 23.hours().into_nanos(),
                },
                "7d_counters": contains {
                    total_duration: 26.hours().into_nanos(),
                    connected_duration: 26.hours().into_nanos(),
                },
            }
        });

        // Disconnect now
        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(8.hours(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    total_duration: 23.hours().into_nanos(),
                    // Now the 1d connected counter should decrease
                    connected_duration: 15.hours().into_nanos(),
                },
                "7d_counters": contains {
                    total_duration: 34.hours().into_nanos(),
                    connected_duration: 26.hours().into_nanos(),
                },
            }
        });

        // The 7d counters do not decrease before the 7th day
        test_helper.advance_by(14.hours(), test_fut.as_mut());
        test_helper.advance_by((5 * 24).hours() - TELEMETRY_QUERY_INTERVAL, test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    total_duration: (24.hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    connected_duration: 0i64,
                },
                "7d_counters": contains {
                    total_duration: ((7 * 24).hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    connected_duration: 26.hours().into_nanos(),
                },
            }
        });

        // On the 7th day, the first window is removed (24 hours of duration is deducted)
        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    total_duration: 23.hours().into_nanos(),
                    connected_duration: 0i64,
                },
                "7d_counters": contains {
                    total_duration: (6 * 24).hours().into_nanos(),
                    connected_duration: 2.hours().into_nanos(),
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_total_duration_counters() {
        let (mut test_helper, mut test_fut) = setup_test();

        test_helper.advance_by(30.minutes(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    total_duration: 30.minutes().into_nanos(),
                },
                "7d_counters": contains {
                    total_duration: 30.minutes().into_nanos(),
                },
            }
        });

        test_helper.advance_by(30.minutes(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    total_duration: 1.hour().into_nanos(),
                },
                "7d_counters": contains {
                    total_duration: 1.hour().into_nanos(),
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_counters_when_idle() {
        let (mut test_helper, mut test_fut) = setup_test();

        test_helper.advance_by(30.minutes(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });

        test_helper.advance_by(30.minutes(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_connected_counters_increase_when_connected() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(30.minutes(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 30.minutes().into_nanos(),
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 30.minutes().into_nanos(),
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });

        test_helper.advance_by(30.minutes(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 1.hour().into_nanos(),
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 1.hour().into_nanos(),
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_downtime_counter() {
        let (mut test_helper, mut test_fut) = setup_test();

        // Disconnect but not track downtime. Downtime counter should not increase.
        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(10.minutes(), test_fut.as_mut());

        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });

        // Disconnect and track downtime. Downtime counter should now increase
        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(15.minutes(), test_fut.as_mut());

        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 15.minutes().into_nanos(),
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 15.minutes().into_nanos(),
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_counters_connect_then_disconnect() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(5.seconds(), test_fut.as_mut());

        // Disconnect but not track downtime. Downtime counter should not increase.
        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // The 5 seconds connected duration is not accounted for yet.
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });

        // At next telemetry checkpoint, `test_fut` updates the connected and downtime durations.
        let downtime_start = fasync::Time::now();
        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 5.seconds().into_nanos(),
                    downtime_duration: (fasync::Time::now() - downtime_start).into_nanos(),
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 5.seconds().into_nanos(),
                    downtime_duration: (fasync::Time::now() - downtime_start).into_nanos(),
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_downtime_no_saved_neighbor_duration_counter() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        test_helper.drain_cobalt_events(&mut test_fut);

        // Disconnect and track downtime.
        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(5.seconds(), test_fut.as_mut());
        // Indicate that there's no saved neighbor in vicinity
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(0),
            selected_any: false,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: TELEMETRY_QUERY_INTERVAL.into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL - 5.seconds()).into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: TELEMETRY_QUERY_INTERVAL.into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL - 5.seconds()).into_nanos(),
                },
            }
        });

        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL*2 - 5.seconds()).into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL*2 - 5.seconds()).into_nanos(),
                },
            }
        });

        test_helper.advance_by(5.seconds(), test_fut.as_mut());
        // Indicate that saved neighbor has been found
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(1),
            selected_any: false,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // `downtime_no_saved_neighbor_duration` counter is not updated right away.
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL*2 - 5.seconds()).into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL*2 - 5.seconds()).into_nanos(),
                },
            }
        });

        // At the next checkpoint, both downtime counters are updated together.
        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 3).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 3).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                },
            }
        });

        // Disconnect but don't track downtime
        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false, info });

        // Indicate that there's no saved neighbor in vicinity
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(0),
            selected_any: false,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);
        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());

        // However, this time neither of the downtime counters should be incremented
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 3).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: (TELEMETRY_QUERY_INTERVAL * 3).into_nanos(),
                    downtime_no_saved_neighbor_duration: (TELEMETRY_QUERY_INTERVAL * 2).into_nanos(),
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_disconnect_count_counter() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    disconnect_count: 0u64,
                },
                "7d_counters": contains {
                    disconnect_count: 0u64,
                },
            }
        });

        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        test_helper.drain_cobalt_events(&mut test_fut);

        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    disconnect_count: 1u64,
                },
                "7d_counters": contains {
                    disconnect_count: 1u64,
                },
            }
        });

        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false, info });
        test_helper.drain_cobalt_events(&mut test_fut);

        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    disconnect_count: 2u64,
                },
                "7d_counters": contains {
                    disconnect_count: 2u64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_rx_tx_counters_no_issue() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                get_iface_stats_fail_count: 0u64,
                "1d_counters": contains {
                    tx_high_packet_drop_duration: 0i64,
                    rx_high_packet_drop_duration: 0i64,
                    no_rx_duration: 0i64,
                },
                "7d_counters": contains {
                    tx_high_packet_drop_duration: 0i64,
                    rx_high_packet_drop_duration: 0i64,
                    no_rx_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_tx_high_packet_drop_duration_counters() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.set_iface_stats_req_handler(Box::new(|responder| {
            let seed = fasync::Time::now().into_nanos() as u64;
            let mut iface_stats = fake_iface_stats(seed);
            match &mut iface_stats.mlme_stats {
                Some(stats) => match **stats {
                    MlmeStats::ClientMlmeStats(ref mut stats) => {
                        stats.tx_frame.in_.count = 10 * seed;
                        stats.tx_frame.drop.count = 3 * seed;
                    }
                    _ => panic!("expect ClientMlmeStats"),
                },
                _ => panic!("expect mlme_stats to be available"),
            }
            responder
                .send(zx::sys::ZX_OK, Some(&mut iface_stats))
                .expect("expect sending IfaceStats response to succeed");
        }));

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                get_iface_stats_fail_count: 0u64,
                "1d_counters": contains {
                    // Deduct 15 seconds beecause there isn't packet counter to diff against in
                    // the first interval of telemetry
                    tx_high_packet_drop_duration: (1.hour() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    rx_high_packet_drop_duration: 0i64,
                    no_rx_duration: 0i64,
                },
                "7d_counters": contains {
                    tx_high_packet_drop_duration: (1.hour() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    rx_high_packet_drop_duration: 0i64,
                    no_rx_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_rx_high_packet_drop_duration_counters() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.set_iface_stats_req_handler(Box::new(|responder| {
            let seed = fasync::Time::now().into_nanos() as u64;
            let mut iface_stats = fake_iface_stats(seed);
            match &mut iface_stats.mlme_stats {
                Some(stats) => match **stats {
                    MlmeStats::ClientMlmeStats(ref mut stats) => {
                        stats.rx_frame.in_.count = 10 * seed;
                        stats.rx_frame.drop.count = 3 * seed;
                    }
                    _ => panic!("expect ClientMlmeStats"),
                },
                _ => panic!("expect mlme_stats to be available"),
            }
            responder
                .send(zx::sys::ZX_OK, Some(&mut iface_stats))
                .expect("expect sending IfaceStats response to succeed");
        }));

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                get_iface_stats_fail_count: 0u64,
                "1d_counters": contains {
                    // Deduct 15 seconds beecause there isn't packet counter to diff against in
                    // the first interval of telemetry
                    rx_high_packet_drop_duration: (1.hour() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    tx_high_packet_drop_duration: 0i64,
                    no_rx_duration: 0i64,
                },
                "7d_counters": contains {
                    rx_high_packet_drop_duration: (1.hour() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    tx_high_packet_drop_duration: 0i64,
                    no_rx_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_no_rx_duration_counters() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.set_iface_stats_req_handler(Box::new(|responder| {
            let seed = fasync::Time::now().into_nanos() as u64;
            let mut iface_stats = fake_iface_stats(seed);
            match &mut iface_stats.mlme_stats {
                Some(stats) => match **stats {
                    MlmeStats::ClientMlmeStats(ref mut stats) => {
                        stats.rx_frame.in_.count = 10;
                    }
                    _ => panic!("expect ClientMlmeStats"),
                },
                _ => panic!("expect mlme_stats to be available"),
            }
            responder
                .send(zx::sys::ZX_OK, Some(&mut iface_stats))
                .expect("expect sending IfaceStats response to succeed");
        }));

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                get_iface_stats_fail_count: 0u64,
                "1d_counters": contains {
                    // Deduct 15 seconds beecause there isn't packet counter to diff against in
                    // the first interval of telemetry
                    no_rx_duration: (1.hour() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    rx_high_packet_drop_duration: 0i64,
                    tx_high_packet_drop_duration: 0i64,
                },
                "7d_counters": contains {
                    no_rx_duration: (1.hour() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                    rx_high_packet_drop_duration: 0i64,
                    tx_high_packet_drop_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_get_iface_stats_fail() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.set_iface_stats_req_handler(Box::new(|responder| {
            responder
                .send(zx::sys::ZX_ERR_NOT_SUPPORTED, None)
                .expect("expect sending IfaceStats response to succeed");
        }));

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                get_iface_stats_fail_count: NonZeroUintProperty,
                "1d_counters": contains {
                    no_rx_duration: 0i64,
                    rx_high_packet_drop_duration: 0i64,
                    tx_high_packet_drop_duration: 0i64,
                },
                "7d_counters": contains {
                    no_rx_duration: 0i64,
                    rx_high_packet_drop_duration: 0i64,
                    tx_high_packet_drop_duration: 0i64,
                },
            }
        });
    }

    #[fuchsia::test]
    fn test_log_daily_uptime_ratio_cobalt_metric() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(12.hours(), test_fut.as_mut());

        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(6.hours(), test_fut.as_mut());

        // Indicate that there's no saved neighbor in vicinity
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(0),
            selected_any: false,
        });

        test_helper.advance_by(6.hours(), test_fut.as_mut());

        let uptime_ratios =
            test_helper.get_logged_metrics(metrics::CONNECTED_UPTIME_RATIO_METRIC_ID);
        assert_eq!(uptime_ratios.len(), 1);
        // 12 hours of uptime, 6 hours of adjusted downtime => 66.66% uptime
        assert_eq!(uptime_ratios[0].payload, MetricEventPayload::IntegerValue(6666));

        let device_low_uptime =
            test_helper.get_logged_metrics(metrics::DEVICE_WITH_LOW_UPTIME_METRIC_ID);
        assert_eq!(device_low_uptime.len(), 1);
        assert_eq!(device_low_uptime[0].payload, MetricEventPayload::Count(1));

        let uptime_ratio_breakdowns = test_helper
            .get_logged_metrics(metrics::DEVICE_CONNECTED_UPTIME_RATIO_BREAKDOWN_METRIC_ID);
        assert_eq!(uptime_ratio_breakdowns.len(), 1);
        assert_eq!(
            uptime_ratio_breakdowns[0].event_codes,
            &[metrics::ConnectivityWlanMetricDimensionUptimeRatio::LessThan75Percent as u32]
        );
        assert_eq!(uptime_ratio_breakdowns[0].payload, MetricEventPayload::Count(1));
    }

    #[fuchsia::test]
    fn test_log_daily_disconnect_per_day_connected_cobalt_metric() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(6.hours(), test_fut.as_mut());

        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(18.hours(), test_fut.as_mut());

        let dpdc_ratios =
            test_helper.get_logged_metrics(metrics::DISCONNECT_PER_DAY_CONNECTED_METRIC_ID);
        assert_eq!(dpdc_ratios.len(), 1);
        // 1 disconnect, 0.25 day connected => 4 disconnects per day connected
        // (which equals 40_0000 in TenThousandth unit)
        assert_eq!(dpdc_ratios[0].payload, MetricEventPayload::IntegerValue(40_000));

        let device_high_disconnect =
            test_helper.get_logged_metrics(metrics::DEVICE_WITH_HIGH_DISCONNECT_RATE_METRIC_ID);
        assert_eq!(device_high_disconnect.len(), 0);

        let dpdc_ratio_breakdowns = test_helper
            .get_logged_metrics(metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_METRIC_ID);
        assert_eq!(dpdc_ratio_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdownMetricDimensionDpdcRatio::UpTo5
                as u32]
        );
        assert_eq!(dpdc_ratio_breakdowns[0].payload, MetricEventPayload::Count(1));

        let dpdc_ratio_7d_breakdowns = test_helper.get_logged_metrics(
            metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_7D_METRIC_ID,
        );
        assert_eq!(dpdc_ratio_7d_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_7d_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdown7dMetricDimensionDpdcRatio::UpTo5
                as u32]
        );
        assert_eq!(dpdc_ratio_7d_breakdowns[0].payload, MetricEventPayload::Count(1));

        // Clear record of logged Cobalt events
        test_helper.cobalt_events.clear();

        // Connect for another 1 day to dilute the 7d ratio
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(24.hours(), test_fut.as_mut());

        // No disconnect in the last day, so the 1d ratio would be 0.
        let dpdc_ratios =
            test_helper.get_logged_metrics(metrics::DISCONNECT_PER_DAY_CONNECTED_METRIC_ID);
        assert_eq!(dpdc_ratios.len(), 1);
        assert_eq!(dpdc_ratios[0].payload, MetricEventPayload::IntegerValue(0));

        let dpdc_ratio_breakdowns = test_helper
            .get_logged_metrics(metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_METRIC_ID);
        assert_eq!(dpdc_ratio_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdownMetricDimensionDpdcRatio::_0 as u32]
        );
        assert_eq!(dpdc_ratio_breakdowns[0].payload, MetricEventPayload::Count(1));

        // In the last 7 days, 1 disconnects and 1.25 days connected => 0.8 dpdc ratio
        let dpdc_ratio_7d_breakdowns = test_helper.get_logged_metrics(
            metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_7D_METRIC_ID,
        );
        assert_eq!(dpdc_ratio_7d_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_7d_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdown7dMetricDimensionDpdcRatio::UpTo1
                as u32]
        );
        assert_eq!(dpdc_ratio_7d_breakdowns[0].payload, MetricEventPayload::Count(1));
    }

    #[fuchsia::test]
    fn test_log_daily_disconnect_per_day_connected_cobalt_metric_device_high_disconnect() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hours(), test_fut.as_mut());
        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(23.hours(), test_fut.as_mut());

        let device_high_disconnect =
            test_helper.get_logged_metrics(metrics::DEVICE_WITH_HIGH_DISCONNECT_RATE_METRIC_ID);
        assert_eq!(device_high_disconnect.len(), 1);
        assert_eq!(device_high_disconnect[0].payload, MetricEventPayload::Count(1));
    }

    #[fuchsia::test]
    fn test_log_daily_rx_tx_ratio_cobalt_metrics() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.set_iface_stats_req_handler(Box::new(|responder| {
            let seed = fasync::Time::now().into_nanos() as u64 / 1000_000_000;
            let mut iface_stats = fake_iface_stats(seed);
            match &mut iface_stats.mlme_stats {
                Some(stats) => match **stats {
                    MlmeStats::ClientMlmeStats(ref mut stats) => {
                        stats.tx_frame.in_.count = 10 * seed;
                        // TX drop rate stops increasing at 1 hour + TELEMETRY_QUERY_INTERVAL mark.
                        // Because the first TELEMETRY_QUERY_INTERVAL doesn't count when
                        // computing counters, this leads to 3 hour of high TX drop rate.
                        stats.tx_frame.drop.count = 3 * min(
                            seed,
                            (3.hours() + TELEMETRY_QUERY_INTERVAL).into_seconds() as u64,
                        );
                        // RX drop rate stops increasing at 4 hour + TELEMETRY_QUERY_INTERVAL mark.
                        stats.rx_frame.drop.count = 3 * min(
                            seed,
                            (4.hours() + TELEMETRY_QUERY_INTERVAL).into_seconds() as u64,
                        );
                        // RX total stops increasing at 23 hour mark
                        stats.rx_frame.in_.count = 10 * min(seed, 23.hours().into_seconds() as u64);
                    }
                    _ => panic!("expect ClientMlmeStats"),
                },
                _ => panic!("expect mlme_stats to be available"),
            }
            responder
                .send(zx::sys::ZX_OK, Some(&mut iface_stats))
                .expect("expect sending IfaceStats response to succeed");
        }));

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(24.hours(), test_fut.as_mut());

        let high_rx_drop_time_ratios =
            test_helper.get_logged_metrics(metrics::TIME_RATIO_WITH_HIGH_RX_PACKET_DROP_METRIC_ID);
        // 4 hours of high RX drop rate, 24 hours connected => 16.66% duration
        assert_eq!(high_rx_drop_time_ratios.len(), 1);
        assert_eq!(high_rx_drop_time_ratios[0].payload, MetricEventPayload::IntegerValue(1666));

        let device_frequent_high_rx_drop = test_helper
            .get_logged_metrics(metrics::DEVICE_WITH_FREQUENT_HIGH_RX_PACKET_DROP_METRIC_ID);
        assert_eq!(device_frequent_high_rx_drop.len(), 1);
        assert_eq!(device_frequent_high_rx_drop[0].payload, MetricEventPayload::Count(1));

        let high_tx_drop_time_ratios =
            test_helper.get_logged_metrics(metrics::TIME_RATIO_WITH_HIGH_TX_PACKET_DROP_METRIC_ID);
        // 3 hours of high RX drop rate, 24 hours connected => 12.48% duration
        assert_eq!(high_tx_drop_time_ratios.len(), 1);
        assert_eq!(high_tx_drop_time_ratios[0].payload, MetricEventPayload::IntegerValue(1250));

        let device_frequent_high_tx_drop = test_helper
            .get_logged_metrics(metrics::DEVICE_WITH_FREQUENT_HIGH_TX_PACKET_DROP_METRIC_ID);
        assert_eq!(device_frequent_high_tx_drop.len(), 1);
        assert_eq!(device_frequent_high_tx_drop[0].payload, MetricEventPayload::Count(1));

        // 1 hour of no RX, 24 hours connected => 4.16% duration
        let no_rx_time_ratios =
            test_helper.get_logged_metrics(metrics::TIME_RATIO_WITH_NO_RX_METRIC_ID);
        assert_eq!(no_rx_time_ratios.len(), 1);
        assert_eq!(no_rx_time_ratios[0].payload, MetricEventPayload::IntegerValue(416));

        let device_frequent_no_rx =
            test_helper.get_logged_metrics(metrics::DEVICE_WITH_FREQUENT_NO_RX_METRIC_ID);
        assert_eq!(device_frequent_no_rx.len(), 1);
        assert_eq!(device_frequent_no_rx[0].payload, MetricEventPayload::Count(1));
    }

    #[fuchsia::test]
    fn test_log_daily_rx_tx_ratio_cobalt_metrics_zero() {
        // This test is to verify that when the RX/TX ratios are 0 (there's no issue), we still
        // log to Cobalt.
        let (mut test_helper, mut test_fut) = setup_test();

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(24.hours(), test_fut.as_mut());

        let high_rx_drop_time_ratios =
            test_helper.get_logged_metrics(metrics::TIME_RATIO_WITH_HIGH_RX_PACKET_DROP_METRIC_ID);
        assert_eq!(high_rx_drop_time_ratios.len(), 1);
        assert_eq!(high_rx_drop_time_ratios[0].payload, MetricEventPayload::IntegerValue(0));

        let high_tx_drop_time_ratios =
            test_helper.get_logged_metrics(metrics::TIME_RATIO_WITH_HIGH_TX_PACKET_DROP_METRIC_ID);
        assert_eq!(high_tx_drop_time_ratios.len(), 1);
        assert_eq!(high_tx_drop_time_ratios[0].payload, MetricEventPayload::IntegerValue(0));

        let no_rx_time_ratios =
            test_helper.get_logged_metrics(metrics::TIME_RATIO_WITH_NO_RX_METRIC_ID);
        assert_eq!(no_rx_time_ratios.len(), 1);
        assert_eq!(no_rx_time_ratios[0].payload, MetricEventPayload::IntegerValue(0));
    }

    #[fuchsia::test]
    fn test_log_hourly_fleetwise_uptime_cobalt_metrics() {
        let (mut test_helper, mut test_fut) = setup_test();

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());

        let total_wlan_uptime_durs =
            test_helper.get_logged_metrics(metrics::TOTAL_WLAN_UPTIME_NEAR_SAVED_NETWORK_METRIC_ID);
        assert_eq!(total_wlan_uptime_durs.len(), 1);
        assert_eq!(
            total_wlan_uptime_durs[0].payload,
            MetricEventPayload::IntegerValue(1.hour().into_micros())
        );

        let connected_durs =
            test_helper.get_logged_metrics(metrics::TOTAL_CONNECTED_UPTIME_METRIC_ID);
        assert_eq!(connected_durs.len(), 1);
        assert_eq!(
            connected_durs[0].payload,
            MetricEventPayload::IntegerValue(1.hour().into_micros())
        );

        // Clear record of logged Cobalt events
        test_helper.cobalt_events.clear();

        test_helper.advance_by(30.minutes(), test_fut.as_mut());

        let info = fake_disconnect_info();
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(15.minutes(), test_fut.as_mut());

        // Indicate that there's no saved neighbor in vicinity
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(0),
            selected_any: false,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(15.minutes(), test_fut.as_mut());

        let total_wlan_uptime_durs =
            test_helper.get_logged_metrics(metrics::TOTAL_WLAN_UPTIME_NEAR_SAVED_NETWORK_METRIC_ID);
        assert_eq!(total_wlan_uptime_durs.len(), 1);
        // 30 minutes connected uptime + 15 minutes downtime near saved network
        assert_eq!(
            total_wlan_uptime_durs[0].payload,
            MetricEventPayload::IntegerValue(45.minutes().into_micros())
        );

        let connected_durs =
            test_helper.get_logged_metrics(metrics::TOTAL_CONNECTED_UPTIME_METRIC_ID);
        assert_eq!(connected_durs.len(), 1);
        assert_eq!(
            connected_durs[0].payload,
            MetricEventPayload::IntegerValue(30.minutes().into_micros())
        );
    }

    #[fuchsia::test]
    fn test_log_hourly_fleetwise_rx_tx_cobalt_metrics() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.set_iface_stats_req_handler(Box::new(|responder| {
            let seed = fasync::Time::now().into_nanos() as u64 / 1000_000_000;
            let mut iface_stats = fake_iface_stats(seed);
            match &mut iface_stats.mlme_stats {
                Some(stats) => match **stats {
                    MlmeStats::ClientMlmeStats(ref mut stats) => {
                        stats.tx_frame.in_.count = 10 * seed;
                        // TX drop rate stops increasing at 10 min + TELEMETRY_QUERY_INTERVAL mark.
                        // Because the first TELEMETRY_QUERY_INTERVAL doesn't count when
                        // computing counters, this leads to 10 min of high TX drop rate.
                        stats.tx_frame.drop.count = 3 * min(
                            seed,
                            (10.minutes() + TELEMETRY_QUERY_INTERVAL).into_seconds() as u64,
                        );
                        // RX drop rate stops increasing at 20 min + TELEMETRY_QUERY_INTERVAL mark.
                        stats.rx_frame.drop.count = 3 * min(
                            seed,
                            (20.minutes() + TELEMETRY_QUERY_INTERVAL).into_seconds() as u64,
                        );
                        // RX total stops increasing at 45 min mark
                        stats.rx_frame.in_.count =
                            10 * min(seed, 45.minutes().into_seconds() as u64);
                    }
                    _ => panic!("expect ClientMlmeStats"),
                },
                _ => panic!("expect mlme_stats to be available"),
            }
            responder
                .send(zx::sys::ZX_OK, Some(&mut iface_stats))
                .expect("expect sending IfaceStats response to succeed");
        }));

        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());

        let rx_high_drop_durs =
            test_helper.get_logged_metrics(metrics::TOTAL_TIME_WITH_HIGH_RX_PACKET_DROP_METRIC_ID);
        assert_eq!(rx_high_drop_durs.len(), 1);
        assert_eq!(
            rx_high_drop_durs[0].payload,
            MetricEventPayload::IntegerValue(20.minutes().into_micros())
        );

        let tx_high_drop_durs =
            test_helper.get_logged_metrics(metrics::TOTAL_TIME_WITH_HIGH_TX_PACKET_DROP_METRIC_ID);
        assert_eq!(tx_high_drop_durs.len(), 1);
        assert_eq!(
            tx_high_drop_durs[0].payload,
            MetricEventPayload::IntegerValue(10.minutes().into_micros())
        );

        let no_rx_durs = test_helper.get_logged_metrics(metrics::TOTAL_TIME_WITH_NO_RX_METRIC_ID);
        assert_eq!(no_rx_durs.len(), 1);
        assert_eq!(
            no_rx_durs[0].payload,
            MetricEventPayload::IntegerValue(15.minutes().into_micros())
        );
    }

    #[fuchsia::test]
    fn test_log_disconnect_cobalt_metrics() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.advance_by(3.hours(), test_fut.as_mut());
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(5.hours(), test_fut.as_mut());

        let primary_channel = 8;
        let channel = fidl_common::WlanChannel {
            primary: primary_channel,
            cbw: fidl_common::ChannelBandwidth::Cbw20,
            secondary80: 0,
        };
        let latest_ap_state = random_bss_description!(Wpa2, channel: channel);
        let info = DisconnectInfo {
            connected_duration: 5.hours(),
            reason_code: 3,
            disconnect_source: fidl_sme::DisconnectSource::Mlme,
            latest_ap_state,
            ..fake_disconnect_info()
        };
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        test_helper.drain_cobalt_events(&mut test_fut);

        let disconnect_counts =
            test_helper.get_logged_metrics(metrics::TOTAL_DISCONNECT_COUNT_METRIC_ID);
        assert_eq!(disconnect_counts.len(), 1);
        assert_eq!(disconnect_counts[0].payload, MetricEventPayload::Count(1));

        let breakdowns_by_device_uptime = test_helper
            .get_logged_metrics(metrics::DISCONNECT_BREAKDOWN_BY_DEVICE_UPTIME_METRIC_ID);
        assert_eq!(breakdowns_by_device_uptime.len(), 1);
        assert_eq!(breakdowns_by_device_uptime[0].event_codes, vec![
            metrics::DisconnectBreakdownByDeviceUptimeMetricDimensionDeviceUptime::LessThan12Hours as u32,
        ]);
        assert_eq!(breakdowns_by_device_uptime[0].payload, MetricEventPayload::Count(1));

        let breakdowns_by_connected_duration = test_helper
            .get_logged_metrics(metrics::DISCONNECT_BREAKDOWN_BY_CONNECTED_DURATION_METRIC_ID);
        assert_eq!(breakdowns_by_connected_duration.len(), 1);
        assert_eq!(breakdowns_by_connected_duration[0].event_codes, vec![
            metrics::DisconnectBreakdownByConnectedDurationMetricDimensionConnectedDuration::LessThan6Hours as u32,
        ]);
        assert_eq!(breakdowns_by_connected_duration[0].payload, MetricEventPayload::Count(1));

        let breakdowns_by_reason =
            test_helper.get_logged_metrics(metrics::DISCONNECT_BREAKDOWN_BY_REASON_CODE_METRIC_ID);
        assert_eq!(breakdowns_by_reason.len(), 1);
        assert_eq!(
            breakdowns_by_reason[0].event_codes,
            vec![3u32, metrics::ConnectivityWlanMetricDimensionDisconnectSource::Mlme as u32,]
        );
        assert_eq!(breakdowns_by_reason[0].payload, MetricEventPayload::Count(1));

        let breakdowns_by_channel = test_helper
            .get_logged_metrics(metrics::DISCONNECT_BREAKDOWN_BY_PRIMARY_CHANNEL_METRIC_ID);
        assert_eq!(breakdowns_by_channel.len(), 1);
        assert_eq!(breakdowns_by_channel[0].event_codes, vec![channel.primary as u32]);
        assert_eq!(breakdowns_by_channel[0].payload, MetricEventPayload::Count(1));
    }

    #[fuchsia::test]
    fn test_log_downtime_cobalt_metrics() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        test_helper.drain_cobalt_events(&mut test_fut);

        let info = DisconnectInfo {
            reason_code: 3,
            disconnect_source: fidl_sme::DisconnectSource::Mlme,
            ..fake_disconnect_info()
        };
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true, info });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(42.minutes(), test_fut.as_mut());
        // Indicate that there's no saved neighbor in vicinity
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(0),
            selected_any: false,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(5.minutes(), test_fut.as_mut());
        // Indicate that there's some saved neighbor in vicinity
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(5),
            selected_any: true,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(7.minutes(), test_fut.as_mut());
        // Reconnect
        test_helper.send_connected_event(random_bss_description!(Wpa2));
        test_helper.drain_cobalt_events(&mut test_fut);

        let breakdowns_by_reason = test_helper
            .get_logged_metrics(metrics::DOWNTIME_BREAKDOWN_BY_DISCONNECT_REASON_METRIC_ID);
        assert_eq!(breakdowns_by_reason.len(), 1);
        assert_eq!(
            breakdowns_by_reason[0].event_codes,
            vec![3u32, metrics::ConnectivityWlanMetricDimensionDisconnectSource::Mlme as u32,]
        );
        assert_eq!(
            breakdowns_by_reason[0].payload,
            MetricEventPayload::IntegerValue(49.minutes().into_micros())
        );
    }

    #[fuchsia::test]
    fn test_log_device_connected_cobalt_metrics() {
        let (mut test_helper, mut test_fut) = setup_test();

        let wmm_info = vec![0x80]; // U-APSD enabled
        #[rustfmt::skip]
        let rm_enabled_capabilities = vec![
            0x03, // link measurement and neighbor report enabled
            0x00, 0x00, 0x00, 0x00,
        ];
        #[rustfmt::skip]
        let ext_capabilities = vec![
            0x04, 0x00,
            0x08, // BSS transition supported
            0x00, 0x00, 0x00, 0x00, 0x40
        ];
        let bss_description = random_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::WMM_PARAM)
                .set(IeType::WMM_INFO, wmm_info)
                .set(IeType::RM_ENABLED_CAPABILITIES, rm_enabled_capabilities)
                .set(IeType::MOBILITY_DOMAIN, vec![0x00; 3])
                .set(IeType::EXT_CAPABILITIES, ext_capabilities)
        );
        test_helper.send_connected_event(bss_description);
        test_helper.drain_cobalt_events(&mut test_fut);

        let num_devices_connected =
            test_helper.get_logged_metrics(metrics::NUMBER_OF_CONNECTED_DEVICES_METRIC_ID);
        assert_eq!(num_devices_connected.len(), 1);
        assert_eq!(num_devices_connected[0].payload, MetricEventPayload::Count(1));

        let connected_security_type =
            test_helper.get_logged_metrics(metrics::CONNECTED_NETWORK_SECURITY_TYPE_METRIC_ID);
        assert_eq!(connected_security_type.len(), 1);
        assert_eq!(
            connected_security_type[0].event_codes,
            vec![
                metrics::ConnectedNetworkSecurityTypeMetricDimensionSecurityType::Wpa2Personal
                    as u32
            ]
        );
        assert_eq!(connected_security_type[0].payload, MetricEventPayload::Count(1));

        let connected_apsd = test_helper
            .get_logged_metrics(metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_APSD_METRIC_ID);
        assert_eq!(connected_apsd.len(), 1);
        assert_eq!(connected_apsd[0].payload, MetricEventPayload::Count(1));

        let connected_link_measurement = test_helper.get_logged_metrics(
            metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_LINK_MEASUREMENT_METRIC_ID,
        );
        assert_eq!(connected_link_measurement.len(), 1);
        assert_eq!(connected_link_measurement[0].payload, MetricEventPayload::Count(1));

        let connected_neighbor_report = test_helper.get_logged_metrics(
            metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_NEIGHBOR_REPORT_METRIC_ID,
        );
        assert_eq!(connected_neighbor_report.len(), 1);
        assert_eq!(connected_neighbor_report[0].payload, MetricEventPayload::Count(1));

        let connected_ft = test_helper
            .get_logged_metrics(metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_FT_METRIC_ID);
        assert_eq!(connected_ft.len(), 1);
        assert_eq!(connected_ft[0].payload, MetricEventPayload::Count(1));

        let connected_bss_transition_mgmt = test_helper.get_logged_metrics(
            metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_BSS_TRANSITION_MANAGEMENT_METRIC_ID,
        );
        assert_eq!(connected_bss_transition_mgmt.len(), 1);
        assert_eq!(connected_bss_transition_mgmt[0].payload, MetricEventPayload::Count(1));
    }

    #[fuchsia::test]
    fn test_log_device_connected_cobalt_metrics_ap_features_not_supported() {
        let (mut test_helper, mut test_fut) = setup_test();

        let bss_description = random_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::WMM_PARAM)
                .remove(IeType::WMM_INFO)
                .remove(IeType::RM_ENABLED_CAPABILITIES)
                .remove(IeType::MOBILITY_DOMAIN)
                .remove(IeType::EXT_CAPABILITIES)
        );
        test_helper.send_connected_event(bss_description);
        test_helper.drain_cobalt_events(&mut test_fut);

        let connected_apsd = test_helper
            .get_logged_metrics(metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_APSD_METRIC_ID);
        assert_eq!(connected_apsd.len(), 0);

        let connected_link_measurement = test_helper.get_logged_metrics(
            metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_LINK_MEASUREMENT_METRIC_ID,
        );
        assert_eq!(connected_link_measurement.len(), 0);

        let connected_neighbor_report = test_helper.get_logged_metrics(
            metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_NEIGHBOR_REPORT_METRIC_ID,
        );
        assert_eq!(connected_neighbor_report.len(), 0);

        let connected_ft = test_helper
            .get_logged_metrics(metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_FT_METRIC_ID);
        assert_eq!(connected_ft.len(), 0);

        let connected_bss_transition_mgmt = test_helper.get_logged_metrics(
            metrics::DEVICE_CONNECTED_TO_AP_THAT_SUPPORTS_BSS_TRANSITION_MANAGEMENT_METRIC_ID,
        );
        assert_eq!(connected_bss_transition_mgmt.len(), 0);
    }

    struct TestHelper {
        telemetry_sender: TelemetrySender,
        inspector: Inspector,
        dev_svc_stream: fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream,
        cobalt_1dot1_stream: fidl_fuchsia_metrics::MetricEventLoggerRequestStream,
        iface_stats_req_handler: Option<Box<dyn FnMut(DeviceServiceGetIfaceStatsResponder)>>,
        /// As requests to Cobalt are responded to via `self.drain_cobalt_events()`,
        /// their payloads are drained to this HashMap
        cobalt_events: Vec<MetricEvent>,

        // Note: keep the executor field last in the struct so it gets dropped last.
        exec: fasync::TestExecutor,
    }

    impl TestHelper {
        /// Advance executor by `duration`.
        /// This function repeatedly advances the executor by 1 second, triggering
        /// any expired timers and running the test_fut, until `duration` is reached.
        fn advance_by(
            &mut self,
            duration: zx::Duration,
            mut test_fut: Pin<&mut impl Future<Output = ()>>,
        ) {
            assert_eq!(
                duration.into_nanos() % STEP_INCREMENT.into_nanos(),
                0,
                "duration {:?} is not divisible by STEP_INCREMENT",
                duration,
            );
            const_assert_eq!(
                TELEMETRY_QUERY_INTERVAL.into_nanos() % STEP_INCREMENT.into_nanos(),
                0
            );

            for _i in 0..(duration.into_nanos() / STEP_INCREMENT.into_nanos()) {
                self.exec.set_fake_time(fasync::Time::after(STEP_INCREMENT));
                let _ = self.exec.wake_expired_timers();
                assert_eq!(self.exec.run_until_stalled(&mut test_fut), Poll::Pending);

                let dev_svc_req_fut = self.dev_svc_stream.try_next();
                pin_mut!(dev_svc_req_fut);
                if let Poll::Ready(Ok(Some(request))) =
                    self.exec.run_until_stalled(&mut dev_svc_req_fut)
                {
                    match request {
                        DeviceServiceRequest::GetIfaceStats { iface_id, responder } => {
                            // Telemetry should make stats request to the client iface that's
                            // connected.
                            assert_eq!(iface_id, IFACE_ID);

                            match self.iface_stats_req_handler.as_mut() {
                                Some(handle_iface_stats_req) => handle_iface_stats_req(responder),
                                None => {
                                    let seed = fasync::Time::now().into_nanos() as u64;
                                    let mut iface_stats = fake_iface_stats(seed);
                                    responder
                                        .send(zx::sys::ZX_OK, Some(&mut iface_stats))
                                        .expect("expect sending IfaceStats response to succeed");
                                }
                            }
                        }
                        _ => {
                            panic!("unexpected request: {:?}", request);
                        }
                    }
                }

                // Respond to any potential Cobalt request, draining their payloads to
                // `self.cobalt_events`.
                self.drain_cobalt_events(&mut test_fut);

                assert_eq!(self.exec.run_until_stalled(&mut test_fut), Poll::Pending);
            }
        }

        fn set_iface_stats_req_handler(
            &mut self,
            iface_stats_req_handler: Box<dyn FnMut(DeviceServiceGetIfaceStatsResponder)>,
        ) {
            let _ = self.iface_stats_req_handler.replace(iface_stats_req_handler);
        }

        /// Advance executor by some duration until the next time `test_fut` handles periodic
        /// telemetry. This uses `self.advance_by` underneath.
        ///
        /// This function assumes that executor starts test_fut at time 0 (which should be true
        /// if TestHelper is created from `setup_test()`)
        fn advance_to_next_telemetry_checkpoint(
            &mut self,
            test_fut: Pin<&mut impl Future<Output = ()>>,
        ) {
            let now = fasync::Time::now();
            let remaining_interval = TELEMETRY_QUERY_INTERVAL.into_nanos()
                - (now.into_nanos() % TELEMETRY_QUERY_INTERVAL.into_nanos());
            self.advance_by(zx::Duration::from_nanos(remaining_interval), test_fut)
        }

        /// Continually execute the future and respond to any incoming Cobalt request with Ok.
        /// Append each metric request payload into `self.cobalt_events`.
        fn drain_cobalt_events(&mut self, test_fut: &mut (impl Future + Unpin)) {
            let mut made_progress = true;
            while made_progress {
                let _result = self.exec.run_until_stalled(test_fut);
                made_progress = false;
                while let Poll::Ready(Some(Ok(req))) =
                    self.exec.run_until_stalled(&mut self.cobalt_1dot1_stream.next())
                {
                    self.cobalt_events
                        .append(&mut req.respond_to_metric_req(fidl_fuchsia_metrics::Status::Ok));
                    made_progress = true;
                }
            }
        }

        fn get_logged_metrics(&self, metric_id: u32) -> Vec<MetricEvent> {
            self.cobalt_events.iter().filter(|ev| ev.metric_id == metric_id).cloned().collect()
        }

        fn send_connected_event(&self, latest_ap_state: BssDescription) {
            self.telemetry_sender
                .send(TelemetryEvent::Connected { iface_id: IFACE_ID, latest_ap_state });
        }
    }

    trait CobaltExt {
        // Respond to MetricEventLoggerRequest and extract its MetricEvent
        fn respond_to_metric_req(
            self,
            status: fidl_fuchsia_metrics::Status,
        ) -> Vec<fidl_fuchsia_metrics::MetricEvent>;
    }

    impl CobaltExt for MetricEventLoggerRequest {
        fn respond_to_metric_req(
            self,
            status: fidl_fuchsia_metrics::Status,
        ) -> Vec<fidl_fuchsia_metrics::MetricEvent> {
            match self {
                Self::LogOccurrence { metric_id, count, event_codes, responder } => {
                    assert!(responder.send(status).is_ok());
                    vec![MetricEvent {
                        metric_id,
                        event_codes,
                        payload: MetricEventPayload::Count(count),
                    }]
                }
                Self::LogInteger { metric_id, value, event_codes, responder } => {
                    assert!(responder.send(status).is_ok());
                    vec![MetricEvent {
                        metric_id,
                        event_codes,
                        payload: MetricEventPayload::IntegerValue(value),
                    }]
                }
                Self::LogIntegerHistogram { metric_id, histogram, event_codes, responder } => {
                    assert!(responder.send(status).is_ok());
                    vec![MetricEvent {
                        metric_id,
                        event_codes,
                        payload: MetricEventPayload::Histogram(histogram),
                    }]
                }
                Self::LogString { metric_id, string_value, event_codes, responder } => {
                    assert!(responder.send(status).is_ok());
                    vec![MetricEvent {
                        metric_id,
                        event_codes,
                        payload: MetricEventPayload::StringValue(string_value),
                    }]
                }
                Self::LogMetricEvents { events, responder } => {
                    assert!(responder.send(status).is_ok());
                    events
                }
                Self::LogCustomEvent { .. } => {
                    panic!("Testing for Cobalt LogCustomEvent not supported");
                }
            }
        }
    }

    fn setup_test() -> (TestHelper, Pin<Box<impl Future<Output = ()>>>) {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(0));

        let (dev_svc_proxy, dev_svc_stream) =
            create_proxy_and_stream::<fidl_fuchsia_wlan_device_service::DeviceServiceMarker>()
                .expect("failed to create DeviceService proxy");

        let (cobalt_1dot1_proxy, cobalt_1dot1_stream) =
            create_proxy_and_stream::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create MetricsEventLogger proxy");

        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("stats");
        let (telemetry_sender, test_fut) =
            serve_telemetry(dev_svc_proxy, cobalt_1dot1_proxy, inspect_node);
        let mut test_fut = Box::pin(test_fut);

        assert_eq!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

        let test_helper = TestHelper {
            telemetry_sender,
            inspector,
            dev_svc_stream,
            cobalt_1dot1_stream,
            iface_stats_req_handler: None,
            cobalt_events: vec![],
            exec,
        };
        (test_helper, test_fut)
    }

    fn fake_iface_stats(nth_req: u64) -> fidl_fuchsia_wlan_stats::IfaceStats {
        fidl_fuchsia_wlan_stats::IfaceStats {
            dispatcher_stats: fidl_fuchsia_wlan_stats::DispatcherStats {
                any_packet: fake_packet_counter(nth_req),
                mgmt_frame: fake_packet_counter(nth_req),
                ctrl_frame: fake_packet_counter(nth_req),
                data_frame: fake_packet_counter(nth_req),
            },
            mlme_stats: Some(Box::new(MlmeStats::ClientMlmeStats(ClientMlmeStats {
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
            out: Counter { count: 1 * nth_req, name: "out".to_string() },
            drop: Counter { count: 0 * nth_req, name: "drop".to_string() },
            in_bytes: Counter { count: 13 * nth_req, name: "in_bytes".to_string() },
            out_bytes: Counter { count: 13 * nth_req, name: "out_bytes".to_string() },
            drop_bytes: Counter { count: 0 * nth_req, name: "drop_bytes".to_string() },
        }
    }

    fn fake_rssi(nth_req: u64) -> fidl_fuchsia_wlan_stats::RssiStats {
        fidl_fuchsia_wlan_stats::RssiStats { hist: vec![nth_req] }
    }

    fn fake_antenna_id() -> Option<Box<fidl_fuchsia_wlan_stats::AntennaId>> {
        Some(Box::new(fidl_fuchsia_wlan_stats::AntennaId {
            freq: fidl_fuchsia_wlan_stats::AntennaFreq::Antenna5G,
            index: 0,
        }))
    }

    fn fake_noise_floor_histograms() -> Vec<fidl_fuchsia_wlan_stats::NoiseFloorHistogram> {
        vec![fidl_fuchsia_wlan_stats::NoiseFloorHistogram {
            hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // Noise floor bucket_index 165 indicates -90 dBm.
            noise_floor_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                bucket_index: 165,
                num_samples: 10,
            }],
            invalid_samples: 0,
        }]
    }

    fn fake_rssi_histograms() -> Vec<fidl_fuchsia_wlan_stats::RssiHistogram> {
        vec![fidl_fuchsia_wlan_stats::RssiHistogram {
            hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // RSSI bucket_index 225 indicates -30 dBm.
            rssi_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                bucket_index: 225,
                num_samples: 10,
            }],
            invalid_samples: 0,
        }]
    }

    fn fake_rx_rate_index_histograms() -> Vec<fidl_fuchsia_wlan_stats::RxRateIndexHistogram> {
        vec![fidl_fuchsia_wlan_stats::RxRateIndexHistogram {
            hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // Rate bucket_index 74 indicates HT BW40 MCS 14 SGI, which is 802.11n 270 Mb/s.
            // Rate bucket_index 75 indicates HT BW40 MCS 15 SGI, which is 802.11n 300 Mb/s.
            rx_rate_index_samples: vec![
                fidl_fuchsia_wlan_stats::HistBucket { bucket_index: 74, num_samples: 5 },
                fidl_fuchsia_wlan_stats::HistBucket { bucket_index: 75, num_samples: 5 },
            ],
            invalid_samples: 0,
        }]
    }

    fn fake_snr_histograms() -> Vec<fidl_fuchsia_wlan_stats::SnrHistogram> {
        vec![fidl_fuchsia_wlan_stats::SnrHistogram {
            hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
            antenna_id: fake_antenna_id(),
            // Signal to noise ratio bucket_index 60 indicates 60 dB.
            snr_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                bucket_index: 60,
                num_samples: 10,
            }],
            invalid_samples: 0,
        }]
    }

    fn fake_disconnect_info() -> DisconnectInfo {
        use crate::util::testing::generate_disconnect_info;
        let is_sme_reconnecting = false;
        let fidl_disconnect_info = generate_disconnect_info(is_sme_reconnecting);
        DisconnectInfo {
            connected_duration: 6.hours(),
            is_sme_reconnecting: fidl_disconnect_info.is_sme_reconnecting,
            reason_code: fidl_disconnect_info.reason_code,
            disconnect_source: fidl_disconnect_info.disconnect_source,
            latest_ap_state: random_bss_description!(Wpa2),
        }
    }
}

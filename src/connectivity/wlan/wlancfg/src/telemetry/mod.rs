// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod windowed_stats;

use {
    crate::telemetry::windowed_stats::WindowedStats,
    fidl_fuchsia_metrics::{MetricEvent, MetricEventPayload},
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
pub enum TelemetryEvent {
    /// Notify the telemetry event loop that network selection is complete.
    NetworkSelectionDecision {
        /// Type of network selection. If it's undirected and no candidate network is found,
        /// telemetry will toggle the `no_saved_neighbor` flag.
        network_selection_type: NetworkSelectionType,
        /// When there's a scan error, `num_candidates` should be Err.
        /// When `num_candidates` is `Ok(0)` for an undirected network selection, telemetry
        /// will toggle the `no_saved_neighbor` flag.  If the event loop is tracking downtime,
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
    Connected { iface_id: u16 },
    /// Notify the telemetry event loop that the client has disconnected.
    /// Subsequently, the telemetry event loop will increment the downtime counters periodically
    /// if TelemetrySender has requested downtime to be tracked via `track_subsequent_downtime`
    /// flag.
    Disconnected {
        /// Indicates whether subsequent period should be used to increment the downtime counters.
        track_subsequent_downtime: bool,
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

#[derive(Debug, Clone, PartialEq)]
enum ConnectionState {
    // Like disconnected, but no downtime is tracked.
    Idle,
    Connected {
        iface_id: u16,
        prev_counters: Option<fidl_fuchsia_wlan_stats::IfaceStats>,
    },
    Disconnected {
        /// Flag to track whether there's a saved neighbor in vicinity.
        latest_no_saved_neighbor_time: Option<fasync::Time>,
    },
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
            ConnectionState::Connected { iface_id, prev_counters } => {
                self.stats_logger.log_stat(StatOp::AddConnectedDuration(duration)).await;
                match self.dev_svc_proxy.get_iface_stats(*iface_id).await {
                    Ok((zx::sys::ZX_OK, Some(stats))) => {
                        if let Some(prev_counters) = prev_counters.as_ref() {
                            diff_and_log_counters(
                                &mut self.stats_logger,
                                prev_counters,
                                &stats,
                                duration,
                            )
                            .await;
                        }
                        let _prev = prev_counters.replace(*stats);
                    }
                    _ => {
                        self.get_iface_stats_fail_count.add(1);
                        let _ = prev_counters.take();
                    }
                }
            }
            ConnectionState::Disconnected { latest_no_saved_neighbor_time } => {
                self.stats_logger.log_stat(StatOp::AddDowntimeDuration(duration)).await;
                if let Some(prev) = latest_no_saved_neighbor_time.take() {
                    self.stats_logger
                        .log_stat(StatOp::AddDowntimeNoSavedNeighborDuration(now - prev))
                        .await;
                    *latest_no_saved_neighbor_time = Some(now);
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
                        // Saved neighbors are seen, so clear the `no_saved_neighbor` flag. Account
                        // for any untracked time to the `downtime_no_saved_neighbor_duration`
                        // counter.
                        if let ConnectionState::Disconnected { latest_no_saved_neighbor_time } =
                            &mut self.connection_state
                        {
                            if let Some(prev) = latest_no_saved_neighbor_time.take() {
                                self.stats_logger.queue_stat_op(
                                    StatOp::AddDowntimeNoSavedNeighborDuration(now - prev),
                                );
                            }
                        }
                    }
                    Ok(0) if network_selection_type == NetworkSelectionType::Undirected => {
                        // No saved neighbor is seen. If `no_saved_neighbor` flag isn't set, then
                        // set it to the current time. Otherwise, do nothing because the telemetry
                        // loop will account for untracked downtime during periodic telemetry run.
                        if let ConnectionState::Disconnected { latest_no_saved_neighbor_time } =
                            &mut self.connection_state
                        {
                            if latest_no_saved_neighbor_time.is_none() {
                                *latest_no_saved_neighbor_time = Some(now);
                            }
                        }
                    }
                    _ => (),
                }
            }
            TelemetryEvent::Connected { iface_id } => {
                let duration = now - self.last_checked_connection_state;
                if let ConnectionState::Disconnected { .. } = self.connection_state {
                    self.stats_logger.queue_stat_op(StatOp::AddDowntimeDuration(duration));
                }
                self.connection_state =
                    ConnectionState::Connected { iface_id, prev_counters: None };
                self.last_checked_connection_state = now;
            }
            TelemetryEvent::Disconnected { track_subsequent_downtime } => {
                self.stats_logger.log_stat(StatOp::AddDisconnectCount).await;

                let duration = now - self.last_checked_connection_state;
                if let ConnectionState::Connected { .. } = self.connection_state {
                    self.stats_logger.queue_stat_op(StatOp::AddConnectedDuration(duration));
                }
                self.connection_state = if track_subsequent_downtime {
                    ConnectionState::Disconnected { latest_no_saved_neighbor_time: None }
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
            StatOp::AddDisconnectCount => {
                log_cobalt_1dot1!(
                    self.cobalt_1dot1_proxy,
                    log_occurrence,
                    metrics::TOTAL_DISCONNECT_COUNT_METRIC_ID,
                    1,
                    &[],
                );
                StatCounters { disconnect_count: 1, ..zero }
            }
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
        fidl_fuchsia_wlan_device_service::{
            DeviceServiceGetIfaceStatsResponder, DeviceServiceRequest,
        },
        fidl_fuchsia_wlan_stats::{self, ClientMlmeStats, Counter, PacketCounter},
        fuchsia_inspect::{assert_data_tree, testing::NonZeroUintProperty, Inspector},
        futures::{pin_mut, task::Poll, TryStreamExt},
        std::{cmp::min, collections::HashMap, pin::Pin},
        wlan_common::assert_variant,
    };

    const STEP_INCREMENT: zx::Duration = zx::Duration::from_seconds(1);
    const IFACE_ID: u16 = 1;

    #[fuchsia::test]
    fn test_stat_cycles() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false });
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false });
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
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(5.seconds(), test_fut.as_mut());

        // Disconnect but not track downtime. Downtime counter should not increase.
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // Disconnect and track downtime.
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
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
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false });

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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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

        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
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

        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false });
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(12.hours(), test_fut.as_mut());

        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(6.hours(), test_fut.as_mut());

        // Indicate that there's no saved neighbor in vicinity
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            network_selection_type: NetworkSelectionType::Undirected,
            num_candidates: Ok(0),
            selected_any: false,
        });

        test_helper.advance_by(6.hours(), test_fut.as_mut());

        let uptime_ratios: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::CONNECTED_UPTIME_RATIO_METRIC_ID)
            .collect();
        assert_eq!(uptime_ratios.len(), 1);
        // 12 hours of uptime, 6 hours of adjusted downtime => 66.66% uptime
        assert_eq!(uptime_ratios[0].payload, MetricEventPayload::IntegerValue(6666));

        let uptime_ratio_breakdowns: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::DEVICE_CONNECTED_UPTIME_RATIO_BREAKDOWN_METRIC_ID)
            .collect();
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(6.hours(), test_fut.as_mut());

        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(18.hours(), test_fut.as_mut());

        let dpdc_ratios: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::DISCONNECT_PER_DAY_CONNECTED_METRIC_ID)
            .collect();
        assert_eq!(dpdc_ratios.len(), 1);
        // 1 disconnect, 0.25 day connected => 4 disconnects per day connected
        // (which equals 40_0000 in TenThousandth unit)
        assert_eq!(dpdc_ratios[0].payload, MetricEventPayload::IntegerValue(40_000));

        let dpdc_ratio_breakdowns: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| {
                ev.metric_id == metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_METRIC_ID
            })
            .collect();
        assert_eq!(dpdc_ratio_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdownMetricDimensionDpdcRatio::UpTo5
                as u32]
        );
        assert_eq!(dpdc_ratio_breakdowns[0].payload, MetricEventPayload::Count(1));

        let dpdc_ratio_7d_breakdowns: Vec<_> = test_helper
            .cobalt_events
            .drain(..)
            .filter(|ev| {
                ev.metric_id == metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_7D_METRIC_ID
            })
            .collect();
        assert_eq!(dpdc_ratio_7d_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_7d_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdown7dMetricDimensionDpdcRatio::UpTo5
                as u32]
        );
        assert_eq!(dpdc_ratio_7d_breakdowns[0].payload, MetricEventPayload::Count(1));

        // Connect for another 1 day to dilute the 7d ratio
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(24.hours(), test_fut.as_mut());

        // No disconnect in the last day, so the 1d ratio would be 0.
        let dpdc_ratios: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::DISCONNECT_PER_DAY_CONNECTED_METRIC_ID)
            .collect();
        assert_eq!(dpdc_ratios.len(), 1);
        assert_eq!(dpdc_ratios[0].payload, MetricEventPayload::IntegerValue(0));

        let dpdc_ratio_breakdowns: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| {
                ev.metric_id == metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_METRIC_ID
            })
            .collect();
        assert_eq!(dpdc_ratio_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdownMetricDimensionDpdcRatio::_0 as u32]
        );
        assert_eq!(dpdc_ratio_breakdowns[0].payload, MetricEventPayload::Count(1));

        // In the last 7 days, 1 disconnects and 1.25 days connected => 0.8 dpdc ratio
        let dpdc_ratio_7d_breakdowns: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| {
                ev.metric_id == metrics::DEVICE_DISCONNECT_PER_DAY_CONNECTED_BREAKDOWN_7D_METRIC_ID
            })
            .collect();
        assert_eq!(dpdc_ratio_7d_breakdowns.len(), 1);
        assert_eq!(
            dpdc_ratio_7d_breakdowns[0].event_codes,
            &[metrics::DeviceDisconnectPerDayConnectedBreakdown7dMetricDimensionDpdcRatio::UpTo1
                as u32]
        );
        assert_eq!(dpdc_ratio_7d_breakdowns[0].payload, MetricEventPayload::Count(1));
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
                        // computing counters, this leads to 1 hour of high TX drop rate.
                        stats.tx_frame.drop.count = 3 * min(
                            seed,
                            (1.hour() + TELEMETRY_QUERY_INTERVAL).into_seconds() as u64,
                        );
                        // RX drop rate stops increasing at 2 hour + TELEMETRY_QUERY_INTERVAL mark.
                        stats.rx_frame.drop.count = 3 * min(
                            seed,
                            (2.hour() + TELEMETRY_QUERY_INTERVAL).into_seconds() as u64,
                        );
                        // RX total stops increasing at 20 hour mark
                        stats.rx_frame.in_.count = 10 * min(seed, 20.hours().into_seconds() as u64);
                    }
                    _ => panic!("expect ClientMlmeStats"),
                },
                _ => panic!("expect mlme_stats to be available"),
            }
            responder
                .send(zx::sys::ZX_OK, Some(&mut iface_stats))
                .expect("expect sending IfaceStats response to succeed");
        }));

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(24.hours(), test_fut.as_mut());

        let ratios: HashMap<_, _> =
            test_helper.cobalt_events.drain(..).map(|ev| (ev.metric_id, ev.payload)).collect();
        let high_rx_drop_time_ratio =
            ratios.get(&metrics::TIME_RATIO_WITH_HIGH_RX_PACKET_DROP_METRIC_ID);
        // 2 hours of high RX drop rate, 24 hours connected => 8.33% duration
        assert_variant!(high_rx_drop_time_ratio, Some(&MetricEventPayload::IntegerValue(833)));

        let high_tx_drop_time_ratio =
            ratios.get(&metrics::TIME_RATIO_WITH_HIGH_TX_PACKET_DROP_METRIC_ID);
        // 1 hour of high RX drop rate, 24 hours connected => 4.16% duration
        assert_variant!(high_tx_drop_time_ratio, Some(&MetricEventPayload::IntegerValue(416)));

        // 4 hours of no RX, 24 hours connected => 16.66% duration
        let no_rx_time_ratio = ratios.get(&metrics::TIME_RATIO_WITH_NO_RX_METRIC_ID);
        assert_variant!(no_rx_time_ratio, Some(&MetricEventPayload::IntegerValue(1666)));
    }

    #[fuchsia::test]
    fn test_log_daily_rx_tx_ratio_cobalt_metrics_zero() {
        // This test is to verify that when the RX/TX ratios are 0 (there's no issue), we still
        // log to Cobalt.
        let (mut test_helper, mut test_fut) = setup_test();

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(24.hours(), test_fut.as_mut());

        let ratios: HashMap<_, _> =
            test_helper.cobalt_events.drain(..).map(|ev| (ev.metric_id, ev.payload)).collect();
        let high_rx_drop_time_ratio =
            ratios.get(&metrics::TIME_RATIO_WITH_HIGH_RX_PACKET_DROP_METRIC_ID);
        assert_variant!(high_rx_drop_time_ratio, Some(&MetricEventPayload::IntegerValue(0)));

        let high_tx_drop_time_ratio =
            ratios.get(&metrics::TIME_RATIO_WITH_HIGH_TX_PACKET_DROP_METRIC_ID);
        assert_variant!(high_tx_drop_time_ratio, Some(&MetricEventPayload::IntegerValue(0)));

        let no_rx_time_ratio = ratios.get(&metrics::TIME_RATIO_WITH_NO_RX_METRIC_ID);
        assert_variant!(no_rx_time_ratio, Some(&MetricEventPayload::IntegerValue(0)));
    }

    #[fuchsia::test]
    fn test_log_hourly_fleetwise_uptime_cobalt_metrics() {
        let (mut test_helper, mut test_fut) = setup_test();

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());

        let total_wlan_uptime_durs: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::TOTAL_WLAN_UPTIME_NEAR_SAVED_NETWORK_METRIC_ID)
            .collect();
        assert_eq!(total_wlan_uptime_durs.len(), 1);
        assert_eq!(
            total_wlan_uptime_durs[0].payload,
            MetricEventPayload::IntegerValue(1.hour().into_micros())
        );

        let connected_durs: Vec<_> = test_helper
            .cobalt_events
            .drain(..)
            .filter(|ev| ev.metric_id == metrics::TOTAL_CONNECTED_UPTIME_METRIC_ID)
            .collect();
        assert_eq!(connected_durs.len(), 1);
        assert_eq!(
            connected_durs[0].payload,
            MetricEventPayload::IntegerValue(1.hour().into_micros())
        );

        test_helper.advance_by(30.minutes(), test_fut.as_mut());

        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
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

        let total_wlan_uptime_durs: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::TOTAL_WLAN_UPTIME_NEAR_SAVED_NETWORK_METRIC_ID)
            .collect();
        assert_eq!(total_wlan_uptime_durs.len(), 1);
        // 30 minutes connected uptime + 15 minutes downtime near saved network
        assert_eq!(
            total_wlan_uptime_durs[0].payload,
            MetricEventPayload::IntegerValue(45.minutes().into_micros())
        );

        let connected_durs: Vec<_> = test_helper
            .cobalt_events
            .drain(..)
            .filter(|ev| ev.metric_id == metrics::TOTAL_CONNECTED_UPTIME_METRIC_ID)
            .collect();
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

        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(1.hour(), test_fut.as_mut());

        let rx_high_drop_durs: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::TOTAL_TIME_WITH_HIGH_RX_PACKET_DROP_METRIC_ID)
            .collect();
        assert_eq!(rx_high_drop_durs.len(), 1);
        assert_eq!(
            rx_high_drop_durs[0].payload,
            MetricEventPayload::IntegerValue(20.minutes().into_micros())
        );

        let tx_high_drop_durs: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::TOTAL_TIME_WITH_HIGH_TX_PACKET_DROP_METRIC_ID)
            .collect();
        assert_eq!(tx_high_drop_durs.len(), 1);
        assert_eq!(
            tx_high_drop_durs[0].payload,
            MetricEventPayload::IntegerValue(10.minutes().into_micros())
        );

        let no_rx_durs: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::TOTAL_TIME_WITH_NO_RX_METRIC_ID)
            .collect();
        assert_eq!(no_rx_durs.len(), 1);
        assert_eq!(
            no_rx_durs[0].payload,
            MetricEventPayload::IntegerValue(15.minutes().into_micros())
        );
    }

    #[fuchsia::test]
    fn test_log_disconnect_metric() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.telemetry_sender.send(TelemetryEvent::Connected { iface_id: IFACE_ID });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(30.minutes(), test_fut.as_mut());

        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
        test_helper.drain_cobalt_events(&mut test_fut);

        let disconnect_counts: Vec<_> = test_helper
            .cobalt_events
            .iter()
            .filter(|ev| ev.metric_id == metrics::TOTAL_DISCONNECT_COUNT_METRIC_ID)
            .collect();
        assert_eq!(disconnect_counts.len(), 1);
        assert_eq!(disconnect_counts[0].payload, MetricEventPayload::Count(1));
    }

    struct TestHelper {
        telemetry_sender: TelemetrySender,
        inspector: Inspector,
        dev_svc_stream: fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream,
        cobalt_1dot1_stream: fidl_fuchsia_metrics::MetricEventLoggerRequestStream,
        iface_stats_req_handler: Option<Box<dyn FnMut(DeviceServiceGetIfaceStatsResponder)>>,
        /// As requests to Cobalt are responded to via `self.drain_cobalt_events()`,
        /// their payloads are drained to this Vec
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
}

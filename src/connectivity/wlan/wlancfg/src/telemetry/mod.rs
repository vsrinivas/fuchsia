// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod windowed_stats;

use {
    crate::telemetry::windowed_stats::WindowedStats,
    fidl_fuchsia_wlan_stats::MlmeStats,
    fuchsia_async as fasync,
    fuchsia_inspect::{Inspector, Node as InspectNode, NumericProperty, UintProperty},
    fuchsia_inspect_contrib::inspect_insert,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, select, Future, FutureExt, StreamExt},
    log::{info, warn},
    num_traits::SaturatingAdd,
    parking_lot::Mutex,
    static_assertions::const_assert_eq,
    std::{
        ops::Add,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
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
    inspect_node: InspectNode,
) -> (TelemetrySender, impl Future<Output = ()>) {
    let (sender, mut receiver) = mpsc::channel::<TelemetryEvent>(TELEMETRY_EVENT_BUFFER_SIZE);
    let fut = async move {
        let mut report_interval_stream = fasync::Interval::new(TELEMETRY_QUERY_INTERVAL);
        const ONE_HOUR: zx::Duration = zx::Duration::from_hours(1);
        const_assert_eq!(ONE_HOUR.into_nanos() % TELEMETRY_QUERY_INTERVAL.into_nanos(), 0);
        const INTERVAL_TICKS_PER_HR: u64 =
            (ONE_HOUR.into_nanos() / TELEMETRY_QUERY_INTERVAL.into_nanos()) as u64;
        let mut interval_tick = 0;
        let mut telemetry = Telemetry::new(dev_svc_proxy, inspect_node);
        loop {
            select! {
                event = receiver.next() => {
                    if let Some(event) = event {
                        telemetry.handle_telemetry_event(event);
                    }
                }
                _ = report_interval_stream.next() => {
                    telemetry.handle_periodic_telemetry().await;
                    // This ensures that `signal_hr_passed` is always called after
                    // `handle_periodic_telemetry` at the hour mark. This is mainly for ease
                    // of testing.
                    interval_tick = (interval_tick + 1) % INTERVAL_TICKS_PER_HR;
                    if interval_tick == 0 {
                        telemetry.signal_hr_passed();
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
                let counters = counters_mutex_guard.windowed_stat();
                inspect_insert!(inspector.root(), {
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
        inspect_node: InspectNode,
    ) -> Self {
        let stats_logger = StatsLogger::new();
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

        self.stats_logger.log_queued_stats();

        match &mut self.connection_state {
            ConnectionState::Idle => (),
            ConnectionState::Connected { iface_id, prev_counters } => {
                self.stats_logger.log_stat(StatOp::AddConnectedDuration(duration));
                match self.dev_svc_proxy.get_iface_stats(*iface_id).await {
                    Ok((zx::sys::ZX_OK, Some(stats))) => {
                        if let Some(prev_counters) = prev_counters.as_ref() {
                            diff_and_log_counters(
                                &mut self.stats_logger,
                                prev_counters,
                                &stats,
                                duration,
                            );
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
                self.stats_logger.log_stat(StatOp::AddDowntimeDuration(duration));
                if let Some(prev) = latest_no_saved_neighbor_time.take() {
                    self.stats_logger
                        .log_stat(StatOp::AddDowntimeNoSavedNeighborDuration(now - prev));
                    *latest_no_saved_neighbor_time = Some(now);
                }
            }
        }
        self.last_checked_connection_state = now;
    }

    pub fn handle_telemetry_event(&mut self, event: TelemetryEvent) {
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
                self.stats_logger.log_stat(StatOp::AddDisconnectCount);

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

    pub fn signal_hr_passed(&mut self) {
        self.stats_logger.handle_hr_passed();
    }
}

const PACKET_DROP_RATE_THRESHOLD: f64 = 0.02;

fn diff_and_log_counters(
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
        stats_logger.log_stat(StatOp::AddTxHighPacketDropDuration(duration));
    }
    if rx_drop_rate > PACKET_DROP_RATE_THRESHOLD {
        stats_logger.log_stat(StatOp::AddRxHighPacketDropDuration(duration));
    }
    if rx_total == 0 {
        stats_logger.log_stat(StatOp::AddNoRxDuration(duration));
    }
}

struct StatsLogger {
    last_1d_stats: Arc<Mutex<WindowedStats<StatCounters>>>,
    last_7d_stats: Arc<Mutex<WindowedStats<StatCounters>>>,
    stat_ops: Vec<StatOp>,
    hr_tick: u32,
}

impl StatsLogger {
    pub fn new() -> Self {
        Self {
            last_1d_stats: Arc::new(Mutex::new(WindowedStats::new(24))),
            last_7d_stats: Arc::new(Mutex::new(WindowedStats::new(7))),
            stat_ops: vec![],
            hr_tick: 0,
        }
    }

    fn log_stat(&mut self, stat_op: StatOp) {
        let zero = StatCounters::default();
        let addition = match stat_op {
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

    fn log_queued_stats(&mut self) {
        while let Some(stat_op) = self.stat_ops.pop() {
            self.log_stat(stat_op);
        }
    }

    fn handle_hr_passed(&mut self) {
        self.hr_tick = (self.hr_tick + 1) % 24;
        self.last_1d_stats.lock().slide_window();
        if self.hr_tick == 0 {
            self.last_7d_stats.lock().slide_window();
        }
    }
}

enum StatOp {
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
    connected_duration: zx::Duration,
    downtime_duration: zx::Duration,
    downtime_no_saved_neighbor_duration: zx::Duration,
    disconnect_count: u64,
    tx_high_packet_drop_duration: zx::Duration,
    rx_high_packet_drop_duration: zx::Duration,
    no_rx_duration: zx::Duration,
}

// `Add` implementation is required to implement `SaturatingAdd` down below.
impl Add for StatCounters {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Self {
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
        fidl_fuchsia_wlan_device_service::{
            DeviceServiceGetIfaceStatsResponder, DeviceServiceRequest,
        },
        fidl_fuchsia_wlan_stats::{self, ClientMlmeStats, Counter, PacketCounter},
        fuchsia_inspect::{assert_data_tree, testing::NonZeroUintProperty, Inspector},
        fuchsia_zircon::DurationNum,
        futures::{pin_mut, task::Poll, TryStreamExt},
        std::pin::Pin,
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
                    connected_duration: (24.hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: (24.hours() - TELEMETRY_QUERY_INTERVAL).into_nanos(),
                },
            }
        });

        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    // The first hour window is now discarded, so it only shows 23 hours
                    // of connected duration.
                    connected_duration: 23.hours().into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 24.hours().into_nanos(),
                },
            }
        });

        test_helper.advance_by(2.hours(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 23.hours().into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 26.hours().into_nanos(),
                },
            }
        });

        // Disconnect now
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // Now the 1d counter should decrease
        test_helper.advance_by(8.hours(), test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 15.hours().into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 26.hours().into_nanos(),
                },
            }
        });

        // The 7d counter does not decrease before the 7th day
        test_helper.advance_by(14.hours(), test_fut.as_mut());
        test_helper.advance_by((5 * 24).hours() - TELEMETRY_QUERY_INTERVAL, test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 26.hours().into_nanos(),
                },
            }
        });

        // On the 7th day, the first 24 hours of connected duration is deducted
        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 2.hours().into_nanos(),
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
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

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
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

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

    struct TestHelper {
        telemetry_sender: TelemetrySender,
        inspector: Inspector,
        dev_svc_stream: fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream,
        iface_stats_req_handler: Option<Box<dyn FnMut(DeviceServiceGetIfaceStatsResponder)>>,

        // Note: keep the executor field last in the struct so it gets dropped last.
        exec: fasync::TestExecutor,
    }

    impl TestHelper {
        // Advance executor by `duration`.
        // This function repeatedly advances the executor by 1 second, triggering any expired timers
        // and running the test_fut, until `duration` is reached.
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

                assert_eq!(self.exec.run_until_stalled(&mut test_fut), Poll::Pending);
            }
        }

        fn set_iface_stats_req_handler(
            &mut self,
            iface_stats_req_handler: Box<dyn FnMut(DeviceServiceGetIfaceStatsResponder)>,
        ) {
            let _ = self.iface_stats_req_handler.replace(iface_stats_req_handler);
        }

        // Advance executor by some duration until the next time `test_fut` handles periodic
        // telemetry. This uses `self.advance_by` underneath.
        //
        // This function assumes that executor starts test_fut at time 0 (which should be true
        // if TestHelper is created from `setup_test()`)
        fn advance_to_next_telemetry_checkpoint(
            &mut self,
            test_fut: Pin<&mut impl Future<Output = ()>>,
        ) {
            let now = fasync::Time::now();
            let remaining_interval = TELEMETRY_QUERY_INTERVAL.into_nanos()
                - (now.into_nanos() % TELEMETRY_QUERY_INTERVAL.into_nanos());
            self.advance_by(zx::Duration::from_nanos(remaining_interval), test_fut)
        }
    }

    fn setup_test() -> (TestHelper, Pin<Box<impl Future<Output = ()>>>) {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(0));

        let (dev_svc_proxy, dev_svc_stream) =
            create_proxy_and_stream::<fidl_fuchsia_wlan_device_service::DeviceServiceMarker>()
                .expect("failed to create DeviceService proxy");
        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("stats");
        let (telemetry_sender, test_fut) = serve_telemetry(dev_svc_proxy, inspect_node);
        let mut test_fut = Box::pin(test_fut);

        assert_eq!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

        let test_helper = TestHelper {
            telemetry_sender,
            inspector,
            dev_svc_stream,
            iface_stats_req_handler: None,
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

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod windowed_stats;

use {
    crate::telemetry::windowed_stats::WindowedStats,
    fuchsia_async as fasync,
    fuchsia_inspect::{Inspector, Node as InspectNode},
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
    /// Notify the telemetry event loop that network selection has started.
    StartNetworkSelection {
        /// Type of network selection. This field is currently unused.
        network_selection_type: NetworkSelectionType,
    },
    /// Notify the telemetry event loop that network selection is complete.
    NetworkSelectionDecision {
        /// When there's a scan error, `num_candidates` should be Err.
        /// When `num_candidates` is `Ok(0)` and the telemetry event loop is tracking downtime,
        /// the event loop will use the period of network selection to increment the
        /// `downtime_no_saved_neighbor_duration` counter. This would later be used to
        /// adjust the raw downtime.
        num_candidates: Result<usize, ()>,
        /// Whether a network has been selected. This field is currently unused.
        selected_any: bool,
    },
    /// Notify the telemetry event loop that the client has connected.
    /// Subsequently, the telemetry event loop will increment the `connected_duration` counter
    /// periodically.
    Connected,
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
pub fn serve_telemetry(inspect_node: InspectNode) -> (TelemetrySender, impl Future<Output = ()>) {
    let (sender, mut receiver) = mpsc::channel::<TelemetryEvent>(TELEMETRY_EVENT_BUFFER_SIZE);
    let fut = async move {
        let mut report_interval_stream = fasync::Interval::new(TELEMETRY_QUERY_INTERVAL);
        const ONE_HOUR: zx::Duration = zx::Duration::from_hours(1);
        const_assert_eq!(ONE_HOUR.into_nanos() % TELEMETRY_QUERY_INTERVAL.into_nanos(), 0);
        const INTERVAL_TICKS_PER_HR: u64 =
            (ONE_HOUR.into_nanos() / TELEMETRY_QUERY_INTERVAL.into_nanos()) as u64;
        let mut interval_tick = 0;
        let mut telemetry = Telemetry::new(inspect_node);
        loop {
            select! {
                event = receiver.next() => {
                    if let Some(event) = event {
                        telemetry.handle_telemetry_event(event);
                    }
                }
                _ = report_interval_stream.next() => {
                    telemetry.handle_periodic_telemetry();
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
    Connected,
    Disconnected,
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
                });
            }
            Ok(inspector)
        }
        .boxed()
    });
}

pub struct Telemetry {
    connection_state: ConnectionState,
    last_checked_connection_state: fasync::Time,
    network_selection_start_time: Option<fasync::Time>,
    stats_logger: StatsLogger,
    _inspect_node: InspectNode,
}

impl Telemetry {
    pub fn new(inspect_node: InspectNode) -> Self {
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
        Self {
            connection_state: ConnectionState::Idle,
            last_checked_connection_state: fasync::Time::now(),
            network_selection_start_time: None,
            stats_logger,
            _inspect_node: inspect_node,
        }
    }

    pub fn handle_periodic_telemetry(&mut self) {
        let now = fasync::Time::now();
        let duration = now - self.last_checked_connection_state;
        match &self.connection_state {
            ConnectionState::Idle => (),
            ConnectionState::Connected => {
                self.stats_logger.log_stat(StatOp::AddConnectedDuration(duration));
            }
            ConnectionState::Disconnected => {
                self.stats_logger.log_stat(StatOp::AddDowntimeDuration(duration));
            }
        }
        self.last_checked_connection_state = now;
    }

    pub fn handle_telemetry_event(&mut self, event: TelemetryEvent) {
        let now = fasync::Time::now();
        match event {
            TelemetryEvent::StartNetworkSelection { .. } => {
                let _prev = self.network_selection_start_time.replace(now);
            }
            TelemetryEvent::NetworkSelectionDecision { num_candidates, .. } => {
                if let Some(start_time) = self.network_selection_start_time.take() {
                    match self.connection_state {
                        ConnectionState::Disconnected => match num_candidates {
                            Ok(0) => {
                                // TODO(fxbug.dev/80699): Track a `no_saved_neighbor` flag and add
                                //                        all subsequent downtime to this counter.
                                self.stats_logger.log_stat(
                                    StatOp::AddDowntimeNoSavedNeighborDuration(now - start_time),
                                );
                            }
                            _ => (),
                        },
                        _ => (),
                    }
                }
            }
            TelemetryEvent::Connected => {
                let duration = now - self.last_checked_connection_state;
                if let ConnectionState::Disconnected = self.connection_state {
                    self.stats_logger.log_stat(StatOp::AddDowntimeDuration(duration));
                }
                self.connection_state = ConnectionState::Connected;
                self.last_checked_connection_state = now;
            }
            TelemetryEvent::Disconnected { track_subsequent_downtime } => {
                let duration = now - self.last_checked_connection_state;
                if let ConnectionState::Connected = self.connection_state {
                    self.stats_logger.log_stat(StatOp::AddConnectedDuration(duration));
                }
                self.connection_state = if track_subsequent_downtime {
                    ConnectionState::Disconnected
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

struct StatsLogger {
    last_1d_stats: Arc<Mutex<WindowedStats<StatCounters>>>,
    last_7d_stats: Arc<Mutex<WindowedStats<StatCounters>>>,
    hr_tick: u32,
}

impl StatsLogger {
    pub fn new() -> Self {
        Self {
            last_1d_stats: Arc::new(Mutex::new(WindowedStats::new(24))),
            last_7d_stats: Arc::new(Mutex::new(WindowedStats::new(7))),
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
        };
        self.last_1d_stats.lock().saturating_add(&addition);
        self.last_7d_stats.lock().saturating_add(&addition);
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
}

#[derive(Clone, Default)]
struct StatCounters {
    connected_duration: zx::Duration,
    downtime_duration: zx::Duration,
    downtime_no_saved_neighbor_duration: zx::Duration,
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
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_data_tree, Inspector},
        fuchsia_zircon::DurationNum,
        futures::task::Poll,
        std::pin::Pin,
    };

    const STEP_INCREMENT: zx::Duration = zx::Duration::from_seconds(1);

    #[fuchsia::test]
    fn test_stat_cycles() {
        let (mut test_helper, mut test_fut) = setup_test();
        test_helper.telemetry_sender.send(TelemetryEvent::Connected);
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected);
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected);
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(5.seconds(), test_fut.as_mut());

        // Disconnect but not track downtime. Downtime counter should not increase.
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // The 5 seconds connected duration is accounted for right away
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 5.seconds().into_nanos(),
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
                "7d_counters": contains {
                    connected_duration: 5.seconds().into_nanos(),
                    downtime_duration: 0i64,
                    downtime_no_saved_neighbor_duration: 0i64,
                },
            }
        });

        // At next telemetry checkpoint, `test_fut` updates the downtime duration
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
        test_helper.telemetry_sender.send(TelemetryEvent::Connected);
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // Disconnect and track downtime.
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: true });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(5.seconds(), test_fut.as_mut());
        test_helper.telemetry_sender.send(TelemetryEvent::StartNetworkSelection {
            network_selection_type: NetworkSelectionType::Undirected,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(2.seconds(), test_fut.as_mut());
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
            num_candidates: Ok(0),
            selected_any: false,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        let prev = fasync::Time::now();
        test_helper.advance_to_next_telemetry_checkpoint(test_fut.as_mut());
        let downtime_duration = (7.seconds() + (fasync::Time::now() - prev)).into_nanos();
        assert_data_tree!(test_helper.inspector, root: {
            stats: contains {
                "1d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: downtime_duration,
                    downtime_no_saved_neighbor_duration: 2.seconds().into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: downtime_duration,
                    downtime_no_saved_neighbor_duration: 2.seconds().into_nanos(),
                },
            }
        });

        // Disconnect but don't track downtime
        test_helper
            .telemetry_sender
            .send(TelemetryEvent::Disconnected { track_subsequent_downtime: false });

        // Go through the same sequence of network selection as before
        test_helper.advance_by(5.seconds(), test_fut.as_mut());
        test_helper.telemetry_sender.send(TelemetryEvent::StartNetworkSelection {
            network_selection_type: NetworkSelectionType::Undirected,
        });
        assert_eq!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        test_helper.advance_by(2.seconds(), test_fut.as_mut());
        test_helper.telemetry_sender.send(TelemetryEvent::NetworkSelectionDecision {
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
                    downtime_duration: downtime_duration,
                    downtime_no_saved_neighbor_duration: 2.seconds().into_nanos(),
                },
                "7d_counters": contains {
                    connected_duration: 0i64,
                    downtime_duration: downtime_duration,
                    downtime_no_saved_neighbor_duration: 2.seconds().into_nanos(),
                },
            }
        });
    }

    struct TestHelper {
        exec: fasync::TestExecutor,
        telemetry_sender: TelemetrySender,
        inspector: Inspector,
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
            }
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

        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("stats");
        let (telemetry_sender, test_fut) = serve_telemetry(inspect_node);
        let mut test_fut = Box::pin(test_fut);

        assert_eq!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

        let test_helper = TestHelper { exec, telemetry_sender, inspector };
        (test_helper, test_fut)
    }
}

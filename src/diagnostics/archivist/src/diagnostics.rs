// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::archive::EventFileGroupStatsMap,
    anyhow::Error,
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_inspect::{
        component, health::Reporter, HistogramProperty, Node, NumericProperty, UintProperty,
    },
    fuchsia_zircon::{self as zx, Duration},
    lazy_static::lazy_static,
    parking_lot::Mutex,
    std::collections::BTreeMap,
    std::sync::Arc,
};

lazy_static! {
    static ref GROUPS: Arc<Mutex<Groups>> = Arc::new(Mutex::new(Groups::new(
        component::inspector().root().create_child("archived_events")
    )));
}

enum GroupData {
    Node(Node),
    Count(UintProperty),
}

struct Groups {
    node: Node,
    children: Vec<GroupData>,
}

impl Groups {
    fn new(node: Node) -> Self {
        Groups { node, children: vec![] }
    }

    fn replace(&mut self, stats: &EventFileGroupStatsMap) {
        self.children.clear();
        for (name, stat) in stats {
            let node = self.node.create_child(name);
            let files = node.create_uint("file_count", stat.file_count as u64);
            let size = node.create_uint("size_in_bytes", stat.size);

            self.children.push(GroupData::Node(node));
            self.children.push(GroupData::Count(files));
            self.children.push(GroupData::Count(size));
        }
    }
}

pub fn init() {
    //TODO(fxbug.dev/36574): Replace log calls once archivist can use LogSink service.
    component::health().set_starting_up();
}

pub fn root() -> &'static Node {
    component::inspector().root()
}

pub fn serve(service_fs: &mut ServiceFs<impl ServiceObjTrait>) -> Result<(), Error> {
    component::inspector().serve(service_fs)?;
    Ok(())
}

pub(crate) fn set_group_stats(stats: &EventFileGroupStatsMap) {
    GROUPS.lock().replace(stats);
}

pub struct ArchiveAccessorStats {
    /// Inspect node for tracking usage/health metrics of diagnostics platform.
    pub archive_accessor_node: fuchsia_inspect::Node,

    /// Metrics aggregated across all client connections.
    pub global_stats: Arc<ArchiveAccessorStatsGlobal>,

    /// Global stats tracking the usages of StreamDiagnostics for
    /// exfiltrating inspect data.
    pub global_inspect_stats: Arc<DiagnosticsServerStatsGlobal>,

    /// Global stats tracking the usages of StreamDiagnostics for
    /// exfiltrating lifecycle data.
    pub global_lifecycle_stats: Arc<DiagnosticsServerStatsGlobal>,

    /// Global stats tracking the usages of StreamDiagnostics for
    /// exfiltrating logs.
    pub global_logs_stats: Arc<DiagnosticsServerStatsGlobal>,
}

pub struct ArchiveAccessorStatsGlobal {
    /// Property tracking number of opening connections to any archive_accessor instance.
    pub archive_accessor_connections_opened: fuchsia_inspect::UintProperty,
    /// Property tracking number of closing connections to any archive_accessor instance.
    pub archive_accessor_connections_closed: fuchsia_inspect::UintProperty,
    /// Number of requests to a single ArchiveAccessor to StreamDiagnostics, starting a
    /// new inspect ReaderServer.
    pub stream_diagnostics_requests: fuchsia_inspect::UintProperty,
}

impl ArchiveAccessorStats {
    pub fn new(mut archive_accessor_node: fuchsia_inspect::Node) -> Self {
        let archive_accessor_connections_opened =
            archive_accessor_node.create_uint("archive_accessor_connections_opened", 0);
        let archive_accessor_connections_closed =
            archive_accessor_node.create_uint("archive_accessor_connections_closed", 0);

        let stream_diagnostics_requests =
            archive_accessor_node.create_uint("stream_diagnostics_requests", 0);

        let global_inspect_stats =
            Arc::new(DiagnosticsServerStatsGlobal::for_inspect(&mut archive_accessor_node));

        let global_lifecycle_stats =
            Arc::new(DiagnosticsServerStatsGlobal::for_lifecycle(&mut archive_accessor_node));

        let global_logs_stats =
            Arc::new(DiagnosticsServerStatsGlobal::for_logs(&mut archive_accessor_node));

        ArchiveAccessorStats {
            archive_accessor_node,
            global_stats: Arc::new(ArchiveAccessorStatsGlobal {
                archive_accessor_connections_opened,
                archive_accessor_connections_closed,
                stream_diagnostics_requests,
            }),
            global_inspect_stats,
            global_lifecycle_stats,
            global_logs_stats,
        }
    }
}

// Exponential histograms for time in microseconds contains power-of-two intervals
const EXPONENTIAL_HISTOGRAM_USEC_FLOOR: u64 = 0;
const EXPONENTIAL_HISTOGRAM_USEC_STEP: u64 = 1;
const EXPONENTIAL_HISTOGRAM_USEC_MULTIPLIER: u64 = 2;
const EXPONENTIAL_HISTOGRAM_USEC_BUCKETS: u64 = 26;

pub struct DiagnosticsServerStatsGlobal {
    /// The name of the diagnostics source being tracked by this struct.
    diagnostics_source: &'static str,
    /// Weak clone of the node that stores stats, used for on-demand population.
    archive_accessor_node: fuchsia_inspect::Node,
    /// Number of DiagnosticsServers created in response to an StreamDiagnostics
    /// client request.
    reader_servers_constructed: fuchsia_inspect::UintProperty,
    /// Number of DiagnosticsServers destroyed in response to falling out of scope.
    reader_servers_destroyed: fuchsia_inspect::UintProperty,
    /// Property tracking number of opening connections to any batch iterator instance.
    batch_iterator_connections_opened: fuchsia_inspect::UintProperty,
    /// Property tracking number of closing connections to any batch iterator instance.
    batch_iterator_connections_closed: fuchsia_inspect::UintProperty,
    /// Property tracking number of times a future to retrieve diagnostics data for a component
    /// timed out.
    component_timeouts_count: fuchsia_inspect::UintProperty,
    /// Number of times "GetNext" was called
    batch_iterator_get_next_requests: fuchsia_inspect::UintProperty,
    /// Number of times a "GetNext" response was sent
    batch_iterator_get_next_responses: fuchsia_inspect::UintProperty,
    /// Number of times "GetNext" resulted in an error
    batch_iterator_get_next_errors: fuchsia_inspect::UintProperty,
    /// Number of items returned in batches from "GetNext"
    batch_iterator_get_next_result_count: fuchsia_inspect::UintProperty,
    /// Number of items returned in batches from "GetNext" that contained errors
    batch_iterator_get_next_result_errors: fuchsia_inspect::UintProperty,

    /// Histogram of processing times for overall "GetNext" requests.
    batch_iterator_get_next_time_usec: fuchsia_inspect::UintExponentialHistogramProperty,
    /// Optional histogram of processing times for individual components in GetNext
    component_time_usec: Mutex<Option<fuchsia_inspect::UintExponentialHistogramProperty>>,
    /// Longest processing times for individual components, with timestamps.
    processing_time_tracker: Mutex<Option<ProcessingTimeTracker>>,
}

impl DiagnosticsServerStatsGlobal {
    // TODO(fxbug.dev/54442): Consider encoding prefix as node name and represent the same
    //              named properties under different nodes for each diagnostics source.
    pub fn for_inspect(archive_accessor_node: &mut fuchsia_inspect::Node) -> Self {
        DiagnosticsServerStatsGlobal::generate_server_properties(archive_accessor_node, "inspect")
    }

    // TODO(fxbug.dev/54442): Consider encoding prefix as node name and represent the same
    //              named properties under different nodes for each diagnostics source.
    pub fn for_lifecycle(archive_accessor_node: &mut fuchsia_inspect::Node) -> Self {
        DiagnosticsServerStatsGlobal::generate_server_properties(archive_accessor_node, "lifecycle")
    }

    // TODO(fxbug.dev/54442): Consider encoding prefix as node name and represent the same
    //              named properties under different nodes for each diagnostics source.
    pub fn for_logs(archive_accessor_node: &mut fuchsia_inspect::Node) -> Self {
        DiagnosticsServerStatsGlobal::generate_server_properties(archive_accessor_node, "logs")
    }

    fn generate_server_properties(
        archive_accessor_node: &mut fuchsia_inspect::Node,
        diagnostics_source: &'static str,
    ) -> Self {
        let reader_servers_constructed = archive_accessor_node
            .create_uint(format!("{}_reader_servers_constructed", diagnostics_source), 0);
        let reader_servers_destroyed = archive_accessor_node
            .create_uint(format!("{}_reader_servers_destroyed", diagnostics_source), 0);
        let batch_iterator_connections_opened = archive_accessor_node
            .create_uint(format!("{}_batch_iterator_connections_opened", diagnostics_source), 0);
        let batch_iterator_connections_closed = archive_accessor_node
            .create_uint(format!("{}_batch_iterator_connections_closed", diagnostics_source), 0);
        let component_timeouts_count = archive_accessor_node
            .create_uint(format!("{}_component_timeouts_count", diagnostics_source), 0);
        let batch_iterator_get_next_requests = archive_accessor_node
            .create_uint(format!("{}_batch_iterator_get_next_requests", diagnostics_source), 0);
        let batch_iterator_get_next_responses = archive_accessor_node
            .create_uint(format!("{}_batch_iterator_get_next_responses", diagnostics_source), 0);
        let batch_iterator_get_next_errors = archive_accessor_node
            .create_uint(format!("{}_batch_iterator_get_next_errors", diagnostics_source), 0);
        let batch_iterator_get_next_result_count = archive_accessor_node
            .create_uint(format!("{}_batch_iterator_get_next_result_count", diagnostics_source), 0);
        let batch_iterator_get_next_result_errors = archive_accessor_node.create_uint(
            format!("{}_batch_iterator_get_next_result_errors", diagnostics_source),
            0,
        );
        let batch_iterator_get_next_time_usec = archive_accessor_node
            .create_uint_exponential_histogram(
                format!("{}_batch_iterator_get_next_time_usec", diagnostics_source),
                fuchsia_inspect::ExponentialHistogramParams {
                    floor: EXPONENTIAL_HISTOGRAM_USEC_FLOOR,
                    initial_step: EXPONENTIAL_HISTOGRAM_USEC_STEP,
                    step_multiplier: EXPONENTIAL_HISTOGRAM_USEC_MULTIPLIER,
                    buckets: EXPONENTIAL_HISTOGRAM_USEC_BUCKETS as usize,
                },
            );

        DiagnosticsServerStatsGlobal {
            diagnostics_source,
            archive_accessor_node: archive_accessor_node.clone_weak(),
            reader_servers_constructed,
            reader_servers_destroyed,
            batch_iterator_connections_opened,
            batch_iterator_connections_closed,
            component_timeouts_count,
            batch_iterator_get_next_requests,
            batch_iterator_get_next_responses,
            batch_iterator_get_next_errors,
            batch_iterator_get_next_result_count,
            batch_iterator_get_next_result_errors,
            batch_iterator_get_next_time_usec,
            component_time_usec: Mutex::new(None),
            processing_time_tracker: Mutex::new(None),
        }
    }

    pub fn add_timeout(&self) {
        self.component_timeouts_count.add(1);
    }

    /// Record the duration of a whole request to GetNext.
    pub fn record_batch_duration(&self, duration: Duration) {
        let micros = duration.into_micros();
        if micros >= 0 {
            self.batch_iterator_get_next_time_usec.insert(micros as u64);
        }
    }

    /// Record the duration of obtaining data from a single component.
    pub fn record_component_duration(&self, moniker: &str, duration: Duration) {
        let nanos = duration.into_nanos();
        if nanos >= 0 {
            // Lazily initialize stats that may not be needed for all diagnostics types.

            let mut component_time_usec = self.component_time_usec.lock();
            if component_time_usec.is_none() {
                *component_time_usec =
                    Some(self.archive_accessor_node.create_uint_exponential_histogram(
                        format!("{}_component_time_usec", self.diagnostics_source),
                        fuchsia_inspect::ExponentialHistogramParams {
                            floor: EXPONENTIAL_HISTOGRAM_USEC_FLOOR,
                            initial_step: EXPONENTIAL_HISTOGRAM_USEC_STEP,
                            step_multiplier: EXPONENTIAL_HISTOGRAM_USEC_MULTIPLIER,
                            buckets: EXPONENTIAL_HISTOGRAM_USEC_BUCKETS as usize,
                        },
                    ));
            }

            let mut processing_time_tracker = self.processing_time_tracker.lock();
            if processing_time_tracker.is_none() {
                *processing_time_tracker =
                    Some(ProcessingTimeTracker::new(self.archive_accessor_node.create_child(
                        format!("{}_longest_processing_times", self.diagnostics_source),
                    )));
            }

            component_time_usec.as_ref().unwrap().insert(nanos as u64 / 1000);
            processing_time_tracker.as_mut().unwrap().track(moniker, nanos as u64);
        }
    }
}

const PROCESSING_TIME_COMPONENT_COUNT_LIMIT: usize = 20;

/// Holds stats on the longest processing times for individual components' data.
struct ProcessingTimeTracker {
    /// The node holding all properties for the tracker.
    node: fuchsia_inspect::Node,
    /// Map from component moniker to a tuple of its time and a node containing the stats about it.
    longest_times_by_component: BTreeMap<String, (u64, fuchsia_inspect::Node)>,
    /// The shortest time seen so far. If a new component is being
    /// recorded and its time is greater than this, we need to pop the
    /// entry containing this time.
    shortest_time_ns: u64,
}

impl ProcessingTimeTracker {
    fn new(node: fuchsia_inspect::Node) -> Self {
        Self { node, longest_times_by_component: BTreeMap::new(), shortest_time_ns: u64::MAX }
    }
    fn track(&mut self, moniker: &str, time_ns: u64) {
        let at_capacity =
            self.longest_times_by_component.len() >= PROCESSING_TIME_COMPONENT_COUNT_LIMIT;

        // Do nothing if the container it as the limit and the new time doesn't need to get
        // inserted.
        if at_capacity && time_ns < self.shortest_time_ns {
            return;
        }

        let parent_node = &self.node;

        let make_entry = || {
            let n = parent_node.create_child(moniker.to_string());
            n.record_int("@time", zx::Time::get_monotonic().into_nanos());
            n.record_double("duration_seconds", time_ns as f64 / 1e9);
            (time_ns, n)
        };

        self.longest_times_by_component
            .entry(moniker.to_string())
            .and_modify(move |v| {
                if v.0 < time_ns {
                    *v = make_entry();
                }
            })
            .or_insert_with(make_entry);

        // Repeatedly find the key for the smallest time and remove it until we are under the
        // limit.
        while self.longest_times_by_component.len() > PROCESSING_TIME_COMPONENT_COUNT_LIMIT {
            let mut key = "".to_string();
            for (k, (val, _)) in &self.longest_times_by_component {
                if *val == self.shortest_time_ns {
                    key = k.clone();
                    break;
                }
            }
            self.longest_times_by_component.remove(&key);
            self.shortest_time_ns = self
                .longest_times_by_component
                .values()
                .map(|v| v.0)
                .min()
                .unwrap_or(std::u64::MAX);
        }

        self.shortest_time_ns = std::cmp::min(self.shortest_time_ns, time_ns);
    }
}

pub struct DiagnosticsServerStats {
    /// Inspect node for tracking usage/health metrics of a single connection to a batch iterator.
    _batch_iterator_connection_node: fuchsia_inspect::Node,

    /// Global stats for the accessor itself.
    global_stats: Arc<DiagnosticsServerStatsGlobal>,

    /// Property tracking number of requests to the BatchIterator instance this struct is tracking.
    batch_iterator_get_next_requests: fuchsia_inspect::UintProperty,
    /// Property tracking number of responses from the BatchIterator instance this struct is tracking.
    batch_iterator_get_next_responses: fuchsia_inspect::UintProperty,
    /// Property tracking number of times the batch iterator has served a terminal batch signalling that
    /// the client has reached the end of the iterator and should terminate their connection.
    batch_iterator_terminal_responses: fuchsia_inspect::UintProperty,
}

impl DiagnosticsServerStats {
    pub fn open_connection(&self) {
        self.global_stats.batch_iterator_connections_opened.add(1);
    }

    pub fn close_connection(&self) {
        self.global_stats.batch_iterator_connections_closed.add(1);
    }

    pub fn global_stats(&self) -> &Arc<DiagnosticsServerStatsGlobal> {
        &self.global_stats
    }

    pub fn add_request(self: &Arc<Self>) {
        self.global_stats.batch_iterator_get_next_requests.add(1);
        self.batch_iterator_get_next_requests.add(1);
    }

    pub fn add_response(self: &Arc<Self>) {
        self.global_stats.batch_iterator_get_next_responses.add(1);
        self.batch_iterator_get_next_responses.add(1);
    }

    pub fn add_terminal(self: &Arc<Self>) {
        self.batch_iterator_terminal_responses.add(1);
    }

    pub fn add_result(&self) {
        self.global_stats.batch_iterator_get_next_result_count.add(1);
    }

    pub fn add_error(&self) {
        self.global_stats.batch_iterator_get_next_errors.add(1);
    }

    pub fn add_result_error(&self) {
        self.global_stats.batch_iterator_get_next_result_errors.add(1);
    }

    pub fn for_inspect(archive_accessor_stats: Arc<ArchiveAccessorStats>) -> Self {
        let global_inspect = archive_accessor_stats.global_inspect_stats.clone();
        DiagnosticsServerStats::generate_diagnostics_server_stats(
            archive_accessor_stats,
            global_inspect,
            "inspect",
        )
    }

    pub fn for_lifecycle(archive_accessor_stats: Arc<ArchiveAccessorStats>) -> Self {
        let global_lifecycle = archive_accessor_stats.global_lifecycle_stats.clone();
        DiagnosticsServerStats::generate_diagnostics_server_stats(
            archive_accessor_stats,
            global_lifecycle,
            "lifecycle",
        )
    }

    pub fn for_logs(archive_accessor_stats: Arc<ArchiveAccessorStats>) -> Self {
        let global_logs = archive_accessor_stats.global_logs_stats.clone();
        DiagnosticsServerStats::generate_diagnostics_server_stats(
            archive_accessor_stats,
            global_logs,
            "logs",
        )
    }

    pub fn generate_diagnostics_server_stats(
        archive_accessor_stats: Arc<ArchiveAccessorStats>,
        global_stats: Arc<DiagnosticsServerStatsGlobal>,
        prefix: &str,
    ) -> Self {
        // we'll decrement these on drop
        global_stats.reader_servers_constructed.add(1);

        // TODO(fxbug.dev/59454) add this to a "list node" instead of using numeric suffixes
        let batch_iterator_connection_node =
            archive_accessor_stats.archive_accessor_node.create_child(
                fuchsia_inspect::unique_name(&format!("{}_batch_iterator_connection", prefix)),
            );

        let batch_iterator_get_next_requests = batch_iterator_connection_node
            .create_uint(format!("{}_batch_iterator_get_next_requests", prefix), 0);
        let batch_iterator_get_next_responses = batch_iterator_connection_node
            .create_uint(format!("{}_batch_iterator_get_next_responses", prefix), 0);
        let batch_iterator_terminal_responses = batch_iterator_connection_node
            .create_uint(format!("{}_batch_iterator_terminal_responses", prefix), 0);

        DiagnosticsServerStats {
            _batch_iterator_connection_node: batch_iterator_connection_node,
            global_stats,
            batch_iterator_get_next_requests,
            batch_iterator_get_next_responses,
            batch_iterator_terminal_responses,
        }
    }
}

impl Drop for DiagnosticsServerStats {
    fn drop(&mut self) {
        self.global_stats.reader_servers_destroyed.add(1);
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, crate::archive::EventFileGroupStats, fuchsia_inspect::assert_inspect_tree,
        fuchsia_inspect::health::Reporter, fuchsia_inspect::testing::AnyProperty,
        std::iter::FromIterator,
    };

    #[test]
    fn health() {
        component::health().set_ok();
        assert_inspect_tree!(component::inspector(),
        root: {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });

        component::health().set_unhealthy("Bad state");
        assert_inspect_tree!(component::inspector(),
        root: contains {
            "fuchsia.inspect.Health": {
                status: "UNHEALTHY",
                message: "Bad state",
                start_timestamp_nanos: AnyProperty,
            }
        });

        component::health().set_ok();
        assert_inspect_tree!(component::inspector(),
        root: contains {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });
    }

    #[test]
    fn group_stats() {
        let inspector = fuchsia_inspect::Inspector::new();
        let mut group = Groups::new(inspector.root().create_child("archived_events"));
        group.replace(&EventFileGroupStatsMap::from_iter(vec![
            ("a/b".to_string(), EventFileGroupStats { file_count: 1, size: 2 }),
            ("c/d".to_string(), EventFileGroupStats { file_count: 3, size: 4 }),
        ]));

        assert_inspect_tree!(inspector,
        root: contains {
            archived_events: {
               "a/b": {
                    file_count: 1u64,
                    size_in_bytes: 2u64
               },
               "c/d": {
                   file_count: 3u64,
                   size_in_bytes: 4u64
               }
            }
        });
    }

    #[test]
    fn processing_time_tracker() {
        let inspector = fuchsia_inspect::Inspector::new();
        let mut tracker = ProcessingTimeTracker::new(inspector.root().create_child("test"));

        tracker.track("a", 1e9 as u64);
        assert_inspect_tree!(inspector,
        root: {
            test: {
                a: {
                    "@time": AnyProperty,
                    duration_seconds: 1f64
                }
            }
        });

        tracker.track("a", 5e8 as u64);
        assert_inspect_tree!(inspector,
        root: {
            test: {
                a: {
                    "@time": AnyProperty,
                    duration_seconds: 1f64
                }
            }
        });

        tracker.track("a", 5500e6 as u64);
        assert_inspect_tree!(inspector,
        root: {
            test: {
                a: {
                    "@time": AnyProperty,
                    duration_seconds: 5.5f64
                }
            }
        });

        for time in 0..60 {
            tracker.track(&format!("b{}", time), time * 1e9 as u64);
        }

        assert_inspect_tree!(inspector,
        root: {
            test: {
                b40: { "@time": AnyProperty, duration_seconds: 40f64 },
                b41: { "@time": AnyProperty, duration_seconds: 41f64 },
                b42: { "@time": AnyProperty, duration_seconds: 42f64 },
                b43: { "@time": AnyProperty, duration_seconds: 43f64 },
                b44: { "@time": AnyProperty, duration_seconds: 44f64 },
                b45: { "@time": AnyProperty, duration_seconds: 45f64 },
                b46: { "@time": AnyProperty, duration_seconds: 46f64 },
                b47: { "@time": AnyProperty, duration_seconds: 47f64 },
                b48: { "@time": AnyProperty, duration_seconds: 48f64 },
                b49: { "@time": AnyProperty, duration_seconds: 49f64 },
                b50: { "@time": AnyProperty, duration_seconds: 50f64 },
                b51: { "@time": AnyProperty, duration_seconds: 51f64 },
                b52: { "@time": AnyProperty, duration_seconds: 52f64 },
                b53: { "@time": AnyProperty, duration_seconds: 53f64 },
                b54: { "@time": AnyProperty, duration_seconds: 54f64 },
                b55: { "@time": AnyProperty, duration_seconds: 55f64 },
                b56: { "@time": AnyProperty, duration_seconds: 56f64 },
                b57: { "@time": AnyProperty, duration_seconds: 57f64 },
                b58: { "@time": AnyProperty, duration_seconds: 58f64 },
                b59: { "@time": AnyProperty, duration_seconds: 59f64 },
            }
        });
    }
}

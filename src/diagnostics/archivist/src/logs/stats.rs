// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::message::{Message, Severity};
use anyhow::*;
use fidl_fuchsia_sys_internal::SourceIdentity;
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use fuchsia_inspect_derive::Inspect;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use regex::Regex;
use std::collections::{hash_map::Entry, HashMap, VecDeque};
use std::convert::TryFrom;
use std::sync::Arc;

// Maximum number of GranularLogStatsRecords kept in a GranularLogStatsBucket.
const MAX_RECORDS_PER_BUCKET: usize = 100;

// Length of the interval corresdponding to a GranularLogStatsBucket.
const BUCKET_INTERVAL_IN_MINUTES: u64 = 15;

// Interval between per-component stats garbage collection runs to clean up stale stats.
const COMPONENT_STATS_GC_INTERVAL_HOURS: i64 = 1;

// `limit` in `stats_are_stale = freshness < fasync::Time::now() - zx::Duration::from_hours(limit)`.
const COMPONENT_STATS_FRESHNESS_EXPIRY_HOURS: i64 = 1;

/// Structure that holds stats for the log manager.
#[derive(Default, Inspect)]
pub(super) struct LogManagerStats {
    total_logs: inspect::UintProperty,
    kernel_logs: inspect::UintProperty,
    logsink_logs: inspect::UintProperty,
    trace_logs: inspect::UintProperty,
    debug_logs: inspect::UintProperty,
    info_logs: inspect::UintProperty,
    warning_logs: inspect::UintProperty,
    error_logs: inspect::UintProperty,
    by_component: LogStatsByComponent,
    granular_stats: GranularLogStats,
    fatal_logs: inspect::UintProperty,
    closed_streams: inspect::UintProperty,
    unattributed_log_sinks: inspect::UintProperty,
}

#[derive(Inspect)]
struct LogStatsByComponent {
    // Note: This field is manually managed as the Inspect derive macro does
    // not yet support collections.
    #[inspect(skip)]
    components: Arc<Mutex<HashMap<String, Arc<ComponentLogStats>>>>,
    inspect_node: inspect::Node,

    // Maintain reference to periodic task that GCs stale `LogStatsByComponentGC`. This ensures that
    // the task will be aborted when this `LogStatsByComponent` is dropped.
    #[inspect(skip)]
    _gc_task: fasync::Task<()>,
}

impl LogStatsByComponent {
    fn new(gc_timeout: zx::Duration, inspect_node: inspect::Node) -> Self {
        let components = Arc::new(Mutex::new(HashMap::new()));
        let gc_components = components.clone();
        Self {
            components,
            inspect_node,
            _gc_task: fasync::Task::spawn(async move {
                loop {
                    {
                        let mut components = gc_components.lock().await;
                        let limit = (fasync::Time::now()
                            - zx::Duration::from_hours(COMPONENT_STATS_FRESHNESS_EXPIRY_HOURS))
                        .into_nanos();
                        components.retain(|_, stats| {
                            // TODO(fxbug.dev/56527): Report failure to access freshness somewhere.
                            //For now, assume failure to indicate a bad state and garbage collect
                            // such components (i.e., `false` below).
                            stats
                                .last_log_monotonic_nanos
                                .get()
                                .map_or(false, |freshness| freshness >= limit)
                        });
                    };
                    // Release `log_stats_component_gc` lock while waiting on timer.

                    fasync::Timer::new(fasync::Time::after(gc_timeout)).await;
                }
            }),
        }
    }

    pub async fn get_component_log_stats(
        &self,
        identity: &SourceIdentity,
    ) -> Arc<ComponentLogStats> {
        let url = identity.component_url.clone().unwrap_or("(unattributed)".to_string());
        let mut components = self.components.lock().await;
        match components.get(&url) {
            Some(stats) => stats.clone(),
            None => {
                let mut stats = ComponentLogStats::default();
                // TODO(fxbug.dev/60396): Report failure to attach somewhere.
                let _ = stats.iattach(&self.inspect_node, url.clone());
                let stats = Arc::new(stats);
                components.insert(url, stats.clone());
                stats
            }
        }
    }
}

impl Default for LogStatsByComponent {
    fn default() -> Self {
        Self::new(
            zx::Duration::from_hours(COMPONENT_STATS_GC_INTERVAL_HOURS),
            inspect::Node::default(),
        )
    }
}

/// Holds up to 3 buckets, each bucket containing statistics for error & fatal log messages that
/// occured during an interval of length BUCKET_INTERVAL_IN_MINUTES. Logs with the same file and
/// line number are considered to be the same (even if their messages are not identical) and their
/// count will be exposed in inspect. Logs without file and line number are ignored.
#[derive(Default, Inspect)]
struct GranularLogStats {
    #[inspect(skip)]
    buckets: VecDeque<GranularLogStatsBucket>,
    inspect_node: inspect::Node,
}

impl GranularLogStats {
    pub fn record_log(&mut self, msg: &Message) {
        self.ensure_bucket().record_log(msg);
    }

    fn ensure_bucket(&mut self) -> &mut GranularLogStatsBucket {
        let bucket_id = fasync::Time::now().into_nanos() as u64
            / 1_000_000_000
            / 60
            / BUCKET_INTERVAL_IN_MINUTES;
        if self.buckets.is_empty() || self.buckets.back().unwrap().bucket_id < bucket_id {
            let mut new_bucket = GranularLogStatsBucket::default();
            let _ = new_bucket.iattach(&self.inspect_node, bucket_id.to_string());
            new_bucket.bucket_id = bucket_id;
            self.buckets.push_back(new_bucket);
            if self.buckets.len() > 3 {
                self.buckets.pop_front();
            }
        }
        self.buckets.back_mut().unwrap()
    }
}

#[derive(Hash, PartialEq, Eq, Clone)]
struct LogIdentifier {
    file_path: String,
    line_no: u64,
}

impl TryFrom<&Message> for LogIdentifier {
    type Error = anyhow::Error;

    fn try_from(msg: &Message) -> Result<Self, Self::Error> {
        let re = Regex::new(r"^\[([^\(\]:]+)\((\d+)\)\]").unwrap();
        let msg_str = msg.msg().ok_or_else(|| format_err!("No message"))?;
        let cap =
            re.captures(msg_str).ok_or_else(|| format_err!("Message didn't match pattern"))?;
        let file = cap.get(1).ok_or_else(|| format_err!("Couldn't capture file path"))?.as_str();
        let line_str =
            cap.get(2).ok_or_else(|| format_err!("Couldn't capture line number"))?.as_str();
        let line = line_str.parse::<u64>()?;
        Ok(LogIdentifier { file_path: file.to_string(), line_no: line })
    }
}

#[derive(Default, Inspect)]
struct GranularLogStatsRecord {
    file_path: inspect::StringProperty,
    line_no: inspect::UintProperty,
    count: inspect::UintProperty,
    inspect_node: inspect::Node,
}

#[derive(Default, Inspect)]
struct GranularLogStatsBucket {
    #[inspect(skip)]
    stats: HashMap<LogIdentifier, GranularLogStatsRecord>,
    #[inspect(skip)]
    bucket_id: u64,
    overflowed: inspect::BoolProperty,
    inspect_node: inspect::Node,
}

impl GranularLogStatsBucket {
    pub fn record_log(&mut self, msg: &Message) {
        if msg.metadata.severity != Severity::Error && msg.metadata.severity != Severity::Fatal {
            return;
        }
        let id = match LogIdentifier::try_from(msg) {
            Ok(val) => val,
            Err(_) => return,
        };
        let num_records = self.stats.len();
        let record = match self.stats.entry(id.clone()) {
            Entry::Occupied(o) => o.into_mut(),
            Entry::Vacant(v) => {
                if num_records == MAX_RECORDS_PER_BUCKET {
                    self.overflowed.set(true);
                    return;
                }
                let next_id = num_records.to_string();
                let mut record = GranularLogStatsRecord::default();
                let _ = record.iattach(&self.inspect_node, next_id);
                record.file_path.set(&id.file_path);
                record.line_no.set(id.line_no);
                v.insert(record)
            }
        };
        record.count.add(1);
    }
}

#[derive(Inspect, Default)]
pub struct ComponentLogStats {
    last_log_monotonic_nanos: inspect::IntProperty,
    total_logs: inspect::UintProperty,
    trace_logs: inspect::UintProperty,
    debug_logs: inspect::UintProperty,
    info_logs: inspect::UintProperty,
    warning_logs: inspect::UintProperty,
    error_logs: inspect::UintProperty,
    fatal_logs: inspect::UintProperty,

    inspect_node: inspect::Node,
}

impl ComponentLogStats {
    pub fn record_log(&self, msg: &Message) {
        self.last_log_monotonic_nanos.set(fasync::Time::now().into_nanos());
        self.total_logs.add(1);
        match msg.metadata.severity {
            Severity::Trace => self.trace_logs.add(1),
            Severity::Debug => self.debug_logs.add(1),
            Severity::Info => self.info_logs.add(1),
            Severity::Warn => self.warning_logs.add(1),
            Severity::Error => self.error_logs.add(1),
            Severity::Fatal => self.fatal_logs.add(1),
        }
    }
}

impl LogManagerStats {
    /// Create a stat holder. Note that this needs to be attached to inspect in order
    /// for it to be inspected. See `fuchsia_inspect_derive::Inspect`.
    pub fn new_detached() -> Self {
        Self::default()
    }

    /// Record an incoming log from a given source.
    ///
    /// This method updates the counters based on the contents of the log message.
    pub fn record_log(&mut self, msg: &Message, source: LogSource) {
        self.total_logs.add(1);
        self.granular_stats.record_log(msg);
        match source {
            LogSource::Kernel => {
                self.kernel_logs.add(1);
            }
            LogSource::LogSink => {
                self.logsink_logs.add(1);
            }
        }
        match msg.metadata.severity {
            Severity::Trace => self.trace_logs.add(1),
            Severity::Debug => self.debug_logs.add(1),
            Severity::Info => self.info_logs.add(1),
            Severity::Warn => self.warning_logs.add(1),
            Severity::Error => self.error_logs.add(1),
            Severity::Fatal => self.fatal_logs.add(1),
        }
    }

    /// Returns the stats for a particular component specified by `identity`.
    pub async fn get_component_log_stats(
        &self,
        identity: &SourceIdentity,
    ) -> Arc<ComponentLogStats> {
        self.by_component.get_component_log_stats(identity).await
    }

    /// Record that we rejected a message.
    pub fn record_closed_stream(&self) {
        self.closed_streams.add(1);
    }

    /// Record an unattributed log message.
    pub fn record_unattributed(&self) {
        self.unattributed_log_sinks.add(1);
    }
}

/// Denotes the source of a particular log message.
pub(super) enum LogSource {
    /// Log came from the kernel log (klog)
    Kernel,
    /// Log came from log sink
    LogSink,
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::logs::message::*, fuchsia_async as fasync, fuchsia_inspect::testing::*,
        fuchsia_inspect::*, fuchsia_zircon as zx, futures::Future, proptest::prelude::*,
        std::panic,
    };

    struct GranularTestState {
        exec: fasync::Executor,
        granular_stats: GranularLogStats,
        inspector: Inspector,
    }

    impl GranularTestState {
        fn new() -> Result<GranularTestState, anyhow::Error> {
            let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
            exec.set_fake_time(fasync::Time::from_nanos(0));
            let inspector = Inspector::new();
            let mut granular_stats = GranularLogStats::default();
            granular_stats.iattach(inspector.root(), "granular_stats")?;
            Ok(GranularTestState {
                exec: exec,
                granular_stats: granular_stats,
                inspector: inspector,
            })
        }
    }

    #[test]
    fn granular_stats() -> Result<(), anyhow::Error> {
        let mut state = GranularTestState::new()?;

        let msg1 = create_message("[path/to/file.cpp(123)] Hello");
        let msg2 = create_message("[another_file.h(1)]");

        // Send |msg1|. Creates a new entry in bucket 0.
        state.granular_stats.record_log(&msg1);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        // Advance time but not enough to create a new bucket.
        state.exec.set_fake_time(fasync::Time::after(zx::Duration::from_minutes(5)));

        // Send |msg2|. Creates a new entry in bucket 0.
        state.granular_stats.record_log(&msg2);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 1u64
                },
                "1": {
                    "file_path": "another_file.h",
                    "line_no": 1u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        // Advance time but not enough to create a new bucket.
        state.exec.set_fake_time(fasync::Time::after(zx::Duration::from_minutes(5)));

        // Send |msg1| again. No new entry is added. The existing entry will have its count
        // incremented.
        state.granular_stats.record_log(&msg1);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 2u64
                },
                "1": {
                    "file_path": "another_file.h",
                    "line_no": 1u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        // Advance time enough to make create a new bucket.
        state.exec.set_fake_time(fasync::Time::after(zx::Duration::from_minutes(5)));

        // Send |msg2| again. A new entry must be added because a new bucket is created.
        state.granular_stats.record_log(&msg2);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 2u64
                },
                "1": {
                    "file_path": "another_file.h",
                    "line_no": 1u64,
                    "count": 1u64
                }
              },
              "1": {
                overflowed: false,
                "0": {
                    "file_path": "another_file.h",
                    "line_no": 1u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        // Advance time enough to make create a new bucket.
        state.exec.set_fake_time(fasync::Time::after(zx::Duration::from_minutes(15)));

        // Send |msg1|. Creates a new entry in the new bucket.
        state.granular_stats.record_log(&msg1);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 2u64
                },
                "1": {
                    "file_path": "another_file.h",
                    "line_no": 1u64,
                    "count": 1u64
                }
              },
              "1": {
                overflowed: false,
                "0": {
                    "file_path": "another_file.h",
                    "line_no": 1u64,
                    "count": 1u64
                }
              },
              "2": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 1u64
                },
              }
            }
          }
        );

        // Advance time enough to make create a new bucket.
        state.exec.set_fake_time(fasync::Time::after(zx::Duration::from_minutes(15)));

        // Send |msg1|. Creates a new entry in the new bucket. The first bucket is dropped.
        state.granular_stats.record_log(&msg1);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "1": {
                overflowed: false,
                "0": {
                    "file_path": "another_file.h",
                    "line_no": 1u64,
                    "count": 1u64
                }
              },
              "2": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 1u64
                },
              },
              "3": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 123u64,
                    "count": 1u64
                },
              }
            }
          }
        );

        Ok(())
    }

    #[test]
    fn different_severities() -> Result<(), anyhow::Error> {
        let mut state = GranularTestState::new()?;

        let fatal_msg =
            create_message_with_severity("[path/to/file.cpp(121)] Hello", Severity::Fatal);
        let error_msg =
            create_message_with_severity("[path/to/file.cpp(122)] Hello", Severity::Error);
        let warn_msg =
            create_message_with_severity("[path/to/file.cpp(123)] Hello", Severity::Warn);
        let info_msg =
            create_message_with_severity("[path/to/file.cpp(124)] Hello", Severity::Info);
        let debug_msg =
            create_message_with_severity("[path/to/file.cpp(125)] Hello", Severity::Debug);
        let trace_msg =
            create_message_with_severity("[path/to/file.cpp(126)] Hello", Severity::Trace);

        // Fatal message should be recorded.
        state.granular_stats.record_log(&fatal_msg);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 121u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        // Error message should be recorded.
        state.granular_stats.record_log(&error_msg);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 121u64,
                    "count": 1u64
                },
                "1": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 122u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        // Lower severities should be ignored.
        state.granular_stats.record_log(&warn_msg);
        state.granular_stats.record_log(&info_msg);
        state.granular_stats.record_log(&debug_msg);
        state.granular_stats.record_log(&trace_msg);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": {
                overflowed: false,
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 121u64,
                    "count": 1u64
                },
                "1": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 122u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        Ok(())
    }

    #[test]
    fn negative_edge_cases() -> Result<(), anyhow::Error> {
        // File and line not at the beginning
        verify_message_ignored("ERROR: [abc/xyz.cc(10)] dsafq")?;

        // No file or line number
        verify_message_ignored("[()] dsafq")?;
        verify_message_ignored("[(] dsafq")?;
        verify_message_ignored("[] dsafq")?;
        verify_message_ignored("() dsafq")?;
        verify_message_ignored("[ dsafq")?;
        verify_message_ignored("dsafq")?;

        // No line number
        verify_message_ignored("[daddsfsdafsadf()] dsafq")?;
        verify_message_ignored("[daddsfsdafsadf(] dsafq")?;
        verify_message_ignored("[daddsfsdafsadf] dsafq")?;

        // No file
        verify_message_ignored("[(12)] dsafq")?;
        verify_message_ignored("[12] dsafq")?;

        // Negative line number
        verify_message_ignored("[daddsfsdafsadf(-1)] dsafq")?;

        // Line number not a number
        verify_message_ignored("[daddsfsdafsadf(1a3)] dsafq")?;
        verify_message_ignored("[daddsfsdafsadf(xyz)] dsafq")?;
        verify_message_ignored("[daddsfsdafsadf(12.3)] dsafq")?;

        // Line number too large
        verify_message_ignored(
            "[daddsfsdafsadf(999999999999999999999999999999999999999999)] dsafq",
        )?;

        // Random unicode characters
        verify_message_ignored("𜌼:ع鱆髦頄񋉡j�삩ĕ낪�ˏ6ؔ𛙄񓈫b۷wuuN󡸲�劰ﵫ⮡ٸ󀁁")?;
        verify_message_ignored("[𜌼:ع鱆髦頄񋉡j�삩ĕ낪�ˏ6ؔ𛙄񓈫b۷wuuN(12)] Hi")?;

        Ok(())
    }

    #[test]
    fn positive_edge_cases() -> Result<(), anyhow::Error> {
        // Message is unicode.
        verify_file_and_line("[dir/file.cc(34)] 𜌼:ع鱆髦頄", "dir/file.cc", 34)?;

        // Message is empty.
        verify_file_and_line("[dir/file.cc(34)]", "dir/file.cc", 34)?;

        Ok(())
    }

    #[test]
    fn too_many_logs() -> Result<(), anyhow::Error> {
        let mut state = GranularTestState::new()?;

        for i in 1..MAX_RECORDS_PER_BUCKET + 2 {
            let msg_str = format!("[path/to/file.cpp({})] Hello", i);
            let msg = create_message(&msg_str);
            state.granular_stats.record_log(&msg);
            if i == MAX_RECORDS_PER_BUCKET + 1 {
                assert_inspect_tree!(state.inspector,
                   root: {
                       granular_stats: {
                           "0": contains {
                               overflowed: true
                           }
                       }
                });
            } else {
                assert_inspect_tree!(state.inspector,
                   root: {
                       granular_stats: {
                           "0": contains {
                               overflowed: false
                           }
                       }
                });
            }
        }

        // First MAX_RECORDS_PER_BUCKET logs should be recorded.
        let last_key = &(MAX_RECORDS_PER_BUCKET - 1).to_string();
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": contains {
                  var last_key: {
                    "file_path": "path/to/file.cpp",
                    "line_no": MAX_RECORDS_PER_BUCKET as u64,
                    "count": 1u64
                }
              }
            }
          }
        );

        // The last log should be ignored.
        let ignored_key = &MAX_RECORDS_PER_BUCKET.to_string();
        let tree_assertion = tree_assertion!(
              root: {
                granular_stats: {
                  "0": contains{
                      var ignored_key: contains {}
                  }
                }
              }
        );
        if tree_assertion.run(state.inspector.get_node_hierarchy().as_ref()).is_ok() {
            return Err(format_err!("Should not retain more than {} logs", MAX_RECORDS_PER_BUCKET));
        }

        // Repeat the first message another time. Its count should increase to 2.
        let msg = create_message("[path/to/file.cpp(1)] Hello");
        state.granular_stats.record_log(&msg);
        assert_inspect_tree!(state.inspector,
          root: {
            granular_stats: {
              "0": contains {
                "0": {
                    "file_path": "path/to/file.cpp",
                    "line_no": 1u64,
                    "count": 2u64
                }
              }
            }
          }
        );
        Ok(())
    }

    struct ComponentTestState {
        executor: fasync::Executor,
        max_run_until_stalled_iterations: u32,
    }

    impl ComponentTestState {
        fn new(max_run_until_stalled_iterations: u32) -> Result<ComponentTestState, anyhow::Error> {
            let executor = fasync::Executor::new_with_fake_time().expect("executor should build");
            executor.set_fake_time(fasync::Time::from_nanos(0));
            Ok(ComponentTestState { executor, max_run_until_stalled_iterations })
        }

        fn set_time(&mut self, time: i64) {
            self.executor.set_fake_time(fasync::Time::from_nanos(time));
        }

        fn run<F>(&mut self, fut: &mut F)
        where
            F: Future + Unpin,
        {
            assert!(self.executor.run_until_stalled(fut).is_ready());
        }

        fn run_timers(&mut self) {
            assert!(self.executor.wake_expired_timers());
            self.sync();
        }

        fn sync(&mut self) {
            let mut i = 0;
            while self.executor.is_waiting() == fasync::WaitState::Ready {
                assert!(self.executor.run_until_stalled(&mut Box::pin(async {})).is_ready());
                i += 1;
                assert!(i < self.max_run_until_stalled_iterations);
            }
        }

        fn set_time_and_run_timers(&mut self, time: i64) {
            self.set_time(time);
            self.run_timers();
        }

        fn assert_no_timers(&mut self) {
            assert_eq!(None, self.executor.wake_next_timer());
        }

        fn assert_next_timer_at(&mut self, time: i64) {
            assert_eq!(
                fasync::WaitState::Waiting(fasync::Time::from_nanos(time)),
                self.executor.is_waiting()
            );
        }
    }

    #[test]
    fn component_stats_retained_then_dropped() -> Result<(), anyhow::Error> {
        // Setup clean state with predictable executor.
        let mut state = ComponentTestState::new(1000)?;
        state.assert_no_timers();

        let inspector = Inspector::new();
        let mut component_stats = LogStatsByComponent::default();
        component_stats.iattach(inspector.root(), "component_stats")?;
        let component_a = source_id_with_url("a");
        let gc_interval_nanos =
            zx::Duration::from_hours(COMPONENT_STATS_GC_INTERVAL_HOURS).into_nanos();

        // Allow GC task to spin unp, then assert first GC timer set.
        state.sync();
        state.assert_next_timer_at(gc_interval_nanos);

        println!("About to run 1st access");

        // 1st access: stats lazily created.
        state.run(&mut Box::pin(async {
            println!("Started 1st access");
            let component_stats = component_stats.get_component_log_stats(&component_a).await;
            println!("Component stats unlocked");
            let msg = create_message_with_severity("[path/to/file.cpp(123)] Hello", Severity::Info);
            component_stats.record_log(&msg);
            println!("Log recorded");
            // Do not retain Arc; it should be kept alive by the timeout mechanism.
            drop(component_stats);
            println!("Component stats Arc dropped");
        }));
        assert_inspect_tree!(inspector,
            root: {
                component_stats: {
                    a: {
                        "last_log_monotonic_nanos": 0i64,
                        "total_logs": 1u64,
                        "trace_logs": 0u64,
                        "debug_logs": 0u64,
                        "info_logs": 1u64,
                        "warning_logs": 0u64,
                        "error_logs": 0u64,
                        "fatal_logs": 0u64,
                    }
                }
            }
        );

        // Advance to t=timeout: timer should fire, marking stats for GC, but not deleting them.
        state.set_time_and_run_timers(gc_interval_nanos);
        state.assert_next_timer_at(2 * gc_interval_nanos);

        // 2nd access: stats un-marked for GC.
        state.run(&mut Box::pin(async {
            let component_stats = component_stats.get_component_log_stats(&component_a).await;
            let msg = create_message_with_severity("[path/to/file.cpp(123)] Hello", Severity::Info);
            component_stats.record_log(&msg);
            // Do not retain Arc; it should be kept alive by the timeout mechanism.
            drop(component_stats);
        }));

        // Advance to t=2*timeout: timer should fire, marking stats (again) for GC.
        state.set_time_and_run_timers(2 * gc_interval_nanos);
        state.assert_next_timer_at(3 * gc_interval_nanos);

        // Access from another component, "b". Should not be GC'd in the next cycle (but rather,
        // the following) cycle.
        let component_b = source_id_with_url("b");
        state.run(&mut Box::pin(async {
            let component_stats = component_stats.get_component_log_stats(&component_b).await;
            let msg =
                create_message_with_severity("[some/other/file.rs(456)] Goodbye", Severity::Info);
            component_stats.record_log(&msg);
            // Do not retain Arc; it should be kept alive by the timeout mechanism.
            drop(component_stats);
        }));

        // Both logs are in stats; freshness matches time of last recorded log.
        assert_inspect_tree!(inspector,
            root: {
                component_stats: {
                    a: {
                        "last_log_monotonic_nanos": gc_interval_nanos,
                        "total_logs": 2u64,
                        "trace_logs": 0u64,
                        "debug_logs": 0u64,
                        "info_logs": 2u64,
                        "warning_logs": 0u64,
                        "error_logs": 0u64,
                        "fatal_logs": 0u64,
                    },
                    b: {
                        "last_log_monotonic_nanos": 2 * gc_interval_nanos,
                        "total_logs": 1u64,
                        "trace_logs": 0u64,
                        "debug_logs": 0u64,
                        "info_logs": 1u64,
                        "warning_logs": 0u64,
                        "error_logs": 0u64,
                        "fatal_logs": 0u64,
                    }
                }
            }
        );

        // Advance to t=3*timeout: timer should fire, deleting "a" stats as garbage.
        state.set_time_and_run_timers(3 * gc_interval_nanos);
        state.assert_next_timer_at(4 * gc_interval_nanos);
        assert_inspect_tree!(inspector, root: {
            "component_stats": {
                b: {
                    "last_log_monotonic_nanos": 2 * gc_interval_nanos,
                    "total_logs": 1u64,
                    "trace_logs": 0u64,
                    "debug_logs": 0u64,
                    "info_logs": 1u64,
                    "warning_logs": 0u64,
                    "error_logs": 0u64,
                    "fatal_logs": 0u64,
                }
            }
        });

        // Advance to t=4*timeout: timer should fire, deleting "b" stats as garbage.
        state.set_time_and_run_timers(4 * gc_interval_nanos);
        state.assert_next_timer_at(5 * gc_interval_nanos);
        assert_inspect_tree!(inspector, root: {
            "component_stats": {}
        });

        Ok(())
    }

    proptest! {
        #[test]
        fn random_string(string in r"\p{Any}*") {
            prop_assert!(verify_message_ignored(&string).is_ok());
        }

        #[test]
        fn random_file_and_line(file in r"[a-zA-Z0-9_/\.]+", line in 1..999999, msg in r"\p{Any}*") {
            let msg_str = format!("[{}({})] {}", file, line, msg);
            prop_assert!(verify_file_and_line(&msg_str, &file, line as u64).is_ok());
        }
    }

    fn create_message(msg: &str) -> Message {
        create_message_with_severity(msg, Severity::Error)
    }

    fn create_message_with_severity(msg: &str, severity: Severity) -> Message {
        Message::new(
            zx::Time::from_nanos(1),
            severity,
            METADATA_SIZE + 1 + msg.len(),
            0, // dropped
            PLACEHOLDER_MONIKER,
            PLACEHOLDER_URL,
            LogsHierarchy::new(
                "root",
                vec![LogsProperty::String(LogsField::Msg, msg.to_string())],
                vec![],
            ),
        )
    }

    fn verify_message_ignored(msg_str: &str) -> Result<(), anyhow::Error> {
        let mut state = GranularTestState::new()?;
        let msg = create_message(msg_str);
        state.granular_stats.record_log(&msg);
        let tree_assertion = tree_assertion!(
              root: {
                granular_stats: {
                    "0": {
                        overflowed: false,
                    }
                }
              }
        );
        match tree_assertion.run(state.inspector.get_node_hierarchy().as_ref()) {
            Ok(()) => Ok(()),
            Err(e) => Err(format_err!("Message not ignored: {} \n {}", msg_str, e)),
        }
    }

    fn verify_file_and_line(msg_str: &str, file: &str, line: u64) -> Result<(), anyhow::Error> {
        let mut state = GranularTestState::new()?;
        let msg = create_message(msg_str);
        state.granular_stats.record_log(&msg);
        let tree_assertion = tree_assertion!(
              root: {
                granular_stats: {
                  "0": {
                    overflowed: false,
                    "0": {
                        "file_path": file.to_string(),
                        "line_no": line,
                        "count": 1u64,
                    }
                  }
                }
              }
        );
        match tree_assertion.run(state.inspector.get_node_hierarchy().as_ref()) {
            Ok(()) => Ok(()),
            Err(e) => Err(format_err!("Parsing failed for message: {} \n {}", msg_str, e)),
        }
    }

    fn source_id_with_url(component_url: &str) -> SourceIdentity {
        SourceIdentity {
            realm_path: None,
            component_url: Some(component_url.to_string()),
            component_name: None,
            instance_id: None,
        }
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::message::Severity;
use diagnostics_data::LogsData;
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use fuchsia_inspect_derive::Inspect;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::Arc;

/// Structure that holds stats for the log manager.
#[derive(Default, Inspect)]
pub struct LogManagerStats {
    total_logs: inspect::UintProperty,
    kernel_logs: inspect::UintProperty,
    logsink_logs: inspect::UintProperty,
    trace_logs: inspect::UintProperty,
    debug_logs: inspect::UintProperty,
    info_logs: inspect::UintProperty,
    warning_logs: inspect::UintProperty,
    error_logs: inspect::UintProperty,
    by_component: LogStatsByComponent,
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
}

impl LogStatsByComponent {
    fn new(inspect_node: inspect::Node) -> Self {
        let components = Arc::new(Mutex::new(HashMap::new()));
        Self { components, inspect_node }
    }

    pub fn get_component_log_stats(&self, url: &str) -> Arc<ComponentLogStats> {
        let mut components = self.components.lock();
        match components.get(url) {
            Some(stats) => stats.clone(),
            None => {
                let mut stats = ComponentLogStats::default();
                // TODO(fxbug.dev/60396): Report failure to attach somewhere.
                let _ = stats.iattach(&self.inspect_node, url);
                let stats = Arc::new(stats);
                components.insert(url.to_string(), stats.clone());
                stats
            }
        }
    }
}

impl Default for LogStatsByComponent {
    fn default() -> Self {
        Self::new(inspect::Node::default())
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
    pub fn record_log(&self, msg: &LogsData) {
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
    pub fn record_log(&mut self, msg: &LogsData, source: LogSource) {
        self.total_logs.add(1);
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
    pub fn get_component_log_stats(&self, url: &str) -> Arc<ComponentLogStats> {
        self.by_component.get_component_log_stats(url)
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
pub enum LogSource {
    /// Log came from the kernel log (klog)
    Kernel,
    /// Log came from log sink
    LogSink,
}

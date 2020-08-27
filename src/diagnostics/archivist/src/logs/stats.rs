// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::message::{Message, Severity};
use fidl_fuchsia_sys_internal::SourceIdentity;
use fuchsia_inspect::{self as inspect, NumericProperty};
use fuchsia_inspect_derive::Inspect;
use futures::lock::Mutex;
use std::collections::HashMap;
use std::sync::{Arc, Weak};

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
    fatal_logs: inspect::UintProperty,
    closed_streams: inspect::UintProperty,
    unattributed_log_sinks: inspect::UintProperty,
}

#[derive(Default, Inspect)]
struct LogStatsByComponent {
    // Note: This field is manually managed as the Inspect derive macro does
    // not yet support collections.
    #[inspect(skip)]
    components: Arc<Mutex<HashMap<String, Weak<Mutex<ComponentLogStats>>>>>,
    inspect_node: inspect::Node,
}

impl LogStatsByComponent {
    pub async fn get_component_log_stats(
        &self,
        identity: &SourceIdentity,
    ) -> Arc<Mutex<ComponentLogStats>> {
        let url = identity.component_url.clone().unwrap_or("(unattributed)".to_string());
        let mut components = self.components.lock().await;
        if let Some(component_log_stats) =
            components.get(&url).map(|value| value.upgrade()).flatten()
        {
            component_log_stats
        } else {
            let mut component_log_stats = ComponentLogStats::default();
            let _ = component_log_stats.iattach(&self.inspect_node, url.clone());
            let component_log_stats = Arc::new(Mutex::new(component_log_stats));
            components.insert(url, Arc::downgrade(&component_log_stats));
            component_log_stats
        }
    }
}

#[derive(Inspect, Default)]
pub struct ComponentLogStats {
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
        self.total_logs.add(1);
        match msg.0.metadata.severity {
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
    pub fn record_log(&self, msg: &Message, source: LogSource) {
        self.total_logs.add(1);
        match source {
            LogSource::Kernel => {
                self.kernel_logs.add(1);
            }
            LogSource::LogSink => {
                self.logsink_logs.add(1);
            }
        }
        match msg.0.metadata.severity {
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
    ) -> Arc<Mutex<ComponentLogStats>> {
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

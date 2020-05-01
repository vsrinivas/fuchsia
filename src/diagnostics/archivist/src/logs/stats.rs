// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::message::{Message, Severity};
use fuchsia_inspect::{self as inspect, NumericProperty};
use fuchsia_inspect_derive::Inspect;

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
    fatal_logs: inspect::UintProperty,
    closed_streams: inspect::UintProperty,
    unattributed_log_sinks: inspect::UintProperty,
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
        match msg.severity {
            Severity::Trace => self.trace_logs.add(1),
            Severity::Debug => self.debug_logs.add(1),
            Severity::Info => self.info_logs.add(1),
            Severity::Warn => self.warning_logs.add(1),
            Severity::Error => self.error_logs.add(1),
            Severity::Fatal => self.fatal_logs.add(1),
        }
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
    /// Log came from unattributed log sink
    LogSink,
}

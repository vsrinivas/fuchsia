// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_logger::{LogLevelFilter, LogMessage};
use fuchsia_inspect::{self as inspect, NumericProperty};
use std::convert::TryInto;

/// Structure that holds stats for the log manager.
pub(super) struct LogManagerStats {
    _node: inspect::Node,
    total_logs: inspect::UintProperty,
    kernel_logs: inspect::UintProperty,
    logsink_logs: inspect::UintProperty,
    verbose_logs: inspect::UintProperty,
    info_logs: inspect::UintProperty,
    warning_logs: inspect::UintProperty,
    error_logs: inspect::UintProperty,
    fatal_logs: inspect::UintProperty,
}

impl LogManagerStats {
    /// Create a stat holder, publishing counters under the given node.
    pub fn new(node: inspect::Node) -> Self {
        let total_logs = node.create_uint("total_logs", 0);
        let kernel_logs = node.create_uint("kernel_logs", 0);
        let logsink_logs = node.create_uint("logsink_logs", 0);
        let verbose_logs = node.create_uint("verbose_logs", 0);
        let info_logs = node.create_uint("info_logs", 0);
        let warning_logs = node.create_uint("warning_logs", 0);
        let error_logs = node.create_uint("error_logs", 0);
        let fatal_logs = node.create_uint("fatal_logs", 0);

        Self {
            _node: node,
            kernel_logs,
            logsink_logs,
            total_logs,
            verbose_logs,
            info_logs,
            warning_logs,
            error_logs,
            fatal_logs,
        }
    }

    /// Record an incoming log from a given source.
    ///
    /// This method updates the counters based on the contents of the log message.
    pub fn record_log(&self, msg: &LogMessage, source: LogSource) {
        self.total_logs.add(1);
        match source {
            LogSource::Kernel => {
                self.kernel_logs.add(1);
            }
            LogSource::LogSink => {
                self.logsink_logs.add(1);
            }
        }

        let severity = msg.severity.try_into().unwrap_or(i8::max_value());
        match LogLevelFilter::from_primitive(severity) {
            Some(LogLevelFilter::Info) => {
                self.info_logs.add(1);
            }
            Some(LogLevelFilter::Warn) => {
                self.warning_logs.add(1);
            }
            Some(LogLevelFilter::Error) => {
                self.error_logs.add(1);
            }
            Some(LogLevelFilter::Fatal) => {
                self.fatal_logs.add(1);
            }
            _ => {
                self.verbose_logs.add(1);
            }
        }
    }
}

/// Denotes the source of a particular log message.
pub(super) enum LogSource {
    /// Log came from the kernel log (klog)
    Kernel,
    /// Log came from unattributed log sink
    LogSink,
}

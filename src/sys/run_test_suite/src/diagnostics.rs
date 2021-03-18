// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::writer,
    anyhow::Error,
    diagnostics_data::{LogsData, Severity},
    futures::{Stream, TryStreamExt},
};

// TODO(fxbug.dev/54198, fxbug.dev/70581): deprecate this when implementing metadata selectors for
// logs or when we support OnRegisterInterest that can be sent to *all* test components.
#[derive(Clone, Default)]
pub struct LogCollectionOptions {
    pub min_severity: Option<Severity>,
    pub max_severity: Option<Severity>,
}

impl LogCollectionOptions {
    fn is_restricted_log(&self, log: &LogsData) -> bool {
        let severity = log.metadata.severity;
        matches!(self.max_severity, Some(max) if severity > max)
    }

    fn should_display(&self, log: &LogsData) -> bool {
        let severity = log.metadata.severity;
        matches!(self.min_severity, None)
            || matches!(self.min_severity, Some(min) if severity >= min)
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum LogCollectionOutcome {
    Error { restricted_logs: Vec<String> },
    Passed,
}

impl From<Vec<String>> for LogCollectionOutcome {
    fn from(restricted_logs: Vec<String>) -> Self {
        if restricted_logs.is_empty() {
            LogCollectionOutcome::Passed
        } else {
            LogCollectionOutcome::Error { restricted_logs }
        }
    }
}

pub async fn collect_logs<S, W>(
    mut stream: S,
    writer: &mut W,
    options: LogCollectionOptions,
) -> Result<LogCollectionOutcome, Error>
where
    W: writer::WriteLine,
    S: Stream<Item = Result<LogsData, Error>> + Unpin,
{
    let mut restricted_logs = vec![];
    while let Some(log) = stream.try_next().await? {
        let msg = log.msg().unwrap_or("");
        let moniker = log.moniker.replace("test_root/", "");
        let log_repr =
            format!("[{}][{}] {}: {}", log.metadata.timestamp, moniker, log.metadata.severity, msg);

        if options.should_display(&log) {
            writer.write_line(&format!("{}", log_repr)).expect("Failed to write log");
        }

        if options.is_restricted_log(&log) {
            restricted_logs.push(log_repr);
        }
    }
    Ok(restricted_logs.into())
}

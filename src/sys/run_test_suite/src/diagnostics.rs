// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{LogsData, Severity},
    futures::{Stream, TryStreamExt},
    std::io::Write,
};

// TODO(fxbug.dev/54198, fxbug.dev/70581): deprecate this when implementing metadata selectors for
// logs or when we support OnRegisterInterest that can be sent to *all* test components.
#[derive(Clone, Default)]
pub struct LogCollectionOptions {
    pub min_severity: Option<Severity>,
}

pub async fn collect_logs<S, W>(
    mut stream: S,
    writer: &mut W,
    options: LogCollectionOptions,
) -> Result<(), Error>
where
    W: Write,
    S: Stream<Item = Result<LogsData, Error>> + Unpin,
{
    while let Some(log) = stream.try_next().await? {
        // TODO(fxbug.dev/67960): check log severity.
        if should_skip(&log, &options) {
            continue;
        }
        if let Some(msg) = log.msg() {
            let moniker = log.moniker.replace("test_root/", "");
            writeln!(
                writer,
                "[{}][{}] {}: {}",
                log.metadata.timestamp, moniker, log.metadata.severity, msg
            )
            .expect("Failed to write log");
        }
    }
    Ok(())
}

fn should_skip(log: &LogsData, options: &LogCollectionOptions) -> bool {
    let severity = log.metadata.severity;
    match options.min_severity {
        Some(min) => severity < min,
        None => false,
    }
}

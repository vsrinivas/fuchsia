// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    diagnostics_data::{BuilderArgs, LogsDataBuilder, Severity},
    futures::AsyncWriteExt,
};

/// Generates a system log entry from the given `msg` and sends it to the given `socket`.
///
/// This mimics logs generated using //src/lib/diagnostics/data. These logs include:
///   * A timestamp in nanoseconds when the log was generated.
///   * The URL of the component generating the log.
///   * The component moniker (https://fuchsia.dev/fuchsia-src/reference/components/moniker)
///   * The log severity, e.g. fatal, error, warning, etc.
///   * The log message.
///
/// See also `writer::Writer::log` which formats these entries for display.
pub async fn send_log_entry<S: AsRef<str>>(socket: &mut fidl::AsyncSocket, msg: S) -> Result<()> {
    let builder_args = BuilderArgs {
        timestamp_nanos: 0.into(),
        component_url: Some(String::default()),
        moniker: "moniker".to_string(),
        severity: Severity::Info,
    };
    let builder = LogsDataBuilder::new(builder_args);
    let logs_data = vec![builder.set_message(msg.as_ref()).build()];
    let bytes = serde_json::to_vec(&logs_data)?;
    socket.write_all(&bytes).await?;
    Ok(())
}

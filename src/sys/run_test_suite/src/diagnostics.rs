// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::LogsData,
    futures::{Stream, TryStreamExt},
    std::io::Write,
};

pub async fn collect_logs<S, W>(mut stream: S, writer: &mut W) -> Result<(), Error>
where
    W: Write,
    S: Stream<Item = Result<LogsData, Error>> + Unpin,
{
    while let Some(log) = stream.try_next().await? {
        // TODO(fxbug.dev/67960): check log severity.
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

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::logs::{
    debuglog::KERNEL_URL,
    stats::{LogManagerStats, LogSource},
};
use diagnostics_reader::{ArchiveReader, Logs};
use fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, ArchiveAccessorProxy};
use fuchsia_async as fasync;
use fuchsia_component::{client::connect_to_service, server::ServiceFs};
use fuchsia_inspect::{
    component::{health, inspector},
    health::Reporter,
};
use fuchsia_inspect_derive::WithInspect;
use futures::{future::join, prelude::*};
use tracing::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init()?;
    let mut service_fs = ServiceFs::new_local();
    service_fs.take_and_serve_directory_handle()?;

    inspector().serve(&mut service_fs)?;
    health().set_starting_up();
    let stats = LogManagerStats::default().with_inspect(inspector().root(), "log_stats")?;

    let accessor = connect_to_service::<ArchiveAccessorMarker>()?;

    health().set_ok();
    info!("Maintaining.");
    join(maintain(stats, accessor), service_fs.collect::<()>()).await;
    info!("Exiting.");
    Ok(())
}

async fn maintain(mut stats: LogManagerStats, archive: ArchiveAccessorProxy) {
    let reader = ArchiveReader::new().with_archive(archive);

    let (mut logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap();
    let _errors = fasync::Task::spawn(async move {
        while let Some(error) = errors.next().await {
            panic!("Error encountered while retrieving logs: {}", error);
        }
    });

    while let Some(log) = logs.next().await {
        let source = if log.metadata.component_url == KERNEL_URL {
            LogSource::Kernel
        } else {
            LogSource::LogSink
        };
        stats.record_log(&log, source);
        stats.get_component_log_stats(&log.metadata.component_url).await.record_log(&log);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn smoke_test() {
        assert!(true);
    }
}

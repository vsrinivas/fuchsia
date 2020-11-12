// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use anyhow::Error;
use archivist_lib::logs::redact::{REDACTED_CANARY_MESSAGE, UNREDACTED_CANARY_MESSAGE};
use diagnostics_reader::{ArchiveReader, Logs};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_async::Task;
use fuchsia_component::client::launcher;
use fuchsia_component::client::{App, AppBuilder};
use futures::prelude::*;
use std::{
    fs::{create_dir_all, remove_dir_all, write, File},
    path::Path,
};
use tracing::debug;

#[fuchsia::test]
async fn canary_is_redacted_with_filtering() {
    debug!("test started");
    let env = TestEnv::with_filtering().await.unwrap();
    debug!("retrieving logs from feedback accessor");
    let feedback_logs = env.feedback_reader.snapshot::<Logs>().await.unwrap();
    let all_logs = env.all_reader.snapshot::<Logs>().await.unwrap();

    let (unredacted, redacted) = all_logs
        .into_iter()
        .zip(feedback_logs)
        .find(|(u, _)| u.msg() == Some(UNREDACTED_CANARY_MESSAGE))
        .unwrap();
    debug!(unredacted = %unredacted.msg().unwrap());
    if unredacted.msg().unwrap() == UNREDACTED_CANARY_MESSAGE {
        assert_eq!(redacted.msg().unwrap(), REDACTED_CANARY_MESSAGE);
    }
}

#[fuchsia::test]
async fn canary_is_unredacted_without_filtering() {
    debug!("test started");
    let env = TestEnv::without_filtering().await.unwrap();
    debug!("retrieving logs from feedback accessor");
    let feedback_logs = env.feedback_reader.snapshot::<Logs>().await.unwrap();
    let all_logs = env.all_reader.snapshot::<Logs>().await.unwrap();

    let (unredacted, redacted) = all_logs
        .into_iter()
        .zip(feedback_logs)
        .find(|(u, _)| u.msg() == Some(UNREDACTED_CANARY_MESSAGE))
        .unwrap();
    debug!(unredacted = %unredacted.msg().unwrap());
    if unredacted.msg().unwrap() == UNREDACTED_CANARY_MESSAGE {
        assert_eq!(redacted.msg().unwrap(), UNREDACTED_CANARY_MESSAGE);
    }
}

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/test-logs-redaction#meta/own-logs-consuming-archivist.cmx";
const ARCHIVIST_CONFIG: &[u8] = include_bytes!("../configs/archivist_config.json");

struct TestEnv {
    _archivist: App,
    _monitor_archivist: Task<()>,
    feedback_reader: ArchiveReader,
    all_reader: ArchiveReader,
}

impl TestEnv {
    async fn without_filtering() -> Result<Self, Error> {
        Self::new(false).await
    }

    async fn with_filtering() -> Result<Self, Error> {
        Self::new(true).await
    }

    async fn new(filter: bool) -> Result<Self, Error> {
        debug!(%filter, "creating test environment");
        let config_path = Path::new("/tmp/config/data");
        let feedback_path = config_path.join("feedback");
        remove_dir_all(config_path).unwrap_or_default();
        create_dir_all(&feedback_path)?;

        write(config_path.join("archivist_config.json"), ARCHIVIST_CONFIG)?;

        // VERY IMPORTANT -- the feedback pipeline disables all filtering if no files present
        write(feedback_path.join("empty.cfg"), b"")?;

        if !filter {
            write(
                config_path.join("feedback/DISABLE_FILTERING.txt"),
                "This file disables filtering",
            )?;
        }

        let _archivist = AppBuilder::new(ARCHIVIST_URL)
            .add_dir_to_namespace("/config/data".into(), File::open(config_path)?)?
            .spawn(&launcher()?)?;

        // first we'll wait for the archivist to start, then make sure it doesn't stop in the background
        let mut component_stream = _archivist.controller().take_event_stream();
        let handle_termination = |code, reason| {
            panic!("Archivist terminated unexpectedly. Code: {}. Reason: {:?}", code, reason);
        };

        debug!("waiting for archivist to start");
        match component_stream.try_next().await?.unwrap() {
            ComponentControllerEvent::OnDirectoryReady {} => debug!("archivist directory ready"),
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                handle_termination(return_code, termination_reason)
            }
        }

        let _monitor_archivist = Task::spawn(async move {
            while let Some(next) = component_stream.try_next().await.unwrap() {
                match next {
                    ComponentControllerEvent::OnDirectoryReady {} => {
                        panic!("received dir ready 2x")
                    }
                    ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                        handle_termination(return_code, termination_reason)
                    }
                }
            }
        });

        let all_reader = ArchiveReader::new()
            .with_archive(_archivist.connect_to_service::<ArchiveAccessorMarker>().unwrap());
        let feedback_reader = ArchiveReader::new().with_archive(
            _archivist
                .connect_to_named_service::<ArchiveAccessorMarker>(
                    "fuchsia.diagnostics.FeedbackArchiveAccessor",
                )
                .unwrap(),
        );

        debug!("test env created");
        Ok(TestEnv { _archivist, all_reader, feedback_reader, _monitor_archivist })
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_reader::{assert_data_tree, ArchiveReader, Logs, Severity};
use fuchsia_async::Task;
use futures::prelude::*;

#[fuchsia_async::run_singlethreaded(test)]
async fn logs_from_crashing_component() {
    fuchsia_syslog::init().unwrap();
    let reader = ArchiveReader::new();
    let (mut logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap();
    let _errors = Task::spawn(async move {
        while let Some(e) = errors.next().await {
            panic!("error in subscription: {}", e);
        }
    });

    log::info!("test started");
    let our_message = logs.next().await.unwrap();
    assert_data_tree!(our_message.payload.as_ref().unwrap(), root: contains {
        "message": "test started",
    });

    let crasher_status = fuchsia_component::client::launch(
        &fuchsia_component::client::launcher().unwrap(),
        "fuchsia-pkg://fuchsia.com/test-logs-from-crash#meta/logs-then-crashes.cmx".into(),
        None,
    )
    .unwrap()
    .wait()
    .await
    .unwrap();
    assert!(!crasher_status.success(), "crasher should have crashed!");

    let crasher_info = logs.next().await.unwrap();
    assert_eq!(crasher_info.metadata.severity, Severity::Info);
    assert_data_tree!(crasher_info.payload.unwrap(), root: contains {
        "message": "crasher has initialized",
    });

    let crasher_warn = logs.next().await.unwrap();
    assert_eq!(crasher_warn.metadata.severity, Severity::Warn);
    assert_data_tree!(crasher_warn.payload.unwrap(), root: contains {
        "message": "crasher is approaching the crash",
    });

    let crasher_error = logs.next().await.unwrap();
    assert_eq!(crasher_error.metadata.severity, Severity::Error);
    assert_data_tree!(crasher_error.payload.unwrap(), root: contains {
        "message": "oh no we're crashing",
    });
}

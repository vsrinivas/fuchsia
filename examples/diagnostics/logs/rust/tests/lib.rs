// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_reader::{assert_data_tree, ArchiveReader, Data, Logs, Severity};
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage};
use fuchsia_async::Task;
use fuchsia_syslog as syslog;
use fuchsia_syslog_listener::run_log_listener_with_proxy;
use futures::{channel::mpsc, prelude::*};

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_example_and_read_hello_world() {
    let url = "fuchsia-pkg://fuchsia.com/rust-logs-example-tests#meta/rust-logs-example.cmx";
    let launcher = fuchsia_component::client::launcher().unwrap();
    let mut app = fuchsia_component::client::launch(&launcher, url.into(), None).unwrap();
    let status = app.wait().await.unwrap();
    assert!(status.success());

    let (logs, mut new_logs, _tasks) = listen_to_logs();
    pin_utils::pin_mut!(logs);

    let (next, new_next) = (logs.next().await.unwrap(), new_logs.next().await.unwrap());
    assert_eq!(next.severity, syslog::levels::DEBUG);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "should print ");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    assert_eq!(new_next.metadata.severity, Severity::Debug);
    // TODO(fxbug.dev/65319) uncomment when built-in archivist has attribution again
    // assert_eq!(new_next.metadata.component_url, url);
    // assert_eq!(new_next.moniker, "rust-logs-example.cmx");
    assert_data_tree!(new_next.payload.unwrap(), root: contains {
        "tag": "rust_logs_example",
        "message": "should print ",
    });

    let (next, new_next) = (logs.next().await.unwrap(), new_logs.next().await.unwrap());
    assert_eq!(next.severity, syslog::levels::INFO);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "hello, world! foo=1 bar=\"baz\" ");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    assert_eq!(new_next.metadata.severity, Severity::Info);
    // TODO(fxbug.dev/65319) uncomment when built-in archivist has attribution again
    // assert_eq!(new_next.metadata.component_url, url);
    // assert_eq!(new_next.moniker, "rust-logs-example.cmx");
    assert_data_tree!(new_next.payload.unwrap(), root: contains {
        "tag": "rust_logs_example",
        // note that the frontend is still stringifying the structured fields, will tackle soon
        "message": "hello, world! foo=1 bar=\"baz\" ",
    });

    let (next, new_next) = (logs.next().await.unwrap(), new_logs.next().await.unwrap());
    assert_eq!(next.severity, syslog::levels::WARN);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "warning: using old api");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    assert_eq!(new_next.metadata.severity, Severity::Warn);
    // TODO(fxbug.dev/65319) uncomment when built-in archivist has attribution again
    // assert_eq!(new_next.metadata.component_url, url);
    // assert_eq!(new_next.moniker, "rust-logs-example.cmx");
    assert_data_tree!(new_next.payload.unwrap(), root: contains {
        "tag": "rust_logs_example",
        "message": "warning: using old api",
    });
}

fn listen_to_logs(
) -> (impl Stream<Item = LogMessage>, impl Stream<Item = Data<Logs>>, (Task<()>, Task<()>)) {
    let reader = ArchiveReader::new();

    let log_proxy = fuchsia_component::client::connect_to_service::<LogMarker>().unwrap();
    let mut options = LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        min_severity: LogLevelFilter::None,
        verbosity: 0,
        filter_by_tid: false,
        tid: 0,
        tags: vec![],
    };
    let (send_logs, recv_logs) = mpsc::unbounded();
    let _old_listener = Task::spawn(async move {
        run_log_listener_with_proxy(&log_proxy, send_logs, Some(&mut options), false, None)
            .await
            .unwrap();
    });

    let logs = recv_logs.filter(|m| {
        let from_archivist = m.tags.iter().any(|t| t == "archivist");
        async move { !from_archivist }
    });
    let (new_logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap();

    let _check_errors = Task::spawn(async move {
        loop {
            match errors.next().await {
                Some(error) => panic!("log testing client encountered an error: {}", error),
                None => break,
            }
        }
    });

    (logs, new_logs, (_old_listener, _check_errors))
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_testing::{assert_data_tree, EnvWithDiagnostics, Launched, Logs, Severity};
use fuchsia_async as fasync;
use fuchsia_syslog as syslog;
use futures::prelude::*;

#[fasync::run_singlethreaded(test)]
async fn launch_example_and_read_hello_world() {
    let mut test_env = EnvWithDiagnostics::new().await;
    let url = "fuchsia-pkg://fuchsia.com/rust-logs-example-tests#meta/rust-logs-example.cmx";
    let Launched { mut app, reader } = test_env.launch(url, None);
    let status = app.wait().await.unwrap();
    assert!(status.success());

    let mut logs = test_env.listen_to_logs().take(3).collect::<Vec<_>>().await.into_iter();
    let mut new_logs = reader.snapshot::<Logs>().await.into_iter();

    let (next, new_next) = (logs.next().unwrap(), new_logs.next().unwrap());
    assert_eq!(next.severity, syslog::levels::DEBUG);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "should print ");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    assert_eq!(new_next.metadata.severity, Severity::Debug);
    assert_eq!(new_next.metadata.component_url, url);
    assert_eq!(new_next.moniker, "rust-logs-example.cmx");
    assert_data_tree!(new_next.payload.unwrap(), root: contains {
        "tag": "rust_logs_example",
        "message": "should print ",
    });

    let (next, new_next) = (logs.next().unwrap(), new_logs.next().unwrap());
    assert_eq!(next.severity, syslog::levels::INFO);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "hello, world! foo=1 bar=\"baz\" ");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    assert_eq!(new_next.metadata.severity, Severity::Info);
    assert_eq!(new_next.metadata.component_url, url);
    assert_eq!(new_next.moniker, "rust-logs-example.cmx");
    assert_data_tree!(new_next.payload.unwrap(), root: contains {
        "tag": "rust_logs_example",
        // note that the frontend is still stringifying the structured fields, will tackle soon
        "message": "hello, world! foo=1 bar=\"baz\" ",
    });

    let (next, new_next) = (logs.next().unwrap(), new_logs.next().unwrap());
    assert_eq!(next.severity, syslog::levels::WARN);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "warning: using old api");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    assert_eq!(new_next.metadata.severity, Severity::Warn);
    assert_eq!(new_next.metadata.component_url, url);
    assert_eq!(new_next.moniker, "rust-logs-example.cmx");
    assert_data_tree!(new_next.payload.unwrap(), root: contains {
        "tag": "rust_logs_example",
        "message": "warning: using old api",
    });
}

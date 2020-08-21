// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_testing::EnvWithDiagnostics;
use fuchsia_async as fasync;
use fuchsia_syslog as syslog;
use futures::prelude::*;

#[fasync::run_singlethreaded(test)]
async fn launch_example_and_read_hello_world() {
    let mut test_env = EnvWithDiagnostics::new().await;
    let url = "fuchsia-pkg://fuchsia.com/rust-logs-example-tests#meta/rust-logs-example.cmx";
    let status = test_env.launch(url, None).app.wait().await.unwrap();
    assert!(status.success());

    let logs = test_env.listen_to_logs().take(3).collect::<Vec<_>>().await;
    let mut logs = logs.into_iter();

    let next = logs.next().unwrap();
    assert_eq!(next.severity, syslog::levels::DEBUG);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "should print ");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    let next = logs.next().unwrap();
    assert_eq!(next.severity, syslog::levels::INFO);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "hello, world! foo=1 bar=\"baz\" ");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);

    let next = logs.next().unwrap();
    assert_eq!(next.severity, syslog::levels::WARN);
    assert_eq!(next.tags, vec!["rust_logs_example"]);
    assert_eq!(next.msg, "warning: using old api");
    assert_ne!(next.pid, 0);
    assert_ne!(next.tid, 0);
}

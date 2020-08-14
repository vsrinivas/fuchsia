// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_testing::AppWithDiagnostics;
use fuchsia_async as fasync;
use fuchsia_syslog as syslog;

#[fasync::run_singlethreaded(test)]
async fn launch_example_and_read_hello_world() {
    let url = "fuchsia-pkg://fuchsia.com/rust-logs-example-tests#meta/rust-logs-example.cmx";
    let (status, mut logs) = AppWithDiagnostics::launch("logged", url, None).wait().await;
    assert!(status.success());

    logs.sort_by_key(|l| l.time);
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

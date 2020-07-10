// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_syslog as syslog;
use fuchsia_syslog_listener::AppWithLogs;

#[fasync::run_singlethreaded(test)]
async fn launch_example_and_read_hello_world() {
    let url = "fuchsia-pkg://fuchsia.com/rust-logs-example-tests#meta/rust-logs-example.cmx";
    let (status, logs) = AppWithLogs::launch(url, None).wait().await;
    assert!(status.success());
    assert_eq!(logs.len(), 1);

    assert_eq!(logs[0].severity, syslog::levels::INFO);
    assert_eq!(logs[0].tags, vec!["rust-logs-example.cmx"]);
    assert_eq!(logs[0].msg, "hello, world!");
}

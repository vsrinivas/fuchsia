// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage},
    fidl_fuchsia_sys::LauncherMarker,
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    validating_log_listener,
};

#[fasync::run_singlethreaded(test)]
async fn listen_for_syslog() {
    let log_proxy =
        fclient::connect_to_protocol::<LogMarker>().expect("failed to connect to log server");

    let launcher =
        fclient::connect_to_protocol::<LauncherMarker>().expect("failed to connect to launcher");
    let mut child_component = fclient::AppBuilder::new(
        "fuchsia-pkg://fuchsia.com/fuchsia-syslog-integration-tests#meta/panicker.cmx",
    )
    .spawn(&launcher)
    .expect("failed to launch panicker");

    assert!(child_component.wait().await.unwrap().success());

    // only interested in catching the panic log
    let options = LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        filter_by_tid: false,
        tid: 0,
        verbosity: 0,
        min_severity: LogLevelFilter::Error,
        tags: Vec::new(),
    };

    validating_log_listener::validate_log_stream(
        vec![LogMessage {
            // these are ignored by validating-log-listener
            pid: 0,
            tid: 0,
            time: 0,
            dropped_logs: 0,

            // these are checked
            severity: LogLevelFilter::Error as i32,
            tags: vec!["panicker".to_string()],
            msg: "PANIC: oh no, I panicked".to_string(),
        }],
        log_proxy,
        Some(options),
    )
    .await;
}

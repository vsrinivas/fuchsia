// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_logger::{LogLevelFilter, LogMarker, LogMessage},
    fidl_fuchsia_sys::LauncherMarker,
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    validating_log_listener,
};

// TODO(55914): re-enable when not flaking on fuchsia-arm64-asan
#[ignore]
#[fasync::run_singlethreaded(test)]
async fn listen_for_syslog() {
    let log_proxy =
        fclient::connect_to_service::<LogMarker>().expect("failed to connect to log server");

    let launcher =
        fclient::connect_to_service::<LauncherMarker>().expect("failed to connect to launcher");
    let _child_component = fclient::AppBuilder::new(
        "fuchsia-pkg://fuchsia.com/fuchsia-syslog-integration-tests#meta/panicker.cmx",
    )
    .spawn(&launcher)
    .expect("failed to launch panicker");

    validating_log_listener::validate_log_stream(
        vec![
            LogMessage {
                // these are ignored by validating-log-listener
                pid: 0,
                tid: 0,
                time: 0,
                dropped_logs: 0,

                // these are checked
                severity: LogLevelFilter::Info as i32,
                tags: vec!["observer".to_owned(), "archivist".to_owned()],
                msg: "Logging started.".to_string(),
            },
            LogMessage {
                // these are ignored by validating-log-listener
                pid: 0,
                tid: 0,
                time: 0,
                dropped_logs: 0,

                // these are checked
                severity: LogLevelFilter::Error as i32,
                tags: vec!["panicker".to_string()],
                msg: "PANIC: oh no, I panicked".to_string(),
            },
        ],
        log_proxy,
        None,
    )
    .await;
}

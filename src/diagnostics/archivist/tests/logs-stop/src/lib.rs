// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogSinkMarker};
use fidl_fuchsia_sys::LauncherMarker;
use fuchsia_async as fasync;
use fuchsia_component::{
    client::{connect_to_service, launch_with_options, LaunchOptions},
    server::ServiceFs,
};
use fuchsia_syslog::levels::{DEBUG, INFO, WARN};
use fuchsia_syslog_listener::{run_log_listener_with_proxy, LogProcessor};
use futures::{channel::mpsc, StreamExt};

#[fasync::run_singlethreaded(test)]
async fn embedding_stop_api() {
    let launcher = connect_to_service::<LauncherMarker>().unwrap();
    // launch archivist
    let mut archivist = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cmx"
            .to_owned(),
        Some(vec!["--disable-log-connector".to_owned()]),
        LaunchOptions::new(),
    )
    .unwrap();

    let log_proxy = archivist.connect_to_service::<LogMarker>().unwrap();
    let dir_req = archivist.directory_request().clone();
    let mut fs = ServiceFs::new();

    let (env_proxy, mut logging_component) = fs
        .add_proxy_service_to::<LogSinkMarker, _>(dir_req)
        .launch_component_in_nested_environment(
            "fuchsia-pkg://fuchsia.com/test-logs-stop#meta/logging-component.cmx".to_owned(),
            None,
            "stop_test_env",
        )
        .unwrap();
    let _fs = fasync::Task::spawn(Box::pin(async move {
        fs.collect::<()>().await;
    }));

    let mut options = LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        min_severity: LogLevelFilter::None,
        verbosity: 0,
        filter_by_tid: false,
        tid: 0,
        tags: vec!["logging component".to_owned()],
    };
    let (send_logs, recv_logs) = mpsc::unbounded();

    fasync::Task::spawn(async move {
        let l = Listener { send_logs };
        run_log_listener_with_proxy(&log_proxy, l, Some(&mut options), false, None).await.unwrap();
    })
    .detach();

    // wait for logging_component to die
    assert!(logging_component.wait().await.unwrap().success());

    // kill environment before stopping archivist.
    env_proxy.kill().await.unwrap();

    // connect to controller and call stop
    let controller = archivist.connect_to_service::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|l| (l.severity, l.msg)).collect::<Vec<_>>().await;

    // recv_logs returned, means archivist must be dead. check.
    assert!(archivist.wait().await.unwrap().success());
    assert_eq!(
        logs,
        vec![
            (DEBUG, "my debug message.".to_owned()),
            (INFO, "my info message.".to_owned()),
            (WARN, "my warn message.".to_owned()),
        ]
    );
}

struct Listener {
    send_logs: mpsc::UnboundedSender<LogMessage>,
}

impl LogProcessor for Listener {
    fn log(&mut self, message: LogMessage) {
        self.send_logs.unbounded_send(message).unwrap();
    }

    fn done(&mut self) {
        panic!("this should not be called");
    }
}

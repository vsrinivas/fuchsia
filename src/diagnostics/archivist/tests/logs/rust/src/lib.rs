// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogSinkMarker};
use fidl_fuchsia_sys::LauncherMarker;
use fidl_test_log_stdio::StdioPuppetMarker;
use fuchsia_async as fasync;
use fuchsia_component::{
    client::{self as fclient, connect_to_service, launch_with_options, LaunchOptions},
    server::{ServiceFs, ServiceObj},
};
use fuchsia_syslog::{
    self as syslog, fx_log_info,
    levels::{INFO, WARN},
};
use fuchsia_syslog_listener::{self as syslog_listener, run_log_listener_with_proxy, LogProcessor};
use fuchsia_zircon as zx;
use futures::{channel::mpsc, Stream, StreamExt};
use log::warn;

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

fn run_listener(tag: &str) -> impl Stream<Item = LogMessage> {
    let mut options = LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        min_severity: LogLevelFilter::None,
        verbosity: 0,
        filter_by_tid: false,
        tid: 0,
        tags: vec![tag.to_string()],
    };
    let (send_logs, recv_logs) = mpsc::unbounded();
    let l = Listener { send_logs };
    fasync::spawn(async move {
        let fut = syslog_listener::run_log_listener(l, Some(&mut options), false);
        if let Err(e) = fut.await {
            panic!("test fail {:?}", e);
        }
    });
    recv_logs
}

#[fasync::run_singlethreaded(test)]
async fn listen_for_syslog() {
    let random = rand::random::<u16>();
    let tag = "logger_integration_rust".to_string() + &random.to_string();
    syslog::init_with_tags(&[&tag]).expect("should not fail");
    let incoming = run_listener(&tag);
    fx_log_info!("my msg: {}", 10);
    warn!("log crate: {}", 20);

    let mut logs: Vec<LogMessage> = incoming.take(2).collect().await;

    // sort logs to account for out-of-order arrival
    logs.sort_by(|a, b| a.time.cmp(&b.time));
    assert_eq!(2, logs.len());
    assert_eq!(logs[0].tags, vec![tag.clone()]);
    assert_eq!(logs[0].severity, LogLevelFilter::Info as i32);
    assert_eq!(logs[0].msg, "my msg: 10");

    assert_eq!(logs[1].tags[0], tag.clone());
    assert_eq!(logs[1].severity, LogLevelFilter::Warn as i32);
    assert_eq!(logs[1].msg, "log crate: 20");
}

#[fasync::run_singlethreaded(test)]
async fn listen_for_klog() {
    let logs = run_listener("klog");

    let msg = format!("logger_integration_rust test_klog {}", rand::random::<u64>());

    let resource = zx::Resource::from(zx::Handle::invalid());
    let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::empty()).unwrap();
    debuglog.write(msg.as_bytes()).unwrap();

    logs.filter(|m| futures::future::ready(m.msg == msg)).next().await;
}

#[fasync::run_singlethreaded(test)]
async fn listen_for_klog_routed_stdio() {
    let mut logs = run_listener("klog");

    let app_builder = fclient::AppBuilder::new(
        "fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/stdio_puppet.cmx",
    );

    let stdout_resource = zx::Resource::from(zx::Handle::invalid());
    let stdout_debuglog =
        zx::DebugLog::create(&stdout_resource, zx::DebugLogOpts::empty()).unwrap();
    let stderr_resource = zx::Resource::from(zx::Handle::invalid());
    let stderr_debuglog =
        zx::DebugLog::create(&stderr_resource, zx::DebugLogOpts::empty()).unwrap();

    let app = app_builder
        .stdout(stdout_debuglog)
        .stderr(stderr_debuglog)
        .spawn(&fclient::launcher().unwrap())
        .unwrap();

    let puppet = app.connect_to_service::<StdioPuppetMarker>().unwrap();

    let msg = format!("logger_integration_rust test_klog stdout {}", rand::random::<u64>());
    puppet.writeln_stdout(&msg).unwrap();
    logs.by_ref().filter(|m| futures::future::ready(m.msg == msg)).next().await;

    let msg = format!("logger_integration_rust test_klog stderr {}", rand::random::<u64>());
    puppet.writeln_stderr(&msg).unwrap();
    logs.filter(|m| futures::future::ready(m.msg == msg)).next().await;

    // TODO(49357): add test for multiline log once behavior is defined.
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_observer_stop_api() {
    let launcher = connect_to_service::<LauncherMarker>().unwrap();
    // launch observer.cmx
    let mut observer = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx".to_owned(),
        Some(vec!["--disable-log-connector".to_owned()]),
        LaunchOptions::new(),
    )
    .unwrap();

    let log_proxy = observer.connect_to_service::<LogMarker>().unwrap();
    let dir_req = observer.directory_request().clone();
    let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();

    let (_env_proxy, mut logging_component) = fs
        .add_proxy_service_to::<LogSinkMarker, _>(dir_req)
        .launch_component_in_nested_environment(
            "fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/logging_component.cmx"
                .to_owned(),
            None,
            "test_env",
        )
        .unwrap();
    fasync::spawn(Box::pin(async move {
        fs.collect::<()>().await;
    }));

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
    let l = Listener { send_logs };
    fasync::spawn(async move {
        run_log_listener_with_proxy(&log_proxy, l, Some(&mut options), false).await.unwrap();
    });

    // wait for logging_component to die
    assert!(logging_component.wait().await.unwrap().success());

    // connect to controller and call stop
    let controller = observer.connect_to_service::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|l| (l.severity, l.msg)).collect::<Vec<_>>().await;

    // recv_logs returned, means observer must be dead. check.
    assert!(observer.wait().await.unwrap().success());
    assert_eq!(
        logs,
        vec![
            (INFO, "Logging started.".to_owned()),
            (-1, "my debug message.".to_owned()),
            (INFO, "my info message.".to_owned()),
            (WARN, "my warn message.".to_owned()),
        ]
    );
}

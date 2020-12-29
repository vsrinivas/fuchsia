// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_boot::WriteOnlyLogMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMessage};
use fidl_test_log_stdio::StdioPuppetMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{self as fclient, connect_to_service};
use fuchsia_syslog::{self as syslog, fx_log_info};
use fuchsia_syslog_listener::{self as syslog_listener, LogProcessor};
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
    fasync::Task::spawn(async move {
        let fut = syslog_listener::run_log_listener(l, Some(&mut options), false, None);
        if let Err(e) = fut.await {
            panic!("test fail {:?}", e);
        }
    })
    .detach();
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
        "fuchsia-pkg://fuchsia.com/test-logs-basic-integration#meta/stdio_puppet.cmx",
    );

    let boot_log = connect_to_service::<WriteOnlyLogMarker>().unwrap();
    let stdout_debuglog = boot_log.get().await.unwrap();
    let stderr_debuglog = boot_log.get().await.unwrap();

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

    // TODO(fxbug.dev/49357): add test for multiline log once behavior is defined.
}

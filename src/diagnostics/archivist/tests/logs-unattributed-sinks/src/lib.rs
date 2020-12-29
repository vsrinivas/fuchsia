// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::logs::message::fx_log_packet_t;
use fidl::{Socket, SocketOpts};
use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogSinkMarker};
use fidl_fuchsia_sys::LauncherMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{connect_to_service, launch_with_options, LaunchOptions};
use fuchsia_syslog::levels::INFO;
use fuchsia_syslog_listener::{run_log_listener_with_proxy, LogProcessor};
use futures::{channel::mpsc, StreamExt};

#[fasync::run_singlethreaded(test)]
async fn same_log_sink_simultaneously() {
    // launch archivist-for-embedding.cmx
    let launcher = connect_to_service::<LauncherMarker>().unwrap();
    let mut archivist = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cmx"
            .to_owned(),
        Some(vec!["--disable-log-connector".to_owned()]),
        LaunchOptions::new(),
    )
    .unwrap();

    // connect multiple identical log sinks
    for _ in 0..50 {
        let (message_client, message_server) = Socket::create(SocketOpts::DATAGRAM).unwrap();
        let log_sink = archivist.connect_to_service::<LogSinkMarker>().unwrap();
        log_sink.connect(message_server).unwrap();

        // each with the same message repeated multiple times
        let mut packet = fx_log_packet_t::default();
        packet.metadata.pid = 1000;
        packet.metadata.tid = 2000;
        packet.metadata.severity = LogLevelFilter::Info.into_primitive().into();
        packet.data[0] = 0;
        packet.add_data(1, "repeated log".as_bytes());
        for _ in 0..5 {
            message_client.write(&mut packet.as_bytes()).unwrap();
        }
    }

    // run log listener
    let log_proxy = archivist.connect_to_service::<LogMarker>().unwrap();
    let (send_logs, recv_logs) = mpsc::unbounded();
    fasync::Task::spawn(async move {
        let listen = Listener { send_logs };
        let mut options = LogFilterOptions {
            filter_by_pid: true,
            pid: 1000,
            filter_by_tid: true,
            tid: 2000,
            verbosity: 0,
            min_severity: LogLevelFilter::None,
            tags: Vec::new(),
        };
        run_log_listener_with_proxy(&log_proxy, listen, Some(&mut options), false, None)
            .await
            .unwrap();
    })
    .detach();

    // connect to controller and call stop
    let controller = archivist.connect_to_service::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|message| (message.severity, message.msg)).collect::<Vec<_>>().await;

    // recv_logs returned, means archivist must be dead. check.
    assert!(archivist.wait().await.unwrap().success());
    assert_eq!(
        logs,
        std::iter::repeat((INFO, "repeated log".to_owned())).take(250).collect::<Vec<_>>()
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

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::logs::message::fx_log_packet_t;
use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{LogLevelFilter, LogMarker, LogMessage, LogSinkMarker};
use fidl_fuchsia_sys::LauncherMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{connect_to_service, launch_with_options, App, LaunchOptions};
use fuchsia_syslog::levels::INFO;
use fuchsia_syslog_listener::{run_log_listener_with_proxy, LogProcessor};
use fuchsia_zircon as zx;
use futures::{channel::mpsc, StreamExt};

#[fasync::run_singlethreaded(test)]
async fn timestamp_sorting_for_batches() {
    let message_times = [1_000, 5_000, 10_000, 15_000];
    let hare_times = (0, 2);
    let tort_times = (1, 3);
    let packets = message_times
        .iter()
        .map(|t| {
            let mut packet = fx_log_packet_t::default();
            packet.metadata.time = *t;
            packet.metadata.pid = 1000;
            packet.metadata.tid = 2000;
            packet.metadata.severity = LogLevelFilter::Info.into_primitive().into();
            packet.add_data(1, "timing log".as_bytes());
            packet
        })
        .collect::<Vec<_>>();
    let messages = packets
        .iter()
        .map(|p| LogMessage {
            severity: INFO,
            time: p.metadata.time,
            dropped_logs: 0,
            msg: "timing log".to_owned(),
            tags: vec!["UNKNOWN".to_owned()],
            pid: p.metadata.pid,
            tid: p.metadata.tid,
        })
        .collect::<Vec<_>>();

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

    {
        // there are two writers in this test, a "tortoise" and a "hare"
        // the hare's messages are always timestamped earlier but arrive later
        let (send_tort, recv_tort) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let (send_hare, recv_hare) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        // put a message in each socket
        send_tort.write(packets[tort_times.0].as_bytes()).unwrap();
        send_hare.write(packets[hare_times.0].as_bytes()).unwrap();

        // connect to log_sink and make sure we have a clean slate
        let mut early_listener = listen_to_archivist(&archivist);
        let log_sink = archivist.connect_to_service::<LogSinkMarker>().unwrap();

        // connect the tortoise's socket
        log_sink.connect(recv_tort).unwrap();
        let tort_expected = messages[tort_times.0].clone();
        let mut expected_dump = vec![tort_expected.clone()];
        assert_eq!(&early_listener.next().await.unwrap(), &tort_expected);
        assert_eq!(&dump_from_archivist(&archivist).await, &expected_dump);

        // connect hare's socket
        log_sink.connect(recv_hare).unwrap();
        let hare_expected = messages[hare_times.0].clone();
        expected_dump.push(hare_expected.clone());
        expected_dump.sort_by_key(|m| m.time);

        assert_eq!(&early_listener.next().await.unwrap(), &hare_expected);
        assert_eq!(&dump_from_archivist(&archivist).await, &expected_dump);

        // start a new listener and make sure it gets backlog reversed from early listener
        let mut middle_listener = listen_to_archivist(&archivist);
        assert_eq!(&middle_listener.next().await.unwrap(), &hare_expected);
        assert_eq!(&middle_listener.next().await.unwrap(), &tort_expected);

        // send the second tortoise message and assert it's seen
        send_tort.write(packets[tort_times.1].as_bytes()).unwrap();
        let tort_expected2 = messages[tort_times.1].clone();
        expected_dump.push(tort_expected2.clone());
        expected_dump.sort_by_key(|m| m.time);

        assert_eq!(&early_listener.next().await.unwrap(), &tort_expected2);
        assert_eq!(&middle_listener.next().await.unwrap(), &tort_expected2);
        assert_eq!(&dump_from_archivist(&archivist).await, &expected_dump);

        // send the second hare message and assert it's seen
        send_tort.write(packets[hare_times.1].as_bytes()).unwrap();
        let hare_expected2 = messages[hare_times.1].clone();
        expected_dump.push(hare_expected2.clone());
        expected_dump.sort_by_key(|m| m.time);

        assert_eq!(&early_listener.next().await.unwrap(), &hare_expected2);
        assert_eq!(&middle_listener.next().await.unwrap(), &hare_expected2);
        assert_eq!(&dump_from_archivist(&archivist).await, &expected_dump);

        // listening after all messages were seen by archivist-for-embedding.cmx should be time-ordered
        let mut final_listener = listen_to_archivist(&archivist);
        assert_eq!(&final_listener.next().await.unwrap(), &hare_expected);
        assert_eq!(&final_listener.next().await.unwrap(), &tort_expected);
        assert_eq!(&final_listener.next().await.unwrap(), &hare_expected2);
        assert_eq!(&final_listener.next().await.unwrap(), &tort_expected2);
    }

    // connect to controller and call stop
    let controller = archivist.connect_to_service::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    assert!(archivist.wait().await.unwrap().success());
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

async fn dump_from_archivist(archivist: &App) -> Vec<LogMessage> {
    let log_proxy = archivist.connect_to_service::<LogMarker>().unwrap();
    let (send_logs, recv_logs) = mpsc::unbounded();
    fasync::Task::spawn(async move {
        run_log_listener_with_proxy(&log_proxy, send_logs, None, true, None).await.unwrap();
    })
    .detach();
    recv_logs.collect::<Vec<_>>().await
}

fn listen_to_archivist(archivist: &App) -> mpsc::UnboundedReceiver<LogMessage> {
    let log_proxy = archivist.connect_to_service::<LogMarker>().unwrap();
    let (send_logs, recv_logs) = mpsc::unbounded();
    fasync::Task::spawn(async move {
        run_log_listener_with_proxy(&log_proxy, send_logs, None, false, None).await.unwrap();
    })
    .detach();
    recv_logs
}

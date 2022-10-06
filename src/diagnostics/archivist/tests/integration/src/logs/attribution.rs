// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, logs::utils::Listener, test_topology};
use component_events::{
    events::{EventStream, Started},
    matcher::EventMatcher,
};
use diagnostics_hierarchy::assert_data_tree;
use diagnostics_message::fx_log_packet_t;
use diagnostics_reader::{ArchiveReader, Logs, Severity};
use fidl::{Socket, SocketOpts};
use fidl_fuchsia_diagnostics as fdiagnostics;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogSinkMarker};
use fuchsia_async as fasync;
use fuchsia_syslog_listener::run_log_listener_with_proxy;
use futures::{channel::mpsc, StreamExt};

// This test verifies that Archivist knows about logging from this component.
#[fuchsia::test]
async fn log_attribution() {
    let (builder, test_realm) = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_eager_child(&test_realm, "child", STUB_INSPECT_COMPONENT_URL)
        .await
        .expect("add child");

    let instance = builder.build().await.expect("create instance");

    let accessor = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fdiagnostics::ArchiveAccessorMarker>()
        .unwrap();
    let mut result = ArchiveReader::new()
        .with_archive(accessor)
        .snapshot_then_subscribe::<Logs>()
        .expect("snapshot then subscribe");

    for log_str in &["This is a syslog message", "This is another syslog message"] {
        let log_record = result.next().await.expect("received log").expect("log is not an error");

        assert_eq!(
            log_record.moniker,
            format!("realm_builder:{}/test/child", instance.root.child_name())
        );
        assert_eq!(log_record.metadata.component_url, Some(STUB_INSPECT_COMPONENT_URL.to_string()));
        assert_eq!(log_record.metadata.severity, Severity::Info);
        assert_data_tree!(log_record.payload.unwrap(), root: contains {
            message: {
              value: log_str.to_string(),
            }
        });
    }
}

#[fuchsia::test]
async fn archivist_without_attribution_ingests_logs_but_doesnt_attribute_monikers() {
    let (builder, test_realm) = test_topology::create(test_topology::Options {
        archivist_url: ARCHIVIST_WITHOUT_ATTRIBUTION,
    })
    .await
    .expect("create base topology");
    test_topology::add_eager_child(&test_realm, "child", STUB_INSPECT_COMPONENT_URL)
        .await
        .expect("add child");

    let instance = builder.build().await.expect("create instance");

    let accessor = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fdiagnostics::ArchiveAccessorMarker>()
        .unwrap();
    let mut result = ArchiveReader::new()
        .with_archive(accessor)
        .snapshot_then_subscribe::<Logs>()
        .expect("snapshot then subscribe");

    for log_str in &["This is a syslog message", "This is another syslog message"] {
        let log_record = result.next().await.expect("received log").expect("log is not an error");

        assert_eq!(log_record.moniker, "UNKNOWN");
        assert_eq!(log_record.metadata.component_url, Some("fuchsia-pkg://UNKNOWN".into()));
        assert_eq!(log_record.metadata.severity, Severity::Info);
        assert_data_tree!(log_record.payload.unwrap(), root: contains {
            message: {
              value: log_str.to_string(),
            }
        });
    }
}

#[fuchsia::test]
async fn log_unattributed_stream() {
    let (builder, _test_realm) = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");

    // Hook to up to event source before starting realm. This is done to avoid
    // a race condition in which the instance is started before the proper
    // event matcher is ready.
    let mut event_stream = EventStream::open().await.unwrap();

    let instance = builder.build().await.expect("create instance");

    // Bind to Log to start archivist.
    let log_proxy = instance.root.connect_to_protocol_at_exposed_dir::<LogMarker>().unwrap();

    // Ensure that Archivist has started before continuing with tests.
    let _ = EventMatcher::ok()
        .moniker_regex("archivist$")
        .wait::<Started>(&mut event_stream)
        .await
        .unwrap();

    // connect multiple identical log sinks
    for _ in 0..50 {
        let (message_client, message_server) = Socket::create(SocketOpts::DATAGRAM).unwrap();
        let log_sink = instance.root.connect_to_protocol_at_exposed_dir::<LogSinkMarker>().unwrap();
        log_sink.connect(message_server).unwrap();

        // each with the same message repeated multiple times
        let mut packet = fx_log_packet_t::default();
        packet.metadata.pid = 1000;
        packet.metadata.tid = 2000;
        packet.metadata.severity = LogLevelFilter::Info.into_primitive().into();
        packet.data[0] = 0;
        packet.add_data(1, "repeated log".as_bytes());
        for _ in 0..5 {
            message_client.write(packet.as_bytes()).unwrap();
        }
    }

    // run log listener
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

    // collect all logs
    let logs = recv_logs
        .map(|message| (message.severity, message.msg))
        .take(250)
        .collect::<Vec<_>>()
        .await;

    assert_eq!(
        logs,
        std::iter::repeat((
            fdiagnostics::Severity::Info.into_primitive() as i32,
            "repeated log".to_owned()
        ))
        .take(250)
        .collect::<Vec<_>>()
    );
}

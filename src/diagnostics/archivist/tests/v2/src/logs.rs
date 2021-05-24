// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use component_events::{events::*, matcher::*};

use archivist_lib::logs::{
    message::{fx_log_metadata_t, fx_log_packet_t},
    redact::{REDACTED_CANARY_MESSAGE, UNREDACTED_CANARY_MESSAGE},
};
use cm_rust::{ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget};
use diagnostics_reader::{assert_data_tree, ArchiveReader, Data, Logs};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io::DirectoryMarker;
use fidl_fuchsia_logger::{LogLevelFilter, LogMarker, LogMessage, LogSinkMarker, LogSinkProxy};
use fidl_fuchsia_sys2::{ChildRef, EventSourceMarker, RealmMarker};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_component_test::{Moniker, RealmInstance};
use fuchsia_syslog::levels::INFO;
use fuchsia_syslog_listener::{run_log_listener_with_proxy, LogProcessor};
use fuchsia_zircon as zx;
use futures::{channel::mpsc, StreamExt};
use tracing::debug;

#[fuchsia::test]
async fn timestamp_sorting_for_batches() {
    // launch archivist
    let builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");

    let instance = builder.build().create().await.expect("create instance");

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
            tags: vec![format!("fuchsia_component_test_collection:{}", instance.root.child_name())],
            pid: p.metadata.pid,
            tid: p.metadata.tid,
        })
        .collect::<Vec<_>>();

    {
        // there are two writers in this test, a "tortoise" and a "hare"
        // the hare's messages are always timestamped earlier but arrive later
        let (send_tort, recv_tort) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let (send_hare, recv_hare) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        // put a message in each socket
        send_tort.write(packets[tort_times.0].as_bytes()).unwrap();
        send_hare.write(packets[hare_times.0].as_bytes()).unwrap();

        // connect to log_sink and make sure we have a clean slate
        let mut early_listener = listen_to_archivist(&instance);
        let log_sink = instance.root.connect_to_protocol_at_exposed_dir::<LogSinkMarker>().unwrap();

        // connect the tortoise's socket
        log_sink.connect(recv_tort).unwrap();
        let tort_expected = messages[tort_times.0].clone();
        let mut expected_dump = vec![tort_expected.clone()];
        assert_eq!(&early_listener.next().await.unwrap(), &tort_expected);
        assert_eq!(&dump_from_archivist(&instance).await, &expected_dump);

        // connect hare's socket
        log_sink.connect(recv_hare).unwrap();
        let hare_expected = messages[hare_times.0].clone();
        expected_dump.push(hare_expected.clone());
        expected_dump.sort_by_key(|m| m.time);

        assert_eq!(&early_listener.next().await.unwrap(), &hare_expected);
        assert_eq!(&dump_from_archivist(&instance).await, &expected_dump);

        // start a new listener and make sure it gets backlog reversed from early listener
        let mut middle_listener = listen_to_archivist(&instance);
        assert_eq!(&middle_listener.next().await.unwrap(), &hare_expected);
        assert_eq!(&middle_listener.next().await.unwrap(), &tort_expected);

        // send the second tortoise message and assert it's seen
        send_tort.write(packets[tort_times.1].as_bytes()).unwrap();
        let tort_expected2 = messages[tort_times.1].clone();
        expected_dump.push(tort_expected2.clone());
        expected_dump.sort_by_key(|m| m.time);

        assert_eq!(&early_listener.next().await.unwrap(), &tort_expected2);
        assert_eq!(&middle_listener.next().await.unwrap(), &tort_expected2);
        assert_eq!(&dump_from_archivist(&instance).await, &expected_dump);

        // send the second hare message and assert it's seen
        send_tort.write(packets[hare_times.1].as_bytes()).unwrap();
        let hare_expected2 = messages[hare_times.1].clone();
        expected_dump.push(hare_expected2.clone());
        expected_dump.sort_by_key(|m| m.time);

        assert_eq!(&early_listener.next().await.unwrap(), &hare_expected2);
        assert_eq!(&middle_listener.next().await.unwrap(), &hare_expected2);
        assert_eq!(&dump_from_archivist(&instance).await, &expected_dump);

        // listening after all messages were seen by archivist-for-embedding.cmx should be time-ordered
        let mut final_listener = listen_to_archivist(&instance);
        assert_eq!(&final_listener.next().await.unwrap(), &hare_expected);
        assert_eq!(&final_listener.next().await.unwrap(), &tort_expected);
        assert_eq!(&final_listener.next().await.unwrap(), &hare_expected2);
        assert_eq!(&final_listener.next().await.unwrap(), &tort_expected2);
    }
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

async fn dump_from_archivist(instance: &RealmInstance) -> Vec<LogMessage> {
    let log_proxy = instance.root.connect_to_protocol_at_exposed_dir::<LogMarker>().unwrap();
    let (send_logs, recv_logs) = mpsc::unbounded();
    fasync::Task::spawn(async move {
        run_log_listener_with_proxy(&log_proxy, send_logs, None, true, None).await.unwrap();
    })
    .detach();
    recv_logs.collect::<Vec<_>>().await
}

fn listen_to_archivist(instance: &RealmInstance) -> mpsc::UnboundedReceiver<LogMessage> {
    let log_proxy = instance.root.connect_to_protocol_at_exposed_dir::<LogMarker>().unwrap();
    let (send_logs, recv_logs) = mpsc::unbounded();
    fasync::Task::spawn(async move {
        run_log_listener_with_proxy(&log_proxy, send_logs, None, false, None).await.unwrap();
    })
    .detach();
    recv_logs
}

#[fuchsia::test]
async fn canary_is_redacted_with_filtering() {
    let test = RedactionTest::new(ARCHIVIST_WITH_FEEDBACK_FILTERING).await;
    let redacted = test.get_feedback_canary().await;
    assert_eq!(redacted.msg().unwrap().trim_end(), REDACTED_CANARY_MESSAGE);
}

#[fuchsia::test]
async fn canary_is_unredacted_without_filtering() {
    let test = RedactionTest::new(ARCHIVIST_WITH_FEEDBACK_FILTERING_DISABLED).await;
    let redacted = test.get_feedback_canary().await;
    assert_eq!(redacted.msg().unwrap().trim_end(), UNREDACTED_CANARY_MESSAGE);
}

struct RedactionTest {
    _instance: RealmInstance,
    _log_sink: LogSinkProxy,
    all_reader: ArchiveReader,
    feedback_reader: ArchiveReader,
}

impl RedactionTest {
    async fn new(archivist_url: &'static str) -> Self {
        let builder = test_topology::create(test_topology::Options { archivist_url })
            .await
            .expect("create base topology");

        let instance = builder.build().create().await.expect("create instance");
        let mut packet = fx_log_packet_t {
            metadata: fx_log_metadata_t {
                time: 3000,
                pid: 1000,
                tid: 2000,
                severity: LogLevelFilter::Info.into_primitive().into(),
                ..fx_log_metadata_t::default()
            },
            ..fx_log_packet_t::default()
        };
        packet.add_data(1, UNREDACTED_CANARY_MESSAGE.as_bytes());
        let (snd, rcv) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        snd.write(packet.as_bytes()).unwrap();
        let log_sink = instance.root.connect_to_protocol_at_exposed_dir::<LogSinkMarker>().unwrap();
        log_sink.connect(rcv).unwrap();

        let all_reader = ArchiveReader::new().with_archive(
            instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap(),
        );
        let feedback_reader = ArchiveReader::new().with_archive(
            instance
                .root
                .connect_to_named_protocol_at_exposed_dir::<ArchiveAccessorMarker>(
                    "fuchsia.diagnostics.FeedbackArchiveAccessor",
                )
                .unwrap(),
        );

        Self { _instance: instance, _log_sink: log_sink, all_reader, feedback_reader }
    }

    async fn get_feedback_canary(&self) -> Data<Logs> {
        debug!("retrieving logs from feedback accessor");
        let feedback_logs = self.feedback_reader.snapshot::<Logs>().await.unwrap();
        let all_logs = self.all_reader.snapshot::<Logs>().await.unwrap();

        let (unredacted, redacted) = all_logs
            .into_iter()
            .zip(feedback_logs)
            .find(|(u, _)| u.msg().unwrap().contains(UNREDACTED_CANARY_MESSAGE))
            .unwrap();
        debug!(unredacted = %unredacted.msg().unwrap());
        redacted
    }
}

#[fuchsia::test]
async fn test_logs_lifecycle() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "log_and_exit", LOG_AND_EXIT_COMPONENT_URL)
        .await
        .expect("add log_and_exit");

    // Currently RealmBuilder doesn't support to expose a capability from framework, therefore we
    // manually update the decl that the builder creates.
    let mut realm = builder.build();
    realm.get_decl_mut(&"test".into()).unwrap().exposes.push(ExposeDecl::Protocol(
        ExposeProtocolDecl {
            source: ExposeSource::Framework,
            source_name: "fuchsia.sys2.Realm".into(),
            target: ExposeTarget::Parent,
            target_name: "fuchsia.sys2.Realm".into(),
        },
    ));
    realm.get_decl_mut(&Moniker::root()).unwrap().exposes.push(ExposeDecl::Protocol(
        cm_rust::ExposeProtocolDecl {
            source: ExposeSource::Child("test".to_string()),
            source_name: "fuchsia.sys2.Realm".into(),
            target: ExposeTarget::Parent,
            target_name: "fuchsia.sys2.Realm".into(),
        },
    ));

    let instance = realm.create().await.expect("create instance");
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();

    let reader = ArchiveReader::new()
        .with_archive(accessor)
        .with_minimum_schema_count(0) // we want this to return even when no log messages
        .retry_if_empty(false);

    let (mut subscription, mut errors) =
        reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _log_errors = fasync::Task::spawn(async move {
        if let Some(error) = errors.next().await {
            panic!("{:#?}", error);
        }
    });

    let moniker = format!(
        "fuchsia_component_test_collection:{}/test/log_and_exit",
        instance.root.child_name()
    );

    let event_source =
        EventSource::from_proxy(client::connect_to_protocol::<EventSourceMarker>().unwrap());

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    let mut child_ref = ChildRef { name: "log_and_exit".to_string(), collection: None };
    for i in 1..100 {
        // launch our child and wait for it to exit before asserting on its logs
        let (_client_end, server_end) =
            fidl::endpoints::create_endpoints::<DirectoryMarker>().unwrap();
        let realm = instance.root.connect_to_protocol_at_exposed_dir::<RealmMarker>().unwrap();
        realm.bind_child(&mut child_ref, server_end).await.unwrap().unwrap();

        EventMatcher::ok()
            .stop(Some(ExitStatusMatcher::Clean))
            .moniker(format!(
                "./fuchsia_component_test_collection:{}:\\d+/test:\\d+/log_and_exit:\\d+",
                instance.root.child_name()
            ))
            .wait::<Stopped>(&mut event_stream)
            .await
            .unwrap();

        check_message(&moniker, subscription.next().await.unwrap());

        let all_messages = reader.snapshot::<Logs>().await.unwrap();
        assert_eq!(all_messages.len(), i, "must have 1 message per launch");

        for message in all_messages {
            check_message(&moniker, message);
        }
    }
}

fn check_message(expected_moniker: &str, message: Data<Logs>) {
    assert_eq!(message.moniker, expected_moniker,);
    assert_eq!(message.metadata.component_url, LOG_AND_EXIT_COMPONENT_URL);

    assert_data_tree!(message.payload.unwrap(), root: {
        message: {
            value: "Hello, world!",
        }
    });
}

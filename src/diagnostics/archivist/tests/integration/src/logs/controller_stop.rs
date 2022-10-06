// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use crate::{constants, test_topology, utils};
use component_events::events::*;
use component_events::matcher::*;
use diagnostics_data::Severity;
use diagnostics_reader::{ArchiveReader, Data, Logs};
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fdecl;
use fidl_fuchsia_diagnostics as fdiagnostics;
use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_component_test::RealmInstance;
use fuchsia_syslog_listener::{run_log_listener_with_proxy, LogProcessor};
use futures::{channel::mpsc, StreamExt};

const LOGGING_COMPONENT: &str = "logging_component";

#[fuchsia::test]
async fn embedding_stop_api_for_log_listener() {
    let instance = initialize_topology().await;

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

    let log_proxy = instance.root.connect_to_protocol_at_exposed_dir::<LogMarker>().unwrap();
    fasync::Task::spawn(async move {
        let l = Listener { send_logs };
        run_log_listener_with_proxy(&log_proxy, l, Some(&mut options), false, None).await.unwrap();
    })
    .detach();

    let mut event_stream = EventStream::open().await.unwrap();

    run_logging_component(&instance, &mut event_stream).await;

    // connect to controller and call stop
    let controller =
        instance.root.connect_to_protocol_at_exposed_dir::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|l| (l.severity as i8, l.msg)).collect::<Vec<_>>().await;

    utils::wait_for_component_stopped_event(
        instance.root.child_name(),
        "archivist",
        ExitStatusMatcher::Clean,
        &mut event_stream,
    )
    .await;

    assert_eq!(
        logs,
        vec![
            (
                fdiagnostics::Severity::Debug.into_primitive() as i8,
                "Logging initialized".to_owned()
            ),
            (fdiagnostics::Severity::Debug.into_primitive() as i8, "my debug message.".to_owned()),
            (fdiagnostics::Severity::Info.into_primitive() as i8, "my info message.".to_owned()),
            (fdiagnostics::Severity::Warn.into_primitive() as i8, "my warn message.".to_owned()),
        ]
    );
}

#[fuchsia::test]
async fn embedding_stop_api_works_for_batch_iterator() {
    let instance = initialize_topology().await;
    let accessor = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fdiagnostics::ArchiveAccessorMarker>()
        .expect("cannot connect to accessor proxy");
    let subscription =
        ArchiveReader::new().with_archive(accessor).snapshot_then_subscribe().expect("subscribed");

    let mut event_stream = EventStream::open().await.unwrap();

    run_logging_component(&instance, &mut event_stream).await;

    // connect to controller and call stop
    let controller =
        instance.root.connect_to_protocol_at_exposed_dir::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = subscription
        .map(|result| {
            let data: Data<Logs> = result.expect("got result");
            (data.metadata.severity, data.msg().unwrap().to_owned())
        })
        .collect::<Vec<_>>()
        .await;

    utils::wait_for_component_stopped_event(
        instance.root.child_name(),
        "archivist",
        ExitStatusMatcher::Clean,
        &mut event_stream,
    )
    .await;

    assert_eq!(
        logs,
        vec![
            (Severity::Debug, "Logging initialized".to_owned()),
            (Severity::Debug, "my debug message.".to_owned()),
            (Severity::Info, "my info message.".to_owned()),
            (Severity::Warn, "my warn message.".to_owned()),
        ]
    );
}

async fn initialize_topology() -> RealmInstance {
    let (builder, test_realm) = test_topology::create(test_topology::Options {
        archivist_url: constants::ARCHIVIST_FOR_V1_URL,
    })
    .await
    .unwrap();
    let _test_component = test_topology::add_lazy_child(
        &test_realm,
        LOGGING_COMPONENT,
        constants::LOGGING_COMPONENT_URL,
    )
    .await
    .unwrap();
    test_topology::expose_test_realm_protocol(&builder, &test_realm).await;
    builder.build().await.expect("create instance")
}

async fn run_logging_component(instance: &RealmInstance, event_stream: &mut EventStream) {
    let mut child_ref = fdecl::ChildRef { name: LOGGING_COMPONENT.to_string(), collection: None };

    let (exposed_dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let realm =
        instance.root.connect_to_protocol_at_exposed_dir::<fcomponent::RealmMarker>().unwrap();
    realm.open_exposed_dir(&mut child_ref, server_end).await.unwrap().unwrap();

    let _ =
        client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir).unwrap();

    utils::wait_for_component_stopped_event(
        instance.root.child_name(),
        LOGGING_COMPONENT,
        ExitStatusMatcher::Clean,
        event_stream,
    )
    .await;
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

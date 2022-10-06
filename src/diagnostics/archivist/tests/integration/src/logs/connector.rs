// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use crate::{constants, logs::utils::Listener, test_topology, utils};
use component_events::{events::*, matcher::*};
use diagnostics_message::fx_log_packet_t;
use fidl::{
    endpoints::{ClientEnd, DiscoverableProtocolMarker, ServerEnd},
    Socket, SocketOpts,
};
use fidl_fuchsia_diagnostics as fdiagnostics;
use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogSinkMarker};
use fidl_fuchsia_sys_internal::{
    LogConnection, LogConnectionListenerMarker, LogConnectorMarker, LogConnectorRequest,
    LogConnectorRequestStream, SourceIdentity,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{Capability, ChildOptions, LocalComponentHandles, Ref, Route};
use fuchsia_syslog_listener::run_log_listener_with_proxy;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, SinkExt, StreamExt, TryStreamExt};

// TODO(fxbug.dev/104555): re-enable when fixing the flake.
#[ignore]
#[fuchsia::test]
async fn same_log_sink_simultaneously_via_connector() {
    let (take_log_listener_response_snd, mut take_log_listener_response_rcv) =
        futures::channel::mpsc::channel(1);
    let (builder, test_realm) = test_topology::create(test_topology::Options {
        archivist_url: constants::ARCHIVIST_FOR_V1_URL,
    })
    .await
    .expect("create base topology");
    let mocks_server = builder
        .add_local_child(
            "mocks-server",
            move |handles| Box::pin(serve_mocks(handles, take_log_listener_response_snd.clone())),
            ChildOptions::new(),
        )
        .await
        .unwrap();

    let incomplete_route =
        Route::new().capability(Capability::protocol_by_name(LogConnectorMarker::PROTOCOL_NAME));
    builder.add_route(incomplete_route.clone().from(&mocks_server).to(&test_realm)).await.unwrap();
    test_realm
        .add_route(incomplete_route.from(Ref::parent()).to(Ref::child("archivist")))
        .await
        .unwrap();

    let instance = builder.build().await.expect("create instance");

    take_log_listener_response_rcv.next().await.unwrap();

    // run log listener
    let log_proxy = instance.root.connect_to_protocol_at_exposed_dir::<LogMarker>().unwrap();
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

    let mut event_stream = EventStream::open().await.unwrap();

    let controller =
        instance.root.connect_to_protocol_at_exposed_dir::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|message| (message.severity, message.msg)).collect::<Vec<_>>().await;

    // recv_logs returned, means archivist_for_test must be dead. check.
    utils::wait_for_component_stopped_event(
        instance.root.child_name(),
        "archivist",
        ExitStatusMatcher::Clean,
        &mut event_stream,
    )
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

async fn serve_mocks(
    handles: LocalComponentHandles,
    take_log_listener_response_snd: mpsc::Sender<()>,
) -> Result<(), anyhow::Error> {
    let (client, server) = zx::Channel::create().unwrap();
    let mut server_end = Some(ServerEnd::new(server));

    // connect multiple identical log sinks
    let mut sockets = Vec::new();
    for _ in 0..50 {
        let (message_client, message_server) = Socket::create(SocketOpts::DATAGRAM).unwrap();
        sockets.push(message_server);

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
    {
        let listener = ClientEnd::<LogConnectionListenerMarker>::new(client).into_proxy().unwrap();
        for socket in sockets {
            let (client, server) = zx::Channel::create().unwrap();
            let log_request = ServerEnd::<LogSinkMarker>::new(server);
            let source_identity = SourceIdentity {
                realm_path: Some(vec![]),
                component_name: Some("testing123".to_string()),
                instance_id: Some("0".to_string()),
                component_url: Some(
                    "fuchsia-pkg://fuchsia.com/test-logs-connector#meta/test-logs-connector.cm"
                        .to_string(),
                ),
                ..SourceIdentity::EMPTY
            };
            listener
                .on_new_connection(&mut LogConnection { log_request, source_identity })
                .unwrap();
            let log_sink = ClientEnd::<LogSinkMarker>::new(client).into_proxy().unwrap();
            log_sink.connect(socket).unwrap();
        }
    }
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: LogConnectorRequestStream| {
        let mut server_end = server_end.take();
        let mut sender_mut = Some(take_log_listener_response_snd.clone());
        fasync::Task::spawn(async move {
            while let Some(LogConnectorRequest::TakeLogConnectionListener { responder }) =
                stream.try_next().await.unwrap()
            {
                responder.send(server_end.take()).unwrap();
                if let Some(mut sender) = sender_mut.take() {
                    sender.send(()).await.unwrap();
                }
            }
        })
        .detach()
    });
    fs.serve_connection(handles.outgoing_dir).unwrap();
    fs.collect::<()>().await;
    Ok(())
}

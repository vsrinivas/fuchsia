// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

mod id;
mod interrupter;
mod proxies;
mod services;
#[cfg(test)]
mod test;

use self::services::{
    active_session::ActiveSession,
    discovery::{filter::Filter, Discovery},
    publisher::Publisher,
};
use anyhow::{Context, Error};
use fidl::endpoints::{create_endpoints, create_request_stream};
use fidl_fuchsia_media::UsageReporterMarker;
use fidl_fuchsia_media_sessions2::*;
use fuchsia_async as fasync;
use fuchsia_component::{client, server::ServiceFs};
use fuchsia_inspect::component;
use fuchsia_syslog::fx_log_warn;
use futures::{channel::mpsc, prelude::*};
use std::sync::Arc;

type Result<T> = std::result::Result<T, Error>;
type SessionId = u64;

const CHANNEL_BUFFER_SIZE: usize = 100;

/// This number should be forgiving. If we lower it we may want to build some
/// in-process staging area for changes so we can send them to clients that ACK
/// late. At 20 though, clients that don't ACK can't reasonably expect to be
/// accomodated.
const MAX_EVENTS_SENT_WITHOUT_ACK: usize = 20;

fn spawn_log_error(fut: impl Future<Output = Result<()>> + 'static) {
    fasync::Task::local(fut.unwrap_or_else(|e| fx_log_warn!("{}", e))).detach()
}

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["mediasession"]).expect("Initializing syslogger");
    fuchsia_syslog::fx_log_info!("Initializing Fuchsia Media Session Service");

    let mut server = ServiceFs::new_local();
    let inspector = component::inspector();
    let player_list = Arc::new(inspector.root().create_child("players"));

    let (player_sink, player_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
    let (discovery_request_sink, discovery_request_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
    let (observer_request_sink, observer_request_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);

    let usage_reporter_proxy =
        client::connect_to_service::<UsageReporterMarker>().expect("Connecting to UsageReporter");
    let discovery = Discovery::new(player_stream, usage_reporter_proxy);
    let sessions_info_stream = discovery.sessions_info_stream(Filter::default());
    spawn_log_error(discovery.serve(discovery_request_stream, observer_request_stream));

    let internal_discovery_request_sink = discovery_request_sink.clone();
    let connect_to_session = move |session_id| {
        let (discovery, request_stream) = create_request_stream::<DiscoveryMarker>()?;
        let discovery = discovery.into_proxy()?;
        let (session, session_request) = create_endpoints()?;
        discovery.connect_to_session(session_id, session_request)?;

        let discovery_request_sink = internal_discovery_request_sink.clone().sink_err_into();
        spawn_log_error(request_stream.err_into().forward(discovery_request_sink));

        Ok(session)
    };

    let (active_session_service, active_session_client_sink) =
        ActiveSession::new(sessions_info_stream, connect_to_session)
            .expect("Creating active session service");
    spawn_log_error(active_session_service.serve());

    server
        .dir("svc")
        .add_fidl_service(move |request_stream| {
            spawn_log_error(
                Publisher::new(player_sink.clone(), player_list.clone()).serve(request_stream),
            )
        })
        .add_fidl_service(move |request_stream: DiscoveryRequestStream| {
            let discovery_request_sink = discovery_request_sink.clone().sink_err_into();
            spawn_log_error(request_stream.err_into().forward(discovery_request_sink));
        })
        .add_fidl_service(move |request_stream: ObserverDiscoveryRequestStream| {
            let observer_request_sink = observer_request_sink.clone().sink_err_into();
            spawn_log_error(request_stream.err_into().forward(observer_request_sink));
        })
        .add_fidl_service(move |request_stream: ActiveSessionRequestStream| {
            let mut active_session_client_sink = active_session_client_sink.clone();
            spawn_log_error(async move {
                Ok(active_session_client_sink
                    .send(request_stream)
                    .await
                    .context("Sending new client to Active Session service")?)
            });
        });
    inspector.serve(&mut server).expect("Serving inspect");

    server.take_and_serve_directory_handle().expect("To serve Media Session services");
    server.collect::<()>().await;
}

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

use anyhow::Error;
use fidl_fuchsia_media::UsageReporterMarker;
use fuchsia_async as fasync;
use fuchsia_component::{client, server::ServiceFs};
use fuchsia_inspect::component;
use fuchsia_syslog::fx_log_warn;
use futures::{channel::mpsc, prelude::*};

type Result<T> = std::result::Result<T, Error>;

const CHANNEL_BUFFER_SIZE: usize = 100;

/// This number should be forgiving. If we lower it we may want to build some
/// in-process staging area for changes so we can send them to clients that ACK
/// late. At 20 though, clients that don't ACK can't reasonably expect to be
/// accomodated.
const MAX_EVENTS_SENT_WITHOUT_ACK: usize = 20;

fn spawn_log_error(fut: impl Future<Output = Result<()>> + 'static) {
    fasync::spawn_local(fut.unwrap_or_else(|e| fx_log_warn!("{}", e)))
}

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["mediasession"]).expect("Initializing syslogger");
    fuchsia_syslog::fx_log_info!("Initializing Fuchsia Media Session Service");

    let mut server = ServiceFs::new_local();
    let inspector = component::inspector();
    let root = inspector.root();

    let (player_sink, player_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
    let (discovery_request_sink, discovery_request_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
    let (observer_request_sink, observer_request_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);

    let usage_reporter_proxy =
        client::connect_to_service::<UsageReporterMarker>().expect("Connecting to UsageReporter");
    let discovery = self::services::discovery::Discovery::new(player_stream, usage_reporter_proxy);
    spawn_log_error(discovery.serve(discovery_request_stream, observer_request_stream));

    server
        .dir("svc")
        .add_fidl_service(move |request_stream| {
            spawn_log_error(
                self::services::publisher::Publisher::new(player_sink.clone(), root)
                    .serve(request_stream),
            )
        })
        .add_fidl_service(
            move |request_stream: fidl_fuchsia_media_sessions2::DiscoveryRequestStream| {
                let discovery_request_sink = discovery_request_sink.clone().sink_err_into();
                spawn_log_error(request_stream.err_into().forward(discovery_request_sink));
            },
        )
        .add_fidl_service(
            move |request_stream: fidl_fuchsia_media_sessions2::ObserverDiscoveryRequestStream| {
                let observer_request_sink = observer_request_sink.clone().sink_err_into();
                spawn_log_error(request_stream.err_into().forward(observer_request_sink));
            },
        );
    inspector.serve(&mut server).expect("Serving inspect");

    server.take_and_serve_directory_handle().expect("To serve Media Session services");
    server.collect::<()>().await;
}

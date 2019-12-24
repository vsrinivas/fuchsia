// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

mod proxies;
mod services;
#[cfg(test)]
mod test;

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::{channel::mpsc, prelude::*};

type Result<T> = std::result::Result<T, Error>;

const CHANNEL_BUFFER_SIZE: usize = 100;

/// This number should be forgiving. If we lower it we may want to build some
/// in-process staging area for changes so we can send them to clients that ACK
/// late. At 20 though, clients that don't ACK can't reasonably expect to be
/// accomodated.
const MAX_EVENTS_SENT_WITHOUT_ACK: usize = 20;

fn spawn_log_error(fut: impl Future<Output = Result<()>> + 'static) {
    fasync::spawn_local(fut.unwrap_or_else(|e| eprintln!("{}", e)))
}

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["mediasession"]).expect("Initializing syslogger");
    fuchsia_syslog::fx_log_info!("Initializing Fuchsia Media Session Service");

    let (player_sink, player_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
    let (request_sink, request_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
    let discovery = self::services::discovery::Discovery::new(player_stream);
    spawn_log_error(discovery.serve(request_stream));

    let mut server = ServiceFs::new_local();
    server
        .dir("svc")
        .add_fidl_service(|request_stream| {
            spawn_log_error(
                self::services::publisher::Publisher::new(player_sink.clone())
                    .serve(request_stream),
            )
        })
        .add_fidl_service(
            |request_stream: fidl_fuchsia_media_sessions2::DiscoveryRequestStream| {
                let request_sink = request_sink.clone().sink_map_err(Into::into);
                spawn_log_error(request_stream.map_err(Into::into).forward(request_sink));
            },
        );
    server.take_and_serve_directory_handle().expect("To serve Media Session services");

    server.collect::<()>().await;
}

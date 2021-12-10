// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use futures::{FutureExt as _, TryStreamExt as _};
use hyper::service::{make_service_fn, service_fn};
use parking_lot::RwLock;
use std::convert::Infallible;
use std::net::{IpAddr, Ipv6Addr, SocketAddr};
use std::sync::Arc;

use sl4f_lib::server::sl4f::{serve, Sl4f, Sl4fClients};
use sl4f_lib::server::sl4f_executor::run_fidl_loop;

// Config, flexible for any ip/port combination
const SERVER_PORT: u16 = 80;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["sl4f"]).expect("Can't init logger");
    log::info!("Starting sl4f server");

    // State for clients that utilize the /init endpoint
    let sl4f_clients = Arc::new(RwLock::new(Sl4fClients::new()));

    // State for facades
    let sl4f = Sl4f::new(Arc::clone(&sl4f_clients)).expect("failed to create SL4F");
    let sl4f = Arc::new(sl4f);

    let addr = SocketAddr::new(IpAddr::V6(Ipv6Addr::UNSPECIFIED), SERVER_PORT);
    log::info!("Now listening on: {:?}", addr);
    let listener = fasync::net::TcpListener::bind(&addr).expect("bind");
    let listener = listener
        .accept_stream()
        .map_ok(|(stream, _): (_, SocketAddr)| fuchsia_hyper::TcpStream { stream });

    // Create channel for communication between http server and FIDL. This once bridged a sync/async
    // gap, but no longer does. It would be good to refactor this away.
    let (sender, async_receiver) = async_channel::unbounded();

    let make_svc = make_service_fn(move |_: &fuchsia_hyper::TcpStream| {
        let sender = sender.clone();
        let sl4f_clients = Arc::clone(&sl4f_clients);
        futures::future::ok::<_, Infallible>(service_fn(move |request| {
            serve(request, Arc::clone(&sl4f_clients), sender.clone()).map(Ok::<_, Infallible>)
        }))
    });

    let server = hyper::Server::builder(hyper::server::accept::from_stream(listener))
        .executor(fuchsia_hyper::Executor)
        .serve(make_svc);

    futures::select! {
        res = server.fuse() => panic!("HTTP server died: {:?}", res),
        () = run_fidl_loop(sl4f, async_receiver).fuse() => panic!("FIDL handler died")
    }
}

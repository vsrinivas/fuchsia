// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fuchsia_async as fasync;
use hyper::{Body, Request, Response};
use serde::{Deserialize, Serialize};
use std::convert::Infallible;
use std::future::Future;
use std::net::{IpAddr, SocketAddr};

const SERVER_PORT: u16 = 8880;

pub enum SetupEvent {
    Root,
    DevhostOta { cfg: DevhostConfig },
}

/// Devhost configuration, passed to the actual OTA process.
pub struct DevhostConfig {
    pub url: String,
}

#[derive(Deserialize, Serialize)]
/// Configuration provided by the host for the devhost OTA. Only used for de/serialization.
struct DevhostRequestInfo {
    /// We assume that the OTA server is running on the requester's address
    /// at the given port.
    pub port: u16,
}

async fn parse_ota_json(
    request: Request<Body>,
    remote_addr: IpAddr,
) -> Result<DevhostConfig, Error> {
    use bytes::Buf as _;

    let body = hyper::body::aggregate(request.into_body()).await.context("read request")?;
    let DevhostRequestInfo { port } =
        serde_json::from_reader(body.reader()).context("Failed to parse JSON")?;

    let url = format!("http://{}/config.json", SocketAddr::new(remote_addr, port));
    Ok(DevhostConfig { url })
}

async fn serve<Fut, F>(
    request: Request<Body>,
    remote_addr: SocketAddr,
    handler: F,
) -> Response<Body>
where
    Fut: Future<Output = ()>,
    F: FnOnce(SetupEvent) -> Fut,
{
    use hyper::{Method, StatusCode};

    match (request.method(), request.uri().path()) {
        (&Method::GET, "/") => {
            let () = handler(SetupEvent::Root).await;
            Response::new("Root document".into())
        }
        (&Method::POST, "/ota/devhost") => {
            // get devhost info out of POST request.
            match parse_ota_json(request, remote_addr.ip()).await {
                Err(e) => {
                    let mut response = Response::new(format!("Bad request: {:?}", e).into());
                    *response.status_mut() = StatusCode::BAD_REQUEST;
                    response
                }
                Ok(cfg) => {
                    let () = handler(SetupEvent::DevhostOta { cfg }).await;
                    Response::new("Started OTA".into())
                }
            }
        }
        _ => {
            let mut response = Response::new("Unknown command".into());
            *response.status_mut() = StatusCode::NOT_FOUND;
            response
        }
    }
}

pub fn start_server<Fut, F>(handler: F) -> impl Future<Output = Result<(), hyper::Error>>
where
    Fut: Future<Output = ()>,
    F: FnOnce(SetupEvent) -> Fut,
    Fut: Send + 'static,
    F: Clone + Send + 'static,
{
    use futures::{FutureExt as _, TryStreamExt as _};
    use hyper::service::{make_service_fn, service_fn};

    println!("recovery: start_server");

    let addr = SocketAddr::new(IpAddr::V6(std::net::Ipv6Addr::UNSPECIFIED), SERVER_PORT);
    let listener = fasync::net::TcpListener::bind(&addr).expect("bind");
    let listener = listener
        .accept_stream()
        .map_ok(|(stream, _): (_, SocketAddr)| fuchsia_hyper::TcpStream { stream });

    let make_svc = make_service_fn(move |fuchsia_hyper::TcpStream { stream }| {
        let handler = handler.clone();
        std::future::ready((|| {
            let remote_addr = stream.std().peer_addr().context("peer addr")?;
            Ok::<_, Error>(service_fn(move |request| {
                let handler = handler.clone();
                serve(request, remote_addr, handler).map(Ok::<_, Infallible>)
            }))
        })())
    });

    hyper::Server::builder(hyper::server::accept::from_stream(listener))
        .executor(fuchsia_hyper::Executor)
        .serve(make_svc)
}

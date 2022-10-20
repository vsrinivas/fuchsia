// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::responder::Responder;
use anyhow::{anyhow, Error};
use async_trait::async_trait;
use fuchsia_async as fasync;
use futures::lock::Mutex;
use futures::TryStreamExt;
use hyper::service::{make_service_fn, service_fn};
use mockall::automock;
use std::convert::Infallible;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::Arc;

#[automock]
#[async_trait]
pub trait WebServer {
    async fn run(
        &self,
        port: u16,
        responder1: Arc<futures::lock::Mutex<dyn Responder>>,
    ) -> Result<(), Error>;
}

pub struct WebServerImpl {}

#[async_trait]
impl WebServer for WebServerImpl {
    /// Runs a Hyper webserver and passes requests to responder.handle().
    async fn run(&self, port: u16, responder: Arc<Mutex<dyn Responder>>) -> Result<(), Error> {
        let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), port);
        let listener = fasync::net::TcpListener::bind(&addr)?;
        let listener = listener
            .accept_stream()
            .map_ok(|(stream, _): (_, SocketAddr)| fuchsia_hyper::TcpStream { stream });

        // Nested-cloning explanation at: https://www.fpcomplete.com/blog/captures-closures-async/
        let make_svc = make_service_fn(move |_conn| {
            let responder = responder.clone();
            async move {
                Ok::<_, Infallible>(service_fn(move |req| {
                    let responder = responder.clone();
                    async move {
                        let locked_responder = responder.lock().await;
                        (*locked_responder).handle(req)
                    }
                }))
            }
        });

        let server = hyper::Server::builder(hyper::server::accept::from_stream(listener))
            .executor(fuchsia_hyper::Executor)
            .serve(make_svc);

        server.await.map_err(|e| anyhow!("Hyper Server Shutdown {:?}", e))
    }
}

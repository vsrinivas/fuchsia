// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_fs::file;
use futures::FutureExt;
use futures::TryStreamExt;
use hyper::service::{make_service_fn, service_fn};
use std::convert::Infallible;
use std::net::{Ipv4Addr, SocketAddr};
use tracing;

#[async_trait]
pub trait ServerController: Sized + Sync + Send {
    async fn start(&self, port: u16);
}

pub struct HttpServer {}

#[async_trait]
impl ServerController for HttpServer {
    async fn start(&self, port: u16) {
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), port);
        let listener = fasync::net::TcpListener::bind(&addr).expect("cannot bind to addr");
        let local_addr = listener.local_addr().expect("cannot get local address");
        tracing::info!("Http server local address: {:?}", local_addr);

        let listener = listener
            .accept_stream()
            .map_ok(|(stream, _): (_, SocketAddr)| fuchsia_hyper::TcpStream { stream });
        let make_svc = make_service_fn(move |_: &fuchsia_hyper::TcpStream| {
            std::future::ready((|| {
                Ok::<_, Error>(service_fn(move |request| handle(request).map(Ok::<_, Infallible>)))
            })())
        });

        let server = hyper::Server::builder(hyper::server::accept::from_stream(listener))
            .executor(fuchsia_hyper::Executor)
            .serve(make_svc);
        server.await.unwrap();
    }
}

async fn handle(request: hyper::Request<hyper::Body>) -> hyper::Response<hyper::Body> {
    tracing::info!("http server request received: {:?}", request);
    match request.uri().path() {
        "/png" => {
            let body = stream_file("/pkg/data/fuchsia_logo.png");
            hyper::Response::builder()
                .status(hyper::StatusCode::OK)
                .header("Content-Type", "image/png")
                .body(body)
                .unwrap()
        }
        "/webm" => {
            let body = stream_file("/pkg/data/sample_video.webm");
            hyper::Response::builder()
                .status(hyper::StatusCode::OK)
                .header("Content-Type", "video/webm")
                .body(body)
                .unwrap()
        }
        _ => hyper::Response::builder()
            .status(hyper::StatusCode::NOT_FOUND)
            .body(hyper::Body::from(format!("Request URI not supported: {:?}", request.uri())))
            .unwrap(),
    }
}

fn stream_file(file: &'static str) -> hyper::body::Body {
    let (mut writer, body) = hyper::Body::channel();
    fuchsia_async::Task::spawn(async move {
        let f = file::open_in_namespace(file, fio::OpenFlags::RIGHT_READABLE).expect("cannot open");
        let content_size = f.get_attr().await.unwrap().1.content_size;
        tracing::info!("file content_size: {:?}", content_size);
        loop {
            let bytes = f.read(fio::MAX_BUF).await.unwrap().unwrap();
            if bytes.is_empty() {
                tracing::info!("done sending data");
                break;
            }
            writer.send_data(bytes.into()).await.expect("failed to stream data chunk");
        }
        let _ = f.close().await.expect("failed to close file");
    })
    .detach();
    return body;
}

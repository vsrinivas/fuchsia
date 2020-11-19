// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        future::{Future, FutureExt},
        io::{self, AsyncRead, AsyncWrite},
        task::{Context, Poll},
    },
    hyper::{
        client::{
            connect::{Connected, Connection},
            Client,
        },
        Body,
    },
    std::pin::Pin,
};

#[cfg(not(target_os = "fuchsia"))]
use async_std::net;

#[cfg(target_os = "fuchsia")]
use fuchsia_async::net;

#[cfg(not(target_os = "fuchsia"))]
mod not_fuchsia;
#[cfg(not(target_os = "fuchsia"))]
pub use not_fuchsia::*;

#[cfg(target_os = "fuchsia")]
mod fuchsia;
#[cfg(target_os = "fuchsia")]
pub use fuchsia::*;

/// A Fuchsia-compatible hyper client configured for making HTTP requests.
pub type HttpClient = Client<HyperConnector, Body>;

/// A Fuchsia-compatible hyper client configured for making HTTP and HTTPS requests.
pub type HttpsClient = Client<hyper_rustls::HttpsConnector<HyperConnector>, Body>;

/// A future that yields a hyper-compatible TCP stream.
#[must_use = "futures do nothing unless polled"]
pub struct HyperConnectorFuture {
    // FIXME(https://github.com/rust-lang/rust/issues/63063): We should be able to remove this
    // `Box` once rust allows impl Traits in type aliases.
    fut: Pin<Box<dyn Future<Output = Result<TcpStream, io::Error>> + Send>>,
}

impl Future for HyperConnectorFuture {
    type Output = Result<TcpStream, io::Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.fut.as_mut().poll(cx)
    }
}

pub struct TcpStream {
    pub stream: net::TcpStream,
}

impl tokio::io::AsyncRead for TcpStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.stream).poll_read(cx, buf)
    }

    // TODO: override poll_read_buf and call readv on the underlying stream
}

impl tokio::io::AsyncWrite for TcpStream {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.stream).poll_write(cx, buf)
    }

    fn poll_flush(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_shutdown(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    // TODO: override poll_write_buf and call writev on the underlying stream
}

impl Connection for TcpStream {
    fn connected(&self) -> Connected {
        Connected::new()
    }
}

#[non_exhaustive]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
/// A container of TCP settings to be applied to the sockets created by the hyper client.
pub struct TcpOptions {
    /// This sets TCP_KEEPIDLE and SO_KEEPALIVE.
    pub keepalive_idle: Option<std::time::Duration>,
    /// This sets TCP_KEEPINTVL and SO_KEEPALIVE.
    pub keepalive_interval: Option<std::time::Duration>,
    /// This sets TCP_KEEPCNT and SO_KEEPALIVE.
    pub keepalive_count: Option<i32>,
}

#[derive(Clone)]
pub struct Executor;

impl<F: Future + Send + 'static> hyper::rt::Executor<F> for Executor {
    fn execute(&self, fut: F) {
        fuchsia_async::Task::spawn(fut.map(|_| ())).detach()
    }
}

/// Returns a new Fuchsia-compatible hyper client for making HTTP requests.
pub fn new_client() -> HttpClient {
    Client::builder().executor(Executor).build(HyperConnector::new())
}

pub fn new_https_client_dangerous(
    tls: rustls::ClientConfig,
    tcp_options: TcpOptions,
) -> HttpsClient {
    let https =
        hyper_rustls::HttpsConnector::from((HyperConnector::from_tcp_options(tcp_options), tls));
    Client::builder().executor(Executor).build(https)
}

/// Returns a new Fuchsia-compatible hyper client for making HTTP and HTTPS requests.
pub fn new_https_client_from_tcp_options(tcp_options: TcpOptions) -> HttpsClient {
    let mut tls = rustls::ClientConfig::new();
    configure_cert_store(&mut tls);
    new_https_client_dangerous(tls, tcp_options)
}

/// Returns a new Fuchsia-compatible hyper client for making HTTP and HTTPS requests.
pub fn new_https_client() -> HttpsClient {
    new_https_client_from_tcp_options(std::default::Default::default())
}

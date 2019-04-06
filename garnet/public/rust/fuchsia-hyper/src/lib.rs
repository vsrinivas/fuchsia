// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]

use hyper_rustls;
use rustls;

use {
    fuchsia_async::{
        net::{TcpConnector, TcpStream},
        EHandle,
    },
    futures::{
        compat::Compat,
        future::{Future, FutureExt, TryFutureExt},
        io::{self, AsyncReadExt},
        ready,
        task::{Poll, SpawnExt, Waker},
    },
    hyper::{
        client::{
            connect::{Connect, Connected, Destination},
            Client,
        },
        Body,
    },
    std::net::ToSocketAddrs,
    std::pin::Pin,
};

/// A future that yields a hyper-compatible TCP stream.
pub struct HyperTcpConnector(Result<TcpConnector, Option<io::Error>>);

impl Future for HyperTcpConnector {
    type Output = Result<(Compat<TcpStream>, Connected), io::Error>;

    fn poll(mut self: Pin<&mut Self>, lw: &Waker) -> Poll<Self::Output> {
        let connector = self.0.as_mut().map_err(|x| x.take().unwrap())?;
        let stream = ready!(connector.poll_unpin(lw)?);
        Poll::Ready(Ok((stream.compat(), Connected::new())))
    }
}

/// A Fuchsia-compatible implementation of hyper's `Connect` trait which allows
/// creating a TcpStream to a particular destination.
pub struct HyperConnector;

impl Connect for HyperConnector {
    type Transport = Compat<TcpStream>;
    type Error = io::Error;
    type Future = Compat<HyperTcpConnector>;
    fn connect(&self, dst: Destination) -> Self::Future {
        let res = (|| {
            let host = dst.host();
            let port = match dst.port() {
                Some(port) => port,
                None => {
                    if dst.scheme() == "https" {
                        443
                    } else {
                        80
                    }
                }
            };

            // TODO(cramertj): smarter DNS-- nonblocking, don't just pick first addr
            let addr_opt = (host, port).to_socket_addrs()?.next();
            let addr = addr_opt.ok_or_else(|| {
                io::Error::new(io::ErrorKind::Other, "destination resolved to no address")
            })?;
            TcpStream::connect(addr)
        })();
        HyperTcpConnector(res.map_err(Some)).compat()
    }
}

/// Returns a new Fuchsia-compatible hyper client for making HTTP requests.
pub fn new_client() -> Client<HyperConnector, Body> {
    Client::builder().executor(EHandle::local().compat()).build(HyperConnector)
}

pub fn new_https_client_dangerous(
    tls: rustls::ClientConfig,
) -> Client<hyper_rustls::HttpsConnector<HyperConnector>, Body> {
    let https = hyper_rustls::HttpsConnector::from((HyperConnector, tls));
    Client::builder().executor(EHandle::local().compat()).build(https)
}

pub fn new_https_client() -> Client<hyper_rustls::HttpsConnector<HyperConnector>, Body> {
    let mut tls = rustls::ClientConfig::new();
    tls.root_store.add_server_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS);
    tls.ct_logs = Some(&ct_logs::LOGS);
    new_https_client_dangerous(tls)
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async::Executor;

    #[test]
    fn can_create_client() {
        let _exec = Executor::new().unwrap();
        let _client = new_client();
    }

    #[test]
    fn can_create_https_client() {
        let _exec = Executor::new().unwrap();
        let _client = new_https_client();
    }
}

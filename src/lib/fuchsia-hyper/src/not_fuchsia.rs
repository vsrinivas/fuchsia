// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{HyperConnectorFuture, TcpOptions, TcpStream},
    async_std::net,
    futures::io,
    http::uri::{Scheme, Uri},
    hyper::service::Service,
    rustls::ClientConfig,
    std::{
        net::ToSocketAddrs,
        task::{Context, Poll},
    },
};

pub(crate) fn configure_cert_store(tls: &mut ClientConfig) {
    tls.root_store =
        rustls_native_certs::load_native_certs().expect("could not load platform certs");
}

/// A Async-std-compatible implementation of hyper's `Connect` trait which allows
/// creating a TcpStream to a particular destination.
#[derive(Clone)]
pub struct HyperConnector {
    tcp_options: TcpOptions,
}

impl HyperConnector {
    pub fn new() -> Self {
        Self::from_tcp_options(TcpOptions::default())
    }

    pub fn from_tcp_options(tcp_options: TcpOptions) -> Self {
        Self { tcp_options }
    }
}

impl Service<Uri> for HyperConnector {
    type Response = TcpStream;
    type Error = std::io::Error;
    type Future = HyperConnectorFuture;

    fn poll_ready(&mut self, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn call(&mut self, dst: Uri) -> Self::Future {
        let self_ = self.clone();
        HyperConnectorFuture { fut: Box::pin(async move { self_.call_async(dst).await }) }
    }
}

impl HyperConnector {
    async fn call_async(&self, dst: Uri) -> Result<TcpStream, io::Error> {
        let port = match dst.port() {
            Some(p) => p.as_u16(),
            None => {
                if dst.scheme() == Some(&Scheme::HTTPS) {
                    443
                } else {
                    80
                }
            }
        };

        let host = match dst.host() {
            Some(host) => host,
            _ => return Err(io::Error::new(io::ErrorKind::Other, "missing host in Uri")),
        };

        let addr = (host, port).to_socket_addrs()?.next().ok_or_else(|| {
            io::Error::new(io::ErrorKind::Other, "destination resolved to no address")
        })?;

        let stream = net::TcpStream::connect(addr).await?;
        // TODO: figure out how to apply tcp options
        Ok(TcpStream { stream })
    }
}

////////////////////////////////////////////////////////////////////////////////
///// tests

#[cfg(test)]
mod test {
    use crate::*;
    use anyhow::Result;
    use hyper::body::HttpBody;
    use hyper::StatusCode;
    use std::io::Write;

    async fn fetch_url<W: Write>(url: hyper::Uri, mut buffer: W) -> Result<StatusCode> {
        let client = new_https_client();

        let mut res = client.get(url).await?;
        let status = res.status();

        if status == StatusCode::OK {
            while let Some(next) = res.data().await {
                let chunk = next?;
                buffer.write_all(&chunk)?;
            }
            buffer.flush()?;
        }

        Ok(status)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_download_succeeds() -> Result<()> {
        let output: Vec<u8> = Vec::new();
        let status = fetch_url("https://www.google.com".parse::<hyper::Uri>()?, output).await?;
        assert_eq!(StatusCode::OK, status);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_download_handles_bad_domain() -> Result<()> {
        let output: Vec<u8> = Vec::new();
        let res = fetch_url("https://not-exist.example.test".parse::<hyper::Uri>()?, output).await;
        assert!(res.is_err());
        Ok(())
    }
}

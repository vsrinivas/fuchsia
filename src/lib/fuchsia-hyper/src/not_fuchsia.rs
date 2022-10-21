// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{HyperConnectorFuture, TcpOptions, TcpStream},
    async_net as net,
    futures::io,
    http::uri::{Scheme, Uri},
    hyper::service::Service,
    rustls::ClientConfig,
    std::net::ToSocketAddrs,
    std::task::{Context, Poll},
    tracing::warn,
};

pub(crate) fn configure_cert_store(tls: &mut ClientConfig) {
    tls.root_store = rustls_native_certs::load_native_certs().unwrap_or_else(|(certs, err)| {
        if certs.is_some() {
            warn!("One or more TLS certificates in root store failed to load: {}", err);
        }
        certs.expect(&format!(
            "Unable to load any TLS CA certificates from platform root store: {}",
            err
        ))
    })
}

/// A Async-std-compatible implementation of hyper's `Connect` trait which allows
/// creating a TcpStream to a particular destination.
#[derive(Clone, Debug)]
pub struct HyperConnector {
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
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
    use {
        crate::*,
        anyhow::{Error, Result},
        async_net::{Ipv6Addr, SocketAddr, TcpListener},
        futures::{
            future::BoxFuture, stream::FuturesUnordered, FutureExt, StreamExt, TryFutureExt,
            TryStreamExt,
        },
        hyper::{
            body::HttpBody,
            server::{accept::from_stream, Server},
            service::{make_service_fn, service_fn},
            Body, Response, StatusCode,
        },
        std::{convert::Infallible, io::Write},
    };

    trait AsyncReadWrite: tokio::io::AsyncRead + tokio::io::AsyncWrite + Send {}
    impl<T> AsyncReadWrite for T where T: tokio::io::AsyncRead + tokio::io::AsyncWrite + Send {}

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
        let (listener, addr) = {
            let addr = SocketAddr::new(Ipv6Addr::LOCALHOST.into(), 0);
            let listener = TcpListener::bind(&addr).await.unwrap();
            let local_addr = listener.local_addr().unwrap();
            (listener, local_addr)
        };

        let listener =
            listener.incoming().map_err(Error::from).map_ok(|conn| TcpStream { stream: conn });

        let connections = listener
            .map_ok(|conn| Pin::new(Box::new(conn)) as Pin<Box<dyn AsyncReadWrite>>)
            .boxed();

        let make_svc = make_service_fn(move |_socket| async move {
            Ok::<_, Infallible>(service_fn(move |_req| async move {
                Ok::<_, Infallible>(Response::new(Body::from("Hello")))
            }))
        });

        let (stop, rx_stop) = futures::channel::oneshot::channel();

        let server = async {
            Server::builder(from_stream(connections))
                .executor(Executor)
                .serve(make_svc)
                .with_graceful_shutdown(
                    rx_stop.map(|res| res.unwrap_or_else(|futures::channel::oneshot::Canceled| ())),
                )
                .unwrap_or_else(|e| panic!("error serving repo over http: {}", e))
                .await;
            Ok(())
        };

        let client = async {
            let output: Vec<u8> = Vec::new();
            let status = fetch_url(
                format!("http://localhost:{}", addr.port()).parse::<hyper::Uri>().unwrap(),
                output,
            )
            .await
            .unwrap();
            match status {
                StatusCode::OK | StatusCode::FOUND => {}
                _ => assert!(false, "Unexpected status code: {}", status),
            }
            stop.send(()).expect("server to still be running");
            Ok(())
        };

        let mut tasks: FuturesUnordered<BoxFuture<'_, Result<(), Error>>> = FuturesUnordered::new();
        tasks.push(Box::pin(server));
        tasks.push(Box::pin(client));
        while let Some(Ok(())) = tasks.next().await {}
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_download_handles_bad_domain() -> Result<()> {
        let output: Vec<u8> = Vec::new();
        let res = fetch_url("https://domain.invalid".parse::<hyper::Uri>()?, output).await;
        assert!(res.is_err());
        Ok(())
    }
}

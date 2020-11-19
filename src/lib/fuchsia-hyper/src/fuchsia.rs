// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{HyperConnectorFuture, TcpOptions, TcpStream},
    fidl_fuchsia_posix_socket::{ProviderMarker, ProviderProxy},
    fuchsia_async::{self, net},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    futures::{
        future::{Future, FutureExt},
        io,
        task::{Context, Poll},
    },
    http::uri::{Scheme, Uri},
    hyper::service::Service,
    rustls::ClientConfig,
    std::{
        convert::TryFrom as _,
        net::{
            AddrParseError, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6,
            ToSocketAddrs,
        },
        num::{ParseIntError, TryFromIntError},
    },
    tcp_stream_ext::TcpStreamExt as _,
};

pub(crate) fn configure_cert_store(tls: &mut ClientConfig) {
    tls.root_store.add_server_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS);
}

/// A Fuchsia-compatible implementation of hyper's `Connect` trait which allows
/// creating a TcpStream to a particular destination.
#[derive(Clone)]
pub struct HyperConnector {
    tcp_options: TcpOptions,
    provider: RealProviderConnector,
}

impl HyperConnector {
    pub fn new() -> Self {
        Self::from_tcp_options(TcpOptions::default())
    }

    pub fn from_tcp_options(tcp_options: TcpOptions) -> Self {
        Self { tcp_options, provider: RealProviderConnector }
    }
}

impl Service<Uri> for HyperConnector {
    type Response = TcpStream;
    type Error = std::io::Error;
    type Future = HyperConnectorFuture;

    fn poll_ready(&mut self, _: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        // This connector is always ready, but others might not be.
        Poll::Ready(Ok(()))
    }

    fn call(&mut self, dst: Uri) -> Self::Future {
        let self_ = self.clone();
        HyperConnectorFuture { fut: Box::pin(async move { self_.call_async(dst).await }) }
    }
}

impl HyperConnector {
    async fn call_async(&self, dst: Uri) -> Result<TcpStream, io::Error> {
        let host = dst
            .host()
            .ok_or(io::Error::new(io::ErrorKind::Other, "destination host is unspecified"))?;
        let port = match dst.port() {
            Some(port) => port.as_u16(),
            None => {
                if dst.scheme() == Some(&Scheme::HTTPS) {
                    443
                } else {
                    80
                }
            }
        };

        let addr = if let Some(addr) = parse_ip_addr(&self.provider, host, port).await? {
            addr
        } else {
            // TODO(cramertj): smarter DNS-- nonblocking, don't just pick first addr
            (host, port).to_socket_addrs()?.next().ok_or_else(|| {
                io::Error::new(io::ErrorKind::Other, "destination resolved to no address")
            })?
        };

        let stream = net::TcpStream::connect(addr)?.await?;
        let () = apply_tcp_options(stream.std(), &self.tcp_options)?;

        Ok(TcpStream { stream })
    }
}

fn apply_tcp_options(stream: &std::net::TcpStream, options: &TcpOptions) -> Result<(), io::Error> {
    if let Some(idle) = options.keepalive_idle {
        stream.set_keepalive(Some(idle))?;
    } else if options.keepalive_interval.is_some() || options.keepalive_count.is_some() {
        // This sets SO_KEEPALIVE without setting TCP_KEEPIDLE.
        stream.set_keepalive(None)?;
    }
    if let Some(interval) = options.keepalive_interval {
        stream.set_keepalive_interval(interval)?;
    }
    if let Some(count) = options.keepalive_count {
        stream.set_keepalive_count(count)?;
    }
    Ok(())
}

#[derive(Clone)]
pub struct Executor;

impl<F: Future + Send + 'static> hyper::rt::Executor<F> for Executor {
    fn execute(&self, fut: F) {
        fuchsia_async::Task::spawn(fut.map(|_| ())).detach()
    }
}

trait ProviderConnector {
    fn connect(&self) -> Result<ProviderProxy, io::Error>;
}

#[derive(Clone)]
struct RealProviderConnector;

impl ProviderConnector for RealProviderConnector {
    fn connect(&self) -> Result<ProviderProxy, io::Error> {
        connect_to_service::<ProviderMarker>().map_err(|err| {
            io::Error::new(
                io::ErrorKind::Other,
                format!("failed to connect to socket provider service: {}", err),
            )
        })
    }
}

async fn parse_ip_addr(
    provider: &impl ProviderConnector,
    host: &str,
    port: u16,
) -> Result<Option<SocketAddr>, io::Error> {
    match host.parse::<Ipv4Addr>() {
        Ok(addr) => {
            return Ok(Some(SocketAddr::V4(SocketAddrV4::new(addr, port))));
        }
        Err(AddrParseError { .. }) => {}
    }

    // IPv6 literals are always enclosed in [].
    if !host.starts_with("[") || !host.ends_with(']') {
        return Ok(None);
    }

    let host = &host[1..host.len() - 1];

    // IPv6 addresses with zones always contain "%25", which is "%" URL encoded.
    let mut host_parts = host.splitn(2, "%25");

    let addr = match host_parts.next() {
        Some(addr) => addr,
        None => {
            return Ok(None);
        }
    };

    let addr = match addr.parse::<Ipv6Addr>() {
        Ok(addr) => addr,
        Err(AddrParseError { .. }) => {
            return Ok(None);
        }
    };

    let scope_id = match host_parts.next() {
        Some(zone_id) => {
            // rfc6874 section 4 states:
            //
            //     The security considerations from the URI syntax specification
            //     [RFC3986] and the IPv6 Scoped Address Architecture specification
            //     [RFC4007] apply.  In particular, this URI format creates a specific
            //     pathway by which a deceitful zone index might be communicated, as
            //     mentioned in the final security consideration of the Scoped Address
            //     Architecture specification.  It is emphasised that the format is
            //     intended only for debugging purposes, but of course this intention
            //     does not prevent misuse.
            //
            //     To limit this risk, implementations MUST NOT allow use of this format
            //     except for well-defined usages, such as sending to link-local
            //     addresses under prefix fe80::/10.  At the time of writing, this is
            //     the only well-defined usage known.
            //
            // Since the only known use-case of IPv6 Zone Identifiers on Fuchsia is to communicate
            // with link-local devices, restrict addresse to link-local zone identifiers.
            //
            // TODO: use Ipv6Addr::is_unicast_link_local_strict when available in stable rust.
            if addr.segments()[..4] != [0xfe80, 0, 0, 0] {
                return Err(io::Error::new(
                    io::ErrorKind::Other,
                    "zone_id is only usable with link local addresses",
                ));
            }

            // TODO: validate that the value matches rfc6874 grammar `ZoneID = 1*( unreserved / pct-encoded )`.
            match zone_id.parse::<u32>() {
                Ok(zone_id_num) => zone_id_num,
                Err(ParseIntError { .. }) => {
                    let proxy = provider.connect()?;
                    let id = proxy
                        .interface_name_to_index(zone_id)
                        .await
                        .map_err(|err| {
                            io::Error::new(
                                io::ErrorKind::Other,
                                format!(
                                    "failed to get interface index from socket provider: {}",
                                    err
                                ),
                            )
                        })?
                        .map_err(|status| zx::Status::from_raw(status).into_io_error())?;

                    // SocketAddrV6 only works with 32 bit scope ids.
                    u32::try_from(id).map_err(|TryFromIntError { .. }| {
                        io::Error::new(
                            io::ErrorKind::Other,
                            "interface index too large to convert to scope_id",
                        )
                    })?
                }
            }
        }
        None => 0,
    };

    Ok(Some(SocketAddr::V6(SocketAddrV6::new(addr, port, 0, scope_id))))
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_posix_socket::ProviderRequest,
        fuchsia_async::{self as fasync, net::TcpListener, Executor},
        futures::prelude::*,
        matches::assert_matches,
        std::cell::RefCell,
    };

    struct PanicConnector;

    impl ProviderConnector for PanicConnector {
        fn connect(&self) -> Result<ProviderProxy, io::Error> {
            panic!("should not be trying to talk to the Provider service")
        }
    }

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

    #[fasync::run_singlethreaded(test)]
    async fn hyper_connector_sets_tcp_options() {
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);
        let listener = TcpListener::bind(&addr).unwrap();
        let addr = listener.local_addr().unwrap();
        let listener = listener.accept_stream();
        fasync::Task::spawn(async move {
            listener
                .map(|res| {
                    res.unwrap();
                })
                .collect()
                .await
        })
        .detach();

        let idle = std::time::Duration::from_secs(36);
        let interval = std::time::Duration::from_secs(47);
        let count = 58;
        let uri = format!("https://{}", addr).parse::<hyper::Uri>().unwrap();
        let stream = HyperConnector::from_tcp_options(TcpOptions {
            keepalive_idle: Some(idle),
            keepalive_interval: Some(interval),
            keepalive_count: Some(count),
            ..Default::default()
        })
        .call(uri)
        .await
        .unwrap()
        .stream;

        assert_matches!(stream.std().keepalive(), Ok(Some(v)) if v == idle);
        assert_matches!(stream.std().keepalive_interval(), Ok(v) if v == interval);
        assert_matches!(stream.std().keepalive_count(), Ok(v) if v == count);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv4_addr() {
        let expected = "1.2.3.4:8080".parse::<SocketAddr>().unwrap();
        assert_matches!(parse_ip_addr(&PanicConnector, "1.2.3.4", 8080).await, Ok(Some(addr)) if addr == expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_invalid_addresses() {
        assert_matches!(parse_ip_addr(&PanicConnector, "1.2.3", 8080).await, Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "1.2.3.4.5", 8080).await, Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "localhost", 8080).await, Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4", 8080).await, Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "[[fe80::1:2:3:4]", 8080).await, Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "[]", 8080).await, Ok(None));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv6_addr() {
        let expected = "[fe80::1:2:3:4]:8080".parse::<SocketAddr>().unwrap();
        assert_matches!(parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4]", 8080).await, Ok(Some(addr)) if addr == expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv6_addr_with_zone() {
        let expected = "fe80::1:2:3:4".parse::<Ipv6Addr>().unwrap();

        assert_matches!(
            parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4%250]", 8080).await,
            Ok(Some(addr)) if addr == SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 0))
        );

        assert_matches!(
            parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4%252]", 8080).await,
            Ok(Some(addr)) if addr == SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 2))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv6_addr_with_zone_supports_interface_names() {
        let expected = "fe80::1:2:3:4".parse::<Ipv6Addr>().unwrap();

        assert_matches!(
            parse_ip_addr(&RealProviderConnector, "[fe80::1:2:3:4%25lo]", 8080).await,
            Ok(Some(addr)) if addr == SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 1))
        );

        assert_matches!(
            parse_ip_addr(&RealProviderConnector, "[fe80::1:2:3:4%25]", 8080).await,
            Err(err) if err.kind() == io::ErrorKind::NotFound
        );

        assert_matches!(
            parse_ip_addr(&RealProviderConnector, "[fe80::1:2:3:4%25unknownif]", 8080).await,
            Err(err) if err.kind() == io::ErrorKind::NotFound
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv6_addr_with_zone_must_be_local() {
        assert_matches!(
            parse_ip_addr(&PanicConnector, "[fe81::1:2:3:4%252]", 8080).await,
            Err(err) if err.kind() == io::ErrorKind::Other);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv6_addr_handles_connection_errors() {
        struct ErrorConnector;

        impl ProviderConnector for ErrorConnector {
            fn connect(&self) -> Result<ProviderProxy, io::Error> {
                Err(io::Error::new(io::ErrorKind::Other, "something bad happened"))
            }
        }

        assert_matches!(parse_ip_addr(&ErrorConnector, "[fe80::1:2:3:4%25lo]", 8080).await,
            Err(err) if err.kind() == io::ErrorKind::Other);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv6_addr_handles_large_interface_indices() {
        let (proxy, mut stream) = create_proxy_and_stream::<ProviderMarker>().unwrap();

        let provider_fut = async move {
            while let Some(req) = stream.try_next().await.unwrap_or(None) {
                match req {
                    ProviderRequest::InterfaceNameToIndex { name: _, responder } => {
                        let mut res = Ok(std::u64::MAX);
                        responder.send(&mut res).unwrap()
                    }
                    _ => panic!("unexpected request"),
                }
            }
        };

        struct ErrorConnector {
            proxy: RefCell<Option<ProviderProxy>>,
        }

        impl ProviderConnector for ErrorConnector {
            fn connect(&self) -> Result<ProviderProxy, io::Error> {
                let proxy = self.proxy.borrow_mut().take().unwrap();
                Ok(proxy)
            }
        }

        let mut connector = ErrorConnector { proxy: RefCell::new(Some(proxy)) };

        let parse_ip_fut = parse_ip_addr(&mut connector, "[fe80::1:2:3:4%25lo]", 8080);

        // Join the two futures to make sure they both complete.
        let ((), res) = future::join(provider_fut, parse_ip_fut).await;

        assert_matches!(res, Err(err) if err.kind() == io::ErrorKind::Other);
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_posix_socket::{ProviderMarker, ProviderSynchronousProxy},
    fuchsia_async::{
        self,
        net::{self, TcpConnector},
    },
    fuchsia_component::client::connect_channel_to_service_at,
    fuchsia_zircon as zx,
    futures::{
        future::{Future, FutureExt},
        io::{self, AsyncRead, AsyncWrite},
        ready,
        task::{Context, Poll},
    },
    http::uri::{Scheme, Uri},
    hyper::{
        client::{
            connect::{Connected, Connection},
            Client,
        },
        service::Service,
        Body,
    },
    std::{
        convert::TryFrom as _,
        net::{
            AddrParseError, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6,
            ToSocketAddrs,
        },
        num::{ParseIntError, TryFromIntError},
        pin::Pin,
    },
    tcp_stream_ext::TcpStreamExt as _,
};

/// A Fuchsia-compatible hyper client configured for making HTTP requests.
pub type HttpClient = Client<HyperConnector, Body>;

/// A Fuchsia-compatible hyper client configured for making HTTP and HTTPS requests.
pub type HttpsClient = Client<hyper_rustls::HttpsConnector<HyperConnector>, Body>;

/// A future that yields a hyper-compatible TCP stream.
#[must_use = "futures do nothing unless polled"]
pub struct HyperConnectorFuture {
    tcp_connector_res: Result<TcpConnector, Option<io::Error>>,
    tcp_options: TcpOptions,
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

impl Future for HyperConnectorFuture {
    type Output = Result<TcpStream, io::Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let HyperConnectorFuture { tcp_connector_res, tcp_options } = &mut *self;
        let connector = tcp_connector_res.as_mut().map_err(|x| x.take().unwrap())?;
        let stream = ready!(connector.poll_unpin(cx))?;
        let () = apply_tcp_options(stream.std(), tcp_options)?;
        Poll::Ready(Ok(TcpStream { stream }))
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
        HyperConnectorFuture {
            tcp_connector_res: (|| {
                let host = dst.host().ok_or(io::Error::new(
                    io::ErrorKind::Other,
                    "destination host is unspecified",
                ))?;
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

                let addr = if let Some(addr) = parse_ip_addr(&self.provider, host, port)? {
                    addr
                } else {
                    // TODO(cramertj): smarter DNS-- nonblocking, don't just pick first addr
                    (host, port).to_socket_addrs()?.next().ok_or_else(|| {
                        io::Error::new(io::ErrorKind::Other, "destination resolved to no address")
                    })?
                };
                net::TcpStream::connect(addr)
            })()
            .map_err(Some),
            tcp_options: self.tcp_options.clone(),
        }
    }
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
    tls.root_store.add_server_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS);
    new_https_client_dangerous(tls, tcp_options)
}

/// Returns a new Fuchsia-compatible hyper client for making HTTP and HTTPS requests.
pub fn new_https_client() -> HttpsClient {
    new_https_client_from_tcp_options(std::default::Default::default())
}

trait ProviderConnector {
    fn connect(&self) -> Result<ProviderSynchronousProxy, io::Error>;
}

#[derive(Clone)]
struct RealProviderConnector;

impl ProviderConnector for RealProviderConnector {
    fn connect(&self) -> Result<ProviderSynchronousProxy, io::Error> {
        let (proxy, server) = zx::Channel::create()?;
        let () =
            connect_channel_to_service_at::<ProviderMarker>(server, "/svc").map_err(|err| {
                io::Error::new(
                    io::ErrorKind::Other,
                    format!("failed to connect to socket provider service: {}", err),
                )
            })?;
        Ok(ProviderSynchronousProxy::new(proxy))
    }
}

fn parse_ip_addr(
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
                    let mut proxy = provider.connect()?;
                    let id = proxy
                        .interface_name_to_index(zone_id, zx::Time::INFINITE)
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
        fidl::endpoints::RequestStream,
        fidl_fuchsia_posix_socket::{ProviderRequest, ProviderRequestStream},
        fuchsia_async::{self as fasync, net::TcpListener, Executor},
        futures::stream::{StreamExt as _, TryStreamExt as _},
        matches::assert_matches,
        std::cell::RefCell,
    };

    struct PanicConnector;

    impl ProviderConnector for PanicConnector {
        fn connect(&self) -> Result<ProviderSynchronousProxy, io::Error> {
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

    #[test]
    fn test_parse_ipv4_addr() {
        let expected = "1.2.3.4:8080".parse::<SocketAddr>().unwrap();
        assert_matches!(parse_ip_addr(&PanicConnector, "1.2.3.4", 8080), Ok(Some(addr)) if addr == expected);
    }

    #[test]
    fn test_parse_invalid_addresses() {
        assert_matches!(parse_ip_addr(&PanicConnector, "1.2.3", 8080), Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "1.2.3.4.5", 8080), Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "localhost", 8080), Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4", 8080), Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "[[fe80::1:2:3:4]", 8080), Ok(None));
        assert_matches!(parse_ip_addr(&PanicConnector, "[]", 8080), Ok(None));
    }

    #[test]
    fn test_parse_ipv6_addr() {
        let expected = "[fe80::1:2:3:4]:8080".parse::<SocketAddr>().unwrap();
        assert_matches!(parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4]", 8080), Ok(Some(addr)) if addr == expected);
    }

    #[test]
    fn test_parse_ipv6_addr_with_zone() {
        let expected = "fe80::1:2:3:4".parse::<Ipv6Addr>().unwrap();

        assert_matches!(
            parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4%250]", 8080),
            Ok(Some(addr)) if addr == SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 0))
        );

        assert_matches!(
            parse_ip_addr(&PanicConnector, "[fe80::1:2:3:4%252]", 8080),
            Ok(Some(addr)) if addr == SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 2))
        );
    }

    #[test]
    fn test_parse_ipv6_addr_with_zone_supports_interface_names() {
        let expected = "fe80::1:2:3:4".parse::<Ipv6Addr>().unwrap();

        assert_matches!(
            parse_ip_addr(&RealProviderConnector, "[fe80::1:2:3:4%25lo]", 8080),
            Ok(Some(addr)) if addr == SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 1))
        );

        assert_matches!(
            parse_ip_addr(&RealProviderConnector, "[fe80::1:2:3:4%25]", 8080),
            Err(err) if err.kind() == io::ErrorKind::NotFound
        );

        assert_matches!(
            parse_ip_addr(&RealProviderConnector, "[fe80::1:2:3:4%25unknownif]", 8080),
            Err(err) if err.kind() == io::ErrorKind::NotFound
        );
    }

    #[test]
    fn test_parse_ipv6_addr_with_zone_must_be_local() {
        assert_matches!(
            parse_ip_addr(&PanicConnector, "[fe81::1:2:3:4%252]", 8080),
            Err(err) if err.kind() == io::ErrorKind::Other);
    }

    #[test]
    fn test_parse_ipv6_addr_handles_connection_errors() {
        struct ErrorConnector;

        impl ProviderConnector for ErrorConnector {
            fn connect(&self) -> Result<ProviderSynchronousProxy, io::Error> {
                Err(io::Error::new(io::ErrorKind::Other, "something bad happened"))
            }
        }

        assert_matches!(parse_ip_addr(&ErrorConnector, "[fe80::1:2:3:4%25lo]", 8080),
            Err(err) if err.kind() == io::ErrorKind::Other);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parse_ipv6_addr_handles_large_interface_indices() {
        let (proxy, server) = zx::Channel::create().unwrap();
        let proxy = ProviderSynchronousProxy::new(proxy);
        let server = fasync::Channel::from_channel(server).unwrap();
        let mut stream = ProviderRequestStream::from_channel(server);

        let task = fasync::Task::blocking(async move {
            while let Some(req) = stream.try_next().await.unwrap_or(None) {
                match req {
                    ProviderRequest::InterfaceNameToIndex { name: _, responder } => {
                        let mut res = Ok(std::u64::MAX);
                        responder.send(&mut res).unwrap()
                    }
                    _ => panic!("unexpected request"),
                }
            }
        });

        struct ErrorConnector {
            proxy: RefCell<Option<ProviderSynchronousProxy>>,
        }

        impl ProviderConnector for ErrorConnector {
            fn connect(&self) -> Result<ProviderSynchronousProxy, io::Error> {
                let proxy = self.proxy.borrow_mut().take().unwrap();
                Ok(proxy)
            }
        }

        let mut connector = ErrorConnector { proxy: RefCell::new(Some(proxy)) };

        assert_matches!(
            parse_ip_addr(&mut connector, "[fe80::1:2:3:4%25lo]", 8080),
            Err(err) if err.kind() == io::ErrorKind::Other
        );

        // Make sure the connection is terminated.
        let () = task.await;
    }
}

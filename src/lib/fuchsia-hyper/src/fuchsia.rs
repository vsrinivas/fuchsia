// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        happy_eyeballs::{self, RealSocketConnector},
        HyperConnectorFuture, TcpOptions, TcpStream,
    },
    fidl_connector::{Connect, ServiceReconnector},
    fidl_fuchsia_net_name::{LookupIpOptions, LookupMarker, LookupProxy, LookupResult},
    fidl_fuchsia_posix_socket::{ProviderMarker, ProviderProxy},
    fuchsia_async::{self, net},
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
        net::{AddrParseError, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6},
        num::{ParseIntError, TryFromIntError},
    },
};

pub(crate) fn configure_cert_store(tls: &mut ClientConfig) {
    tls.root_store.add_server_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS);
}

/// A Fuchsia-compatible implementation of hyper's `Connect` trait which allows
/// creating a TcpStream to a particular destination.
#[derive(Clone)]
pub struct HyperConnector {
    tcp_options: TcpOptions,
    provider: RealServiceConnector,
}

impl HyperConnector {
    pub fn new() -> Self {
        Self::from_tcp_options(TcpOptions::default())
    }

    pub fn from_tcp_options(tcp_options: TcpOptions) -> Self {
        Self { tcp_options, provider: RealServiceConnector::new() }
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

        let stream = connect_to_addr(&self.provider, host, port).await?;
        let () = apply_tcp_options(stream.std(), &self.tcp_options)?;

        Ok(TcpStream { stream })
    }
}

fn apply_tcp_options(stream: &std::net::TcpStream, options: &TcpOptions) -> Result<(), io::Error> {
    let stream = socket2::SockRef::from(stream);
    let mut any = false;
    let mut keepalive = socket2::TcpKeepalive::new();
    if let Some(idle) = options.keepalive_idle {
        any = true;
        keepalive = keepalive.with_time(idle);
    };
    if let Some(interval) = options.keepalive_interval {
        any = true;
        keepalive = keepalive.with_interval(interval);
    }
    if let Some(count) = options.keepalive_count {
        any = true;
        keepalive = keepalive.with_retries(count);
    }
    if any {
        stream.set_tcp_keepalive(&keepalive)
    } else {
        Ok(())
    }
}

#[derive(Clone)]
pub struct Executor;

impl<F: Future + Send + 'static> hyper::rt::Executor<F> for Executor {
    fn execute(&self, fut: F) {
        fuchsia_async::Task::spawn(fut.map(|_| ())).detach()
    }
}

#[derive(Clone)]
pub struct LocalExecutor;

impl<F: Future + 'static> hyper::rt::Executor<F> for LocalExecutor {
    fn execute(&self, fut: F) {
        fuchsia_async::Task::local(fut.map(drop)).detach()
    }
}

trait ProviderConnector {
    fn connect(&self) -> Result<ProviderProxy, io::Error>;
}

trait LookupConnector {
    fn connect(&self) -> Result<LookupProxy, io::Error>;
}

#[derive(Clone)]
struct RealServiceConnector {
    socket_provider_connector: ServiceReconnector<ProviderMarker>,
    name_lookup_connector: ServiceReconnector<LookupMarker>,
}

impl RealServiceConnector {
    fn new() -> Self {
        RealServiceConnector {
            socket_provider_connector: ServiceReconnector::<ProviderMarker>::new(),
            name_lookup_connector: ServiceReconnector::<LookupMarker>::new(),
        }
    }
}

impl ProviderConnector for RealServiceConnector {
    fn connect(&self) -> Result<ProviderProxy, io::Error> {
        self.socket_provider_connector.connect().map_err(|err| {
            io::Error::new(
                io::ErrorKind::Other,
                format!("failed to connect to socket provider service: {}", err),
            )
        })
    }
}

impl LookupConnector for RealServiceConnector {
    fn connect(&self) -> Result<LookupProxy, io::Error> {
        self.name_lookup_connector.connect().map_err(|err| {
            io::Error::new(
                io::ErrorKind::Other,
                format!("failed to connect to name lookup service: {}", err),
            )
        })
    }
}

async fn connect_to_addr<T: ProviderConnector + LookupConnector>(
    provider: &T,
    host: &str,
    port: u16,
) -> Result<net::TcpStream, io::Error> {
    if let Some(addr) = parse_ip_addr(provider, host, port).await? {
        return net::TcpStream::connect(addr)?.await;
    }

    happy_eyeballs::happy_eyeballs(
        resolve_ip_addr(provider, host, port).await?,
        RealSocketConnector,
        happy_eyeballs::RECOMMENDED_MIN_CONN_ATT_DELAY,
        happy_eyeballs::RECOMMENDED_CONN_ATT_DELAY,
    )
    .await
}

async fn resolve_ip_addr(
    name_lookup: &impl LookupConnector,
    host: &str,
    port: u16,
) -> Result<impl Iterator<Item = SocketAddr>, io::Error> {
    let proxy = name_lookup.connect()?;
    let LookupResult { addresses, .. } = proxy
        .lookup_ip(
            host,
            LookupIpOptions {
                ipv4_lookup: Some(true),
                ipv6_lookup: Some(true),
                sort_addresses: Some(true),
                ..LookupIpOptions::EMPTY
            },
        )
        .await
        .map_err(|err| {
            io::Error::new(
                io::ErrorKind::Other,
                format!("failed to call NameProvider.LookupIp: {}", err),
            )
        })?
        .map_err(|err| {
            // Match stdlib's behavior, which maps all GAI errors but EAI_SYSTEM
            // to io::ErrorKind::Other.
            io::Error::new(
                io::ErrorKind::Other,
                format!("NameProvider.LookupIp failure: {:?}", err),
            )
        })?;

    Ok(addresses
        .ok_or_else(|| {
            io::Error::new(io::ErrorKind::Other, "addresses not provided in NameProvider response")
        })?
        .into_iter()
        .map(move |addr| {
            let fidl_fuchsia_net_ext::IpAddress(addr) = addr.into();
            SocketAddr::new(addr, port)
        }))
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
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_net_name::{LookupError, LookupRequest},
        fidl_fuchsia_posix_socket::ProviderRequest,
        fuchsia_async::{self as fasync, net::TcpListener, LocalExecutor},
        futures::prelude::*,
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
        let _exec = LocalExecutor::new().unwrap();
        let _client = new_client();
    }

    #[test]
    fn can_create_https_client() {
        let _exec = LocalExecutor::new().unwrap();
        let _client = new_https_client();
    }

    #[fasync::run_singlethreaded(test)]
    async fn hyper_connector_sets_tcp_options() {
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);
        let listener = TcpListener::bind(&addr).unwrap();
        let addr = listener.local_addr().unwrap();

        let idle = std::time::Duration::from_secs(36);
        let interval = std::time::Duration::from_secs(47);
        let count = 58;
        let uri = format!("https://{}", addr).parse::<hyper::Uri>().unwrap();
        let (TcpStream { stream }, _server) = future::try_join(
            HyperConnector::from_tcp_options(TcpOptions {
                keepalive_idle: Some(idle),
                keepalive_interval: Some(interval),
                keepalive_count: Some(count),
                ..Default::default()
            })
            .call(uri),
            listener.accept_stream().try_next(),
        )
        .await
        .unwrap();

        let stream = socket2::SockRef::from(stream.std());

        assert_matches!(stream.keepalive(), Ok(v) if v);
        assert_matches!(stream.keepalive_time(), Ok(v) if v == idle);
        assert_matches!(stream.keepalive_interval(), Ok(v) if v == interval);
        assert_matches!(stream.keepalive_retries(), Ok(v) if v == count);
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
        let connector = RealServiceConnector::new();
        let expected = "fe80::1:2:3:4".parse::<Ipv6Addr>().unwrap();

        assert_matches!(
            parse_ip_addr(&connector, "[fe80::1:2:3:4%25lo]", 8080).await,
            Ok(Some(addr)) if addr == SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 1))
        );

        assert_matches!(
            parse_ip_addr(&connector, "[fe80::1:2:3:4%25]", 8080).await,
            Err(err) if err.kind() == io::ErrorKind::NotFound
        );

        assert_matches!(
            parse_ip_addr(&connector, "[fe80::1:2:3:4%25unknownif]", 8080).await,
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

    struct ProxyConnector<T> {
        proxy: T,
    }

    impl LookupConnector for ProxyConnector<LookupProxy> {
        fn connect(&self) -> Result<LookupProxy, io::Error> {
            Ok(self.proxy.clone())
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_ip_addr() {
        let (sender, receiver) = futures::channel::mpsc::unbounded();
        let (proxy, stream) = create_proxy_and_stream::<LookupMarker>()
            .expect("failed to create Lookup proxy and stream");
        const TEST_HOSTNAME: &'static str = "foobar.com";
        let name_lookup_fut = stream.zip(receiver).for_each(|(req, mut rsp)| match req {
            Ok(LookupRequest::LookupIp { hostname, options, responder }) => {
                assert_eq!(hostname.as_str(), TEST_HOSTNAME);
                assert_eq!(
                    options,
                    LookupIpOptions {
                        ipv4_lookup: Some(true),
                        ipv6_lookup: Some(true),
                        sort_addresses: Some(true),
                        ..LookupIpOptions::EMPTY
                    }
                );
                futures::future::ready(
                    responder.send(&mut rsp).expect("failed to send FIDL response"),
                )
            }
            req => panic!("unexpected item in request stream {:?}", req),
        });

        let connector = ProxyConnector { proxy };

        let ip_v4 = Ipv4Addr::LOCALHOST.into();
        let ip_v6 = Ipv6Addr::LOCALHOST.into();
        const PORT1: u16 = 1234;
        const PORT2: u16 = 4321;

        let test_fut = async move {
            // Test expectation's error variant is a tuple of the lookup error
            // to inject and the expected io error kind returned.
            type Expectation = Result<Vec<std::net::IpAddr>, (LookupError, io::ErrorKind)>;
            let test_resolve = |port, expect: Expectation| {
                let fidl_response = expect
                    .clone()
                    .map(|addrs| LookupResult {
                        addresses: Some(
                            addrs
                                .into_iter()
                                .map(|std| fidl_fuchsia_net_ext::IpAddress(std).into())
                                .collect(),
                        ),
                        ..LookupResult::EMPTY
                    })
                    .map_err(|(fidl_err, _io_err)| fidl_err);
                let expect = expect
                    .map(|addrs| {
                        addrs.into_iter().map(|addr| SocketAddr::new(addr, port)).collect()
                    })
                    .map_err(|(_fidl_err, io_err)| io_err);
                let () = sender
                    .unbounded_send(fidl_response.clone())
                    .expect("failed to send expectation");
                resolve_ip_addr(&connector, TEST_HOSTNAME, port)
                    .map_ok(Iterator::collect::<Vec<_>>)
                    // Map IO error to kind so we can do equality.
                    .map_err(|err| err.kind())
                    .map(move |result| {
                        assert_eq!(result, expect);
                    })
            };
            let () = test_resolve(PORT1, Ok(vec![ip_v4])).await;
            let () = test_resolve(PORT2, Ok(vec![ip_v6])).await;
            let () = test_resolve(PORT1, Ok(vec![ip_v4, ip_v6])).await;
            let () = test_resolve(PORT1, Err((LookupError::NotFound, io::ErrorKind::Other))).await;
        };

        let ((), ()) = futures::future::join(name_lookup_fut, test_fut).await;
    }
}

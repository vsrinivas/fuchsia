// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    dns::{
        async_resolver::{Handle, Resolver},
        policy::{ServerConfigPolicy, ServerList},
    },
    fidl_fuchsia_net::{self as fnet, NameLookupRequest, NameLookupRequestStream},
    fidl_fuchsia_net_ext as net_ext,
    fidl_fuchsia_net_name::{self as fname, LookupAdminRequest, LookupAdminRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as fx_syslog, fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        task::{Context, Poll},
        FutureExt, Sink, SinkExt, StreamExt, TryFutureExt, TryStreamExt,
    },
    parking_lot::RwLock,
    std::pin::Pin,
    std::sync::Arc,
    std::{net::IpAddr, rc::Rc},
    trust_dns_proto::rr::{domain::IntoName, TryParseIp},
    trust_dns_resolver::{
        config::{
            LookupIpStrategy, NameServerConfig, NameServerConfigGroup, Protocol, ResolverConfig,
            ResolverOpts,
        },
        error::ResolveError,
        lookup, lookup_ip,
    },
};

struct SharedResolver<T>(RwLock<Rc<T>>);

impl<T> SharedResolver<T> {
    fn new(resolver: T) -> Self {
        SharedResolver(RwLock::new(Rc::new(resolver)))
    }

    fn read(&self) -> Rc<T> {
        self.0.read().clone()
    }

    fn write(&self, other: Rc<T>) {
        *self.0.write() = other;
    }
}

/// `SharedResolverConfigSink` acts as a `Sink` that takes resolver
/// configurations in the form of [`ServerList`]s and applies them to a
/// [`SharedResolver`].
struct SharedResolverConfigSink<'a, T> {
    shared_resolver: &'a SharedResolver<T>,
    staged_change: Option<ServerList>,
    flush_state: Option<futures::future::LocalBoxFuture<'a, ()>>,
}

impl<'a, T: ResolverLookup> SharedResolverConfigSink<'a, T> {
    fn new(shared_resolver: &'a SharedResolver<T>) -> Self {
        Self { shared_resolver, staged_change: None, flush_state: None }
    }

    fn update_resolver(&self, servers: ServerList) -> impl 'a + futures::Future<Output = ()> {
        let mut resolver_opts = ResolverOpts::default();
        resolver_opts.ip_strategy = LookupIpStrategy::Ipv4AndIpv6;

        // We're going to add each server twice, once with protocol UDP and
        // then with protocol TCP.
        let mut name_servers = NameServerConfigGroup::with_capacity(servers.len() * 2);

        name_servers.extend(servers.into_iter().flat_map(|server| {
            let net_ext::SocketAddress(socket_addr) = server.address.into();
            // Every server config gets UDP and TCP versions with
            // preference for UDP.
            std::iter::once(NameServerConfig {
                socket_addr,
                protocol: Protocol::Udp,
                tls_dns_name: None,
            })
            .chain(std::iter::once(NameServerConfig {
                socket_addr,
                protocol: Protocol::Tcp,
                tls_dns_name: None,
            }))
        }));

        let resolver_ref = self.shared_resolver;

        T::new(ResolverConfig::from_parts(None, Vec::new(), name_servers), resolver_opts)
            .map(move |r| resolver_ref.write(Rc::new(r)))
    }

    fn poll(&mut self, cx: &mut Context<'_>) -> Poll<Result<(), never::Never>> {
        // Finish the last update first.
        if let Some(fut) = self.flush_state.as_mut() {
            match fut.poll_unpin(cx) {
                Poll::Ready(()) => {
                    self.flush_state = None;
                }
                Poll::Pending => return Poll::Pending,
            }
        }

        // Update to the latest state if any.
        if let Some(pending) = self.staged_change.take() {
            self.flush_state = Some(self.update_resolver(pending).boxed_local());
            return self.poll(cx);
        }
        Poll::Ready(Ok(()))
    }
}

impl<'a, T: ResolverLookup> Sink<ServerList> for SharedResolverConfigSink<'a, T> {
    type Error = never::Never;

    fn poll_ready(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        // We always allow overwriting the staged_change, configuration will
        // always only be applied on flush. This allows a series of
        // configuration events to be received but consolidation only happens
        // on flush.
        Poll::Ready(Ok(()))
    }

    fn start_send(self: Pin<&mut Self>, item: ServerList) -> Result<(), Self::Error> {
        self.get_mut().staged_change = Some(item);
        Ok(())
    }

    fn poll_flush(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.get_mut().poll(cx)
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Self::poll_flush(self, cx)
    }
}

enum IncomingRequest {
    // NameLookup service.
    NameLookup(NameLookupRequestStream),
    // LookupAdmin Service.
    LookupAdmin(LookupAdminRequestStream),
}

#[async_trait]
trait ResolverLookup {
    async fn new(config: ResolverConfig, options: ResolverOpts) -> Self;

    async fn lookup_ip<N: IntoName + TryParseIp + Send>(
        &self,
        host: N,
    ) -> Result<lookup_ip::LookupIp, ResolveError>;

    async fn ipv4_lookup<N: IntoName + Send>(
        &self,
        host: N,
    ) -> Result<lookup::Ipv4Lookup, ResolveError>;

    async fn ipv6_lookup<N: IntoName + Send>(
        &self,
        host: N,
    ) -> Result<lookup::Ipv6Lookup, ResolveError>;

    async fn reverse_lookup(&self, addr: IpAddr) -> Result<lookup::ReverseLookup, ResolveError>;
}

#[async_trait]
impl ResolverLookup for Resolver {
    async fn new(config: ResolverConfig, options: ResolverOpts) -> Self {
        let handle = Handle::new(fasync::EHandle::local());
        Resolver::new(config, options, handle).await.expect("failed to create resolver")
    }

    async fn lookup_ip<N: IntoName + TryParseIp + Send>(
        &self,
        host: N,
    ) -> Result<lookup_ip::LookupIp, ResolveError> {
        self.lookup_ip(host).await
    }

    async fn ipv4_lookup<N: IntoName + Send>(
        &self,
        host: N,
    ) -> Result<lookup::Ipv4Lookup, ResolveError> {
        self.ipv4_lookup(host).await
    }

    async fn ipv6_lookup<N: IntoName + Send>(
        &self,
        host: N,
    ) -> Result<lookup::Ipv6Lookup, ResolveError> {
        self.ipv6_lookup(host).await
    }

    async fn reverse_lookup(&self, addr: IpAddr) -> Result<lookup::ReverseLookup, ResolveError> {
        self.reverse_lookup(addr).await
    }
}

fn convert_err(err: ResolveError) -> fnet::LookupError {
    use {trust_dns_proto::error::ProtoErrorKind, trust_dns_resolver::error::ResolveErrorKind};

    match err.kind() {
        // The following mapping is based on the analysis of `ResolveError` enumerations.
        // For cases that are not obvious such as `ResolveErrorKind::Msg` and
        // `ResolveErrorKind::Message`, I (chunyingw) did code searches to have more insights.
        // `ResolveErrorKind::Msg`: An error with arbitray message, it could be ex. "lock was
        // poisoned, this is non-recoverable" and ""DNS Error".
        // `ResolveErrorKind::Message`: An error with arbitray message, it is mostly returned when
        // there is no name in the input vector to look up with "can not lookup for no names".
        // This is a best-effort mapping.
        ResolveErrorKind::NoRecordsFound { query: _, valid_until: _ } => {
            fnet::LookupError::NotFound
        }
        ResolveErrorKind::Proto(err) => match err.kind() {
            ProtoErrorKind::DomainNameTooLong(_) | ProtoErrorKind::EdnsNameNotRoot(_) => {
                fnet::LookupError::InvalidArgs
            }
            ProtoErrorKind::Canceled(_) | ProtoErrorKind::Io(_) | ProtoErrorKind::Timeout => {
                fnet::LookupError::Transient
            }
            ProtoErrorKind::CharacterDataTooLong { max: _, len: _ }
            | ProtoErrorKind::LabelOverlapsWithOther { label: _, other: _ }
            | ProtoErrorKind::DnsKeyProtocolNot3(_)
            | ProtoErrorKind::IncorrectRDataLengthRead { read: _, len: _ }
            | ProtoErrorKind::LabelBytesTooLong(_)
            | ProtoErrorKind::PointerNotPriorToLabel { idx: _, ptr: _ }
            | ProtoErrorKind::MaxBufferSizeExceeded(_)
            | ProtoErrorKind::Message(_)
            | ProtoErrorKind::Msg(_)
            | ProtoErrorKind::NoError
            | ProtoErrorKind::NotAllRecordsWritten { count: _ }
            | ProtoErrorKind::RrsigsNotPresent { name: _, record_type: _ }
            | ProtoErrorKind::UnknownAlgorithmTypeValue(_)
            | ProtoErrorKind::UnknownDnsClassStr(_)
            | ProtoErrorKind::UnknownDnsClassValue(_)
            | ProtoErrorKind::UnknownRecordTypeStr(_)
            | ProtoErrorKind::UnknownRecordTypeValue(_)
            | ProtoErrorKind::UnrecognizedLabelCode(_)
            | ProtoErrorKind::UnrecognizedNsec3Flags(_)
            | ProtoErrorKind::Poisoned
            | ProtoErrorKind::Ring(_)
            | ProtoErrorKind::SSL(_)
            | ProtoErrorKind::Timer
            | ProtoErrorKind::UrlParsing(_)
            | ProtoErrorKind::Utf8(_) => fnet::LookupError::InternalError,
        },
        ResolveErrorKind::Io(_) | ResolveErrorKind::Timeout => fnet::LookupError::Transient,
        ResolveErrorKind::Msg(_) | ResolveErrorKind::Message(_) => fnet::LookupError::InternalError,
    }
}

async fn handle_lookup_ip<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    hostname: String,
    options: fnet::LookupIpOptions,
) -> Result<fnet::IpAddressInfo, fnet::LookupError> {
    let resolver = resolver.read();

    let response: Result<Vec<fnet::IpAddress>, ResolveError> =
        if options.contains(fnet::LookupIpOptions::V4Addrs | fnet::LookupIpOptions::V6Addrs) {
            resolver
                .lookup_ip(hostname)
                .await
                .map(|addrs| addrs.iter().map(|addr| net_ext::IpAddress(addr).into()).collect())
        } else if options.contains(fnet::LookupIpOptions::V4Addrs) {
            resolver.ipv4_lookup(hostname).await.map(|addrs| {
                addrs.iter().map(|addr| net_ext::IpAddress(IpAddr::V4(*addr)).into()).collect()
            })
        } else if options.contains(fnet::LookupIpOptions::V6Addrs) {
            resolver.ipv6_lookup(hostname).await.map(|addrs| {
                addrs.iter().map(|addr| net_ext::IpAddress(IpAddr::V6(*addr)).into()).collect()
            })
        } else {
            return Err(fnet::LookupError::InvalidArgs);
        };

    match response {
        Ok(response) => {
            if response.is_empty() {
                return Err(fnet::LookupError::NotFound);
            }

            let mut result = fnet::IpAddressInfo {
                ipv4_addrs: vec![],
                ipv6_addrs: vec![],
                canonical_name: None,
            };

            for address in response.iter() {
                match address {
                    fnet::IpAddress::Ipv4(ipv4) => {
                        result.ipv4_addrs.push(*ipv4);
                    }
                    fnet::IpAddress::Ipv6(ipv6) => {
                        result.ipv6_addrs.push(*ipv6);
                    }
                }
            }
            Ok(result)
        }
        Err(error) => {
            fx_log_err!("failed to LookupIp with error {}", error);
            Err(convert_err(error))
        }
    }
}

async fn handle_lookup_hostname<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    addr: fnet::IpAddress,
) -> Result<String, fnet::LookupError> {
    let net_ext::IpAddress(addr) = addr.into();
    let resolver = resolver.read();

    match resolver.reverse_lookup(addr).await {
        // TODO(chuningw): Revisit LookupHostname() method of namelookup.fidl.
        Ok(response) => {
            response.iter().next().ok_or(fnet::LookupError::NotFound).map(|h| h.to_string())
        }
        Err(error) => {
            fx_log_err!("failed to LookupHostname with error {}", error);
            Err(convert_err(error))
        }
    }
}

async fn run_name_lookup<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    stream: NameLookupRequestStream,
) -> Result<(), fidl::Error> {
    // TODO(fxb/45035):Limit the number of parallel requests to 1000.
    stream
        .try_for_each_concurrent(None, |request| async {
            match request {
                NameLookupRequest::LookupIp { hostname, options, responder } => {
                    responder.send(&mut handle_lookup_ip(resolver, hostname, options).await)
                }
                NameLookupRequest::LookupHostname { addr, responder } => {
                    responder.send(&mut handle_lookup_hostname(resolver, addr).await)
                }
            }
        })
        .await
}

/// Serves `stream` and forwards received configurations to `sink`.
async fn run_lookup_admin(
    sink: mpsc::Sender<dns::policy::ServerConfig>,
    policy_state: Arc<dns::policy::ServerConfigState>,
    stream: LookupAdminRequestStream,
) -> Result<(), anyhow::Error> {
    stream
        .try_filter_map(|req| async {
            match req {
                LookupAdminRequest::SetDefaultDnsServers { servers, responder } => {
                    let (mut response, ret) = if servers.iter().any(|s| {
                        let net_ext::IpAddress(ip) = s.clone().into();
                        ip.is_multicast() || ip.is_unspecified()
                    }) {
                        (Err(zx::Status::INVALID_ARGS.into_raw()), None)
                    } else {
                        (Ok(()), Some(dns::policy::ServerConfig::DefaultServers(servers)))
                    };
                    let () = responder.send(&mut response)?;
                    Ok(ret)
                }
                LookupAdminRequest::GetDnsServers { responder } => {
                    let () = responder.send(
                        &mut policy_state.consolidate_map(|s| s.clone().into()).into_iter(),
                    )?;
                    Ok(None)
                }
            }
        })
        .map_err(anyhow::Error::from)
        .forward(sink.sink_map_err(anyhow::Error::from))
        .await
}

/// Creates a tuple of [`mpsc::Sender`] and [`Future`] handled by
/// [`dns::policy::ServerConfiguration`].
///
/// The returned `sender` is used to publish configuration changes to a `Sink`
/// composed of [`dns::policy::ServerConfigPolicy`] and
/// [`SharedResolverConfigSink`], which will set the consolidated configuration
/// on `resolver`.
///
/// Configuration changes are only when the returned `Future` is polled.
fn create_policy_fut<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    config_state: Arc<dns::policy::ServerConfigState>,
) -> (
    mpsc::Sender<dns::policy::ServerConfig>,
    impl futures::Future<Output = Result<(), anyhow::Error>> + '_,
) {
    // Create configuration channel pair. A small buffer in the channel allows
    // for multiple configurations coming in rapidly to be flushed together.
    let (servers_config_sink, servers_config_source) = mpsc::channel(10);
    let policy = ServerConfigPolicy::new_with_state(
        SharedResolverConfigSink::new(&resolver)
            .sink_map_err(never::Never::into_any::<anyhow::Error>),
        config_state,
    );
    let policy_fut = servers_config_source
        .map(Ok)
        .forward(policy)
        .map_err(|e| anyhow::anyhow!("Sink error {:?}", e));
    (servers_config_sink, policy_fut)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fx_syslog::init_with_tags(&["dns_resolver"]).expect("cannot init logger");
    fx_log_info!("dns_resolver started");

    // Creates a handle to a thead-local executor which is used to spawn a background
    // task to resolve DNS requests.
    let handle = Handle::new(fasync::EHandle::local());

    let mut resolver_opts = ResolverOpts::default();
    // Resolver will query for A and AAAA in parallel for lookup_ip.
    resolver_opts.ip_strategy = LookupIpStrategy::Ipv4AndIpv6;
    let resolver = SharedResolver::new(
        Resolver::new(ResolverConfig::default(), resolver_opts, handle)
            .await
            .expect("failed to create resolver"),
    );

    let config_state = Arc::new(dns::policy::ServerConfigState::new());
    let (servers_config_sink, policy_fut) = create_policy_fut(&resolver, config_state.clone());

    let dns_watcher = {
        let stack =
            fuchsia_component::client::connect_to_service::<fidl_fuchsia_net_stack::StackMarker>()?;
        let (proxy, req) = fidl::endpoints::create_proxy::<fname::DnsServerWatcherMarker>()?;
        let () = stack.get_dns_server_watcher(req)?;
        dns::util::new_dns_server_stream(proxy)
    };

    let dns_watcher_fut = dns_watcher
        .map_ok(|servers| dns::policy::ServerConfig::DynamicServers(servers))
        .map_err(anyhow::Error::from)
        .forward(servers_config_sink.clone().sink_map_err(anyhow::Error::from));

    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(IncomingRequest::NameLookup)
        .add_fidl_service(IncomingRequest::LookupAdmin);
    fs.take_and_serve_directory_handle()?;

    let serve_fut = fs
        .for_each_concurrent(None, |incoming_service| async {
            match incoming_service {
                IncomingRequest::LookupAdmin(stream) => {
                    run_lookup_admin(servers_config_sink.clone(), config_state.clone(), stream)
                        .await
                        .unwrap_or_else(|e| {
                            fx_log_err!("run_lookup_admin finished with error: {:?}", e)
                        })
                }
                IncomingRequest::NameLookup(stream) => {
                    run_name_lookup(&resolver, stream).await.unwrap_or_else(|e| {
                        fx_log_err!("run_name_lookup finished with error: {:?}", e)
                    })
                }
            }
        })
        .map(Ok);

    let ((), (), ()) = futures::future::try_join3(policy_fut, serve_fut, dns_watcher_fut).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use dns::DEFAULT_PORT;
    use std::net::SocketAddr;
    use std::{
        net::{IpAddr, Ipv4Addr, Ipv6Addr},
        str::FromStr,
        sync::Arc,
    };
    use trust_dns_proto::{
        op::Query,
        rr::{Name, RData, Record},
    };
    use trust_dns_resolver::{
        lookup::Ipv4Lookup, lookup::Ipv6Lookup, lookup::Lookup, lookup::ReverseLookup,
        lookup_ip::LookupIp,
    };

    const IPV4_LOOPBACK: [u8; 4] = [127, 0, 0, 1];
    const IPV6_LOOPBACK: [u8; 16] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1];
    const LOCAL_HOST: &str = "localhost.";

    // IPv4 address returned by mock lookup.
    const IPV4_HOST: Ipv4Addr = Ipv4Addr::new(240, 0, 0, 2);
    // IPv6 address returned by mock lookup.
    const IPV6_HOST: Ipv6Addr = Ipv6Addr::new(0xABCD, 0, 0, 0, 0, 0, 0, 2);

    const IPV4_NAMESERVER: IpAddr = IpAddr::V4(Ipv4Addr::new(1, 8, 8, 1));
    const IPV6_NAMESERVER: IpAddr = IpAddr::V6(Ipv6Addr::new(1, 4860, 4860, 0, 0, 0, 0, 1));

    // host which has IPv4 address only.
    const REMOTE_IPV4_HOST: &str = "www.foo.com";
    // host which has IPv6 address only.
    const REMOTE_IPV6_HOST: &str = "www.bar.com";
    // host used in reverse_lookup when multiple hostnames are returned.
    const REMOTE_IPV6_HOST_EXTRA: &str = "www.bar2.com";
    // host which has IPv4 and IPv6 address if reset name servers.
    const REMOTE_IPV4_IPV6_HOST: &str = "www.foobar.com";

    async fn setup_namelookup_service() -> fnet::NameLookupProxy {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fnet::NameLookupMarker>()
            .expect("failed to create NamelookupProxy");

        // Creates a handle to a thead-local executor which is used to spawn a background
        // task to resolve DNS requests.
        let handle = Handle::new(fasync::EHandle::local());

        let mut resolver_opts = ResolverOpts::default();
        // Resolver will query for A and AAAA in parallel for lookup_ip.
        resolver_opts.ip_strategy = LookupIpStrategy::Ipv4AndIpv6;

        let resolver = SharedResolver::new(
            Resolver::new(ResolverConfig::default(), resolver_opts, handle)
                .await
                .expect("failed to create resolver"),
        );

        fasync::spawn_local(async move {
            let () = run_name_lookup(&resolver, stream).await.expect("failed to run_name_lookup");
        });
        proxy
    }

    async fn check_lookup_ip(
        proxy: &fnet::NameLookupProxy,
        host: &str,
        option: fnet::LookupIpOptions,
        expected: Result<fnet::IpAddressInfo, fnet::LookupError>,
    ) {
        let res = proxy.lookup_ip(host, option).await.expect("failed to lookup ip");
        assert_eq!(res, expected);
    }

    async fn check_lookup_hostname(
        proxy: &fnet::NameLookupProxy,
        mut addr: fnet::IpAddress,
        expected: Result<String, fnet::LookupError>,
    ) {
        let res = proxy.lookup_hostname(&mut addr).await.expect("failed to lookup hostname");
        assert_eq!(res, expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_invalid_option() {
        let proxy = setup_namelookup_service().await;

        // IP Lookup localhost with invalid option.
        let res = proxy
            .lookup_ip(LOCAL_HOST, fnet::LookupIpOptions::CnameLookup)
            .await
            .expect("failed to LookupIp");
        assert_eq!(res, Err(fnet::LookupError::InvalidArgs));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_localhost() {
        let proxy = setup_namelookup_service().await;

        // IP Lookup IPv4 and IPv6 for localhost.
        check_lookup_ip(
            &proxy,
            LOCAL_HOST,
            fnet::LookupIpOptions::V4Addrs | fnet::LookupIpOptions::V6Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_LOOPBACK }],
                ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_LOOPBACK }],
                canonical_name: None,
            }),
        )
        .await;

        // IP Lookup IPv4 only for localhost.
        check_lookup_ip(
            &proxy,
            LOCAL_HOST,
            fnet::LookupIpOptions::V4Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_LOOPBACK }],
                ipv6_addrs: vec![],
                canonical_name: None,
            }),
        )
        .await;

        // IP Lookup IPv6 only for localhost.
        check_lookup_ip(
            &proxy,
            LOCAL_HOST,
            fnet::LookupIpOptions::V6Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![],
                ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_LOOPBACK }],
                canonical_name: None,
            }),
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookuphostname_localhost() {
        let proxy = setup_namelookup_service().await;
        check_lookup_hostname(
            &proxy,
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: IPV4_LOOPBACK }),
            Ok(String::from(LOCAL_HOST)),
        )
        .await;
    }

    struct MockResolver {
        config: ResolverConfig,
    }

    impl MockResolver {
        fn ip_lookup<N: IntoName + Send>(&self, host: N) -> Lookup {
            let rdatas = match host.into_name().unwrap().to_utf8().as_str() {
                REMOTE_IPV4_HOST => vec![RData::A(IPV4_HOST)],
                REMOTE_IPV6_HOST => vec![RData::AAAA(IPV6_HOST)],
                REMOTE_IPV4_IPV6_HOST => vec![RData::A(IPV4_HOST), RData::AAAA(IPV6_HOST)],
                _ => vec![],
            };

            let records: Vec<Record> = rdatas
                .into_iter()
                .map(|rdata| {
                    Record::from_rdata(
                        Name::new(),
                        // The following ttl value is taken arbitrarily and does not matter in the
                        // test.
                        60,
                        rdata,
                    )
                })
                .collect();

            Lookup::new_with_max_ttl(Query::default(), Arc::new(records))
        }
    }

    #[async_trait]
    impl ResolverLookup for MockResolver {
        async fn new(config: ResolverConfig, _options: ResolverOpts) -> Self {
            MockResolver { config }
        }

        async fn lookup_ip<N: IntoName + TryParseIp + Send>(
            &self,
            host: N,
        ) -> Result<lookup_ip::LookupIp, ResolveError> {
            Ok(LookupIp::from(self.ip_lookup(host)))
        }

        async fn ipv4_lookup<N: IntoName + Send>(
            &self,
            host: N,
        ) -> Result<lookup::Ipv4Lookup, ResolveError> {
            Ok(Ipv4Lookup::from(self.ip_lookup(host)))
        }

        async fn ipv6_lookup<N: IntoName + Send>(
            &self,
            host: N,
        ) -> Result<lookup::Ipv6Lookup, ResolveError> {
            Ok(Ipv6Lookup::from(self.ip_lookup(host)))
        }

        async fn reverse_lookup(
            &self,
            addr: IpAddr,
        ) -> Result<lookup::ReverseLookup, ResolveError> {
            let lookup = if addr == IPV4_HOST {
                Lookup::from_rdata(
                    Query::default(),
                    RData::PTR(Name::from_str(REMOTE_IPV4_HOST).unwrap()),
                )
            } else if addr == IPV6_HOST {
                Lookup::new_with_max_ttl(
                    Query::default(),
                    Arc::new(vec![
                        Record::from_rdata(
                            Name::new(),
                            60, // The value is taken arbitrarily and does not matter
                            // in the test.
                            RData::PTR(Name::from_str(REMOTE_IPV6_HOST).unwrap()),
                        ),
                        Record::from_rdata(
                            Name::new(),
                            60, // The value is taken arbitrarily and does not matter
                            // in the test.
                            RData::PTR(Name::from_str(REMOTE_IPV6_HOST_EXTRA).unwrap()),
                        ),
                    ]),
                )
            } else {
                Lookup::new_with_max_ttl(Query::default(), Arc::new(vec![]))
            };
            Ok(ReverseLookup::from(lookup))
        }
    }

    struct TestEnvironment {
        shared_resolver: SharedResolver<MockResolver>,
        config_state: Arc<dns::policy::ServerConfigState>,
    }

    impl TestEnvironment {
        fn new() -> Self {
            Self {
                shared_resolver: SharedResolver::new(MockResolver {
                    config: ResolverConfig::from_parts(
                        None,
                        vec![],
                        // Set name_servers as empty, so it's guaranteed to be different from IPV4_NAMESERVER
                        // and IPV6_NAMESERVER.
                        NameServerConfigGroup::with_capacity(0),
                    ),
                }),
                config_state: Arc::new(dns::policy::ServerConfigState::new()),
            }
        }

        async fn run_lookup<F, Fut>(&self, f: F)
        where
            Fut: futures::Future<Output = ()>,
            F: FnOnce(fnet::NameLookupProxy) -> Fut,
        {
            let (name_lookup_proxy, name_lookup_stream) =
                fidl::endpoints::create_proxy_and_stream::<fnet::NameLookupMarker>()
                    .expect("failed to create NameLookupProxy");

            let ((), ()) = futures::future::try_join(
                run_name_lookup(&self.shared_resolver, name_lookup_stream),
                f(name_lookup_proxy).map(Ok),
            )
            .await
            .expect("Error running lookup future");
        }

        async fn run_admin<F, Fut>(&self, f: F)
        where
            Fut: futures::Future<Output = ()>,
            F: FnOnce(fname::LookupAdminProxy) -> Fut,
        {
            let (lookup_admin_proxy, lookup_admin_stream) =
                fidl::endpoints::create_proxy_and_stream::<fname::LookupAdminMarker>()
                    .expect("failed to create AdminResolverProxy");

            let (sink, policy_fut) =
                create_policy_fut(&self.shared_resolver, self.config_state.clone());

            let ((), (), ()) = futures::future::try_join3(
                run_lookup_admin(sink, self.config_state.clone(), lookup_admin_stream),
                policy_fut,
                f(lookup_admin_proxy).map(Ok),
            )
            .await
            .expect("Error running admin future");
        }

        async fn run_config_sink<F, Fut>(&self, f: F)
        where
            Fut: futures::Future<Output = ()>,
            F: FnOnce(mpsc::Sender<dns::policy::ServerConfig>) -> Fut,
        {
            let (sink, policy_fut) =
                create_policy_fut(&self.shared_resolver, self.config_state.clone());

            let ((), ()) = futures::future::try_join(policy_fut, f(sink).map(Ok))
                .await
                .expect("Error running admin future");
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_remotehost_ipv4() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                // IP Lookup IPv4 and IPv6 for REMOTE_IPV4_HOST.
                check_lookup_ip(
                    &proxy,
                    REMOTE_IPV4_HOST,
                    fnet::LookupIpOptions::V4Addrs | fnet::LookupIpOptions::V6Addrs,
                    Ok(fnet::IpAddressInfo {
                        ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_HOST.octets() }],
                        ipv6_addrs: vec![],
                        canonical_name: None,
                    }),
                )
                .await;

                // IP Lookup IPv4 for REMOTE_IPV4_HOST.
                check_lookup_ip(
                    &proxy,
                    REMOTE_IPV4_HOST,
                    fnet::LookupIpOptions::V4Addrs,
                    Ok(fnet::IpAddressInfo {
                        ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_HOST.octets() }],
                        ipv6_addrs: vec![],
                        canonical_name: None,
                    }),
                )
                .await;

                // IP Lookup IPv6 for REMOTE_IPV4_HOST.
                check_lookup_ip(
                    &proxy,
                    REMOTE_IPV4_HOST,
                    fnet::LookupIpOptions::V6Addrs,
                    Err(fnet::LookupError::NotFound),
                )
                .await;
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_remotehost_ipv6() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                // IP Lookup IPv4 and IPv6 for REMOTE_IPV6_HOST.
                check_lookup_ip(
                    &proxy,
                    REMOTE_IPV6_HOST,
                    fnet::LookupIpOptions::V4Addrs | fnet::LookupIpOptions::V6Addrs,
                    Ok(fnet::IpAddressInfo {
                        ipv4_addrs: vec![],
                        ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_HOST.octets() }],
                        canonical_name: None,
                    }),
                )
                .await;

                // IP Lookup IPv4 for REMOTE_IPV6_HOST.
                check_lookup_ip(
                    &proxy,
                    REMOTE_IPV6_HOST,
                    fnet::LookupIpOptions::V4Addrs,
                    Err(fnet::LookupError::NotFound),
                )
                .await;

                // IP Lookup IPv6 for REMOTE_IPV4_HOST.
                check_lookup_ip(
                    &proxy,
                    REMOTE_IPV6_HOST,
                    fnet::LookupIpOptions::V6Addrs,
                    Ok(fnet::IpAddressInfo {
                        ipv4_addrs: vec![],
                        ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_HOST.octets() }],
                        canonical_name: None,
                    }),
                )
                .await;
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_hostname() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                check_lookup_hostname(
                    &proxy,
                    fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: IPV4_HOST.octets() }),
                    Ok(String::from(REMOTE_IPV4_HOST)),
                )
                .await;
            })
            .await;
    }

    // Multiple hostnames returned from trust-dns* APIs, and only the first one will be returned
    // by the FIDL.
    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_hostname_multi() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                check_lookup_hostname(
                    &proxy,
                    fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: IPV6_HOST.octets() }),
                    Ok(String::from(REMOTE_IPV6_HOST)),
                )
                .await;
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_server_names() {
        let env = TestEnvironment::new();
        // Assert that mock config has no servers originally.

        assert_eq!(env.shared_resolver.read().config.name_servers().to_vec(), vec![]);

        env.run_admin(|proxy| async move {
            let v4: fnet::IpAddress = net_ext::IpAddress(IPV4_NAMESERVER).into();
            let v6: fnet::IpAddress = net_ext::IpAddress(IPV6_NAMESERVER).into();
            let () = proxy
                .set_default_dns_servers(&mut vec![v4, v6].iter_mut())
                .await
                .expect("Failed to call SetDefaultDnsServers")
                .expect("SetDefaultDnsServers error");
        })
        .await;

        let to_server_configs = |addr: IpAddr| -> [NameServerConfig; 2] {
            let socket_addr = SocketAddr::new(addr, DEFAULT_PORT);
            [
                NameServerConfig { socket_addr, protocol: Protocol::Udp, tls_dns_name: None },
                NameServerConfig { socket_addr, protocol: Protocol::Tcp, tls_dns_name: None },
            ]
        };

        assert_eq!(
            env.shared_resolver.read().config.name_servers().to_vec(),
            to_server_configs(IPV4_NAMESERVER)
                .iter()
                .chain(to_server_configs(IPV6_NAMESERVER).iter())
                .cloned()
                .collect::<Vec<_>>()
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_server_names_error() {
        let env = TestEnvironment::new();
        // Assert that mock config has no servers originally.
        assert_eq!(env.shared_resolver.read().config.name_servers().to_vec(), vec![]);

        env.run_admin(|proxy| async move {
            // Attempt to set bad addresses.

            // Multicast not allowed.
            let status = proxy
                .set_default_dns_servers(
                    &mut vec![fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [224, 0, 0, 1] })]
                        .iter_mut(),
                )
                .await
                .expect("Failed to call SetDefaultDnsServers")
                .expect_err("SetDefaultDnsServers should fail for multicast address");
            assert_eq!(zx::Status::from_raw(status), zx::Status::INVALID_ARGS);

            // Unspecified not allowed.
            let status = proxy
                .set_default_dns_servers(
                    &mut vec![fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: [0u8; 16] })]
                        .iter_mut(),
                )
                .await
                .expect("Failed to call SetDefaultDnsServers")
                .expect_err("SetDefaultDnsServers should fail for unspecified address");
            assert_eq!(zx::Status::from_raw(status), zx::Status::INVALID_ARGS);
        })
        .await;

        // Assert that config didn't change.
        assert_eq!(env.shared_resolver.read().config.name_servers().to_vec(), vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_servers() {
        const STATIC_SERVER_IPV4: fnet::Ipv4Address = fnet::Ipv4Address { addr: [8, 8, 8, 8] };
        const STATIC_SERVER: fnet::IpAddress = fnet::IpAddress::Ipv4(STATIC_SERVER_IPV4);

        const DHCP_SERVER: fname::DnsServer_ = fname::DnsServer_ {
            address: Some(fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
                address: fnet::Ipv4Address { addr: [8, 8, 4, 4] },
                port: DEFAULT_PORT,
            })),
            source: Some(fname::DnsServerSource::Dhcp(fname::DhcpDnsServerSource {
                source_interface: Some(1),
            })),
        };
        const NDP_SERVER: fname::DnsServer_ = fname::DnsServer_ {
            address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                address: fnet::Ipv6Address {
                    addr: [
                        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x44, 0x44,
                    ],
                },
                port: DEFAULT_PORT,
                zone_index: 2,
            })),
            source: Some(fname::DnsServerSource::Ndp(fname::NdpDnsServerSource {
                source_interface: Some(2),
            })),
        };

        let env = TestEnvironment::new();
        env.run_config_sink(|mut sink| async move {
            let () = sink
                .send(dns::policy::ServerConfig::DefaultServers(vec![STATIC_SERVER]))
                .await
                .unwrap();
            let () = sink
                .send(dns::policy::ServerConfig::DynamicServers(vec![NDP_SERVER, DHCP_SERVER]))
                .await
                .unwrap();
        })
        .await;

        env.run_admin(|proxy| async move {
            let servers = proxy.get_dns_servers().await.expect("Failed to get DNS servers");
            assert_eq!(
                servers,
                vec![
                    NDP_SERVER,
                    DHCP_SERVER,
                    fname::DnsServer_ {
                        address: Some(fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
                            address: STATIC_SERVER_IPV4,
                            port: DEFAULT_PORT
                        })),
                        source: Some(fname::DnsServerSource::StaticSource(
                            fname::StaticDnsServerSource {}
                        )),
                    }
                ]
            )
        })
        .await;
    }
}

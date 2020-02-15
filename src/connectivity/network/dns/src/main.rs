// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    dns::async_resolver::{Handle, Resolver},
    fidl_fuchsia_net::{self as fnet, NameLookupRequest, NameLookupRequestStream},
    fidl_fuchsia_net_ext::IpAddress,
    fidl_fuchsia_netstack::{ResolverAdminRequest, ResolverAdminRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as fx_syslog, fx_log_err, fx_log_info},
    futures::{StreamExt, TryStreamExt},
    parking_lot::RwLock,
    std::{net::IpAddr, rc::Rc},
    trust_dns_proto::rr::{domain::IntoName, TryParseIp},
    trust_dns_resolver::{
        config::{LookupIpStrategy, NameServerConfigGroup, ResolverConfig, ResolverOpts},
        error::ResolveError,
        lookup, lookup_ip,
    },
};

const DNS_PORT_NUM: u16 = 53;

struct SharedResolver<T>(RwLock<Rc<T>>);

// NOTE: The following cannot be async fn, as while holding a RwLock, awaiting on a future will
// cause deadlocks.
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

enum IncomingRequest {
    // NameLookup service.
    NameLookup(NameLookupRequestStream),
    // ResolverAdmin Service.
    ResolverAdmin(ResolverAdminRequestStream),
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
                .map(|addrs| addrs.iter().map(|addr| IpAddress(addr).into()).collect())
        } else if options.contains(fnet::LookupIpOptions::V4Addrs) {
            resolver
                .ipv4_lookup(hostname)
                .await
                .map(|addrs| addrs.iter().map(|addr| IpAddress(IpAddr::V4(*addr)).into()).collect())
        } else if options.contains(fnet::LookupIpOptions::V6Addrs) {
            resolver
                .ipv6_lookup(hostname)
                .await
                .map(|addrs| addrs.iter().map(|addr| IpAddress(IpAddr::V6(*addr)).into()).collect())
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
    let IpAddress(addr) = addr.into();
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

async fn run_namelookup<T: ResolverLookup>(
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

async fn handle_set_server_names<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    servers: Vec<fnet::IpAddress>,
) -> Result<(), fidl::Error> {
    let servers: Vec<IpAddr> = servers
        .into_iter()
        .map(|addr| {
            let IpAddress(addr) = addr.into();
            addr
        })
        .collect();
    // TODO (chunyingw): Ideally the configuration of the existing resolver instance could be
    // updated directly w/o initializing a new resolver.
    let mut resolver_opts = ResolverOpts::default();
    resolver_opts.ip_strategy = LookupIpStrategy::Ipv4AndIpv6;
    let name_servers = NameServerConfigGroup::from_ips_clear(&servers, DNS_PORT_NUM);
    let new_resolver = Rc::new(
        T::new(ResolverConfig::from_parts(None, vec![], name_servers), resolver_opts).await,
    );
    resolver.write(new_resolver);
    Ok(())
}

async fn run_resolveradmin<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    stream: ResolverAdminRequestStream,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each_concurrent(None, |request| async {
            match request {
                ResolverAdminRequest::SetNameServers { servers, control_handle: _ } => {
                    handle_set_server_names(resolver, servers).await
                }
            }
        })
        .await
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

    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(IncomingRequest::NameLookup)
        .add_fidl_service(IncomingRequest::ResolverAdmin);
    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(None, |incoming_service| async {
        match incoming_service {
            IncomingRequest::ResolverAdmin(stream) => run_resolveradmin(&resolver, stream)
                .await
                .unwrap_or_else(|e| fx_log_err!("run_resolveradmin finished with error: {:?}", e)),
            IncomingRequest::NameLookup(stream) => run_namelookup(&resolver, stream)
                .await
                .unwrap_or_else(|e| fx_log_err!("run_namelookup finished with error: {:?}", e)),
        }
    })
    .await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_netstack as fnetstack;
    use std::{net::Ipv4Addr, net::Ipv6Addr, str::FromStr, sync::Arc};
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

    // IPv4 address returned if the name server is IPV4_NAMESERVER.
    const IPV4_HOST_W_SERVER: Ipv4Addr = Ipv4Addr::new(240, 0, 0, 1);
    // IPv4 address returned if the name server is not IPV4_NAMESERVER.
    const IPV4_HOST_WO_SERVER: Ipv4Addr = Ipv4Addr::new(240, 0, 0, 2);
    // IPv6 address returned if the name servers is IPV6_NAMESERVER.
    const IPV6_HOST_W_SERVER: Ipv6Addr = Ipv6Addr::new(0xABCD, 0, 0, 0, 0, 0, 0, 1);
    // IPv6 address returned if the name server is not IPV6_NAMESERVER.
    const IPV6_HOST_WO_SERVER: Ipv6Addr = Ipv6Addr::new(0xABCD, 0, 0, 0, 0, 0, 0, 2);
    const IPV4_NAMESERVER: Ipv4Addr = Ipv4Addr::new(1, 8, 8, 1);
    const IPV6_NAMESERVER: Ipv6Addr = Ipv6Addr::new(1, 4860, 4860, 0, 0, 0, 0, 1);

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
            let () = run_namelookup(&resolver, stream).await.expect("failed to run_namelookup");
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

    async fn check_set_name_servers(
        name_lookup_proxy: &fnet::NameLookupProxy,
        resolver_admin_proxy: &fnetstack::ResolverAdminProxy,
        mut name_servers: Vec<fnet::IpAddress>,
        host: &str,
        option: fnet::LookupIpOptions,
        expected: Result<fnet::IpAddressInfo, fnet::LookupError>,
    ) {
        resolver_admin_proxy
            .set_name_servers(&mut name_servers.iter_mut())
            .expect("failed to set name servers");
        // Test set_server_names by checking the result of lookup ip.
        check_lookup_ip(&name_lookup_proxy, host, option, expected).await;
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

    struct MockResolver(pub ResolverConfig);

    impl MockResolver {
        fn name_servers_equality(&self, servers: &Vec<IpAddr>) -> bool {
            let name_servers = NameServerConfigGroup::from_ips_clear(&servers, DNS_PORT_NUM);
            if name_servers.len() != self.0.name_servers().len() {
                return false;
            }
            name_servers.iter().zip(self.0.name_servers().iter()).all(|(a, b)| a == b)
        }

        fn ip_lookup<N: IntoName + Send>(&self, host: N) -> Lookup {
            let rdatas = match host.into_name().unwrap().to_utf8().as_str() {
                REMOTE_IPV4_HOST => {
                    if self.name_servers_equality(&vec![IpAddr::V4(IPV4_NAMESERVER)]) {
                        vec![RData::A(IPV4_HOST_W_SERVER)]
                    } else {
                        vec![RData::A(IPV4_HOST_WO_SERVER)]
                    }
                }
                REMOTE_IPV6_HOST => {
                    if self.name_servers_equality(&vec![IpAddr::V6(IPV6_NAMESERVER)]) {
                        vec![RData::AAAA(IPV6_HOST_W_SERVER)]
                    } else {
                        vec![RData::AAAA(IPV6_HOST_WO_SERVER)]
                    }
                }
                REMOTE_IPV4_IPV6_HOST => {
                    if self.name_servers_equality(&vec![
                        IpAddr::V4(IPV4_NAMESERVER),
                        IpAddr::V6(IPV6_NAMESERVER),
                    ]) {
                        vec![RData::A(IPV4_HOST_W_SERVER), RData::AAAA(IPV6_HOST_W_SERVER)]
                    } else {
                        vec![RData::A(IPV4_HOST_WO_SERVER), RData::AAAA(IPV6_HOST_WO_SERVER)]
                    }
                }
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
            MockResolver(config)
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
            let lookup = if addr == IPV4_HOST_WO_SERVER {
                Lookup::from_rdata(
                    Query::default(),
                    RData::PTR(Name::from_str(REMOTE_IPV4_HOST).unwrap()),
                )
            } else if addr == IPV6_HOST_WO_SERVER {
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

    fn setup_services_with_mock_resolver() -> (fnet::NameLookupProxy, fnetstack::ResolverAdminProxy)
    {
        let (name_lookup_proxy, name_lookup_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet::NameLookupMarker>()
                .expect("failed to create NameLookupProxy");
        let (resolver_admin_proxy, resolver_admin_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnetstack::ResolverAdminMarker>()
                .expect("failed to create AdminResolverProxy");

        let mock_resolver = SharedResolver::new(MockResolver(ResolverConfig::from_parts(
            None,
            vec![],
            // Set name_servers as empty, so it's guaranteed to be different from IPV4_NAMESERVER
            // and IPV6_NAMESERVER.
            NameServerConfigGroup::with_capacity(0),
        )));

        fasync::spawn_local(async move {
            let name_lookup_fut = run_namelookup(&mock_resolver, name_lookup_stream);
            let resolver_admin_fut = run_resolveradmin(&mock_resolver, resolver_admin_stream);
            let (resolver_admin, name_lookup) =
                futures::future::join(resolver_admin_fut, name_lookup_fut).await;
            name_lookup.expect("failed to run_namelookup");
            resolver_admin.expect("failed to run_adminresolver");
        });

        (name_lookup_proxy, resolver_admin_proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_remotehost_ipv4() {
        let (proxy, _) = setup_services_with_mock_resolver();

        // IP Lookup IPv4 and IPv6 for REMOTE_IPV4_HOST.
        check_lookup_ip(
            &proxy,
            REMOTE_IPV4_HOST,
            fnet::LookupIpOptions::V4Addrs | fnet::LookupIpOptions::V6Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_HOST_WO_SERVER.octets() }],
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
                ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_HOST_WO_SERVER.octets() }],
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
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_remotehost_ipv6() {
        let (proxy, _) = setup_services_with_mock_resolver();

        // IP Lookup IPv4 and IPv6 for REMOTE_IPV6_HOST.
        check_lookup_ip(
            &proxy,
            REMOTE_IPV6_HOST,
            fnet::LookupIpOptions::V4Addrs | fnet::LookupIpOptions::V6Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![],
                ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_HOST_WO_SERVER.octets() }],
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
                ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_HOST_WO_SERVER.octets() }],
                canonical_name: None,
            }),
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_hostname() {
        let (proxy, _) = setup_services_with_mock_resolver();

        check_lookup_hostname(
            &proxy,
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: IPV4_HOST_WO_SERVER.octets() }),
            Ok(String::from(REMOTE_IPV4_HOST)),
        )
        .await;
    }

    // Multiple hostnames returned from trust-dns* APIs, and only the first one will be returned
    // by the FIDL.
    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_hostname_multi() {
        let (proxy, _) = setup_services_with_mock_resolver();

        check_lookup_hostname(
            &proxy,
            fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: IPV6_HOST_WO_SERVER.octets() }),
            Ok(String::from(REMOTE_IPV6_HOST)),
        )
        .await;
    }

    // dns::async_resolver::Resolver does not expose ResolverConfig info, so the MockResolver is
    // used for testing.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_server_names_ipv4() {
        let (name_lookup_proxy, resolver_admin_proxy) = setup_services_with_mock_resolver();
        check_set_name_servers(
            &name_lookup_proxy,
            &resolver_admin_proxy,
            vec![fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: IPV4_NAMESERVER.octets() })],
            REMOTE_IPV4_HOST,
            fnet::LookupIpOptions::V4Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_HOST_W_SERVER.octets() }],
                ipv6_addrs: vec![],
                canonical_name: None,
            }),
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_server_names_ipv6() {
        let (name_lookup_proxy, resolver_admin_proxy) = setup_services_with_mock_resolver();
        check_set_name_servers(
            &name_lookup_proxy,
            &resolver_admin_proxy,
            vec![fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: IPV6_NAMESERVER.octets() })],
            REMOTE_IPV6_HOST,
            fnet::LookupIpOptions::V6Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![],
                ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_HOST_W_SERVER.octets() }],
                canonical_name: None,
            }),
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_server_names_ipv4_ipv6() {
        let (name_lookup_proxy, resolver_admin_proxy) = setup_services_with_mock_resolver();
        check_set_name_servers(
            &name_lookup_proxy,
            &resolver_admin_proxy,
            vec![
                fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: IPV4_NAMESERVER.octets() }),
                fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: IPV6_NAMESERVER.octets() }),
            ],
            REMOTE_IPV4_IPV6_HOST,
            fnet::LookupIpOptions::V4Addrs | fnet::LookupIpOptions::V6Addrs,
            Ok(fnet::IpAddressInfo {
                ipv4_addrs: vec![fnet::Ipv4Address { addr: IPV4_HOST_W_SERVER.octets() }],
                ipv6_addrs: vec![fnet::Ipv6Address { addr: IPV6_HOST_W_SERVER.octets() }],
                canonical_name: None,
            }),
        )
        .await;
    }
}

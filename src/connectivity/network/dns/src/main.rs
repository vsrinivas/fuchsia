// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    async_trait::async_trait,
    dns::{
        async_resolver::{Resolver, Spawner},
        config::{ServerList, UpdateServersResult},
    },
    fidl_fuchsia_net as fnet, fidl_fuchsia_net_ext as net_ext,
    fidl_fuchsia_net_name::{
        self as fname, LookupAdminRequest, LookupAdminRequestStream, LookupRequest,
        LookupRequestStream,
    },
    fidl_fuchsia_net_routes as fnet_routes, fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    fuchsia_inspect, fuchsia_zircon as zx,
    futures::{
        channel::mpsc, lock::Mutex, FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _,
    },
    net_declare::fidl_ip_v6,
    net_types::ip::IpAddress,
    parking_lot::RwLock,
    std::collections::{BTreeMap, HashMap, VecDeque},
    std::convert::TryFrom as _,
    std::hash::{Hash, Hasher},
    std::net::IpAddr,
    std::num::NonZeroUsize,
    std::rc::Rc,
    std::str::FromStr as _,
    std::sync::Arc,
    tracing::{debug, error, info, warn},
    trust_dns_proto::{
        op::ResponseCode,
        rr::{domain::IntoName, RData, RecordType},
    },
    trust_dns_resolver::{
        config::{
            LookupIpStrategy, NameServerConfig, NameServerConfigGroup, Protocol, ResolverConfig,
            ResolverOpts, ServerOrderingStrategy,
        },
        error::{ResolveError, ResolveErrorKind},
        lookup,
    },
    unicode_xid::UnicodeXID as _,
};

struct SharedResolver<T>(RwLock<Rc<T>>);

impl<T> SharedResolver<T> {
    fn new(resolver: T) -> Self {
        SharedResolver(RwLock::new(Rc::new(resolver)))
    }

    fn read(&self) -> Rc<T> {
        let Self(inner) = self;
        inner.read().clone()
    }

    fn write(&self, other: Rc<T>) {
        let Self(inner) = self;
        *inner.write() = other;
    }
}

const STAT_WINDOW_DURATION: zx::Duration = zx::Duration::from_seconds(60);
const STAT_WINDOW_COUNT: usize = 30;

/// Stats about queries during the last `STAT_WINDOW_COUNT` windows of
/// `STAT_WINDOW_DURATION` time.
///
/// For example, if `STAT_WINDOW_DURATION` == 1 minute, and
/// `STAT_WINDOW_COUNT` == 30, `past_queries` contains information about, at
/// most, 30 one-minute windows of completed queries.
///
/// NB: there is no guarantee that these windows are directly consecutive; only
/// that each window begins at least `STAT_WINDOW_DURATION` after the previous
/// window's start time.
struct QueryStats {
    inner: Mutex<VecDeque<QueryWindow>>,
}

/// Relevant info to be recorded about a completed query. The `Ok` variant
/// contains the number of addresses in the response, and the `Err` variant
/// contains the kind of error that was encountered.
type QueryResult<'a> = Result<NonZeroUsize, &'a ResolveErrorKind>;

impl QueryStats {
    fn new() -> Self {
        Self { inner: Mutex::new(VecDeque::new()) }
    }

    async fn finish_query(&self, start_time: fasync::Time, result: QueryResult<'_>) {
        let end_time = fasync::Time::now();
        let finish = move |window: &mut QueryWindow| {
            let elapsed_time = end_time - start_time;
            match result {
                Ok(num_addrs) => window.succeed(elapsed_time, num_addrs),
                Err(e) => window.fail(elapsed_time, e),
            }
        };

        let Self { inner } = self;
        let past_queries = &mut *inner.lock().await;

        let current_window = past_queries.back_mut().and_then(|window| {
            let QueryWindow { start, .. } = window;
            (end_time - *start < STAT_WINDOW_DURATION).then(|| window)
        });

        match current_window {
            Some(window) => finish(window),
            None => {
                if past_queries.len() == STAT_WINDOW_COUNT {
                    // Remove the oldest window of query stats.
                    let _: QueryWindow = past_queries
                        .pop_front()
                        .expect("there should be at least one element in `past_queries`");
                }
                let mut window = QueryWindow::new(end_time);
                finish(&mut window);
                past_queries.push_back(window);
            }
        }
    }
}

#[derive(Debug)]
struct HashableResponseCode {
    response_code: ResponseCode,
}

impl Hash for HashableResponseCode {
    fn hash<H: Hasher>(&self, state: &mut H) {
        let HashableResponseCode { response_code } = self;
        u16::from(*response_code).hash(state)
    }
}

// Hand-implemented because of clippy's derive_hash_xor_eq lint.
impl PartialEq for HashableResponseCode {
    fn eq(&self, other: &Self) -> bool {
        let HashableResponseCode { response_code } = self;
        let HashableResponseCode { response_code: other } = other;
        response_code.eq(other)
    }
}

impl Eq for HashableResponseCode {}

impl From<ResponseCode> for HashableResponseCode {
    fn from(response_code: ResponseCode) -> Self {
        HashableResponseCode { response_code }
    }
}

#[derive(Default, Debug, PartialEq)]
struct NoRecordsFoundStats {
    response_code_counts: HashMap<HashableResponseCode, u64>,
}

impl NoRecordsFoundStats {
    fn increment(&mut self, response_code: &ResponseCode) {
        let NoRecordsFoundStats { response_code_counts } = self;
        let count = response_code_counts.entry((*response_code).into()).or_insert(0);
        *count += 1
    }
}

#[derive(Default, Debug, PartialEq)]
struct UnhandledResolveErrorKindStats {
    resolve_error_kind_counts: HashMap<String, u64>,
}

impl UnhandledResolveErrorKindStats {
    fn increment(&mut self, resolve_error_kind: &ResolveErrorKind) {
        let Self { resolve_error_kind_counts } = self;
        // We just want to keep the part of the debug string that indicates
        // which enum variant this is.
        // See https://doc.rust-lang.org/reference/identifiers.html
        let truncated_debug = {
            let debug = format!("{:?}", resolve_error_kind);
            match debug.find(|c: char| !c.is_xid_continue() && !c.is_xid_start()) {
                Some(i) => debug[..i].to_string(),
                None => debug,
            }
        };
        let count = resolve_error_kind_counts.entry(truncated_debug).or_insert(0);
        *count += 1
    }
}

/// Stats about queries that failed due to an internal trust-dns error.
/// These counters map to variants of
/// [`trust_dns_resolver::error::ResolveErrorKind`].
#[derive(Default, Debug, PartialEq)]
struct FailureStats {
    message: u64,
    no_connections: u64,
    no_records_found: NoRecordsFoundStats,
    io: u64,
    proto: u64,
    timeout: u64,
    unhandled_resolve_error_kind: UnhandledResolveErrorKindStats,
}

impl FailureStats {
    fn increment(&mut self, kind: &ResolveErrorKind) {
        let FailureStats {
            message,
            no_connections,
            no_records_found,
            io,
            proto,
            timeout,
            unhandled_resolve_error_kind,
        } = self;

        match kind {
            ResolveErrorKind::Message(error) => {
                let _: &str = error;
                *message += 1
            }
            ResolveErrorKind::Msg(error) => {
                let _: &String = error;
                *message += 1
            }
            ResolveErrorKind::NoConnections => *no_connections += 1,
            ResolveErrorKind::NoRecordsFound {
                query: _,
                soa: _,
                negative_ttl: _,
                response_code,
                trusted: _,
            } => no_records_found.increment(response_code),
            ResolveErrorKind::Io(error) => {
                let _: &std::io::Error = error;
                *io += 1
            }
            ResolveErrorKind::Proto(error) => {
                let _: &trust_dns_proto::error::ProtoError = error;
                *proto += 1
            }
            ResolveErrorKind::Timeout => *timeout += 1,
            // ResolveErrorKind is marked #[non_exhaustive] in trust-dns:
            // https://github.com/bluejekyll/trust-dns/blob/v0.21.0-alpha.1/crates/resolver/src/error.rs#L29
            // So we have to include a wildcard match.
            // TODO(https://github.com/rust-lang/rust/issues/89554): remove once
            // we're able to apply the non_exhaustive_omitted_patterns lint
            kind => {
                error!("unhandled variant {:?}", kind);
                unhandled_resolve_error_kind.increment(kind)
            }
        }
    }
}

struct QueryWindow {
    start: fasync::Time,
    success_count: u64,
    failure_count: u64,
    success_elapsed_time: zx::Duration,
    failure_elapsed_time: zx::Duration,
    failure_stats: FailureStats,
    address_counts_histogram: BTreeMap<NonZeroUsize, u64>,
}

impl QueryWindow {
    fn new(start: fasync::Time) -> Self {
        Self {
            start,
            success_count: 0,
            failure_count: 0,
            success_elapsed_time: zx::Duration::from_nanos(0),
            failure_elapsed_time: zx::Duration::from_nanos(0),
            failure_stats: FailureStats::default(),
            address_counts_histogram: Default::default(),
        }
    }

    fn succeed(&mut self, elapsed_time: zx::Duration, num_addrs: NonZeroUsize) {
        let QueryWindow {
            success_count,
            success_elapsed_time,
            address_counts_histogram: address_counts,
            start: _,
            failure_count: _,
            failure_elapsed_time: _,
            failure_stats: _,
        } = self;
        *success_count += 1;
        *success_elapsed_time += elapsed_time;
        *address_counts.entry(num_addrs).or_default() += 1;
    }

    fn fail(&mut self, elapsed_time: zx::Duration, error: &ResolveErrorKind) {
        let QueryWindow {
            failure_count,
            failure_elapsed_time,
            failure_stats,
            start: _,
            success_count: _,
            success_elapsed_time: _,
            address_counts_histogram: _,
        } = self;
        *failure_count += 1;
        *failure_elapsed_time += elapsed_time;
        failure_stats.increment(error)
    }
}

fn update_resolver<T: ResolverLookup>(resolver: &SharedResolver<T>, servers: ServerList) {
    let mut resolver_opts = ResolverOpts::default();
    // TODO(https://fxbug.dev/102536): Set ip_strategy once a unified lookup API
    // exists that respects this setting.
    resolver_opts.num_concurrent_reqs = 10;
    // TODO(https://github.com/bluejekyll/trust-dns/issues/1702): Use the
    // default server ordering strategy once the algorithm is improved.
    resolver_opts.server_ordering_strategy = ServerOrderingStrategy::UserProvidedOrder;

    // We're going to add each server twice, once with protocol UDP and
    // then with protocol TCP.
    let mut name_servers = NameServerConfigGroup::with_capacity(servers.len() * 2);

    name_servers.extend(servers.into_iter().flat_map(|server| {
        let net_ext::SocketAddress(socket_addr) = server.into();
        // Every server config gets UDP and TCP versions with
        // preference for UDP.
        std::iter::once(NameServerConfig {
            socket_addr,
            protocol: Protocol::Udp,
            tls_dns_name: None,
            trust_nx_responses: false,
            bind_addr: None,
        })
        .chain(std::iter::once(NameServerConfig {
            socket_addr,
            protocol: Protocol::Tcp,
            tls_dns_name: None,
            trust_nx_responses: false,
            bind_addr: None,
        }))
    }));

    let new_resolver =
        T::new(ResolverConfig::from_parts(None, Vec::new(), name_servers), resolver_opts);
    let () = resolver.write(Rc::new(new_resolver));
}

enum IncomingRequest {
    Lookup(LookupRequestStream),
    LookupAdmin(LookupAdminRequestStream),
}

#[async_trait]
trait ResolverLookup {
    fn new(config: ResolverConfig, options: ResolverOpts) -> Self;

    async fn lookup<N: IntoName + Send>(
        &self,
        name: N,
        record_type: RecordType,
    ) -> Result<lookup::Lookup, ResolveError>;

    async fn reverse_lookup(&self, addr: IpAddr) -> Result<lookup::ReverseLookup, ResolveError>;
}

#[async_trait]
impl ResolverLookup for Resolver {
    fn new(config: ResolverConfig, options: ResolverOpts) -> Self {
        Resolver::new(config, options, Spawner).expect("failed to create resolver")
    }

    async fn lookup<N: IntoName + Send>(
        &self,
        name: N,
        record_type: RecordType,
    ) -> Result<lookup::Lookup, ResolveError> {
        self.lookup(name, record_type).await
    }

    async fn reverse_lookup(&self, addr: IpAddr) -> Result<lookup::ReverseLookup, ResolveError> {
        self.reverse_lookup(addr).await
    }
}

fn handle_err(source: &str, err: ResolveError) -> fname::LookupError {
    use trust_dns_proto::error::ProtoErrorKind;

    let (lookup_err, ioerr) = match err.kind() {
        // The following mapping is based on the analysis of `ResolveError` enumerations.
        // For cases that are not obvious such as `ResolveErrorKind::Msg` and
        // `ResolveErrorKind::Message`, I (chunyingw) did code searches to have more insights.
        // `ResolveErrorKind::Msg`: An error with arbitrary message, it could be ex. "lock was
        // poisoned, this is non-recoverable" and ""DNS Error".
        // `ResolveErrorKind::Message`: An error with arbitrary message, it is mostly returned when
        // there is no name in the input vector to look up with "can not lookup for no names".
        // This is a best-effort mapping.
        ResolveErrorKind::NoRecordsFound {
            query: _,
            soa: _,
            negative_ttl: _,
            response_code: _,
            trusted: _,
        } => (fname::LookupError::NotFound, None),
        ResolveErrorKind::Proto(err) => match err.kind() {
            ProtoErrorKind::DomainNameTooLong(_) | ProtoErrorKind::EdnsNameNotRoot(_) => {
                (fname::LookupError::InvalidArgs, None)
            }
            ProtoErrorKind::Busy | ProtoErrorKind::Canceled(_) | ProtoErrorKind::Timeout => {
                (fname::LookupError::Transient, None)
            }
            ProtoErrorKind::Io(inner) => (fname::LookupError::Transient, Some(inner)),
            ProtoErrorKind::BadQueryCount(_)
            | ProtoErrorKind::CharacterDataTooLong { max: _, len: _ }
            | ProtoErrorKind::LabelOverlapsWithOther { label: _, other: _ }
            | ProtoErrorKind::DnsKeyProtocolNot3(_)
            | ProtoErrorKind::FormError { header: _, error: _ }
            | ProtoErrorKind::HmacInvalid()
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
            | ProtoErrorKind::UnrecognizedCsyncFlags(_)
            | ProtoErrorKind::Poisoned
            | ProtoErrorKind::Ring(_)
            | ProtoErrorKind::SSL(_)
            | ProtoErrorKind::Timer
            | ProtoErrorKind::UrlParsing(_)
            | ProtoErrorKind::Utf8(_)
            | ProtoErrorKind::FromUtf8(_)
            | ProtoErrorKind::ParseInt(_) => (fname::LookupError::InternalError, None),
            // ProtoErrorKind is marked #[non_exhaustive] in trust-dns:
            // https://github.com/bluejekyll/trust-dns/blob/v0.21.0-alpha.1/crates/proto/src/error.rs#L66
            // So we have to include a wildcard match.
            kind => {
                error!("unhandled variant {:?}", kind);
                (fname::LookupError::InternalError, None)
            }
        },
        ResolveErrorKind::Io(inner) => (fname::LookupError::Transient, Some(inner)),
        ResolveErrorKind::Timeout => (fname::LookupError::Transient, None),
        ResolveErrorKind::Msg(_)
        | ResolveErrorKind::Message(_)
        | ResolveErrorKind::NoConnections => (fname::LookupError::InternalError, None),
        // ResolveErrorKind is marked #[non_exhaustive] in trust-dns:
        // https://github.com/bluejekyll/trust-dns/blob/v0.21.0-alpha.1/crates/resolver/src/error.rs#L29
        // So we have to include a wildcard match.
        kind => {
            error!("unhandled variant {:?}", kind);
            (fname::LookupError::InternalError, None)
        }
    };

    if let Some(ioerr) = ioerr {
        match ioerr.raw_os_error() {
            Some(libc::EHOSTUNREACH) => debug!("{} error: {}; (IO error {:?})", source, err, ioerr),
            _ => warn!("{} error: {}; (IO error {:?})", source, err, ioerr),
        }
    } else {
        warn!("{} error: {}", source, err)
    }

    lookup_err
}

async fn sort_preferred_addresses(
    mut addrs: Vec<fnet::IpAddress>,
    routes: &fnet_routes::StateProxy,
) -> Result<Vec<fnet::IpAddress>, fname::LookupError> {
    let mut addrs_info = futures::future::try_join_all(
        addrs
            // Drain addresses from addrs, but keep it alive so we don't need to
            // reallocate.
            .drain(..)
            .map(|mut addr| async move {
                let source_addr = match routes.resolve(&mut addr).await? {
                    Ok(fnet_routes::Resolved::Direct(fnet_routes::Destination {
                        source_address,
                        ..
                    }))
                    | Ok(fnet_routes::Resolved::Gateway(fnet_routes::Destination {
                        source_address,
                        ..
                    })) => source_address,
                    // If resolving routes returns an error treat it as an
                    // unreachable address.
                    Err(e) => {
                        debug!(
                            "fuchsia.net.routes/State.resolve({}) failed {}",
                            net_ext::IpAddress::from(addr),
                            zx::Status::from_raw(e)
                        );
                        None
                    }
                };
                Ok((addr, DasCmpInfo::from_addrs(&addr, source_addr.as_ref())))
            }),
    )
    .await
    .map_err(|e: fidl::Error| {
        warn!("fuchsia.net.routes/State.resolve FIDL error {:?}", e);
        fname::LookupError::InternalError
    })?;
    let () = addrs_info.sort_by(|(_laddr, left), (_raddr, right)| left.cmp(right));
    // Reinsert the addresses in order from addr_info.
    let () = addrs.extend(addrs_info.into_iter().map(|(addr, _)| addr));
    Ok(addrs)
}

#[derive(Debug)]
struct Policy {
    prefix: net_types::ip::Subnet<net_types::ip::Ipv6Addr>,
    precedence: usize,
    label: usize,
}

macro_rules! decl_policy {
    ($ip:tt/$prefix:expr => $precedence:expr, $label:expr) => {
        Policy {
            // Unsafe allows us to declare constant subnets.
            // We make sure no invalid subnets are created in
            // test_valid_policy_table.
            prefix: unsafe {
                net_types::ip::Subnet::new_unchecked(
                    net_types::ip::Ipv6Addr::from_bytes(fidl_ip_v6!($ip).addr),
                    $prefix,
                )
            },
            precedence: $precedence,
            label: $label,
        }
    };
}

/// Policy table is defined in RFC 6724, section 2.1
///
/// A more human-readable version:
///
///  Prefix        Precedence Label
///  ::1/128               50     0
///  ::/0                  40     1
///  ::ffff:0:0/96         35     4
///  2002::/16             30     2
///  2001::/32              5     5
///  fc00::/7               3    13
///  ::/96                  1     3
///  fec0::/10              1    11
///  3ffe::/16              1    12
///
/// We willingly left out ::/96, fec0::/10, 3ffe::/16 since those prefix
/// assignments are deprecated.
///
/// The table is sorted by prefix length so longest-prefix match can be easily
/// achieved.
const POLICY_TABLE: [Policy; 6] = [
    decl_policy!("::1"/128 => 50, 0),
    decl_policy!("::ffff:0:0"/96 => 35, 4),
    decl_policy!("2001::"/32 => 5, 5),
    decl_policy!("2002::"/16 => 30, 2),
    decl_policy!("fc00::"/7 => 3, 13),
    decl_policy!("::"/0 => 40, 1),
];

fn policy_lookup(addr: &net_types::ip::Ipv6Addr) -> &'static Policy {
    POLICY_TABLE
        .iter()
        .find(|policy| policy.prefix.contains(addr))
        .expect("policy table MUST contain the all addresses subnet")
}

/// Destination Address selection information.
///
/// `DasCmpInfo` provides an implementation of a subset of Destination Address
/// Selection according to the sorting rules defined in [RFC 6724 Section 6].
///
/// TODO(fxbug.dev/65219): Implement missing rules 3, 4, and 7.
/// Rules 3, 4, and 7 are omitted for compatibility with the equivalent
/// implementation in Fuchsia's libc.
///
/// `DasCmpInfo` provides an [`std::cmp::Ord`] implementation that will return
/// preferred addresses as "lesser" values.
///
/// [RFC 6724 Section 6]: https://tools.ietf.org/html/rfc6724#section-6
#[derive(Debug)]
struct DasCmpInfo {
    usable: bool,
    matching_scope: bool,
    matching_label: bool,
    precedence: usize,
    scope: net_types::ip::Ipv6Scope,
    common_prefix_len: u8,
}

impl DasCmpInfo {
    /// Helper function to convert a FIDL IP address into
    /// [`net_types::ip::Ipv6Addr`], using a mapped IPv4 when that's the case.
    fn convert_addr(fidl: &fnet::IpAddress) -> net_types::ip::Ipv6Addr {
        match fidl {
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => {
                net_types::ip::Ipv6Addr::from(net_types::ip::Ipv4Addr::new(*addr))
            }
            fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => {
                net_types::ip::Ipv6Addr::from_bytes(*addr)
            }
        }
    }

    fn from_addrs(dst_addr: &fnet::IpAddress, src_addr: Option<&fnet::IpAddress>) -> Self {
        use net_types::ScopeableAddress;

        let dst_addr = Self::convert_addr(dst_addr);
        let Policy { prefix: _, precedence, label: dst_label } = policy_lookup(&dst_addr);
        let (usable, matching_scope, matching_label, common_prefix_len) = match src_addr {
            Some(src_addr) => {
                let src_addr = Self::convert_addr(src_addr);
                let Policy { prefix: _, precedence: _, label: src_label } =
                    policy_lookup(&src_addr);
                (
                    true,
                    dst_addr.scope() == src_addr.scope(),
                    dst_label == src_label,
                    dst_addr.common_prefix_len(&src_addr),
                )
            }
            None => (false, false, false, 0),
        };
        DasCmpInfo {
            usable,
            matching_scope,
            matching_label,
            precedence: *precedence,
            scope: dst_addr.scope(),
            common_prefix_len,
        }
    }
}

impl std::cmp::Ord for DasCmpInfo {
    // TODO(fxbug.dev/65219): Implement missing rules 3, 4, and 7.
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        use std::cmp::Ordering;
        let DasCmpInfo {
            usable: self_usable,
            matching_scope: self_matching_scope,
            matching_label: self_matching_label,
            precedence: self_precedence,
            scope: self_scope,
            common_prefix_len: self_common_prefix_len,
        } = self;
        let DasCmpInfo {
            usable: other_usable,
            matching_scope: other_matching_scope,
            matching_label: other_matching_label,
            precedence: other_precedence,
            scope: other_scope,
            common_prefix_len: other_common_prefix_len,
        } = other;

        fn prefer_true(left: bool, right: bool) -> Ordering {
            match (left, right) {
                (true, false) => Ordering::Less,
                (false, true) => Ordering::Greater,
                (false, false) | (true, true) => Ordering::Equal,
            }
        }

        // Rule 1: Avoid unusable destinations.
        prefer_true(*self_usable, *other_usable)
            .then(
                // Rule 2: Prefer matching scope.
                prefer_true(*self_matching_scope, *other_matching_scope),
            )
            .then(
                // Rule 5: Prefer matching label.
                prefer_true(*self_matching_label, *other_matching_label),
            )
            .then(
                // Rule 6: Prefer higher precedence.
                self_precedence.cmp(other_precedence).reverse(),
            )
            .then(
                // Rule 8: Prefer smaller scope.
                self_scope.multicast_scope_id().cmp(&other_scope.multicast_scope_id()),
            )
            .then(
                // Rule 9: Use longest matching prefix.
                self_common_prefix_len.cmp(other_common_prefix_len).reverse(),
            )
        // Rule 10: Otherwise, leave the order unchanged.
    }
}

impl std::cmp::PartialOrd for DasCmpInfo {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl std::cmp::PartialEq for DasCmpInfo {
    fn eq(&self, other: &Self) -> bool {
        self.cmp(other) == std::cmp::Ordering::Equal
    }
}

impl std::cmp::Eq for DasCmpInfo {}

async fn handle_lookup_hostname<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    addr: fnet::IpAddress,
) -> Result<String, fname::LookupError> {
    let net_ext::IpAddress(addr) = addr.into();
    let resolver = resolver.read();

    match resolver.reverse_lookup(addr).await {
        Ok(response) => {
            response.iter().next().ok_or(fname::LookupError::NotFound).map(ToString::to_string)
        }
        Err(error) => Err(handle_err("LookupHostname", error)),
    }
}

struct IpLookupRequest {
    hostname: String,
    options: fname::LookupIpOptions,
    responder: fname::LookupLookupIpResponder,
}

async fn run_lookup<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    stream: LookupRequestStream,
    sender: mpsc::Sender<IpLookupRequest>,
) -> Result<(), fidl::Error> {
    let result = stream
        .try_for_each_concurrent(None, |request| async {
            match request {
                LookupRequest::LookupIp { hostname, options, responder } => {
                    let () = sender
                        .clone()
                        .send(IpLookupRequest { hostname, options, responder })
                        .await
                        .expect("receiver should not be closed");
                    Ok(())
                }
                LookupRequest::LookupHostname { addr, responder } => {
                    responder.send(&mut handle_lookup_hostname(&resolver, addr).await)
                }
            }
        })
        .await;
    // Some clients will drop the channel when timing out
    // requests. Mute those errors to prevent log spamming.
    if let Err(fidl::Error::ServerResponseWrite(zx::Status::PEER_CLOSED)) = result {
        Ok(())
    } else {
        result
    }
}

const MAX_PARALLEL_REQUESTS: usize = 256;

fn create_ip_lookup_fut<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    stats: Arc<QueryStats>,
    routes: fnet_routes::StateProxy,
    recv: mpsc::Receiver<IpLookupRequest>,
) -> impl futures::Future<Output = ()> + '_ {
    recv.for_each_concurrent(
        MAX_PARALLEL_REQUESTS,
        move |IpLookupRequest { hostname, options, responder }| {
            let stats = stats.clone();
            let routes = routes.clone();
            async move {
                let fname::LookupIpOptions { ipv4_lookup, ipv6_lookup, sort_addresses, .. } =
                    options;
                let ipv4_lookup = ipv4_lookup.unwrap_or(false);
                let ipv6_lookup = ipv6_lookup.unwrap_or(false);
                let sort_addresses = sort_addresses.unwrap_or(false);
                let mut lookup_result = (|| async {
                    let hostname = hostname.as_str();
                    // The [`IntoName`] implementation for &str does not
                    // properly reject IPv4 addresses in accordance with RFC
                    // 1123 section 2.1:
                    //
                    //   If a dotted-decimal number can be entered without such
                    //   identifying delimiters, then a full syntactic check must be
                    //   made, because a segment of a host domain name is now allowed
                    //   to begin with a digit and could legally be entirely numeric
                    //   (see Section 6.1.2.4).  However, a valid host name can never
                    //   have the dotted-decimal form #.#.#.#, since at least the
                    //   highest-level component label will be alphabetic.
                    //
                    // Thus we explicitly reject such input here.
                    //
                    // TODO(https://github.com/bluejekyll/trust-dns/issues/1725):
                    // Remove this when the implementation is sufficiently
                    // strict.
                    match IpAddr::from_str(hostname) {
                        Ok(addr) => {
                            let _: IpAddr = addr;
                            return Err(fname::LookupError::InvalidArgs);
                        }
                        Err(std::net::AddrParseError { .. }) => {}
                    };
                    let resolver = resolver.read();
                    let start_time = fasync::Time::now();
                    let (ret1, ret2) = futures::future::join(
                        futures::future::OptionFuture::from(
                            ipv4_lookup.then(|| resolver.lookup(hostname, RecordType::A)),
                        ),
                        futures::future::OptionFuture::from(
                            ipv6_lookup.then(|| resolver.lookup(hostname, RecordType::AAAA)),
                        ),
                    )
                    .await;
                    let result = [ret1, ret2];
                    if result.iter().all(Option::is_none) {
                        return Err(fname::LookupError::InvalidArgs);
                    }
                    let (addrs, error) = result.into_iter()
                        .filter_map(std::convert::identity)
                        .fold((Vec::new(), None), |(mut addrs, mut error), result| {
                            let () = match result {
                                Err(err) => match error.as_ref() {
                                    Some(_err) => {}
                                    None => {
                                        error = Some(err);
                                    }
                                },
                                Ok(lookup) => lookup.iter().for_each(|rdata| match rdata {
                                    RData::A(addr) if ipv4_lookup => {
                                        addrs.push(net_ext::IpAddress(IpAddr::V4(*addr)).into())
                                    }
                                    RData::AAAA(addr) if ipv6_lookup => {
                                        addrs.push(net_ext::IpAddress(IpAddr::V6(*addr)).into())
                                    }
                                    rdata => {
                                        let record_type = rdata.to_record_type();
                                        match record_type {
                                            // CNAME records are known to be present with other
                                            // query types; avoid logging in that case.
                                            RecordType::CNAME => (),
                                            record_type => {
                                                error!(
                                                    "Lookup(_, {:?}) yielded unexpected record type: {}",
                                                    options,
                                                    record_type
                                                )
                                            }
                                        }
                                    }
                                }),
                            };
                            (addrs, error)
                        });
                    let count = match NonZeroUsize::try_from(addrs.len()) {
                        Ok(count) => Ok(count),
                        Err(std::num::TryFromIntError { .. }) => {
                            match error {
                                None => {
                                    // TODO(https://fxbug.dev/111095): Remove
                                    // this once Trust-DNS enforces that all
                                    // responses with no records return a
                                    // `NoRecordsFound` error.
                                    //
                                    // Note that returning here means that query
                                    // stats for inspect will not get logged.
                                    // This is ok since this case should be rare
                                    // and is considered to be temporary.
                                    // Moreover, the failed query counters are
                                    // based on the `ResolverError::kind`, which
                                    // isn't applicable here.
                                    error!("resolver response unexpectedly contained no records and no error. See https://fxbug.dev/111095.");
                                    return Err(fname::LookupError::NotFound);
                                },
                                Some(e) => Err(e),
                            }
                        }
                    };
                    let () = stats
                        .finish_query(
                            start_time,
                            count.as_ref().copied().map_err(ResolveError::kind),
                        )
                        .await;
                    let _: NonZeroUsize = count.map_err(|err| handle_err("LookupIp", err))?;
                    let addrs = if sort_addresses {
                        sort_preferred_addresses(addrs, &routes).await
                    } else {
                        Ok(addrs)
                    }?;
                    Ok(fname::LookupResult { addresses: Some(addrs), ..fname::LookupResult::EMPTY })
                })()
                .await;
                responder.send(&mut lookup_result).unwrap_or_else(|e| match e {
                    // Some clients will drop the channel when timing out
                    // requests. Mute those errors to prevent log spamming.
                    fidl::Error::ServerResponseWrite(zx::Status::PEER_CLOSED) => {}
                    e => warn!(
                        "failed to send IP lookup result {:?} due to FIDL error: {}",
                        lookup_result, e
                    ),
                })
            }
        },
    )
}

/// Serves `stream` and forwards received configurations to `sink`.
async fn run_lookup_admin<T: ResolverLookup>(
    resolver: &SharedResolver<T>,
    state: &dns::config::ServerConfigState,
    stream: LookupAdminRequestStream,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|req| async {
            match req {
                LookupAdminRequest::SetDnsServers { servers, responder } => {
                    let mut response = match state.update_servers(servers) {
                        UpdateServersResult::Updated(servers) => {
                            let () = update_resolver(resolver, servers);
                            Ok(())
                        }
                        UpdateServersResult::NoChange => Ok(()),
                        UpdateServersResult::InvalidsServers => {
                            Err(zx::Status::INVALID_ARGS.into_raw())
                        }
                    };
                    let () = responder.send(&mut response)?;
                }
                LookupAdminRequest::GetDnsServers { responder } => {
                    let () = responder.send(&mut state.servers().iter_mut())?;
                }
            }
            Ok(())
        })
        .await
}

/// Adds a [`dns::policy::ServerConfigState`] inspection child node to
/// `parent`.
fn add_config_state_inspect(
    parent: &fuchsia_inspect::Node,
    config_state: Arc<dns::config::ServerConfigState>,
) -> fuchsia_inspect::LazyNode {
    parent.create_lazy_child("servers", move || {
        let config_state = config_state.clone();
        async move {
            let srv = fuchsia_inspect::Inspector::new();
            let server_list = config_state.servers();
            for (i, server) in server_list.into_iter().enumerate() {
                let child = srv.root().create_child(format!("{}", i));
                let net_ext::SocketAddress(addr) = server.into();
                let () = child.record_string("address", format!("{}", addr));
                let () = srv.root().record(child);
            }
            Ok(srv)
        }
        .boxed()
    })
}

/// Adds a [`QueryStats`] inspection child node to `parent`.
fn add_query_stats_inspect(
    parent: &fuchsia_inspect::Node,
    stats: Arc<QueryStats>,
) -> fuchsia_inspect::LazyNode {
    parent.create_lazy_child("query_stats", move || {
        let stats = stats.clone();
        async move {
            let past_queries = &*stats.inner.lock().await;
            let node = fuchsia_inspect::Inspector::new();
            for (
                i,
                QueryWindow {
                    start,
                    success_count,
                    failure_count,
                    success_elapsed_time,
                    failure_elapsed_time,
                    failure_stats,
                    address_counts_histogram,
                },
            ) in past_queries.iter().enumerate()
            {
                let child = node.root().create_child(format!("window {}", i + 1));

                match u64::try_from(start.into_nanos()) {
                    Ok(nanos) => {
                        let () = child.record_uint("start_time_nanos", nanos);
                    },
                    Err(e) => warn!(
                        "error computing `start_time_nanos`: {:?}.into_nanos() from i64 -> u64 failed: {}",
                        start, e
                    ),
                }
                let () = child.record_uint("successful_queries", *success_count);
                let () = child.record_uint("failed_queries", *failure_count);
                let record_average = |name: &str, total: zx::Duration, count: u64| {
                    // Don't record an average if there are no stats.
                    if count == 0 {
                        return;
                    }
                    match u64::try_from(total.into_micros()) {
                        Ok(micros) => child.record_uint(name, micros / count),
                        Err(e) => warn!(
                            "error computing `{}`: {:?}.into_micros() from i64 -> u64 failed: {}",
                            name, success_elapsed_time, e
                        ),
                    }
                };
                let () = record_average(
                    "average_success_duration_micros",
                    *success_elapsed_time,
                    *success_count,
                );
                let () = record_average(
                    "average_failure_duration_micros",
                    *failure_elapsed_time,
                    *failure_count,
                );
                let FailureStats {
                    message,
                    no_connections,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts,
                    },
                    io,
                    proto,
                    timeout,
                    unhandled_resolve_error_kind: UnhandledResolveErrorKindStats {
                        resolve_error_kind_counts,
                    },
                } = failure_stats;
                let errors = child.create_child("errors");
                let () = errors.record_uint("Message", *message);
                let () = errors.record_uint("NoConnections", *no_connections);
                let () = errors.record_uint("Io", *io);
                let () = errors.record_uint("Proto", *proto);
                let () = errors.record_uint("Timeout", *timeout);

                let no_records_found_response_codes =
                    errors.create_child("NoRecordsFoundResponseCodeCounts");
                for (HashableResponseCode { response_code }, count) in response_code_counts {
                    let () = no_records_found_response_codes.record_uint(
                        format!("{:?}", response_code),
                        *count,
                    );
                }
                let () = errors.record(no_records_found_response_codes);

                let unhandled_resolve_error_kinds =
                    errors.create_child("UnhandledResolveErrorKindCounts");
                for (error_kind, count) in resolve_error_kind_counts {
                    let () = unhandled_resolve_error_kinds.record_uint(error_kind, *count);
                }
                let () = errors.record(unhandled_resolve_error_kinds);

                let () = child.record(errors);

                let address_counts_node = child.create_child("address_counts");
                for (count, occurrences) in address_counts_histogram {
                    address_counts_node.record_uint(count.to_string(), *occurrences);
                }
                child.record(address_counts_node);

                let () = node.root().record(child);
            }
            Ok(node)
        }
        .boxed()
    })
}

// NB: We manually set tags so logs from trust-dns crates also get the same
// tags as opposed to only the crate path.
#[fuchsia::main(logging_tags = ["dns"])]
async fn main() -> Result<(), Error> {
    info!("starting");

    let mut resolver_opts = ResolverOpts::default();
    // Resolver will query for A and AAAA in parallel for lookup_ip.
    resolver_opts.ip_strategy = LookupIpStrategy::Ipv4AndIpv6;
    let resolver = SharedResolver::new(
        Resolver::new(ResolverConfig::default(), resolver_opts, Spawner)
            .expect("failed to create resolver"),
    );

    let config_state = Arc::new(dns::config::ServerConfigState::new());
    let stats = Arc::new(QueryStats::new());

    let mut fs = ServiceFs::new_local();

    let inspector = fuchsia_inspect::component::inspector();
    let _state_inspect_node = add_config_state_inspect(inspector.root(), config_state.clone());
    let _query_stats_inspect_node = add_query_stats_inspect(inspector.root(), stats.clone());
    let () = inspect_runtime::serve(inspector, &mut fs)?;

    let routes = fuchsia_component::client::connect_to_protocol::<fnet_routes::StateMarker>()
        .context("failed to connect to fuchsia.net.routes/State")?;

    let _: &mut ServiceFsDir<'_, _> = fs
        .dir("svc")
        .add_fidl_service(IncomingRequest::Lookup)
        .add_fidl_service(IncomingRequest::LookupAdmin);
    let _: &mut ServiceFs<_> =
        fs.take_and_serve_directory_handle().context("failed to serve directory")?;

    // Create a channel with buffer size `MAX_PARALLEL_REQUESTS`, which allows
    // request processing to always be fully saturated.
    let (sender, recv) = mpsc::channel(MAX_PARALLEL_REQUESTS);
    let serve_fut = fs.for_each_concurrent(None, |incoming_service| async {
        match incoming_service {
            IncomingRequest::Lookup(stream) => run_lookup(&resolver, stream, sender.clone())
                .await
                .unwrap_or_else(|e| warn!("run_lookup finished with error: {}", e)),
            IncomingRequest::LookupAdmin(stream) => {
                run_lookup_admin(&resolver, &config_state, stream).await.unwrap_or_else(|e| {
                    if e.is_closed() {
                        warn!("run_lookup_admin finished with error: {}", e)
                    } else {
                        error!("run_lookup_admin finished with error: {}", e)
                    }
                })
            }
        }
    });
    let ip_lookup_fut = create_ip_lookup_fut(&resolver, stats.clone(), routes, recv);

    let ((), ()) = futures::future::join(serve_fut, ip_lookup_fut).await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::{
        net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
        str::FromStr,
        sync::Arc,
    };

    use assert_matches::assert_matches;
    use dns::test_util::*;
    use dns::DEFAULT_PORT;
    use fuchsia_inspect::{assert_data_tree, testing::NonZeroUintProperty, tree_assertion};
    use futures::future::TryFutureExt as _;
    use itertools::Itertools as _;
    use net_declare::{fidl_ip, std_ip, std_ip_v4, std_ip_v6};
    use net_types::ip::Ip as _;
    use trust_dns_proto::{
        op::Query,
        rr::{Name, RData, Record},
    };
    use trust_dns_resolver::{error::ResolveErrorKind, lookup::Lookup, lookup::ReverseLookup};

    use super::*;

    const IPV4_LOOPBACK: fnet::IpAddress = fidl_ip!("127.0.0.1");
    const IPV6_LOOPBACK: fnet::IpAddress = fidl_ip!("::1");
    const LOCAL_HOST: &str = "localhost.";

    // IPv4 address returned by mock lookup.
    const IPV4_HOST: Ipv4Addr = std_ip_v4!("240.0.0.2");
    // IPv6 address returned by mock lookup.
    const IPV6_HOST: Ipv6Addr = std_ip_v6!("abcd::2");

    // host which has IPv4 address only.
    const REMOTE_IPV4_HOST: &str = "www.foo.com";
    // host which has IPv6 address only.
    const REMOTE_IPV6_HOST: &str = "www.bar.com";
    // host used in reverse_lookup when multiple hostnames are returned.
    const REMOTE_IPV6_HOST_EXTRA: &str = "www.bar2.com";
    // host which has IPv4 and IPv6 address if reset name servers.
    const REMOTE_IPV4_IPV6_HOST: &str = "www.foobar.com";
    // host which has no records and does not result in an error.
    const NO_RECORDS_AND_NO_ERROR_HOST: &str = "www.no-records-and-no-error.com";

    async fn setup_namelookup_service() -> (fname::LookupProxy, impl futures::Future<Output = ()>) {
        let (name_lookup_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fname::LookupMarker>()
                .expect("failed to create NamelookupProxy");

        let mut resolver_opts = ResolverOpts::default();
        resolver_opts.ip_strategy = LookupIpStrategy::Ipv4AndIpv6;

        let resolver = SharedResolver::new(
            Resolver::new(ResolverConfig::default(), resolver_opts, Spawner)
                .expect("failed to create resolver"),
        );
        let stats = Arc::new(QueryStats::new());
        let (routes_proxy, routes_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet_routes::StateMarker>()
                .expect("failed to create routes.StateProxy");
        let routes_fut =
            routes_stream.try_for_each(|req| -> futures::future::Ready<Result<(), fidl::Error>> {
                panic!("Should not call routes/State. Received request {:?}", req)
            });
        let (sender, recv) = mpsc::channel(MAX_PARALLEL_REQUESTS);

        (name_lookup_proxy, async move {
            futures::future::try_join3(
                run_lookup(&resolver, stream, sender),
                routes_fut,
                create_ip_lookup_fut(&resolver, stats.clone(), routes_proxy, recv).map(Ok),
            )
            .map(|r| match r {
                Ok(((), (), ())) => (),
                Err(e) => panic!("namelookup service error {:?}", e),
            })
            .await
        })
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_localhost() {
        let (proxy, fut) = setup_namelookup_service().await;
        let ((), ()) = futures::future::join(fut, async move {
            // IP Lookup IPv4 and IPv6 for localhost.
            assert_eq!(
                proxy
                    .lookup_ip(
                        LOCAL_HOST,
                        fname::LookupIpOptions {
                            ipv4_lookup: Some(true),
                            ipv6_lookup: Some(true),
                            ..fname::LookupIpOptions::EMPTY
                        }
                    )
                    .await
                    .expect("lookup_ip"),
                Ok(fname::LookupResult {
                    addresses: Some(vec![IPV4_LOOPBACK, IPV6_LOOPBACK]),
                    ..fname::LookupResult::EMPTY
                }),
            );

            // IP Lookup IPv4 only for localhost.
            assert_eq!(
                proxy
                    .lookup_ip(
                        LOCAL_HOST,
                        fname::LookupIpOptions {
                            ipv4_lookup: Some(true),
                            ..fname::LookupIpOptions::EMPTY
                        }
                    )
                    .await
                    .expect("lookup_ip"),
                Ok(fname::LookupResult {
                    addresses: Some(vec![IPV4_LOOPBACK]),
                    ..fname::LookupResult::EMPTY
                }),
            );

            // IP Lookup IPv6 only for localhost.
            assert_eq!(
                proxy
                    .lookup_ip(
                        LOCAL_HOST,
                        fname::LookupIpOptions {
                            ipv6_lookup: Some(true),
                            ..fname::LookupIpOptions::EMPTY
                        }
                    )
                    .await
                    .expect("lookup_ip"),
                Ok(fname::LookupResult {
                    addresses: Some(vec![IPV6_LOOPBACK]),
                    ..fname::LookupResult::EMPTY
                }),
            );
        })
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookuphostname_localhost() {
        let (proxy, fut) = setup_namelookup_service().await;
        let ((), ()) = futures::future::join(fut, async move {
            let mut hostname = IPV4_LOOPBACK;
            assert_eq!(
                proxy.lookup_hostname(&mut hostname).await.expect("lookup_hostname").as_deref(),
                Ok(LOCAL_HOST)
            );
        })
        .await;
    }

    struct MockResolver {
        config: ResolverConfig,
    }

    #[async_trait]
    impl ResolverLookup for MockResolver {
        fn new(config: ResolverConfig, _options: ResolverOpts) -> Self {
            MockResolver { config }
        }

        async fn lookup<N: IntoName + Send>(
            &self,
            name: N,
            record_type: RecordType,
        ) -> Result<lookup::Lookup, ResolveError> {
            let name = name.into_name()?;
            let host_name = name.to_utf8();

            if host_name == NO_RECORDS_AND_NO_ERROR_HOST {
                return Ok(Lookup::new_with_max_ttl(Query::default(), Arc::new([])));
            }
            let rdatas = match record_type {
                RecordType::A => [REMOTE_IPV4_HOST, REMOTE_IPV4_IPV6_HOST]
                    .contains(&host_name.as_str())
                    .then(|| RData::A(IPV4_HOST)),
                RecordType::AAAA => [REMOTE_IPV6_HOST, REMOTE_IPV4_IPV6_HOST]
                    .contains(&host_name.as_str())
                    .then(|| RData::AAAA(IPV6_HOST)),
                record_type => {
                    panic!("unexpected record type {:?}", record_type)
                }
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

            if records.is_empty() {
                let mut response = trust_dns_proto::op::Message::new();
                let _: &mut trust_dns_proto::op::Message =
                    response.set_response_code(ResponseCode::NoError);
                let error = ResolveError::from_response(response.into(), false)
                    .expect_err("response with no records should be a NoRecordsFound error");
                return Err(error);
            }

            Ok(Lookup::new_with_max_ttl(Query::default(), records.into()))
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
                    Arc::new([
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
                Lookup::new_with_max_ttl(Query::default(), Arc::new([]))
            };
            Ok(ReverseLookup::from(lookup))
        }
    }

    struct TestEnvironment {
        shared_resolver: SharedResolver<MockResolver>,
        config_state: Arc<dns::config::ServerConfigState>,
        stats: Arc<QueryStats>,
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
                config_state: Arc::new(dns::config::ServerConfigState::new()),
                stats: Arc::new(QueryStats::new()),
            }
        }

        async fn run_lookup<F, Fut>(&self, f: F)
        where
            Fut: futures::Future<Output = ()>,
            F: FnOnce(fname::LookupProxy) -> Fut,
        {
            self.run_lookup_with_routes_handler(f, |req| {
                panic!("Should not call routes/State. Received request {:?}", req)
            })
            .await
        }

        async fn run_lookup_with_routes_handler<F, Fut, R>(&self, f: F, handle_routes: R)
        where
            Fut: futures::Future<Output = ()>,
            F: FnOnce(fname::LookupProxy) -> Fut,
            R: Fn(fnet_routes::StateRequest),
        {
            let (name_lookup_proxy, name_lookup_stream) =
                fidl::endpoints::create_proxy_and_stream::<fname::LookupMarker>()
                    .expect("failed to create LookupProxy");

            let (routes_proxy, routes_stream) =
                fidl::endpoints::create_proxy_and_stream::<fnet_routes::StateMarker>()
                    .expect("failed to create routes.StateProxy");

            let (sender, recv) = mpsc::channel(MAX_PARALLEL_REQUESTS);
            let Self { shared_resolver, config_state: _, stats } = self;
            let ((), (), (), ()) = futures::future::try_join4(
                run_lookup(shared_resolver, name_lookup_stream, sender),
                f(name_lookup_proxy).map(Ok),
                routes_stream.try_for_each(|req| futures::future::ok(handle_routes(req))),
                create_ip_lookup_fut(shared_resolver, stats.clone(), routes_proxy, recv).map(Ok),
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
            let Self { shared_resolver, config_state, stats: _ } = self;
            let ((), ()) = futures::future::try_join(
                run_lookup_admin(shared_resolver, config_state, lookup_admin_stream)
                    .map_err(anyhow::Error::from),
                f(lookup_admin_proxy).map(Ok),
            )
            .await
            .expect("Error running admin future");
        }
    }

    fn map_ip<T: Into<IpAddr>>(addr: T) -> fnet::IpAddress {
        net_ext::IpAddress(addr.into()).into()
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_records_and_no_error() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                let proxy = &proxy;
                futures::stream::iter([(true, true), (true, false), (false, true)])
                    .for_each_concurrent(None, move |(ipv4_lookup, ipv6_lookup)| async move {
                        // Verify that the resolver does not panic when the
                        // response contains no records and no error. This
                        // scenario should theoretically not occur, but
                        // currently does. See https://fxbug.dev/111095.
                        assert_eq!(
                            proxy
                                .lookup_ip(
                                    NO_RECORDS_AND_NO_ERROR_HOST,
                                    fname::LookupIpOptions {
                                        ipv4_lookup: Some(ipv4_lookup),
                                        ipv6_lookup: Some(ipv6_lookup),
                                        ..fname::LookupIpOptions::EMPTY
                                    }
                                )
                                .await
                                .expect("lookup_ip"),
                            Err(fname::LookupError::NotFound),
                        );
                    })
                    .await
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_remotehost_ipv4() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                // IP Lookup IPv4 and IPv6 for REMOTE_IPV4_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV4_HOST,
                            fname::LookupIpOptions {
                                ipv4_lookup: Some(true),
                                ipv6_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Ok(fname::LookupResult {
                        addresses: Some(vec![map_ip(IPV4_HOST)]),
                        ..fname::LookupResult::EMPTY
                    }),
                );

                // IP Lookup IPv4 for REMOTE_IPV4_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV4_HOST,
                            fname::LookupIpOptions {
                                ipv4_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Ok(fname::LookupResult {
                        addresses: Some(vec![map_ip(IPV4_HOST)]),
                        ..fname::LookupResult::EMPTY
                    }),
                );

                // IP Lookup IPv6 for REMOTE_IPV4_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV4_HOST,
                            fname::LookupIpOptions {
                                ipv6_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Err(fname::LookupError::NotFound),
                );
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_remotehost_ipv6() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                // IP Lookup IPv4 and IPv6 for REMOTE_IPV6_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV6_HOST,
                            fname::LookupIpOptions {
                                ipv4_lookup: Some(true),
                                ipv6_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Ok(fname::LookupResult {
                        addresses: Some(vec![map_ip(IPV6_HOST)]),
                        ..fname::LookupResult::EMPTY
                    }),
                );

                // IP Lookup IPv4 for REMOTE_IPV6_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV6_HOST,
                            fname::LookupIpOptions {
                                ipv4_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Err(fname::LookupError::NotFound),
                );

                // IP Lookup IPv6 for REMOTE_IPV4_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV6_HOST,
                            fname::LookupIpOptions {
                                ipv6_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Ok(fname::LookupResult {
                        addresses: Some(vec![map_ip(IPV6_HOST)]),
                        ..fname::LookupResult::EMPTY
                    }),
                );
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip_ip_literal() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                let proxy = &proxy;

                let range = || [true, false].into_iter();

                futures::stream::iter(range().cartesian_product(range()))
                    .for_each_concurrent(None, move |(ipv4_lookup, ipv6_lookup)| async move {
                        assert_eq!(
                            proxy
                                .lookup_ip(
                                    "240.0.0.2",
                                    fname::LookupIpOptions {
                                        ipv4_lookup: Some(ipv4_lookup),
                                        ipv6_lookup: Some(ipv6_lookup),
                                        ..fname::LookupIpOptions::EMPTY
                                    }
                                )
                                .await
                                .expect("lookup_ip"),
                            Err(fname::LookupError::InvalidArgs),
                            "ipv4_lookup={},ipv6_lookup={}",
                            ipv4_lookup,
                            ipv6_lookup,
                        );

                        assert_eq!(
                            proxy
                                .lookup_ip(
                                    "abcd::2",
                                    fname::LookupIpOptions {
                                        ipv4_lookup: Some(ipv4_lookup),
                                        ipv6_lookup: Some(ipv6_lookup),
                                        ..fname::LookupIpOptions::EMPTY
                                    }
                                )
                                .await
                                .expect("lookup_ip"),
                            Err(fname::LookupError::InvalidArgs),
                            "ipv4_lookup={},ipv6_lookup={}",
                            ipv4_lookup,
                            ipv6_lookup,
                        );
                    })
                    .await
            })
            .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_hostname() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                assert_eq!(
                    proxy
                        .lookup_hostname(&mut map_ip(IPV4_HOST))
                        .await
                        .expect("lookup_hostname")
                        .as_deref(),
                    Ok(REMOTE_IPV4_HOST)
                );
            })
            .await;
    }

    // Multiple hostnames returned from trust-dns* APIs, and only the first one will be returned
    // by the FIDL.
    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_hostname_multi() {
        TestEnvironment::new()
            .run_lookup(|proxy| async move {
                assert_eq!(
                    proxy
                        .lookup_hostname(&mut map_ip(IPV6_HOST))
                        .await
                        .expect("lookup_hostname")
                        .as_deref(),
                    Ok(REMOTE_IPV6_HOST)
                );
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_server_names() {
        let env = TestEnvironment::new();

        let to_server_configs = |socket_addr: SocketAddr| -> [NameServerConfig; 2] {
            [
                NameServerConfig {
                    socket_addr,
                    protocol: Protocol::Udp,
                    tls_dns_name: None,
                    trust_nx_responses: false,
                    bind_addr: None,
                },
                NameServerConfig {
                    socket_addr,
                    protocol: Protocol::Tcp,
                    tls_dns_name: None,
                    trust_nx_responses: false,
                    bind_addr: None,
                },
            ]
        };

        // Assert that mock config has no servers originally.
        assert_eq!(env.shared_resolver.read().config.name_servers().to_vec(), vec![]);

        // Set servers.
        env.run_admin(|proxy| async move {
            let () = proxy
                .set_dns_servers(&mut vec![DHCP_SERVER, NDP_SERVER, DHCPV6_SERVER].iter_mut())
                .await
                .expect("Failed to call SetDnsServers")
                .expect("SetDnsServers error");
        })
        .await;
        assert_eq!(
            env.shared_resolver.read().config.name_servers().to_vec(),
            vec![DHCP_SERVER, NDP_SERVER, DHCPV6_SERVER]
                .into_iter()
                .map(|s| {
                    let net_ext::SocketAddress(s) = s.into();
                    s
                })
                .flat_map(|x| to_server_configs(x).to_vec().into_iter())
                .collect::<Vec<_>>()
        );

        // Clear servers.
        env.run_admin(|proxy| async move {
            let () = proxy
                .set_dns_servers(&mut vec![].into_iter())
                .await
                .expect("Failed to call SetDnsServers")
                .expect("SetDnsServers error");
        })
        .await;
        assert_eq!(env.shared_resolver.read().config.name_servers().to_vec(), Vec::new());
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
                .set_dns_servers(
                    &mut vec![fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
                        address: fnet::Ipv4Address { addr: [224, 0, 0, 1] },
                        port: DEFAULT_PORT,
                    })]
                    .iter_mut(),
                )
                .await
                .expect("Failed to call SetDnsServers")
                .expect_err("SetDnsServers should fail for multicast address");
            assert_eq!(zx::Status::from_raw(status), zx::Status::INVALID_ARGS);

            // Unspecified not allowed.
            let status = proxy
                .set_dns_servers(
                    &mut vec![fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                        address: fnet::Ipv6Address { addr: [0; 16] },
                        port: DEFAULT_PORT,
                        zone_index: 0,
                    })]
                    .iter_mut(),
                )
                .await
                .expect("Failed to call SetDnsServers")
                .expect_err("SetDnsServers should fail for unspecified address");
            assert_eq!(zx::Status::from_raw(status), zx::Status::INVALID_ARGS);
        })
        .await;

        // Assert that config didn't change.
        assert_eq!(env.shared_resolver.read().config.name_servers().to_vec(), vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_servers() {
        let env = TestEnvironment::new();
        env.run_admin(|proxy| async move {
            let expect = vec![NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER];
            let () = proxy
                .set_dns_servers(&mut expect.clone().iter_mut())
                .await
                .expect("FIDL error")
                .expect("set_servers failed");
            assert_matches!(proxy.get_dns_servers().await, Ok(got) if got == expect);
        })
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_config_inspect() {
        let env = TestEnvironment::new();
        let inspector = fuchsia_inspect::Inspector::new();
        let _config_state_node =
            add_config_state_inspect(inspector.root(), env.config_state.clone());
        assert_data_tree!(inspector, root:{
            servers: {}
        });
        env.run_admin(|proxy| async move {
            let mut servers = [NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER];
            let () = proxy
                .set_dns_servers(&mut servers.iter_mut())
                .await
                .expect("FIDL error")
                .expect("set_servers failed");
        })
        .await;
        assert_data_tree!(inspector, root:{
            servers: {
                "0": {
                    address: "[2001:4860:4860::4444%2]:53",
                },
                "1": {
                    address: "8.8.4.4:53",
                },
                "2": {
                    address: "[2002:4860:4860::4444%3]:53",
                },
                "3": {
                    address: "8.8.8.8:53",
                },
            }
        });
    }

    #[test]
    fn test_unhandled_resolve_error_kind_stats() {
        use ResolveErrorKind::{Msg, Timeout};
        let mut unhandled_resolve_error_kind_stats = UnhandledResolveErrorKindStats::default();
        unhandled_resolve_error_kind_stats.increment(&Msg(String::from("abcdefgh")));
        unhandled_resolve_error_kind_stats.increment(&Msg(String::from("ijklmn")));
        unhandled_resolve_error_kind_stats.increment(&Timeout);
        assert_eq!(
            unhandled_resolve_error_kind_stats,
            UnhandledResolveErrorKindStats {
                resolve_error_kind_counts: [(String::from("Msg"), 2), (String::from("Timeout"), 1)]
                    .into()
            }
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_query_stats_updated() {
        let env = TestEnvironment::new();
        let inspector = fuchsia_inspect::Inspector::new();
        let _query_stats_inspect_node =
            add_query_stats_inspect(inspector.root(), env.stats.clone());
        assert_data_tree!(inspector, root:{
            query_stats: {}
        });

        let () = env
            .run_lookup(|proxy| async move {
                // IP Lookup IPv4 for REMOTE_IPV4_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV4_HOST,
                            fname::LookupIpOptions {
                                ipv4_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Ok(fname::LookupResult {
                        addresses: Some(vec![map_ip(IPV4_HOST)]),
                        ..fname::LookupResult::EMPTY
                    }),
                );
            })
            .await;
        let () = env
            .run_lookup(|proxy| async move {
                // IP Lookup IPv6 for REMOTE_IPV4_HOST.
                assert_eq!(
                    proxy
                        .lookup_ip(
                            REMOTE_IPV4_HOST,
                            fname::LookupIpOptions {
                                ipv6_lookup: Some(true),
                                ..fname::LookupIpOptions::EMPTY
                            }
                        )
                        .await
                        .expect("lookup_ip"),
                    Err(fname::LookupError::NotFound),
                );
            })
            .await;
        assert_data_tree!(inspector, root:{
            query_stats: {
                "window 1": {
                    start_time_nanos: NonZeroUintProperty,
                    successful_queries: 1u64,
                    failed_queries: 1u64,
                    average_success_duration_micros: NonZeroUintProperty,
                    average_failure_duration_micros: NonZeroUintProperty,
                    errors: {
                        Message: 0u64,
                        NoConnections: 0u64,
                        NoRecordsFoundResponseCodeCounts: {
                            NoError: 1u64,
                        },
                        Io: 0u64,
                        Proto: 0u64,
                        Timeout: 0u64,
                        UnhandledResolveErrorKindCounts: {},
                    },
                    address_counts: {
                        "1": 1u64,
                    },
                },
            }
        });
    }

    fn run_fake_lookup(
        exec: &mut fasync::TestExecutor,
        stats: Arc<QueryStats>,
        result: QueryResult<'_>,
        delay: zx::Duration,
    ) {
        let start_time = fasync::Time::now();
        let () = exec.set_fake_time(fasync::Time::after(delay));
        let update_stats = stats.finish_query(start_time, result);
        futures::pin_mut!(update_stats);
        assert!(exec.run_until_stalled(&mut update_stats).is_ready());
    }

    // Safety: This is safe because the initial value is not zero.
    const NON_ZERO_USIZE_ONE: NonZeroUsize =
        const_unwrap::const_unwrap_option(NonZeroUsize::new(1));

    #[test]
    fn test_query_stats_inspect_average() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        const START_NANOS: i64 = 1_234_567;
        let () = exec.set_fake_time(fasync::Time::from_nanos(START_NANOS));

        let stats = Arc::new(QueryStats::new());
        let inspector = fuchsia_inspect::Inspector::new();
        let _query_stats_inspect_node = add_query_stats_inspect(inspector.root(), stats.clone());
        const SUCCESSFUL_QUERY_COUNT: u64 = 10;
        const SUCCESSFUL_QUERY_DURATION: zx::Duration = zx::Duration::from_seconds(30);
        for _ in 0..SUCCESSFUL_QUERY_COUNT / 2 {
            let () = run_fake_lookup(
                &mut exec,
                stats.clone(),
                Ok(/*addresses*/ NON_ZERO_USIZE_ONE),
                zx::Duration::from_nanos(0),
            );
            let () = run_fake_lookup(
                &mut exec,
                stats.clone(),
                Ok(/*addresses*/ NON_ZERO_USIZE_ONE),
                SUCCESSFUL_QUERY_DURATION,
            );
            let () = exec.set_fake_time(fasync::Time::after(
                STAT_WINDOW_DURATION - SUCCESSFUL_QUERY_DURATION,
            ));
        }
        let mut expected = tree_assertion!(query_stats: {});
        for i in 0..SUCCESSFUL_QUERY_COUNT / 2 {
            let name = &format!("window {}", i + 1);
            let child = tree_assertion!(var name: {
                start_time_nanos: u64::try_from(
                    START_NANOS + STAT_WINDOW_DURATION.into_nanos() * i64::try_from(i).unwrap()
                ).unwrap(),
                successful_queries: 2u64,
                failed_queries: 0u64,
                average_success_duration_micros: u64::try_from(
                    SUCCESSFUL_QUERY_DURATION.into_micros()
                ).unwrap() / 2,
                errors: {
                    Message: 0u64,
                    NoConnections: 0u64,
                    NoRecordsFoundResponseCodeCounts: {},
                    Io: 0u64,
                    Proto: 0u64,
                    Timeout: 0u64,
                    UnhandledResolveErrorKindCounts: {},
                },
                address_counts: {
                    "1": 2u64,
                },
            });
            expected.add_child_assertion(child);
        }
        assert_data_tree!(inspector, root: {
            expected,
        });
    }

    #[test]
    fn test_query_stats_inspect_error_counters() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        const START_NANOS: i64 = 1_234_567;
        let () = exec.set_fake_time(fasync::Time::from_nanos(START_NANOS));

        let stats = Arc::new(QueryStats::new());
        let inspector = fuchsia_inspect::Inspector::new();
        let _query_stats_inspect_node = add_query_stats_inspect(inspector.root(), stats.clone());
        const FAILED_QUERY_COUNT: u64 = 10;
        const FAILED_QUERY_DURATION: zx::Duration = zx::Duration::from_millis(500);
        for _ in 0..FAILED_QUERY_COUNT {
            let () = run_fake_lookup(
                &mut exec,
                stats.clone(),
                Err(&ResolveErrorKind::Timeout),
                FAILED_QUERY_DURATION,
            );
        }
        assert_data_tree!(inspector, root:{
            query_stats: {
                "window 1": {
                    start_time_nanos: u64::try_from(
                        START_NANOS + FAILED_QUERY_DURATION.into_nanos()
                    ).unwrap(),
                    successful_queries: 0u64,
                    failed_queries: FAILED_QUERY_COUNT,
                    average_failure_duration_micros: u64::try_from(
                        FAILED_QUERY_DURATION.into_micros()
                    ).unwrap(),
                    errors: {
                        Message: 0u64,
                        NoConnections: 0u64,
                        NoRecordsFoundResponseCodeCounts: {},
                        Io: 0u64,
                        Proto: 0u64,
                        Timeout: FAILED_QUERY_COUNT,
                        UnhandledResolveErrorKindCounts: {},
                    },
                    address_counts: {},
                },
            }
        });
    }

    #[test]
    fn test_query_stats_inspect_no_records_found() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        const START_NANOS: i64 = 1_234_567;
        let () = exec.set_fake_time(fasync::Time::from_nanos(START_NANOS));

        let stats = Arc::new(QueryStats::new());
        let inspector = fuchsia_inspect::Inspector::new();
        let _query_stats_inspect_node = add_query_stats_inspect(inspector.root(), stats.clone());
        const FAILED_QUERY_COUNT: u64 = 10;
        const FAILED_QUERY_DURATION: zx::Duration = zx::Duration::from_millis(500);

        let mut run_fake_no_records_lookup = |response_code: ResponseCode| {
            run_fake_lookup(
                &mut exec,
                stats.clone(),
                Err(&ResolveErrorKind::NoRecordsFound {
                    query: Box::new(Query::default()),
                    soa: None,
                    negative_ttl: None,
                    response_code,
                    trusted: false,
                }),
                FAILED_QUERY_DURATION,
            )
        };

        for _ in 0..FAILED_QUERY_COUNT {
            let () = run_fake_no_records_lookup(ResponseCode::NXDomain);
            let () = run_fake_no_records_lookup(ResponseCode::Refused);
            let () = run_fake_no_records_lookup(4096.into());
            let () = run_fake_no_records_lookup(4097.into());
        }

        assert_data_tree!(inspector, root:{
            query_stats: {
                "window 1": {
                    start_time_nanos: u64::try_from(
                        START_NANOS + FAILED_QUERY_DURATION.into_nanos()
                    ).unwrap(),
                    successful_queries: 0u64,
                    failed_queries: FAILED_QUERY_COUNT * 4,
                    average_failure_duration_micros: u64::try_from(
                        FAILED_QUERY_DURATION.into_micros()
                    ).unwrap(),
                    errors: {
                        Message: 0u64,
                        NoConnections: 0u64,
                        NoRecordsFoundResponseCodeCounts: {
                          NXDomain: FAILED_QUERY_COUNT,
                          Refused: FAILED_QUERY_COUNT,
                          "Unknown(4096)": FAILED_QUERY_COUNT,
                          "Unknown(4097)": FAILED_QUERY_COUNT,
                        },
                        Io: 0u64,
                        Proto: 0u64,
                        Timeout: 0u64,
                        UnhandledResolveErrorKindCounts: {},
                    },
                    address_counts: {},
                },
            }
        });
    }

    #[test]
    fn test_query_stats_resolved_address_counts() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        const START_NANOS: i64 = 1_234_567;
        exec.set_fake_time(fasync::Time::from_nanos(START_NANOS));

        let stats = Arc::new(QueryStats::new());
        let inspector = fuchsia_inspect::Inspector::new();
        let _query_stats_inspect_node = add_query_stats_inspect(inspector.root(), stats.clone());

        // Create some test data to run fake lookups. Simulate a histogram with:
        //  - 99 occurrences of a response with 1 address,
        //  - 98 occurrences of a response with 2 addresses,
        //  - ...
        //  - 1 occurrence of a response with 99 addresses.
        let address_counts: HashMap<usize, _> = (1..100).zip((1..100).rev()).collect();
        const QUERY_DURATION: zx::Duration = zx::Duration::from_millis(10);
        for (count, occurrences) in address_counts.iter() {
            for _ in 0..*occurrences {
                run_fake_lookup(
                    &mut exec,
                    stats.clone(),
                    Ok(NonZeroUsize::new(*count).expect("address count must be greater than zero")),
                    QUERY_DURATION,
                );
            }
        }

        let mut expected_address_counts = tree_assertion!(address_counts: {});
        for (count, occurrences) in address_counts.iter() {
            expected_address_counts
                .add_property_assertion(&count.to_string(), Box::new(*occurrences));
        }
        assert_data_tree!(inspector, root: {
            query_stats: {
                "window 1": {
                    start_time_nanos: u64::try_from(
                        START_NANOS + QUERY_DURATION.into_nanos()
                    ).unwrap(),
                    successful_queries: address_counts.values().sum::<u64>(),
                    failed_queries: 0u64,
                    average_success_duration_micros: u64::try_from(
                        QUERY_DURATION.into_micros()
                    ).unwrap(),
                    errors: {
                        Message: 0u64,
                        NoConnections: 0u64,
                        NoRecordsFoundResponseCodeCounts: {},
                        Io: 0u64,
                        Proto: 0u64,
                        Timeout: 0u64,
                        UnhandledResolveErrorKindCounts: {},
                    },
                    expected_address_counts,
                },
            },
        });
    }

    #[test]
    fn test_query_stats_inspect_oldest_stats_erased() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        const START_NANOS: i64 = 1_234_567;
        let () = exec.set_fake_time(fasync::Time::from_nanos(START_NANOS));

        let stats = Arc::new(QueryStats::new());
        let inspector = fuchsia_inspect::Inspector::new();
        let _query_stats_inspect_node = add_query_stats_inspect(inspector.root(), stats.clone());
        const DELAY: zx::Duration = zx::Duration::from_millis(100);
        for _ in 0..STAT_WINDOW_COUNT {
            let () =
                run_fake_lookup(&mut exec, stats.clone(), Err(&ResolveErrorKind::Timeout), DELAY);
            let () = exec.set_fake_time(fasync::Time::after(STAT_WINDOW_DURATION - DELAY));
        }
        for _ in 0..STAT_WINDOW_COUNT {
            let () = run_fake_lookup(
                &mut exec,
                stats.clone(),
                Ok(/*addresses*/ NON_ZERO_USIZE_ONE),
                DELAY,
            );
            let () = exec.set_fake_time(fasync::Time::after(STAT_WINDOW_DURATION - DELAY));
        }
        // All the failed queries should be erased from the stats as they are
        // now out of date.
        let mut expected = tree_assertion!(query_stats: {});
        let start_offset = START_NANOS
            + DELAY.into_nanos()
            + STAT_WINDOW_DURATION.into_nanos() * i64::try_from(STAT_WINDOW_COUNT).unwrap();
        for i in 0..STAT_WINDOW_COUNT {
            let name = &format!("window {}", i + 1);
            let child = tree_assertion!(var name: {
                start_time_nanos: u64::try_from(
                    start_offset + STAT_WINDOW_DURATION.into_nanos() * i64::try_from(i).unwrap()
                ).unwrap(),
                successful_queries: 1u64,
                failed_queries: 0u64,
                average_success_duration_micros: u64::try_from(DELAY.into_micros()).unwrap(),
                errors: {
                    Message: 0u64,
                    NoConnections: 0u64,
                    NoRecordsFoundResponseCodeCounts: {},
                    Io: 0u64,
                    Proto: 0u64,
                    Timeout: 0u64,
                    UnhandledResolveErrorKindCounts: {},
                },
                address_counts: {
                    "1": 1u64,
                },
            });
            expected.add_child_assertion(child);
        }
        assert_data_tree!(inspector, root: {
            expected,
        });
    }

    struct BlockingResolver {}

    #[async_trait]
    impl ResolverLookup for BlockingResolver {
        fn new(_config: ResolverConfig, _options: ResolverOpts) -> Self {
            BlockingResolver {}
        }

        async fn lookup<N: IntoName + Send>(
            &self,
            _name: N,
            _record_type: RecordType,
        ) -> Result<lookup::Lookup, ResolveError> {
            futures::future::pending().await
        }

        async fn reverse_lookup(
            &self,
            _addr: IpAddr,
        ) -> Result<lookup::ReverseLookup, ResolveError> {
            panic!("BlockingResolver does not handle reverse lookup")
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parallel_query_limit() {
        // Collect requests by setting up a FIDL proxy and stream for the Lookup
        // protocol, because there isn't a good way to directly construct fake
        // requests to be used for testing.
        let requests = {
            let (name_lookup_proxy, name_lookup_stream) =
                fidl::endpoints::create_proxy_and_stream::<fname::LookupMarker>()
                    .expect("failed to create LookupProxy");
            const NUM_REQUESTS: usize = MAX_PARALLEL_REQUESTS * 2 + 2;
            for _ in 0..NUM_REQUESTS {
                // Don't await on this future because we are using these
                // requests to collect FIDL responders in order to send test
                // requests later, and will not respond to these requests.
                let _: fidl::client::QueryResponseFut<fname::LookupLookupIpResult> =
                    name_lookup_proxy.lookup_ip(
                        LOCAL_HOST,
                        fname::LookupIpOptions {
                            ipv4_lookup: Some(true),
                            ipv6_lookup: Some(true),
                            ..fname::LookupIpOptions::EMPTY
                        },
                    );
            }
            // Terminate the stream so its items can be collected below.
            drop(name_lookup_proxy);
            let requests = name_lookup_stream
                .map(|request| match request.expect("channel error") {
                    LookupRequest::LookupIp { hostname, options, responder } => {
                        IpLookupRequest { hostname, options, responder }
                    }
                    req => panic!("Expected LookupRequest::LookupIp request, found {:?}", req),
                })
                .collect::<Vec<_>>()
                .await;
            assert_eq!(requests.len(), NUM_REQUESTS);
            requests
        };

        let (mut sender, recv) = mpsc::channel(MAX_PARALLEL_REQUESTS);

        // The channel's capacity is equal to buffer + num-senders. Thus the
        // channel has a capacity of `MAX_PARALLEL_REQUESTS` + 1, and the
        // `for_each_concurrent` future has a limit of `MAX_PARALLEL_REQUESTS`,
        // so the sender should be able to queue `MAX_PARALLEL_REQUESTS` * 2 + 1
        // requests before `send` fails.
        const BEFORE_LAST_INDEX: usize = MAX_PARALLEL_REQUESTS * 2;
        const LAST_INDEX: usize = MAX_PARALLEL_REQUESTS * 2 + 1;
        let send_fut = async {
            for (i, req) in requests.into_iter().enumerate() {
                match i {
                    BEFORE_LAST_INDEX => assert_matches!(sender.try_send(req), Ok(())),
                    LAST_INDEX => assert_matches!(sender.try_send(req), Err(e) if e.is_full()),
                    _ => assert_matches!(sender.send(req).await, Ok(())),
                }
            }
        }
        .fuse();
        let recv_fut = {
            let resolver = SharedResolver::new(BlockingResolver::new(
                ResolverConfig::default(),
                ResolverOpts::default(),
            ));
            let stats = Arc::new(QueryStats::new());
            let (routes_proxy, _routes_stream) =
                fidl::endpoints::create_proxy_and_stream::<fnet_routes::StateMarker>()
                    .expect("failed to create routes.StateProxy");
            async move { create_ip_lookup_fut(&resolver, stats.clone(), routes_proxy, recv).await }
                .fuse()
        };
        futures::pin_mut!(send_fut, recv_fut);
        futures::select! {
            () = send_fut => {},
            () = recv_fut => panic!("recv_fut should never complete"),
        };
    }

    #[test]
    fn test_failure_stats() {
        use anyhow::anyhow;
        use trust_dns_proto::{error::ProtoError, op::Query};

        let mut stats = FailureStats::default();
        for (error_kind, expected) in &[
            (ResolveErrorKind::Message("foo"), FailureStats { message: 1, ..Default::default() }),
            (
                ResolveErrorKind::Msg("foo".to_string()),
                FailureStats { message: 2, ..Default::default() },
            ),
            (
                ResolveErrorKind::NoRecordsFound {
                    query: Box::new(Query::default()),
                    soa: None,
                    negative_ttl: None,
                    response_code: ResponseCode::Refused,
                    trusted: false,
                },
                FailureStats {
                    message: 2,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts: [(ResponseCode::Refused.into(), 1)].into(),
                    },
                    ..Default::default()
                },
            ),
            (
                ResolveErrorKind::Io(std::io::Error::new(
                    std::io::ErrorKind::NotFound,
                    anyhow!("foo"),
                )),
                FailureStats {
                    message: 2,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts: [(ResponseCode::Refused.into(), 1)].into(),
                    },
                    io: 1,
                    ..Default::default()
                },
            ),
            (
                ResolveErrorKind::Proto(ProtoError::from("foo")),
                FailureStats {
                    message: 2,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts: [(ResponseCode::Refused.into(), 1)].into(),
                    },
                    io: 1,
                    proto: 1,
                    ..Default::default()
                },
            ),
            (
                ResolveErrorKind::NoConnections,
                FailureStats {
                    message: 2,
                    no_connections: 1,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts: [(ResponseCode::Refused.into(), 1)].into(),
                    },
                    io: 1,
                    proto: 1,
                    ..Default::default()
                },
            ),
            (
                ResolveErrorKind::Timeout,
                FailureStats {
                    message: 2,
                    no_connections: 1,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts: [(ResponseCode::Refused.into(), 1)].into(),
                    },
                    io: 1,
                    proto: 1,
                    timeout: 1,
                    unhandled_resolve_error_kind: Default::default(),
                },
            ),
            (
                ResolveErrorKind::NoRecordsFound {
                    query: Box::new(Query::default()),
                    soa: None,
                    negative_ttl: None,
                    response_code: ResponseCode::NXDomain,
                    trusted: false,
                },
                FailureStats {
                    message: 2,
                    no_connections: 1,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts: [
                            (ResponseCode::NXDomain.into(), 1),
                            (ResponseCode::Refused.into(), 1),
                        ]
                        .into(),
                    },
                    io: 1,
                    proto: 1,
                    timeout: 1,
                    unhandled_resolve_error_kind: Default::default(),
                },
            ),
            (
                ResolveErrorKind::NoRecordsFound {
                    query: Box::new(Query::default()),
                    soa: None,
                    negative_ttl: None,
                    response_code: ResponseCode::NXDomain,
                    trusted: false,
                },
                FailureStats {
                    message: 2,
                    no_connections: 1,
                    no_records_found: NoRecordsFoundStats {
                        response_code_counts: [
                            (ResponseCode::NXDomain.into(), 2),
                            (ResponseCode::Refused.into(), 1),
                        ]
                        .into(),
                    },
                    io: 1,
                    proto: 1,
                    timeout: 1,
                    unhandled_resolve_error_kind: Default::default(),
                },
            ),
        ][..]
        {
            let () = stats.increment(error_kind);
            assert_eq!(&stats, expected, "invalid stats after incrementing with {:?}", error_kind);
        }
    }

    fn test_das_helper(
        l_addr: fnet::IpAddress,
        l_src: Option<fnet::IpAddress>,
        r_addr: fnet::IpAddress,
        r_src: Option<fnet::IpAddress>,
        want: std::cmp::Ordering,
    ) {
        let left = DasCmpInfo::from_addrs(&l_addr, l_src.as_ref());
        let right = DasCmpInfo::from_addrs(&r_addr, r_src.as_ref());
        assert_eq!(
            left.cmp(&right),
            want,
            "want = {:?}\n left = {:?}({:?}) DAS={:?}\n right = {:?}({:?}) DAS={:?}",
            want,
            l_addr,
            l_src,
            left,
            r_addr,
            r_src,
            right
        );
    }

    macro_rules! add_das_test {
        ($name:ident, preferred: $pref_dst:expr => $pref_src:expr, other: $other_dst:expr => $other_src:expr) => {
            #[test]
            fn $name() {
                test_das_helper(
                    $pref_dst,
                    $pref_src,
                    $other_dst,
                    $other_src,
                    std::cmp::Ordering::Less,
                )
            }
        };
    }

    add_das_test!(
        prefer_reachable,
        preferred: fidl_ip!("198.51.100.121") => Some(fidl_ip!("198.51.100.117")),
        other: fidl_ip!("2001:db8:1::1") => Option::<fnet::IpAddress>::None
    );

    // These test cases are taken from RFC 6724, section 10.2.

    add_das_test!(
        prefer_matching_scope,
        preferred: fidl_ip!("198.51.100.121") => Some(fidl_ip!("198.51.100.117")),
        other: fidl_ip!("2001:db8:1::1") => Some(fidl_ip!("fe80::1"))
    );

    add_das_test!(
        prefer_matching_label,
        preferred: fidl_ip!("2002:c633:6401::1") => Some(fidl_ip!("2002:c633:6401::2")),
        other:  fidl_ip!("2001:db8:1::1") => Some(fidl_ip!("2002:c633:6401::2"))
    );

    add_das_test!(
        prefer_higher_precedence_1,
        preferred: fidl_ip!("2001:db8:1::1") => Some(fidl_ip!("2001:db8:1::2")),
        other: fidl_ip!("10.1.2.3") => Some(fidl_ip!("10.1.2.4"))
    );

    add_das_test!(
        prefer_higher_precedence_2,
        preferred: fidl_ip!("2001:db8:1::1") => Some(fidl_ip!("2001:db8:1::2")),
        other: fidl_ip!("2002:c633:6401::1") => Some(fidl_ip!("2002:c633:6401::2"))
    );

    add_das_test!(
        prefer_smaller_scope,
        preferred: fidl_ip!("fe80::1") => Some(fidl_ip!("fe80::2")),
        other: fidl_ip!("2001:db8:1::1") => Some(fidl_ip!("2001:db8:1::2"))
    );

    add_das_test!(
        prefer_longest_matching_prefix,
        preferred: fidl_ip!("2001:db8:1::1") => Some(fidl_ip!("2001:db8:1::2")),
        other: fidl_ip!("2001:db8:3ffe::1") => Some(fidl_ip!("2001:db8:3f44::2"))
    );

    #[test]
    fn test_das_equals() {
        for (dst, src) in [
            (fidl_ip!("192.168.0.1"), fidl_ip!("192.168.0.2")),
            (fidl_ip!("2001:db8::1"), fidl_ip!("2001:db8::2")),
        ]
        .iter()
        {
            let () = test_das_helper(*dst, None, *dst, None, std::cmp::Ordering::Equal);
            let () = test_das_helper(*dst, Some(*src), *dst, Some(*src), std::cmp::Ordering::Equal);
        }
    }

    #[test]
    fn test_valid_policy_table() {
        // Last element in policy table MUST be ::/0.
        assert_eq!(
            POLICY_TABLE.iter().last().expect("empty policy table").prefix,
            net_types::ip::Subnet::new(net_types::ip::Ipv6::UNSPECIFIED_ADDRESS, 0)
                .expect("invalid subnet")
        );
        // Policy table must be sorted by prefix length.
        let () = POLICY_TABLE.windows(2).for_each(|w| {
            let Policy { prefix: cur, precedence: _, label: _ } = w[0];
            let Policy { prefix: nxt, precedence: _, label: _ } = w[1];
            assert!(
                cur.prefix() >= nxt.prefix(),
                "bad ordering of prefixes, {} must come after {}",
                cur,
                nxt
            )
        });
        // Assert that POLICY_TABLE declaration does not use any invalid
        // subnets.
        for policy in POLICY_TABLE.iter() {
            assert!(policy.prefix.prefix() <= 128, "Invalid subnet in policy {:?}", policy);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sort_preferred_addresses() {
        const TEST_IPS: [(fnet::IpAddress, Option<fnet::IpAddress>); 5] = [
            (fidl_ip!("127.0.0.1"), Some(fidl_ip!("127.0.0.1"))),
            (fidl_ip!("::1"), Some(fidl_ip!("::1"))),
            (fidl_ip!("192.168.50.22"), None),
            (fidl_ip!("2001::2"), None),
            (fidl_ip!("2001:db8:1::1"), Some(fidl_ip!("2001:db8:1::2"))),
        ];
        // Declared using std types so we get cleaner output when we assert
        // expectations.
        const SORTED: [IpAddr; 5] = [
            std_ip!("::1"),
            std_ip!("2001:db8:1::1"),
            std_ip!("127.0.0.1"),
            std_ip!("192.168.50.22"),
            std_ip!("2001::2"),
        ];
        let (routes_proxy, routes_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet_routes::StateMarker>()
                .expect("failed to create routes.StateProxy");
        let routes_fut = routes_stream.map(|r| r.context("stream FIDL error")).try_for_each(
            |fnet_routes::StateRequest::Resolve { destination, responder }| {
                let mut result = TEST_IPS
                    .iter()
                    .enumerate()
                    .find_map(|(i, (dst, src))| {
                        if *dst == destination && src.is_some() {
                            let inner = fnet_routes::Destination {
                                address: Some(*dst),
                                source_address: *src,
                                ..fnet_routes::Destination::EMPTY
                            };
                            // Send both Direct and Gateway resolved routes to show we
                            // don't care about that part.
                            if i % 2 == 0 {
                                Some(fnet_routes::Resolved::Direct(inner))
                            } else {
                                Some(fnet_routes::Resolved::Gateway(inner))
                            }
                        } else {
                            None
                        }
                    })
                    .ok_or(zx::Status::ADDRESS_UNREACHABLE.into_raw());
                futures::future::ready(
                    responder.send(&mut result).context("failed to send Resolve response"),
                )
            },
        );

        let ((), ()) = futures::future::try_join(routes_fut, async move {
            let addrs = TEST_IPS.iter().map(|(dst, _src)| *dst).collect();
            let addrs = sort_preferred_addresses(addrs, &routes_proxy)
                .await
                .expect("failed to sort addresses");
            let addrs = addrs
                .into_iter()
                .map(|a| {
                    let net_ext::IpAddress(a) = a.into();
                    a
                })
                .collect::<Vec<_>>();
            assert_eq!(&addrs[..], &SORTED[..]);
            Ok(())
        })
        .await
        .expect("error running futures");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_lookupip() {
        // Routes handler will say that only IPV6_HOST is reachable.
        let routes_handler = |fnet_routes::StateRequest::Resolve { destination, responder }| {
            let mut response = if destination == map_ip(IPV6_HOST) {
                Ok(fnet_routes::Resolved::Direct(fnet_routes::Destination {
                    address: Some(destination),
                    source_address: Some(destination),
                    ..fnet_routes::Destination::EMPTY
                }))
            } else {
                Err(zx::Status::ADDRESS_UNREACHABLE.into_raw())
            };
            let () = responder.send(&mut response).expect("failed to send Resolve FIDL response");
        };
        TestEnvironment::new()
            .run_lookup_with_routes_handler(
                |proxy| async move {
                    // All arguments unset.
                    assert_eq!(
                        proxy
                            .lookup_ip(
                                REMOTE_IPV4_HOST,
                                fname::LookupIpOptions { ..fname::LookupIpOptions::EMPTY }
                            )
                            .await
                            .expect("lookup_ip"),
                        Err(fname::LookupError::InvalidArgs)
                    );
                    // No IP addresses to look.
                    assert_eq!(
                        proxy
                            .lookup_ip(
                                REMOTE_IPV4_HOST,
                                fname::LookupIpOptions {
                                    ipv4_lookup: Some(false),
                                    ipv6_lookup: Some(false),
                                    ..fname::LookupIpOptions::EMPTY
                                }
                            )
                            .await
                            .expect("lookup_ip"),
                        Err(fname::LookupError::InvalidArgs)
                    );
                    // No results for an IPv4 only host.
                    assert_eq!(
                        proxy
                            .lookup_ip(
                                REMOTE_IPV4_HOST,
                                fname::LookupIpOptions {
                                    ipv4_lookup: Some(false),
                                    ipv6_lookup: Some(true),
                                    ..fname::LookupIpOptions::EMPTY
                                }
                            )
                            .await
                            .expect("lookup_ip"),
                        Err(fname::LookupError::NotFound)
                    );
                    // Successfully resolve IPv4.
                    assert_eq!(
                        proxy
                            .lookup_ip(
                                REMOTE_IPV4_HOST,
                                fname::LookupIpOptions {
                                    ipv4_lookup: Some(true),
                                    ipv6_lookup: Some(true),
                                    ..fname::LookupIpOptions::EMPTY
                                }
                            )
                            .await
                            .expect("lookup_ip"),
                        Ok(fname::LookupResult {
                            addresses: Some(vec![map_ip(IPV4_HOST)]),
                            ..fname::LookupResult::EMPTY
                        })
                    );
                    // Successfully resolve IPv4 + IPv6 (no sorting).
                    assert_eq!(
                        proxy
                            .lookup_ip(
                                REMOTE_IPV4_IPV6_HOST,
                                fname::LookupIpOptions {
                                    ipv4_lookup: Some(true),
                                    ipv6_lookup: Some(true),
                                    ..fname::LookupIpOptions::EMPTY
                                }
                            )
                            .await
                            .expect("lookup_ip"),
                        Ok(fname::LookupResult {
                            addresses: Some(vec![map_ip(IPV4_HOST), map_ip(IPV6_HOST)]),
                            ..fname::LookupResult::EMPTY
                        })
                    );
                    // Successfully resolve IPv4 + IPv6 (with sorting).
                    assert_eq!(
                        proxy
                            .lookup_ip(
                                REMOTE_IPV4_IPV6_HOST,
                                fname::LookupIpOptions {
                                    ipv4_lookup: Some(true),
                                    ipv6_lookup: Some(true),
                                    sort_addresses: Some(true),
                                    ..fname::LookupIpOptions::EMPTY
                                }
                            )
                            .await
                            .expect("lookup_ip"),
                        Ok(fname::LookupResult {
                            addresses: Some(vec![map_ip(IPV6_HOST), map_ip(IPV4_HOST)]),
                            ..fname::LookupResult::EMPTY
                        })
                    );
                },
                routes_handler,
            )
            .await
    }
}

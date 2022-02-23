// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Core DHCPv6 client state transitions.

use assert_matches::assert_matches;
use num::{rational::Ratio, CheckedMul};
use packet::serialize::InnerPacketBuilder;
use packet_formats_dhcp::v6;
use rand::{thread_rng, Rng};
use std::{
    cmp::{Eq, Ord, PartialEq, PartialOrd},
    collections::{hash_map::Entry, BinaryHeap, HashMap},
    convert::TryFrom,
    default::Default,
    net::Ipv6Addr,
    time::{Duration, Instant},
};
use zerocopy::ByteSlice;

/// Initial Information-request timeout `INF_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INITIAL_INFO_REQ_TIMEOUT: Duration = Duration::from_secs(1);
/// Max Information-request timeout `INF_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const MAX_INFO_REQ_TIMEOUT: Duration = Duration::from_secs(3600);
/// Default information refresh time from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const IRT_DEFAULT: Duration = Duration::from_secs(86400);

/// The max duration in seconds `std::time::Duration` supports.
///
/// NOTE: it is possible for `Duration` to be bigger by filling in the nanos
/// field, but this value is good enough for the purpose of this crate.
const MAX_DURATION: Duration = Duration::from_secs(std::u64::MAX);

/// Initial Solicit timeout `SOL_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INITIAL_SOLICIT_TIMEOUT: Duration = Duration::from_secs(1);

/// Max Solicit timeout `SOL_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const MAX_SOLICIT_TIMEOUT: Duration = Duration::from_secs(3600);

/// The valid range for `SOL_MAX_RT`, as defined in [RFC 8415, Section 21.24].
///
/// [RFC 8415, Section 21.24](https://datatracker.ietf.org/doc/html/rfc8415#section-21.24)
const VALID_MAX_SOLICIT_TIMEOUT_RANGE: std::ops::RangeInclusive<u32> = 60..=86400;

/// The maximum [Preference option] value that can be present in an advertise,
/// as described in [RFC 8415, Section 18.2.1].
///
/// [RFC 8415, Section 18.2.1]: https://datatracker.ietf.org/doc/html/rfc8415#section-18.2.1
/// [Preference option]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.8
const ADVERTISE_MAX_PREFERENCE: u8 = std::u8::MAX;

/// Denominator used for transforming the elapsed time from milliseconds to
/// hundredths of a second.
///
/// [RFC 8415, Section 21.9]: https://tools.ietf.org/html/rfc8415#section-21.9
const ELAPSED_TIME_DENOMINATOR: u128 = 10;

/// The length of the [Client Identifier].
///
/// [Client Identifier]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.2
const CLIENT_ID_LEN: usize = 18;

/// The minimum value for the randomization factor `RAND` used in calculating
/// retransmission timeout, as specified in [RFC 8415, Section 15].
///
/// [RFC 8415, Section 15](https://datatracker.ietf.org/doc/html/rfc8415#section-15)
const RANDOMIZATION_FACTOR_MIN: f64 = -0.1;

/// The maximum value for the randomization factor `RAND` used in calculating
/// retransmission timeout, as specified in [RFC 8415, Section 15].
///
/// [RFC 8415, Section 15](https://datatracker.ietf.org/doc/html/rfc8415#section-15)
const RANDOMIZATION_FACTOR_MAX: f64 = 0.1;

/// Initial Request timeout `REQ_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INITIAL_REQUEST_TIMEOUT: Duration = Duration::from_secs(1);

/// Max Request timeout `REQ_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const MAX_REQUEST_TIMEOUT: Duration = Duration::from_secs(30);

/// Max Request retry attempts `REQ_MAX_RC` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const REQUEST_MAX_RC: u8 = 10;

/// The ratio used for calculating T1 based on the shortest preferred lifetime,
/// when the T1 value received from the server is 0.
///
/// When T1 is set to 0 by the server, the value is left to the discretion of
/// the client, as described in [RFC 8415, Section 14.2]. The client computes
/// T1 using the recommended ratio from [RFC 8415, Section 21.4]:
///    T1 = shortest lifetime * 0.5
///
/// [RFC 8415, Section 14.2]: https://datatracker.ietf.org/doc/html/rfc8415#section-14.2
/// [RFC 8415, Section 21.4]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.4
const T1_MIN_LIFETIME_RATIO: Ratio<u32> = Ratio::new_raw(1, 2);

/// The ratio used for calculating T2 based on T1, when the T2 value received
/// from the server is 0.
///
/// When T2 is set to 0 by the server, the value is left to the discretion of
/// the client, as described in [RFC 8415, Section 14.2]. The client computes
/// T2 using the recommended ratios from [RFC 8415, Section 21.4]:
///    T2 = T1 * 0.8 / 0.5
///
/// [RFC 8415, Section 14.2]: https://datatracker.ietf.org/doc/html/rfc8415#section-14.2
/// [RFC 8415, Section 21.4]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.4
const T2_T1_RATIO: Ratio<u32> = Ratio::new_raw(8, 5);

/// Initial Renew timeout `REN_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INITIAL_RENEW_TIMEOUT: Duration = Duration::from_secs(10);

/// Max Renew timeout `REN_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const MAX_RENEW_TIMEOUT: Duration = Duration::from_secs(600);

/// Calculates retransmission timeout based on formulas defined in [RFC 8415, Section 15].
/// A zero `prev_retrans_timeout` indicates this is the first transmission, so
/// `initial_retrans_timeout` will be used.
///
/// Relevant formulas from [RFC 8415, Section 15]:
///
/// ```text
/// RT      Retransmission timeout
/// IRT     Initial retransmission time
/// MRT     Maximum retransmission time
/// RAND    Randomization factor
///
/// RT for the first message transmission is based on IRT:
///
///     RT = IRT + RAND*IRT
///
/// RT for each subsequent message transmission is based on the previous value of RT:
///
///     RT = 2*RTprev + RAND*RTprev
///
/// MRT specifies an upper bound on the value of RT (disregarding the randomization added by
/// the use of RAND).  If MRT has a value of 0, there is no upper limit on the value of RT.
/// Otherwise:
///
///     if (RT > MRT)
///         RT = MRT + RAND*MRT
/// ```
///
/// [RFC 8415, Section 15]: https://tools.ietf.org/html/rfc8415#section-15
fn retransmission_timeout<R: Rng>(
    prev_retrans_timeout: Duration,
    initial_retrans_timeout: Duration,
    max_retrans_timeout: Duration,
    rng: &mut R,
) -> Duration {
    let rand = rng.gen_range(RANDOMIZATION_FACTOR_MIN..RANDOMIZATION_FACTOR_MAX);

    let next_rt = if prev_retrans_timeout.as_nanos() == 0 {
        let irt = initial_retrans_timeout.as_secs_f64();
        irt + rand * irt
    } else {
        let rt = prev_retrans_timeout.as_secs_f64();
        2. * rt + rand * rt
    };

    if max_retrans_timeout.as_nanos() == 0 || next_rt < max_retrans_timeout.as_secs_f64() {
        clipped_duration(next_rt)
    } else {
        let mrt = max_retrans_timeout.as_secs_f64();
        clipped_duration(mrt + rand * mrt)
    }
}

/// Clips overflow and returns a duration using the input seconds.
fn clipped_duration(secs: f64) -> Duration {
    if secs <= 0. {
        Duration::from_nanos(0)
    } else if secs >= MAX_DURATION.as_secs_f64() {
        MAX_DURATION
    } else {
        Duration::from_secs_f64(secs)
    }
}

/// Creates a transaction ID used by the client to match outgoing messages with
/// server replies, as defined in [RFC 8415, Section 16.1].
///
/// [RFC 8415, Section 16.1]: https://tools.ietf.org/html/rfc8415#section-16.1
pub fn transaction_id() -> [u8; 3] {
    let mut id = [0u8; 3];
    thread_rng().fill(&mut id[..]);
    id
}

/// Identifies what event should be triggered when a timer fires.
#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone)]
pub enum ClientTimerType {
    Retransmission,
    Refresh,
    Renew,
}

/// Possible actions that need to be taken for a state transition to happen successfully.
#[derive(Debug, PartialEq, Clone)]
pub enum Action {
    SendMessage(Vec<u8>),
    ScheduleTimer(ClientTimerType, Duration),
    CancelTimer(ClientTimerType),
    UpdateDnsServers(Vec<Ipv6Addr>),
}

pub type Actions = Vec<Action>;

/// Holds data and provides methods for handling state transitions from information requesting
/// state.
#[derive(Debug)]
struct InformationRequesting {
    retrans_timeout: Duration,
}

impl InformationRequesting {
    /// Starts in information requesting state following [RFC 8415, Section 18.2.6].
    ///
    /// [RFC 8415, Section 18.2.6]: https://tools.ietf.org/html/rfc8415#section-18.2.6
    fn start<R: Rng>(
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let info_req = Self { retrans_timeout: Default::default() };
        info_req.send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }

    /// Calculates timeout for retransmitting information requests using parameters specified in
    /// [RFC 8415, Section 18.2.6].
    ///
    /// [RFC 8415, Section 18.2.6]: https://tools.ietf.org/html/rfc8415#section-18.2.6
    fn retransmission_timeout<R: Rng>(&self, rng: &mut R) -> Duration {
        let Self { retrans_timeout } = self;
        retransmission_timeout(
            *retrans_timeout,
            INITIAL_INFO_REQ_TIMEOUT,
            MAX_INFO_REQ_TIMEOUT,
            rng,
        )
    }

    /// A helper function that returns a transition back to `InformationRequesting`, with actions
    /// to send an information request and schedules retransmission.
    fn send_and_schedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let options_array = [v6::DhcpOption::Oro(options_to_request)];
        let options = if options_to_request.is_empty() { &[][..] } else { &options_array[..] };

        let builder =
            v6::MessageBuilder::new(v6::MessageType::InformationRequest, transaction_id, options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);

        let retrans_timeout = self.retransmission_timeout(rng);

        Transition {
            state: ClientState::InformationRequesting(InformationRequesting { retrans_timeout }),
            actions: vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
            transaction_id: None,
        }
    }

    /// Retransmits information request.
    fn retransmission_timer_expired<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        self.send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }

    /// Handles reply to information requests based on [RFC 8415, Section 18.2.10.4].
    ///
    /// [RFC 8415, Section 18.2.10.4]: https://tools.ietf.org/html/rfc8415#section-18.2.10.4
    fn reply_message_received<B: ByteSlice>(self, msg: v6::Message<'_, B>) -> Transition {
        let mut information_refresh_time = IRT_DEFAULT;
        let mut dns_servers: Option<Vec<Ipv6Addr>> = None;

        for opt in msg.options() {
            match opt {
                v6::ParsedDhcpOption::InformationRefreshTime(refresh_time) => {
                    information_refresh_time = Duration::from_secs(u64::from(refresh_time))
                }
                v6::ParsedDhcpOption::DnsServers(server_addrs) => dns_servers = Some(server_addrs),
                v6::ParsedDhcpOption::DomainList(_) => {
                    // TODO(https://fxbug.dev/87176) implement domain list.
                }
                v6::ParsedDhcpOption::ClientId(_)
                | v6::ParsedDhcpOption::ServerId(_)
                | v6::ParsedDhcpOption::SolMaxRt(_)
                | v6::ParsedDhcpOption::Preference(_)
                | v6::ParsedDhcpOption::Iana(_)
                | v6::ParsedDhcpOption::IaAddr(_)
                | v6::ParsedDhcpOption::Oro(_)
                | v6::ParsedDhcpOption::ElapsedTime(_)
                | v6::ParsedDhcpOption::StatusCode(_, _) => {
                    // TODO(https://fxbug.dev/48867): emit more actions for other options received.
                }
            }
        }

        let actions = IntoIterator::into_iter([
            Action::CancelTimer(ClientTimerType::Retransmission),
            Action::ScheduleTimer(ClientTimerType::Refresh, information_refresh_time),
        ])
        .chain(dns_servers.clone().map(|server_addrs| Action::UpdateDnsServers(server_addrs)))
        .collect::<Vec<_>>();

        Transition {
            state: ClientState::InformationReceived(InformationReceived {
                dns_servers: dns_servers.unwrap_or(Vec::new()),
            }),
            actions,
            transaction_id: None,
        }
    }
}

/// Provides methods for handling state transitions from information received state.
#[derive(Debug)]
struct InformationReceived {
    /// Stores the DNS servers received from the reply.
    dns_servers: Vec<Ipv6Addr>,
}

impl InformationReceived {
    /// Refreshes information by starting another round of information request.
    fn refresh_timer_expired<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        InformationRequesting::start(transaction_id, options_to_request, rng)
    }
}

#[derive(Debug, PartialEq, Clone)]
struct IdentityAssociation {
    // TODO(https://fxbug.dev/86950): use UnicastAddr.
    address: Ipv6Addr,
    preferred_lifetime: v6::TimeValue,
    valid_lifetime: v6::TimeValue,
}

// Holds the information received in an Advertise message.
#[derive(Debug, Clone)]
struct AdvertiseMessage {
    server_id: Vec<u8>,
    addresses: HashMap<v6::IAID, IdentityAssociation>,
    dns_servers: Vec<Ipv6Addr>,
    preference: u8,
    receive_time: Instant,
    preferred_addresses_count: usize,
}

impl AdvertiseMessage {
    fn is_complete(
        &self,
        configured_addresses: &HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: &[v6::OptionCode],
    ) -> bool {
        let Self {
            server_id,
            addresses,
            dns_servers,
            preference: _,
            receive_time: _,
            preferred_addresses_count,
        } = self;
        // TODO(fxbug.dev/69696): remove assert once `server_id` is used in
        // Requesting implementation; Temporary use of `server_id` to avoid
        // dead code warning.
        assert!(!server_id.is_empty());
        addresses.len() >= configured_addresses.keys().len()
            && *preferred_addresses_count
                == configured_addresses
                    .values()
                    .filter(|&value| value.is_some())
                    .collect::<Vec<_>>()
                    .len()
            && options_to_request.contains(&v6::OptionCode::DnsServers) == !dns_servers.is_empty()
    }
}

// Orders Advertise by address count, then preference, dns servers count, and
// earliest receive time. This ordering gives precedence to higher address
// count over preference, to maximise the number of assigned addresses, as
// described in RFC 8415, section 18.2.9:
//
//    Those Advertise messages with the highest server preference value SHOULD
//    be preferred over all other Advertise messages. The client MAY choose a
//    less preferred server if that server has a better set of advertised
//    parameters, such as the available set of IAs.
//
// Between two Advertise of equal address count and preference, the Advertise
// received earliest is greater.
impl Ord for AdvertiseMessage {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        let Self {
            server_id: _,
            addresses,
            dns_servers,
            preference,
            receive_time,
            preferred_addresses_count,
        } = self;
        let Self {
            server_id: _,
            addresses: other_addresses,
            dns_servers: other_dns_server,
            preference: other_preference,
            receive_time: other_receive_time,
            preferred_addresses_count: other_preferred_addresses_count,
        } = other;
        (
            addresses.len(),
            *preferred_addresses_count,
            *preference,
            dns_servers.len(),
            *other_receive_time,
        )
            .cmp(&(
                other_addresses.len(),
                *other_preferred_addresses_count,
                *other_preference,
                other_dns_server.len(),
                *receive_time,
            ))
    }
}

impl PartialOrd for AdvertiseMessage {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for AdvertiseMessage {
    fn eq(&self, other: &Self) -> bool {
        self.cmp(other) == std::cmp::Ordering::Equal
    }
}

impl Eq for AdvertiseMessage {}

fn compute_preferred_address_count(
    got_addresses: &HashMap<v6::IAID, IdentityAssociation>,
    configured_addresses: &HashMap<v6::IAID, Option<Ipv6Addr>>,
) -> usize {
    configured_addresses.iter().fold(0, |count, (iaid, address)| {
        count
            + address.map_or(0, |addr| {
                got_addresses.get(iaid).map_or(0, |got_ia| usize::from(got_ia.address == addr))
            })
    })
}

// Calculates the elapsed time since `start_time`, in centiseconds.
fn elapsed_time_in_centisecs(start_time: Instant) -> u16 {
    u16::try_from(
        Instant::now()
            .duration_since(start_time)
            .as_millis()
            .checked_div(ELAPSED_TIME_DENOMINATOR)
            .expect("division should succeed, denominator is non-zero"),
    )
    .unwrap_or(u16::MAX)
}

// Returns the common value in `values` if all the values are equal, or None
// otherwise.
fn get_common_value(values: &Vec<u32>) -> Option<Duration> {
    if !values.is_empty() && values.iter().all(|value| *value == values[0]) {
        return Some(Duration::from_secs(values[0].into()));
    }
    None
}

/// Provides methods for handling state transitions from server discovery
/// state.
#[derive(Debug)]
struct ServerDiscovery {
    /// [Client Identifier] used for uniquely identifying the client in
    /// communication with servers.
    ///
    /// [Client Identifier]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.2
    client_id: [u8; CLIENT_ID_LEN],
    /// The addresses the client is configured to negotiate, indexed by IAID.
    configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
    /// The time of the first solicit. `None` before a solicit is sent. Used in
    /// calculating the [elapsed time].
    ///
    /// [elapsed time]:https://datatracker.ietf.org/doc/html/rfc8415#section-21.9
    first_solicit_time: Option<Instant>,
    /// The solicit retransmission timeout.
    retrans_timeout: Duration,
    /// The [SOL_MAX_RT] used by the client.
    ///
    /// [SOL_MAX_RT]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.24
    solicit_max_rt: Duration,
    /// The advertise collected from servers during [server discovery], with
    /// the best advertise at the top of the heap.
    ///
    /// [server discovery]: https://datatracker.ietf.org/doc/html/rfc8415#section-18
    collected_advertise: BinaryHeap<AdvertiseMessage>,
    /// The valid SOL_MAX_RT options received from servers.
    collected_sol_max_rt: Vec<u32>,
}

impl ServerDiscovery {
    /// Starts server discovery by sending a solicit message, as described in
    /// [RFC 8415, Section 18.2.1].
    ///
    /// [RFC 8415, Section 18.2.1]: https://datatracker.ietf.org/doc/html/rfc8415#section-18.2.1
    fn start<R: Rng>(
        transaction_id: [u8; 3],
        client_id: [u8; CLIENT_ID_LEN],
        configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: &[v6::OptionCode],
        solicit_max_rt: Duration,
        rng: &mut R,
    ) -> Transition {
        Self {
            client_id,
            configured_addresses,
            first_solicit_time: None,
            retrans_timeout: Duration::default(),
            solicit_max_rt,
            collected_advertise: BinaryHeap::new(),
            collected_sol_max_rt: Vec::new(),
        }
        .send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }

    /// Calculates timeout for retransmitting solicits using parameters
    /// specified in [RFC 8415, Section 18.2.1].
    ///
    /// [RFC 8415, Section 18.2.1]: https://datatracker.ietf.org/doc/html/rfc8415#section-18.2.1
    fn retransmission_timeout<R: Rng>(
        prev_retrans_timeout: Duration,
        max_retrans_timeout: Duration,
        rng: &mut R,
    ) -> Duration {
        retransmission_timeout(
            prev_retrans_timeout,
            INITIAL_SOLICIT_TIMEOUT,
            max_retrans_timeout,
            rng,
        )
    }

    /// Returns a transition back to `ServerDiscovery`, with actions to send a
    /// solicit and schedule retransmission.
    fn send_and_schedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let Self {
            client_id,
            configured_addresses,
            first_solicit_time,
            retrans_timeout,
            solicit_max_rt,
            collected_advertise,
            collected_sol_max_rt,
        } = self;
        let mut options = vec![v6::DhcpOption::ClientId(&client_id)];

        let (start_time, elapsed_time) = match first_solicit_time {
            None => (Instant::now(), 0),
            Some(start_time) => (start_time, elapsed_time_in_centisecs(start_time)),
        };
        options.push(v6::DhcpOption::ElapsedTime(elapsed_time));

        // TODO(https://fxbug.dev/86945): remove `address_hint` construction
        // once `IanaSerializer::new()` takes options by value.
        let mut address_hint = HashMap::new();
        for (iaid, addr_opt) in &configured_addresses {
            let entry = address_hint.insert(
                *iaid,
                addr_opt.map(|addr| {
                    [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(addr, 0, 0, &[]))]
                }),
            );
            assert_matches!(entry, None);
        }

        // Adds IA_NA options: one IA_NA per address hint, plus IA_NA options
        // without hints, up to the configured `address_count`, as described in
        // RFC 8415, section 6.6.
        for (iaid, addr_hint) in &address_hint {
            options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
                *iaid,
                0,
                0,
                addr_hint.as_ref().map_or(&[], AsRef::as_ref),
            )));
        }

        let mut oro = vec![v6::OptionCode::SolMaxRt];
        oro.extend_from_slice(options_to_request);
        options.push(v6::DhcpOption::Oro(&oro));

        let builder = v6::MessageBuilder::new(v6::MessageType::Solicit, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);

        let retrans_timeout =
            ServerDiscovery::retransmission_timeout(retrans_timeout, solicit_max_rt, rng);

        Transition {
            state: ClientState::ServerDiscovery(ServerDiscovery {
                client_id,
                configured_addresses,
                first_solicit_time: Some(start_time),
                retrans_timeout,
                solicit_max_rt,
                collected_advertise,
                collected_sol_max_rt,
            }),
            actions: vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
            transaction_id: None,
        }
    }

    /// Selects a server, or retransmits solicit if no valid advertise were
    /// received.
    fn retransmission_timer_expired<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let Self {
            client_id,
            configured_addresses,
            first_solicit_time,
            retrans_timeout,
            solicit_max_rt,
            mut collected_advertise,
            collected_sol_max_rt,
        } = self;
        let solicit_max_rt = get_common_value(&collected_sol_max_rt).unwrap_or(solicit_max_rt);

        // Update SOL_MAX_RT, per RFC 8415, section 18.2.9:
        //
        //    A client SHOULD only update its SOL_MAX_RT [..] if all received
        //    Advertise messages that contained the corresponding option
        //    specified the same value.
        if let Some(advertise) = collected_advertise.pop() {
            return Requesting::start(
                client_id,
                configured_addresses,
                advertise,
                &options_to_request,
                collected_advertise,
                solicit_max_rt,
                rng,
            );
        }

        ServerDiscovery {
            client_id,
            configured_addresses,
            first_solicit_time,
            retrans_timeout,
            solicit_max_rt,
            collected_advertise,
            collected_sol_max_rt,
        }
        .send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }

    fn advertise_message_received<R: Rng, B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        msg: v6::Message<'_, B>,
    ) -> Transition {
        let Self {
            client_id,
            configured_addresses,
            first_solicit_time,
            retrans_timeout,
            solicit_max_rt,
            collected_advertise,
            collected_sol_max_rt,
        } = self;
        let mut client_id_option = None;
        let mut solicit_max_rt_option = None;
        let mut server_id = None;
        let mut preference = 0;
        let mut addresses: HashMap<v6::IAID, IdentityAssociation> = HashMap::new();
        let mut status_code = None;
        let mut dns_servers: Option<Vec<Ipv6Addr>> = None;

        // Process options; the client does not check whether an option is
        // present in the Advertise message multiple times because each option
        // is expected to appear only once, per RFC 8415, section 21:
        //
        //    Unless otherwise noted, each option may appear only in the options
        //    area of a DHCP message and may appear only once.
        //
        // If an option is present more than once, the client will use the value
        // of the last read option.
        //
        // Options that are not allowed in Advertise messages, as specified in
        // RFC 8415, appendix B table, are ignored.
        for opt in msg.options() {
            match opt {
                v6::ParsedDhcpOption::ClientId(client_id_opt) => {
                    client_id_option = Some(client_id_opt)
                }
                v6::ParsedDhcpOption::ServerId(server_id_opt) => {
                    server_id = Some(server_id_opt.to_vec())
                }
                v6::ParsedDhcpOption::SolMaxRt(sol_max_rt_opt) => {
                    solicit_max_rt_option = Some(sol_max_rt_opt.get())
                }
                v6::ParsedDhcpOption::Preference(preference_opt) => preference = preference_opt,
                v6::ParsedDhcpOption::Iana(iana_data) => {
                    // Ignore invalid IANA options, per RFC 8415, section 21.4:
                    //
                    //    If a client receives an IA_NA with T1 greater than T2
                    //    and both T1 and T2 are greater than 0, the client
                    //    discards the IA_NA option and processes the remainder
                    //    of the message as though the server had not included
                    //    the invalid IA_NA option.
                    match (iana_data.t1(), iana_data.t2()) {
                        (v6::TimeValue::Zero, _) | (_, v6::TimeValue::Zero) => {}
                        (t1, t2) => {
                            if t1 > t2 {
                                continue;
                            }
                        }
                    }

                    // Per RFC 8415, section 21.4, IAIDs are expected to be
                    // unique. Ignore IA_NA option with duplicate IAID.
                    //
                    //    A DHCP message may contain multiple IA_NA options
                    //    (though each must have a unique IAID).
                    let vacant_ia_entry = match addresses.entry(v6::IAID::new(iana_data.iaid())) {
                        Entry::Occupied(entry) => {
                            log::debug!(
                                "received unexpected IA_NA option with
                                non-unique IAID {:?}.",
                                entry.key()
                            );
                            continue;
                        }
                        Entry::Vacant(entry) => entry,
                    };

                    let mut iaaddr_opt = None;
                    let mut iana_status_code = None;
                    for iana_opt in iana_data.iter_options() {
                        match iana_opt {
                            v6::ParsedDhcpOption::IaAddr(iaaddr_data) => {
                                // Ignore invalid IA Address options, per RFC
                                // 8415, section 21.6:
                                //
                                //    The client MUST discard any addresses for
                                //    which the preferred lifetime is greater
                                //    than the valid lifetime.
                                if iaaddr_data.preferred_lifetime() > iaaddr_data.valid_lifetime() {
                                    continue;
                                }
                                iaaddr_opt = Some(iaaddr_data);
                            }
                            v6::ParsedDhcpOption::StatusCode(code, _) => {
                                iana_status_code = Some(code.get());
                            }
                            v6::ParsedDhcpOption::ClientId(_)
                            | v6::ParsedDhcpOption::ServerId(_)
                            | v6::ParsedDhcpOption::SolMaxRt(_)
                            | v6::ParsedDhcpOption::Preference(_)
                            | v6::ParsedDhcpOption::Iana(_)
                            | v6::ParsedDhcpOption::InformationRefreshTime(_)
                            | v6::ParsedDhcpOption::Oro(_)
                            | v6::ParsedDhcpOption::ElapsedTime(_)
                            | v6::ParsedDhcpOption::DnsServers(_)
                            | v6::ParsedDhcpOption::DomainList(_) => {
                                log::debug!(
                                    "received unexpected suboption with code
                                    {:?} in IANA options in Advertise message.",
                                    iana_opt.code()
                                );
                            }
                        }
                    }
                    if let Some(iaaddr_data) = iaaddr_opt {
                        if iana_status_code.is_none()
                            || iana_status_code == Some(v6::StatusCode::Success.into())
                        {
                            let _: &mut IdentityAssociation =
                                vacant_ia_entry.insert(IdentityAssociation {
                                    address: Ipv6Addr::from(iaaddr_data.addr()),
                                    preferred_lifetime: iaaddr_data.preferred_lifetime(),
                                    valid_lifetime: iaaddr_data.valid_lifetime(),
                                });
                        }
                    }
                }
                v6::ParsedDhcpOption::StatusCode(code, message) => {
                    status_code = Some(code.get());
                    if !message.is_empty() {
                        log::debug!("received status code {:?}: {}", status_code.as_ref(), message);
                    }
                }
                v6::ParsedDhcpOption::InformationRefreshTime(refresh_time) => {
                    log::debug!(
                        "received unexpected option Information Refresh Time
                        ({:?}) in Advertise message",
                        refresh_time
                    );
                }
                v6::ParsedDhcpOption::IaAddr(iaaddr_data) => {
                    log::debug!(
                        "received unexpected option IA Addr [addr: {:?}] as top
                        option in Advertise message",
                        iaaddr_data.addr()
                    );
                }
                v6::ParsedDhcpOption::Oro(option_codes) => {
                    log::debug!(
                        "received unexpected option ORO ({:?}) in Advertise
                        message",
                        option_codes
                    );
                }
                v6::ParsedDhcpOption::ElapsedTime(elapsed_time) => {
                    log::debug!(
                        "received unexpected option Elapsed Time ({:?}) in
                        Advertise message",
                        elapsed_time
                    );
                }
                v6::ParsedDhcpOption::DnsServers(server_addrs) => dns_servers = Some(server_addrs),
                v6::ParsedDhcpOption::DomainList(_domains) => {
                    // TODO(https://fxbug.dev/87176) implement domain list.
                }
            }
        }

        // Per RFC 8415, section 16.3:
        //
        //    Clients MUST discard any received Advertise message that meets
        //    any of the following conditions:
        //    -  the message does not include a Server Identifier option (see
        //       Section 21.3).
        //    -  the message does not include a Client Identifier option (see
        //       Section 21.2).
        //    -  the contents of the Client Identifier option do not match the
        //       client's DUID.
        let (server_id, client_id_option) =
            if let (Some(sid), Some(cid)) = (server_id, client_id_option) {
                (sid, cid)
            } else {
                return Transition {
                    state: ClientState::ServerDiscovery(ServerDiscovery {
                        client_id,
                        configured_addresses,
                        first_solicit_time,
                        retrans_timeout,
                        solicit_max_rt,
                        collected_advertise,
                        collected_sol_max_rt,
                    }),
                    actions: Vec::new(),
                    transaction_id: None,
                };
            };
        if &client_id != client_id_option {
            return Transition {
                state: ClientState::ServerDiscovery(ServerDiscovery {
                    client_id,
                    configured_addresses,
                    first_solicit_time,
                    retrans_timeout,
                    solicit_max_rt,
                    collected_advertise,
                    collected_sol_max_rt,
                }),
                actions: Vec::new(),
                transaction_id: None,
            };
        }

        // Process SOL_MAX_RT and discard invalid advertise following RFC 8415,
        // section 18.2.9:
        //
        //    The client MUST process any SOL_MAX_RT option [..] even if the
        //    message contains a Status Code option indicating a failure, and
        //    the Advertise message will be discarded by the client.
        //
        //    The client MUST ignore any Advertise message that contains no
        //    addresses [..], with the exception that the client MUST process
        //    an included SOL_MAX_RT option.
        //
        // Per RFC 8415, section 21.24:
        //
        //    SOL_MAX_RT value MUST be in this range: 60 <= "value" <= 86400
        //
        //    A DHCP client MUST ignore any SOL_MAX_RT option values that are
        //    less than 60 or more than 86400.
        //
        let mut collected_sol_max_rt = collected_sol_max_rt;
        if let Some(solicit_max_rt_option) = solicit_max_rt_option {
            if VALID_MAX_SOLICIT_TIMEOUT_RANGE.contains(&solicit_max_rt_option) {
                collected_sol_max_rt.push(solicit_max_rt_option);
            }
        }
        if addresses.is_empty()
            || status_code.is_some() && status_code != Some(v6::StatusCode::Success.into())
        {
            return Transition {
                state: ClientState::ServerDiscovery(ServerDiscovery {
                    client_id,
                    configured_addresses,
                    first_solicit_time,
                    retrans_timeout,
                    solicit_max_rt,
                    collected_advertise,
                    collected_sol_max_rt,
                }),
                actions: Vec::new(),
                transaction_id: None,
            };
        }

        let preferred_addresses_count =
            compute_preferred_address_count(&addresses, &configured_addresses);
        let advertise = AdvertiseMessage {
            server_id,
            addresses,
            dns_servers: dns_servers.unwrap_or(Vec::new()),
            preference,
            receive_time: Instant::now(),
            preferred_addresses_count,
        };

        let solicit_timeout = INITIAL_SOLICIT_TIMEOUT.as_secs_f64();
        let is_retransmitting = retrans_timeout.as_secs_f64()
            >= solicit_timeout + solicit_timeout * RANDOMIZATION_FACTOR_MAX;

        // Select server if its preference value is `255` and the advertise is
        // complete, as described in RFC 8415, section 18.2.1:
        //
        //    If the client receives a valid Advertise message that includes a
        //    Preference option with a preference value of 255, the client
        //    immediately begins a client-initiated message exchange (as
        //    described in Section 18.2.2) by sending a Request message to the
        //    server from which the Advertise message was received.
        //
        // Per RFC 8415, section 18.2.9:
        //
        //    Those Advertise messages with the highest server preference value
        //    SHOULD be preferred over all other Advertise messages.  The
        //    client MAY choose a less preferred server if that server has a
        //    better set of advertised parameters.
        //
        // During retrasmission, the client select the server that sends the
        // first valid advertise, regardless of preference value or advertise
        // completeness, as described in RFC 8415, section 18.2.1:
        //
        //    The client terminates the retransmission process as soon as it
        //    receives any valid Advertise message, and the client acts on the
        //    received Advertise message without waiting for any additional
        //    Advertise messages.
        if (advertise.preference == ADVERTISE_MAX_PREFERENCE
            && advertise.is_complete(&configured_addresses, options_to_request))
            || is_retransmitting
        {
            let solicit_max_rt = get_common_value(&collected_sol_max_rt).unwrap_or(solicit_max_rt);
            return Requesting::start(
                client_id,
                configured_addresses,
                advertise,
                &options_to_request,
                collected_advertise,
                solicit_max_rt,
                rng,
            );
        }

        let mut collected_advertise = collected_advertise;
        collected_advertise.push(advertise);
        Transition {
            state: ClientState::ServerDiscovery(ServerDiscovery {
                client_id,
                configured_addresses,
                first_solicit_time,
                retrans_timeout,
                solicit_max_rt,
                collected_advertise,
                collected_sol_max_rt,
            }),
            actions: Vec::new(),
            transaction_id: None,
        }
    }
}

// Returns the min value greater than zero, if the arguments are non zero.  If
// the new value is zero, the old value is returned unchanged; otherwise if the
// old value is zero, the new value is returned. Used for calculating the
// minimum T1/T2 as described in RFC 8415, section 18.2.4:
//
//    [..] the client SHOULD renew/rebind all IAs from the
//    server at the same time, the client MUST select T1 and
//    T2 times from all IA options that will guarantee that
//    the client initiates transmissions of Renew/Rebind
//    messages not later than at the T1/T2 times associated
//    with any of the client's bindings (earliest T1/T2).
fn maybe_get_nonzero_min(old_value: v6::TimeValue, new_value: v6::TimeValue) -> v6::TimeValue {
    match old_value {
        v6::TimeValue::Zero => new_value,
        v6::TimeValue::NonZero(old_t) => v6::TimeValue::NonZero(get_nonzero_min(old_t, new_value)),
    }
}

// Returns the min value greater than zero.
fn get_nonzero_min(
    old_value: v6::NonZeroTimeValue,
    new_value: v6::TimeValue,
) -> v6::NonZeroTimeValue {
    match new_value {
        v6::TimeValue::Zero => old_value,
        v6::TimeValue::NonZero(new_val) => std::cmp::min(old_value, new_val),
    }
}

mod private {
    use super::*;
    use std::net::Ipv6Addr;

    /// Holds an address different from what was configured.
    #[derive(Debug, PartialEq, Clone)]
    pub(super) struct NonConfiguredAddress {
        address: Option<Ipv6Addr>,
        configured_address: Option<Ipv6Addr>,
    }

    impl NonConfiguredAddress {
        /// Creates a `NonConfiguredAddress`. Returns `None` if the address is
        /// the same as what was configured.
        pub(super) fn new(
            address: Option<Ipv6Addr>,
            configured_address: Option<Ipv6Addr>,
        ) -> Option<Self> {
            if address == configured_address {
                return None;
            }
            Some(Self { address, configured_address })
        }

        /// Returns the address.
        pub(super) fn address(&self) -> Option<Ipv6Addr> {
            let Self { address, configured_address: _ } = self;
            *address
        }

        /// Returns the configured address.
        pub(super) fn configured_address(&self) -> Option<Ipv6Addr> {
            let Self { address: _, configured_address } = self;
            *configured_address
        }
    }

    /// Holds an IA for an address different from what was configured.
    #[derive(Debug, PartialEq, Clone)]
    pub(super) struct NonConfiguredIA {
        ia: IdentityAssociation,
        configured_address: Option<Ipv6Addr>,
    }

    impl NonConfiguredIA {
        /// Creates a `NonConfiguredIA`. Returns `None` if the address within
        /// the IA is the same as what was configured.
        pub(super) fn new(
            ia: IdentityAssociation,
            configured_address: Option<Ipv6Addr>,
        ) -> Option<Self> {
            let IdentityAssociation { address, preferred_lifetime: _, valid_lifetime: _ } = &ia;
            match configured_address {
                Some(c) => {
                    if *address == c {
                        return None;
                    }
                }
                None => (),
            }
            Some(Self { ia, configured_address })
        }

        /// Returns the address within the IA.
        pub(crate) fn address(&self) -> Ipv6Addr {
            let IdentityAssociation { address, preferred_lifetime: _, valid_lifetime: _ } = self.ia;
            address
        }
    }
}

use private::*;

/// Represents an address to request in an IA, relative to the configured
/// address for that IA.
#[derive(Debug, PartialEq, Clone)]
enum AddressToRequest {
    /// The address to request is the same as the configured address.
    Configured(Option<Ipv6Addr>),
    /// The address to request is different than the configured address.
    NonConfigured(NonConfiguredAddress),
}

impl AddressToRequest {
    /// Extracts the configured addresses from a map of addresses to request.
    fn to_configured_addresses(
        addresses_to_request: HashMap<v6::IAID, AddressToRequest>,
    ) -> HashMap<v6::IAID, Option<Ipv6Addr>> {
        addresses_to_request
            .iter()
            .map(|(iaid, addr_to_request)| (*iaid, addr_to_request.configured_address()))
            .collect()
    }

    /// Creates an `AddressToRequest`.
    fn new(address: Option<Ipv6Addr>, configured_address: Option<Ipv6Addr>) -> Self {
        let non_conf_addr_opt = NonConfiguredAddress::new(address, configured_address);
        match non_conf_addr_opt {
            None => AddressToRequest::Configured(configured_address),
            Some(non_conf_addr) => AddressToRequest::NonConfigured(non_conf_addr),
        }
    }

    /// Returns the address to request.
    fn address(&self) -> Option<Ipv6Addr> {
        match self {
            AddressToRequest::Configured(c) => *c,
            AddressToRequest::NonConfigured(non_conf_addr) => non_conf_addr.address(),
        }
    }

    /// Returns the configured address.
    fn configured_address(&self) -> Option<Ipv6Addr> {
        match self {
            AddressToRequest::Configured(c) => *c,
            AddressToRequest::NonConfigured(non_conf_addr) => non_conf_addr.configured_address(),
        }
    }
}

/// Provides methods for handling state transitions from requesting state.
#[derive(Debug)]
struct Requesting {
    /// [Client Identifier] used for uniquely identifying the client in
    /// communication with servers.
    ///
    /// [Client Identifier]:
    /// https://datatracker.ietf.org/doc/html/rfc8415#section-21.2
    client_id: [u8; CLIENT_ID_LEN],
    /// The addresses to request from the server.
    addresses_to_request: HashMap<v6::IAID, AddressToRequest>,
    /// The [server identifier] of the server to which the client sends
    /// requests.
    ///
    /// [Server Identifier]:
    /// https://datatracker.ietf.org/doc/html/rfc8415#section-21.3
    server_id: Vec<u8>,
    /// The advertise collected from servers during [server discovery].
    ///
    /// [server discovery]:
    /// https://datatracker.ietf.org/doc/html/rfc8415#section-18
    collected_advertise: BinaryHeap<AdvertiseMessage>,
    /// The time of the first request. `None` before a request is sent. Used in
    /// calculating the [elapsed time].
    ///
    /// [elapsed time]:
    /// https://datatracker.ietf.org/doc/html/rfc8415#section-21.9
    first_request_time: Option<Instant>,
    /// The request retransmission timeout.
    retrans_timeout: Duration,
    /// The request retransmission count.
    retrans_count: u8,
    /// The [SOL_MAX_RT] used by the client.
    ///
    /// [SOL_MAX_RT]:
    /// https://datatracker.ietf.org/doc/html/rfc8415#section-21.24
    solicit_max_rt: Duration,
}

// Helper function to send a request to an alternate server, or if there are no
// other collected servers, restart server discovery.
fn request_from_alternate_server_or_restart_server_discovery<R: Rng>(
    client_id: [u8; CLIENT_ID_LEN],
    configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
    options_to_request: &[v6::OptionCode],
    mut collected_advertise: BinaryHeap<AdvertiseMessage>,
    solicit_max_rt: Duration,
    rng: &mut R,
) -> Transition {
    if let Some(advertise) = collected_advertise.pop() {
        return Requesting::start(
            client_id,
            configured_addresses,
            advertise,
            options_to_request,
            collected_advertise,
            solicit_max_rt,
            rng,
        );
    }
    return ServerDiscovery::start(
        transaction_id(),
        client_id,
        configured_addresses,
        &options_to_request,
        solicit_max_rt,
        rng,
    );
}

fn compute_t(min: v6::NonZeroTimeValue, ratio: Ratio<u32>) -> v6::NonZeroTimeValue {
    match min {
        v6::NonZeroTimeValue::Finite(t) => {
            ratio.checked_mul(&Ratio::new_raw(t.get(), 1)).map_or(
                v6::NonZeroTimeValue::Infinity,
                |t| {
                    v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(t.to_integer()).expect(
                        "non-zero ratio of NonZeroOrMaxU32 value should be NonZeroOrMaxU32",
                    ))
                },
            )
        }
        v6::NonZeroTimeValue::Infinity => v6::NonZeroTimeValue::Infinity,
    }
}

impl Requesting {
    /// Starts in requesting state following [RFC 8415, Section 18.2.2].
    ///
    /// [RFC 8415, Section 18.2.2]: https://tools.ietf.org/html/rfc8415#section-18.2.2
    fn start<R: Rng>(
        client_id: [u8; CLIENT_ID_LEN],
        configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
        advertise: AdvertiseMessage,
        options_to_request: &[v6::OptionCode],
        collected_advertise: BinaryHeap<AdvertiseMessage>,
        solicit_max_rt: Duration,
        rng: &mut R,
    ) -> Transition {
        let AdvertiseMessage {
            server_id,
            addresses: advertised_addresses,
            dns_servers: _,
            preference: _,
            receive_time: _,
            preferred_addresses_count: _,
        } = advertise;
        // Create a map of addresses to be requested, combining the IA in the selected
        // Advertise with the configured IAs that were not received in the Advertise
        // message.
        let addresses_to_request = configured_addresses.iter().fold(
            HashMap::new(),
            |mut addrs, (iaid, configured_address)| {
                // Note that the advertised address for an IAID may be different
                // from what was solicited by the client.
                match advertised_addresses.get(iaid) {
                    Some(ia) => {
                        let IdentityAssociation {
                            address: advertised_address,
                            preferred_lifetime: _,
                            valid_lifetime: _,
                        } = ia;
                        assert_eq!(
                            addrs.insert(
                                *iaid,
                                AddressToRequest::new(
                                    Some(*advertised_address),
                                    *configured_address
                                )
                            ),
                            None
                        );
                    }
                    // The configured address was not advertised; the client
                    // will continue to request it in subsequent messages, per
                    // RFC 8415 section 18.2:
                    //
                    //    When possible, the client SHOULD use the best
                    //    configuration available and continue to request the
                    //    additional IAs in subsequent messages.
                    None => {
                        assert_eq!(
                            addrs.insert(*iaid, AddressToRequest::Configured(*configured_address)),
                            None
                        );
                    }
                }
                addrs
            },
        );
        Self {
            client_id,
            addresses_to_request,
            server_id,
            collected_advertise,
            first_request_time: None,
            retrans_timeout: Duration::default(),
            retrans_count: 0,
            solicit_max_rt,
        }
        .send_and_reschedule_retransmission(transaction_id(), options_to_request, rng)
    }

    /// Calculates timeout for retransmitting requests using parameters
    /// specified in [RFC 8415, Section 18.2.2].
    ///
    /// [RFC 8415, Section 18.2.2]: https://tools.ietf.org/html/rfc8415#section-18.2.2
    fn retransmission_timeout<R: Rng>(prev_retrans_timeout: Duration, rng: &mut R) -> Duration {
        retransmission_timeout(
            prev_retrans_timeout,
            INITIAL_REQUEST_TIMEOUT,
            MAX_REQUEST_TIMEOUT,
            rng,
        )
    }

    /// A helper function that returns a transition back to `Requesting`, with
    /// actions to cancel current retransmission timer, send a request and
    /// schedules retransmission.
    fn send_and_reschedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let Transition { state, actions: request_actions, transaction_id } =
            self.send_and_schedule_retransmission(transaction_id, options_to_request, rng);
        let actions = std::iter::once(Action::CancelTimer(ClientTimerType::Retransmission))
            .chain(request_actions.into_iter())
            .collect();
        Transition { state, actions, transaction_id }
    }

    /// A helper function that returns a transition back to `Requesting`, with
    /// actions to send a request and schedules retransmission.
    ///
    /// # Panics
    ///
    /// Panics if `options_to_request` contains SOLICIT_MAX_RT.
    fn send_and_schedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let Self {
            client_id,
            server_id,
            addresses_to_request,
            collected_advertise,
            first_request_time,
            retrans_timeout: prev_retrans_timeout,
            mut retrans_count,
            solicit_max_rt,
        } = self;
        let retrans_timeout = Self::retransmission_timeout(prev_retrans_timeout, rng);

        // Per RFC 8415, section 18.2.2:
        //
        //    The client MUST include the identifier of the destination server
        //    in a Server Identifier option (see Section 21.3).
        //
        //    The client MUST include a Client Identifier option (see Section
        //    21.2) to identify itself to the server.  The client adds any other
        //    appropriate options, including one or more IA options.
        let mut options =
            vec![v6::DhcpOption::ServerId(&server_id), v6::DhcpOption::ClientId(&client_id)];

        let mut iaaddr_options = HashMap::new();
        for (iaid, addr_to_request) in &addresses_to_request {
            assert_matches!(
                iaaddr_options.insert(
                    *iaid,
                    addr_to_request.address().map(|addr| {
                        [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(addr, 0, 0, &[]))]
                    }),
                ),
                None
            );
        }
        for (iaid, iaddr_opt) in &iaaddr_options {
            options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
                *iaid,
                0,
                0,
                iaddr_opt.as_ref().map_or(&[], AsRef::as_ref),
            )));
        }

        // Per RFC 8415, section 18.2.2:
        //
        //    The client MUST include an Elapsed Time option (see Section 21.9)
        //    to indicate how long the client has been trying to complete the
        //    current DHCP message exchange.
        let mut elapsed_time = 0;
        let first_request_time = Some(first_request_time.map_or(Instant::now(), |start_time| {
            elapsed_time = elapsed_time_in_centisecs(start_time);
            retrans_count += 1;
            start_time
        }));
        options.push(v6::DhcpOption::ElapsedTime(elapsed_time));

        // Per RFC 8415, section 18.2.2:
        //
        //    The client MUST include an Option Request option (ORO) (see
        //    Section 21.7) to request the SOL_MAX_RT option (see Section 21.24)
        //    and any other options the client is interested in receiving.
        assert!(!options_to_request.contains(&v6::OptionCode::SolMaxRt));
        let oro = std::iter::once(v6::OptionCode::SolMaxRt)
            .chain(options_to_request.iter().cloned())
            .collect::<Vec<_>>();
        options.push(v6::DhcpOption::Oro(&oro));

        let builder = v6::MessageBuilder::new(v6::MessageType::Request, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);

        Transition {
            state: ClientState::Requesting(Requesting {
                client_id,
                addresses_to_request,
                server_id,
                collected_advertise,
                first_request_time,
                retrans_timeout,
                retrans_count,
                solicit_max_rt,
            }),
            actions: vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
            transaction_id: Some(transaction_id),
        }
    }

    /// Retransmits request. Per RFC 8415, section 18.2.2:
    ///
    ///    The client transmits the message according to Section 15, using the
    ///    following parameters:
    ///
    ///       IRT     REQ_TIMEOUT
    ///       MRT     REQ_MAX_RT
    ///       MRC     REQ_MAX_RC
    ///       MRD     0
    ///
    /// Per RFC 8415, section 15:
    ///
    ///    MRC specifies an upper bound on the number of times a client may
    ///    retransmit a message.  Unless MRC is zero, the message exchange fails
    ///    once the client has transmitted the message MRC times.
    ///
    /// Per RFC 8415, section 18.2.2:
    ///
    ///    If the message exchange fails, the client takes an action based on
    ///    the client's local policy.  Examples of actions the client might take
    ///    include the following:
    ///    -  Select another server from a list of servers known to the client
    ///       -- for example, servers that responded with an Advertise message.
    ///    -  Initiate the server discovery process described in Section 18.
    ///    -  Terminate the configuration process and report failure.
    ///
    /// The client's policy on message exchange failure is to select another
    /// server; if there are no  more servers available, restart server
    /// discovery.
    /// TODO(https://fxbug.dev/88117): make the client policy configurable.
    fn retransmission_timer_expired<R: Rng>(
        self,
        request_transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let Self {
            client_id,
            addresses_to_request,
            server_id,
            mut collected_advertise,
            first_request_time,
            retrans_timeout,
            retrans_count,
            solicit_max_rt,
        } = self;
        if retrans_count != REQUEST_MAX_RC {
            return Self {
                client_id,
                addresses_to_request,
                server_id,
                collected_advertise,
                first_request_time,
                retrans_timeout,
                retrans_count,
                solicit_max_rt,
            }
            .send_and_schedule_retransmission(
                request_transaction_id,
                options_to_request,
                rng,
            );
        }
        if let Some(advertise) = collected_advertise.pop() {
            return Requesting::start(
                client_id,
                AddressToRequest::to_configured_addresses(addresses_to_request),
                advertise,
                &options_to_request,
                collected_advertise,
                solicit_max_rt,
                rng,
            );
        }
        return ServerDiscovery::start(
            transaction_id(),
            client_id,
            AddressToRequest::to_configured_addresses(addresses_to_request),
            &options_to_request,
            solicit_max_rt,
            rng,
        );
    }

    fn reply_message_received<R: Rng, B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        msg: v6::Message<'_, B>,
    ) -> Transition {
        let Self {
            client_id,
            addresses_to_request,
            server_id,
            collected_advertise,
            first_request_time,
            retrans_timeout,
            retrans_count,
            solicit_max_rt,
        } = self;
        let mut status_code = None;
        let mut client_id_option = None;
        let mut server_id_option = None;
        let mut solicit_max_rt_option = None;
        let mut t1 = v6::TimeValue::Zero;
        let mut t2 = v6::TimeValue::Zero;
        let mut min_preferred_lifetime = v6::TimeValue::Zero;
        // Ok to initialize with Infinity, `get_nonzero_min` will pick a
        // smaller value once we see an IA with a valid lifetime less than
        // Infinity.
        let mut min_valid_lifetime = v6::NonZeroTimeValue::Infinity;
        let mut addresses: HashMap<v6::IAID, AddressEntry> = HashMap::new();

        let mut dns_servers: Option<Vec<Ipv6Addr>> = None;

        // Process options; the client does not check whether an option is
        // present in the Reply message multiple times because each option is
        // expected to appear only once, per RFC 8415, section 21:
        //
        //    Unless otherwise noted, each option may appear only in the options
        //    area of a DHCP message and may appear only once.
        //
        // If an option is present more than once, the client will use the value
        // of the last read option.
        //
        // Options that are not allowed in Reply messages, as specified in RFC
        // 8415, appendix B table, are ignored. NOTE: the appendix B table holds
        // some options that are not expected in a reply while in the requesting
        // state; such options are ignore as well below.
        'top_level_options: for opt in msg.options() {
            match opt {
                v6::ParsedDhcpOption::StatusCode(status_code_opt, message) => {
                    status_code = Some(match v6::StatusCode::try_from(status_code_opt.get()) {
                        Ok(code) => code,
                        Err(code) => {
                            log::debug!("received unknown status code {:?}", code);
                            continue;
                        }
                    });
                    if !message.is_empty() {
                        // Status message is intended for logging only; log if
                        // not empty.
                        log::debug!("received status code {:?}: {}", status_code.as_ref(), message);
                    }
                }
                v6::ParsedDhcpOption::ClientId(client_id_opt) => {
                    client_id_option = Some(client_id_opt.to_vec())
                }
                v6::ParsedDhcpOption::ServerId(server_id_opt) => {
                    server_id_option = Some(server_id_opt.to_vec())
                }
                v6::ParsedDhcpOption::SolMaxRt(sol_max_rt_opt) => {
                    let sol_max_rt_opt = sol_max_rt_opt.get();
                    if VALID_MAX_SOLICIT_TIMEOUT_RANGE.contains(&sol_max_rt_opt) {
                        solicit_max_rt_option = Some(Duration::from_secs(sol_max_rt_opt.into()));
                    }
                }
                v6::ParsedDhcpOption::Iana(iana_data) => {
                    // Ignore invalid IANA options, per RFC 8415, section 21.4:
                    //
                    //    If a client receives an IA_NA with T1 greater than T2
                    //    and both T1 and T2 are greater than 0, the client
                    //    discards the IA_NA option and processes the remainder
                    //    of the message as though the server had not included
                    //    the invalid IA_NA option.
                    match (iana_data.t1(), iana_data.t2()) {
                        (v6::TimeValue::Zero, _) | (_, v6::TimeValue::Zero) => {}
                        (t1, t2) => {
                            if t1 > t2 {
                                continue;
                            }
                        }
                    }

                    let configured_address =
                        match addresses_to_request.get(&v6::IAID::new(iana_data.iaid())) {
                            Some(address_to_request) => address_to_request.configured_address(),
                            None => {
                                // The RFC does not explicitly call out what to
                                // do with IAs that were not requested by the
                                // client. Ignore unsolicited IAs to control how
                                // many addresses are assigned to the client.
                                log::debug!(
                                    "received unexpected IA_NA option
                                  {:?} not requested by the client.",
                                    iana_data
                                );
                                continue;
                            }
                        };

                    // Per RFC 8415, section 21.4, IAIDs are expected to be
                    // unique. Ignore IA_NA option with duplicate IAID.
                    //
                    //    A DHCP message may contain multiple IA_NA options
                    //    (though each must have a unique IAID).
                    let vacant_ia_entry = match addresses.entry(v6::IAID::new(iana_data.iaid())) {
                        Entry::Occupied(entry) => {
                            log::debug!(
                                "received unexpected IA_NA option with
                                non-unique IAID {:?}.",
                                entry.key()
                            );
                            continue;
                        }
                        Entry::Vacant(entry) => entry,
                    };
                    // If T1/T2 are set by the server to values greater than 0,
                    // compute the minimum T1 and T2 values, per RFC 8415,
                    // section 18.2.4:
                    //
                    //    [..] the client SHOULD renew/rebind all IAs from the
                    //    server at the same time, the client MUST select T1 and
                    //    T2 times from all IA options that will guarantee that
                    //    the client initiates transmissions of Renew/Rebind
                    //    messages not later than at the T1/T2 times associated
                    //    with any of the client's bindings (earliest T1/T2).
                    t1 = maybe_get_nonzero_min(t1, iana_data.t1());
                    t2 = maybe_get_nonzero_min(t2, iana_data.t2());

                    let mut iaaddr_opt = None;
                    let mut iana_status_code = None;
                    for iana_opt in iana_data.iter_options() {
                        match iana_opt {
                            v6::ParsedDhcpOption::IaAddr(iaaddr_data) => {
                                if iaaddr_data.preferred_lifetime() > iaaddr_data.valid_lifetime() {
                                    // Ignore invalid IA Address options, per
                                    // RFC 8415, section 21.6:
                                    //
                                    //    The client MUST discard any addresses
                                    //    for which the preferred lifetime is
                                    //    greater than the valid lifetime.
                                    continue;
                                }
                                match iaaddr_data.valid_lifetime() {
                                    // Per RFC 8415, section 18.2.10.1:
                                    //
                                    //    Discard any leases from the IA, as
                                    //    recorded by the client, that have a
                                    //    valid lifetime of 0 in the IA Address.
                                    v6::TimeValue::Zero => {
                                        log::debug!(
                                            "IA(address: {:?}) with valid
                                            lifetime 0 is ignored",
                                            iaaddr_data.addr()
                                        );
                                        continue;
                                    }
                                    v6::TimeValue::NonZero(_t) => {}
                                }
                                iaaddr_opt = Some(iaaddr_data);
                            }
                            v6::ParsedDhcpOption::StatusCode(code, message) => {
                                iana_status_code =
                                    Some(match v6::StatusCode::try_from(code.get()) {
                                        Ok(code) => code,
                                        Err(code) => {
                                            log::debug!(
                                                "received unknown IANA status code {:?}",
                                                code
                                            );
                                            // Ignore IANA options with unknown
                                            // status code.
                                            continue 'top_level_options;
                                        }
                                    });
                                if !message.is_empty() {
                                    log::debug!(
                                        "received status code {:?}: {}",
                                        iana_status_code.as_ref(),
                                        message
                                    );
                                }
                            }
                            v6::ParsedDhcpOption::ClientId(_)
                            | v6::ParsedDhcpOption::ServerId(_)
                            | v6::ParsedDhcpOption::SolMaxRt(_)
                            | v6::ParsedDhcpOption::Preference(_)
                            | v6::ParsedDhcpOption::Iana(_)
                            | v6::ParsedDhcpOption::InformationRefreshTime(_)
                            | v6::ParsedDhcpOption::Oro(_)
                            | v6::ParsedDhcpOption::ElapsedTime(_)
                            | v6::ParsedDhcpOption::DnsServers(_)
                            | v6::ParsedDhcpOption::DomainList(_) => {
                                log::debug!(
                                    "received unexpected option with code {:?}
                                    in IANA options in Reply.",
                                    iana_opt.code()
                                );
                            }
                        }
                    }

                    // Per RFC 8415, section 21.13:
                    //
                    //    If the Status Code option does not appear in a message
                    //    in which the option could appear, the status of the
                    //    message is assumed to be Success.
                    let iana_status_code = iana_status_code.unwrap_or(v6::StatusCode::Success);
                    match iana_status_code {
                        v6::StatusCode::Success => {
                            if let Some(iaaddr_data) = iaaddr_opt {
                                let _: &mut AddressEntry =
                                    vacant_ia_entry.insert(AddressEntry::new_assigned(
                                        IdentityAssociation {
                                            address: Ipv6Addr::from(iaaddr_data.addr()),
                                            preferred_lifetime: iaaddr_data.preferred_lifetime(),
                                            valid_lifetime: iaaddr_data.valid_lifetime(),
                                        },
                                        configured_address,
                                    ));
                                min_preferred_lifetime = maybe_get_nonzero_min(
                                    min_preferred_lifetime,
                                    iaaddr_data.preferred_lifetime(),
                                );
                                min_valid_lifetime = get_nonzero_min(
                                    min_valid_lifetime,
                                    iaaddr_data.valid_lifetime(),
                                );
                            }
                        }
                        v6::StatusCode::NotOnLink => {
                            // If the client receives IAs with NotOnLink status,
                            // try to obtain other addresses in follow-up messages.
                            let _: &mut AddressEntry = vacant_ia_entry
                                .insert(AddressEntry::new_to_request(None, configured_address));
                        }
                        v6::StatusCode::UnspecFail
                        | v6::StatusCode::NoAddrsAvail
                        | v6::StatusCode::NoBinding
                        | v6::StatusCode::UseMulticast
                        | v6::StatusCode::NoPrefixAvail => {
                            log::debug!(
                                "received unexpected status code {:?} in IANA
                                option",
                                iana_status_code
                            );
                        }
                    }
                }
                v6::ParsedDhcpOption::DnsServers(server_addrs) => dns_servers = Some(server_addrs),
                v6::ParsedDhcpOption::InformationRefreshTime(refresh_time) => {
                    log::debug!(
                        "received unexpected option Information Refresh
                                 Time ({:?}) in Reply to non-Information Request
                                 message",
                        refresh_time
                    );
                }
                v6::ParsedDhcpOption::IaAddr(iaaddr_data) => {
                    log::debug!(
                        "received unexpected option IA Addr [addr: {:?}] as top
                        option in Reply message",
                        iaaddr_data.addr()
                    );
                }
                v6::ParsedDhcpOption::Preference(preference_opt) => {
                    log::debug!(
                        "received unexpected option Preference
                                         ({:?}) in Reply message",
                        preference_opt
                    );
                }
                v6::ParsedDhcpOption::Oro(option_codes) => {
                    log::debug!(
                        "received unexpected option ORO ({:?})
                                         in Reply message",
                        option_codes
                    );
                }
                v6::ParsedDhcpOption::ElapsedTime(elapsed_time) => {
                    log::debug!(
                        "received unexpected option Elapsed Time ({:?}) in Reply
                        message",
                        elapsed_time
                    );
                }
                v6::ParsedDhcpOption::DomainList(_domains) => {
                    // TODO(https://fxbug.dev/87176) implement domain list.
                }
            }
        }

        // Perform message validation per RFC 8415, section 16.10:
        //    Clients MUST discard any received Reply message that meets any of
        //    the following conditions:
        //    -  the message does not include a Server Identifier option (see
        //    Section 21.3).
        //    [..]
        //    - the Reply message MUST include a Client Identifier option, and
        //    the contents of the Client Identifier option MUST match the DUID
        //    of the client.
        if server_id_option == None
            || client_id_option.map_or(true, |client_id_opt| client_id_opt != client_id)
        {
            return Transition {
                state: ClientState::Requesting(Self {
                    client_id,
                    addresses_to_request,
                    server_id,
                    collected_advertise,
                    first_request_time,
                    retrans_timeout,
                    retrans_count,
                    solicit_max_rt,
                }),
                actions: Vec::new(),
                transaction_id: None,
            };
        }

        // Always update SOL_MAX_RT, per RFC 8415, section 18.2.10:
        //
        //    The client MUST process any SOL_MAX_RT option (see Section 21.24)
        //    and INF_MAX_RT option (see Section
        //    21.25) present in a Reply message, even if the message contains a
        //    Status Code option indicating a failure.
        let solicit_max_rt = solicit_max_rt_option.unwrap_or(solicit_max_rt);

        // Per RFC 8415, section 21.13:
        //
        //    If the Status Code option does not appear in a message
        //    in which the option could appear, the status of the
        //    message is assumed to be Success.
        let status_code = status_code.unwrap_or(v6::StatusCode::Success);
        match status_code {
            v6::StatusCode::UnspecFail => {
                // Per RFC 8415, section 18.2.10:
                //
                //    If the client receives a Reply message with a status code of
                //    UnspecFail, the server is indicating that it was unable to process
                //    the client's message due to an unspecified failure condition.  If
                //    the client retransmits the original message to the same server to
                //    retry the desired operation, the client MUST limit the rate at
                //    which it retransmits the message and limit the duration of the
                //    time during which it retransmits the message (see Section 14.1).
                //
                // TODO(https://fxbug.dev/81086): implement rate limiting.
                return Requesting {
                    client_id,
                    addresses_to_request,
                    server_id,
                    collected_advertise,
                    first_request_time,
                    retrans_timeout,
                    retrans_count,
                    solicit_max_rt,
                }
                .send_and_reschedule_retransmission(
                    *msg.transaction_id(),
                    options_to_request,
                    rng,
                );
            }
            v6::StatusCode::NotOnLink => {
                // Per RFC 8415, section 18.2.10.1:
                //
                //    If the client receives a NotOnLink status from the server in
                //    response to a Solicit (with a Rapid Commit option; see Section
                //    21.14) or a Request, the client can either reissue the message
                //    without specifying any addresses or restart the DHCP server
                //    discovery process (see Section 18).
                //
                // The client reissues the message without specifying addresses, leaving
                // it up to the server to assign addresses appropriate for the client's
                // link.
                let addresses_to_request = addresses_to_request.iter().fold(
                    HashMap::new(),
                    |mut addrs, (iaid, address_to_request)| {
                            assert_eq!(
                                addrs.insert(
                                    *iaid,
                                    AddressToRequest::new(
                                        None,
                                        address_to_request.configured_address()
                                        )
                                    ),
                                    None
                                    );
                            addrs
                        }
                    );
                return Requesting {
                    client_id,
                    addresses_to_request,
                    server_id,
                    collected_advertise,
                    first_request_time,
                    retrans_timeout,
                    retrans_count,
                    solicit_max_rt,
                }
                .send_and_reschedule_retransmission(
                    *msg.transaction_id(),
                    options_to_request,
                    rng,
                );
            }
            // TODO(https://fxbug.dev/76764): implement unicast.
            // The client already uses multicast.
            v6::StatusCode::UseMulticast |
            // Not expected as top level status.
            v6::StatusCode::NoAddrsAvail
            | v6::StatusCode::NoPrefixAvail
            | v6::StatusCode::NoBinding => {
                log::debug!(
                    "received top level error status code {:?} in Reply to
                    Request",
                    status_code,
                );
                return request_from_alternate_server_or_restart_server_discovery(
                    client_id,
                    AddressToRequest::to_configured_addresses(addresses_to_request),
                    &options_to_request,
                    collected_advertise,
                    solicit_max_rt,
                    rng,
                );
            }
            // Per RFC 8415, section 18.2.10.1:
            //
            //    If the Reply message contains any IAs but the client finds no
            //    usable addresses and/or delegated prefixes in any of these IAs,
            //    the client may either try another server (perhaps restarting the
            //    DHCP server discovery process) or use the Information-request
            //    message to obtain other configuration information only.
            //
            // If there are no usable addresses and no other servers to select,
            // the client restarts server discover instead of requesting
            // configuration information only. This option is preferred when the
            // client operates in stateful mode, where the main goal for the
            // client is to negotiate addresses.
            v6::StatusCode::Success => if !addresses.iter().any(|(_iaid, entry)| {
                match entry {
                    AddressEntry::Configured(_conf_ia) => true,
                    AddressEntry::NonConfigured(_non_conf_ia_) => true,
                    AddressEntry::ToRequest(_addr) => false,
                }
            }) {
                return request_from_alternate_server_or_restart_server_discovery(
                    client_id,
                    AddressToRequest::to_configured_addresses(addresses_to_request),
                    &options_to_request,
                    collected_advertise,
                    solicit_max_rt,
                    rng,
                );
            },
        }

        // Add configured addresses that were requested by the client but were
        // not received in this Reply.
        for (iaid, address_to_request) in addresses_to_request {
            let _: &mut AddressEntry =
                addresses.entry(iaid).or_insert(AddressEntry::ToRequest(address_to_request));
        }

        // If not set or 0, choose a value for T1 and T2, per RFC 8415, section
        // 18.2.4:
        //
        //    If T1 or T2 had been set to 0 by the server (for an
        //    IA_NA or IA_PD) or there are no T1 or T2 times (for an
        //    IA_TA) in a previous Reply, the client may, at its
        //    discretion, send a Renew or Rebind message,
        //    respectively.  The client MUST follow the rules
        //    defined in Section 14.2.
        //
        // Per RFC 8415, section 14.2:
        //
        //    When T1 and/or T2 values are set to 0, the client MUST choose a
        //    time to avoid packet storms.  In particular, it MUST NOT transmit
        //    immediately.
        //
        // When left to the client's discretion, the client chooses T1/T1 values
        // following the recommentations in RFC 8415, section 21.4:
        //
        //    Recommended values for T1 and T2 are 0.5 and 0.8 times the
        //    shortest preferred lifetime of the addresses in the IA that the
        //    server is willing to extend, respectively.  If the "shortest"
        //    preferred lifetime is 0xffffffff ("infinity"), the recommended T1
        //    and T2 values are also 0xffffffff.
        //
        // The RFC does not specify how to compute T1 if the shortest preferred
        // lifetime is zero and T1 is zero. In this case, T1 is calculated as a
        // fraction of the shortest valid lifetime.
        let t1 = match t1 {
            v6::TimeValue::Zero => {
                let min = match min_preferred_lifetime {
                    v6::TimeValue::Zero => min_valid_lifetime,
                    v6::TimeValue::NonZero(t) => t,
                };
                compute_t(min, T1_MIN_LIFETIME_RATIO)
            }
            v6::TimeValue::NonZero(t) => t,
        };
        // T2 must be >= T1, compute its value based on T1.
        let t2 = match t2 {
            v6::TimeValue::Zero => compute_t(t1, T2_T1_RATIO),
            // TODO(https://fxbug.dev/76766): set rebind timer.
            v6::TimeValue::NonZero(t2_val) => {
                if t2_val < t1 {
                    compute_t(t1, T2_T1_RATIO)
                } else {
                    t2_val
                }
            }
        };
        let actions = std::iter::once(Action::CancelTimer(ClientTimerType::Retransmission))
            .chain(dns_servers.clone().map(Action::UpdateDnsServers))
            // Set timer to start renewing addresses, per RFC 8415, section
            // 18.2.4:
            //
            //    At time T1, the client initiates a Renew/Reply message
            //    exchange to extend the lifetimes on any leases in the IA.
            //
            // Addresses are not renewed if T1 is infinity, per RFC 8415,
            // section 7.7:
            //
            //    A client will never attempt to extend the lifetimes of any
            //    addresses in an IA with T1 set to 0xffffffff.
            .chain(std::iter::once(t1).filter_map(|t1| match t1 {
                v6::NonZeroTimeValue::Finite(t1_val) => Some(Action::ScheduleTimer(
                    ClientTimerType::Renew,
                    Duration::from_secs(t1_val.get().into()),
                )),
                v6::NonZeroTimeValue::Infinity => None,
            }))
            .collect::<Vec<_>>();

        // TODO(https://fxbug.dev/72701) Send AddressWatcher update with
        // assigned addresses.
        Transition {
            state: ClientState::AddressAssigned(AddressAssigned {
                client_id,
                addresses,
                server_id,
                t1,
                t2,
                dns_servers: dns_servers.unwrap_or(Vec::new()),
                solicit_max_rt,
            }),
            actions,
            transaction_id: None,
        }
    }
}

/// Represents an address entry negotiated by the client.
#[derive(Debug, PartialEq)]
enum AddressEntry {
    /// The address is assigned and it's the same address as the configured
    /// address.
    Configured(IdentityAssociation),
    /// The address is assigned and it's different than as the configured
    /// address.
    NonConfigured(NonConfiguredIA),
    /// The address is not assigned and it's to be requested in subsequent
    /// messages.
    ToRequest(AddressToRequest),
}

impl AddressEntry {
    /// Creates a new assigned address entry.
    fn new_assigned(ia: IdentityAssociation, configured_address: Option<Ipv6Addr>) -> AddressEntry {
        match NonConfiguredIA::new(ia.clone(), configured_address) {
            Some(non_conf_ia) => AddressEntry::NonConfigured(non_conf_ia),
            None => AddressEntry::Configured(ia),
        }
    }

    /// Creates a new address to request entry.
    fn new_to_request(
        address: Option<Ipv6Addr>,
        configured_address: Option<Ipv6Addr>,
    ) -> AddressEntry {
        AddressEntry::ToRequest(AddressToRequest::new(address, configured_address))
    }

    fn address(&self) -> Option<Ipv6Addr> {
        match self {
            AddressEntry::Configured(IdentityAssociation {
                address,
                preferred_lifetime: _,
                valid_lifetime: _,
            }) => Some(*address),
            AddressEntry::NonConfigured(non_conf_ia) => Some(non_conf_ia.address()),
            AddressEntry::ToRequest(address) => address.address(),
        }
    }
}

/// Provides methods for handling state transitions from address assigned
/// state.
#[derive(Debug, PartialEq)]
struct AddressAssigned {
    /// [Client Identifier] used for uniquely identifying the client in
    /// communication with servers.
    ///
    /// [Client Identifier]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.2
    client_id: [u8; CLIENT_ID_LEN],
    /// The addresses entries negotiated by the client.
    addresses: HashMap<v6::IAID, AddressEntry>,
    /// The [server identifier] of the server to which the client sends
    /// requests.
    ///
    /// [Server Identifier]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.3
    server_id: Vec<u8>,
    /// The time interval after which the client contacts the server that
    /// assigned addresses to the client, to extend the lifetimes of the
    /// assigned addresses.
    t1: v6::NonZeroTimeValue,
    /// The time interval after which the client contacts any server to extend
    /// the lifetimes of the assigned addresses.
    t2: v6::NonZeroTimeValue,
    /// Stores the DNS servers received from the reply.
    dns_servers: Vec<Ipv6Addr>,
    /// The [SOL_MAX_RT](https://datatracker.ietf.org/doc/html/rfc8415#section-21.24)
    /// used by the client.
    solicit_max_rt: Duration,
}

impl AddressAssigned {
    /// Handles renew timer, following [RFC 8415, Section 18.2.4].
    ///
    /// [RFC 8415, Section 18.2.4]: https://tools.ietf.org/html/rfc8415#section-18.2.4
    fn renew_timer_expired<R: Rng>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let Self { client_id, addresses, server_id, t1, t2, dns_servers, solicit_max_rt } = self;
        let t1 = match t1 {
            v6::NonZeroTimeValue::Finite(t1_val) => t1_val,
            v6::NonZeroTimeValue::Infinity => {
                // The renew timer is not set if T1 is Infinity.
                std::unreachable!("addresses are not renewed for T1 Infinity.");
            }
        };
        // Start renewing addresses, per RFC 8415, section 18.2.4:
        //
        //    At time T1, the client initiates a Renew/Reply message
        //    exchange to extend the lifetimes on any leases in the IA.
        Renewing::start(
            transaction_id(),
            client_id,
            addresses,
            server_id,
            options_to_request,
            t1,
            t2,
            dns_servers,
            solicit_max_rt,
            rng,
        )
    }
}

#[derive(Debug)]
struct Renewing {
    /// [Client Identifier](https://datatracker.ietf.org/doc/html/rfc8415#section-21.2)
    /// used for uniquely identifying the client in communication with servers.
    client_id: [u8; CLIENT_ID_LEN],
    /// The addresses the client is initially configured to solicit, used when
    /// server discovery is restarted.
    addresses: HashMap<v6::IAID, AddressEntry>,
    /// [Server Identifier](https://datatracker.ietf.org/doc/html/rfc8415#section-21.2)
    /// of the server selected during server discovery.
    server_id: Vec<u8>,
    /// The time interval after which the client contacts the server that
    /// assigned addresses to the client, to extend the lifetimes of the
    /// assigned addresses.
    t1: v6::NonZeroOrMaxU32,
    /// The time interval after which the client contacts any server to extend
    /// the lifetimes of the assigned addresses.
    t2: v6::NonZeroTimeValue,
    /// Stores the DNS servers received from the reply.
    dns_servers: Vec<Ipv6Addr>,
    /// [elapsed time](https://datatracker.ietf.org/doc/html/rfc8415#section-21.9).
    first_renew_time: Option<Instant>,
    /// The renew retransmission timeout.
    retrans_timeout: Duration,
    /// The [SOL_MAX_RT](https://datatracker.ietf.org/doc/html/rfc8415#section-21.24)
    /// used by the client.
    solicit_max_rt: Duration,
}

impl Renewing {
    /// Starts renewing, following [RFC 8415, Section 18.2.4].
    ///
    /// [RFC 8415, Section 18.2.4]: https://tools.ietf.org/html/rfc8415#section-18.2.4
    fn start<R: Rng>(
        transaction_id: [u8; 3],
        client_id: [u8; CLIENT_ID_LEN],
        addresses: HashMap<v6::IAID, AddressEntry>,
        server_id: Vec<u8>,
        options_to_request: &[v6::OptionCode],
        t1: v6::NonZeroOrMaxU32,
        t2: v6::NonZeroTimeValue,
        dns_servers: Vec<Ipv6Addr>,
        solicit_max_rt: Duration,
        rng: &mut R,
    ) -> Transition {
        Self {
            client_id,
            addresses,
            server_id,
            t1,
            t2,
            dns_servers,
            first_renew_time: None,
            retrans_timeout: Duration::default(),
            solicit_max_rt,
        }
        .send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }

    /// Calculates timeout for retransmitting Renew using parameters specified
    /// in [RFC 8415, Section 18.2.4].
    ///
    /// [RFC 8415, Section 18.2.4]: https://tools.ietf.org/html/rfc8415#section-18.2.4
    fn retransmission_timeout<R: Rng>(prev_retrans_timeout: Duration, rng: &mut R) -> Duration {
        retransmission_timeout(prev_retrans_timeout, INITIAL_RENEW_TIMEOUT, MAX_RENEW_TIMEOUT, rng)
    }

    /// Returns a transition back to Renewing, with actions to send a Renew and
    /// schedule retransmission.
    fn send_and_schedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let Self {
            client_id,
            addresses,
            server_id,
            t1,
            t2,
            dns_servers,
            first_renew_time,
            retrans_timeout: prev_retrans_timeout,
            solicit_max_rt,
        } = self;
        let (start_time, elapsed_time) = match first_renew_time {
            None => (Instant::now(), 0),
            Some(start_time) => (start_time, elapsed_time_in_centisecs(start_time)),
        };

        let oro = std::iter::once(v6::OptionCode::SolMaxRt)
            .chain(options_to_request.iter().cloned())
            .collect::<Vec<_>>();

        let mut options = vec![
            v6::DhcpOption::ServerId(&server_id),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ElapsedTime(elapsed_time),
            v6::DhcpOption::Oro(&oro),
        ];

        // TODO(https://fxbug.dev/86945): remove `iaaddr_options` construction
        // once `IanaSerializer::new()` takes options by value.
        let mut iaaddr_options = HashMap::new();
        // TODO(https://fxbug.dev/74324): all addresses in the map should be
        // valid; invalid addresses to be removed part of the
        // AddressStateProvider work.
        for (iaid, addr_entry) in &addresses {
            assert_matches!(
                iaaddr_options.insert(
                    *iaid,
                    addr_entry.address().map(|addr| {
                        [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(addr, 0, 0, &[]))]
                    }),
                ),
                None
            );
        }
        for (iaid, iaaddr_opt) in &iaaddr_options {
            options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
                *iaid,
                0,
                0,
                iaaddr_opt.as_ref().map_or(&[], AsRef::as_ref),
            )));
        }

        let builder = v6::MessageBuilder::new(v6::MessageType::Renew, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let retrans_timeout = Renewing::retransmission_timeout(prev_retrans_timeout, rng);

        Transition {
            state: ClientState::Renewing(Renewing {
                client_id,
                addresses,
                server_id,
                t1,
                t2,
                dns_servers,
                first_renew_time: Some(start_time),
                retrans_timeout,
                solicit_max_rt,
            }),
            actions: vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
            transaction_id: Some(transaction_id),
        }
    }

    /// Retransmits Renew.
    fn retransmission_timer_expired<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        self.send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }
}

/// All possible states of a DHCPv6 client.
///
/// States not found in this enum are not supported yet.
#[derive(Debug)]
enum ClientState {
    /// Creating and (re)transmitting an information request, and waiting for
    /// a reply.
    InformationRequesting(InformationRequesting),
    /// Client is waiting to refresh, after receiving a valid reply to a
    /// previous information request.
    InformationReceived(InformationReceived),
    /// Sending solicit messages, collecting advertise messages, and selecting
    /// a server from which to obtain addresses and other optional
    /// configuration information.
    ServerDiscovery(ServerDiscovery),
    /// Creating and (re)transmitting a request message, and waiting for a
    /// reply.
    Requesting(Requesting),
    /// Client is waiting to renew, after receiving a valid reply to a previous request.
    AddressAssigned(AddressAssigned),
    /// Creating and (re)transmitting a renew message, and awaiting reply.
    Renewing(Renewing),
}

/// State transition, containing the next state, and the actions the client
/// should take to transition to that state, and the new transaction ID if it
/// has been updated.
struct Transition {
    state: ClientState,
    actions: Actions,
    transaction_id: Option<[u8; 3]>,
}

impl ClientState {
    /// Handles a received advertise message.
    fn advertise_message_received<R: Rng, B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        msg: v6::Message<'_, B>,
    ) -> Transition {
        match self {
            ClientState::ServerDiscovery(s) => {
                s.advertise_message_received(options_to_request, rng, msg)
            }
            ClientState::InformationRequesting(_)
            | ClientState::InformationReceived(_)
            | ClientState::Requesting(_)
            | ClientState::AddressAssigned(_)
            | ClientState::Renewing(_) => {
                Transition { state: self, actions: vec![], transaction_id: None }
            }
        }
    }

    /// Handles a received reply message.
    fn reply_message_received<R: Rng, B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        msg: v6::Message<'_, B>,
    ) -> Transition {
        match self {
            ClientState::InformationRequesting(s) => s.reply_message_received(msg),
            ClientState::Requesting(s) => s.reply_message_received(options_to_request, rng, msg),
            ClientState::InformationReceived(_)
            | ClientState::ServerDiscovery(_)
            | ClientState::AddressAssigned(_)
            // TODO(https://fxbug.dev/76765): process Reply to Renew.
            | ClientState::Renewing(_) => {
                Transition { state: self, actions: vec![], transaction_id: None }
            }
        }
    }

    /// Handles retransmission timeout.
    fn retransmission_timer_expired<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        match self {
            ClientState::InformationRequesting(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng)
            }
            ClientState::ServerDiscovery(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng)
            }
            ClientState::Requesting(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng)
            }
            ClientState::Renewing(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng)
            }
            ClientState::InformationReceived(_) | ClientState::AddressAssigned(_) => {
                unreachable!("received unexpected retransmission timeout in state {:?}.", self);
            }
        }
    }

    /// Handles refresh timeout.
    fn refresh_timer_expired<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        match self {
            ClientState::InformationReceived(s) => {
                s.refresh_timer_expired(transaction_id, options_to_request, rng)
            }
            ClientState::InformationRequesting(_)
            | ClientState::ServerDiscovery(_)
            // TODO(https://fxbug.dev/88838): add test for Requesting.
            | ClientState::Requesting(_)
            | ClientState::AddressAssigned(_)
            | ClientState::Renewing(_) => {
                unreachable!("received unexpected refresh timeout in state {:?}.", self);
            }
        }
    }

    /// Handles renew timeout.
    fn renew_timer_expired<R: Rng>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        match self {
            ClientState::AddressAssigned(s) => s.renew_timer_expired(options_to_request, rng),
            ClientState::InformationRequesting(_)
            | ClientState::InformationReceived(_)
            | ClientState::ServerDiscovery(_)
            | ClientState::Requesting(_)
            | ClientState::Renewing(_) => {
                unreachable!("received unexpected renew timeout in state {:?}.", self);
            }
        }
    }

    /// Returns the DNS servers advertised by the server.
    fn get_dns_servers(&self) -> Vec<Ipv6Addr> {
        match self {
            ClientState::InformationReceived(InformationReceived { dns_servers }) => {
                dns_servers.clone()
            }
            ClientState::AddressAssigned(AddressAssigned {
                client_id: _,
                addresses: _,
                server_id: _,
                t1: _,
                t2: _,
                dns_servers,
                solicit_max_rt: _,
            }) => dns_servers.clone(),
            ClientState::InformationRequesting(InformationRequesting { retrans_timeout: _ })
            | ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise: _,
                collected_sol_max_rt: _,
            })
            | ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            })
            | ClientState::Renewing(Renewing {
                client_id: _,
                addresses: _,
                server_id: _,
                t1: _,
                t2: _,
                dns_servers: _,
                first_renew_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
            }) => Vec::new(),
        }
    }
}

/// The DHCPv6 core state machine.
///
/// This struct maintains the state machine for a DHCPv6 client, and expects an imperative shell to
/// drive it by taking necessary actions (e.g. send packets, schedule timers, etc.) and dispatch
/// events (e.g. packets received, timer expired, etc.). All the functions provided by this struct
/// are pure-functional. All state transition functions return a list of actions that the
/// imperative shell should take to complete the transition.
#[derive(Debug)]
pub struct ClientStateMachine<R: Rng> {
    /// [Transaction ID] the client is using to communicate with servers.
    ///
    /// [Transaction ID]: https://tools.ietf.org/html/rfc8415#section-16.1
    transaction_id: [u8; 3],
    /// Options to include in [Option Request Option].
    /// [Option Request Option]: https://tools.ietf.org/html/rfc8415#section-21.7
    options_to_request: Vec<v6::OptionCode>,
    /// Current state of the client, must not be `None`.
    ///
    /// Using an `Option` here allows the client to consume and replace the state during
    /// transitions.
    state: Option<ClientState>,
    /// Used by the client to generate random numbers.
    rng: R,
}

impl<R: Rng> ClientStateMachine<R> {
    /// Starts the client in Stateless mode, as defined in [RFC 8415, Section 6.1].
    /// The client exchanges messages with servers to obtain the configuration
    /// information specified in `options_to_request`.
    ///
    /// [RFC 8415, Section 6.1]: https://tools.ietf.org/html/rfc8415#section-6.1
    pub fn start_stateless(
        transaction_id: [u8; 3],
        options_to_request: Vec<v6::OptionCode>,
        mut rng: R,
    ) -> (Self, Actions) {
        let Transition { state, actions, transaction_id: new_transaction_id } =
            InformationRequesting::start(transaction_id, &options_to_request, &mut rng);
        (
            Self {
                state: Some(state),
                transaction_id: new_transaction_id.unwrap_or(transaction_id),
                options_to_request,
                rng,
            },
            actions,
        )
    }

    /// Starts the client in Statelful mode, as defined in [RFC 8415, Section 6.2].
    /// The client exchanges messages with servers to obtain addresses in
    /// `configured_addresses`, and the configuration information in
    /// `options_to_request`.
    ///
    /// [RFC 8415, Section 6.1]: https://tools.ietf.org/html/rfc8415#section-6.2
    pub fn start_stateful(
        transaction_id: [u8; 3],
        client_id: [u8; CLIENT_ID_LEN],
        configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: Vec<v6::OptionCode>,
        mut rng: R,
    ) -> (Self, Actions) {
        let Transition { state, actions, transaction_id: new_transaction_id } =
            ServerDiscovery::start(
                transaction_id,
                client_id,
                configured_addresses,
                &options_to_request,
                MAX_SOLICIT_TIMEOUT,
                &mut rng,
            );
        (
            Self {
                state: Some(state),
                transaction_id: new_transaction_id.unwrap_or(transaction_id),
                options_to_request,
                rng,
            },
            actions,
        )
    }

    pub fn get_dns_servers(&self) -> Vec<Ipv6Addr> {
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = self;
        state.as_ref().expect("state should not be empty").get_dns_servers()
    }

    /// Handles a timeout event, dispatches based on timeout type.
    ///
    /// # Panics
    ///
    /// `handle_timeout` panics if current state is None.
    pub fn handle_timeout(&mut self, timeout_type: ClientTimerType) -> Actions {
        let ClientStateMachine { transaction_id, options_to_request, state, rng } = self;
        let old_state = state.take().expect("state should not be empty");
        let Transition { state: new_state, actions, transaction_id: new_transaction_id } =
            match timeout_type {
                ClientTimerType::Retransmission => old_state.retransmission_timer_expired(
                    *transaction_id,
                    &options_to_request,
                    rng,
                ),
                ClientTimerType::Refresh => {
                    old_state.refresh_timer_expired(*transaction_id, &options_to_request, rng)
                }
                ClientTimerType::Renew => old_state.renew_timer_expired(&options_to_request, rng),
            };
        *state = Some(new_state);
        *transaction_id = new_transaction_id.unwrap_or(*transaction_id);
        actions
    }

    /// Handles a received DHCPv6 message.
    ///
    /// # Panics
    ///
    /// `handle_reply` panics if current state is None.
    pub fn handle_message_receive<B: ByteSlice>(&mut self, msg: v6::Message<'_, B>) -> Actions {
        let ClientStateMachine { transaction_id, options_to_request, state, rng } = self;
        if msg.transaction_id() != transaction_id {
            Vec::new() // Ignore messages for other clients.
        } else {
            match msg.msg_type() {
                v6::MessageType::Reply => {
                    let Transition {
                        state: new_state,
                        actions,
                        transaction_id: new_transaction_id,
                    } = state.take().expect("state should not be empty").reply_message_received(
                        &options_to_request,
                        rng,
                        msg,
                    );
                    *state = Some(new_state);
                    *transaction_id = new_transaction_id.unwrap_or(*transaction_id);
                    actions
                }
                v6::MessageType::Advertise => {
                    let Transition {
                        state: new_state,
                        actions,
                        transaction_id: new_transaction_id,
                    } = state
                        .take()
                        .expect("state should not be empty")
                        .advertise_message_received(&options_to_request, rng, msg);
                    *state = Some(new_state);
                    *transaction_id = new_transaction_id.unwrap_or(*transaction_id);
                    actions
                }
                v6::MessageType::Reconfigure => {
                    // TODO(jayzhuang): support Reconfigure messages when needed.
                    // https://tools.ietf.org/html/rfc8415#section-18.2.11
                    Vec::new()
                }
                v6::MessageType::Solicit
                | v6::MessageType::Request
                | v6::MessageType::Confirm
                | v6::MessageType::Renew
                | v6::MessageType::Rebind
                | v6::MessageType::Release
                | v6::MessageType::Decline
                | v6::MessageType::InformationRequest
                | v6::MessageType::RelayForw
                | v6::MessageType::RelayRepl => {
                    // Ignore unexpected message types.
                    Vec::new()
                }
            }
        }
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use super::*;
    use packet::ParsablePacket;

    pub(crate) fn to_configured_addresses(
        address_count: u32,
        preferred_addresses: Vec<Ipv6Addr>,
    ) -> HashMap<v6::IAID, Option<Ipv6Addr>> {
        let addresses = preferred_addresses
            .into_iter()
            .map(Some)
            .chain(std::iter::repeat(None))
            .take(usize::try_from(address_count).unwrap());

        let configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>> =
            (0..).map(v6::IAID::new).zip(addresses).collect();
        configured_addresses
    }

    /// Creates a stateful client and asserts that:
    ///    - the client is started in ServerDiscovery state
    ///    - the state contain the expected value
    ///    - the actions are correct
    ///    - the Solicit message is correct
    ///
    /// Returns the client in ServerDiscovery state.
    pub(crate) fn start_and_assert_server_discovery<R: Rng + std::fmt::Debug>(
        transaction_id: [u8; 3],
        client_id: [u8; CLIENT_ID_LEN],
        configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: Vec<v6::OptionCode>,
        rng: R,
    ) -> ClientStateMachine<R> {
        let (client, actions) = ClientStateMachine::start_stateful(
            transaction_id.clone(),
            client_id.clone(),
            configured_addresses.clone(),
            options_to_request.clone(),
            rng,
        );

        assert_matches!(
            &client,
            ClientStateMachine {
                transaction_id: got_transaction_id,
                options_to_request: got_options_to_request,
                state: Some(ClientState::ServerDiscovery(ServerDiscovery {
                    client_id: got_client_id,
                    configured_addresses: got_configured_addresses,
                    first_solicit_time: Some(_),
                    retrans_timeout: INITIAL_SOLICIT_TIMEOUT,
                    solicit_max_rt: MAX_SOLICIT_TIMEOUT,
                    collected_advertise,
                    collected_sol_max_rt,
                })),
                rng: _,
            } if *got_transaction_id == transaction_id &&
                 *got_options_to_request == options_to_request &&
                 *got_client_id == client_id &&
                 *got_configured_addresses == configured_addresses &&
                 collected_advertise.is_empty() &&
                 collected_sol_max_rt.is_empty()
        );

        // Start of server discovery should send a solicit and schedule a
        // retransmission timer.
        let mut buf = assert_matches!( &actions[..],
            [Action::SendMessage(buf), Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_SOLICIT_TIMEOUT)] => buf
        );

        assert_outgoing_stateful_message(
            &mut buf,
            v6::MessageType::Solicit,
            &client_id,
            None,
            &options_to_request,
            &configured_addresses,
        );

        client
    }

    impl IdentityAssociation {
        pub(crate) fn new_default(address: Ipv6Addr) -> IdentityAssociation {
            IdentityAssociation {
                address,
                preferred_lifetime: v6::TimeValue::Zero,
                valid_lifetime: v6::TimeValue::Zero,
            }
        }
    }

    impl AdvertiseMessage {
        pub(crate) fn new_default(
            server_id: Vec<u8>,
            addresses: &[Ipv6Addr],
            dns_servers: &[Ipv6Addr],
            configured_addresses: &HashMap<v6::IAID, Option<Ipv6Addr>>,
        ) -> AdvertiseMessage {
            let addresses = (0..)
                .map(v6::IAID::new)
                .zip(addresses.iter().fold(Vec::new(), |mut addrs, address| {
                    addrs.push(IdentityAssociation::new_default(*address));
                    addrs
                }))
                .collect();
            let preferred_addresses_count =
                compute_preferred_address_count(&addresses, &configured_addresses);
            AdvertiseMessage {
                server_id,
                addresses,
                dns_servers: dns_servers.to_vec(),
                preference: 0,
                receive_time: Instant::now(),
                preferred_addresses_count,
            }
        }
    }

    /// Parses `buf` and returns the DHCPv6 message type.
    ///
    /// # Panics
    ///
    /// `msg_type` panics if parsing fails.
    pub(crate) fn msg_type(mut buf: &[u8]) -> v6::MessageType {
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        msg.msg_type()
    }

    /// A helper identity association test type specifying T1/T2, for testing
    /// T1/T2 variations across IAs.
    #[derive(Copy, Clone)]
    pub(crate) struct TestIdentityAssociation {
        pub(crate) address: Ipv6Addr,
        pub(crate) preferred_lifetime: v6::TimeValue,
        pub(crate) valid_lifetime: v6::TimeValue,
        pub(crate) t1: v6::TimeValue,
        pub(crate) t2: v6::TimeValue,
    }

    impl TestIdentityAssociation {
        /// Creates a `TestIdentityAssociation` with finite values for
        /// lifetimes.
        pub(crate) fn new_nonzero_finite(
            address: Ipv6Addr,
            preferred_lifetime: v6::NonZeroOrMaxU32,
            valid_lifetime: v6::NonZeroOrMaxU32,
            t1: v6::NonZeroOrMaxU32,
            t2: v6::NonZeroOrMaxU32,
        ) -> TestIdentityAssociation {
            TestIdentityAssociation {
                address,
                preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    preferred_lifetime,
                )),
                valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    valid_lifetime,
                )),
                t1: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(t1)),
                t2: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(t2)),
            }
        }
    }

    /// Creates a stateful client and exchanges messages to assign the
    /// configured addresses.  Returns the client in AddressAssigned state and
    /// the actions returned on transitioning to the AddressAssigned state.
    /// Asserts the content of the client state.
    ///
    /// # Panics
    ///
    /// `assign_addresses_and_assert` panics if address assignment fails.
    pub(crate) fn assign_addresses_and_assert<R: Rng + std::fmt::Debug>(
        client_id: [u8; CLIENT_ID_LEN],
        addresses_to_assign: Vec<TestIdentityAssociation>,
        expected_dns_servers: &[Ipv6Addr],
        expected_t1: v6::NonZeroTimeValue,
        rng: R,
    ) -> (ClientStateMachine<R>, Actions) {
        let transaction_id = [0, 1, 2];
        let configured_addresses = to_configured_addresses(
            u32::try_from(addresses_to_assign.len()).unwrap(),
            addresses_to_assign
                .iter()
                .map(
                    |TestIdentityAssociation {
                         address,
                         preferred_lifetime: _,
                         valid_lifetime: _,
                         t1: _,
                         t2: _,
                     }| *address,
                )
                .collect(),
        );
        let options_to_request = if expected_dns_servers.is_empty() {
            Vec::new()
        } else {
            vec![v6::OptionCode::DnsServers]
        };
        let mut client = testutil::start_and_assert_server_discovery(
            transaction_id.clone(),
            client_id.clone(),
            configured_addresses.clone(),
            options_to_request,
            rng,
        );

        let server_id = [1, 2, 3];
        let mut options = vec![
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&server_id),
            v6::DhcpOption::Preference(ADVERTISE_MAX_PREFERENCE),
        ];
        if !expected_dns_servers.is_empty() {
            options.push(v6::DhcpOption::DnsServers(&expected_dns_servers));
        }
        let addresses_to_assign: HashMap<v6::IAID, TestIdentityAssociation> =
            (0..).map(v6::IAID::new).zip(addresses_to_assign).collect();
        let mut iaaddr_opts = HashMap::new();
        for (iaid, ia) in &addresses_to_assign {
            assert_matches!(
                iaaddr_opts.insert(
                    *iaid,
                    [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
                        ia.address,
                        testutil::get_value(ia.preferred_lifetime),
                        testutil::get_value(ia.valid_lifetime),
                        &[]
                    ))]
                ),
                None
            );
        }
        for (iaid, ia) in &addresses_to_assign {
            options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
                *iaid,
                testutil::get_value(ia.t1),
                testutil::get_value(ia.t2),
                iaaddr_opts.get(iaid).unwrap(),
            )));
        }
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        // The client should transition to Requesting and select the server that
        // sent the best advertise.
        assert_matches!(
            &client.handle_message_receive(msg)[..],
           [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
                // TODO(https://fxbug.dev/88838): reuse `send_request` test
                // asserts to check the content of the Request message.
           ] if testutil::msg_type(buf) == v6::MessageType::Request
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        assert_matches!(&state, Some(ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
        })) if *server_id == vec![1,2,3]);

        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let expected_addresses =
            addresses_to_assign.iter().fold(HashMap::new(), |mut addrs, (iaid, ia)| {
                let TestIdentityAssociation {
                    address,
                    preferred_lifetime,
                    valid_lifetime,
                    t1: _,
                    t2: _,
                } = ia;
                assert_eq!(
                    addrs.insert(
                        *iaid,
                        AddressEntry::new_assigned(
                            IdentityAssociation {
                                address: *address,
                                preferred_lifetime: *preferred_lifetime,
                                valid_lifetime: *valid_lifetime
                            },
                            Some(*address)
                        )
                    ),
                    None
                );
                addrs
            });
        assert_matches!(
            &state,
            Some(ClientState::AddressAssigned(AddressAssigned {
                client_id: got_client_id,
                addresses,
                server_id: got_server_id,
                t1,
                // TODO(https://fxbug.dev/76766) check T2 when Rebind is
                // implemented.
                t2: _,
                dns_servers,
                solicit_max_rt,
            })) if *got_client_id == client_id &&
                   *addresses == expected_addresses &&
                   *got_server_id == server_id &&
                   *t1 == expected_t1 &&
                   dns_servers == expected_dns_servers &&
                   *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );
        (client, actions)
    }

    /// Gets the `u32` value inside a `v6::TimeValue`.
    fn get_value(t: v6::TimeValue) -> u32 {
        const INFINITY: u32 = u32::MAX;
        match t {
            v6::TimeValue::Zero => 0,
            v6::TimeValue::NonZero(non_zero_tv) => match non_zero_tv {
                v6::NonZeroTimeValue::Finite(t) => t.get(),
                v6::NonZeroTimeValue::Infinity => INFINITY,
            },
        }
    }

    /// Checks that the buffer contains the expected type and options for an
    /// outgoing message in stateful mode.
    ///
    /// # Panics
    ///
    /// `assert_outgoing_stateful_message` panics if the message cannot be
    /// parsed, or does not contain the expected options.
    pub(crate) fn assert_outgoing_stateful_message(
        mut buf: &[u8],
        expected_msg_type: v6::MessageType,
        expected_client_id: &[u8; CLIENT_ID_LEN],
        expected_server_id: Option<&Vec<u8>>,
        expected_oro: &[v6::OptionCode],
        expected_addresses: &HashMap<v6::IAID, Option<Ipv6Addr>>,
    ) {
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_eq!(msg.msg_type(), expected_msg_type);

        let (mut non_ia_opts, ia_opts, other) = msg.options().fold(
            (Vec::new(), Vec::new(), Vec::new()),
            |(mut non_ia_opts, mut ia_opts, mut other), opt| {
                match opt {
                    v6::ParsedDhcpOption::ClientId(_)
                    | v6::ParsedDhcpOption::ElapsedTime(_)
                    | v6::ParsedDhcpOption::Oro(_) => non_ia_opts.push(opt),
                    v6::ParsedDhcpOption::ServerId(_) if expected_server_id.is_some() => {
                        non_ia_opts.push(opt)
                    }
                    v6::ParsedDhcpOption::Iana(iana_data) => ia_opts.push(iana_data),
                    opt => other.push(opt),
                }
                (non_ia_opts, ia_opts, other)
            },
        );
        let option_sorter: fn(
            &v6::ParsedDhcpOption<'_>,
            &v6::ParsedDhcpOption<'_>,
        ) -> std::cmp::Ordering =
            |opt1, opt2| (u16::from(opt1.code())).cmp(&(u16::from(opt2.code())));

        // Check that the non-IA options are correct.
        non_ia_opts.sort_by(option_sorter);
        let expected_non_ia_opts = {
            let oro = std::iter::once(v6::OptionCode::SolMaxRt)
                .chain(expected_oro.iter().copied())
                .collect();
            let mut expected_non_ia_opts = vec![
                v6::ParsedDhcpOption::ClientId(expected_client_id),
                v6::ParsedDhcpOption::ElapsedTime(0),
                v6::ParsedDhcpOption::Oro(oro),
            ];
            if let Some(server_id) = expected_server_id {
                expected_non_ia_opts.push(v6::ParsedDhcpOption::ServerId(server_id));
            }
            expected_non_ia_opts.sort_by(option_sorter);
            expected_non_ia_opts
        };
        assert_eq!(non_ia_opts, expected_non_ia_opts);

        // Check that the IA options are correct.
        let sent_addresses = {
            let mut sent_addresses: HashMap<v6::IAID, Option<Ipv6Addr>> = HashMap::new();
            for iana_data in ia_opts.iter() {
                if iana_data.iter_options().count() == 0 {
                    assert_eq!(sent_addresses.insert(v6::IAID::new(iana_data.iaid()), None), None);
                    continue;
                }
                for iana_option in iana_data.iter_options() {
                    match iana_option {
                        v6::ParsedDhcpOption::IaAddr(iaaddr_data) => {
                            assert_eq!(
                                sent_addresses.insert(
                                    v6::IAID::new(iana_data.iaid()),
                                    Some(iaaddr_data.addr())
                                ),
                                None
                            );
                        }
                        option => panic!("unexpected option {:?}", option),
                    }
                }
            }
            sent_addresses
        };
        assert_eq!(&sent_addresses, expected_addresses);

        // Check that there are no other options besides the expected non-IA and
        // IA options.
        assert_eq!(&other, &[]);
    }

    /// Creates a stateful client, exchanges messages to assign the configured
    /// addresses, and sends a Renew message. Asserts the content of the client
    /// state and of the renew message, and returns the client in Renewing
    /// state.
    ///
    /// # Panics
    ///
    /// `send_renew_and_assert` panics if address assignment fails, or if
    /// sending a renew fails.
    pub(crate) fn send_renew_and_assert<R: Rng + std::fmt::Debug>(
        client_id: [u8; CLIENT_ID_LEN],
        addresses_to_assign: Vec<TestIdentityAssociation>,
        expected_t1: v6::NonZeroTimeValue,
        rng: R,
    ) -> ClientStateMachine<R> {
        let (mut client, actions) = testutil::assign_addresses_and_assert(
            client_id.clone(),
            addresses_to_assign.clone(),
            &[],
            expected_t1,
            rng,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        let old_transaction_id = *transaction_id;
        let (expected_server_id, expected_t1, expected_t2) = assert_matches!(state,
            Some(ClientState::AddressAssigned(AddressAssigned {
                client_id: _,
                addresses: _,
                server_id,
                t1: v6::NonZeroTimeValue::Finite(t1_val),
                t2,
                dns_servers: _,
                solicit_max_rt: _,
            })) => (server_id.clone(), *t1_val, *t2));
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1)
            ] if *t1 == Duration::from_secs(expected_t1.get().into())
        );

        // Renew timeout should trigger a transition to Renewing, send a renew
        // message and schedule retransmission.
        let actions = client.handle_timeout(ClientTimerType::Renew);
        let mut buf = match &actions[..] {
            [Action::SendMessage(buf), Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_RENEW_TIMEOUT)] => {
                buf
            }
            actions => panic!("unexpected actions {:?}", actions),
        };
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        // Assert that sending a renew starts a new transaction.
        assert_ne!(*transaction_id, old_transaction_id);
        assert_matches!(
            state,
            Some(ClientState::Renewing(Renewing {
                client_id: got_client_id,
                addresses: _,
                server_id,
                t1,
                t2,
                dns_servers,
                first_renew_time: _,
                retrans_timeout: _,
                solicit_max_rt,
            })) if *got_client_id == client_id &&
                   *server_id == expected_server_id &&
                   *t1 == expected_t1 &&
                   *t2 == expected_t2 &&
                   *dns_servers == Vec::<Ipv6Addr>::new() &&
                   *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );
        let expected_addresses_to_renew: HashMap<v6::IAID, Option<Ipv6Addr>> = (0..)
            .map(v6::IAID::new)
            .zip(addresses_to_assign.iter().map(
                |TestIdentityAssociation {
                     address,
                     preferred_lifetime: _,
                     valid_lifetime: _,
                     t1: _,
                     t2: _,
                 }| Some(*address),
            ))
            .collect();
        testutil::assert_outgoing_stateful_message(
            &mut buf,
            v6::MessageType::Renew,
            &client_id,
            Some(&expected_server_id),
            &[],
            &expected_addresses_to_renew,
        );
        client
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use net_declare::std_ip_v6;
    use packet::ParsablePacket;
    use rand::rngs::mock::StepRng;
    use test_case::test_case;
    use testutil::TestIdentityAssociation;

    const INFINITY: u32 = u32::MAX;

    #[test]
    fn send_information_request_and_receive_reply() {
        // Try to start information request with different list of requested options.
        for options in IntoIterator::into_iter([
            Vec::new(),
            vec![v6::OptionCode::DnsServers],
            vec![v6::OptionCode::DnsServers, v6::OptionCode::DomainList],
        ]) {
            let (mut client, actions) = ClientStateMachine::start_stateless(
                [0, 1, 2],
                options.clone(),
                StepRng::new(std::u64::MAX / 2, 0),
            );

            let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
                &client;
            assert_matches!(
                *state,
                Some(ClientState::InformationRequesting(InformationRequesting {
                    retrans_timeout: INITIAL_INFO_REQ_TIMEOUT,
                }))
            );

            // Start of information requesting should send an information request and schedule a
            // retransmission timer.
            let want_options_array = [v6::DhcpOption::Oro(&options)];
            let want_options = if options.is_empty() { &[][..] } else { &want_options_array[..] };
            let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
                &client;
            let builder = v6::MessageBuilder::new(
                v6::MessageType::InformationRequest,
                *transaction_id,
                want_options,
            );
            let mut want_buf = vec![0; builder.bytes_len()];
            builder.serialize(&mut want_buf);
            assert_eq!(
                actions[..],
                [
                    Action::SendMessage(want_buf),
                    Action::ScheduleTimer(
                        ClientTimerType::Retransmission,
                        INITIAL_INFO_REQ_TIMEOUT
                    )
                ]
            );

            let dns_servers = [std_ip_v6!("ff01::0102"), std_ip_v6!("ff01::0304")];

            let test_dhcp_refresh_time = 42u32;
            let options = [
                v6::DhcpOption::InformationRefreshTime(test_dhcp_refresh_time),
                v6::DhcpOption::DnsServers(&dns_servers),
            ];
            let builder =
                v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
            let mut buf = vec![0; builder.bytes_len()];
            builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

            let actions = client.handle_message_receive(msg);
            let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
                client;

            {
                assert_matches!(
                    state,
                    Some(ClientState::InformationReceived(InformationReceived {dns_servers: d
                    })) if d == dns_servers.to_vec()
                );
            }
            // Upon receiving a valid reply, client should set up for refresh based on the reply.
            assert_eq!(
                actions[..],
                [
                    Action::CancelTimer(ClientTimerType::Retransmission),
                    Action::ScheduleTimer(
                        ClientTimerType::Refresh,
                        Duration::from_secs(u64::from(test_dhcp_refresh_time)),
                    ),
                    Action::UpdateDnsServers(dns_servers.to_vec())
                ]
            );
        }
    }

    #[test]
    fn send_information_request_on_retransmission_timeout() {
        let (mut client, actions) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );
        assert_matches!(
            actions[..],
            [_, Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_INFO_REQ_TIMEOUT)]
        );

        let actions = client.handle_timeout(ClientTimerType::Retransmission);
        // Following exponential backoff defined in https://tools.ietf.org/html/rfc8415#section-15.
        assert_matches!(
            actions[..],
            [_, Action::ScheduleTimer(ClientTimerType::Retransmission, timeout)] if timeout == 2 * INITIAL_INFO_REQ_TIMEOUT
        );
    }

    #[test]
    fn send_information_request_on_refresh_timeout() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &[]);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // Transition to InformationReceived state.
        assert_eq!(
            client.handle_message_receive(msg)[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Refresh, IRT_DEFAULT)
            ]
        );

        // Refresh should start another round of information request.
        let actions = client.handle_timeout(ClientTimerType::Refresh);
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let builder =
            v6::MessageBuilder::new(v6::MessageType::InformationRequest, *transaction_id, &[]);
        let mut want_buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut want_buf);
        assert_eq!(
            actions[..],
            [
                Action::SendMessage(want_buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_INFO_REQ_TIMEOUT)
            ]
        );
    }

    // Test starting the client in stateful mode with different address
    // configurations.
    #[test_case(1, Vec::new(), Vec::new())]
    #[test_case(2, vec![std_ip_v6!("::ffff:c00a:1ff")], vec![v6::OptionCode::DnsServers])]
    #[test_case(
       2,
       vec![std_ip_v6!("::ffff:c00a:2ff"), std_ip_v6!("::ffff:c00a:3ff")],
       vec![v6::OptionCode::DnsServers])]
    fn send_solicit(
        address_count: u32,
        preferred_addresses: Vec<Ipv6Addr>,
        options_to_request: Vec<v6::OptionCode>,
    ) {
        // The client is checked inside `start_and_assert_server_discovery`.
        let _client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            v6::duid_uuid(),
            testutil::to_configured_addresses(address_count, preferred_addresses),
            options_to_request,
            StepRng::new(std::u64::MAX / 2, 0),
        );
    }

    #[test]
    fn compute_preferred_address_count() {
        // No preferred addresses configured.
        let got_addresses: HashMap<v6::IAID, IdentityAssociation> = (0..)
            .map(v6::IAID::new)
            .zip(vec![IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:1ff"))].into_iter())
            .collect();
        let configured_addresses = testutil::to_configured_addresses(1, vec![]);
        assert_eq!(
            super::compute_preferred_address_count(&got_addresses, &configured_addresses),
            0
        );
        assert_eq!(
            super::compute_preferred_address_count(&HashMap::new(), &configured_addresses),
            0
        );

        // All obtained addresses are preferred addresses.
        let got_addresses: HashMap<v6::IAID, IdentityAssociation> = (0..)
            .map(v6::IAID::new)
            .zip(
                vec![
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:1ff")),
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:2ff")),
                ]
                .into_iter(),
            )
            .collect();
        let configured_addresses = testutil::to_configured_addresses(
            2,
            vec![std_ip_v6!("::ffff:c00a:1ff"), std_ip_v6!("::ffff:c00a:2ff")],
        );
        assert_eq!(
            super::compute_preferred_address_count(&got_addresses, &configured_addresses),
            2
        );

        // Only one of the obtained addresses is a preferred address.
        let got_addresses: HashMap<v6::IAID, IdentityAssociation> = (0..)
            .map(v6::IAID::new)
            .zip(
                vec![
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:1ff")),
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:3ff")),
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:5ff")),
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:6ff")),
                ]
                .into_iter(),
            )
            .collect();
        let configured_addresses = testutil::to_configured_addresses(
            3,
            vec![
                std_ip_v6!("::ffff:c00a:2ff"),
                std_ip_v6!("::ffff:c00a:3ff"),
                std_ip_v6!("::ffff:c00a:4ff"),
            ],
        );
        assert_eq!(
            super::compute_preferred_address_count(&got_addresses, &configured_addresses),
            1
        );
    }

    #[test]
    fn advertise_message_is_complete() {
        let preferred_address = std_ip_v6!("::ffff:c00a:1ff");
        let configured_addresses = testutil::to_configured_addresses(2, vec![preferred_address]);

        let advertise = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[preferred_address, std_ip_v6!("::ffff:c00a:2ff")],
            &[],
            &configured_addresses,
        );
        assert!(advertise.is_complete(&configured_addresses, &[]));

        // Advertise is not complete: does not contain the solicited address
        // count.
        let advertise = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[preferred_address],
            &[],
            &configured_addresses,
        );
        assert!(!advertise.is_complete(&configured_addresses, &[]));

        // Advertise is not complete: does not contain the solicited preferred
        // address.
        let advertise = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[std_ip_v6!("::ffff:c00a:3ff"), std_ip_v6!("::ffff:c00a:4ff")],
            &[],
            &configured_addresses,
        );
        assert!(!advertise.is_complete(&configured_addresses, &[]));

        // Advertise is complete: contains both the requested addresses and
        // the requested options.
        let options_to_request = [v6::OptionCode::DnsServers];
        let advertise = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[preferred_address, std_ip_v6!("::ffff:c00a:2ff")],
            &[std_ip_v6!("::fe80:1:2")],
            &configured_addresses,
        );
        assert!(advertise.is_complete(&configured_addresses, &options_to_request));
    }

    #[test]
    fn advertise_message_ord() {
        let preferred_address = std_ip_v6!("::ffff:c00a:1ff");
        let configured_addresses = testutil::to_configured_addresses(3, vec![preferred_address]);

        // `advertise2` is complete, `advertise1` is not.
        let advertise1 = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[preferred_address, std_ip_v6!("::ffff:c00a:2ff")],
            &[],
            &configured_addresses,
        );
        let advertise2 = AdvertiseMessage::new_default(
            vec![4, 5, 6],
            &[preferred_address, std_ip_v6!("::ffff:c00a:3ff"), std_ip_v6!("::ffff:c00a:4ff")],
            &[],
            &configured_addresses,
        );
        assert!(advertise1 < advertise2);

        // Neither advertise is complete, but `advertise2` has more addresses,
        // hence `advertise2` is preferred even though it does not contain the
        // configured preferred address.
        let advertise1 = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[preferred_address],
            &[],
            &configured_addresses,
        );
        let advertise2 = AdvertiseMessage::new_default(
            vec![4, 5, 6],
            &[std_ip_v6!("::ffff:c00a:5ff"), std_ip_v6!("::ffff:c00a:6ff")],
            &[],
            &configured_addresses,
        );
        assert!(advertise1 < advertise2);

        // Both advertise are complete, but `advertise1` was received first.
        let advertise1 = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[preferred_address, std_ip_v6!("::ffff:c00a:7ff"), std_ip_v6!("::ffff:c00a:8ff")],
            &[],
            &configured_addresses,
        );
        let advertise2 = AdvertiseMessage::new_default(
            vec![4, 5, 6],
            &[preferred_address, std_ip_v6!("::ffff:c00a:9ff"), std_ip_v6!("::ffff:c00a:aff")],
            &[],
            &configured_addresses,
        );
        assert!(advertise1 > advertise2);
    }

    #[test]
    fn receive_complete_advertise_with_max_preference() {
        let client_id = v6::duid_uuid();
        let mut client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            client_id.clone(),
            testutil::to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            std_ip_v6!("::ffff:c00a:1ff"),
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::Preference(42),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &iana_options)),
        ];
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // The client should stay in ServerDiscovery when receiving a complete
        // advertise with preference less than 255.
        assert!(client.handle_message_receive(msg).is_empty());
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            std_ip_v6!("::ffff:c00a:1ff"),
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&[4, 5, 6]),
            v6::DhcpOption::Preference(255),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &iana_options)),
        ];
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // The client should transition to Requesting when receiving a complete
        // advertise with preference 255.
        let actions = client.handle_message_receive(msg);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = client;
        assert_matches!(
            state,
            Some(ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            }))
        );
        let buf = match &actions[..] {
            [Action::CancelTimer(ClientTimerType::Retransmission), Action::SendMessage(buf), Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)] => {
                buf
            }
            actions => panic!("unexpected actions {:?}", actions),
        };
        assert_eq!(testutil::msg_type(buf), v6::MessageType::Request);
    }

    // T1 and T2 are non-zero and T1 > T2, the client should ignore this IA_NA option.
    #[test_case(60, 30, true)]
    #[test_case(INFINITY, 30, true)]
    // T1 > T2, but T2 is zero, the client should process this IA_NA option.
    #[test_case(60, 0, false)]
    // T1 is zero, the client should process this IA_NA option.
    #[test_case(0, 30, false)]
    // T1 <= T2, the client should process this IA_NA option.
    #[test_case(60, 90, false)]
    #[test_case(60, INFINITY, false)]
    #[test_case(INFINITY, INFINITY, false)]
    fn receive_advertise_with_invalid_iana(t1: u32, t2: u32, ignore_iana: bool) {
        let client_id = v6::duid_uuid();
        let transaction_id = [0, 1, 2];
        let mut client = testutil::start_and_assert_server_discovery(
            transaction_id,
            client_id.clone(),
            testutil::to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let preferred_lifetime = 10;
        let valid_lifetime = 20;
        let ia = IdentityAssociation {
            address: std_ip_v6!("::ffff:c00a:1ff"),
            preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                v6::NonZeroOrMaxU32::new(preferred_lifetime)
                    .expect("should succeed for non-zero or u32::MAX values"),
            )),
            valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                v6::NonZeroOrMaxU32::new(valid_lifetime)
                    .expect("should succeed for non-zero or u32::MAX values"),
            )),
        };
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            ia.address,
            preferred_lifetime,
            valid_lifetime,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), t1, t2, &iana_options)),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        assert_matches!(client.handle_message_receive(msg)[..], []);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let collected_advertise = match state {
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise,
                collected_sol_max_rt: _,
            })) => collected_advertise,
            state => panic!("unexpected state {:?}", state),
        };
        match ignore_iana {
            true => assert!(
                collected_advertise.is_empty(),
                "collected_advertise = {:?}",
                collected_advertise
            ),
            false => {
                let addresses = match collected_advertise.peek() {
                    Some(AdvertiseMessage {
                        server_id: _,
                        addresses,
                        dns_servers: _,
                        preference: _,
                        receive_time: _,
                        preferred_addresses_count: _,
                    }) => addresses,
                    advertise => panic!("unexpected advertise {:?}", advertise),
                };
                assert_eq!(*addresses, HashMap::from([(v6::IAID::new(0), ia)]));
            }
        }
    }

    #[test]
    fn select_first_server_while_retransmitting() {
        let client_id = v6::duid_uuid();
        let mut client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            client_id.clone(),
            testutil::to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // On transmission timeout, if no advertise were received the client
        // should stay in server discovery and resend solicit.
        let actions = client.handle_timeout(ClientTimerType::Retransmission);
        let (buf, timeout) = match &actions[..] {
            [Action::SendMessage(buf), Action::ScheduleTimer(ClientTimerType::Retransmission, timeout)] => {
                (buf, timeout)
            }
            actions => panic!("unexpected actions {:?}", actions),
        };
        assert_eq!(testutil::msg_type(buf), v6::MessageType::Solicit);
        assert_eq!(*timeout, 2 * INITIAL_SOLICIT_TIMEOUT);
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        assert_matches!(
            state,
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise,
                collected_sol_max_rt: _,
            })) if collected_advertise.is_empty()
        );

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            std_ip_v6!("::ffff:c00a:5ff"),
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &iana_options)),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // The client should transition to Requesting when receiving any
        // advertise while retransmitting.
        let actions = client.handle_message_receive(msg);
        let buf = match &actions[..] {
            [Action::CancelTimer(ClientTimerType::Retransmission), Action::SendMessage(buf), Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)] => {
                buf
            }
            actions => panic!("unexpected actions {:?}", actions),
        };
        assert_eq!(testutil::msg_type(buf), v6::MessageType::Request);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = client;
        assert_matches!(
            state,
            Some(ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            }))
        );
    }

    #[test]
    fn send_request() {
        let client_id = v6::duid_uuid();
        let configured_addresses = testutil::to_configured_addresses(3, vec![]);
        let advertised_addresses = [
            std_ip_v6!("::ffff:c00a:1ff"),
            std_ip_v6!("::ffff:c00a:2ff"),
            std_ip_v6!("::ffff:c00a:3ff"),
        ];
        let server_id = v6::duid_uuid();
        let selected_advertise = AdvertiseMessage::new_default(
            server_id.to_vec(),
            &advertised_addresses,
            &[],
            &configured_addresses,
        );
        let options_to_request = vec![];
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);
        let Transition { state, actions, transaction_id } = Requesting::start(
            client_id.clone(),
            configured_addresses,
            selected_advertise,
            &options_to_request[..],
            BinaryHeap::new(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
        );
        assert_matches!(transaction_id, Some(_));

        // Start of requesting should send a request and schedule retransmission.
        assert_matches!(
            state,
            ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            })
        );
        let mut buf = match &actions[..] {
            [Action::CancelTimer(ClientTimerType::Retransmission), Action::SendMessage(buf), Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)] => {
                buf
            }
            actions => panic!("unexpected actions {:?}", actions),
        };
        let expected_addresses = (0..)
            .map(v6::IAID::new)
            .zip(advertised_addresses.map(Some))
            .collect::<HashMap<v6::IAID, Option<Ipv6Addr>>>();
        testutil::assert_outgoing_stateful_message(
            &mut buf,
            v6::MessageType::Request,
            &client_id,
            Some(&server_id.to_vec()),
            &options_to_request,
            &expected_addresses,
        );
    }

    #[test]
    fn requesting_receive_reply_with_failure_status_code() {
        let options_to_request = vec![];
        let configured_addresses = testutil::to_configured_addresses(1, vec![]);
        let advertised_addresses = [std_ip_v6!("::ffff:c00a:1ff")];
        let selected_advertise = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &advertised_addresses,
            &[],
            &configured_addresses,
        );
        let mut collected_advertise = BinaryHeap::new();
        collected_advertise.push(AdvertiseMessage::new_default(
            vec![4, 5, 6],
            &[std_ip_v6!("::ffff:c00a:2ff")],
            &[],
            &configured_addresses,
        ));
        collected_advertise.push(AdvertiseMessage::new_default(
            vec![7, 8, 9],
            &[std_ip_v6!("::ffff:c00a:3ff")],
            &[],
            &configured_addresses,
        ));
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let client_id = v6::duid_uuid();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            client_id.clone(),
            configured_addresses.clone(),
            selected_advertise,
            &options_to_request[..],
            collected_advertise,
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
        );

        let (server_id, got_addresses_to_request) = match &state {
            ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: got_addresses_to_request,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            }) => (server_id, got_addresses_to_request),
            state => panic!("unexpected state {:?}", state),
        };
        assert_eq!(*server_id, vec![1, 2, 3]);
        let expected_addresses_to_request = (0..)
            .map(v6::IAID::new)
            .zip(advertised_addresses.iter().map(|addr| AddressToRequest::new(Some(*addr), None)))
            .collect::<HashMap<v6::IAID, AddressToRequest>>();
        assert_eq!(*got_addresses_to_request, expected_addresses_to_request);

        // If the reply contains an top level UnspecFail status code, the
        // request should be resent.
        let options = [
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &[])),
            v6::DhcpOption::StatusCode(v6::StatusCode::UnspecFail.into(), ""),
        ];
        let request_transaction_id = transaction_id.unwrap();
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        let (server_id, got_addresses_to_request) = match &state {
            ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: got_addresses_to_request,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            }) => (server_id, got_addresses_to_request),
            state => panic!("unexpected state {:?}", state),
        };
        assert_eq!(*server_id, vec![1, 2, 3]);
        assert_eq!(*got_addresses_to_request, expected_addresses_to_request);
        assert!(transaction_id.is_some());

        // If the reply contains an top level NotOnLink status code, the
        // request should be resent without specifying any addresses.
        let options = [
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &[])),
            v6::DhcpOption::StatusCode(v6::StatusCode::NotOnLink.into(), ""),
        ];
        let request_transaction_id = transaction_id.unwrap();
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg);

        let expected_addresses_to_request: HashMap<v6::IAID, AddressToRequest> =
            HashMap::from([(v6::IAID::new(0), AddressToRequest::new(None, None))]);
        let (server_id, got_addresses_to_request) = match &state {
            ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: got_addresses_to_request,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            }) => (server_id, got_addresses_to_request),
            state => panic!("unexpected state {:?}", state),
        };
        assert_eq!(*server_id, vec![1, 2, 3]);
        assert_eq!(expected_addresses_to_request, *got_addresses_to_request);
        assert!(transaction_id.is_some());

        // If the reply contains a top level status code indicating failure
        // (other than UnspecFail), the client selects another server and sends
        // a request to it.
        let options = [
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &[])),
            v6::DhcpOption::StatusCode(v6::StatusCode::NoAddrsAvail.into(), ""),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        assert_matches!(&state, ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            }) if *server_id == vec![4, 5, 6]);
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(_buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
            ]
        );
        assert!(transaction_id.is_some());

        // If the reply contains no usable addresses, the client selects
        // another server and sends a request to it.
        let iana_options = [v6::DhcpOption::StatusCode(v6::StatusCode::NoAddrsAvail.into(), "")];
        let options = [
            v6::DhcpOption::ServerId(&[4, 5, 6]),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &iana_options)),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id.unwrap(), &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        assert_matches!(state, ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            }) if server_id == vec![7, 8, 9]);
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(_buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
            ]
        );
        assert!(transaction_id.is_some());
    }

    #[test]
    fn requesting_receive_reply_with_ia_not_on_link() {
        let options_to_request = vec![];
        let address1 = std_ip_v6!("::ffff:c00a:1ff");
        let address2 = std_ip_v6!("::ffff:c00a:2ff");
        let configured_addresses = testutil::to_configured_addresses(2, vec![address1]);
        let selected_advertise = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[address1, address2],
            &[],
            &configured_addresses,
        );
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let client_id = v6::duid_uuid();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            client_id.clone(),
            configured_addresses.clone(),
            selected_advertise,
            &options_to_request[..],
            BinaryHeap::new(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
        );

        // If the reply contains an address with status code NotOnLink, the
        // client should request the IAs without specifying any addresses in
        // subsequent messages.
        let iana_options1 = [v6::DhcpOption::StatusCode(v6::StatusCode::NotOnLink.into(), "")];
        let preferred_lifetime_value = 60;
        let valid_lifetime_value = 90;
        let iana_options2 = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            address2,
            preferred_lifetime_value,
            valid_lifetime_value,
            &[],
        ))];
        let t1 = 90;
        let t2 = 120;
        let iaid1 = v6::IAID::new(0);
        let iaid2 = v6::IAID::new(1);
        let options = [
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(iaid1, t1, t2, &iana_options1)),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(iaid2, t1, t2, &iana_options2)),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id.unwrap(), &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        let expected_addresses = HashMap::from([
            (iaid1, AddressEntry::new_to_request(None, Some(address1))),
            (
                iaid2,
                AddressEntry::new_assigned(
                    IdentityAssociation {
                        address: address2,
                        preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                            v6::NonZeroOrMaxU32::new(preferred_lifetime_value)
                                .expect("should succeed for non-zero or u32::MAX values"),
                        )),
                        valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                            v6::NonZeroOrMaxU32::new(valid_lifetime_value)
                                .expect("should succeed for non-zero or u32::MAX values"),
                        )),
                    },
                    None,
                ),
            ),
        ]);
        assert_matches!(state,
            ClientState::AddressAssigned(AddressAssigned {
                client_id: _,
                addresses,
                server_id,
                t1: _,
                t2: _,
                dns_servers: _,
                solicit_max_rt: _,
            }) if server_id == vec![1, 2, 3] &&
                  addresses == expected_addresses);
        let expected_t1 = Duration::from_secs(t1.into());
        assert_matches!(&actions[..],
            [Action::CancelTimer(ClientTimerType::Retransmission), Action::ScheduleTimer(ClientTimerType::Renew, t1)] if *t1 == expected_t1
        );
        assert!(transaction_id.is_none());
    }

    #[test_case(0, 60, true)]
    #[test_case(60, 0, false)]
    #[test_case(0, 0, false)]
    #[test_case(30, 60, true)]
    fn requesting_receive_reply_with_invalid_ia_lifetimes(
        preferred_lifetime: u32,
        valid_lifetime: u32,
        valid_ia: bool,
    ) {
        let options_to_request = vec![];
        let configured_addresses = testutil::to_configured_addresses(1, vec![]);
        let address = std_ip_v6!("::ffff:c00a:5ff");
        let server_id = vec![1, 2, 3];
        let selected_advertise = AdvertiseMessage::new_default(
            server_id.clone(),
            &[address],
            &[],
            &configured_addresses,
        );
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let client_id = v6::duid_uuid();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            client_id.clone(),
            configured_addresses.clone(),
            selected_advertise,
            &options_to_request[..],
            BinaryHeap::new(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
        );

        // The client should discard the IAs with invalid lifetimes.
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            address,
            preferred_lifetime,
            valid_lifetime,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ServerId(&server_id),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &iana_options)),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id.unwrap(), &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id: _ } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        match valid_ia {
            true =>
            // The client should transition to AddressAssigned if the reply contains
            // a valid IA.
            {
                assert_matches!(
                    state,
                    ClientState::AddressAssigned(AddressAssigned {
                        client_id: _,
                        addresses: _,
                        server_id: _,
                        t1: _,
                        t2: _,
                        dns_servers: _,
                        solicit_max_rt: _,
                    })
                )
            }
            false =>
            // The client should transition to ServerDiscovery if the reply contains
            // no valid IAs.
            {
                assert_matches!(
                    state,
                    ClientState::ServerDiscovery(ServerDiscovery {
                        client_id: _,
                        configured_addresses: _,
                        first_solicit_time: _,
                        retrans_timeout: _,
                        solicit_max_rt: _,
                        collected_advertise: _,
                        collected_sol_max_rt: _,
                    })
                )
            }
        }
    }

    // Test that T1/T2 are calculated correctly on receiving a Reply to Request.
    #[test]
    fn compute_t1_t2_on_reply_to_request() {
        let configured_addresses =
            testutil::to_configured_addresses(2, vec![std_ip_v6!("::ffff:c00a:1ff")]);
        let selected_advertise = AdvertiseMessage::new_default(
            vec![1, 2, 3],
            &[std_ip_v6!("::ffff:c00a:1ff"), std_ip_v6!("::ffff:c00a:2ff")],
            &[],
            &configured_addresses,
        );
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        for (
            (ia1_preferred_lifetime, ia1_valid_lifetime, ia1_t1, ia1_t2),
            (ia2_preferred_lifetime, ia2_valid_lifetime, ia2_t1, ia2_t2),
            expected_t1,
            expected_t2,
        ) in vec![
            // If T1/T2 are 0, they should be computed as as 0.5 * minimum
            // preferred lifetime, and 0.8 * minimum preferred lifetime
            // respectively.
            (
                (100, 160, 0, 0),
                (120, 180, 0, 0),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(50).expect("shoud succeed")),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(80).expect("shoud succeed")),
            ),
            (
                (INFINITY, INFINITY, 0, 0),
                (120, 180, 0, 0),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(60).expect("shoud succeed")),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(96).expect("shoud succeed")),
            ),
            // If T1/T2 are 0, and the minimum preferred lifetime, is infinity,
            // T1/T2 should also be infinity.
            (
                (INFINITY, INFINITY, 0, 0),
                (INFINITY, INFINITY, 0, 0),
                v6::NonZeroTimeValue::Infinity,
                v6::NonZeroTimeValue::Infinity,
            ),
            // If T1/T2 are set, and have different values across IAs, T1/T2
            // should be computed as the minimum T1/T2. NOTE: the server should
            // send the same T1/T2 across all IA, but the client should be
            // prepared for the server sending different T1/T2 values.
            (
                (100, 160, 40, 70),
                (120, 180, 50, 80),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(40).expect("shoud succeed")),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(70).expect("shoud succeed")),
            ),
        ] {
            let client_id = v6::duid_uuid();
            let Transition { state, actions: _, transaction_id } = Requesting::start(
                client_id.clone(),
                configured_addresses.clone(),
                selected_advertise.clone(),
                &[],
                BinaryHeap::new(),
                MAX_SOLICIT_TIMEOUT,
                &mut rng,
            );

            let iana_options1 = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
                std_ip_v6!("::ffff:c00a:1ff"),
                ia1_preferred_lifetime,
                ia1_valid_lifetime,
                &[],
            ))];
            let iana_options2 = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
                std_ip_v6!("::ffff:c00a:2ff"),
                ia2_preferred_lifetime,
                ia2_valid_lifetime,
                &[],
            ))];
            let options = [
                v6::DhcpOption::ServerId(&[1, 2, 3]),
                v6::DhcpOption::ClientId(&client_id),
                v6::DhcpOption::Iana(v6::IanaSerializer::new(
                    v6::IAID::new(0),
                    ia1_t1,
                    ia1_t2,
                    &iana_options1,
                )),
                v6::DhcpOption::Iana(v6::IanaSerializer::new(
                    v6::IAID::new(1),
                    ia2_t1,
                    ia2_t2,
                    &iana_options2,
                )),
            ];
            let builder =
                v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id.unwrap(), &options);
            let mut buf = vec![0; builder.bytes_len()];
            builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
            let Transition { state, actions: _, transaction_id: _ } =
                state.reply_message_received(&[], &mut rng, msg);
            let (t1, t2) = match state {
                ClientState::AddressAssigned(AddressAssigned {
                    client_id: _,
                    addresses: _,
                    server_id: _,
                    t1,
                    t2,
                    dns_servers: _,
                    solicit_max_rt: _,
                }) => (t1, t2),
                state => panic!("unexpected state {:?}", state),
            };
            assert_eq!(t1, expected_t1.into());
            assert_eq!(t2, expected_t2.into());
        }
    }

    // Test that Request retransmission respects max retransmission count.
    #[test]
    fn requesting_retransmit_max_retrans_count() {
        let client_id = v6::duid_uuid();
        let transaction_id = [0, 1, 2];
        let mut client = testutil::start_and_assert_server_discovery(
            transaction_id,
            client_id.clone(),
            testutil::to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            std_ip_v6!("::ffff:c00a:1ff"),
            60,
            60,
            &[],
        ))];
        let server_id_1 = [1, 2, 3];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&server_id_1),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &iana_options)),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(client.handle_message_receive(msg)[..], []);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        assert_matches!(
            state,
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise: _,
                collected_sol_max_rt: _,
            }))
        );

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            std_ip_v6!("::ffff:c00a:2ff"),
            60,
            60,
            &[],
        ))];
        let server_id_2 = [4, 5, 6];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&server_id_2),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 60, 60, &iana_options)),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(client.handle_message_receive(msg)[..], []);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        assert_matches!(
            state,
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise: _,
                collected_sol_max_rt: _,
            }))
        );

        // The client should transition to Requesting and select the server that
        // sent the best advertise.
        assert_matches!(
            &client.handle_timeout(ClientTimerType::Retransmission)[..],
           [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
           ] if testutil::msg_type(buf) == v6::MessageType::Request
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        assert_matches!(state, Some(ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
        })) if *server_id == server_id_1);

        for count in 1..REQUEST_MAX_RC + 1 {
            assert_matches!(
                &client.handle_timeout(ClientTimerType::Retransmission)[..],
               [
                    Action::SendMessage(buf),
                    // `_timeout` is not checked because retransmission timeout
                    // calculation is covered in its respective test.
                    Action::ScheduleTimer(ClientTimerType::Retransmission, _timeout)
               ] if testutil::msg_type(buf) == v6::MessageType::Request
            );
            let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
                &client;
            let (server_id, retrans_count) = match state {
                Some(ClientState::Requesting(Requesting {
                    client_id: _,
                    addresses_to_request: _,
                    server_id,
                    collected_advertise: _,
                    first_request_time: _,
                    retrans_timeout: _,
                    retrans_count,
                    solicit_max_rt: _,
                })) => (server_id, retrans_count),
                state => panic!("unexpected state {:?}", state),
            };
            assert_eq!(*server_id, server_id_1);
            assert_eq!(*retrans_count, count);
        }

        // When the retransmission count reaches REQUEST_MAX_RC, the client
        // should select another server.
        assert_matches!(
            &client.handle_timeout(ClientTimerType::Retransmission)[..],
           [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
           ] if testutil::msg_type(buf) == v6::MessageType::Request
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let (server_id, retrans_count) = match state {
            Some(ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count,
                solicit_max_rt: _,
            })) => (server_id, retrans_count),
            state => panic!("unexpected state {:?}", state),
        };
        assert_eq!(*server_id, server_id_2);
        assert_eq!(*retrans_count, 0);

        for count in 1..REQUEST_MAX_RC + 1 {
            assert_matches!(
                &client.handle_timeout(ClientTimerType::Retransmission)[..],
               [
                    Action::SendMessage(buf),
                    // `_timeout` is not checked because retransmission timeout
                    // calculation is covered in its respective test.
                    Action::ScheduleTimer(ClientTimerType::Retransmission, _timeout)
               ] if testutil::msg_type(buf) == v6::MessageType::Request
            );
            let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
                &client;
            let (server_id, retrans_count) = match state {
                Some(ClientState::Requesting(Requesting {
                    client_id: _,
                    addresses_to_request: _,
                    server_id,
                    collected_advertise: _,
                    first_request_time: _,
                    retrans_timeout: _,
                    retrans_count,
                    solicit_max_rt: _,
                })) => (server_id, retrans_count),
                state => panic!("unexpected state {:?}", state),
            };
            assert_eq!(*server_id, server_id_2);
            assert_eq!(*retrans_count, count);
        }

        // When the retransmission count reaches REQUEST_MAX_RC, and the client
        // does not have information about another server, the client should
        // restart server discovery.
        assert_matches!(
            &client.handle_timeout(ClientTimerType::Retransmission)[..],
           [
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_SOLICIT_TIMEOUT)
           ] if testutil::msg_type(buf) == v6::MessageType::Solicit
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = client;
        assert_matches!(state,
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise,
                collected_sol_max_rt: _,
            })) if collected_advertise.is_empty()
        );
    }

    // Test 4-msg exchange for address assignment.
    #[test]
    fn assign_addresses() {
        const T1: u32 = 50;
        const T2: u32 = 80;
        let t1_time_value = v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(T1).expect("should succeed for non-zero or u32::MAX values"),
        );
        let t2_time_value = v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(T2).expect("should succeed for non-zero or u32::MAX values"),
        );
        let (client, actions) = testutil::assign_addresses_and_assert(
            v6::duid_uuid(),
            vec![
                TestIdentityAssociation {
                    address: std_ip_v6!("::ffff:c00a:1ff"),
                    preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                        v6::NonZeroOrMaxU32::new(100)
                            .expect("should succeed for non-zero or u32::MAX values"),
                    )),
                    valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                        v6::NonZeroOrMaxU32::new(120)
                            .expect("should succeed for non-zero or u32::MAX values"),
                    )),
                    t1: v6::TimeValue::NonZero(t1_time_value),
                    t2: v6::TimeValue::NonZero(t2_time_value),
                },
                TestIdentityAssociation {
                    address: std_ip_v6!("::ffff:c00a:2ff"),
                    preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                        v6::NonZeroOrMaxU32::new(150)
                            .expect("should succeed for non-zero or u32::MAX values"),
                    )),
                    valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                        v6::NonZeroOrMaxU32::new(180)
                            .expect("should succeed for non-zero or u32::MAX values"),
                    )),
                    t1: v6::TimeValue::NonZero(t1_time_value),
                    t2: v6::TimeValue::NonZero(t2_time_value),
                },
            ],
            &[],
            t1_time_value,
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        assert_matches!(
            state,
            Some(ClientState::AddressAssigned(AddressAssigned {
                client_id: _,
                addresses: _,
                server_id: _,
                t1: _,
                t2: got_t2,
                dns_servers: _,
                solicit_max_rt: _,
            })) if *got_t2 == v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(T2).expect("should succeed"))
        );
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1)
            ] if *t1 == Duration::from_secs(T1.into())
        );
    }

    #[test]
    fn address_assigned_get_dns_servers() {
        let dns_servers = [std_ip_v6!("ff01::0102"), std_ip_v6!("ff01::0304")];
        const T1: u32 = 70;
        let t1_time_value = v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(T1).expect("should succeed for non-zero or u32::MAX values"),
        );
        let (client, actions) = testutil::assign_addresses_and_assert(
            v6::duid_uuid(),
            vec![TestIdentityAssociation {
                address: std_ip_v6!("::ffff:c00a:101"),
                preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(100)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
                valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(120)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
                t1: v6::TimeValue::NonZero(t1_time_value),
                t2: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(90)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
            }],
            &dns_servers,
            t1_time_value,
            StepRng::new(std::u64::MAX / 2, 0),
        );
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::UpdateDnsServers(got_dns_servers),
                Action::ScheduleTimer(ClientTimerType::Renew, t1)
            ] if got_dns_servers[..] == dns_servers &&
                 *t1 == Duration::from_secs(T1.into())
        );
        assert_eq!(client.get_dns_servers()[..], dns_servers);
    }

    #[test]
    fn update_sol_max_rt_on_reply_to_request() {
        let options_to_request = vec![];
        let configured_addresses = testutil::to_configured_addresses(1, vec![]);
        let address = std_ip_v6!("::ffff:c00a:1ff");
        let server_id = vec![1, 2, 3];
        let selected_advertise = AdvertiseMessage::new_default(
            server_id.clone(),
            &[address],
            &[],
            &configured_addresses,
        );
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);
        let client_id = v6::duid_uuid();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            client_id.clone(),
            configured_addresses.clone(),
            selected_advertise,
            &options_to_request[..],
            BinaryHeap::new(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
        );
        assert_matches!(&state,
            ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt,
            }) if *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );
        let received_sol_max_rt = 4800;

        // If the reply does not contain a server ID, the reply should be
        // discarded and the `solicit_max_rt` should not be updated.
        let iana_options =
            [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(address, 60, 120, &[]))];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 30, 45, &iana_options)),
            v6::DhcpOption::SolMaxRt(received_sol_max_rt),
        ];
        let request_transaction_id = transaction_id.unwrap();
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id: _ } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        assert_matches!(&state,
            ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt,
            }) if *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );

        // If the reply has a different client ID than the test client's client ID,
        // the `solicit_max_rt` should not be updated.
        let other_client_id = v6::duid_uuid();
        let options = [
            v6::DhcpOption::ServerId(&server_id),
            v6::DhcpOption::ClientId(&other_client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 30, 45, &iana_options)),
            v6::DhcpOption::SolMaxRt(received_sol_max_rt),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id: _ } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        assert_matches!(&state,
            ClientState::Requesting(Requesting {
                client_id: _,
                addresses_to_request: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt,
            }) if *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );

        // If the client receives a valid reply containing a SOL_MAX_RT option,
        // the `solicit_max_rt` should be updated.
        let options = [
            v6::DhcpOption::ServerId(&server_id),
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), 30, 45, &iana_options)),
            v6::DhcpOption::SolMaxRt(received_sol_max_rt),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id: _ } =
            state.reply_message_received(&options_to_request, &mut rng, msg);
        assert_matches!(&state,
            ClientState::AddressAssigned(AddressAssigned {
                    client_id: _,
                    addresses: _,
                    server_id: _,
                    t1: _,
                    t2: _,
                    dns_servers:_,
                    solicit_max_rt,
            }) if *solicit_max_rt == Duration::from_secs(received_sol_max_rt.into())
        );
    }

    #[test]
    fn send_renew() {
        let t1 = v6::NonZeroOrMaxU32::new(30).expect("30 is not zero or u32::MAX");
        let t2 = v6::NonZeroOrMaxU32::new(70).expect("70 is not zero or u32::MAX");
        let preferred_lifetime = v6::NonZeroOrMaxU32::new(80).expect("80 is not zero or u32::MAX");
        let valid_lifetime = v6::NonZeroOrMaxU32::new(110).expect("110 is not zero or u32::MAX");
        let _client = testutil::send_renew_and_assert(
            v6::duid_uuid(),
            vec![
                TestIdentityAssociation::new_nonzero_finite(
                    std_ip_v6!("::ffff:c00a:123"),
                    preferred_lifetime,
                    valid_lifetime,
                    t1,
                    t2,
                ),
                TestIdentityAssociation::new_nonzero_finite(
                    std_ip_v6!("::ffff:c00a:456"),
                    preferred_lifetime,
                    valid_lifetime,
                    t1,
                    t2,
                ),
            ],
            v6::NonZeroTimeValue::Finite(t1),
            StepRng::new(std::u64::MAX / 2, 0),
        );
    }

    #[test]
    fn do_not_renew_for_t1_infinity() {
        let client_id = v6::duid_uuid();
        let expected_t1 = v6::NonZeroTimeValue::Infinity;
        let (client, actions) = testutil::assign_addresses_and_assert(
            client_id.clone(),
            vec![TestIdentityAssociation {
                address: std_ip_v6!("::ffff:c00a:1ff"),
                preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(100)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
                valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(120)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
                t1: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
                t2: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
            }],
            &[],
            expected_t1,
            StepRng::new(std::u64::MAX / 2, 0),
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        assert_matches!(state,
            Some(ClientState::AddressAssigned(AddressAssigned {
                    client_id: _,
                    addresses: _,
                    server_id: _,
                    t1,
                    t2: _,
                    dns_servers:_,
                    solicit_max_rt: _,
            })) if *t1 == expected_t1
        );
        // Asserts that the actions do not include scheduling the renew timer.
        assert_matches!(&actions[..], [Action::CancelTimer(ClientTimerType::Retransmission)]);
    }

    #[test]
    fn retransmit_renew() {
        let client_id = v6::duid_uuid();
        let t1 = v6::NonZeroOrMaxU32::new(70).expect("70 is not zero or u32::MAX");
        let t2 = v6::NonZeroOrMaxU32::new(90).expect("90 is not zero or u32::MAX");
        let preferred_lifetime = v6::NonZeroOrMaxU32::new(90).expect("90 is not zero or u32::MAX");
        let valid_lifetime = v6::NonZeroOrMaxU32::new(120).expect("120 is not zero or u32::MAX");
        let t1_time_value = v6::NonZeroTimeValue::Finite(t1);
        let addresses_to_assign = vec![
            TestIdentityAssociation::new_nonzero_finite(
                std_ip_v6!("::ffff:c00a:123"),
                preferred_lifetime,
                valid_lifetime,
                t1,
                t2,
            ),
            TestIdentityAssociation::new_nonzero_finite(
                std_ip_v6!("::ffff:c00a:456"),
                preferred_lifetime,
                valid_lifetime,
                t1,
                t2,
            ),
        ];
        let mut client = testutil::send_renew_and_assert(
            client_id.clone(),
            addresses_to_assign.clone(),
            t1_time_value,
            StepRng::new(std::u64::MAX / 2, 0),
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        let expected_transaction_id = *transaction_id;
        let (expected_server_id, expected_t2) = assert_matches!(
           state,
           Some(ClientState::Renewing(Renewing {
               client_id: _,
               addresses: _,
               server_id,
               t1: _,
               t2,
               dns_servers: _,
               first_renew_time: _,
               retrans_timeout: _,
               solicit_max_rt: _,
           })) => (server_id.clone(), *t2)
        );

        // Assert renew is retransmitted on retransmission timeout.
        let actions = client.handle_timeout(ClientTimerType::Retransmission);
        let mut buf = assert_matches!(
            &actions[..],
            [
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, timeout)
            ] if *timeout == 2 * INITIAL_RENEW_TIMEOUT => buf
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        // Check that the retransmitted renew is part of the same transaction.
        assert_eq!(*transaction_id, expected_transaction_id);
        assert_matches!(
            state,
            Some(ClientState::Renewing(Renewing {
                client_id: got_client_id,
                addresses: _,
                server_id,
                t1: got_t1,
                t2,
                dns_servers,
                first_renew_time: _,
                retrans_timeout: _,
                solicit_max_rt,
            })) if *got_client_id == client_id &&
                   *server_id == expected_server_id &&
                   *got_t1 == t1 &&
                   *t2 == expected_t2 &&
                   *dns_servers == Vec::<Ipv6Addr>::new() &&
                   *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );
        let expected_addresses_to_renew: HashMap<v6::IAID, Option<Ipv6Addr>> = (0..)
            .map(v6::IAID::new)
            .zip(addresses_to_assign.iter().map(
                |TestIdentityAssociation {
                     address,
                     preferred_lifetime: _,
                     valid_lifetime: _,
                     t1: _,
                     t2: _,
                 }| Some(*address),
            ))
            .collect();
        testutil::assert_outgoing_stateful_message(
            &mut buf,
            v6::MessageType::Renew,
            &client_id,
            Some(&expected_server_id),
            &[],
            &expected_addresses_to_renew,
        );
    }

    #[test]
    fn unexpected_messages_are_ignored() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let builder = v6::MessageBuilder::new(
            v6::MessageType::Reply,
            // Transaction ID is different from the client's.
            [4, 5, 6],
            &[],
        );
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        assert!(client.handle_message_receive(msg).is_empty());

        // Messages with unsupported/unexpected types are discarded.
        for msg_type in IntoIterator::into_iter([
            v6::MessageType::Solicit,
            v6::MessageType::Advertise,
            v6::MessageType::Request,
            v6::MessageType::Confirm,
            v6::MessageType::Renew,
            v6::MessageType::Rebind,
            v6::MessageType::Release,
            v6::MessageType::Decline,
            v6::MessageType::Reconfigure,
            v6::MessageType::InformationRequest,
            v6::MessageType::RelayForw,
            v6::MessageType::RelayRepl,
        ]) {
            let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
                &client;
            let builder = v6::MessageBuilder::new(msg_type, *transaction_id, &[]);
            let mut buf = vec![0; builder.bytes_len()];
            builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

            assert!(client.handle_message_receive(msg).is_empty());
        }
    }

    #[test]
    #[should_panic(expected = "received unexpected refresh timeout")]
    fn infomation_requesting_refresh_timeout_is_unreachable() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // Should panic if Refresh timeout is received while in
        // InformationRequesting state.
        let _actions = client.handle_timeout(ClientTimerType::Refresh);
    }

    #[test]
    #[should_panic(expected = "received unexpected retransmission timeout")]
    fn infomation_received_retransmission_timeout_is_unreachable() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        assert_matches!(
            *state,
            Some(ClientState::InformationRequesting(InformationRequesting {
                retrans_timeout: INITIAL_INFO_REQ_TIMEOUT
            }))
        );

        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &[]);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        // Transition to InformationReceived state.
        assert_eq!(
            client.handle_message_receive(msg)[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Refresh, IRT_DEFAULT)
            ]
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        assert_matches!(
            state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers})) if dns_servers.is_empty()
        );

        // Should panic if Retransmission timeout is received while in
        // InformationReceived state.
        let _actions = client.handle_timeout(ClientTimerType::Retransmission);
    }

    #[test]
    #[should_panic(expected = "received unexpected refresh timeout")]
    fn server_discovery_refresh_timeout_is_unreachable() {
        let client_id = v6::duid_uuid();
        let mut client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            client_id.clone(),
            testutil::to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // Should panic if Refresh is received while in ServerDiscovery state.
        let _actions = client.handle_timeout(ClientTimerType::Refresh);
    }

    #[test_case(ClientTimerType::Refresh)]
    #[test_case(ClientTimerType::Retransmission)]
    #[should_panic(expected = "received unexpected")]
    fn address_assiged_unexpected_timeout_is_unreachable(timeout: ClientTimerType) {
        let expected_t1 = v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(60).expect("should succeed for non-zero or u32::MAX values"),
        );
        let (mut client, _actions) = testutil::assign_addresses_and_assert(
            v6::duid_uuid(),
            vec![TestIdentityAssociation {
                address: std_ip_v6!("::ffff:c00a:1ff"),
                preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(100)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
                valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(120)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
                t1: v6::TimeValue::NonZero(expected_t1),
                t2: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    v6::NonZeroOrMaxU32::new(90)
                        .expect("should succeed for non-zero or u32::MAX values"),
                )),
            }],
            &[],
            expected_t1,
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // Should panic if Refresh or Retransmission timeout is received while
        // in AddressAssigned state.
        let _actions = client.handle_timeout(timeout);
    }

    #[test]
    #[should_panic(expected = "received unexpected refresh timeout")]
    fn renewing_refresh_timeout_is_unreachable() {
        let t1 = v6::NonZeroOrMaxU32::new(40).expect("40 is non-zero or u32::MAX");
        let mut client = testutil::send_renew_and_assert(
            v6::duid_uuid(),
            vec![TestIdentityAssociation::new_nonzero_finite(
                std_ip_v6!("::ffff:c00a:111"),
                v6::NonZeroOrMaxU32::new(50).expect("50 is non-zero or u32::MAX"),
                v6::NonZeroOrMaxU32::new(80).expect("80 is non-zero or u32::MAX"),
                t1,
                v6::NonZeroOrMaxU32::new(60).expect("60 is non-zero or u32::MAX"),
            )],
            v6::NonZeroTimeValue::Finite(t1),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // Should panic if Refresh is received while in Renewing state.
        let _actions = client.handle_timeout(ClientTimerType::Refresh);
    }

    // NOTE: All comparisons are done on millisecond, so this test is not affected by precision
    // loss from floating point arithmetic.
    #[test]
    #[ignore]
    fn retransmission_timeout() {
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let initial_rt = Duration::from_secs(1);
        let max_rt = Duration::from_secs(100);

        // Start with initial timeout if previous timeout is zero.
        let t =
            super::retransmission_timeout(Duration::from_nanos(0), initial_rt, max_rt, &mut rng);
        assert_eq!(t.as_millis(), initial_rt.as_millis());

        // Use previous timeout when it's not zero and apply the formula.
        let t =
            super::retransmission_timeout(Duration::from_secs(10), initial_rt, max_rt, &mut rng);
        assert_eq!(t, Duration::from_secs(20));

        // Cap at max timeout.
        let t = super::retransmission_timeout(100 * max_rt, initial_rt, max_rt, &mut rng);
        assert_eq!(t.as_millis(), max_rt.as_millis());
        let t = super::retransmission_timeout(MAX_DURATION, initial_rt, max_rt, &mut rng);
        assert_eq!(t.as_millis(), max_rt.as_millis());
        // Zero max means no cap.
        let t = super::retransmission_timeout(
            100 * max_rt,
            initial_rt,
            Duration::from_nanos(0),
            &mut rng,
        );
        assert_eq!(t.as_millis(), (200 * max_rt).as_millis());
        // Overflow durations are clipped.
        let t = super::retransmission_timeout(
            MAX_DURATION,
            initial_rt,
            Duration::from_nanos(0),
            &mut rng,
        );
        assert_eq!(t.as_millis(), MAX_DURATION.as_millis());

        // Steps through the range with deterministic randomness, 20% at a time.
        let mut rng = StepRng::new(0, std::u64::MAX / 5);
        [
            (Duration::from_millis(10000), 19000),
            (Duration::from_millis(10000), 19400),
            (Duration::from_millis(10000), 19800),
            (Duration::from_millis(10000), 20200),
            (Duration::from_millis(10000), 20600),
            (Duration::from_millis(10000), 21000),
            (Duration::from_millis(10000), 19400),
            // Cap at max timeout with randomness.
            (100 * max_rt, 98000),
            (100 * max_rt, 102000),
            (100 * max_rt, 106000),
            (100 * max_rt, 110000),
            (100 * max_rt, 94000),
            (100 * max_rt, 98000),
        ]
        .iter()
        .for_each(|(rt, want_ms)| {
            let t = super::retransmission_timeout(*rt, initial_rt, max_rt, &mut rng);
            assert_eq!(t.as_millis(), *want_ms);
        });
    }

    #[test_case(v6::TimeValue::Zero, v6::TimeValue::Zero, v6::TimeValue::Zero)]
    #[test_case(
        v6::TimeValue::Zero,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values")))
     )]
    #[test_case(
        v6::TimeValue::Zero,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity)
    )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))),
        v6::TimeValue::Zero,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values")))
     )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(60).expect("should succeed for non-zero or u32::MAX values"))),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(60).expect("should succeed for non-zero or u32::MAX values")))
     )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values")))
     )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values")))
     )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity)
    )]
    fn maybe_get_nonzero_min(
        old_value: v6::TimeValue,
        new_value: v6::TimeValue,
        expected_value: v6::TimeValue,
    ) {
        assert_eq!(super::maybe_get_nonzero_min(old_value, new_value), expected_value);
    }

    #[test_case(
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values")),
        v6::TimeValue::Zero,
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))
    )]
    #[test_case(
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values")),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(60).expect("should succeed for non-zero or u32::MAX values"))),
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(60).expect("should succeed for non-zero or u32::MAX values"))
    )]
    #[test_case(
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values")),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))
    )]
    #[test_case(
        v6::NonZeroTimeValue::Infinity,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))),
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(120).expect("should succeed for non-zero or u32::MAX values"))
    )]
    #[test_case(
        v6::NonZeroTimeValue::Infinity,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::NonZeroTimeValue::Infinity
    )]
    #[test_case(
        v6::NonZeroTimeValue::Infinity,
        v6::TimeValue::Zero,
        v6::NonZeroTimeValue::Infinity
    )]
    fn get_nonzero_min(
        old_value: v6::NonZeroTimeValue,
        new_value: v6::TimeValue,
        expected_value: v6::NonZeroTimeValue,
    ) {
        assert_eq!(super::get_nonzero_min(old_value, new_value), expected_value);
    }

    #[test_case(
        v6::NonZeroTimeValue::Infinity,
        T1_MIN_LIFETIME_RATIO,
        v6::NonZeroTimeValue::Infinity
    )]
    #[test_case(
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(100).expect("should succeed")),
        T1_MIN_LIFETIME_RATIO,
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(50).expect("should succeed"))
    )]
    #[test_case(v6::NonZeroTimeValue::Infinity, T2_T1_RATIO, v6::NonZeroTimeValue::Infinity)]
    #[test_case(
        v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(INFINITY - 1).expect("should succeed")),
        T2_T1_RATIO,
        v6::NonZeroTimeValue::Infinity
    )]
    fn compute_t(min: v6::NonZeroTimeValue, ratio: Ratio<u32>, expected_t: v6::NonZeroTimeValue) {
        assert_eq!(super::compute_t(min, ratio), expected_t);
    }

    #[test_case(None, None, false)]
    #[test_case(None, Some(std_ip_v6!("ff01::0102")), true)]
    #[test_case(Some(std_ip_v6!("ff01::0102")), None, true)]
    #[test_case(Some(std_ip_v6!("ff01::0102")), Some(std_ip_v6!("ff01::0304")), true)]
    #[test_case(Some(std_ip_v6!("ff01::0304")), Some(std_ip_v6!("ff01::0304")), false)]
    fn create_non_configured_address(
        address: Option<Ipv6Addr>,
        configured_address: Option<Ipv6Addr>,
        expect_address_is_some: bool,
    ) {
        assert_eq!(
            NonConfiguredAddress::new(address, configured_address).is_some(),
            expect_address_is_some
        );
    }

    #[test_case(None, Some(std_ip_v6!("ff01::0102")))]
    #[test_case(Some(std_ip_v6!("ff01::0102")), None)]
    #[test_case(Some(std_ip_v6!("ff01::0102")), Some(std_ip_v6!("ff01::0304")))]
    fn non_configured_address_get_address(
        address: Option<Ipv6Addr>,
        configured_address: Option<Ipv6Addr>,
    ) {
        let non_conf_ia = NonConfiguredAddress::new(address, configured_address);
        match non_conf_ia {
            Some(p) => assert_eq!(p.address(), address),
            None => panic!("should suceed to create non configured IA"),
        }
    }

    #[test_case(None, Some(std_ip_v6!("ff01::0102")))]
    #[test_case(Some(std_ip_v6!("ff01::0102")), None)]
    #[test_case(Some(std_ip_v6!("ff01::0102")), Some(std_ip_v6!("ff01::0304")))]
    fn non_configured_address_get_configured_address(
        address: Option<Ipv6Addr>,
        configured_address: Option<Ipv6Addr>,
    ) {
        let non_conf_ia = NonConfiguredAddress::new(address, configured_address);
        match non_conf_ia {
            Some(p) => assert_eq!(p.configured_address(), configured_address),
            None => panic!("should suceed to create non configured IA"),
        }
    }

    #[test_case(std_ip_v6!("ff01::0102"), None, true)]
    #[test_case(std_ip_v6!("ff01::0102"), Some(std_ip_v6!("ff01::0304")), true)]
    #[test_case(std_ip_v6!("ff01::0304"), Some(std_ip_v6!("ff01::0304")), false)]
    fn create_non_configured_ia(
        address: Ipv6Addr,
        configured_address: Option<Ipv6Addr>,
        expect_ia_is_some: bool,
    ) {
        let ia = IdentityAssociation {
            address,
            preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                v6::NonZeroOrMaxU32::new(60)
                    .expect("should succeed for non-zero or u32::MAX values"),
            )),
            valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                v6::NonZeroOrMaxU32::new(90)
                    .expect("should succeed for non-zero or u32::MAX values"),
            )),
        };
        assert_eq!(NonConfiguredIA::new(ia, configured_address).is_some(), expect_ia_is_some);
    }
}

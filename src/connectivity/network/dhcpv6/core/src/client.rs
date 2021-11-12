// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Core DHCPv6 client state transitions.

use {
    matches::assert_matches,
    packet::serialize::InnerPacketBuilder,
    packet_formats_dhcp::v6,
    rand::Rng,
    std::{
        cmp::{Eq, Ord, PartialEq, PartialOrd},
        collections::{hash_map::Entry, BinaryHeap, HashMap},
        convert::TryFrom,
        default::Default,
        net::Ipv6Addr,
        time::Duration,
        time::Instant,
    },
    zerocopy::ByteSlice,
};

/// Initial Information-request timeout `INF_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INITIAL_INFO_REQ_TIMEOUT: Duration = Duration::from_secs(1);
/// Max Information-request timeout `INF_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INFO_REQ_MAX_RT: Duration = Duration::from_secs(3600);
/// Default information refresh time from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const IRT_DEFAULT: Duration = Duration::from_secs(86400);

/// The max duration in seconds `std::time::Duration` supports.
///
/// NOTE: it is possible for `Duration` to be bigger by filling in the nanos field, but this value
/// is good enough for the purpose of this crate.
const MAX_DURATION: Duration = Duration::from_secs(std::u64::MAX);

/// Initial Solicit timeout `SOL_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INITIAL_SOLICIT_TIMEOUT: Duration = Duration::from_secs(1);

/// Max Solicit timeout `SOL_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const SOLICIT_MAX_RT: Duration = Duration::from_secs(3600);

/// The valid range for `SOL_MAX_RT`, as defined in [RFC 8415, Section 21.24].
///
/// [RFC 8415, Section 21.24](https://datatracker.ietf.org/doc/html/rfc8415#section-21.24)
const VALID_SOLICIT_MAX_RT_RANGE: std::ops::RangeInclusive<u32> = 60..=86400;

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
    let rand = rng.gen_range(RANDOMIZATION_FACTOR_MIN, RANDOMIZATION_FACTOR_MAX);

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

/// Identifies what event should be triggered when a timer fires.
#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone)]
pub enum ClientTimerType {
    Retransmission,
    Refresh,
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
        retransmission_timeout(self.retrans_timeout, INITIAL_INFO_REQ_TIMEOUT, INFO_REQ_MAX_RT, rng)
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
        let () = builder.serialize(&mut buf);

        let retrans_timeout = self.retransmission_timeout(rng);

        Transition {
            state: ClientState::InformationRequesting(InformationRequesting { retrans_timeout }),
            actions: vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
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

        let actions = std::array::IntoIter::new([
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

#[derive(Debug, PartialEq)]
struct IdentityAssociation {
    // TODO(https://fxbug.dev/86950): use UnicastAddr.
    address: Ipv6Addr,
    preferred_lifetime: Duration,
    valid_lifetime: Duration,
}

// Holds the information received in an Advertise message.
#[derive(Debug)]
struct AdvertiseMessage {
    server_id: Vec<u8>,
    addresses: HashMap<u32, IdentityAssociation>,
    dns_servers: Vec<Ipv6Addr>,
    preference: u8,
    receive_time: Instant,
    preferred_addresses_count: usize,
}

impl AdvertiseMessage {
    fn is_complete(
        &self,
        configured_addresses: &HashMap<u32, Option<Ipv6Addr>>,
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
    got_addresses: &HashMap<u32, IdentityAssociation>,
    configured_addresses: &HashMap<u32, Option<Ipv6Addr>>,
) -> usize {
    configured_addresses.iter().fold(0, |count, (iaid, address)| {
        count
            + address.map_or_else(
                || 0,
                |addr| {
                    got_addresses
                        .get(iaid)
                        .map_or_else(|| 0, |got_ia| usize::from(got_ia.address == addr))
                },
            )
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
    configured_addresses: HashMap<u32, Option<Ipv6Addr>>,
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
        configured_addresses: HashMap<u32, Option<Ipv6Addr>>,
        options_to_request: &[v6::OptionCode],
        solicit_max_rt: Duration,
        rng: &mut R,
    ) -> Transition {
        let server_discovery = ServerDiscovery {
            client_id,
            configured_addresses,
            first_solicit_time: None,
            retrans_timeout: Duration::default(),
            solicit_max_rt,
            collected_advertise: BinaryHeap::new(),
            collected_sol_max_rt: Vec::new(),
        };
        server_discovery.send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }

    /// Calculates timeout for retransmitting solicits using parameters
    /// specified in [RFC 8415, Section 18.2.1].
    ///
    /// [RFC 8415, Section 18.2.1]: https://datatracker.ietf.org/doc/html/rfc8415#section-18.2.1
    fn retransmission_timeout<R: Rng>(&self, rng: &mut R) -> Duration {
        let Self {
            client_id: _,
            configured_addresses: _,
            first_solicit_time: _,
            retrans_timeout,
            solicit_max_rt,
            collected_advertise: _,
            collected_sol_max_rt: _,
        } = self;
        retransmission_timeout(*retrans_timeout, INITIAL_SOLICIT_TIMEOUT, *solicit_max_rt, rng)
    }

    /// Returns a transition back to `ServerDiscovery`, with actions to send a
    /// solicit and schedule retransmission.
    fn send_and_schedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
    ) -> Transition {
        let mut options = vec![v6::DhcpOption::ClientId(&self.client_id)];

        let elapsed_time = match self.first_solicit_time {
            None => 0,
            Some(first_solicit_time) => elapsed_time_in_centisecs(first_solicit_time),
        };
        options.push(v6::DhcpOption::ElapsedTime(elapsed_time));

        // TODO(https://fxbug.dev/86945): remove `address_hint` construction
        // once `IanaSerializer::new()` takes options by value.
        let mut address_hint = HashMap::new();
        for (iaid, addr_opt) in &self.configured_addresses {
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
        let () = builder.serialize(&mut buf);

        let retrans_timeout = self.retransmission_timeout(rng);
        let first_solicit_time = Some(self.first_solicit_time.unwrap_or(Instant::now()));

        Transition {
            state: ClientState::ServerDiscovery(ServerDiscovery {
                client_id: self.client_id,
                configured_addresses: self.configured_addresses,
                first_solicit_time,
                retrans_timeout,
                solicit_max_rt: self.solicit_max_rt,
                collected_advertise: self.collected_advertise,
                collected_sol_max_rt: self.collected_sol_max_rt,
            }),
            actions: vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
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
            mut solicit_max_rt,
            collected_advertise,
            collected_sol_max_rt,
        } = self;
        // Update SOL_MAX_RT, per RFC 8415, section 18.2.9:
        //
        //    A client SHOULD only update its SOL_MAX_RT [..] if all received
        //    Advertise messages that contained the corresponding option
        //    specified the same value.
        if !collected_sol_max_rt.is_empty()
            && collected_sol_max_rt.iter().all(|sol_max_rt| *sol_max_rt == collected_sol_max_rt[0])
        {
            solicit_max_rt = Duration::from_secs(collected_sol_max_rt[0].into());
        }

        let best_advertise = collected_advertise.peek();
        if best_advertise.is_some() {
            return Transition {
                // TODO(fxbug.dev/69696): implement Requesting.
                state: ClientState::Requesting(Requesting {}),
                actions: vec![Action::CancelTimer(ClientTimerType::Retransmission)],
            };
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

    fn advertise_message_received<B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
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
        let mut addresses: HashMap<u32, IdentityAssociation> = HashMap::new();
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
                    let vacant_ia_entry = match addresses.entry(iana_data.iaid()) {
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
                                    preferred_lifetime: Duration::from_secs(
                                        iaaddr_data.preferred_lifetime().into(),
                                    ),
                                    valid_lifetime: Duration::from_secs(
                                        iaaddr_data.valid_lifetime().into(),
                                    ),
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
            if VALID_SOLICIT_MAX_RT_RANGE.contains(&solicit_max_rt_option) {
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
            return Transition {
                // TODO(https://fxbug.dev/69696): implement Requesting.
                state: ClientState::Requesting(Requesting {}),
                actions: vec![Action::CancelTimer(ClientTimerType::Retransmission)],
            };
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
        }
    }
}

#[derive(Debug, Default)]
struct Requesting {}

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
}

/// State transition, containing the next state, and the actions the client
/// should take to transition to that state.
struct Transition {
    state: ClientState,
    actions: Actions,
}

impl ClientState {
    /// Handles a received advertise message.
    fn advertise_message_received<B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        msg: v6::Message<'_, B>,
    ) -> Transition {
        match self {
            ClientState::ServerDiscovery(s) => {
                s.advertise_message_received(options_to_request, msg)
            }
            ClientState::InformationRequesting(_)
            | ClientState::InformationReceived(_)
            | ClientState::Requesting(_) => Transition { state: self, actions: Vec::new() },
        }
    }

    /// Handles a received reply message.
    fn reply_message_received<B: ByteSlice>(self, msg: v6::Message<'_, B>) -> Transition {
        match self {
            ClientState::InformationRequesting(s) => s.reply_message_received(msg),
            ClientState::InformationReceived(_)
            | ClientState::ServerDiscovery(_)
            | ClientState::Requesting(_) => Transition { state: self, actions: Vec::new() },
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
            ClientState::InformationReceived(_) | ClientState::Requesting(_) => {
                Transition { state: self, actions: Vec::new() }
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
            | ClientState::Requesting(_) => Transition { state: self, actions: Vec::new() },
        }
    }

    fn get_dns_servers(&self) -> Vec<Ipv6Addr> {
        match self {
            ClientState::InformationReceived(s) => s.dns_servers.clone(),
            ClientState::InformationRequesting(_)
            | ClientState::ServerDiscovery(_)
            | ClientState::Requesting(_) => Vec::new(),
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
        let Transition { state, actions } =
            InformationRequesting::start(transaction_id, &options_to_request, &mut rng);
        (Self { state: Some(state), transaction_id, options_to_request, rng }, actions)
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
        configured_addresses: HashMap<u32, Option<Ipv6Addr>>,
        options_to_request: Vec<v6::OptionCode>,
        mut rng: R,
    ) -> (Self, Actions) {
        let Transition { state, actions } = ServerDiscovery::start(
            transaction_id,
            client_id,
            configured_addresses,
            &options_to_request,
            SOLICIT_MAX_RT,
            &mut rng,
        );
        (Self { state: Some(state), transaction_id, options_to_request, rng }, actions)
    }

    pub fn get_dns_servers(&self) -> Vec<Ipv6Addr> {
        return self.state.as_ref().expect("state should not be empty").get_dns_servers();
    }

    /// Handles a timeout event, dispatches based on timeout type.
    ///
    /// # Panics
    ///
    /// `handle_timeout` panics if current state is None.
    pub fn handle_timeout(&mut self, timeout_type: ClientTimerType) -> Actions {
        let state = self.state.take().expect("state should not be empty");
        let Transition { state, actions } = match timeout_type {
            ClientTimerType::Retransmission => state.retransmission_timer_expired(
                self.transaction_id,
                &self.options_to_request,
                &mut self.rng,
            ),
            ClientTimerType::Refresh => state.refresh_timer_expired(
                self.transaction_id,
                &self.options_to_request,
                &mut self.rng,
            ),
        };
        self.state = Some(state);
        actions
    }

    /// Handles a received DHCPv6 message.
    ///
    /// # Panics
    ///
    /// `handle_reply` panics if current state is None.
    pub fn handle_message_receive<B: ByteSlice>(&mut self, msg: v6::Message<'_, B>) -> Actions {
        if msg.transaction_id() != &self.transaction_id {
            Vec::new() // Ignore messages for other clients.
        } else {
            match msg.msg_type() {
                v6::MessageType::Reply => {
                    let Transition { state, actions } = self
                        .state
                        .take()
                        .expect("state should not be empty")
                        .reply_message_received(msg);
                    self.state = Some(state);
                    actions
                }
                v6::MessageType::Advertise => {
                    let Transition { state, actions } = self
                        .state
                        .take()
                        .expect("state should not be empty")
                        .advertise_message_received(&self.options_to_request, msg);
                    self.state = Some(state);
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
mod tests {
    use {
        super::*, net_declare::std_ip_v6, packet::ParsablePacket, rand::rngs::mock::StepRng,
        test_case::test_case,
    };

    #[test]
    fn test_information_request_and_reply() {
        // Try to start information request with different list of requested options.
        for options in std::array::IntoIter::new([
            Vec::new(),
            vec![v6::OptionCode::DnsServers],
            vec![v6::OptionCode::DnsServers, v6::OptionCode::DomainList],
        ]) {
            let (mut client, actions) = ClientStateMachine::start_stateless(
                [0, 1, 2],
                options.clone(),
                StepRng::new(std::u64::MAX / 2, 0),
            );

            assert_matches!(
                client.state,
                Some(ClientState::InformationRequesting(InformationRequesting {
                    retrans_timeout: INITIAL_INFO_REQ_TIMEOUT,
                }))
            );

            // Start of information requesting should send an information request and schedule a
            // retransmission timer.
            let want_options_array = [v6::DhcpOption::Oro(&options)];
            let want_options = if options.is_empty() { &[][..] } else { &want_options_array[..] };
            let builder = v6::MessageBuilder::new(
                v6::MessageType::InformationRequest,
                client.transaction_id,
                want_options,
            );
            let mut want_buf = vec![0; builder.bytes_len()];
            let () = builder.serialize(&mut want_buf);
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
                v6::MessageBuilder::new(v6::MessageType::Reply, client.transaction_id, &options);
            let mut buf = vec![0; builder.bytes_len()];
            let () = builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

            let actions = client.handle_message_receive(msg);

            {
                assert_matches!(
                    client.state,
                    Some(ClientState::InformationReceived(InformationReceived {
                        dns_servers: d
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

    fn to_configured_addresses(
        address_count: u32,
        preferred_addresses: Vec<Ipv6Addr>,
    ) -> HashMap<u32, Option<Ipv6Addr>> {
        let addresses = preferred_addresses
            .into_iter()
            .map(Some)
            .chain(std::iter::repeat(None))
            .take(usize::try_from(address_count).unwrap());

        let configured_addresses: HashMap<u32, Option<Ipv6Addr>> = (0..).zip(addresses).collect();
        configured_addresses
    }

    /// Creates a stateful client and asserts that:
    ///    - the client is started in ServerDiscovery state
    ///    - the state contain the expected value
    ///    - the actions are correct
    ///    - the Solicit message is correct
    ///
    /// Returns the client in ServerDiscovery state and the actions associated
    /// with transitioning to this state.
    fn start_server_discovery<R: Rng>(
        transaction_id: [u8; 3],
        client_id: [u8; CLIENT_ID_LEN],
        configured_addresses: HashMap<u32, Option<Ipv6Addr>>,
        options_to_request: Vec<v6::OptionCode>,
        rng: R,
    ) -> (ClientStateMachine<R>, Actions) {
        let (client, actions) = ClientStateMachine::start_stateful(
            transaction_id,
            client_id.clone(),
            configured_addresses.clone(),
            options_to_request.clone(),
            rng,
        );

        assert_matches!(&client.state,
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: got_client_id,
                configured_addresses: got_configured_addresses,
                first_solicit_time: Some(_),
                retrans_timeout: INITIAL_SOLICIT_TIMEOUT,
                solicit_max_rt: SOLICIT_MAX_RT,
                collected_advertise,
                collected_sol_max_rt,
            })) if collected_advertise.is_empty() &&
                   collected_sol_max_rt.is_empty() &&
                   *got_client_id == client_id &&
                   *got_configured_addresses == configured_addresses
        );

        // Start of server discovery should send a solicit and schedule a
        // retransmission timer.
        let buf = match &actions[..] {
            [Action::SendMessage(buf), Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_SOLICIT_TIMEOUT)] => {
                buf
            }
            actions => panic!("unexpected actions {:?}", actions),
        };

        let mut buf = &buf[..];
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_eq!(msg.msg_type(), v6::MessageType::Solicit);

        // The solicit should contain the expected options.
        let (mut non_ia_opts, ia_opts, other) = msg.options().fold(
            (Vec::new(), Vec::new(), Vec::new()),
            |(mut non_ia_opts, mut ia_opts, mut other), opt| {
                match opt {
                    v6::ParsedDhcpOption::ClientId(_)
                    | v6::ParsedDhcpOption::ElapsedTime(_)
                    | v6::ParsedDhcpOption::Oro(_) => non_ia_opts.push(opt),
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

        non_ia_opts.sort_by(option_sorter);
        let mut expected_oro = vec![v6::OptionCode::SolMaxRt];
        expected_oro.extend_from_slice(&options_to_request);
        let mut expected_non_ia_opts = vec![
            v6::ParsedDhcpOption::ClientId(&client_id),
            v6::ParsedDhcpOption::ElapsedTime(0),
            v6::ParsedDhcpOption::Oro(expected_oro),
        ];
        expected_non_ia_opts.sort_by(option_sorter);
        assert_eq!(non_ia_opts, expected_non_ia_opts);

        let mut solicited_addresses = HashMap::new();
        for iana_data in ia_opts.iter() {
            if iana_data.iter_options().count() == 0 {
                assert_eq!(solicited_addresses.insert(iana_data.iaid(), None), None);
                continue;
            }
            for iana_option in iana_data.iter_options() {
                match iana_option {
                    v6::ParsedDhcpOption::IaAddr(iaaddr_data) => {
                        assert_eq!(
                            solicited_addresses.insert(iana_data.iaid(), Some(iaaddr_data.addr())),
                            None
                        );
                    }
                    option => panic!("unexpected option {:?}", option),
                }
            }
        }
        assert_eq!(solicited_addresses, configured_addresses);
        assert_eq!(&other, &[]);

        (client, actions)
    }

    // Test starting the client in stateful mode with different address
    // configurations.
    #[test_case(1, Vec::new(), Vec::new())]
    #[test_case(2, vec![std_ip_v6!("::ffff:c00a:1ff")], vec![v6::OptionCode::DnsServers])]
    #[test_case(
       2,
       vec![std_ip_v6!("::ffff:c00a:2ff"), std_ip_v6!("::ffff:c00a:3ff")],
       vec![v6::OptionCode::DnsServers])]
    fn test_solicit(
        address_count: u32,
        preferred_addresses: Vec<Ipv6Addr>,
        options_to_request: Vec<v6::OptionCode>,
    ) {
        // The client and actions are checked inside `start_server_discovery`
        let (_client, _actions) = start_server_discovery(
            [0, 1, 2],
            v6::duid_uuid(),
            to_configured_addresses(address_count, preferred_addresses),
            options_to_request,
            StepRng::new(std::u64::MAX / 2, 0),
        );
    }

    impl IdentityAssociation {
        fn new_default(address: Ipv6Addr) -> IdentityAssociation {
            IdentityAssociation {
                address,
                preferred_lifetime: Duration::from_secs(0),
                valid_lifetime: Duration::from_secs(0),
            }
        }
    }

    #[test]
    fn test_preferred_address_count_computation() {
        // No preferred addresses configured.
        let got_addresses: HashMap<u32, IdentityAssociation> = (0..)
            .zip(vec![IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:1ff"))].into_iter())
            .collect();
        let configured_addresses = to_configured_addresses(1, vec![]);
        assert_eq!(compute_preferred_address_count(&got_addresses, &configured_addresses), 0);
        assert_eq!(compute_preferred_address_count(&HashMap::new(), &configured_addresses), 0);

        // All obtained addresses are preferred addresses.
        let got_addresses: HashMap<u32, IdentityAssociation> = (0..)
            .zip(
                vec![
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:1ff")),
                    IdentityAssociation::new_default(std_ip_v6!("::ffff:c00a:2ff")),
                ]
                .into_iter(),
            )
            .collect();
        let configured_addresses = to_configured_addresses(
            2,
            vec![std_ip_v6!("::ffff:c00a:1ff"), std_ip_v6!("::ffff:c00a:2ff")],
        );
        assert_eq!(compute_preferred_address_count(&got_addresses, &configured_addresses), 2);

        // Only one of the obtained addresses is a preferred address.
        let got_addresses: HashMap<u32, IdentityAssociation> = (0..)
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
        let configured_addresses = to_configured_addresses(
            3,
            vec![
                std_ip_v6!("::ffff:c00a:2ff"),
                std_ip_v6!("::ffff:c00a:3ff"),
                std_ip_v6!("::ffff:c00a:4ff"),
            ],
        );
        assert_eq!(compute_preferred_address_count(&got_addresses, &configured_addresses), 1);
    }

    impl AdvertiseMessage {
        fn new_default(
            server_id: Vec<u8>,
            addresses: &[Ipv6Addr],
            dns_servers: &[Ipv6Addr],
            configured_addresses: &HashMap<u32, Option<Ipv6Addr>>,
        ) -> AdvertiseMessage {
            let addresses = (0..)
                .zip(addresses.iter().fold(Vec::new(), |mut addrs, address| {
                    addrs.push(IdentityAssociation {
                        address: *address,
                        preferred_lifetime: Duration::from_secs(0),
                        valid_lifetime: Duration::from_secs(0),
                    });
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

    #[test]
    fn test_advertise_is_complete() {
        let preferred_address = std_ip_v6!("::ffff:c00a:1ff");
        let configured_addresses = to_configured_addresses(2, vec![preferred_address]);

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
    fn test_advertise_ord() {
        let preferred_address = std_ip_v6!("::ffff:c00a:1ff");
        let configured_addresses = to_configured_addresses(3, vec![preferred_address]);

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
    fn test_receive_complete_advertise_with_max_preference() {
        let client_id = v6::duid_uuid();
        let (mut client, _actions) = start_server_discovery(
            [0, 1, 2],
            client_id.clone(),
            to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
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
            v6::DhcpOption::Iana(v6::IanaSerializer::new(0, 60, 60, &iana_options)),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, client.transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
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
            v6::DhcpOption::Iana(v6::IanaSerializer::new(0, 60, 60, &iana_options)),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, client.transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // The client should transition to Requesting when receiving a complete
        // advertise with preference 255.
        let actions = client.handle_message_receive(msg);
        assert_matches!(client.state, Some(ClientState::Requesting(Requesting {})));
        assert_eq!(actions, [Action::CancelTimer(ClientTimerType::Retransmission)]);
    }

    #[test]
    fn test_select_first_server_while_retransmitting() {
        let client_id = v6::duid_uuid();
        let (mut client, _actions) = start_server_discovery(
            [0, 1, 2],
            client_id.clone(),
            to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
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
        let mut buf = &buf[..];
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_eq!(msg.msg_type(), v6::MessageType::Solicit);
        assert_eq!(*timeout, 2 * INITIAL_SOLICIT_TIMEOUT);
        assert_matches!(
            &client.state,
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
            v6::DhcpOption::Iana(v6::IanaSerializer::new(0, 60, 60, &iana_options)),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, client.transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // The client should transition to Requesting when receiving any
        // advertise while retransmitting.
        let actions = client.handle_message_receive(msg);
        assert_matches!(actions[..], [Action::CancelTimer(ClientTimerType::Retransmission)]);
        assert_matches!(client.state, Some(ClientState::Requesting(Requesting {})));
    }

    const INFINITY: u32 = u32::MAX;
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
    fn test_receive_advertise_with_invalid_iana(t1: u32, t2: u32, ignore_iana: bool) {
        let client_id = v6::duid_uuid();
        let (client, _actions) = start_server_discovery(
            [0, 1, 2],
            client_id.clone(),
            to_configured_addresses(1, vec![std_ip_v6!("::ffff:c00a:1ff")]),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let ia = IdentityAssociation {
            address: std_ip_v6!("::ffff:c00a:1ff"),
            preferred_lifetime: Duration::from_secs(10),
            valid_lifetime: Duration::from_secs(20),
        };
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            ia.address,
            u32::try_from(ia.preferred_lifetime.as_secs()).expect("value should fit in u32"),
            u32::try_from(ia.valid_lifetime.as_secs()).expect("value should fit in u32"),
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&client_id),
            v6::DhcpOption::ServerId(&[1, 2, 3]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(0, t1, t2, &iana_options)),
        ];
        let ClientStateMachine { transaction_id, options_to_request, state, rng } = client;
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id.clone(), &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        let mut client = ClientStateMachine { transaction_id, options_to_request, state, rng };
        assert_matches!(client.handle_message_receive(msg)[..], []);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = client;
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
                assert_eq!(*addresses, HashMap::from([(0, ia)]));
            }
        }
    }

    #[test]
    fn test_unexpected_messages_are_ignored() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );
        client.transaction_id = [1, 2, 3];

        let builder = v6::MessageBuilder::new(
            v6::MessageType::Reply,
            // Transaction ID is different from the client's.
            [4, 5, 6],
            &[],
        );
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        assert!(client.handle_message_receive(msg).is_empty());

        // Messages with unsupported/unexpected types are discarded.
        for msg_type in std::array::IntoIter::new([
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
            let builder = v6::MessageBuilder::new(msg_type, client.transaction_id, &[]);
            let mut buf = vec![0; builder.bytes_len()];
            let () = builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

            assert!(client.handle_message_receive(msg).is_empty());
        }
    }

    #[test]
    fn test_unexpected_events_are_ignored() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // The client expects either a reply or retransmission timeout in the current state.
        assert_eq!(client.handle_timeout(ClientTimerType::Refresh)[..], []);
        assert_matches!(
            client.state,
            Some(ClientState::InformationRequesting(InformationRequesting {
                retrans_timeout: INITIAL_INFO_REQ_TIMEOUT
            }))
        );

        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, client.transaction_id, &[]);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
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
        assert_matches!(
            &client.state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers})) if dns_servers.is_empty()
        );

        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        // Extra replies received in information received state are ignored.
        assert_eq!(client.handle_message_receive(msg)[..], []);
        assert_matches!(
            &client.state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers})) if dns_servers.is_empty()
        );

        // Information received state should only respond to `Refresh` timer.
        assert_eq!(client.handle_timeout(ClientTimerType::Retransmission)[..], []);
        assert_matches!(
            &client.state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers})) if dns_servers.is_empty()
        );
    }

    #[test]
    fn test_information_request_retransmission() {
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
    fn test_information_request_refresh() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, client.transaction_id, &[]);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
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
        let builder = v6::MessageBuilder::new(
            v6::MessageType::InformationRequest,
            client.transaction_id,
            &[],
        );
        let mut want_buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut want_buf);
        assert_eq!(
            actions[..],
            [
                Action::SendMessage(want_buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_INFO_REQ_TIMEOUT)
            ]
        );
    }

    // NOTE: All comparisons are done on millisecond, so this test is not affected by precision
    // loss from floating point arithmetic.
    #[test]
    fn test_retransmission_timeout() {
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let initial_rt = Duration::from_secs(1);
        let max_rt = Duration::from_secs(100);

        // Start with initial timeout if previous timeout is zero.
        let t = retransmission_timeout(Duration::from_nanos(0), initial_rt, max_rt, &mut rng);
        assert_eq!(t.as_millis(), initial_rt.as_millis());

        // Use previous timeout when it's not zero and apply the formula.
        let t = retransmission_timeout(Duration::from_secs(10), initial_rt, max_rt, &mut rng);
        assert_eq!(t, Duration::from_secs(20));

        // Cap at max timeout.
        let t = retransmission_timeout(100 * max_rt, initial_rt, max_rt, &mut rng);
        assert_eq!(t.as_millis(), max_rt.as_millis());
        let t = retransmission_timeout(MAX_DURATION, initial_rt, max_rt, &mut rng);
        assert_eq!(t.as_millis(), max_rt.as_millis());
        // Zero max means no cap.
        let t = retransmission_timeout(100 * max_rt, initial_rt, Duration::from_nanos(0), &mut rng);
        assert_eq!(t.as_millis(), (200 * max_rt).as_millis());
        // Overflow durations are clipped.
        let t = retransmission_timeout(MAX_DURATION, initial_rt, Duration::from_nanos(0), &mut rng);
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
            let t = retransmission_timeout(*rt, initial_rt, max_rt, &mut rng);
            assert_eq!(t.as_millis(), *want_ms);
        });
    }
}

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
use tracing::{debug, info, warn};
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

/// Initial Rebind timeout `REB_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INITIAL_REBIND_TIMEOUT: Duration = Duration::from_secs(10);

/// Max Rebind timeout `REB_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const MAX_REBIND_TIMEOUT: Duration = Duration::from_secs(600);

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
    Rebind,
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

    /// A helper function that returns a transition to stay in `InformationRequesting`,
    /// with actions to send an information request and schedules retransmission.
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
        // Note that although RFC 8415 states that SOL_MAX_RT must be handled,
        // we never send Solicit messages when running in stateless mode, so
        // there is no point in storing or doing anything with it.
        let ProcessedOptions { server_id, solicit_max_rt_opt: _, result } =
            match process_options(&msg, ExchangeType::ReplyToInformationRequest, None) {
                Ok(processed_options) => processed_options,
                Err(e) => {
                    warn!("ignoring Reply to Information-Request: {}", e);
                    return Transition {
                        state: ClientState::InformationRequesting(self),
                        actions: Vec::new(),
                        transaction_id: None,
                    };
                }
            };

        let Options {
            success_status_message,
            next_contact_time,
            preference: _,
            non_temporary_addresses: _,
            dns_servers,
        } = match result {
            Ok(options) => options,
            Err(e) => {
                warn!(
                    "Reply to Information-Request from server {:?} error status code: {}",
                    server_id, e
                );
                return Transition {
                    state: ClientState::InformationRequesting(self),
                    actions: Vec::new(),
                    transaction_id: None,
                };
            }
        };

        // Per RFC 8415 section 21.23:
        //
        //    If the Reply to an Information-request message does not contain this
        //    option, the client MUST behave as if the option with the value
        //    IRT_DEFAULT was provided.
        let information_refresh_time = assert_matches!(
            next_contact_time,
            NextContactTime::InformationRefreshTime(option) => option
        )
        .map(|t| Duration::from_secs(t.into()))
        .unwrap_or(IRT_DEFAULT);

        if let Some(success_status_message) = success_status_message {
            if !success_status_message.is_empty() {
                info!(
                    "Reply to Information-Request from server {:?} \
                    contains success status code message: {}",
                    server_id, success_status_message,
                );
            }
        }

        let actions = [
            Action::CancelTimer(ClientTimerType::Retransmission),
            Action::ScheduleTimer(ClientTimerType::Refresh, information_refresh_time),
        ]
        .into_iter()
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

#[derive(Debug, PartialEq, Clone, Copy)]
pub(crate) struct IdentityAssociation {
    // TODO(https://fxbug.dev/86950): use UnicastAddr.
    address: Ipv6Addr,
    preferred_lifetime: v6::TimeValue,
    valid_lifetime: v6::TimeValue,
}

// Holds the information received in an Advertise message.
#[derive(Debug, Clone)]
struct AdvertiseMessage {
    server_id: Vec<u8>,
    non_temporary_addresses: HashMap<v6::IAID, IdentityAssociation>,
    dns_servers: Vec<Ipv6Addr>,
    preference: u8,
    receive_time: Instant,
    preferred_non_temporary_addresses_count: usize,
}

impl AdvertiseMessage {
    fn is_complete(
        &self,
        configured_non_temporary_addresses: &HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: &[v6::OptionCode],
    ) -> bool {
        let Self {
            server_id: _,
            non_temporary_addresses,
            dns_servers,
            preference: _,
            receive_time: _,
            preferred_non_temporary_addresses_count,
        } = self;
        non_temporary_addresses.len() >= configured_non_temporary_addresses.len()
            && *preferred_non_temporary_addresses_count
                == configured_non_temporary_addresses
                    .values()
                    .filter(|&value| value.is_some())
                    .count()
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
impl Ord for AdvertiseMessage {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        let Self {
            server_id: _,
            non_temporary_addresses,
            dns_servers,
            preference,
            receive_time,
            preferred_non_temporary_addresses_count,
        } = self;
        let Self {
            server_id: _,
            non_temporary_addresses: other_non_temporary_addresses,
            dns_servers: other_dns_server,
            preference: other_preference,
            receive_time: other_receive_time,
            preferred_non_temporary_addresses_count: other_preferred_non_temporary_addresses_count,
        } = other;
        (
            non_temporary_addresses.len(),
            *preferred_non_temporary_addresses_count,
            *preference,
            dns_servers.len(),
            *other_receive_time,
        )
            .cmp(&(
                other_non_temporary_addresses.len(),
                *other_preferred_non_temporary_addresses_count,
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

// Returns a count of entries in `configured_addresses` where the value is
// some address and the corresponding entry in `got_addresses` has the same
// address.
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
fn elapsed_time_in_centisecs(start_time: Instant, now: Instant) -> u16 {
    u16::try_from(
        now.duration_since(start_time)
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

#[derive(thiserror::Error, Debug)]
enum LifetimesError {
    #[error("valid lifetime is zero")]
    ValidLifetimeZero,
    #[error("preferred lifetime greater than valid lifetime: {0:?}")]
    PreferredLifetimeGreaterThanValidLifetime(Lifetimes),
}

#[derive(Debug)]
struct Lifetimes {
    preferred_lifetime: v6::TimeValue,
    valid_lifetime: v6::NonZeroTimeValue,
}

#[derive(Debug)]
struct IaAddress {
    address: Ipv6Addr,
    lifetimes: Result<Lifetimes, LifetimesError>,
}

#[derive(thiserror::Error, Debug)]
enum IaNaError {
    #[error("T1={t1:?} greater than T2={t2:?}")]
    T1GreaterThanT2 { t1: v6::TimeValue, t2: v6::TimeValue },
    #[error("unknown status code {0}")]
    InvalidStatusCode(u16),
    #[error("duplicate Status Code option {0:?} and {1:?}")]
    DuplicateStatusCode((v6::StatusCode, String), (v6::StatusCode, String)),
    // NB: Currently only one address is requested per IA_NA option, so
    // receiving an IA_NA option with multiple IA Address suboptions is
    // indicative of a misbehaving server.
    #[error("duplicate IA Address option {0:?} and {1:?}")]
    MultipleIaAddress(IaAddress, IaAddress),
    // TODO(https://fxbug.dev/104297): Use an owned option type rather
    // than a string of the debug representation of the invalid option.
    #[error("invalid option: {0:?}")]
    InvalidOption(String),
}

#[derive(Debug)]
enum IaNa {
    Success {
        status_message: Option<String>,
        t1: v6::TimeValue,
        t2: v6::TimeValue,
        ia_addr: Option<IaAddress>,
    },
    Failure(StatusCodeError),
}

// TODO(https://fxbug.dev/104519): Move this function and associated types
// into packet-formats-dhcp.
fn process_ia_na(ia_na_data: &v6::IanaData<&'_ [u8]>) -> Result<IaNa, IaNaError> {
    // Ignore invalid IANA options, per RFC 8415, section 21.4:
    //
    //    If a client receives an IA_NA with T1 greater than T2 and both T1
    //    and T2 are greater than 0, the client discards the IA_NA option
    //    and processes the remainder of the message as though the server
    //    had not included the invalid IA_NA option.
    let (t1, t2) = (ia_na_data.t1(), ia_na_data.t2());
    match (t1, t2) {
        (v6::TimeValue::Zero, _) | (_, v6::TimeValue::Zero) => {}
        (t1, t2) => {
            if t1 > t2 {
                return Err(IaNaError::T1GreaterThanT2 { t1, t2 });
            }
        }
    }

    let mut ia_addr_opt = None;
    let mut success_status_message = None;
    for ia_na_opt in ia_na_data.iter_options() {
        match ia_na_opt {
            v6::ParsedDhcpOption::StatusCode(code, msg) => {
                let status_code = code.get().try_into().map_err(|e| match e {
                    v6::ParseError::InvalidStatusCode(code) => IaNaError::InvalidStatusCode(code),
                    e => unreachable!("unreachable status code parse error: {}", e),
                })?;
                if let Some(existing) = success_status_message {
                    return Err(IaNaError::DuplicateStatusCode(
                        (v6::StatusCode::Success, existing),
                        (status_code, msg.to_string()),
                    ));
                }
                match status_code.into_result() {
                    Ok(()) => {
                        success_status_message = Some(msg.to_string());
                    }
                    Err(error_status_code) => {
                        return Ok(IaNa::Failure(StatusCodeError(
                            error_status_code,
                            msg.to_string(),
                        )))
                    }
                }
            }
            v6::ParsedDhcpOption::IaAddr(ia_addr_data) => {
                let lifetimes = match ia_addr_data.valid_lifetime() {
                    v6::TimeValue::Zero => Err(LifetimesError::ValidLifetimeZero),
                    vl @ v6::TimeValue::NonZero(valid_lifetime) => {
                        let preferred_lifetime = ia_addr_data.preferred_lifetime();
                        // Ignore invalid IA Address options, per RFC
                        // 8415, section 21.6:
                        //
                        //    The client MUST discard any addresses for
                        //    which the preferred lifetime is greater
                        //    than the valid lifetime.
                        if preferred_lifetime > vl {
                            Err(LifetimesError::PreferredLifetimeGreaterThanValidLifetime(
                                Lifetimes { preferred_lifetime, valid_lifetime },
                            ))
                        } else {
                            Ok(Lifetimes { preferred_lifetime, valid_lifetime })
                        }
                    }
                };
                let ia_addr = IaAddress { address: ia_addr_data.addr(), lifetimes };
                if let Some(existing) = ia_addr_opt {
                    return Err(IaNaError::MultipleIaAddress(existing, ia_addr));
                }
                ia_addr_opt = Some(ia_addr);
            }
            v6::ParsedDhcpOption::ClientId(_)
            | v6::ParsedDhcpOption::ServerId(_)
            | v6::ParsedDhcpOption::SolMaxRt(_)
            | v6::ParsedDhcpOption::Preference(_)
            | v6::ParsedDhcpOption::Iana(_)
            | v6::ParsedDhcpOption::InformationRefreshTime(_)
            | v6::ParsedDhcpOption::IaPd(_)
            | v6::ParsedDhcpOption::IaPrefix(_)
            | v6::ParsedDhcpOption::Oro(_)
            | v6::ParsedDhcpOption::ElapsedTime(_)
            | v6::ParsedDhcpOption::DnsServers(_)
            | v6::ParsedDhcpOption::DomainList(_) => {
                return Err(IaNaError::InvalidOption(format!("{:?}", ia_na_opt)));
            }
        }
    }
    // Missing status code option means success per RFC 8415 section 7.5:
    //
    //    If the Status Code option (see Section 21.13) does not appear
    //    in a message in which the option could appear, the status
    //    of the message is assumed to be Success.
    Ok(IaNa::Success { status_message: success_status_message, t1, t2, ia_addr: ia_addr_opt })
}

#[derive(Debug)]
enum NextContactTime {
    InformationRefreshTime(Option<u32>),
    RenewRebind { t1: v6::NonZeroTimeValue, t2: v6::NonZeroTimeValue },
}

#[derive(Debug)]
struct Options {
    success_status_message: Option<String>,
    next_contact_time: NextContactTime,
    preference: Option<u8>,
    non_temporary_addresses: HashMap<v6::IAID, IaNa>,
    dns_servers: Option<Vec<Ipv6Addr>>,
}

#[derive(Debug)]
struct ProcessedOptions {
    server_id: Vec<u8>,
    solicit_max_rt_opt: Option<u32>,
    result: Result<Options, StatusCodeError>,
}

#[derive(thiserror::Error, Debug)]
#[error("error status code={0}, message='{1}'")]
struct StatusCodeError(v6::ErrorStatusCode, String);

#[derive(thiserror::Error, Debug)]
enum OptionsError {
    // TODO(https://fxbug.dev/104297): Use an owned option type rather
    // than a string of the debug representation of the invalid option.
    #[error("duplicate option with code {0:?} {1} and {2}")]
    DuplicateOption(v6::OptionCode, String, String),
    #[error("unknown status code {0} with message '{1}'")]
    InvalidStatusCode(u16, String),
    #[error("IA_NA option error")]
    IaNaError(#[from] IaNaError),
    #[error("duplicate IA_NA option with IAID={0:?} {1:?} and {2:?}")]
    DuplicateIaNaId(v6::IAID, IaNa, IaNa),
    #[error("missing Server Id option")]
    MissingServerId,
    #[error("missing Client Id option")]
    MissingClientId,
    #[error("got Client ID option {got:?} but want {want:?}")]
    MismatchedClientId { got: Vec<u8>, want: [u8; CLIENT_ID_LEN] },
    #[error("unexpected Client ID in Reply to anonymous Information-Request: {0:?}")]
    UnexpectedClientId(Vec<u8>),
    // TODO(https://fxbug.dev/104297): Use an owned option type rather
    // than a string of the debug representation of the invalid option.
    #[error("invalid option found: {0:?}")]
    InvalidOption(String),
}

/// Message types sent by the client for which a Reply from the server
/// contains IA options with assigned leases.
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
enum RequestLeasesMessageType {
    Request,
    Renew,
    Rebind,
}

impl std::fmt::Display for RequestLeasesMessageType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Request => write!(f, "Request"),
            Self::Renew => write!(f, "Renew"),
            Self::Rebind => write!(f, "Rebind"),
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
enum ExchangeType {
    ReplyToInformationRequest,
    AdvertiseToSolicit,
    ReplyWithLeases(RequestLeasesMessageType),
}

// TODO(https://fxbug.dev/104025): Make the choice between ignoring invalid
// options and discarding the entire message configurable.
// TODO(https://fxbug.dev/104519): Move this function and associated types
// into packet-formats-dhcp.
/// Process options.
///
/// If any singleton options appears more than once, or there are multiple
/// IA options of the same type with duplicate ID's, the entire message will
/// be ignored as if it was never received.
///
/// Per RFC 8415, section 16:
///
///    This section describes which options are valid in which kinds of
///    message types and explains what to do when a client or server
///    receives a message that contains known options that are invalid for
///    that message. [...]
///
///    Clients and servers MAY choose to either (1) extract information from
///    such a message if the information is of use to the recipient or
///    (2) ignore such a message completely and just discard it.
///
/// The choice made by this function is (2): an error will be returned in such
/// cases to inform callers that they should ignore the entire message.
fn process_options<B: ByteSlice>(
    msg: &v6::Message<'_, B>,
    exchange_type: ExchangeType,
    want_client_id: Option<[u8; CLIENT_ID_LEN]>,
) -> Result<ProcessedOptions, OptionsError> {
    let mut solicit_max_rt_option = None;
    let mut server_id_option = None;
    let mut client_id_option = None;
    let mut preference = None;
    let mut non_temporary_addresses = HashMap::new();
    let mut status_code_option = None;
    let mut dns_servers = None;
    let mut refresh_time_option = None;
    let mut min_t1 = v6::TimeValue::Zero;
    let mut min_t2 = v6::TimeValue::Zero;
    let mut min_preferred_lifetime = v6::TimeValue::Zero;
    // Ok to initialize with Infinity, `get_nonzero_min` will pick a
    // smaller value once we see an IA with a valid lifetime less than
    // Infinity.
    let mut min_valid_lifetime = v6::NonZeroTimeValue::Infinity;

    struct AllowedOptions {
        preference: bool,
        information_refresh_time: bool,
        ia_na: bool,
    }
    // See RFC 8415 appendix B for a summary of which options are allowed in
    // which message types.
    let AllowedOptions {
        preference: preference_allowed,
        information_refresh_time: information_refresh_time_allowed,
        ia_na: ia_na_allowed,
    } = match exchange_type {
        ExchangeType::ReplyToInformationRequest => AllowedOptions {
            preference: false,
            information_refresh_time: true,
            // Per RFC 8415, section 16.12:
            //
            //    Servers MUST discard any received Information-request message that
            //    meets any of the following conditions:
            //
            //    -  the message includes an IA option.
            //
            // Since it's invalid to include IA options in an Information-request message,
            // it is also invalid to receive IA options in a Reply in response to an
            // Information-request message.
            ia_na: false,
        },
        ExchangeType::AdvertiseToSolicit => {
            AllowedOptions { preference: true, information_refresh_time: false, ia_na: true }
        }
        ExchangeType::ReplyWithLeases(
            RequestLeasesMessageType::Request
            | RequestLeasesMessageType::Renew
            | RequestLeasesMessageType::Rebind,
        ) => {
            AllowedOptions {
                preference: false,
                // Per RFC 8415, section 21.23
                //
                //    Information Refresh Time Option
                //
                //    [...] It is only used in Reply messages in response
                //    to Information-request messages.
                information_refresh_time: false,
                ia_na: true,
            }
        }
    };

    for opt in msg.options() {
        match opt {
            v6::ParsedDhcpOption::ClientId(client_id) => {
                if let Some(existing) = client_id_option {
                    return Err(OptionsError::DuplicateOption(
                        v6::OptionCode::ClientId,
                        format!("{:?}", existing),
                        format!("{:?}", client_id.to_vec()),
                    ));
                }
                client_id_option = Some(client_id.to_vec());
            }
            v6::ParsedDhcpOption::ServerId(server_id_opt) => {
                if let Some(existing) = server_id_option {
                    return Err(OptionsError::DuplicateOption(
                        v6::OptionCode::ServerId,
                        format!("{:?}", existing),
                        format!("{:?}", server_id_opt.to_vec()),
                    ));
                }
                server_id_option = Some(server_id_opt.to_vec());
            }
            v6::ParsedDhcpOption::SolMaxRt(sol_max_rt_opt) => {
                if let Some(existing) = solicit_max_rt_option {
                    return Err(OptionsError::DuplicateOption(
                        v6::OptionCode::SolMaxRt,
                        format!("{:?}", existing),
                        format!("{:?}", sol_max_rt_opt.get()),
                    ));
                }
                // Per RFC 8415, section 21.24:
                //
                //    SOL_MAX_RT value MUST be in this range: 60 <= "value" <= 86400
                //
                //    A DHCP client MUST ignore any SOL_MAX_RT option values that are
                //    less than 60 or more than 86400.
                if !VALID_MAX_SOLICIT_TIMEOUT_RANGE.contains(&sol_max_rt_opt.get()) {
                    warn!(
                        "{:?}: ignoring SOL_MAX_RT value {} outside of range {:?}",
                        exchange_type,
                        sol_max_rt_opt.get(),
                        VALID_MAX_SOLICIT_TIMEOUT_RANGE,
                    );
                } else {
                    // TODO(https://fxbug.dev/103407): Use a bounded type to
                    // store SOL_MAX_RT.
                    solicit_max_rt_option = Some(sol_max_rt_opt.get());
                }
            }
            v6::ParsedDhcpOption::Preference(preference_opt) => {
                if !preference_allowed {
                    return Err(OptionsError::InvalidOption(format!("{:?}", opt)));
                }
                if let Some(existing) = preference {
                    return Err(OptionsError::DuplicateOption(
                        v6::OptionCode::Preference,
                        format!("{:?}", existing),
                        format!("{:?}", preference_opt),
                    ));
                }
                preference = Some(preference_opt);
            }
            v6::ParsedDhcpOption::Iana(ref iana_data) => {
                if !ia_na_allowed {
                    return Err(OptionsError::InvalidOption(format!("{:?}", opt)));
                }
                let iaid = v6::IAID::new(iana_data.iaid());
                let processed_ia_na = process_ia_na(iana_data)?;
                match processed_ia_na {
                    IaNa::Failure(_) => {}
                    IaNa::Success { status_message: _, t1, t2, ref ia_addr } => {
                        match ia_addr {
                            None | Some(IaAddress { address: _, lifetimes: Err(_) }) => {}
                            Some(IaAddress {
                                address: _,
                                lifetimes: Ok(Lifetimes { preferred_lifetime, valid_lifetime }),
                            }) => {
                                min_preferred_lifetime = maybe_get_nonzero_min(
                                    min_preferred_lifetime,
                                    *preferred_lifetime,
                                );
                                min_valid_lifetime =
                                    std::cmp::min(min_valid_lifetime, *valid_lifetime);
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
                                //
                                // Only IAs that with success status are included in the earliest
                                // T1/T2 calculation.
                                min_t1 = maybe_get_nonzero_min(min_t1, t1);
                                min_t2 = maybe_get_nonzero_min(min_t2, t2);
                            }
                        }
                    }
                }
                // Per RFC 8415, section 21.4, IAIDs are expected to be
                // unique.
                //
                //    A DHCP message may contain multiple IA_NA options
                //    (though each must have a unique IAID).
                match non_temporary_addresses.entry(iaid) {
                    Entry::Occupied(entry) => {
                        return Err(OptionsError::DuplicateIaNaId(
                            iaid,
                            entry.remove(),
                            processed_ia_na,
                        ));
                    }
                    Entry::Vacant(entry) => {
                        let _: &mut IaNa = entry.insert(processed_ia_na);
                    }
                };
            }
            v6::ParsedDhcpOption::StatusCode(code, message) => {
                let status_code = match v6::StatusCode::try_from(code.get()) {
                    Ok(status_code) => status_code,
                    Err(v6::ParseError::InvalidStatusCode(invalid)) => {
                        return Err(OptionsError::InvalidStatusCode(invalid, message.to_string()));
                    }
                    Err(e) => {
                        unreachable!("unreachable status code parse error: {}", e);
                    }
                };
                if let Some(existing) = status_code_option {
                    return Err(OptionsError::DuplicateOption(
                        v6::OptionCode::StatusCode,
                        format!("{:?}", existing),
                        format!("{:?}", (status_code, message.to_string())),
                    ));
                }
                status_code_option = Some((status_code, message.to_string()));
            }
            v6::ParsedDhcpOption::IaPd(_) => {
                // TODO(https://fxbug.dev/80595): Implement Prefix delegation.
            }
            v6::ParsedDhcpOption::InformationRefreshTime(information_refresh_time) => {
                if !information_refresh_time_allowed {
                    return Err(OptionsError::InvalidOption(format!("{:?}", opt)));
                }
                if let Some(existing) = refresh_time_option {
                    return Err(OptionsError::DuplicateOption(
                        v6::OptionCode::InformationRefreshTime,
                        format!("{:?}", existing),
                        format!("{:?}", information_refresh_time),
                    ));
                }
                refresh_time_option = Some(information_refresh_time);
            }
            v6::ParsedDhcpOption::IaAddr(_)
            | v6::ParsedDhcpOption::IaPrefix(_)
            | v6::ParsedDhcpOption::Oro(_)
            | v6::ParsedDhcpOption::ElapsedTime(_) => {
                return Err(OptionsError::InvalidOption(format!("{:?}", opt)));
            }
            v6::ParsedDhcpOption::DnsServers(server_addrs) => {
                if let Some(existing) = dns_servers {
                    return Err(OptionsError::DuplicateOption(
                        v6::OptionCode::DnsServers,
                        format!("{:?}", existing),
                        format!("{:?}", server_addrs),
                    ));
                }
                dns_servers = Some(server_addrs);
            }
            v6::ParsedDhcpOption::DomainList(_domains) => {
                // TODO(https://fxbug.dev/87176) implement domain list.
            }
        }
    }
    // For all three message types the server sends to the client (Advertise, Reply,
    // and Reconfigue), RFC 8415 sections 16.3, 16.10, and 16.11 respectively state
    // that:
    //
    //    Clients MUST discard any received ... message that meets
    //    any of the following conditions:
    //    -  the message does not include a Server Identifier option (see
    //       Section 21.3).
    let server_id = server_id_option.ok_or(OptionsError::MissingServerId)?;
    // For all three message types the server sends to the client (Advertise, Reply,
    // and Reconfigue), RFC 8415 sections 16.3, 16.10, and 16.11 respectively state
    // that:
    //
    //    Clients MUST discard any received ... message that meets
    //    any of the following conditions:
    //    -  the message does not include a Client Identifier option (see
    //       Section 21.2).
    //    -  the contents of the Client Identifier option do not match the
    //       client's DUID.
    //
    // The exception is that clients may send Information-Request messages
    // without a client ID per RFC 8415 section 18.2.6:
    //
    //    The client SHOULD include a Client Identifier option (see
    //    Section 21.2) to identify itself to the server (however, see
    //    Section 4.3.1 of [RFC7844] for reasons why a client may not want to
    //    include this option).
    match (client_id_option, want_client_id) {
        (None, None) => {}
        (Some(got), None) => return Err(OptionsError::UnexpectedClientId(got)),
        (None, Some::<[u8; CLIENT_ID_LEN]>(_)) => return Err(OptionsError::MissingClientId),
        (Some(got), Some(want)) => {
            if got != want {
                return Err(OptionsError::MismatchedClientId { want, got });
            }
        }
    }
    let success_status_message = match status_code_option {
        Some((status_code, message)) => match status_code.into_result() {
            Ok(()) => Some(message),
            Err(error_code) => {
                return Ok(ProcessedOptions {
                    server_id,
                    solicit_max_rt_opt: solicit_max_rt_option,
                    result: Err(StatusCodeError(error_code, message)),
                });
            }
        },
        // Missing status code option means success per RFC 8415 section 7.5:
        //
        //    If the Status Code option (see Section 21.13) does not appear
        //    in a message in which the option could appear, the status
        //    of the message is assumed to be Success.
        None => None,
    };
    let next_contact_time = match exchange_type {
        ExchangeType::ReplyToInformationRequest => {
            NextContactTime::InformationRefreshTime(refresh_time_option)
        }
        ExchangeType::AdvertiseToSolicit
        | ExchangeType::ReplyWithLeases(
            RequestLeasesMessageType::Request
            | RequestLeasesMessageType::Renew
            | RequestLeasesMessageType::Rebind,
        ) => {
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
            let t1 = match min_t1 {
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
            let t2 = match min_t2 {
                v6::TimeValue::Zero => compute_t(t1, T2_T1_RATIO),
                v6::TimeValue::NonZero(t2_val) => {
                    if t2_val < t1 {
                        compute_t(t1, T2_T1_RATIO)
                    } else {
                        t2_val
                    }
                }
            };

            NextContactTime::RenewRebind { t1, t2 }
        }
    };
    Ok(ProcessedOptions {
        server_id,
        solicit_max_rt_opt: solicit_max_rt_option,
        result: Ok(Options {
            success_status_message,
            next_contact_time,
            preference,
            non_temporary_addresses,
            dns_servers,
        }),
    })
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
    /// The non-temporary addresses the client is configured to negotiate.
    configured_non_temporary_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
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
        configured_non_temporary_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: &[v6::OptionCode],
        solicit_max_rt: Duration,
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        Self {
            client_id,
            configured_non_temporary_addresses,
            first_solicit_time: None,
            retrans_timeout: Duration::default(),
            solicit_max_rt,
            collected_advertise: BinaryHeap::new(),
            collected_sol_max_rt: Vec::new(),
        }
        .send_and_schedule_retransmission(transaction_id, options_to_request, rng, now)
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

    /// Returns a transition to stay in `ServerDiscovery`, with actions to send a
    /// solicit and schedule retransmission.
    fn send_and_schedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        let Self {
            client_id,
            configured_non_temporary_addresses,
            first_solicit_time,
            retrans_timeout,
            solicit_max_rt,
            collected_advertise,
            collected_sol_max_rt,
        } = self;
        let mut options = vec![v6::DhcpOption::ClientId(&client_id)];

        let (start_time, elapsed_time) = match first_solicit_time {
            None => (now, 0),
            Some(start_time) => (start_time, elapsed_time_in_centisecs(start_time, now)),
        };
        options.push(v6::DhcpOption::ElapsedTime(elapsed_time));

        // TODO(https://fxbug.dev/86945): remove `address_hint` construction
        // once `IanaSerializer::new()` takes options by value.
        let mut address_hint = HashMap::new();
        for (iaid, addr_opt) in &configured_non_temporary_addresses {
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
                configured_non_temporary_addresses,
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
        now: Instant,
    ) -> Transition {
        let Self {
            client_id,
            configured_non_temporary_addresses,
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
            let AdvertiseMessage {
                server_id,
                non_temporary_addresses: advertised_non_temporary_addresses,
                dns_servers: _,
                preference: _,
                receive_time: _,
                preferred_non_temporary_addresses_count: _,
            } = advertise;
            return Requesting::start(
                client_id,
                server_id,
                advertise_to_address_entries(
                    advertised_non_temporary_addresses,
                    configured_non_temporary_addresses,
                ),
                &options_to_request,
                collected_advertise,
                solicit_max_rt,
                rng,
                now,
            );
        }

        ServerDiscovery {
            client_id,
            configured_non_temporary_addresses,
            first_solicit_time,
            retrans_timeout,
            solicit_max_rt,
            collected_advertise,
            collected_sol_max_rt,
        }
        .send_and_schedule_retransmission(transaction_id, options_to_request, rng, now)
    }

    fn advertise_message_received<R: Rng, B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        msg: v6::Message<'_, B>,
        now: Instant,
    ) -> Transition {
        let Self {
            client_id,
            configured_non_temporary_addresses,
            first_solicit_time,
            retrans_timeout,
            solicit_max_rt,
            collected_advertise,
            collected_sol_max_rt,
        } = self;

        let ProcessedOptions { server_id, solicit_max_rt_opt, result } =
            match process_options(&msg, ExchangeType::AdvertiseToSolicit, Some(client_id)) {
                Ok(processed_options) => processed_options,
                Err(e) => {
                    warn!("ignoring Advertise: {}", e);
                    return Transition {
                        state: ClientState::ServerDiscovery(ServerDiscovery {
                            client_id,
                            configured_non_temporary_addresses,
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
            };

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
        let mut collected_sol_max_rt = collected_sol_max_rt;
        if let Some(solicit_max_rt) = solicit_max_rt_opt {
            collected_sol_max_rt.push(solicit_max_rt);
        }
        let Options {
            success_status_message,
            next_contact_time: _,
            preference,
            mut non_temporary_addresses,
            dns_servers,
        } = match result {
            Ok(options) => options,
            Err(e) => {
                warn!("Advertise from server {:?} error status code: {}", server_id, e);
                return Transition {
                    state: ClientState::ServerDiscovery(ServerDiscovery {
                        client_id,
                        configured_non_temporary_addresses,
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
        };
        if let Some(success_status_message) = success_status_message {
            if !success_status_message.is_empty() {
                info!(
                    "Advertise from server {:?} contains success status code message: {}",
                    server_id, success_status_message,
                );
            }
        }
        let non_temporary_addresses = non_temporary_addresses
            .drain()
            .filter_map(|(iaid, ia_na)| {
                let (success_status_message, ia_addr) = match ia_na {
                    IaNa::Success { status_message, t1: _, t2: _, ia_addr } => {
                        (status_message, ia_addr)
                    }
                    IaNa::Failure(e) => {
                        warn!(
                            "Advertise from server {:?} contains IA_NA with error status code: {}",
                            server_id, e
                        );
                        return None;
                    }
                };
                if let Some(success_status_message) = success_status_message {
                    if !success_status_message.is_empty() {
                        info!(
                            "Advertise from server {:?} IA_NA with IAID {:?} \
                            success status code message: {}",
                            server_id, iaid, success_status_message,
                        );
                    }
                }
                ia_addr.and_then(|IaAddress { address, lifetimes }| match lifetimes {
                    Ok(Lifetimes { preferred_lifetime, valid_lifetime }) => Some((
                        iaid,
                        IdentityAssociation {
                            address,
                            preferred_lifetime,
                            valid_lifetime: v6::TimeValue::NonZero(valid_lifetime),
                        },
                    )),
                    Err(e) => {
                        warn!(
                            "Advertise from server {:?}: \
                            ignoring IA_NA with IAID {:?} because of invalid lifetimes: {}",
                            server_id, iaid, e
                        );
                        None
                    }
                })
            })
            .collect::<HashMap<_, _>>();
        if non_temporary_addresses.is_empty() {
            return Transition {
                state: ClientState::ServerDiscovery(ServerDiscovery {
                    client_id,
                    configured_non_temporary_addresses,
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

        let preferred_non_temporary_addresses_count = compute_preferred_address_count(
            &non_temporary_addresses,
            &configured_non_temporary_addresses,
        );
        let advertise = AdvertiseMessage {
            server_id,
            non_temporary_addresses,
            dns_servers: dns_servers.unwrap_or(Vec::new()),
            // Per RFC 8415, section 18.2.1:
            //
            //   Any valid Advertise that does not include a Preference
            //   option is considered to have a preference value of 0.
            preference: preference.unwrap_or(0),
            receive_time: now,
            preferred_non_temporary_addresses_count,
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
            && advertise.is_complete(&configured_non_temporary_addresses, options_to_request))
            || is_retransmitting
        {
            let solicit_max_rt = get_common_value(&collected_sol_max_rt).unwrap_or(solicit_max_rt);
            let AdvertiseMessage {
                server_id,
                non_temporary_addresses: advertised_non_temporary_addresses,
                dns_servers: _,
                preference: _,
                receive_time: _,
                preferred_non_temporary_addresses_count: _,
            } = advertise;
            return Requesting::start(
                client_id,
                server_id,
                advertise_to_address_entries(
                    advertised_non_temporary_addresses,
                    configured_non_temporary_addresses,
                ),
                &options_to_request,
                collected_advertise,
                solicit_max_rt,
                rng,
                now,
            );
        }

        let mut collected_advertise = collected_advertise;
        collected_advertise.push(advertise);
        Transition {
            state: ClientState::ServerDiscovery(ServerDiscovery {
                client_id,
                configured_non_temporary_addresses,
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
    #[derive(Debug, PartialEq, Clone, Copy)]
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
    #[derive(Debug, PartialEq, Clone, Copy)]
    pub(super) struct NonConfiguredIa {
        ia: IdentityAssociation,
        configured_address: Option<Ipv6Addr>,
    }

    impl NonConfiguredIa {
        /// Creates a `NonConfiguredIa`. Returns `None` if the address within
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

        /// Returns the configured address.
        pub(super) fn configured_address(&self) -> Option<Ipv6Addr> {
            let Self { ia: _, configured_address } = self;
            *configured_address
        }
    }
}

use private::*;

/// Represents an address to request in an IA, relative to the configured
/// address for that IA.
#[derive(Debug, PartialEq, Clone, Copy)]
enum AddressToRequest {
    /// The address to request is the same as the configured address.
    Configured(Option<Ipv6Addr>),
    /// The address to request is different than the configured address.
    NonConfigured(NonConfiguredAddress),
}

impl AddressToRequest {
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
    /// The non-temporary addresses entries negotiated by the client.
    non_temporary_addresses: HashMap<v6::IAID, AddressEntry>,
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

/// Helper function to send a request to an alternate server, or if there are no
/// other collected servers, restart server discovery.
///
/// The client removes currently assigned addresses, per RFC 8415, section
/// 18.2.10.1:
///
///    Whenever a client restarts the DHCP server discovery process or
///    selects an alternate server as described in Section 18.2.9, the client
///    SHOULD stop using all the addresses and delegated prefixes for which
///    it has bindings and try to obtain all required leases from the new
///    server.
fn request_from_alternate_server_or_restart_server_discovery<R: Rng>(
    client_id: [u8; CLIENT_ID_LEN],
    non_temporary_addresses: HashMap<v6::IAID, AddressEntry>,
    options_to_request: &[v6::OptionCode],
    mut collected_advertise: BinaryHeap<AdvertiseMessage>,
    solicit_max_rt: Duration,
    rng: &mut R,
    now: Instant,
) -> Transition {
    let configured_non_temporary_addresses = to_configured_addresses(non_temporary_addresses);
    if let Some(advertise) = collected_advertise.pop() {
        // TODO(https://fxbug.dev/96674): Before selecting a different server,
        // add actions to remove the existing assigned addresses, if any.
        let AdvertiseMessage {
            server_id,
            non_temporary_addresses: advertised_non_temporary_addresses,
            dns_servers: _,
            preference: _,
            receive_time: _,
            preferred_non_temporary_addresses_count: _,
        } = advertise;
        return Requesting::start(
            client_id,
            server_id,
            advertise_to_address_entries(
                advertised_non_temporary_addresses,
                configured_non_temporary_addresses,
            ),
            &options_to_request,
            collected_advertise,
            solicit_max_rt,
            rng,
            now,
        );
    }
    // TODO(https://fxbug.dev/96674): Before restarting server discovery, add
    // actions to remove the existing assigned addresses, if any.
    return ServerDiscovery::start(
        transaction_id(),
        client_id,
        configured_non_temporary_addresses,
        &options_to_request,
        solicit_max_rt,
        rng,
        now,
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

fn discard_leases(address_entry: &AddressEntry) {
    match address_entry {
        AddressEntry::Assigned(_) => {
            // TODO(https://fxbug.dev/96674): Add action to remove the address.
            // TODO(https://fxbug.dev/96684): add actions to cancel preferred
            // and valid lifetime timers.
        }
        AddressEntry::ToRequest(_) => {
            // If the address was not previously assigned, no additional
            // action is needed.
        }
    }
}

#[derive(Debug, thiserror::Error)]
enum ReplyWithLeasesError {
    #[error("option processing error")]
    OptionsError(#[from] OptionsError),
    #[error("mismatched Server ID, got {got:?} want {want:?}")]
    MismatchedServerId { got: Vec<u8>, want: Vec<u8> },
    #[error("status code error")]
    StatusCodeError(#[from] StatusCodeError),
    #[error("IA_NA with unexpected IAID")]
    UnexpectedIaNa(v6::IAID, IaNa),
}

#[derive(Debug, Copy, Clone)]
enum IaNaStatusError {
    Retry { without_hints: bool },
    Invalid,
    Rerequest,
}

fn process_ia_na_error_status(
    request_type: RequestLeasesMessageType,
    error_status: v6::ErrorStatusCode,
) -> IaNaStatusError {
    match (request_type, error_status) {
        // Per RFC 8415, section 18.3.2:
        //
        //    If any of the prefixes of the included addresses are not
        //    appropriate for the link to which the client is connected,
        //    the server MUST return the IA to the client with a Status Code
        //    option (see Section 21.13) with the value NotOnLink.
        //
        // If the client receives IAs with NotOnLink status, try to obtain
        // other addresses in follow-up messages.
        (RequestLeasesMessageType::Request, v6::ErrorStatusCode::NotOnLink) => {
            IaNaStatusError::Retry { without_hints: true }
        }
        // NotOnLink is not expected in Reply to Renew/Rebind. The server
        // indicates that the IA is not appropriate for the link by setting
        // lifetime 0, not by using NotOnLink status.
        //
        // For Renewing, per RFC 8415 section 18.3.4:
        //
        //    If the server finds that any of the addresses in the IA are
        //    not appropriate for the link to which the client is attached,
        //    the server returns the address to the client with lifetimes of 0.
        //
        // For Rebinding, per RFC 8415 section 18.3.6:
        //
        //    If the server finds that the client entry for the IA and any of
        //    the addresses or delegated prefixes are no longer appropriate for
        //    the link to which the client's interface is attached according to
        //    the server's explicit configuration information, the server
        //    returns those addresses or delegated prefixes to the client with
        //    lifetimes of 0.
        (
            RequestLeasesMessageType::Renew | RequestLeasesMessageType::Rebind,
            v6::ErrorStatusCode::NotOnLink,
        ) => IaNaStatusError::Invalid,

        // Per RFC 18.2.10,
        //
        //   If the client receives a Reply message with a status code of
        //   UnspecFail, the server is indicating that it was unable to process
        //   the client's message due to an unspecified failure condition. If
        //   the client retransmits the original message to the same server to
        //   retry the desired operation, the client MUST limit the rate at
        //   which it retransmits the message and limit the duration of the time
        //   during which it retransmits the message (see Section 14.1).
        //
        // When responding to Request messages, per section 18.3.2:
        //
        //    If the server [..] cannot assign any IP addresses to an IA,
        //    the server MUST return the IA option in the Reply message with
        //    no addresses in the IA and a Status Code option containing
        //    status code NoAddrsAvail in the IA.
        //
        // When responding to Renew messages, per section 18.3.4:
        //
        //    -  If the server is configured to create new bindings as
        //    a result of processing Renew messages but the server will
        //    not assign any leases to an IA, the server returns the IA
        //    option containing a Status Code option with the NoAddrsAvail.
        //
        // When responding to Rebind messages, per section 18.3.5:
        //
        //    -  If the server is configured to create new bindings as a result
        //    of processing Rebind messages but the server will not assign any
        //    leases to an IA, the server returns the IA option containing a
        //    Status Code option (see Section 21.13) with the NoAddrsAvail or
        //    NoPrefixAvail status code and a status message for a user.
        //
        // Retry obtaining this IA_NA in subsequent messages.
        //
        // TODO(https://fxbug.dev/81086): implement rate limiting.
        (
            RequestLeasesMessageType::Request
            | RequestLeasesMessageType::Renew
            | RequestLeasesMessageType::Rebind,
            v6::ErrorStatusCode::NoAddrsAvail | v6::ErrorStatusCode::UnspecFail,
        ) => IaNaStatusError::Retry { without_hints: false },
        // We do not support prefix delegation yet so we don't send any messages
        // requesting/renewing/rebinding prefixes.
        //
        // TODO(https://fxbug.dev/80595): Do not consider this invalid once we
        // support PD.
        (
            RequestLeasesMessageType::Request
            | RequestLeasesMessageType::Renew
            | RequestLeasesMessageType::Rebind,
            v6::ErrorStatusCode::NoPrefixAvail,
        ) => IaNaStatusError::Invalid,

        // Per RFC 8415 section 18.2.10.1:
        //
        //    When the client receives a Reply message in response to a Renew or
        //    Rebind message, the client:
        //
        //    -  Sends a Request message to the server that responded if any of
        //    the IAs in the Reply message contain the NoBinding status code.
        //    The client places IA options in this message for all IAs.  The
        //    client continues to use other bindings for which the server did
        //    not return an error.
        //
        // The client removes the IA not found by the server, and transitions to
        // Requesting after processing all the received IAs.
        (
            RequestLeasesMessageType::Renew | RequestLeasesMessageType::Rebind,
            v6::ErrorStatusCode::NoBinding,
        ) => IaNaStatusError::Rerequest,
        (RequestLeasesMessageType::Request, v6::ErrorStatusCode::NoBinding) => {
            IaNaStatusError::Invalid
        }

        // Per RFC 8415 section 18.2.10,
        //
        //   If the client receives a Reply message with a status code of
        //   UseMulticast, the client records the receipt of the message and
        //   sends subsequent messages to the server through the interface on
        //   which the message was received using multicast. The client resends
        //   the original message using multicast.
        //
        // We currently always multicast our messages so we do not expect the
        // UseMulticast error.
        //
        // TODO(https://fxbug.dev/76764): Do not consider this an invalid error
        // when unicasting messages.
        (
            RequestLeasesMessageType::Request | RequestLeasesMessageType::Renew,
            v6::ErrorStatusCode::UseMulticast,
        ) => IaNaStatusError::Invalid,
        // Per RFC 8415 section 16,
        //
        //   A server MUST discard any Solicit, Confirm, Rebind, or
        //   Information-request messages it receives with a Layer 3 unicast
        //   destination address.
        //
        // Since we must never unicast Rebind messages, we always multicast them
        // so we consider a UseMulticast error invalid.
        (RequestLeasesMessageType::Rebind, v6::ErrorStatusCode::UseMulticast) => {
            IaNaStatusError::Invalid
        }
    }
}

// Possible states to move to after processing a Reply containing leases.
#[derive(Debug)]
enum StateAfterReplyWithLeases {
    RequestNextServer,
    Assigned,
    StayRenewingRebinding,
    Requesting,
}

#[derive(Debug)]
struct ProcessedReplyWithLeases {
    server_id: Vec<u8>,
    non_temporary_addresses: HashMap<v6::IAID, AddressEntry>,
    dns_servers: Option<Vec<Ipv6Addr>>,
    actions: Vec<Action>,
    next_state: StateAfterReplyWithLeases,
}

// Processes a Reply to Solicit (with fast commit), Request, Renew, or Rebind.
//
// If an error is returned, the message should be ignored.
fn process_reply_with_leases<B: ByteSlice>(
    client_id: [u8; CLIENT_ID_LEN],
    server_id: &[u8],
    current_non_temporary_addresses: &HashMap<v6::IAID, AddressEntry>,
    solicit_max_rt: &mut Duration,
    msg: &v6::Message<'_, B>,
    request_type: RequestLeasesMessageType,
) -> Result<ProcessedReplyWithLeases, ReplyWithLeasesError> {
    let ProcessedOptions { server_id: got_server_id, solicit_max_rt_opt, result } =
        process_options(&msg, ExchangeType::ReplyWithLeases(request_type), Some(client_id))?;

    match request_type {
        RequestLeasesMessageType::Request | RequestLeasesMessageType::Renew => {
            if got_server_id != server_id {
                return Err(ReplyWithLeasesError::MismatchedServerId {
                    got: got_server_id,
                    want: server_id.to_vec(),
                });
            }
        }
        // Accept a message from any server if this is a reply to a rebind
        // message.
        RequestLeasesMessageType::Rebind => {}
    }

    // Always update SOL_MAX_RT, per RFC 8415, section 18.2.10:
    //
    //    The client MUST process any SOL_MAX_RT option (see Section 21.24)
    //    and INF_MAX_RT option (see Section
    //    21.25) present in a Reply message, even if the message contains a
    //    Status Code option indicating a failure.
    *solicit_max_rt = solicit_max_rt_opt
        .map_or(*solicit_max_rt, |solicit_max_rt| Duration::from_secs(solicit_max_rt.into()));

    let Options {
        success_status_message,
        next_contact_time,
        preference: _,
        non_temporary_addresses,
        dns_servers,
    } = result?;

    let (t1, t2) = assert_matches!(
        next_contact_time,
        NextContactTime::RenewRebind { t1, t2 } => (t1, t2)
    );

    if let Some(success_status_message) = success_status_message {
        if !success_status_message.is_empty() {
            info!(
                "Reply to {} success status code message: {}",
                request_type, success_status_message
            );
        }
    }

    let (mut non_temporary_addresses, go_to_requesting) =
        non_temporary_addresses.into_iter().try_fold(
            (HashMap::new(), false),
            |(mut non_temporary_addresses, mut go_to_requesting), (iaid, ia_na)| {
                let current_address_entry = match current_non_temporary_addresses.get(&iaid) {
                    Some(address_entry) => address_entry,
                    None => {
                        // The RFC does not explicitly call out what to do with
                        // IAs that were not requested by the client.
                        //
                        // Return an error to cause the entire message to be
                        // ignored.
                        return Err(ReplyWithLeasesError::UnexpectedIaNa(iaid, ia_na));
                    }
                };
                let (success_status_message, ia_addr) = match ia_na {
                    IaNa::Success { status_message, t1: _, t2: _, ia_addr } => {
                        (status_message, ia_addr)
                    }
                    IaNa::Failure(StatusCodeError(error_code, msg)) => {
                        if !msg.is_empty() {
                            warn!(
                                "Reply to {}: IA_NA with IAID {:?} \
                            status code {:?} message: {}",
                                request_type, iaid, error_code, msg
                            );
                        }
                        discard_leases(&current_address_entry);
                        let error = process_ia_na_error_status(request_type, error_code);
                        let without_hints = match error {
                            IaNaStatusError::Retry { without_hints } => without_hints,
                            IaNaStatusError::Invalid => {
                                warn!(
                                "Reply to {}: received unexpected status code {:?} in IA_NA option \
                                with IAID {:?}",
                                request_type, error_code, iaid,
                            );
                                false
                            }
                            IaNaStatusError::Rerequest => {
                                go_to_requesting = true;
                                false
                            }
                        };
                        assert_eq!(
                            non_temporary_addresses
                                .insert(iaid, current_address_entry.to_request(without_hints)),
                            None
                        );
                        return Ok((non_temporary_addresses, go_to_requesting));
                    }
                };
                if let Some(success_status_message) = success_status_message {
                    if !success_status_message.is_empty() {
                        info!(
                            "Reply to {}: IA_NA with IAID {:?} success status code message: {}",
                            request_type, iaid, success_status_message,
                        );
                    }
                }
                let IaAddress { address, lifetimes } = match ia_addr {
                    Some(ia_addr) => ia_addr,
                    None => {
                        // The server has not included an IA Address option in the IA,
                        // keep the previously recorded information, per RFC 8415
                        // section 18.2.10.1:
                        //
                        //     -  Leave unchanged any information about leases the
                        //        client has recorded in the IA but that were not
                        //        included in the IA from the server.
                        //
                        // The address remains assigned until the end of its valid
                        // lifetime, or it is requested later if it was not assigned.
                        assert_eq!(
                            non_temporary_addresses.insert(iaid, *current_address_entry),
                            None
                        );
                        return Ok((non_temporary_addresses, go_to_requesting));
                    }
                };
                let Lifetimes { preferred_lifetime, valid_lifetime } = match lifetimes {
                    Ok(lifetimes) => lifetimes,
                    Err(e) => {
                        warn!(
                            "Reply to {}: IA_NA with IAID {:?}: discarding leases: {}",
                            request_type, iaid, e
                        );
                        discard_leases(&current_address_entry);
                        assert_eq!(
                            non_temporary_addresses.insert(
                                iaid,
                                AddressEntry::ToRequest(AddressToRequest::Configured(
                                    current_address_entry.configured_address(),
                                )),
                            ),
                            None
                        );
                        return Ok((non_temporary_addresses, go_to_requesting));
                    }
                };
                // Per RFC 8415 section 21.13:
                //
                //    If the server finds that the client has included an
                //    IA in the Request message for which the server already
                //    has a binding that associates the IA with the client,
                //    the server sends a Reply message with existing bindings,
                //    possibly with updated lifetimes.  The server may update
                //    the bindings according to its local policies.
                match current_address_entry {
                    AddressEntry::Assigned(ia) => {
                        // If the returned address does not match the address recorded by the client
                        // remove old address and add new one; otherwise, extend the lifetimes of
                        // the existing address.
                        if address != ia.address() {
                            // TODO(https://fxbug.dev/96674): Add
                            // action to remove the previous address.
                            // TODO(https://fxbug.dev/95265): Add action to add
                            // the new address.
                            // TODO(https://fxbug.dev/96684): Add actions to
                            // schedule preferred and valid lifetime timers for
                            // new address and cancel timers for old address.
                            debug!(
                                "Reply to {}: IA_NA with IAID {:?}: \
                            Address does not match {:?}, removing previous address and \
                            adding new address {:?}.",
                                request_type,
                                iaid,
                                ia.address(),
                                address,
                            );
                        } else {
                            // The lifetime was extended, update preferred
                            // and valid lifetime timers.
                            // TODO(https://fxbug.dev/96684): add actions to
                            // reschedule preferred and valid lifetime timers.
                            debug!(
                                "Reply to {}: IA_NA with IAID {:?}: \
                            Lifetime is extended for address {:?}.",
                                request_type,
                                iaid,
                                ia.address()
                            );
                        }
                    }
                    AddressEntry::ToRequest(_) => {
                        // TODO(https://fxbug.dev/95265): Add action to
                        // add the new address.
                    }
                }
                // Add the address entry as renewed by the
                // server.
                let entry = AddressEntry::Assigned(AssignedIa::new(
                    IdentityAssociation {
                        address,
                        preferred_lifetime,
                        valid_lifetime: v6::TimeValue::NonZero(valid_lifetime),
                    },
                    current_address_entry.configured_address(),
                ));
                assert_eq!(non_temporary_addresses.insert(iaid, entry), None);
                Ok((non_temporary_addresses, go_to_requesting))
            },
        )?;

    // Per RFC 8415, section 18.2.10.1:
    //
    //    If the Reply message contains any IAs but the client finds no
    //    usable addresses and/or delegated prefixes in any of these IAs,
    //    the client may either try another server (perhaps restarting the
    //    DHCP server discovery process) or use the Information-request
    //    message to obtain other configuration information only.
    //
    // If there are no usable addresses and no other servers to select,
    // the client restarts server discovery instead of requesting
    // configuration information only. This option is preferred when the
    // client operates in stateful mode, where the main goal for the
    // client is to negotiate addresses.
    let next_state = if non_temporary_addresses.iter().all(|(_iaid, entry)| match entry {
        AddressEntry::ToRequest(_) => true,
        AddressEntry::Assigned(_) => false,
    }) {
        warn!("Reply to {}: no usable lease returned", request_type);
        StateAfterReplyWithLeases::RequestNextServer
    } else if go_to_requesting {
        StateAfterReplyWithLeases::Requesting
    } else {
        match request_type {
            RequestLeasesMessageType::Request => StateAfterReplyWithLeases::Assigned,
            RequestLeasesMessageType::Renew | RequestLeasesMessageType::Rebind => {
                if current_non_temporary_addresses
                    .keys()
                    .any(|iaid| !non_temporary_addresses.contains_key(iaid))
                {
                    // Stay in Renewing/Rebinding if any of the assigned IAs that the client
                    // is trying to renew are not included in the Reply, per RFC 8451 section
                    // 18.2.10.1:
                    //
                    //    When the client receives a Reply message in response to a Renew or
                    //    Rebind message, the client: [..] Sends a Renew/Rebind if any of
                    //    the IAs are not in the Reply message, but as this likely indicates
                    //    that the server that responded does not support that IA type, sending
                    //    immediately is unlikely to produce a different result.  Therefore,
                    //    the client MUST rate-limit its transmissions (see Section 14.1) and
                    //    MAY just wait for the normal retransmission time (as if the Reply
                    //    message had not been received).  The client continues to use other
                    //    bindings for which the server did return information.
                    //
                    // TODO(https://fxbug.dev/81086): implement rate limiting.
                    warn!(
                        "Reply to {}: allowing retransmit timeout to retry due to missing IA",
                        request_type
                    );
                    StateAfterReplyWithLeases::StayRenewingRebinding
                } else {
                    StateAfterReplyWithLeases::Assigned
                }
            }
        }
    };
    // Add configured addresses that were requested by the client but were
    // not received in this Reply.
    for (iaid, addr_entry) in current_non_temporary_addresses {
        let _: &mut AddressEntry =
            non_temporary_addresses.entry(*iaid).or_insert(match addr_entry {
                AddressEntry::ToRequest(address_to_request) => {
                    AddressEntry::ToRequest(*address_to_request)
                }
                AddressEntry::Assigned(ia) => {
                    AddressEntry::Assigned(*ia)
                    // TODO(https://fxbug.dev/76765): handle assigned addresses
                    // on transitioning from `Renewing` to `Requesting` for IAs
                    // with `NoBinding` status.
                }
            });
    }

    // TODO(https://fxbug.dev/96674): Add actions to remove addresses.
    // TODO(https://fxbug.dev/95265): Add action to add addresses.
    // TODO(https://fxbug.dev/96684): add actions to schedule/cancel
    // preferred and valid lifetime timers.
    let actions = match next_state {
        StateAfterReplyWithLeases::Assigned => {
            std::iter::once(Action::CancelTimer(ClientTimerType::Retransmission))
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
                .chain(match t1 {
                    v6::NonZeroTimeValue::Finite(t1_val) => Some(Action::ScheduleTimer(
                        ClientTimerType::Renew,
                        Duration::from_secs(t1_val.get().into()),
                    )),
                    v6::NonZeroTimeValue::Infinity => None,
                })
                // Per RFC 8415 section 18.2.5, set timer to enter rebind state:
                //
                //   At time T2 (which will only be reached if the server to
                //   which the Renew message was sent starting at time T1 has
                //   not responded), the client initiates a Rebind/Reply message
                //   exchange with any available server.
                //
                // Per RFC 8415 section 7.7, do not enter the Rebind state if
                // T2 is infinity:
                //
                //   A client will never attempt to use a Rebind message to
                //   locate a different server to extend the lifetimes of any
                //   addresses in an IA with T2 set to 0xffffffff.
                .chain(match t2 {
                    v6::NonZeroTimeValue::Finite(t2_val) => Some(Action::ScheduleTimer(
                        ClientTimerType::Rebind,
                        Duration::from_secs(t2_val.get().into()),
                    )),
                    v6::NonZeroTimeValue::Infinity => None,
                })
                .collect::<Vec<_>>()
        }
        StateAfterReplyWithLeases::RequestNextServer
        | StateAfterReplyWithLeases::StayRenewingRebinding
        | StateAfterReplyWithLeases::Requesting => Vec::new(),
    };

    Ok(ProcessedReplyWithLeases {
        server_id: got_server_id,
        non_temporary_addresses,
        dns_servers,
        actions,
        next_state,
    })
}

/// Create a map of addresses entries to be requested, combining the IAs in the
/// Advertise with the configured IAs that are not included in the Advertise.
fn advertise_to_address_entries(
    advertised_addresses: HashMap<v6::IAID, IdentityAssociation>,
    configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
) -> HashMap<v6::IAID, AddressEntry> {
    configured_addresses
        .iter()
        .map(|(iaid, configured_address)| {
            let address_to_request = match advertised_addresses.get(iaid) {
                Some(IdentityAssociation {
                    address: advertised_address,
                    preferred_lifetime: _,
                    valid_lifetime: _,
                }) => {
                    // Note that the advertised address for an IAID may
                    // be different from what was solicited by the
                    // client.
                    AddressToRequest::new(Some(*advertised_address), *configured_address)
                }
                // The configured address was not advertised; the client
                // will continue to request it in subsequent messages, per
                // RFC 8415 section 18.2:
                //
                //    When possible, the client SHOULD use the best
                //    configuration available and continue to request the
                //    additional IAs in subsequent messages.
                None => AddressToRequest::Configured(*configured_address),
            };
            (*iaid, AddressEntry::ToRequest(address_to_request))
        })
        .collect::<HashMap<_, _>>()
}

impl Requesting {
    /// Starts in requesting state following [RFC 8415, Section 18.2.2].
    ///
    /// [RFC 8415, Section 18.2.2]: https://tools.ietf.org/html/rfc8415#section-18.2.2
    fn start<R: Rng>(
        client_id: [u8; CLIENT_ID_LEN],
        server_id: Vec<u8>,
        non_temporary_addresses: HashMap<v6::IAID, AddressEntry>,
        options_to_request: &[v6::OptionCode],
        collected_advertise: BinaryHeap<AdvertiseMessage>,
        solicit_max_rt: Duration,
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        Self {
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            first_request_time: None,
            retrans_timeout: Duration::default(),
            retrans_count: 0,
            solicit_max_rt,
        }
        .send_and_reschedule_retransmission(transaction_id(), options_to_request, rng, now)
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

    /// A helper function that returns a transition to stay in `Requesting`, with
    /// actions to cancel current retransmission timer, send a request and
    /// schedules retransmission.
    fn send_and_reschedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        let Transition { state, actions: request_actions, transaction_id } =
            self.send_and_schedule_retransmission(transaction_id, options_to_request, rng, now);
        let actions = std::iter::once(Action::CancelTimer(ClientTimerType::Retransmission))
            .chain(request_actions.into_iter())
            .collect();
        Transition { state, actions, transaction_id }
    }

    /// A helper function that returns a transition to stay in `Requesting`, with
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
        now: Instant,
    ) -> Transition {
        let Self {
            client_id,
            server_id,
            non_temporary_addresses,
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
        for (iaid, addr_entry) in &non_temporary_addresses {
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
        let first_request_time = Some(first_request_time.map_or(now, |start_time| {
            elapsed_time = elapsed_time_in_centisecs(start_time, now);
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
                non_temporary_addresses,
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
        now: Instant,
    ) -> Transition {
        let Self {
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            first_request_time,
            retrans_timeout,
            retrans_count,
            solicit_max_rt,
        } = self;
        if retrans_count != REQUEST_MAX_RC {
            return Self {
                client_id,
                non_temporary_addresses,
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
                now,
            );
        }
        request_from_alternate_server_or_restart_server_discovery(
            client_id,
            non_temporary_addresses,
            &options_to_request,
            collected_advertise,
            solicit_max_rt,
            rng,
            now,
        )
    }

    fn reply_message_received<R: Rng, B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        msg: v6::Message<'_, B>,
        now: Instant,
    ) -> Transition {
        let Self {
            client_id,
            non_temporary_addresses: mut current_non_temporary_addresses,
            server_id,
            collected_advertise,
            first_request_time,
            retrans_timeout,
            retrans_count,
            mut solicit_max_rt,
        } = self;
        let ProcessedReplyWithLeases {
            server_id: got_server_id,
            non_temporary_addresses,
            dns_servers,
            actions,
            next_state,
        } = match process_reply_with_leases(
            client_id,
            &server_id,
            &current_non_temporary_addresses,
            &mut solicit_max_rt,
            &msg,
            RequestLeasesMessageType::Request,
        ) {
            Ok(processed) => processed,
            Err(e) => {
                match e {
                    ReplyWithLeasesError::StatusCodeError(StatusCodeError(error_code, message)) => {
                        match error_code {
                            v6::ErrorStatusCode::NotOnLink => {
                                // Per RFC 8415, section 18.2.10.1:
                                //
                                //    If the client receives a NotOnLink status from the server
                                //    in response to a Solicit (with a Rapid Commit option;
                                //    see Section 21.14) or a Request, the client can either
                                //    reissue the message without specifying any addresses or
                                //    restart the DHCP server discovery process (see Section 18).
                                //
                                // The client reissues the message without specifying addresses,
                                // leaving it up to the server to assign addresses appropriate
                                // for the client's link.
                                current_non_temporary_addresses.iter_mut().for_each(
                                    |(_, current_address_entry)| {
                                        // Discard all currently-assigned addresses.
                                        discard_leases(current_address_entry);

                                        *current_address_entry =
                                            AddressEntry::ToRequest(AddressToRequest::new(
                                                None,
                                                current_address_entry.configured_address(),
                                            ));
                                    },
                                );
                                // TODO(https://fxbug.dev/96674): append to actions to remove
                                // assigned addresses.
                                // TODO(https://fxbug.dev/96684): add actions to cancel
                                // preferred and valid lifetime timers for assigned addresses.
                                warn!(
                                    "Reply to Request: retrying Request without hints due to \
                                    NotOnLink error status code with message '{}'",
                                    message,
                                );
                                return Requesting {
                                    client_id,
                                    non_temporary_addresses: current_non_temporary_addresses,
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
                                    now,
                                );
                            }
                            // Per RFC 8415, section 18.2.10:
                            //
                            //    If the client receives a Reply message with a status code
                            //    of UnspecFail, the server is indicating that it was unable
                            //    to process the client's message due to an unspecified
                            //    failure condition.  If the client retransmits the original
                            //    message to the same server to retry the desired operation,
                            //    the client MUST limit the rate at which it retransmits
                            //    the message and limit the duration of the time during
                            //    which it retransmits the message (see Section 14.1).
                            //
                            // Ignore this Reply and rely on timeout for retransmission.
                            // TODO(https://fxbug.dev/81086): implement rate limiting.
                            v6::ErrorStatusCode::UnspecFail => {
                                warn!(
                                    "ignoring Reply to Request: ignoring due to UnspecFail error
                                    status code with message '{}'",
                                    message,
                                );
                            }
                            // TODO(https://fxbug.dev/76764): implement unicast.
                            // The client already uses multicast.
                            v6::ErrorStatusCode::UseMulticast => {
                                warn!(
                                    "ignoring Reply to Request: ignoring due to UseMulticast \
                                        with message '{}', but Request was already using multicast",
                                    message,
                                );
                            }
                            // Not expected as top level status.
                            v6::ErrorStatusCode::NoAddrsAvail
                            | v6::ErrorStatusCode::NoPrefixAvail
                            | v6::ErrorStatusCode::NoBinding => {
                                warn!(
                                    "ignoring Reply to Request due to unexpected top level error
                                    {:?} with message '{}'",
                                    error_code, message,
                                );
                            }
                        }
                        return Transition {
                            state: ClientState::Requesting(Self {
                                client_id,
                                non_temporary_addresses: current_non_temporary_addresses,
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
                    _ => {}
                }
                warn!("ignoring Reply to Request: {:?}", e);
                return Transition {
                    state: ClientState::Requesting(Self {
                        client_id,
                        non_temporary_addresses: current_non_temporary_addresses,
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
        };
        assert_eq!(
            server_id, got_server_id,
            "should be invalid to accept a reply to Request with mismatched server ID"
        );

        match next_state {
            StateAfterReplyWithLeases::StayRenewingRebinding => {
                unreachable!("cannot stay in Renewing/Rebinding state while in Requesting state");
            }
            StateAfterReplyWithLeases::Requesting => {
                unreachable!(
                    "cannot go back to Requesting from Requesting \
                    (only possible from Renewing/Rebinding)"
                );
            }
            StateAfterReplyWithLeases::RequestNextServer => {
                warn!("Reply to Request: trying next server");
                request_from_alternate_server_or_restart_server_discovery(
                    client_id,
                    current_non_temporary_addresses,
                    &options_to_request,
                    collected_advertise,
                    solicit_max_rt,
                    rng,
                    now,
                )
            }
            StateAfterReplyWithLeases::Assigned => {
                // TODO(https://fxbug.dev/72701) Send AddressWatcher update with
                // assigned addresses.
                Transition {
                    state: ClientState::Assigned(Assigned {
                        client_id,
                        non_temporary_addresses,
                        server_id,
                        collected_advertise,
                        dns_servers: dns_servers.unwrap_or(Vec::new()),
                        solicit_max_rt,
                    }),
                    actions,
                    transaction_id: None,
                }
            }
        }
    }
}

/// Represents an assigned identity association, relative to the configured
/// address for that IA.
#[derive(Debug, PartialEq, Clone, Copy)]
enum AssignedIa {
    /// The assigned address is the same as the configured address for the IA.
    Configured(IdentityAssociation),
    /// The assigned address is different than the configured address for the
    /// IA.
    NonConfigured(NonConfiguredIa),
}

impl AssignedIa {
    /// Creates a new assigned identity association.
    fn new(ia: IdentityAssociation, configured_address: Option<Ipv6Addr>) -> AssignedIa {
        match NonConfiguredIa::new(ia, configured_address) {
            None => AssignedIa::Configured(ia),
            Some(non_conf_ia) => AssignedIa::NonConfigured(non_conf_ia),
        }
    }

    /// Returns the assigned address.
    fn address(&self) -> Ipv6Addr {
        match self {
            AssignedIa::Configured(IdentityAssociation {
                address,
                preferred_lifetime: _,
                valid_lifetime: _,
            }) => *address,
            AssignedIa::NonConfigured(non_conf_ia) => non_conf_ia.address(),
        }
    }

    /// Returns the configured address.
    fn configured_address(&self) -> Option<Ipv6Addr> {
        match self {
            AssignedIa::Configured(IdentityAssociation {
                address,
                preferred_lifetime: _,
                valid_lifetime: _,
            }) => Some(*address),
            AssignedIa::NonConfigured(non_conf_ia) => non_conf_ia.configured_address(),
        }
    }
}

/// Represents an address entry negotiated by the client.
#[derive(Debug, PartialEq, Copy, Clone)]
enum AddressEntry {
    /// The address is assigned.
    Assigned(AssignedIa),
    /// The address is not assigned, and is to be requested in subsequent
    /// messages.
    ToRequest(AddressToRequest),
}

impl AddressEntry {
    /// Returns the assigned address.
    fn address(&self) -> Option<Ipv6Addr> {
        match self {
            AddressEntry::Assigned(ia) => Some(ia.address()),
            AddressEntry::ToRequest(address) => address.address(),
        }
    }

    /// Returns the configured address.
    fn configured_address(&self) -> Option<Ipv6Addr> {
        match self {
            AddressEntry::Assigned(ia) => ia.configured_address(),
            AddressEntry::ToRequest(address) => address.configured_address(),
        }
    }

    fn to_request(&self, without_hints: bool) -> Self {
        Self::ToRequest(AddressToRequest::new(
            if without_hints { None } else { self.address() },
            self.configured_address(),
        ))
    }
}

/// Extracts the configured addresses from a map of address entries.
fn to_configured_addresses(
    addresses: HashMap<v6::IAID, AddressEntry>,
) -> HashMap<v6::IAID, Option<Ipv6Addr>> {
    addresses.iter().map(|(iaid, addr_entry)| (*iaid, addr_entry.configured_address())).collect()
}

/// Provides methods for handling state transitions from Assigned state.
#[derive(Debug)]
struct Assigned {
    /// [Client Identifier] used for uniquely identifying the client in
    /// communication with servers.
    ///
    /// [Client Identifier]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.2
    client_id: [u8; CLIENT_ID_LEN],
    /// The non-temporary addresses entries negotiated by the client.
    non_temporary_addresses: HashMap<v6::IAID, AddressEntry>,
    /// The [server identifier] of the server to which the client sends
    /// requests.
    ///
    /// [Server Identifier]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.3
    server_id: Vec<u8>,
    /// The advertise collected from servers during [server discovery], with
    /// the best advertise at the top of the heap.
    ///
    /// [server discovery]: https://datatracker.ietf.org/doc/html/rfc8415#section-18
    collected_advertise: BinaryHeap<AdvertiseMessage>,
    /// Stores the DNS servers received from the reply.
    dns_servers: Vec<Ipv6Addr>,
    /// The [SOL_MAX_RT](https://datatracker.ietf.org/doc/html/rfc8415#section-21.24)
    /// used by the client.
    solicit_max_rt: Duration,
}

impl Assigned {
    /// Handles renew timer, following [RFC 8415, Section 18.2.4].
    ///
    /// [RFC 8415, Section 18.2.4]: https://tools.ietf.org/html/rfc8415#section-18.2.4
    fn renew_timer_expired<R: Rng>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        let Self {
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            dns_servers,
            solicit_max_rt,
        } = self;
        // Start renewing addresses, per RFC 8415, section 18.2.4:
        //
        //    At time T1, the client initiates a Renew/Reply message
        //    exchange to extend the lifetimes on any leases in the IA.
        Renewing::start(
            transaction_id(),
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            options_to_request,
            dns_servers,
            solicit_max_rt,
            rng,
            now,
        )
    }
}

type Renewing = RenewingOrRebinding<false /* IS_REBINDING */>;
type Rebinding = RenewingOrRebinding<true /* IS_REBINDING */>;

impl Renewing {
    /// Handles rebind timer, following [RFC 8415, Section 18.2.5].
    ///
    /// [RFC 8415, Section 18.2.5]: https://tools.ietf.org/html/rfc8415#section-18.2.4
    fn rebind_timer_expired<R: Rng>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        let Self(RenewingOrRebindingInner {
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            dns_servers,
            start_time: _,
            retrans_timeout: _,
            solicit_max_rt,
        }) = self;

        // Start rebinding, per RFC 8415, section 18.2.5:
        //
        //   At time T2 (which will only be reached if the server to which the
        //   Renew message was sent starting at time T1 has not responded), the
        //   client initiates a Rebind/Reply message exchange with any available
        //   server.
        Rebinding::start(
            transaction_id(),
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            options_to_request,
            dns_servers,
            solicit_max_rt,
            rng,
            now,
        )
    }
}

#[derive(Debug)]
#[cfg_attr(test, derive(Clone))]
struct RenewingOrRebindingInner {
    /// [Client Identifier](https://datatracker.ietf.org/doc/html/rfc8415#section-21.2)
    /// used for uniquely identifying the client in communication with servers.
    client_id: [u8; CLIENT_ID_LEN],
    /// The non-temporary addresses the client is initially configured to
    /// solicit, used when server discovery is restarted.
    non_temporary_addresses: HashMap<v6::IAID, AddressEntry>,
    /// [Server Identifier](https://datatracker.ietf.org/doc/html/rfc8415#section-21.2)
    /// of the server selected during server discovery.
    server_id: Vec<u8>,
    /// The advertise collected from servers during [server discovery].
    ///
    /// [server discovery]:
    /// https://datatracker.ietf.org/doc/html/rfc8415#section-18
    collected_advertise: BinaryHeap<AdvertiseMessage>,
    /// Stores the DNS servers received from the reply.
    dns_servers: Vec<Ipv6Addr>,
    /// [elapsed time](https://datatracker.ietf.org/doc/html/rfc8415#section-21.9).
    start_time: Option<Instant>,
    /// The renew/rebind message retransmission timeout.
    retrans_timeout: Duration,
    /// The [SOL_MAX_RT](https://datatracker.ietf.org/doc/html/rfc8415#section-21.24)
    /// used by the client.
    solicit_max_rt: Duration,
}

impl<const IS_REBINDING: bool> From<RenewingOrRebindingInner>
    for RenewingOrRebinding<IS_REBINDING>
{
    fn from(inner: RenewingOrRebindingInner) -> Self {
        Self(inner)
    }
}

// TODO(https://github.com/rust-lang/rust/issues/76560): Use an enum for the
// constant generic instead of a boolean for readability.
#[derive(Debug)]
struct RenewingOrRebinding<const IS_REBINDING: bool>(RenewingOrRebindingInner);

impl<const IS_REBINDING: bool> RenewingOrRebinding<IS_REBINDING> {
    /// Starts renewing or rebinding, following [RFC 8415, Section 18.2.4] or
    /// [RFC 8415, Section 18.2.5], respectively.
    ///
    /// [RFC 8415, Section 18.2.4]: https://tools.ietf.org/html/rfc8415#section-18.2.4
    /// [RFC 8415, Section 18.2.5]: https://tools.ietf.org/html/rfc8415#section-18.2.5
    fn start<R: Rng>(
        transaction_id: [u8; 3],
        client_id: [u8; CLIENT_ID_LEN],
        non_temporary_addresses: HashMap<v6::IAID, AddressEntry>,
        server_id: Vec<u8>,
        collected_advertise: BinaryHeap<AdvertiseMessage>,
        options_to_request: &[v6::OptionCode],
        dns_servers: Vec<Ipv6Addr>,
        solicit_max_rt: Duration,
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        Self(RenewingOrRebindingInner {
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            dns_servers,
            start_time: None,
            retrans_timeout: Duration::default(),
            solicit_max_rt,
        })
        .send_and_schedule_retransmission(transaction_id, options_to_request, rng, now)
    }

    /// Calculates timeout for retransmitting Renew/Rebind using parameters
    /// specified in [RFC 8415, Section 18.2.4]/[RFC 8415, Section 18.2.5].
    ///
    /// [RFC 8415, Section 18.2.4]: https://tools.ietf.org/html/rfc8415#section-18.2.4
    /// [RFC 8415, Section 18.2.5]: https://tools.ietf.org/html/rfc8415#section-18.2.5
    fn retransmission_timeout<R: Rng>(prev_retrans_timeout: Duration, rng: &mut R) -> Duration {
        let (initial, max) = if IS_REBINDING {
            (INITIAL_REBIND_TIMEOUT, MAX_REBIND_TIMEOUT)
        } else {
            (INITIAL_RENEW_TIMEOUT, MAX_RENEW_TIMEOUT)
        };

        retransmission_timeout(prev_retrans_timeout, initial, max, rng)
    }

    /// Returns a transition to stay in the current state, with actions to send
    /// a message and schedule retransmission.
    fn send_and_schedule_retransmission<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        let Self(RenewingOrRebindingInner {
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise,
            dns_servers,
            start_time,
            retrans_timeout: prev_retrans_timeout,
            solicit_max_rt,
        }) = self;
        let (start_time, elapsed_time) = match start_time {
            None => (now, 0),
            Some(start_time) => (start_time, elapsed_time_in_centisecs(start_time, now)),
        };

        let oro = std::iter::once(v6::OptionCode::SolMaxRt)
            .chain(options_to_request.iter().cloned())
            .collect::<Vec<_>>();

        // As per RFC 8415 section 18.2.5,
        //
        //   The client constructs the Rebind message as described in Section
        //   18.2.4, with the following differences:
        //
        //   -  The client sets the "msg-type" field to REBIND.
        //
        //   -  The client does not include the Server Identifier option (see
        //      Section 21.3) in the Rebind message.
        let (message_type, maybe_server_id_opt) = if IS_REBINDING {
            (v6::MessageType::Rebind, None)
        } else {
            (v6::MessageType::Renew, Some(v6::DhcpOption::ServerId(&server_id)))
        };

        let mut options = maybe_server_id_opt
            .into_iter()
            .chain([
                v6::DhcpOption::ClientId(&client_id),
                v6::DhcpOption::ElapsedTime(elapsed_time),
                v6::DhcpOption::Oro(&oro),
            ])
            .collect::<Vec<_>>();

        // TODO(https://fxbug.dev/86945): remove `iaaddr_options` construction
        // once `IanaSerializer::new()` takes options by value.
        let mut iaaddr_options = HashMap::new();
        // TODO(https://fxbug.dev/74324): all addresses in the map should be
        // valid; invalid addresses to be removed part of the
        // AddressStateProvider work.
        for (iaid, addr_entry) in &non_temporary_addresses {
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

        let builder = v6::MessageBuilder::new(message_type, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let retrans_timeout = Self::retransmission_timeout(prev_retrans_timeout, rng);

        Transition {
            state: {
                let inner = RenewingOrRebindingInner {
                    client_id,
                    non_temporary_addresses,
                    server_id,
                    collected_advertise,
                    dns_servers,
                    start_time: Some(start_time),
                    retrans_timeout,
                    solicit_max_rt,
                };

                if IS_REBINDING {
                    ClientState::Rebinding(inner.into())
                } else {
                    ClientState::Renewing(inner.into())
                }
            },
            actions: vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
            transaction_id: Some(transaction_id),
        }
    }

    /// Retransmits the renew or rebind message.
    fn retransmission_timer_expired<R: Rng>(
        self,
        transaction_id: [u8; 3],
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        self.send_and_schedule_retransmission(transaction_id, options_to_request, rng, now)
    }

    fn reply_message_received<R: Rng, B: ByteSlice>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        msg: v6::Message<'_, B>,
        now: Instant,
    ) -> Transition {
        let Self(RenewingOrRebindingInner {
            client_id,
            non_temporary_addresses: current_non_temporary_addresses,
            server_id,
            collected_advertise,
            dns_servers: current_dns_servers,
            start_time,
            retrans_timeout,
            mut solicit_max_rt,
        }) = self;
        let ProcessedReplyWithLeases {
            server_id: got_server_id,
            non_temporary_addresses,
            dns_servers,
            actions,
            next_state,
        } = match process_reply_with_leases(
            client_id,
            &server_id,
            &current_non_temporary_addresses,
            &mut solicit_max_rt,
            &msg,
            if IS_REBINDING {
                RequestLeasesMessageType::Rebind
            } else {
                RequestLeasesMessageType::Renew
            },
        ) {
            Ok(processed) => processed,
            Err(e) => {
                match e {
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
                    // TODO(https://fxbug.dev/81086): implement rate limiting. Without
                    // rate limiting support, the client relies on the regular
                    // retransmission mechanism to rate limit retransmission.
                    // Similarly, for other status codes indicating failure that are not
                    // expected in Reply to Renew, the client behaves as if the Reply
                    // message had not been received. Note the RFC does not specify what
                    // to do in this case; the client ignores the Reply in order to
                    // preserve existing bindings.
                    ReplyWithLeasesError::StatusCodeError(StatusCodeError(
                        v6::ErrorStatusCode::UnspecFail,
                        message,
                    )) => {
                        warn!(
                            "ignoring Reply to Renew with status code UnspecFail \
                                and message '{}'",
                            message
                        );
                    }
                    ReplyWithLeasesError::StatusCodeError(StatusCodeError(
                        v6::ErrorStatusCode::UseMulticast,
                        message,
                    )) => {
                        // TODO(https://fxbug.dev/76764): Implement unicast.
                        warn!(
                            "ignoring Reply to Renew with status code UseMulticast \
                                and message '{}' as Reply was already sent as multicast",
                            message
                        );
                    }
                    ReplyWithLeasesError::StatusCodeError(StatusCodeError(
                        error_code @ (v6::ErrorStatusCode::NoAddrsAvail
                        | v6::ErrorStatusCode::NoBinding
                        | v6::ErrorStatusCode::NotOnLink
                        | v6::ErrorStatusCode::NoPrefixAvail),
                        message,
                    )) => {
                        warn!(
                            "ignoring Reply to Renew with unexpected status code {:?} \
                                and message '{}'",
                            error_code, message
                        );
                    }
                    e @ (ReplyWithLeasesError::OptionsError(_)
                    | ReplyWithLeasesError::MismatchedServerId { got: _, want: _ }
                    | ReplyWithLeasesError::UnexpectedIaNa(_, _)) => {
                        warn!("ignoring Reply to Renew: {}", e);
                    }
                }

                return Transition {
                    state: {
                        let inner = RenewingOrRebindingInner {
                            client_id,
                            non_temporary_addresses: current_non_temporary_addresses,
                            server_id,
                            collected_advertise,
                            dns_servers: current_dns_servers,
                            start_time,
                            retrans_timeout,
                            solicit_max_rt,
                        };

                        if IS_REBINDING {
                            ClientState::Rebinding(inner.into())
                        } else {
                            ClientState::Renewing(inner.into())
                        }
                    },
                    actions: Vec::new(),
                    transaction_id: None,
                };
            }
        };
        if !IS_REBINDING {
            assert_eq!(
                server_id, got_server_id,
                "should be invalid to accept a reply to Renew with mismatched server ID"
            );
        } else if server_id != got_server_id {
            warn!(
                "using Reply to Rebind message from different server; current={:?}, new={:?}",
                server_id, got_server_id
            );
        }
        let server_id = got_server_id;

        match next_state {
            StateAfterReplyWithLeases::RequestNextServer => {
                request_from_alternate_server_or_restart_server_discovery(
                    client_id,
                    current_non_temporary_addresses,
                    &options_to_request,
                    collected_advertise,
                    solicit_max_rt,
                    rng,
                    now,
                )
            }
            StateAfterReplyWithLeases::StayRenewingRebinding => Transition {
                state: {
                    let inner = RenewingOrRebindingInner {
                        client_id,
                        non_temporary_addresses,
                        server_id,
                        collected_advertise,
                        dns_servers: dns_servers.unwrap_or_else(|| Vec::new()),
                        start_time,
                        retrans_timeout,
                        solicit_max_rt,
                    };

                    if IS_REBINDING {
                        ClientState::Rebinding(inner.into())
                    } else {
                        ClientState::Renewing(inner.into())
                    }
                },
                actions: Vec::new(),
                transaction_id: None,
            },
            StateAfterReplyWithLeases::Assigned => {
                // TODO(https://fxbug.dev/72701) Send AddressWatcher update with
                // assigned addresses.
                Transition {
                    state: ClientState::Assigned(Assigned {
                        client_id,
                        non_temporary_addresses,
                        server_id,
                        collected_advertise,
                        dns_servers: dns_servers.unwrap_or_else(|| Vec::new()),
                        solicit_max_rt,
                    }),
                    actions,
                    transaction_id: None,
                }
            }
            StateAfterReplyWithLeases::Requesting => Requesting::start(
                client_id,
                server_id,
                non_temporary_addresses,
                &options_to_request,
                collected_advertise,
                solicit_max_rt,
                rng,
                now,
            ),
        }
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
    Assigned(Assigned),
    /// Creating and (re)transmitting a renew message, and awaiting reply.
    Renewing(Renewing),
    /// Creating and (re)transmitting a rebind message, and awaiting reply.
    Rebinding(Rebinding),
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
        now: Instant,
    ) -> Transition {
        match self {
            ClientState::ServerDiscovery(s) => {
                s.advertise_message_received(options_to_request, rng, msg, now)
            }
            ClientState::InformationRequesting(_)
            | ClientState::InformationReceived(_)
            | ClientState::Requesting(_)
            | ClientState::Assigned(_)
            | ClientState::Renewing(_)
            | ClientState::Rebinding(_) => {
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
        now: Instant,
    ) -> Transition {
        match self {
            ClientState::InformationRequesting(s) => s.reply_message_received(msg),
            ClientState::Requesting(s) => {
                s.reply_message_received(options_to_request, rng, msg, now)
            }
            ClientState::Renewing(s) => s.reply_message_received(options_to_request, rng, msg, now),
            ClientState::Rebinding(s) => {
                s.reply_message_received(options_to_request, rng, msg, now)
            }
            ClientState::InformationReceived(_)
            | ClientState::ServerDiscovery(_)
            | ClientState::Assigned(_) => {
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
        now: Instant,
    ) -> Transition {
        match self {
            ClientState::InformationRequesting(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng)
            }
            ClientState::ServerDiscovery(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng, now)
            }
            ClientState::Requesting(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng, now)
            }
            ClientState::Renewing(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng, now)
            }
            ClientState::Rebinding(s) => {
                s.retransmission_timer_expired(transaction_id, options_to_request, rng, now)
            }
            ClientState::InformationReceived(_) | ClientState::Assigned(_) => {
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
            | ClientState::Requesting(_)
            | ClientState::Assigned(_)
            | ClientState::Renewing(_)
            | ClientState::Rebinding(_) => {
                unreachable!("received unexpected refresh timeout in state {:?}.", self);
            }
        }
    }

    /// Handles renew timeout.
    fn renew_timer_expired<R: Rng>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        match self {
            ClientState::Assigned(s) => s.renew_timer_expired(options_to_request, rng, now),
            ClientState::InformationRequesting(_)
            | ClientState::InformationReceived(_)
            | ClientState::ServerDiscovery(_)
            | ClientState::Requesting(_)
            | ClientState::Renewing(_)
            | ClientState::Rebinding(_) => {
                unreachable!("received unexpected renew timeout in state {:?}.", self);
            }
        }
    }

    /// Handles rebind timeout.
    fn rebind_timer_expired<R: Rng>(
        self,
        options_to_request: &[v6::OptionCode],
        rng: &mut R,
        now: Instant,
    ) -> Transition {
        match self {
            ClientState::Renewing(s) => s.rebind_timer_expired(options_to_request, rng, now),
            ClientState::InformationRequesting(_)
            | ClientState::InformationReceived(_)
            | ClientState::ServerDiscovery(_)
            | ClientState::Assigned(_)
            | ClientState::Requesting(_)
            | ClientState::Rebinding(_) => {
                unreachable!("received unexpected rebind timeout in state {:?}.", self);
            }
        }
    }

    /// Returns the DNS servers advertised by the server.
    fn get_dns_servers(&self) -> Vec<Ipv6Addr> {
        match self {
            ClientState::InformationReceived(InformationReceived { dns_servers }) => {
                dns_servers.clone()
            }
            ClientState::Assigned(Assigned {
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                collected_advertise: _,
                dns_servers,
                solicit_max_rt: _,
            })
            | ClientState::Renewing(RenewingOrRebinding(RenewingOrRebindingInner {
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                collected_advertise: _,
                dns_servers,
                start_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
            }))
            | ClientState::Rebinding(RenewingOrRebinding(RenewingOrRebindingInner {
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                collected_advertise: _,
                dns_servers,
                start_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
            })) => dns_servers.clone(),
            ClientState::InformationRequesting(InformationRequesting { retrans_timeout: _ })
            | ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_non_temporary_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise: _,
                collected_sol_max_rt: _,
            })
            | ClientState::Requesting(Requesting {
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
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

    /// Starts the client in Stateful mode, as defined in [RFC 8415, Section 6.2].
    /// The client exchanges messages with servers to obtain addresses in
    /// `configured_non_temporary_addresses`, and the configuration information in
    /// `options_to_request`.
    ///
    /// [RFC 8415, Section 6.1]: https://tools.ietf.org/html/rfc8415#section-6.2
    pub fn start_stateful(
        transaction_id: [u8; 3],
        client_id: [u8; CLIENT_ID_LEN],
        configured_non_temporary_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: Vec<v6::OptionCode>,
        mut rng: R,
        now: Instant,
    ) -> (Self, Actions) {
        let Transition { state, actions, transaction_id: new_transaction_id } =
            ServerDiscovery::start(
                transaction_id,
                client_id,
                configured_non_temporary_addresses,
                &options_to_request,
                MAX_SOLICIT_TIMEOUT,
                &mut rng,
                now,
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
    pub fn handle_timeout(&mut self, timeout_type: ClientTimerType, now: Instant) -> Actions {
        let ClientStateMachine { transaction_id, options_to_request, state, rng } = self;
        let old_state = state.take().expect("state should not be empty");
        let Transition { state: new_state, actions, transaction_id: new_transaction_id } =
            match timeout_type {
                ClientTimerType::Retransmission => old_state.retransmission_timer_expired(
                    *transaction_id,
                    &options_to_request,
                    rng,
                    now,
                ),
                ClientTimerType::Refresh => {
                    old_state.refresh_timer_expired(*transaction_id, &options_to_request, rng)
                }
                ClientTimerType::Renew => {
                    old_state.renew_timer_expired(&options_to_request, rng, now)
                }
                ClientTimerType::Rebind => {
                    old_state.rebind_timer_expired(&options_to_request, rng, now)
                }
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
    pub fn handle_message_receive<B: ByteSlice>(
        &mut self,
        msg: v6::Message<'_, B>,
        now: Instant,
    ) -> Actions {
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
                        now,
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
                        .advertise_message_received(&options_to_request, rng, msg, now);
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
pub(crate) mod testconsts {
    use net_declare::std_ip_v6;
    use packet_formats_dhcp::v6;
    use std::net::Ipv6Addr;

    pub(crate) const INFINITY: u32 = u32::MAX;
    pub(crate) const DNS_SERVERS: [Ipv6Addr; 2] =
        [std_ip_v6!("ff01::0102"), std_ip_v6!("ff01::0304")];
    pub(crate) const CLIENT_ID: [u8; 18] =
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17];
    pub(crate) const MISMATCHED_CLIENT_ID: [u8; 18] =
        [20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37];
    pub(crate) const TEST_SERVER_ID_LEN: usize = 3;
    pub(crate) const SERVER_ID: [[u8; TEST_SERVER_ID_LEN]; 3] =
        [[100, 101, 102], [110, 111, 112], [120, 121, 122]];

    pub(crate) const RENEW_NON_TEMPORARY_ADDRESSES: [Ipv6Addr; 3] = [
        std_ip_v6!("::ffff:4e45:123"),
        std_ip_v6!("::ffff:4e45:456"),
        std_ip_v6!("::ffff:4e45:789"),
    ];
    pub(crate) const REPLY_NON_TEMPORARY_ADDRESSES: [Ipv6Addr; 3] = [
        std_ip_v6!("::ffff:5447:123"),
        std_ip_v6!("::ffff:5447:456"),
        std_ip_v6!("::ffff:5447:789"),
    ];
    pub(crate) const CONFIGURED_NON_TEMPORARY_ADDRESSES: [Ipv6Addr; 3] = [
        std_ip_v6!("::ffff:c00a:123"),
        std_ip_v6!("::ffff:c00a:456"),
        std_ip_v6!("::ffff:c00a:789"),
    ];

    pub(crate) const T1: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(30));
    pub(crate) const T2: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(70));
    pub(crate) const PREFERRED_LIFETIME: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(40));
    pub(crate) const VALID_LIFETIME: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(80));

    pub(crate) const RENEWED_T1: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(130));
    pub(crate) const RENEWED_T2: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(170));
    pub(crate) const RENEWED_PREFERRED_LIFETIME: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(140));
    pub(crate) const RENEWED_VALID_LIFETIME: v6::NonZeroOrMaxU32 =
        const_unwrap::const_unwrap_option(v6::NonZeroOrMaxU32::new(180));
}

#[cfg(test)]
pub(crate) mod testutil {
    use super::*;
    use packet::ParsablePacket;
    use testconsts::*;

    pub(crate) fn to_configured_addresses(
        address_count: usize,
        preferred_addresses: impl IntoIterator<Item = Ipv6Addr>,
    ) -> HashMap<v6::IAID, Option<Ipv6Addr>> {
        let addresses = preferred_addresses
            .into_iter()
            .map(Some)
            .chain(std::iter::repeat(None))
            .take(address_count);

        let configured_addresses: HashMap<v6::IAID, Option<Ipv6Addr>> =
            (0..).map(v6::IAID::new).zip(addresses).collect();
        configured_addresses
    }

    pub(crate) fn to_default_ias_map(
        addresses: &[Ipv6Addr],
    ) -> HashMap<v6::IAID, IdentityAssociation> {
        (0..)
            .map(v6::IAID::new)
            .zip(addresses.iter().map(|address| IdentityAssociation::new_default(*address)))
            .collect()
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
        configured_non_temporary_addresses: HashMap<v6::IAID, Option<Ipv6Addr>>,
        options_to_request: Vec<v6::OptionCode>,
        rng: R,
        now: Instant,
    ) -> ClientStateMachine<R> {
        let (client, actions) = ClientStateMachine::start_stateful(
            transaction_id.clone(),
            client_id.clone(),
            configured_non_temporary_addresses.clone(),
            options_to_request.clone(),
            rng,
            now,
        );

        assert_matches!(
            &client,
            ClientStateMachine {
                transaction_id: got_transaction_id,
                options_to_request: got_options_to_request,
                state: Some(ClientState::ServerDiscovery(ServerDiscovery {
                    client_id: got_client_id,
                    configured_non_temporary_addresses: got_configured_non_temporary_addresses,
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
                 *got_configured_non_temporary_addresses == configured_non_temporary_addresses &&
                 collected_advertise.is_empty() &&
                 collected_sol_max_rt.is_empty()
        );

        // Start of server discovery should send a solicit and schedule a
        // retransmission timer.
        let mut buf = assert_matches!( &actions[..],
            [
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_SOLICIT_TIMEOUT)
            ] => buf
        );

        assert_outgoing_stateful_message(
            &mut buf,
            v6::MessageType::Solicit,
            &client_id,
            None,
            &options_to_request,
            &configured_non_temporary_addresses,
        );

        client
    }

    impl IdentityAssociation {
        pub(crate) fn new_finite(
            address: Ipv6Addr,
            preferred_lifetime: v6::NonZeroOrMaxU32,
            valid_lifetime: v6::NonZeroOrMaxU32,
        ) -> Self {
            Self {
                address,
                preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    preferred_lifetime,
                )),
                valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    valid_lifetime,
                )),
            }
        }

        pub(crate) fn new_default(address: Ipv6Addr) -> IdentityAssociation {
            IdentityAssociation {
                address,
                preferred_lifetime: v6::TimeValue::Zero,
                valid_lifetime: v6::TimeValue::Zero,
            }
        }
    }

    impl AddressEntry {
        pub(crate) fn new_assigned(
            address: Ipv6Addr,
            preferred_lifetime: v6::NonZeroOrMaxU32,
            valid_lifetime: v6::NonZeroOrMaxU32,
        ) -> Self {
            Self::Assigned(AssignedIa::new(
                IdentityAssociation::new_finite(address, preferred_lifetime, valid_lifetime),
                Some(address),
            ))
        }
    }

    impl AdvertiseMessage {
        pub(crate) fn new_default(
            server_id: [u8; TEST_SERVER_ID_LEN],
            non_temporary_addresses: &[Ipv6Addr],
            dns_servers: &[Ipv6Addr],
            configured_non_temporary_addresses: &HashMap<v6::IAID, Option<Ipv6Addr>>,
        ) -> AdvertiseMessage {
            let non_temporary_addresses = (0..)
                .map(v6::IAID::new)
                .zip(non_temporary_addresses.iter().fold(Vec::new(), |mut addrs, address| {
                    addrs.push(IdentityAssociation::new_default(*address));
                    addrs
                }))
                .collect();
            let preferred_non_temporary_addresses_count = compute_preferred_address_count(
                &non_temporary_addresses,
                &configured_non_temporary_addresses,
            );
            AdvertiseMessage {
                server_id: server_id.to_vec(),
                non_temporary_addresses,
                dns_servers: dns_servers.to_vec(),
                preference: 0,
                receive_time: Instant::now(),
                preferred_non_temporary_addresses_count,
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
        /// Creates a `TestIdentityAssociation` with default valid values for
        /// lifetimes.
        pub(crate) fn new_default(address: Ipv6Addr) -> TestIdentityAssociation {
            TestIdentityAssociation {
                address,
                preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    PREFERRED_LIFETIME,
                )),
                valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                    VALID_LIFETIME,
                )),
                t1: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(T1)),
                t2: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(T2)),
            }
        }
    }

    /// Creates a stateful client, exchanges messages to bring it in Requesting
    /// state, and sends a Request message. Returns the client in Requesting
    /// state and the transaction ID for the Request-Reply exchange. Asserts the
    /// content of the sent Request message and of the Requesting state.
    ///
    /// # Panics
    ///
    /// `request_addresses_and_assert` panics if the Request message cannot be
    /// parsed or does not contain the expected options, or the Requesting state
    /// is incorrect.
    pub(crate) fn request_addresses_and_assert<R: Rng + std::fmt::Debug>(
        client_id: [u8; CLIENT_ID_LEN],
        server_id: [u8; TEST_SERVER_ID_LEN],
        non_temporary_addresses_to_assign: Vec<TestIdentityAssociation>,
        expected_dns_servers: &[Ipv6Addr],
        rng: R,
        now: Instant,
    ) -> (ClientStateMachine<R>, [u8; 3]) {
        // Generate a transaction_id for the Solicit - Advertise message
        // exchange.
        let transaction_id = [1, 2, 3];
        let configured_non_temporary_addresses = to_configured_addresses(
            non_temporary_addresses_to_assign.len(),
            non_temporary_addresses_to_assign.iter().map(
                |TestIdentityAssociation {
                     address,
                     preferred_lifetime: _,
                     valid_lifetime: _,
                     t1: _,
                     t2: _,
                 }| *address,
            ),
        );
        let options_to_request = if expected_dns_servers.is_empty() {
            Vec::new()
        } else {
            vec![v6::OptionCode::DnsServers]
        };
        let mut client = testutil::start_and_assert_server_discovery(
            transaction_id.clone(),
            client_id.clone(),
            configured_non_temporary_addresses.clone(),
            options_to_request.clone(),
            rng,
            now,
        );

        let mut options = vec![
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&server_id),
            v6::DhcpOption::Preference(ADVERTISE_MAX_PREFERENCE),
        ];
        if !expected_dns_servers.is_empty() {
            options.push(v6::DhcpOption::DnsServers(&expected_dns_servers));
        }
        let non_temporary_addresses_to_assign: HashMap<v6::IAID, TestIdentityAssociation> =
            (0..).map(v6::IAID::new).zip(non_temporary_addresses_to_assign).collect();
        let mut iaaddr_opts = HashMap::new();
        for (iaid, ia) in &non_temporary_addresses_to_assign {
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
        for (iaid, ia) in &non_temporary_addresses_to_assign {
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
        // The client should select the server that sent the best advertise and
        // transition to Requesting.
        let actions = client.handle_message_receive(msg, now);
        let mut buf = assert_matches!(
            &actions[..],
           [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
           ] => buf
        );
        testutil::assert_outgoing_stateful_message(
            &mut buf,
            v6::MessageType::Request,
            &client_id,
            Some(&server_id),
            &options_to_request,
            &configured_non_temporary_addresses,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        let request_transaction_id = *transaction_id;
        {
            let Requesting {
                client_id: got_client_id,
                server_id: got_server_id,
                collected_advertise,
                retrans_timeout,
                retrans_count,
                solicit_max_rt,
                non_temporary_addresses: _,
                first_request_time: _,
            } = assert_matches!(&state, Some(ClientState::Requesting(requesting)) => requesting);
            assert_eq!(*got_client_id, client_id);
            assert_eq!(*got_server_id, server_id);
            assert!(
                collected_advertise.is_empty(),
                "collected_advertise = {:?}",
                collected_advertise
            );
            assert_eq!(*retrans_timeout, INITIAL_REQUEST_TIMEOUT);
            assert_eq!(*retrans_count, 0);
            assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        }
        (client, request_transaction_id)
    }

    /// Creates a stateful client and exchanges messages to assign the
    /// configured addresses. Returns the client in Assigned state and
    /// the actions returned on transitioning to the Assigned state.
    /// Asserts the content of the client state.
    ///
    /// # Panics
    ///
    /// `assign_addresses_and_assert` panics if address assignment fails.
    pub(crate) fn assign_addresses_and_assert<R: Rng + std::fmt::Debug>(
        client_id: [u8; CLIENT_ID_LEN],
        server_id: [u8; TEST_SERVER_ID_LEN],
        non_temporary_addresses_to_assign: Vec<TestIdentityAssociation>,
        expected_dns_servers: &[Ipv6Addr],
        rng: R,
        now: Instant,
    ) -> (ClientStateMachine<R>, Actions) {
        let (mut client, transaction_id) = testutil::request_addresses_and_assert(
            client_id.clone(),
            server_id.clone(),
            non_temporary_addresses_to_assign.clone(),
            expected_dns_servers,
            rng,
            now,
        );

        let mut options =
            vec![v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0])];
        if !expected_dns_servers.is_empty() {
            options.push(v6::DhcpOption::DnsServers(&expected_dns_servers));
        }
        let non_temporary_addresses_to_assign: HashMap<v6::IAID, TestIdentityAssociation> =
            (0..).map(v6::IAID::new).zip(non_temporary_addresses_to_assign).collect();
        let mut iaaddr_opts = HashMap::new();
        for (iaid, ia) in &non_temporary_addresses_to_assign {
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
        for (iaid, ia) in &non_temporary_addresses_to_assign {
            options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
                *iaid,
                testutil::get_value(ia.t1),
                testutil::get_value(ia.t2),
                iaaddr_opts.get(iaid).unwrap(),
            )));
        }
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg, now);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let expected_non_temporary_addresses = non_temporary_addresses_to_assign.iter().fold(
            HashMap::new(),
            |mut addrs, (iaid, ia)| {
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
                        AddressEntry::Assigned(AssignedIa::new(
                            IdentityAssociation {
                                address: *address,
                                preferred_lifetime: *preferred_lifetime,
                                valid_lifetime: *valid_lifetime
                            },
                            Some(*address)
                        ))
                    ),
                    None
                );
                addrs
            },
        );
        let Assigned {
            client_id: got_client_id,
            non_temporary_addresses,
            server_id: got_server_id,
            collected_advertise,
            dns_servers,
            solicit_max_rt,
        } = assert_matches!(
            &state,
            Some(ClientState::Assigned(assigned)) => assigned
        );
        assert_eq!(*got_client_id, client_id);
        assert_eq!(*non_temporary_addresses, expected_non_temporary_addresses);
        assert_eq!(*got_server_id, server_id);
        assert_eq!(dns_servers, expected_dns_servers);
        assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        (client, actions)
    }

    /// Gets the `u32` value inside a `v6::TimeValue`.
    pub(crate) fn get_value(t: v6::TimeValue) -> u32 {
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
        expected_server_id: Option<&[u8; TEST_SERVER_ID_LEN]>,
        expected_oro: &[v6::OptionCode],
        expected_non_temporary_addresses: &HashMap<v6::IAID, Option<Ipv6Addr>>,
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
        assert_eq!(&sent_addresses, expected_non_temporary_addresses);

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
        server_id: [u8; TEST_SERVER_ID_LEN],
        non_temporary_addresses_to_assign: Vec<TestIdentityAssociation>,
        expected_dns_servers: Option<&[Ipv6Addr]>,
        expected_t1_secs: v6::NonZeroOrMaxU32,
        expected_t2_secs: v6::NonZeroOrMaxU32,
        rng: R,
        now: Instant,
    ) -> ClientStateMachine<R> {
        let expected_dns_servers_as_slice = expected_dns_servers.unwrap_or(&[]);
        let (mut client, actions) = testutil::assign_addresses_and_assert(
            client_id.clone(),
            server_id.clone(),
            non_temporary_addresses_to_assign.clone(),
            expected_dns_servers_as_slice,
            rng,
            now,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        let old_transaction_id = *transaction_id;
        {
            let Assigned {
                collected_advertise,
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                dns_servers: _,
                solicit_max_rt: _,
            } = assert_matches!(
                state,
                Some(ClientState::Assigned(assigned)) => assigned
            );
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        }
        let (expected_oro, maybe_dns_server_action) =
            if let Some(expected_dns_servers) = expected_dns_servers {
                (
                    Some([v6::OptionCode::DnsServers]),
                    Some(Action::UpdateDnsServers(expected_dns_servers.to_vec())),
                )
            } else {
                (None, None)
            };
        let expected_actions = [Action::CancelTimer(ClientTimerType::Retransmission)]
            .into_iter()
            .chain(maybe_dns_server_action)
            .chain([
                Action::ScheduleTimer(
                    ClientTimerType::Renew,
                    Duration::from_secs(expected_t1_secs.get().into()),
                ),
                Action::ScheduleTimer(
                    ClientTimerType::Rebind,
                    Duration::from_secs(expected_t2_secs.get().into()),
                ),
            ])
            .collect::<Vec<_>>();
        assert_eq!(actions, expected_actions);

        // Renew timeout should trigger a transition to Renewing, send a renew
        // message and schedule retransmission.
        let actions = client.handle_timeout(ClientTimerType::Renew, now);
        let mut buf = assert_matches!(
            &actions[..],
            [
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_RENEW_TIMEOUT)
            ] => buf
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        // Assert that sending a renew starts a new transaction.
        assert_ne!(*transaction_id, old_transaction_id);
        let RenewingOrRebindingInner {
            client_id: got_client_id,
            server_id: got_server_id,
            dns_servers,
            collected_advertise,
            solicit_max_rt,
            non_temporary_addresses: _,
            start_time: _,
            retrans_timeout: _,
        } = assert_matches!(
            state,
            Some(ClientState::Renewing(RenewingOrRebinding(inner))) => inner
        );
        assert_eq!(*got_client_id, client_id);
        assert_eq!(*got_server_id, server_id);
        assert_eq!(dns_servers, expected_dns_servers_as_slice);
        assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        let expected_addresses_to_renew: HashMap<v6::IAID, Option<Ipv6Addr>> = (0..)
            .map(v6::IAID::new)
            .zip(non_temporary_addresses_to_assign.iter().map(
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
            Some(&server_id),
            expected_oro.as_ref().map_or(&[], |oro| &oro[..]),
            &expected_addresses_to_renew,
        );
        client
    }

    /// Creates a stateful client, exchanges messages to assign the configured
    /// addresses, and sends a Renew then Rebind message. Asserts the content of
    /// the client state and of the rebind message, and returns the client in
    /// Rebinding state.
    ///
    /// # Panics
    ///
    /// `send_rebind_and_assert` panics if address assignment fails, or if
    /// sending a rebind fails.
    pub(crate) fn send_rebind_and_assert<R: Rng + std::fmt::Debug>(
        client_id: [u8; CLIENT_ID_LEN],
        server_id: [u8; TEST_SERVER_ID_LEN],
        non_temporary_addresses_to_assign: Vec<TestIdentityAssociation>,
        expected_dns_servers: Option<&[Ipv6Addr]>,
        expected_t1_secs: v6::NonZeroOrMaxU32,
        expected_t2_secs: v6::NonZeroOrMaxU32,
        rng: R,
        now: Instant,
    ) -> ClientStateMachine<R> {
        let mut client = testutil::send_renew_and_assert(
            client_id,
            server_id,
            non_temporary_addresses_to_assign.clone(),
            expected_dns_servers,
            expected_t1_secs,
            expected_t2_secs,
            rng,
            now,
        );
        let old_transaction_id = client.transaction_id;

        let actions = client.handle_timeout(ClientTimerType::Rebind, now);
        let mut buf = assert_matches!(
            &actions[..],
            [
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REBIND_TIMEOUT)
            ] => buf
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        // Assert that sending a rebind starts a new transaction.
        assert_ne!(*transaction_id, old_transaction_id);
        let RenewingOrRebinding(RenewingOrRebindingInner {
            client_id: got_client_id,
            server_id: got_server_id,
            dns_servers,
            collected_advertise,
            solicit_max_rt,
            non_temporary_addresses: _,
            start_time: _,
            retrans_timeout: _,
        }) = assert_matches!(
            state,
            Some(ClientState::Rebinding(rebinding)) => rebinding
        );
        let (expected_oro, expected_dns_servers) =
            if let Some(expected_dns_servers) = expected_dns_servers {
                (Some([v6::OptionCode::DnsServers]), expected_dns_servers)
            } else {
                (None, &[][..])
            };
        assert_eq!(*got_client_id, client_id);
        assert_eq!(*got_server_id, server_id);
        assert_eq!(dns_servers, expected_dns_servers);
        assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        let expected_addresses_to_renew: HashMap<v6::IAID, Option<Ipv6Addr>> = (0..)
            .map(v6::IAID::new)
            .zip(non_temporary_addresses_to_assign.iter().map(
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
            v6::MessageType::Rebind,
            &client_id,
            None, /* expected_server_id */
            expected_oro.as_ref().map_or(&[], |oro| &oro[..]),
            &expected_addresses_to_renew,
        );

        client
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use packet::ParsablePacket;
    use rand::rngs::mock::StepRng;
    use test_case::test_case;
    use testconsts::*;
    use testutil::TestIdentityAssociation;

    #[test]
    fn send_information_request_and_receive_reply() {
        // Try to start information request with different list of requested options.
        for options in [
            Vec::new(),
            vec![v6::OptionCode::DnsServers],
            vec![v6::OptionCode::DnsServers, v6::OptionCode::DomainList],
        ] {
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

            let test_dhcp_refresh_time = 42u32;
            let options = [
                v6::DhcpOption::ServerId(&SERVER_ID[0]),
                v6::DhcpOption::InformationRefreshTime(test_dhcp_refresh_time),
                v6::DhcpOption::DnsServers(&DNS_SERVERS),
            ];
            let builder =
                v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
            let mut buf = vec![0; builder.bytes_len()];
            builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

            let actions = client.handle_message_receive(msg, Instant::now());
            let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
                client;

            {
                assert_matches!(
                    state,
                    Some(ClientState::InformationReceived(InformationReceived {dns_servers }))
                        if dns_servers == DNS_SERVERS.to_vec()
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
                    Action::UpdateDnsServers(DNS_SERVERS.to_vec())
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

        let actions = client.handle_timeout(ClientTimerType::Retransmission, Instant::now());
        // Following exponential backoff defined in https://tools.ietf.org/html/rfc8415#section-15.
        assert_matches!(
            actions[..],
            [
                _,
                Action::ScheduleTimer(ClientTimerType::Retransmission, timeout)
            ] if timeout == 2 * INITIAL_INFO_REQ_TIMEOUT
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
        let options = [v6::DhcpOption::ServerId(&SERVER_ID[0])];
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // Transition to InformationReceived state.
        let time = Instant::now();
        assert_eq!(
            client.handle_message_receive(msg, time)[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Refresh, IRT_DEFAULT)
            ]
        );

        // Refresh should start another round of information request.
        let actions = client.handle_timeout(ClientTimerType::Refresh, time);
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
    #[test_case(1, std::iter::empty(), Vec::new(); "one_addr_no_hint")]
    #[test_case(
        2, std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), vec![v6::OptionCode::DnsServers];
        "two_addr_one_hint"
    )]
    #[test_case(
        2, (&CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]).iter().copied(), vec![v6::OptionCode::DnsServers];
        "two_addr_two_hints"
    )]
    fn send_solicit(
        address_count: usize,
        preferred_non_temporary_addresses: impl IntoIterator<Item = Ipv6Addr>,
        options_to_request: Vec<v6::OptionCode>,
    ) {
        // The client is checked inside `start_and_assert_server_discovery`.
        let _client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            CLIENT_ID,
            testutil::to_configured_addresses(address_count, preferred_non_temporary_addresses),
            options_to_request,
            StepRng::new(std::u64::MAX / 2, 0),
            Instant::now(),
        );
    }

    #[test_case(
        1, std::iter::empty(), std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), 0;
        "zero"
    )]
    #[test_case(
        2, CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2].iter().copied(), CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2].iter().copied(), 2;
        "two"
    )]
    #[test_case(
        4,
        CONFIGURED_NON_TEMPORARY_ADDRESSES.iter().copied(),
        std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]).chain(REPLY_NON_TEMPORARY_ADDRESSES.iter().copied()),
        1;
        "one"
    )]
    fn compute_preferred_address_count(
        configure_count: usize,
        hints: impl IntoIterator<Item = Ipv6Addr>,
        got_addresses: impl IntoIterator<Item = Ipv6Addr>,
        want: usize,
    ) {
        // No preferred addresses configured.
        let got_addresses: HashMap<v6::IAID, IdentityAssociation> = (0..)
            .map(v6::IAID::new)
            .zip(got_addresses.into_iter().map(IdentityAssociation::new_default))
            .collect();
        let configured_non_temporary_addresses =
            testutil::to_configured_addresses(configure_count, hints);
        assert_eq!(
            super::compute_preferred_address_count(
                &got_addresses,
                &configured_non_temporary_addresses
            ),
            want,
        );
    }

    #[test]
    fn advertise_message_is_complete() {
        let configured_non_temporary_addresses = testutil::to_configured_addresses(
            2,
            std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
        );

        let advertise = AdvertiseMessage::new_default(
            SERVER_ID[0],
            &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2],
            &[],
            &configured_non_temporary_addresses,
        );
        assert!(advertise.is_complete(&configured_non_temporary_addresses, &[]));

        // Advertise is not complete: does not contain the solicited address
        // count.
        let advertise = AdvertiseMessage::new_default(
            SERVER_ID[0],
            &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..1],
            &[],
            &configured_non_temporary_addresses,
        );
        assert!(!advertise.is_complete(&configured_non_temporary_addresses, &[]));

        // Advertise is not complete: does not contain the solicited preferred
        // address.
        let advertise = AdvertiseMessage::new_default(
            SERVER_ID[0],
            &REPLY_NON_TEMPORARY_ADDRESSES[0..2],
            &[],
            &configured_non_temporary_addresses,
        );
        assert!(!advertise.is_complete(&configured_non_temporary_addresses, &[]));

        // Advertise is complete: contains both the requested addresses and
        // the requested options.
        let options_to_request = [v6::OptionCode::DnsServers];
        let advertise = AdvertiseMessage::new_default(
            SERVER_ID[0],
            &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2],
            &DNS_SERVERS,
            &configured_non_temporary_addresses,
        );
        assert!(advertise.is_complete(&configured_non_temporary_addresses, &options_to_request));
    }

    #[test]
    fn advertise_message_ord() {
        let configured_non_temporary_addresses = testutil::to_configured_addresses(
            3,
            std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
        );

        // `advertise2` is complete, `advertise1` is not.
        let advertise1 = AdvertiseMessage::new_default(
            SERVER_ID[0],
            &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2],
            &[],
            &configured_non_temporary_addresses,
        );
        let advertise2 = AdvertiseMessage::new_default(
            SERVER_ID[1],
            &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..3],
            &[],
            &configured_non_temporary_addresses,
        );
        assert!(advertise1 < advertise2);

        // Neither advertise is complete, but `advertise2` has more addresses,
        // hence `advertise2` is preferred even though it does not contain the
        // configured preferred address.
        let advertise1 = AdvertiseMessage::new_default(
            SERVER_ID[0],
            &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..1],
            &[],
            &configured_non_temporary_addresses,
        );
        let advertise2 = AdvertiseMessage::new_default(
            SERVER_ID[1],
            &REPLY_NON_TEMPORARY_ADDRESSES[0..2],
            &[],
            &configured_non_temporary_addresses,
        );
        assert!(advertise1 < advertise2);

        // Both advertise are complete, but `advertise1` was received first.
        let advertise1 = AdvertiseMessage::new_default(
            SERVER_ID[0],
            &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..3],
            &[],
            &configured_non_temporary_addresses,
        );
        let advertise2 = AdvertiseMessage::new_default(
            SERVER_ID[1],
            &[
                CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
                REPLY_NON_TEMPORARY_ADDRESSES[1],
                REPLY_NON_TEMPORARY_ADDRESSES[2],
            ],
            &[],
            &configured_non_temporary_addresses,
        );
        assert!(advertise1 > advertise2);
    }

    #[test_case(v6::DhcpOption::StatusCode(v6::StatusCode::Success.into(), ""); "status_code")]
    #[test_case(v6::DhcpOption::ClientId(&CLIENT_ID); "client_id")]
    #[test_case(v6::DhcpOption::ServerId(&SERVER_ID[0]); "server_id")]
    #[test_case(v6::DhcpOption::Preference(ADVERTISE_MAX_PREFERENCE); "preference")]
    #[test_case(v6::DhcpOption::SolMaxRt(*VALID_MAX_SOLICIT_TIMEOUT_RANGE.end()); "sol_max_rt")]
    #[test_case(v6::DhcpOption::DnsServers(&DNS_SERVERS); "dns_servers")]
    fn process_options_duplicates<'a>(opt: v6::DhcpOption<'a>) {
        let client_id = v6::duid_uuid();
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::StatusCode(v6::StatusCode::Success.into(), ""),
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Preference(ADVERTISE_MAX_PREFERENCE),
            v6::DhcpOption::SolMaxRt(*VALID_MAX_SOLICIT_TIMEOUT_RANGE.end()),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
            v6::DhcpOption::DnsServers(&DNS_SERVERS),
            opt,
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, [0, 1, 2], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(
            process_options(&msg, ExchangeType::AdvertiseToSolicit, Some(client_id)),
            Err(OptionsError::DuplicateOption(_, _, _))
        );
    }

    #[test]
    fn process_options_duplicate_ia_na_id() {
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            60,
            60,
            &[],
        ))];
        let iaid = v6::IAID::new(0);
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(iaid, T1.get(), T2.get(), &iana_options)),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(iaid, T1.get(), T2.get(), &iana_options)),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, [0, 1, 2], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(
            process_options(&msg, ExchangeType::AdvertiseToSolicit, Some(CLIENT_ID)),
            Err(OptionsError::DuplicateIaNaId(got_iaid, _, _)) if got_iaid == iaid
        );
    }

    #[test]
    fn process_options_missing_server_id() {
        let options = [v6::DhcpOption::ClientId(&CLIENT_ID)];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, [0, 1, 2], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(
            process_options(&msg, ExchangeType::AdvertiseToSolicit, Some(CLIENT_ID)),
            Err(OptionsError::MissingServerId)
        );
    }

    #[test]
    fn process_options_missing_client_id() {
        let options = [v6::DhcpOption::ServerId(&SERVER_ID[0])];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, [0, 1, 2], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(
            process_options(&msg, ExchangeType::AdvertiseToSolicit, Some(CLIENT_ID)),
            Err(OptionsError::MissingClientId)
        );
    }

    #[test]
    fn process_options_mismatched_client_id() {
        let options = [
            v6::DhcpOption::ClientId(&MISMATCHED_CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, [0, 1, 2], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(
            process_options(&msg, ExchangeType::AdvertiseToSolicit, Some(CLIENT_ID)),
            Err(OptionsError::MismatchedClientId { got, want })
                if got[..] == MISMATCHED_CLIENT_ID && want == CLIENT_ID
        );
    }

    #[test]
    fn process_options_unexpected_client_id() {
        let options =
            [v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0])];
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, [0, 1, 2], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(
            process_options(&msg, ExchangeType::ReplyToInformationRequest, None),
            Err(OptionsError::UnexpectedClientId(got))
                if got[..] == CLIENT_ID
        );
    }

    #[test_case(
        v6::MessageType::Reply,
        ExchangeType::ReplyToInformationRequest,
        v6::DhcpOption::Preference(ADVERTISE_MAX_PREFERENCE);
        "reply_to_information_request_preference"
    )]
    #[test_case(
        v6::MessageType::Reply,
        ExchangeType::ReplyToInformationRequest,
        v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), T1.get(),T2.get(), &[]));
        "reply_to_information_request_ia_na"
    )]
    #[test_case(
        v6::MessageType::Advertise,
        ExchangeType::AdvertiseToSolicit,
        v6::DhcpOption::InformationRefreshTime(42u32);
        "advertise_to_solicit_information_refresh_time"
    )]
    #[test_case(
        v6::MessageType::Reply,
        ExchangeType::ReplyWithLeases(RequestLeasesMessageType::Request),
        v6::DhcpOption::Preference(ADVERTISE_MAX_PREFERENCE);
        "reply_to_request_preference"
    )]
    fn process_options_invalid<'a>(
        message_type: v6::MessageType,
        exchange_type: ExchangeType,
        opt: v6::DhcpOption<'a>,
    ) {
        let options =
            [v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0]), opt];
        let builder = v6::MessageBuilder::new(message_type, [0, 1, 2], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(
            process_options(&msg, exchange_type, Some(CLIENT_ID)),
            Err(OptionsError::InvalidOption(_))
        );
    }

    #[test]
    fn process_reply_with_leases_unexpected_iaid() {
        let options =
            [v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0])]
                .into_iter()
                .chain((0..2).map(v6::IAID::new).map(|iaid| {
                    v6::DhcpOption::Iana(v6::IanaSerializer::new(iaid, T1.get(), T2.get(), &[]))
                }))
                .collect::<Vec<_>>();
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, [0, 1, 2], options.as_slice());
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        let assigned_addresses = HashMap::from([(
            v6::IAID::new(0),
            AddressEntry::new_assigned(
                CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
                PREFERRED_LIFETIME,
                VALID_LIFETIME,
            ),
        )]);
        let mut solicit_max_rt = MAX_SOLICIT_TIMEOUT;
        let r = process_reply_with_leases(
            CLIENT_ID,
            &SERVER_ID[0],
            &assigned_addresses,
            &mut solicit_max_rt,
            &msg,
            RequestLeasesMessageType::Request,
        );
        assert_matches!(
            r,
            Err(ReplyWithLeasesError::UnexpectedIaNa(iaid, _)) if iaid == v6::IAID::new(1)
        );
    }

    #[test]
    fn receive_complete_advertise_with_max_preference() {
        let time = Instant::now();
        let mut client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            CLIENT_ID,
            testutil::to_configured_addresses(
                1,
                std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
            ),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Preference(42),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
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
        assert!(client.handle_message_receive(msg, time).is_empty());
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Preference(255),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
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
        let actions = client.handle_message_receive(msg, time);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = client;
        let Requesting {
            client_id: _,
            non_temporary_addresses: _,
            server_id: _,
            collected_advertise: _,
            first_request_time: _,
            retrans_timeout: _,
            retrans_count: _,
            solicit_max_rt: _,
        } = assert_matches!(
            state,
            Some(ClientState::Requesting(requesting)) => requesting
        );
        let buf = assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
            ] => buf
        );
        assert_eq!(testutil::msg_type(buf), v6::MessageType::Request);
    }

    // T1 and T2 are non-zero and T1 > T2, the client should ignore this IA_NA option.
    #[test_case(T2.get() + 1, T2.get(), true)]
    #[test_case(INFINITY, T2.get(), true)]
    // T1 > T2, but T2 is zero, the client should process this IA_NA option.
    #[test_case(T1.get(), 0, false)]
    // T1 is zero, the client should process this IA_NA option.
    #[test_case(0, T2.get(), false)]
    // T1 <= T2, the client should process this IA_NA option.
    #[test_case(T1.get(), T2.get(), false)]
    #[test_case(T1.get(), INFINITY, false)]
    #[test_case(INFINITY, INFINITY, false)]
    fn receive_advertise_with_invalid_iana(t1: u32, t2: u32, ignore_iana: bool) {
        let transaction_id = [0, 1, 2];
        let time = Instant::now();
        let mut client = testutil::start_and_assert_server_discovery(
            transaction_id,
            CLIENT_ID,
            testutil::to_configured_addresses(
                1,
                std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
            ),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        let ia = IdentityAssociation {
            address: CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                PREFERRED_LIFETIME,
            )),
            valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(VALID_LIFETIME)),
        };
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            ia.address,
            PREFERRED_LIFETIME.get(),
            VALID_LIFETIME.get(),
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(v6::IAID::new(0), t1, t2, &iana_options)),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        assert_matches!(client.handle_message_receive(msg, time)[..], []);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let collected_advertise = assert_matches!(
            state,
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_non_temporary_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise,
                collected_sol_max_rt: _,
            })) => collected_advertise
        );
        match ignore_iana {
            true => assert!(collected_advertise.is_empty(), "{:?}", collected_advertise),
            false => {
                assert_matches!(
                    collected_advertise.peek(),
                    Some(AdvertiseMessage {
                        server_id: _,
                        non_temporary_addresses,
                        dns_servers: _,
                        preference: _,
                        receive_time: _,
                        preferred_non_temporary_addresses_count: _,
                    }) if *non_temporary_addresses == HashMap::from([(v6::IAID::new(0), ia)])
                )
            }
        }
    }

    #[test]
    fn select_first_server_while_retransmitting() {
        let time = Instant::now();
        let mut client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            CLIENT_ID,
            testutil::to_configured_addresses(
                1,
                std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
            ),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        // On transmission timeout, if no advertise were received the client
        // should stay in server discovery and resend solicit.
        let actions = client.handle_timeout(ClientTimerType::Retransmission, time);
        assert_matches!(
            &actions[..],
            [
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, timeout)
            ] if testutil::msg_type(buf) == v6::MessageType::Solicit &&
                 *timeout == 2 * INITIAL_SOLICIT_TIMEOUT => buf
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        {
            let ServerDiscovery {
                client_id: _,
                configured_non_temporary_addresses: _,
                first_solicit_time: _,
                retrans_timeout: _,
                solicit_max_rt: _,
                collected_advertise,
                collected_sol_max_rt: _,
            } = assert_matches!(
                state,
                Some(ClientState::ServerDiscovery(server_discovery)) => server_discovery
            );
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        }

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Advertise, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // The client should transition to Requesting when receiving any
        // advertise while retransmitting.
        let actions = client.handle_message_receive(msg, time);
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
            ] if testutil::msg_type(buf) == v6::MessageType::Request
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = client;
        let Requesting {
            client_id: _,
            non_temporary_addresses: _,
            server_id: _,
            collected_advertise,
            first_request_time: _,
            retrans_timeout: _,
            retrans_count: _,
            solicit_max_rt: _,
        } = assert_matches!(
            state,
            Some(ClientState::Requesting(requesting )) => requesting
        );
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
    }

    #[test]
    fn send_request() {
        let (mut _client, _transaction_id) = testutil::request_addresses_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            CONFIGURED_NON_TEMPORARY_ADDRESSES
                .into_iter()
                .map(TestIdentityAssociation::new_default)
                .collect(),
            &[],
            StepRng::new(std::u64::MAX / 2, 0),
            Instant::now(),
        );
    }

    // TODO(https://fxbug.dev/109224): Refactor this test into independent test cases.
    #[test]
    fn requesting_receive_reply_with_failure_status_code() {
        let options_to_request = vec![];
        let configured_non_temporary_addresses = testutil::to_configured_addresses(1, vec![]);
        let advertised_non_temporary_addresses = [CONFIGURED_NON_TEMPORARY_ADDRESSES[0]];
        let mut want_collected_advertise = [
            AdvertiseMessage::new_default(
                SERVER_ID[1],
                &CONFIGURED_NON_TEMPORARY_ADDRESSES[1..=1],
                &[],
                &configured_non_temporary_addresses,
            ),
            AdvertiseMessage::new_default(
                SERVER_ID[2],
                &CONFIGURED_NON_TEMPORARY_ADDRESSES[2..=2],
                &[],
                &configured_non_temporary_addresses,
            ),
        ]
        .into_iter()
        .collect::<BinaryHeap<_>>();
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let time = Instant::now();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            CLIENT_ID,
            SERVER_ID[0].to_vec(),
            advertise_to_address_entries(
                testutil::to_default_ias_map(&advertised_non_temporary_addresses),
                configured_non_temporary_addresses.clone(),
            ),
            &options_to_request[..],
            want_collected_advertise.clone(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
            time,
        );

        let expected_non_temporary_addresses = (0..)
            .map(v6::IAID::new)
            .zip(
                advertised_non_temporary_addresses
                    .iter()
                    .map(|addr| AddressEntry::ToRequest(AddressToRequest::new(Some(*addr), None))),
            )
            .collect::<HashMap<v6::IAID, AddressEntry>>();
        {
            let Requesting {
                non_temporary_addresses: got_non_temporary_addresses,
                server_id,
                collected_advertise,
                client_id: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            } = assert_matches!(&state, ClientState::Requesting(requesting) => requesting);
            assert_eq!(server_id[..], SERVER_ID[0]);
            assert_eq!(*got_non_temporary_addresses, expected_non_temporary_addresses);
            assert_eq!(
                collected_advertise.clone().into_sorted_vec(),
                want_collected_advertise.clone().into_sorted_vec()
            );
        }

        // If the reply contains a top level UnspecFail status code, the reply
        // should be ignored.
        let options = [
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &[],
            )),
            v6::DhcpOption::StatusCode(v6::ErrorStatusCode::UnspecFail.into(), ""),
        ];
        let request_transaction_id = transaction_id.unwrap();
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions, transaction_id: got_transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg, time);
        {
            let Requesting {
                client_id: _,
                non_temporary_addresses: got_non_temporary_addresses,
                server_id,
                collected_advertise,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            } = assert_matches!(&state, ClientState::Requesting(requesting) => requesting);
            assert_eq!(server_id[..], SERVER_ID[0]);
            assert_eq!(
                collected_advertise.clone().into_sorted_vec(),
                want_collected_advertise.clone().into_sorted_vec()
            );
            assert_eq!(*got_non_temporary_addresses, expected_non_temporary_addresses);
        }
        assert_eq!(got_transaction_id, None);
        assert_eq!(actions[..], []);

        // If the reply contains a top level NotOnLink status code, the
        // request should be resent without specifying any addresses.
        let options = [
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &[],
            )),
            v6::DhcpOption::StatusCode(v6::ErrorStatusCode::NotOnLink.into(), ""),
        ];
        let request_transaction_id = transaction_id.unwrap();
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg, time);

        let expected_non_temporary_addresses: HashMap<v6::IAID, AddressEntry> = HashMap::from([(
            v6::IAID::new(0),
            AddressEntry::ToRequest(AddressToRequest::new(None, None)),
        )]);
        {
            let Requesting {
                client_id: _,
                non_temporary_addresses: got_non_temporary_addresses,
                server_id,
                collected_advertise,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            } = assert_matches!(
                &state,
                ClientState::Requesting(requesting) => requesting
            );
            assert_eq!(server_id[..], SERVER_ID[0]);
            assert_eq!(
                collected_advertise.clone().into_sorted_vec(),
                want_collected_advertise.clone().into_sorted_vec()
            );
            assert_eq!(*got_non_temporary_addresses, expected_non_temporary_addresses);
        }
        assert!(transaction_id.is_some());

        // If the reply contains no usable addresses, the client selects
        // another server and sends a request to it.
        let iana_options =
            [v6::DhcpOption::StatusCode(v6::ErrorStatusCode::NoAddrsAvail.into(), "")];
        let options = [
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id.unwrap(), &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg, time);
        {
            let Requesting {
                server_id,
                collected_advertise,
                client_id: _,
                non_temporary_addresses: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            } = assert_matches!(
                state,
                ClientState::Requesting(requesting) => requesting
            );
            assert_eq!(server_id[..], SERVER_ID[1]);
            let _: Option<AdvertiseMessage> = want_collected_advertise.pop();
            assert_eq!(
                collected_advertise.clone().into_sorted_vec(),
                want_collected_advertise.clone().into_sorted_vec(),
            );
        }
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
        let configured_non_temporary_addresses =
            testutil::to_configured_addresses(2, vec![CONFIGURED_NON_TEMPORARY_ADDRESSES[0]]);
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let time = Instant::now();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            CLIENT_ID,
            SERVER_ID[0].to_vec(),
            advertise_to_address_entries(
                testutil::to_default_ias_map(&CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]),
                configured_non_temporary_addresses.clone(),
            ),
            &options_to_request[..],
            BinaryHeap::new(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
            time,
        );

        // If the reply contains an address with status code NotOnLink, the
        // client should request the IAs without specifying any addresses in
        // subsequent messages.
        let iana_options1 = [v6::DhcpOption::StatusCode(v6::ErrorStatusCode::NotOnLink.into(), "")];
        let iana_options2 = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[1],
            PREFERRED_LIFETIME.get(),
            VALID_LIFETIME.get(),
            &[],
        ))];
        let iaid1 = v6::IAID::new(0);
        let iaid2 = v6::IAID::new(1);
        let options = [
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                iaid1,
                T1.get(),
                T2.get(),
                &iana_options1,
            )),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                iaid2,
                T1.get(),
                T2.get(),
                &iana_options2,
            )),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id.unwrap(), &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions, transaction_id } =
            state.reply_message_received(&options_to_request, &mut rng, msg, time);
        let expected_non_temporary_addresses = HashMap::from([
            (
                iaid1,
                AddressEntry::ToRequest(AddressToRequest::new(
                    None,
                    Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
                )),
            ),
            (
                iaid2,
                AddressEntry::Assigned(AssignedIa::new(
                    IdentityAssociation {
                        address: CONFIGURED_NON_TEMPORARY_ADDRESSES[1],
                        preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                            PREFERRED_LIFETIME,
                        )),
                        valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                            VALID_LIFETIME,
                        )),
                    },
                    None,
                )),
            ),
        ]);
        {
            let Assigned {
                client_id: _,
                non_temporary_addresses,
                server_id,
                collected_advertise,
                dns_servers: _,
                solicit_max_rt: _,
            } = assert_matches!(
                state,
                ClientState::Assigned(assigned) => assigned
            );
            assert_eq!(server_id[..], SERVER_ID[0]);
            assert_eq!(non_temporary_addresses, expected_non_temporary_addresses);
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        }
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1),
                Action::ScheduleTimer(ClientTimerType::Rebind, t2)
            ] => {
                assert_eq!(*t1, Duration::from_secs(T1.get().into()));
                assert_eq!(*t2, Duration::from_secs(T2.get().into()));
            }
        );
        assert!(transaction_id.is_none());
    }

    #[test_case(0, VALID_LIFETIME.get(), true)]
    #[test_case(PREFERRED_LIFETIME.get(), 0, false)]
    #[test_case(VALID_LIFETIME.get() + 1, VALID_LIFETIME.get(), false)]
    #[test_case(0, 0, false)]
    #[test_case(PREFERRED_LIFETIME.get(), VALID_LIFETIME.get(), true)]
    fn requesting_receive_reply_with_invalid_ia_lifetimes(
        preferred_lifetime: u32,
        valid_lifetime: u32,
        valid_ia: bool,
    ) {
        let options_to_request = vec![];
        let configured_non_temporary_addresses = testutil::to_configured_addresses(1, vec![]);
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);

        let time = Instant::now();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            CLIENT_ID,
            SERVER_ID[0].to_vec(),
            advertise_to_address_entries(
                testutil::to_default_ias_map(&CONFIGURED_NON_TEMPORARY_ADDRESSES[0..1]),
                configured_non_temporary_addresses.clone(),
            ),
            &options_to_request[..],
            BinaryHeap::new(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
            time,
        );

        // The client should discard the IAs with invalid lifetimes.
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            preferred_lifetime,
            valid_lifetime,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id.unwrap(), &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id: _ } =
            state.reply_message_received(&options_to_request, &mut rng, msg, time);
        match valid_ia {
            true =>
            // The client should transition to Assigned if the reply contains
            // a valid IA.
            {
                let Assigned {
                    client_id: _,
                    non_temporary_addresses: _,
                    server_id: _,
                    collected_advertise,
                    dns_servers: _,
                    solicit_max_rt: _,
                } = assert_matches!(
                    state,
                    ClientState::Assigned(assigned) => assigned
                );
                assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            }
            false =>
            // The client should transition to ServerDiscovery if the reply contains
            // no valid IAs.
            {
                let ServerDiscovery {
                    client_id: _,
                    configured_non_temporary_addresses: _,
                    first_solicit_time: _,
                    retrans_timeout: _,
                    solicit_max_rt: _,
                    collected_advertise,
                    collected_sol_max_rt: _,
                } = assert_matches!(
                    state,
                    ClientState::ServerDiscovery(server_discovery) => server_discovery
                );
                assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            }
        }
    }

    // Test that T1/T2 are calculated correctly on receiving a Reply to Request.
    #[test]
    fn compute_t1_t2_on_reply_to_request() {
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
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(50).expect("should succeed")),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(80).expect("should succeed")),
            ),
            (
                (INFINITY, INFINITY, 0, 0),
                (120, 180, 0, 0),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(60).expect("should succeed")),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(96).expect("should succeed")),
            ),
            // If T1/T2 are 0, and the minimum preferred lifetime, is infinity,
            // T1/T2 should also be infinity.
            (
                (INFINITY, INFINITY, 0, 0),
                (INFINITY, INFINITY, 0, 0),
                v6::NonZeroTimeValue::Infinity,
                v6::NonZeroTimeValue::Infinity,
            ),
            // T2 may be infinite if T1 is finite.
            (
                (INFINITY, INFINITY, 50, INFINITY),
                (INFINITY, INFINITY, 50, INFINITY),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(50).expect("should succeed")),
                v6::NonZeroTimeValue::Infinity,
            ),
            // If T1/T2 are set, and have different values across IAs, T1/T2
            // should be computed as the minimum T1/T2. NOTE: the server should
            // send the same T1/T2 across all IA, but the client should be
            // prepared for the server sending different T1/T2 values.
            (
                (100, 160, 40, 70),
                (120, 180, 50, 80),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(40).expect("should succeed")),
                v6::NonZeroTimeValue::Finite(v6::NonZeroOrMaxU32::new(70).expect("should succeed")),
            ),
        ] {
            let time = Instant::now();
            let Transition { state, actions: _, transaction_id } = Requesting::start(
                CLIENT_ID,
                SERVER_ID[0].to_vec(),
                advertise_to_address_entries(
                    testutil::to_default_ias_map(&CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]),
                    testutil::to_configured_addresses(
                        2,
                        std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
                    ),
                ),
                &[],
                BinaryHeap::new(),
                MAX_SOLICIT_TIMEOUT,
                &mut rng,
                time,
            );

            let iana_options1 = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
                CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
                ia1_preferred_lifetime,
                ia1_valid_lifetime,
                &[],
            ))];
            let iana_options2 = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
                CONFIGURED_NON_TEMPORARY_ADDRESSES[1],
                ia2_preferred_lifetime,
                ia2_valid_lifetime,
                &[],
            ))];
            let options = [
                v6::DhcpOption::ServerId(&SERVER_ID[0]),
                v6::DhcpOption::ClientId(&CLIENT_ID),
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
            let Transition { state, actions, transaction_id: _ } =
                state.reply_message_received(&[], &mut rng, msg, time);
            let Assigned {
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                collected_advertise,
                dns_servers: _,
                solicit_max_rt: _,
            } = assert_matches!(
                state,
                ClientState::Assigned(assigned) => assigned
            );
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            match (expected_t1, expected_t2) {
                (v6::NonZeroTimeValue::Finite(t1_val), v6::NonZeroTimeValue::Finite(t2_val)) => {
                    assert_matches!(
                        &actions[..],
                        [
                            Action::CancelTimer(ClientTimerType::Retransmission),
                            Action::ScheduleTimer(ClientTimerType::Renew, t1),
                            Action::ScheduleTimer(ClientTimerType::Rebind, t2)
                        ] => {
                            assert_eq!(*t1, Duration::from_secs(t1_val.get().into()));
                            assert_eq!(*t2, Duration::from_secs(t2_val.get().into()));
                        }
                    );
                }
                (v6::NonZeroTimeValue::Finite(t1_val), v6::NonZeroTimeValue::Infinity) => {
                    assert_matches!(
                        &actions[..],
                        [
                            Action::CancelTimer(ClientTimerType::Retransmission),
                            Action::ScheduleTimer(ClientTimerType::Renew, t1),
                        ] => {
                            assert_eq!(*t1, Duration::from_secs(t1_val.get().into()));
                        }
                    );
                }
                (v6::NonZeroTimeValue::Infinity, v6::NonZeroTimeValue::Infinity) => {
                    assert_matches!(
                        &actions[..],
                        [Action::CancelTimer(ClientTimerType::Retransmission)]
                    );
                }
                (v6::NonZeroTimeValue::Infinity, v6::NonZeroTimeValue::Finite(t2_val)) => {
                    panic!("cannot have a finite T2={:?} with an infinite T1", t2_val)
                }
            };
        }
    }

    // Test that Request retransmission respects max retransmission count.
    #[test]
    fn requesting_retransmit_max_retrans_count() {
        let transaction_id = [0, 1, 2];
        let time = Instant::now();
        let mut client = testutil::start_and_assert_server_discovery(
            transaction_id,
            CLIENT_ID,
            testutil::to_configured_addresses(
                1,
                std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
            ),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(client.handle_message_receive(msg, time)[..], []);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let ServerDiscovery {
            client_id: _,
            configured_non_temporary_addresses: _,
            first_solicit_time: _,
            retrans_timeout: _,
            solicit_max_rt: _,
            collected_advertise: _,
            collected_sol_max_rt: _,
        } = assert_matches!(
            state,
            Some(ClientState::ServerDiscovery(server_discovery)) => server_discovery
        );

        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[1],
            60,
            60,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[1]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Advertise, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        assert_matches!(client.handle_message_receive(msg, time)[..], []);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let ServerDiscovery {
            collected_advertise: want_collected_advertise,
            client_id: _,
            configured_non_temporary_addresses: _,
            first_solicit_time: _,
            retrans_timeout: _,
            solicit_max_rt: _,
            collected_sol_max_rt: _,
        } = assert_matches!(
            state,
            Some(ClientState::ServerDiscovery(server_discovery)) => server_discovery
        );
        let mut want_collected_advertise = want_collected_advertise.clone();
        let _: Option<AdvertiseMessage> = want_collected_advertise.pop();

        // The client should transition to Requesting and select the server that
        // sent the best advertise.
        assert_matches!(
            &client.handle_timeout(ClientTimerType::Retransmission, time)[..],
           [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
           ] if testutil::msg_type(buf) == v6::MessageType::Request
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        {
            let Requesting {
                client_id: _,
                non_temporary_addresses: _,
                server_id,
                collected_advertise,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt: _,
            } = assert_matches!(state, Some(ClientState::Requesting(requesting)) => requesting);
            assert_eq!(
                collected_advertise.clone().into_sorted_vec(),
                want_collected_advertise.clone().into_sorted_vec()
            );
            assert_eq!(server_id[..], SERVER_ID[0]);
        }

        for count in 1..REQUEST_MAX_RC + 1 {
            assert_matches!(
                &client.handle_timeout(ClientTimerType::Retransmission, time)[..],
               [
                    Action::SendMessage(buf),
                    // `_timeout` is not checked because retransmission timeout
                    // calculation is covered in its respective test.
                    Action::ScheduleTimer(ClientTimerType::Retransmission, _timeout)
               ] if testutil::msg_type(buf) == v6::MessageType::Request
            );
            let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
                &client;
            let Requesting {
                client_id: _,
                non_temporary_addresses: _,
                server_id,
                collected_advertise,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count,
                solicit_max_rt: _,
            } = assert_matches!(state, Some(ClientState::Requesting(requesting)) => requesting);
            assert_eq!(
                collected_advertise.clone().into_sorted_vec(),
                want_collected_advertise.clone().into_sorted_vec()
            );
            assert_eq!(server_id[..], SERVER_ID[0]);
            assert_eq!(*retrans_count, count);
        }

        // When the retransmission count reaches REQUEST_MAX_RC, the client
        // should select another server.
        assert_matches!(
            &client.handle_timeout(ClientTimerType::Retransmission, time)[..],
           [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
           ] if testutil::msg_type(buf) == v6::MessageType::Request
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let Requesting {
            client_id: _,
            non_temporary_addresses: _,
            server_id,
            collected_advertise,
            first_request_time: _,
            retrans_timeout: _,
            retrans_count,
            solicit_max_rt: _,
        } = assert_matches!(state, Some(ClientState::Requesting(requesting)) => requesting);
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        assert_eq!(server_id[..], SERVER_ID[1]);
        assert_eq!(*retrans_count, 0);

        for count in 1..REQUEST_MAX_RC + 1 {
            assert_matches!(
                &client.handle_timeout(ClientTimerType::Retransmission, time)[..],
               [
                    Action::SendMessage(buf),
                    // `_timeout` is not checked because retransmission timeout
                    // calculation is covered in its respective test.
                    Action::ScheduleTimer(ClientTimerType::Retransmission, _timeout)
               ] if testutil::msg_type(buf) == v6::MessageType::Request
            );
            let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
                &client;
            let Requesting {
                client_id: _,
                non_temporary_addresses: _,
                server_id,
                collected_advertise,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count,
                solicit_max_rt: _,
            } = assert_matches!(state, Some(ClientState::Requesting(requesting)) => requesting);
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            assert_eq!(server_id[..], SERVER_ID[1]);
            assert_eq!(*retrans_count, count);
        }

        // When the retransmission count reaches REQUEST_MAX_RC, and the client
        // does not have information about another server, the client should
        // restart server discovery.
        assert_matches!(
            &client.handle_timeout(ClientTimerType::Retransmission, time)[..],
           [
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_SOLICIT_TIMEOUT)
           ] if testutil::msg_type(buf) == v6::MessageType::Solicit
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } = client;
        assert_matches!(state,
            Some(ClientState::ServerDiscovery(ServerDiscovery {
                client_id: _,
                configured_non_temporary_addresses: _,
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
        let (client, actions) = testutil::assign_addresses_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]
                .iter()
                .copied()
                .map(TestIdentityAssociation::new_default)
                .collect(),
            &[],
            StepRng::new(std::u64::MAX / 2, 0),
            Instant::now(),
        );

        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let Assigned {
            collected_advertise,
            client_id: _,
            non_temporary_addresses: _,
            server_id: _,
            dns_servers: _,
            solicit_max_rt: _,
        } = assert_matches!(
            state,
            Some(ClientState::Assigned(assigned)) => assigned
        );
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1),
                Action::ScheduleTimer(ClientTimerType::Rebind, t2)
            ] => {
                assert_eq!(*t1, Duration::from_secs(T1.get().into()));
                assert_eq!(*t2, Duration::from_secs(T2.get().into()));
            }
        );
    }

    #[test]
    fn assigned_get_dns_servers() {
        let (client, actions) = testutil::assign_addresses_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            vec![TestIdentityAssociation::new_default(CONFIGURED_NON_TEMPORARY_ADDRESSES[0])],
            &DNS_SERVERS,
            StepRng::new(std::u64::MAX / 2, 0),
            Instant::now(),
        );
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::UpdateDnsServers(dns_servers),
                Action::ScheduleTimer(ClientTimerType::Renew, t1),
                Action::ScheduleTimer(ClientTimerType::Rebind, t2)
            ] => {
                assert_eq!(dns_servers[..], DNS_SERVERS);
                assert_eq!(*t1, Duration::from_secs(T1.get().into()));
                assert_eq!(*t2, Duration::from_secs(T2.get().into()));
            }
        );
        assert_eq!(client.get_dns_servers()[..], DNS_SERVERS);
    }

    #[test]
    fn update_sol_max_rt_on_reply_to_request() {
        let options_to_request = vec![];
        let configured_non_temporary_addresses = testutil::to_configured_addresses(1, vec![]);
        let mut rng = StepRng::new(std::u64::MAX / 2, 0);
        let time = Instant::now();
        let Transition { state, actions: _, transaction_id } = Requesting::start(
            CLIENT_ID,
            SERVER_ID[0].to_vec(),
            advertise_to_address_entries(
                testutil::to_default_ias_map(&CONFIGURED_NON_TEMPORARY_ADDRESSES[0..1]),
                configured_non_temporary_addresses.clone(),
            ),
            &options_to_request[..],
            BinaryHeap::new(),
            MAX_SOLICIT_TIMEOUT,
            &mut rng,
            time,
        );
        {
            let Requesting {
                collected_advertise,
                solicit_max_rt,
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
            } = assert_matches!(&state, ClientState::Requesting(requesting) => requesting);
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        }
        let received_sol_max_rt = 4800;

        // If the reply does not contain a server ID, the reply should be
        // discarded and the `solicit_max_rt` should not be updated.
        let iana_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            60,
            120,
            &[],
        ))];
        let options = [
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
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
            state.reply_message_received(&options_to_request, &mut rng, msg, time);
        {
            let Requesting {
                collected_advertise,
                solicit_max_rt,
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
            } = assert_matches!(&state, ClientState::Requesting(requesting) => requesting);
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        }

        // If the reply has a different client ID than the test client's client ID,
        // the `solicit_max_rt` should not be updated.
        let options = [
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::ClientId(&MISMATCHED_CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
            v6::DhcpOption::SolMaxRt(received_sol_max_rt),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id: _ } =
            state.reply_message_received(&options_to_request, &mut rng, msg, time);
        {
            let Requesting {
                collected_advertise,
                solicit_max_rt,
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
            } = assert_matches!(&state, ClientState::Requesting(requesting) => requesting);
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        }

        // If the client receives a valid reply containing a SOL_MAX_RT option,
        // the `solicit_max_rt` should be updated.
        let options = [
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                T1.get(),
                T2.get(),
                &iana_options,
            )),
            v6::DhcpOption::SolMaxRt(received_sol_max_rt),
        ];
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, request_transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let Transition { state, actions: _, transaction_id: _ } =
            state.reply_message_received(&options_to_request, &mut rng, msg, time);
        {
            let Assigned {
                collected_advertise,
                solicit_max_rt,
                client_id: _,
                non_temporary_addresses: _,
                server_id: _,
                dns_servers: _,
            } = assert_matches!(&state, ClientState::Assigned(assigned) => assigned);
            assert!(
                collected_advertise.is_empty(),
                "collected_advertise = {:?}",
                collected_advertise
            );
            assert_eq!(*solicit_max_rt, Duration::from_secs(received_sol_max_rt.into()));
        }
    }

    struct RenewRebindTest {
        send_and_assert: fn(
            [u8; CLIENT_ID_LEN],
            [u8; TEST_SERVER_ID_LEN],
            Vec<TestIdentityAssociation>,
            Option<&[Ipv6Addr]>,
            v6::NonZeroOrMaxU32,
            v6::NonZeroOrMaxU32,
            StepRng,
            Instant,
        ) -> ClientStateMachine<StepRng>,
        message_type: v6::MessageType,
        expect_server_id: bool,
        with_state: fn(&Option<ClientState>) -> &RenewingOrRebindingInner,
        allow_response_from_any_server: bool,
    }

    const RENEW_TEST: RenewRebindTest = RenewRebindTest {
        send_and_assert: testutil::send_renew_and_assert,
        message_type: v6::MessageType::Renew,
        expect_server_id: true,
        with_state: |state| {
            assert_matches!(
                state,
                Some(ClientState::Renewing(RenewingOrRebinding(inner))) => inner
            )
        },
        allow_response_from_any_server: false,
    };

    const REBIND_TEST: RenewRebindTest = RenewRebindTest {
        send_and_assert: testutil::send_rebind_and_assert,
        message_type: v6::MessageType::Rebind,
        expect_server_id: false,
        with_state: |state| {
            assert_matches!(
                state,
                Some(ClientState::Rebinding(RenewingOrRebinding(inner))) => inner
            )
        },
        allow_response_from_any_server: true,
    };

    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    fn send(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state: _,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let _client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]
                .into_iter()
                .map(|&addr| TestIdentityAssociation::new_default(addr))
                .collect(),
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            Instant::now(),
        );
    }

    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    fn get_dns_server(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state: _,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]
                .into_iter()
                .map(|&addr| TestIdentityAssociation::new_default(addr))
                .collect(),
            Some(&DNS_SERVERS),
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            Instant::now(),
        );
        assert_eq!(client.get_dns_servers()[..], DNS_SERVERS);
    }

    #[test]
    fn do_not_renew_for_t1_infinity() {
        let (client, actions) = testutil::assign_addresses_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            vec![TestIdentityAssociation {
                t1: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
                t2: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
                ..TestIdentityAssociation::new_default(CONFIGURED_NON_TEMPORARY_ADDRESSES[0])
            }],
            &[],
            StepRng::new(std::u64::MAX / 2, 0),
            Instant::now(),
        );
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let Assigned {
            collected_advertise,
            client_id: _,
            non_temporary_addresses: _,
            server_id: _,
            dns_servers: _,
            solicit_max_rt: _,
        } = assert_matches!(
            state,
            Some(ClientState::Assigned(assigned)) => assigned
        );
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
        // Asserts that the actions do not include scheduling the renew timer.
        assert_matches!(&actions[..], [Action::CancelTimer(ClientTimerType::Retransmission)]);
    }

    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    fn retransmit(
        RenewRebindTest {
            send_and_assert,
            message_type,
            expect_server_id,
            with_state,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let non_temporary_addresses_to_assign = CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]
            .into_iter()
            .map(|&addr| TestIdentityAssociation::new_default(addr))
            .collect::<Vec<_>>();
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            non_temporary_addresses_to_assign.clone(),
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        let expected_transaction_id = *transaction_id;
        let RenewingOrRebindingInner {
            collected_advertise,
            client_id: _,
            non_temporary_addresses: _,
            server_id: _,
            dns_servers: _,
            start_time: _,
            retrans_timeout: _,
            solicit_max_rt: _,
        } = with_state(state);
        assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);

        // Assert renew is retransmitted on retransmission timeout.
        let actions = client.handle_timeout(ClientTimerType::Retransmission, time);
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
        {
            let RenewingOrRebindingInner {
                client_id,
                server_id,
                collected_advertise,
                dns_servers,
                solicit_max_rt,
                non_temporary_addresses: _,
                start_time: _,
                retrans_timeout: _,
            } = with_state(state);
            assert_eq!(client_id, &CLIENT_ID);
            assert_eq!(server_id[..], SERVER_ID[0]);
            assert_eq!(dns_servers, &[] as &[Ipv6Addr]);
            assert!(collected_advertise.is_empty(), "{:?}", collected_advertise);
            assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        }
        let expected_non_temporary_addresses_to_renew: HashMap<v6::IAID, Option<Ipv6Addr>> = (0..)
            .map(v6::IAID::new)
            .zip(non_temporary_addresses_to_assign.iter().map(
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
            message_type,
            &CLIENT_ID,
            expect_server_id.then(|| &SERVER_ID[0]),
            &[],
            &expected_non_temporary_addresses_to_renew,
        );
    }

    #[test_case(RENEW_TEST, &SERVER_ID[0], &SERVER_ID[0])]
    #[test_case(REBIND_TEST, &SERVER_ID[0], &SERVER_ID[0])]
    #[test_case(RENEW_TEST, &SERVER_ID[0], &SERVER_ID[1])]
    #[test_case(REBIND_TEST, &SERVER_ID[0], &SERVER_ID[1])]
    fn receive_reply_extends_lifetime(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state,
            allow_response_from_any_server,
        }: RenewRebindTest,
        original_server_id: &[u8; TEST_SERVER_ID_LEN],
        reply_server_id: &[u8],
    ) {
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            original_server_id.clone(),
            CONFIGURED_NON_TEMPORARY_ADDRESSES
                .into_iter()
                .map(TestIdentityAssociation::new_default)
                .collect(),
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state, rng: _ } = &client;
        let iaaddr_opts = (0..)
            .map(v6::IAID::new)
            .zip(CONFIGURED_NON_TEMPORARY_ADDRESSES)
            .map(|(iaid, addr)| {
                (
                    iaid,
                    v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
                        addr,
                        RENEWED_PREFERRED_LIFETIME.get(),
                        RENEWED_VALID_LIFETIME.get(),
                        &[],
                    )),
                )
            })
            .collect::<HashMap<_, _>>();
        let options =
            [v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(reply_server_id)]
                .into_iter()
                .chain((0..).map(v6::IAID::new).take(CONFIGURED_NON_TEMPORARY_ADDRESSES.len()).map(
                    |iaid| {
                        v6::DhcpOption::Iana(v6::IanaSerializer::new(
                            iaid,
                            RENEWED_T1.get(),
                            RENEWED_T2.get(),
                            std::slice::from_ref(iaaddr_opts.get(&iaid).unwrap()),
                        ))
                    },
                ))
                .collect::<Vec<_>>();
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

        // Make sure we are in renewing/rebinding before we handle the message.
        let original_state = with_state(state).clone();

        let actions = client.handle_message_receive(msg, time);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;

        if original_server_id.as_slice() != reply_server_id && !allow_response_from_any_server {
            // Renewing does not allow us to receive replies from a different
            // server but Rebinding does. If we aren't allowed to accept a
            // response from a different server, just make sure we are in the
            // same state.
            let RenewingOrRebindingInner {
                client_id: original_client_id,
                non_temporary_addresses: original_non_temporary_addresses,
                server_id: original_server_id,
                collected_advertise: original_collected_advertise,
                dns_servers: original_dns_servers,
                start_time: original_start_time,
                retrans_timeout: original_retrans_timeout,
                solicit_max_rt: original_solicit_max_rt,
            } = original_state;
            let RenewingOrRebindingInner {
                client_id: new_client_id,
                non_temporary_addresses: new_non_temporary_addresses,
                server_id: new_server_id,
                collected_advertise: new_collected_advertise,
                dns_servers: new_dns_servers,
                start_time: new_start_time,
                retrans_timeout: new_retrans_timeout,
                solicit_max_rt: new_solicit_max_rt,
            } = with_state(state);
            assert_eq!(&original_client_id, new_client_id);
            assert_eq!(&original_non_temporary_addresses, new_non_temporary_addresses);
            assert_eq!(&original_server_id, new_server_id);
            assert_eq!(
                original_collected_advertise.into_sorted_vec(),
                new_collected_advertise.clone().into_sorted_vec()
            );
            assert_eq!(&original_dns_servers, new_dns_servers);
            assert_eq!(&original_start_time, new_start_time);
            assert_eq!(&original_retrans_timeout, new_retrans_timeout);
            assert_eq!(&original_solicit_max_rt, new_solicit_max_rt);
            assert_eq!(actions, []);
            return;
        }

        let expected_non_temporary_addresses = (0..)
            .map(v6::IAID::new)
            .zip(CONFIGURED_NON_TEMPORARY_ADDRESSES.into_iter().map(|addr| {
                AddressEntry::new_assigned(addr, RENEWED_PREFERRED_LIFETIME, RENEWED_VALID_LIFETIME)
            }))
            .collect();
        assert_matches!(
            &state,
            Some(ClientState::Assigned(Assigned {
                client_id,
                non_temporary_addresses,
                server_id,
                collected_advertise: _,
                dns_servers,
                solicit_max_rt,
            })) if client_id == &CLIENT_ID &&
                   *non_temporary_addresses == expected_non_temporary_addresses &&
                   server_id.as_slice() == reply_server_id &&
                   dns_servers.as_slice() == &[] as &[Ipv6Addr] &&
                   *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1),
                Action::ScheduleTimer(ClientTimerType::Rebind, t2)
            ] => {
                assert_eq!(*t1, Duration::from_secs(RENEWED_T1.get().into()));
                assert_eq!(*t2, Duration::from_secs(RENEWED_T2.get().into()));
            }
        );
    }

    // Tests that receiving a Reply with an error status code other than
    // UseMulticast results in only SOL_MAX_RT being updated, with the rest
    // of the message contents ignored.
    #[test_case(RENEW_TEST, v6::ErrorStatusCode::UnspecFail)]
    #[test_case(RENEW_TEST, v6::ErrorStatusCode::NoBinding)]
    #[test_case(RENEW_TEST, v6::ErrorStatusCode::NotOnLink)]
    #[test_case(RENEW_TEST, v6::ErrorStatusCode::NoAddrsAvail)]
    #[test_case(RENEW_TEST, v6::ErrorStatusCode::NoPrefixAvail)]
    #[test_case(REBIND_TEST, v6::ErrorStatusCode::UnspecFail)]
    #[test_case(REBIND_TEST, v6::ErrorStatusCode::NoBinding)]
    #[test_case(REBIND_TEST, v6::ErrorStatusCode::NotOnLink)]
    #[test_case(REBIND_TEST, v6::ErrorStatusCode::NoAddrsAvail)]
    #[test_case(REBIND_TEST, v6::ErrorStatusCode::NoPrefixAvail)]
    fn renewing_receive_reply_with_error_status(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
        error_status_code: v6::ErrorStatusCode,
    ) {
        let time = Instant::now();
        let addr = CONFIGURED_NON_TEMPORARY_ADDRESSES[0];
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            vec![TestIdentityAssociation::new_default(addr)],
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let ia_na_options = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            addr,
            RENEWED_PREFERRED_LIFETIME.get(),
            RENEWED_VALID_LIFETIME.get(),
            &[],
        ))];
        let sol_max_rt = *VALID_MAX_SOLICIT_TIMEOUT_RANGE.start();
        let options = vec![
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::StatusCode(error_status_code.into(), ""),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                RENEWED_T1.get(),
                RENEWED_T2.get(),
                &ia_na_options,
            )),
            v6::DhcpOption::SolMaxRt(sol_max_rt),
        ];
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg, time);
        assert_eq!(actions, &[]);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let expected_non_temporary_addresses = std::iter::once((
            v6::IAID::new(0),
            AddressEntry::new_assigned(addr, PREFERRED_LIFETIME, VALID_LIFETIME),
        ))
        .collect::<HashMap<_, _>>();
        let RenewingOrRebindingInner {
            client_id,
            non_temporary_addresses,
            server_id,
            collected_advertise: _,
            dns_servers,
            start_time: _,
            retrans_timeout: _,
            solicit_max_rt: got_sol_max_rt,
        } = with_state(state);
        assert_eq!(*client_id, CLIENT_ID);
        assert_eq!(*non_temporary_addresses, expected_non_temporary_addresses);
        assert_eq!(*server_id, SERVER_ID[0]);
        assert_eq!(dns_servers, &[] as &[Ipv6Addr]);
        assert_eq!(*got_sol_max_rt, Duration::from_secs(sol_max_rt.into()));
        assert_matches!(&actions[..], []);
    }

    #[test_case(RENEW_TEST, v6::IAID::new(0))]
    #[test_case(RENEW_TEST, v6::IAID::new(1))]
    #[test_case(REBIND_TEST, v6::IAID::new(0))]
    #[test_case(REBIND_TEST, v6::IAID::new(1))]
    fn receive_reply_with_missing_ias(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
        present_iaid: v6::IAID,
    ) {
        let non_temporary_addresses = &CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2];
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            non_temporary_addresses
                .iter()
                .copied()
                .map(TestIdentityAssociation::new_default)
                .collect(),
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;

        // The server includes only the IA with ID equal to `present_iaid` in the
        // reply.
        let iaaddr_opt = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[present_iaid.get() as usize],
            RENEWED_PREFERRED_LIFETIME.get(),
            RENEWED_VALID_LIFETIME.get(),
            &[],
        ))];
        let options =
            [v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0])]
                .into_iter()
                .chain(std::iter::once({
                    v6::DhcpOption::Iana(v6::IanaSerializer::new(
                        present_iaid,
                        RENEWED_T1.get(),
                        RENEWED_T2.get(),
                        &iaaddr_opt,
                    ))
                }))
                .collect::<Vec<_>>();
        let builder =
            v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, options.as_slice());
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg, time);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        // Only the IA that is present will have its lifetimes updated.
        let expected_non_temporary_addresses = (0..)
            .map(v6::IAID::new)
            .zip(non_temporary_addresses)
            .map(|(iaid, &addr)| {
                (
                    iaid,
                    if iaid == present_iaid {
                        AddressEntry::new_assigned(
                            addr,
                            RENEWED_PREFERRED_LIFETIME,
                            RENEWED_VALID_LIFETIME,
                        )
                    } else {
                        AddressEntry::new_assigned(addr, PREFERRED_LIFETIME, VALID_LIFETIME)
                    },
                )
            })
            .collect();
        {
            let RenewingOrRebindingInner {
                client_id,
                non_temporary_addresses,
                server_id,
                collected_advertise: _,
                dns_servers,
                start_time,
                retrans_timeout: _,
                solicit_max_rt,
            } = with_state(state);
            assert_eq!(*client_id, CLIENT_ID);
            assert_eq!(*non_temporary_addresses, expected_non_temporary_addresses);
            assert_eq!(*server_id, SERVER_ID[0]);
            assert!(start_time.is_some());
            assert_eq!(dns_servers, &[] as &[Ipv6Addr]);
            assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        }
        // The client relies on retransmission to send another Renew, so no actions are needed.
        assert_matches!(&actions[..], []);
    }

    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    fn receive_reply_with_missing_ia_address_for_assigned_entry_does_not_extend_lifetime(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state: _,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]
                .iter()
                .copied()
                .map(TestIdentityAssociation::new_default)
                .collect(),
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let mut options =
            vec![v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0])];
        // The server includes an IA Address option in only one of the IAs.
        let iaaddr_opt_ia1 = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            RENEWED_PREFERRED_LIFETIME.get(),
            RENEWED_VALID_LIFETIME.get(),
            &[],
        ))];

        options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
            v6::IAID::new(0),
            RENEWED_T1.get(),
            RENEWED_T2.get(),
            &iaaddr_opt_ia1,
        )));
        options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
            v6::IAID::new(1),
            RENEWED_T1.get(),
            RENEWED_T2.get(),
            &[],
        )));
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg, time);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let expected_non_temporary_addresses = HashMap::from([
            (
                v6::IAID::new(0),
                AddressEntry::new_assigned(
                    CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
                    RENEWED_PREFERRED_LIFETIME,
                    RENEWED_VALID_LIFETIME,
                ),
            ),
            (
                v6::IAID::new(1),
                AddressEntry::new_assigned(
                    CONFIGURED_NON_TEMPORARY_ADDRESSES[1],
                    PREFERRED_LIFETIME,
                    VALID_LIFETIME,
                ),
            ),
        ]);
        // Expect the client to transition to Assigned and only extend
        // the lifetime for one IA.
        assert_matches!(
            &state,
            Some(ClientState::Assigned(Assigned{
                client_id,
                non_temporary_addresses,
                server_id,
                collected_advertise: _,
                dns_servers,
                solicit_max_rt,
            })) if *client_id == CLIENT_ID &&
                   *non_temporary_addresses == expected_non_temporary_addresses &&
                   *server_id == SERVER_ID[0] &&
                   dns_servers == &[] as &[Ipv6Addr] &&
                   *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1),
                Action::ScheduleTimer(ClientTimerType::Rebind, t2)
            ] => {
                assert_eq!(*t1, Duration::from_secs(RENEWED_T1.get().into()));
                assert_eq!(*t2, Duration::from_secs(RENEWED_T2.get().into()));
            }
        );
    }

    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    fn receive_reply_with_different_address(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state: _,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            vec![TestIdentityAssociation::new_default(CONFIGURED_NON_TEMPORARY_ADDRESSES[0])],
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let mut options =
            vec![v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0])];
        // The server includes an IA Address with a different address than
        // previously assigned.
        let iaaddr_opt_ia = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            RENEW_NON_TEMPORARY_ADDRESSES[0],
            RENEWED_PREFERRED_LIFETIME.get(),
            RENEWED_VALID_LIFETIME.get(),
            &[],
        ))];

        options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(
            v6::IAID::new(0),
            RENEWED_T1.get(),
            RENEWED_T2.get(),
            &iaaddr_opt_ia,
        )));
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg, time);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let expected_non_temporary_addresses = HashMap::from([(
            v6::IAID::new(0),
            AddressEntry::Assigned(AssignedIa::new(
                IdentityAssociation::new_finite(
                    RENEW_NON_TEMPORARY_ADDRESSES[0],
                    RENEWED_PREFERRED_LIFETIME,
                    RENEWED_VALID_LIFETIME,
                ),
                Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
            )),
        )]);
        // Expect the client to transition to Assigned and assign the new
        // address.
        assert_matches!(
            &state,
            Some(ClientState::Assigned(Assigned{
                client_id,
                non_temporary_addresses,
                server_id,
                collected_advertise: _,
                dns_servers,
                solicit_max_rt,
            })) if *client_id == CLIENT_ID &&
                   *non_temporary_addresses == expected_non_temporary_addresses &&
                   *server_id == SERVER_ID[0] &&
                   dns_servers == &[] as &[Ipv6Addr] &&
                   *solicit_max_rt == MAX_SOLICIT_TIMEOUT
        );
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1),
                Action::ScheduleTimer(ClientTimerType::Rebind, t2)
                // TODO(https://fxbug.dev/96674): expect initial address is
                // removed.
                // TODO(https://fxbug.dev/95265): expect new address is added.
            ] => {
                assert_eq!(*t1, Duration::from_secs(RENEWED_T1.get().into()));
                assert_eq!(*t2, Duration::from_secs(RENEWED_T2.get().into()));
            }
        );
    }

    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    fn no_binding(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state: _,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0..2]
                .iter()
                .copied()
                .map(TestIdentityAssociation::new_default)
                .collect(),
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;

        // Build a reply with NoBinding status in one of the IA options.
        let iaaddr_opts = [
            vec![v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
                CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
                RENEWED_PREFERRED_LIFETIME.get(),
                RENEWED_VALID_LIFETIME.get(),
                &[],
            ))],
            vec![v6::DhcpOption::StatusCode(
                v6::ErrorStatusCode::NoBinding.into(),
                "Binding not found.",
            )],
        ];
        let options = (0..2)
            .map(|id| {
                v6::DhcpOption::Iana(v6::IanaSerializer::new(
                    v6::IAID::new(id),
                    RENEWED_T1.get(),
                    RENEWED_T2.get(),
                    &iaaddr_opts[id as usize],
                ))
            })
            .chain([v6::DhcpOption::ClientId(&CLIENT_ID), v6::DhcpOption::ServerId(&SERVER_ID[0])])
            .collect::<Vec<_>>();

        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg, time);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        let expected_non_temporary_addresses = HashMap::from([
            (
                v6::IAID::new(0),
                AddressEntry::new_assigned(
                    CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
                    RENEWED_PREFERRED_LIFETIME,
                    RENEWED_VALID_LIFETIME,
                ),
            ),
            // Expect that the address entry for the IA with NoBinding status is
            // no longer assigned, and replaced by an address entry to be
            // requested in subsequent messages.
            (
                v6::IAID::new(1),
                AddressEntry::ToRequest(AddressToRequest::new(
                    Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[1]),
                    Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[1]),
                )),
            ),
        ]);
        // Expect the client to transition to Requesting.
        {
            let Requesting {
                client_id,
                non_temporary_addresses,
                server_id,
                collected_advertise: _,
                first_request_time: _,
                retrans_timeout: _,
                retrans_count: _,
                solicit_max_rt,
            } = assert_matches!(
                &state,
                Some(ClientState::Requesting(requesting)) => requesting
            );
            assert_eq!(*client_id, CLIENT_ID);
            assert_eq!(*non_temporary_addresses, expected_non_temporary_addresses);
            assert_eq!(*server_id, SERVER_ID[0]);
            assert_eq!(*solicit_max_rt, MAX_SOLICIT_TIMEOUT);
        }
        let mut buf = assert_matches!(
            &actions[..],
            [
                // TODO(https://fxbug.dev/96674): should include action to
                // remove the address of IA with NoBinding status.
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, INITIAL_REQUEST_TIMEOUT)
            ] => buf
        );
        // Expect that the Request message contains both the assigned address
        // and the address to request.
        testutil::assert_outgoing_stateful_message(
            &mut buf,
            v6::MessageType::Request,
            &CLIENT_ID,
            Some(&SERVER_ID[0]),
            &[],
            &(0..2)
                .map(v6::IAID::new)
                .zip(CONFIGURED_NON_TEMPORARY_ADDRESSES.into_iter().map(Some))
                .collect(),
        );
    }

    // Tests that a Reply message containing:
    //
    // 1. an IA_NA with IA Address with renewed lifetimes,
    // 2. an IA_NA with IA Address with the NoAddrsAvail status code, and
    // 3. an IA_NA with no options;
    //
    // results in T1 and T2 being calculated solely based on the first IA_NA.
    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    fn receive_reply_calculate_t1_t2(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state: _,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            CONFIGURED_NON_TEMPORARY_ADDRESSES
                .into_iter()
                .map(TestIdentityAssociation::new_default)
                .collect(),
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );
        let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
            &client;
        let ia_addr = [v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(
            CONFIGURED_NON_TEMPORARY_ADDRESSES[0],
            RENEWED_PREFERRED_LIFETIME.get(),
            RENEWED_VALID_LIFETIME.get(),
            &[],
        ))];
        let ia_no_addrs_avail = [v6::DhcpOption::StatusCode(
            v6::ErrorStatusCode::NoAddrsAvail.into(),
            "No address available.",
        )];
        let options = vec![
            v6::DhcpOption::ClientId(&CLIENT_ID),
            v6::DhcpOption::ServerId(&SERVER_ID[0]),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(0),
                RENEWED_T1.get(),
                RENEWED_T2.get(),
                &ia_addr,
            )),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(1),
                // If the server returns an IA with status code indicating failure, the T1/T2
                // values for that IA should not be included in the T1/T2 calculation.
                T1.get(),
                T2.get(),
                &ia_no_addrs_avail,
            )),
            v6::DhcpOption::Iana(v6::IanaSerializer::new(
                v6::IAID::new(2),
                // If the server returns an IA with no IA Address option, the T1/T2 values
                // for that IA should not be included in the T1/T2 calculation.
                T1.get(),
                T2.get(),
                &[],
            )),
        ];

        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        let actions = client.handle_message_receive(msg, time);
        assert_matches!(
            &actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Renew, t1),
                Action::ScheduleTimer(ClientTimerType::Rebind, t2)
            ] => {
                assert_eq!(*t1, Duration::from_secs(RENEWED_T1.get().into()));
                assert_eq!(*t2, Duration::from_secs(RENEWED_T2.get().into()));
            }
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

        assert!(client.handle_message_receive(msg, Instant::now()).is_empty());

        // Messages with unsupported/unexpected types are discarded.
        for msg_type in [
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
        ] {
            let ClientStateMachine { transaction_id, options_to_request: _, state: _, rng: _ } =
                &client;
            let builder = v6::MessageBuilder::new(msg_type, *transaction_id, &[]);
            let mut buf = vec![0; builder.bytes_len()];
            builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

            assert!(client.handle_message_receive(msg, Instant::now()).is_empty());
        }
    }

    #[test]
    #[should_panic(expected = "received unexpected refresh timeout")]
    fn information_requesting_refresh_timeout_is_unreachable() {
        let (mut client, _) = ClientStateMachine::start_stateless(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // Should panic if Refresh timeout is received while in
        // InformationRequesting state.
        let _actions = client.handle_timeout(ClientTimerType::Refresh, Instant::now());
    }

    #[test]
    #[should_panic(expected = "received unexpected retransmission timeout")]
    fn information_received_retransmission_timeout_is_unreachable() {
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

        let options = [v6::DhcpOption::ServerId(&SERVER_ID[0])];
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, *transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        // Transition to InformationReceived state.
        let time = Instant::now();
        let actions = client.handle_message_receive(msg, time);
        let ClientStateMachine { transaction_id: _, options_to_request: _, state, rng: _ } =
            &client;
        assert_matches!(
            state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers}))
                if dns_servers.is_empty()
        );
        assert_eq!(
            actions[..],
            [
                Action::CancelTimer(ClientTimerType::Retransmission),
                Action::ScheduleTimer(ClientTimerType::Refresh, IRT_DEFAULT)
            ]
        );

        // Should panic if Retransmission timeout is received while in
        // InformationReceived state.
        let _actions = client.handle_timeout(ClientTimerType::Retransmission, time);
    }

    #[test]
    #[should_panic(expected = "received unexpected refresh timeout")]
    fn server_discovery_refresh_timeout_is_unreachable() {
        let time = Instant::now();
        let mut client = testutil::start_and_assert_server_discovery(
            [0, 1, 2],
            v6::duid_uuid(),
            testutil::to_configured_addresses(
                1,
                std::iter::once(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]),
            ),
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        // Should panic if Refresh is received while in ServerDiscovery state.
        let _actions = client.handle_timeout(ClientTimerType::Refresh, time);
    }

    #[test]
    #[should_panic(expected = "received unexpected refresh timeout")]
    fn requesting_refresh_timeout_is_unreachable() {
        let time = Instant::now();
        let (mut client, _transaction_id) = testutil::request_addresses_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            vec![TestIdentityAssociation::new_default(CONFIGURED_NON_TEMPORARY_ADDRESSES[0])],
            &[],
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        // Should panic if Refresh is received while in Requesting state.
        let _actions = client.handle_timeout(ClientTimerType::Refresh, time);
    }

    #[test_case(ClientTimerType::Refresh)]
    #[test_case(ClientTimerType::Retransmission)]
    #[should_panic(expected = "received unexpected")]
    fn address_assiged_unexpected_timeout_is_unreachable(timeout: ClientTimerType) {
        let time = Instant::now();
        let (mut client, _actions) = testutil::assign_addresses_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            vec![TestIdentityAssociation::new_default(CONFIGURED_NON_TEMPORARY_ADDRESSES[0])],
            &[],
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        // Should panic if Refresh or Retransmission timeout is received while
        // in Assigned state.
        let _actions = client.handle_timeout(timeout, time);
    }

    #[test_case(RENEW_TEST)]
    #[test_case(REBIND_TEST)]
    #[should_panic(expected = "received unexpected refresh timeout")]
    fn refresh_timeout_is_unreachable(
        RenewRebindTest {
            send_and_assert,
            message_type: _,
            expect_server_id: _,
            with_state: _,
            allow_response_from_any_server: _,
        }: RenewRebindTest,
    ) {
        let time = Instant::now();
        let mut client = send_and_assert(
            CLIENT_ID,
            SERVER_ID[0],
            vec![TestIdentityAssociation::new_default(CONFIGURED_NON_TEMPORARY_ADDRESSES[0])],
            None,
            T1,
            T2,
            StepRng::new(std::u64::MAX / 2, 0),
            time,
        );

        // Should panic if Refresh is received while in Renewing state.
        let _actions = client.handle_timeout(ClientTimerType::Refresh, time);
    }

    // NOTE: All comparisons are done on millisecond, so this test is not affected by precision
    // loss from floating point arithmetic.
    #[test]
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
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        ))
     )]
    #[test_case(
        v6::TimeValue::Zero,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity)
    )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )),
        v6::TimeValue::Zero,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        ))
     )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(60)
                .expect("should succeed for non-zero or u32::MAX values")
        )),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(60)
                .expect("should succeed for non-zero or u32::MAX values")
        ))
     )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        ))
     )]
    #[test_case(
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        ))
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
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        ),
        v6::TimeValue::Zero,
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )
    )]
    #[test_case(
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        ),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(60)
                .expect("should succeed for non-zero or u32::MAX values")
        )),
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(60)
                .expect("should succeed for non-zero or u32::MAX values")
        )
    )]
    #[test_case(
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        ),
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Infinity),
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )
    )]
    #[test_case(
        v6::NonZeroTimeValue::Infinity,
        v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values"))
        ),
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(120)
                .expect("should succeed for non-zero or u32::MAX values")
        )
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
        v6::NonZeroTimeValue::Finite(
            v6::NonZeroOrMaxU32::new(INFINITY - 1)
                .expect("should succeed")
        ),
        T2_T1_RATIO,
        v6::NonZeroTimeValue::Infinity
    )]
    fn compute_t(min: v6::NonZeroTimeValue, ratio: Ratio<u32>, expected_t: v6::NonZeroTimeValue) {
        assert_eq!(super::compute_t(min, ratio), expected_t);
    }

    #[test_case(None, None, false)]
    #[test_case(None, Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), true)]
    #[test_case(Some(REPLY_NON_TEMPORARY_ADDRESSES[0]), None, true)]
    #[test_case(Some(REPLY_NON_TEMPORARY_ADDRESSES[0]), Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), true)]
    #[test_case(Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), false)]
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

    #[test_case(None, Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]))]
    #[test_case(Some(REPLY_NON_TEMPORARY_ADDRESSES[0]), None)]
    #[test_case(Some(REPLY_NON_TEMPORARY_ADDRESSES[0]), Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]))]
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

    #[test_case(None, Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]))]
    #[test_case(Some(REPLY_NON_TEMPORARY_ADDRESSES[0]), None)]
    #[test_case(Some(REPLY_NON_TEMPORARY_ADDRESSES[0]), Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]))]
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

    #[test_case(REPLY_NON_TEMPORARY_ADDRESSES[0], None, true)]
    #[test_case(REPLY_NON_TEMPORARY_ADDRESSES[0], Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), true)]
    #[test_case(CONFIGURED_NON_TEMPORARY_ADDRESSES[0], Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]), false)]
    fn create_non_configured_ia(
        address: Ipv6Addr,
        configured_address: Option<Ipv6Addr>,
        expect_ia_is_some: bool,
    ) {
        let ia = IdentityAssociation {
            address,
            preferred_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(
                PREFERRED_LIFETIME,
            )),
            valid_lifetime: v6::TimeValue::NonZero(v6::NonZeroTimeValue::Finite(VALID_LIFETIME)),
        };
        let non_conf_ia_opt = NonConfiguredIa::new(ia, configured_address);
        assert_eq!(non_conf_ia_opt.is_some(), expect_ia_is_some);
        if let Some(non_conf_ia) = non_conf_ia_opt {
            assert_eq!(non_conf_ia.address(), address);
            assert_eq!(non_conf_ia.configured_address(), configured_address);
        }
    }

    #[test]
    fn create_assigned_ia() {
        let ia1 = IdentityAssociation::new_default(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]);
        let assigned_ia = AssignedIa::new(ia1, Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]));
        assert_eq!(assigned_ia, AssignedIa::Configured(ia1));
        assert_eq!(assigned_ia.address(), CONFIGURED_NON_TEMPORARY_ADDRESSES[0]);

        let ia2 = IdentityAssociation::new_default(REPLY_NON_TEMPORARY_ADDRESSES[0]);
        let assigned_ia = AssignedIa::new(ia2, Some(CONFIGURED_NON_TEMPORARY_ADDRESSES[0]));
        assert_matches!(&assigned_ia, AssignedIa::NonConfigured(_non_conf_ia));
        assert_eq!(assigned_ia.address(), REPLY_NON_TEMPORARY_ADDRESSES[0]);

        let assigned_ia = AssignedIa::new(ia2, None);
        assert_matches!(&assigned_ia, AssignedIa::NonConfigured(_non_conf_ia));
        assert_eq!(assigned_ia.address(), REPLY_NON_TEMPORARY_ADDRESSES[0]);
    }
}

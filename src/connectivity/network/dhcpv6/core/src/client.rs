// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Core DHCPv6 client state transitions.

use std::collections::HashMap;

use {
    matches::assert_matches,
    packet::serialize::InnerPacketBuilder,
    packet_formats_dhcp::v6,
    rand::Rng,
    std::{convert::TryFrom, default::Default, net::Ipv6Addr, time::Duration, time::Instant},
    zerocopy::ByteSlice,
};

/// Initial Information-request timeout `INF_TIMEOUT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const INFO_REQ_TIMEOUT: Duration = Duration::from_secs(1);
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
const SOLICIT_TIMEOUT: Duration = Duration::from_secs(1);

/// Max Solicit timeout `SOL_MAX_RT` from [RFC 8415, Section 7.6].
///
/// [RFC 8415, Section 7.6]: https://tools.ietf.org/html/rfc8415#section-7.6
const SOLICIT_MAX_RT: Duration = Duration::from_secs(3600);

/// Denominator used for transforming the elapsed time from milliseconds to
/// hundredths of a second.
///
/// [RFC 8415, Section 21.9]: https://tools.ietf.org/html/rfc8415#section-21.9
const ELAPSED_TIME_DENOMINATOR: u128 = 10;

/// The length of the [Client Identifier].
///
/// [Client Identifier]: https://datatracker.ietf.org/doc/html/rfc8415#section-21.2
const CLIENT_ID_LEN: usize = 18;

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
    let rand = rng.gen_range(-0.1, 0.1);

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
#[derive(Debug, PartialEq)]
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
        retransmission_timeout(self.retrans_timeout, INFO_REQ_TIMEOUT, INFO_REQ_MAX_RT, rng)
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

        (
            ClientState::InformationRequesting(InformationRequesting { retrans_timeout }),
            vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
        )
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
                // TODO(https://fxbug.dev/48867): emit more actions for other options received.
                _ => (),
            }
        }

        let actions = std::array::IntoIter::new([
            Action::CancelTimer(ClientTimerType::Retransmission),
            Action::ScheduleTimer(ClientTimerType::Refresh, information_refresh_time),
        ])
        .chain(dns_servers.clone().map(|server_addrs| Action::UpdateDnsServers(server_addrs)))
        .collect::<Vec<_>>();

        (
            ClientState::InformationReceived(InformationReceived {
                dns_servers: dns_servers.unwrap_or(Vec::new()),
            }),
            actions,
        )
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
        rng: &mut R,
    ) -> Transition {
        let server_discovery = ServerDiscovery {
            client_id,
            configured_addresses,
            first_solicit_time: None,
            retrans_timeout: Duration::default(),
        };
        server_discovery.send_and_schedule_retransmission(transaction_id, options_to_request, rng)
    }

    /// Calculates timeout for retransmitting solicits, as specified in
    /// [RFC 8415, Section 18.2.1].
    ///
    /// [RFC 8415, Section 18.2.1]: https://datatracker.ietf.org/doc/html/rfc8415#section-18.2.1
    fn retransmission_timeout<R: Rng>(&self, rng: &mut R) -> Duration {
        // TODO(fxbug.dev/69696): implement server selection.
        retransmission_timeout(self.retrans_timeout, SOLICIT_TIMEOUT, SOLICIT_MAX_RT, rng)
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
            Some(first_solicit_time) => u16::try_from(
                Instant::now()
                    .duration_since(first_solicit_time)
                    .as_millis()
                    .checked_div(ELAPSED_TIME_DENOMINATOR)
                    .expect("division should succeed"),
            )
            .unwrap_or(u16::MAX),
        };
        options.push(v6::DhcpOption::ElapsedTime(elapsed_time));

        let mut address_hint = HashMap::new();
        for (iaid, addr_opt) in &self.configured_addresses {
            let entry = address_hint.insert(
                *iaid,
                match addr_opt {
                    None => vec![],
                    Some(addr) => {
                        vec![v6::DhcpOption::IaAddr(v6::IaAddrSerializer::new(*addr, 0, 0, &[]))]
                    }
                },
            );
            assert_matches!(entry, None);
        }

        // Adds IA_NA options: one IA_NA per address hint, plus IA_NA options
        // without hints, up to the configured `address_count`, as described in
        // https://datatracker.ietf.org/doc/html/rfc8415#section-6.6.
        for (iaid, addr_hint) in &address_hint {
            options.push(v6::DhcpOption::Iana(v6::IanaSerializer::new(*iaid, 0, 0, addr_hint)));
        }

        let mut oro = vec![v6::OptionCode::SolMaxRt];
        oro.extend_from_slice(options_to_request);
        options.push(v6::DhcpOption::Oro(&oro));

        let builder = v6::MessageBuilder::new(v6::MessageType::Solicit, transaction_id, &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);

        let retrans_timeout = self.retransmission_timeout(rng);
        let first_solicit_time = self.first_solicit_time.unwrap_or(Instant::now());

        (
            ClientState::ServerDiscovery(ServerDiscovery {
                client_id: self.client_id,
                configured_addresses: self.configured_addresses,
                first_solicit_time: Some(first_solicit_time),
                retrans_timeout,
            }),
            vec![
                Action::SendMessage(buf),
                Action::ScheduleTimer(ClientTimerType::Retransmission, retrans_timeout),
            ],
        )
    }
}

/// All possible states of a DHCPv6 client.
///
/// States not found in this enum are not supported yet.
#[derive(Debug)]
enum ClientState {
    /// Creating and (re)transmitting an information request, and waiting for a reply.
    InformationRequesting(InformationRequesting),
    /// Client is waiting to refresh, after receiving a valid reply to a previous information
    /// request.
    InformationReceived(InformationReceived),
    /// Sending solicit messages, collecting advertise messages, and selecting
    /// a server from which to obtain addresses and other optional
    /// configuration information.
    ServerDiscovery(ServerDiscovery),
}

/// Defines the next state, and the actions the client should take to transition to that state.
type Transition = (ClientState, Actions);

impl ClientState {
    /// Dispatches reply message received event based on the current state (self).
    ///
    /// Consumes `self` and returns the transition the client should take.
    fn reply_message_received<B: ByteSlice>(self, msg: v6::Message<'_, B>) -> Transition {
        match self {
            ClientState::InformationRequesting(s) => s.reply_message_received(msg),
            state => (state, Vec::new()),
        }
    }

    /// Dispatches retransmission timer expired event based on the current state (self).
    ///
    /// Consumes `self` and returns the transition the client should take.
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
            state => (state, Vec::new()),
        }
    }

    /// Dispatches refresh timer expired event based on the current state (self).
    ///
    /// Consumes `self` and returns the transition the client should take.
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
            state => (state, Vec::new()),
        }
    }

    fn get_dns_servers(&self) -> Vec<Ipv6Addr> {
        match self {
            ClientState::InformationReceived(s) => s.dns_servers.clone(),
            _ => Vec::new(),
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
        let (state, actions) =
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
        let (state, actions) = ServerDiscovery::start(
            transaction_id,
            client_id,
            configured_addresses,
            &options_to_request,
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
        let (new_state, actions) = match timeout_type {
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
        self.state = Some(new_state);
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
                    let (new_state, actions) = self
                        .state
                        .take()
                        .expect("state should not be empty")
                        .reply_message_received(msg);
                    self.state = Some(new_state);
                    actions
                }
                v6::MessageType::Advertise => {
                    // TODO(jayzhuang): support Advertise messages when needed.
                    // https://tools.ietf.org/html/rfc8415#section-18.2.9
                    Vec::new()
                }
                v6::MessageType::Reconfigure => {
                    // TODO(jayzhuang): support Reconfigure messages when needed.
                    // https://tools.ietf.org/html/rfc8415#section-18.2.11
                    Vec::new()
                }
                _ => {
                    // Ignore unexpected message types.
                    Vec::new()
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, net_declare::std_ip_v6, packet::ParsablePacket, rand::rngs::mock::StepRng};

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
                    retrans_timeout: INFO_REQ_TIMEOUT,
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
                    Action::ScheduleTimer(ClientTimerType::Retransmission, INFO_REQ_TIMEOUT)
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

    #[test]
    fn test_solicit() {
        // Try to start the client in stateful mode with different address
        // configurations.
        for (address_count, preferred_addresses) in vec![
            (1u32, Vec::new()),
            (2u32, vec![Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff)]),
            (
                2u32,
                vec![
                    Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff),
                    Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x3ff),
                ],
            ),
        ] {
            let client_id = v6::duid_uuid();
            let mut configured_addresses: HashMap<u32, Option<Ipv6Addr>> = HashMap::new();
            let addresses: Vec<Option<Ipv6Addr>> =
                preferred_addresses.iter().map(|&addr| Some(addr)).collect();
            let addresses = addresses
                .into_iter()
                .chain(std::iter::repeat(None))
                .take(usize::try_from(address_count).unwrap());
            for (iaid, addr) in (0..).zip(addresses) {
                let entry = configured_addresses.insert(iaid, addr);
                assert_matches!(entry, None);
            }

            let (client, actions) = ClientStateMachine::start_stateful(
                [0, 1, 2],
                client_id.clone(),
                configured_addresses,
                Vec::new(),
                StepRng::new(std::u64::MAX / 2, 0),
            );

            assert_matches!(
                client.state,
                Some(ClientState::ServerDiscovery(ServerDiscovery {
                    client_id: ref state_client_id,
                    first_solicit_time: Some(_),
                    retrans_timeout: SOLICIT_TIMEOUT,
                    ..
                })) if *state_client_id == client_id
            );

            // Start of server discovery should send a solicit and schedule a
            // retransmission timer.
            assert_matches!(
                &actions[..],
                [
                    Action::SendMessage(buf),
                    Action::ScheduleTimer(ClientTimerType::Retransmission, SOLICIT_TIMEOUT)
                ]
                if {
                    let mut buf = &buf[..];
                    let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
                    assert_eq!(msg.msg_type(), v6::MessageType::Solicit);

                    // The solicit should contain the expected options.
                    let mut got_options = msg
                        .options()
                        .filter(|opt| {
                            vec![
                                v6::OptionCode::ClientId,
                                v6::OptionCode::ElapsedTime,
                                v6::OptionCode::Oro,
                            ]
                            .contains(&opt.code())
                        })
                        .collect::<Vec<_>>();
                    let option_sorter: fn(
                        &v6::ParsedDhcpOption<'_>,
                        &v6::ParsedDhcpOption<'_>,
                    ) -> std::cmp::Ordering =
                        |opt1, opt2| (u16::from(opt1.code())).cmp(&(u16::from(opt2.code())));

                    got_options.sort_by(option_sorter);
                    let mut expected_options = vec![
                        v6::ParsedDhcpOption::ClientId(&client_id),
                        v6::ParsedDhcpOption::ElapsedTime(0),
                        v6::ParsedDhcpOption::Oro(vec![v6::OptionCode::SolMaxRt]),
                    ];
                    expected_options.sort_by(option_sorter);
                    assert_eq!(got_options, expected_options);

                    let iana_options = msg.options().filter(|opt| {
                        opt.code() == v6::OptionCode::Iana
                    }).collect::<Vec<_>>();
                    assert_eq!(usize::try_from(address_count).unwrap(), iana_options.len());
                    let mut got_preferred_addresses = Vec::new();
                    for option in iana_options {
                        let iana_data = if let v6::ParsedDhcpOption::Iana(iana_data) = option {
                            iana_data
                        } else {
                            continue;
                        };
                        // Each IANA option should at most one IA Address option.
                        assert!(iana_data.iter_options().count() <= 1);
                        for iana_option in iana_data.iter_options() {
                            if let v6::ParsedDhcpOption::IaAddr(iaaddr_data) = iana_option {
                                got_preferred_addresses.push(iaaddr_data.addr());
                            }
                        }
                    }
                    got_preferred_addresses.sort();
                    assert_eq!(got_preferred_addresses, preferred_addresses);
                    true
                }
            );
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
                retrans_timeout: INFO_REQ_TIMEOUT
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
            client.state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers : ref d})) if *d == Vec::<Ipv6Addr>::new()
        );

        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        // Extra replies received in information received state are ignored.
        assert_eq!(client.handle_message_receive(msg)[..], []);
        assert_matches!(
            client.state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers : ref d})) if *d == Vec::<Ipv6Addr>::new()
        );

        // Information received state should only respond to `Refresh` timer.
        assert_eq!(client.handle_timeout(ClientTimerType::Retransmission)[..], []);
        assert_matches!(
            client.state,
            Some(ClientState::InformationReceived(InformationReceived { dns_servers : ref d})) if *d == Vec::<Ipv6Addr>::new()
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
            [_, Action::ScheduleTimer(ClientTimerType::Retransmission, INFO_REQ_TIMEOUT)]
        );

        let actions = client.handle_timeout(ClientTimerType::Retransmission);
        // Following exponential backoff defined in https://tools.ietf.org/html/rfc8415#section-15.
        assert_matches!(
            actions[..],
            [_, Action::ScheduleTimer(ClientTimerType::Retransmission, timeout)] if timeout == 2 * INFO_REQ_TIMEOUT
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
                Action::ScheduleTimer(ClientTimerType::Retransmission, INFO_REQ_TIMEOUT)
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

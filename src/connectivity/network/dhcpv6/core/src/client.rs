// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Core DHCPv6 client state transitions.

use {
    packet::serialize::InnerPacketBuilder,
    packet_formats_dhcp::v6,
    rand::Rng,
    std::{default::Default, net::Ipv6Addr, time::Duration},
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
#[derive(Debug, Default)]
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
        let info_req: Self = Default::default();
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
        let options = if options_to_request.is_empty() {
            Vec::new()
        } else {
            vec![v6::DhcpOption::Oro(options_to_request.to_vec())]
        };

        let builder =
            v6::MessageBuilder::new(v6::MessageType::InformationRequest, transaction_id, &options);
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
                v6::DhcpOption::InformationRefreshTime(refresh_time) => {
                    information_refresh_time = Duration::from_secs(u64::from(refresh_time))
                }
                v6::DhcpOption::DnsServers(server_addrs) => dns_servers = Some(server_addrs),
                // TODO(https://fxbug.dev/48867): emit more actions for other options received.
                _ => (),
            }
        }

        let actions = vec![
            Action::CancelTimer(ClientTimerType::Retransmission),
            Action::ScheduleTimer(ClientTimerType::Refresh, information_refresh_time),
        ]
        .into_iter()
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
            state => (state, vec![]),
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
            state => (state, vec![]),
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
            state => (state, vec![]),
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
    /// [Transaction ID](https://tools.ietf.org/html/rfc8415#section-16.1) the client is using to
    /// communicate with servers.
    transaction_id: [u8; 3],
    /// Options to include in
    /// [Option Request Option](https://tools.ietf.org/html/rfc8415#section-21.7).
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
    /// Starts the client to send information requests and respond to replies. The client will
    /// operate in the Stateless DHCP model defined in [RFC 8415, Section 6.1].
    ///
    /// [RFC 8415, Section 6.1]: https://tools.ietf.org/html/rfc8415#section-6.1
    pub fn start_information_request(
        transaction_id: [u8; 3],
        options_to_request: Vec<v6::OptionCode>,
        mut rng: R,
    ) -> (Self, Actions) {
        let (state, actions) =
            InformationRequesting::start(transaction_id, &options_to_request, &mut rng);
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
    use {
        super::*, matches::assert_matches, net_declare::std_ip_v6, packet::ParsablePacket,
        rand::rngs::mock::StepRng,
    };

    #[test]
    fn test_information_request_and_reply() {
        // Try to start information request with different list of requested options.
        for options in vec![
            Vec::new(),
            vec![v6::OptionCode::DnsServers],
            vec![v6::OptionCode::DnsServers, v6::OptionCode::DomainList],
        ] {
            let (mut client, actions) = ClientStateMachine::start_information_request(
                [0, 1, 2],
                options.clone(),
                StepRng::new(std::u64::MAX / 2, 0),
            );

            assert_matches!(client.state, Some(ClientState::InformationRequesting(_)));

            // Start of information requesting should send a information request and schedule a
            // retransmission timer.
            let want_options = if options.is_empty() {
                Vec::new()
            } else {
                vec![v6::DhcpOption::Oro(options.clone())]
            };
            let builder = v6::MessageBuilder::new(
                v6::MessageType::InformationRequest,
                client.transaction_id,
                &want_options,
            );
            let mut want_buf = vec![0; builder.bytes_len()];
            let () = builder.serialize(&mut want_buf);
            assert_eq!(
                actions,
                vec![
                    Action::SendMessage(want_buf),
                    Action::ScheduleTimer(ClientTimerType::Retransmission, INFO_REQ_TIMEOUT)
                ]
            );

            let test_dhcp_refresh_time = 42u32;
            let options = [
                v6::DhcpOption::InformationRefreshTime(test_dhcp_refresh_time),
                v6::DhcpOption::DnsServers(vec![
                    std_ip_v6!("ff01::0102"),
                    std_ip_v6!("ff01::0304"),
                ]),
            ];
            let builder =
                v6::MessageBuilder::new(v6::MessageType::Reply, client.transaction_id, &options);
            let mut buf = vec![0; builder.bytes_len()];
            let () = builder.serialize(&mut buf);
            let mut buf = &buf[..]; // Implements BufferView.
            let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");

            let actions = client.handle_message_receive(msg);

            assert_matches!(client.state, Some(ClientState::InformationReceived(_)));
            // Upon receiving a valid reply, client should set up for refresh based on the reply.
            assert_eq!(
                actions,
                vec![
                    Action::CancelTimer(ClientTimerType::Retransmission),
                    Action::ScheduleTimer(
                        ClientTimerType::Refresh,
                        Duration::from_secs(u64::from(test_dhcp_refresh_time)),
                    ),
                    Action::UpdateDnsServers(vec![
                        std_ip_v6!("ff01::0102"),
                        std_ip_v6!("ff01::0304")
                    ]),
                ]
            );
        }
    }

    #[test]
    fn test_unexpected_messages_are_ignored() {
        let (mut client, _) = ClientStateMachine::start_information_request(
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
        for msg_type in vec![
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
        let (mut client, _) = ClientStateMachine::start_information_request(
            [0, 1, 2],
            Vec::new(),
            StepRng::new(std::u64::MAX / 2, 0),
        );

        // The client expects either a reply or retransmission timeout in the current state.
        client.handle_timeout(ClientTimerType::Refresh);
        assert_matches!(client.state, Some(ClientState::InformationRequesting(_)));

        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, client.transaction_id, &[]);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        // Transition to InformationReceived state.
        client.handle_message_receive(msg);
        assert_matches!(client.state, Some(ClientState::InformationReceived(_)));

        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let mut buf = &buf[..]; // Implements BufferView.
        let msg = v6::Message::parse(&mut buf, ()).expect("failed to parse test buffer");
        // Extra replies received in information received state are ignored.
        client.handle_message_receive(msg);
        assert_matches!(client.state, Some(ClientState::InformationReceived(_)));

        // Information received state should only respond to `Refresh` timer.
        client.handle_timeout(ClientTimerType::Retransmission);
        assert_matches!(client.state, Some(ClientState::InformationReceived(_)));
    }

    #[test]
    fn test_information_request_retransmission() {
        let (mut client, actions) = ClientStateMachine::start_information_request(
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
        let (mut client, _) = ClientStateMachine::start_information_request(
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
        client.handle_message_receive(msg);

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
            actions,
            vec![
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

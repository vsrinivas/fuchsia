// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Implements a DHCPv6 client.
use {
    anyhow::{Context as _, Result},
    dhcpv6_core,
    dns_server_watcher::DEFAULT_DNS_PORT,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_net as fnet,
    fidl_fuchsia_net_dhcpv6::{
        ClientMarker, ClientRequest, ClientRequestStream, ClientWatchServersResponder,
        NewClientParams, OperationalModels, RequestableOptionCode, Stateless,
        RELAY_AGENT_AND_SERVER_LINK_LOCAL_MULTICAST_ADDRESS, RELAY_AGENT_AND_SERVER_PORT,
    },
    fidl_fuchsia_net_ext as fnetext, fidl_fuchsia_net_name as fnetname, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    futures::{
        future::{AbortHandle, Abortable},
        select, stream,
        stream::futures_unordered::FuturesUnordered,
        task, Future, FutureExt as _, StreamExt as _, TryStreamExt as _,
    },
    packet::ParsablePacket,
    packet_formats_dhcp::v6,
    rand::{rngs::StdRng, thread_rng, FromEntropy, Rng},
    std::{
        collections::{
            hash_map::{DefaultHasher, Entry},
            HashMap,
        },
        convert::TryFrom,
        hash::{Hash, Hasher},
        net::{IpAddr, Ipv6Addr, SocketAddr},
        num::TryFromIntError,
        pin::Pin,
        str::FromStr as _,
        time::Duration,
    },
};

#[derive(Debug, thiserror::Error)]
pub enum ClientError {
    #[error("no timer scheduled for {:?}", _0)]
    MissingTimer(dhcpv6_core::client::ClientTimerType),
    #[error("a timer is already scheduled for {:?}", _0)]
    TimerAlreadyExist(dhcpv6_core::client::ClientTimerType),
    #[error("fidl error: {}", _0)]
    Fidl(fidl::Error),
    #[error("got watch request while the previous one is pending")]
    DoubleWatch(),
    #[error("unsupported DHCPv6 operational models: {:?}, only stateless is supported", _0)]
    UnsupportedModels(OperationalModels),
    #[error("socket receive error: {:?}", _0)]
    SocketRecv(std::io::Error),
}

/// Theoretical size limit for UDP datagrams.
///
/// NOTE: This does not take [jumbograms](https://tools.ietf.org/html/rfc2675) into account.
const MAX_UDP_DATAGRAM_SIZE: usize = 65_535;

/// A future that waits on a timer and always resolves to a `dhcpv6_core::client::ClientTimerType`.
// TODO(http://fxbug.dev/53611): pull TimerFut to a common place, there's an templated version of
// this struct in netstack3.
#[derive(Debug)]
#[must_use = "futures do nothing unless you `.await` or poll them"]
struct TimerFut {
    timer_type: Option<dhcpv6_core::client::ClientTimerType>,
    timer: fasync::Timer,
}

impl TimerFut {
    pin_utils::unsafe_pinned!(timer: fasync::Timer);

    fn new(timer_type: dhcpv6_core::client::ClientTimerType, time: fasync::Time) -> Self {
        Self { timer_type: Some(timer_type), timer: fasync::Timer::new(time) }
    }
}

impl Future for TimerFut {
    type Output = dhcpv6_core::client::ClientTimerType;

    fn poll(mut self: Pin<&mut Self>, cx: &mut task::Context<'_>) -> task::Poll<Self::Output> {
        match self.as_mut().timer().poll(cx) {
            task::Poll::Ready(()) => task::Poll::Ready(
                self.timer_type
                    .take()
                    .expect("TimerFut must not be polled after it returned `Poll::Ready`"),
            ),
            task::Poll::Pending => task::Poll::Pending,
        }
    }
}

/// A DHCPv6 client.
pub(crate) struct Client<S: for<'a> AsyncSocket<'a>> {
    /// The interface the client is running on.
    interface_id: u64,
    /// Stores the hash of the last observed version of DNS servers by a watcher.
    ///
    /// The client uses this hash to determine whether new changes in DNS servers are observed and
    /// updates should be replied to the watcher.
    last_observed_dns_hash: u64,
    /// Stores a responder to send DNS server updates.
    dns_responder: Option<ClientWatchServersResponder>,
    /// Maintains the state for the client.
    state_machine: dhcpv6_core::client::ClientStateMachine<StdRng>,
    /// The socket used to communicate with DHCPv6 servers.
    socket: S,
    /// The address to send outgoing messages to.
    server_addr: SocketAddr,
    /// A collection of abort handles to all currently scheduled timers.
    timer_abort_handles: HashMap<dhcpv6_core::client::ClientTimerType, AbortHandle>,
    /// A set of all scheduled timers.
    timer_futs: FuturesUnordered<Abortable<TimerFut>>,
    /// A stream of FIDL requests to this client.
    request_stream: ClientRequestStream,
}

/// A trait that allows stubbing [`fuchsia_async::net::UdpSocket`] in tests.
pub(crate) trait AsyncSocket<'a> {
    type RecvFromFut: Future<Output = Result<(usize, SocketAddr), std::io::Error>> + 'a;
    type SendToFut: Future<Output = Result<usize, std::io::Error>> + 'a;

    fn recv_from(&'a self, buf: &'a mut [u8]) -> Self::RecvFromFut;
    fn send_to(&'a self, buf: &'a [u8], addr: SocketAddr) -> Self::SendToFut;
}

impl<'a> AsyncSocket<'a> for fasync::net::UdpSocket {
    type RecvFromFut = fasync::net::RecvFrom<'a>;
    type SendToFut = fasync::net::SendTo<'a>;

    fn recv_from(&'a self, buf: &'a mut [u8]) -> Self::RecvFromFut {
        self.recv_from(buf)
    }
    fn send_to(&'a self, buf: &'a [u8], addr: SocketAddr) -> Self::SendToFut {
        self.send_to(buf, addr)
    }
}

/// Converts a collection of `RequestableOptionCode` to `v6::OptionCode`.
fn to_dhcpv6_option_codes(codes: Vec<RequestableOptionCode>) -> Vec<v6::OptionCode> {
    codes
        .into_iter()
        .map(|option| match option {
            RequestableOptionCode::DnsServers => v6::OptionCode::DnsServers,
        })
        .collect()
}

/// Creates a state machine for the input operational models.
fn create_state_machine(
    transaction_id: [u8; 3],
    models: OperationalModels,
) -> Result<
    (dhcpv6_core::client::ClientStateMachine<StdRng>, dhcpv6_core::client::Actions),
    ClientError,
> {
    match models {
        OperationalModels { stateless: Some(Stateless { options_to_request }) } => {
            Ok(dhcpv6_core::client::ClientStateMachine::start_information_request(
                transaction_id,
                options_to_request.map(to_dhcpv6_option_codes).unwrap_or(Vec::new()),
                StdRng::from_entropy(),
            ))
        }
        OperationalModels { stateless: None } => Err(ClientError::UnsupportedModels(models)),
    }
}

/// Calculates a hash for the input.
fn hash<H: Hash>(h: &H) -> u64 {
    let mut dh = DefaultHasher::new();
    let () = h.hash(&mut dh);
    dh.finish()
}

impl<S: for<'a> AsyncSocket<'a>> Client<S> {
    /// Starts the client in `models`.
    ///
    /// Input `transaction_id` is used to label outgoing messages and match incoming ones.
    pub(crate) async fn start(
        transaction_id: [u8; 3],
        models: OperationalModels,
        interface_id: u64,
        socket: S,
        server_addr: SocketAddr,
        request_stream: ClientRequestStream,
    ) -> Result<Self, ClientError> {
        let (state_machine, actions) = create_state_machine(transaction_id, models)?;
        let mut client = Self {
            state_machine,
            interface_id,
            socket,
            server_addr,
            request_stream,
            timer_abort_handles: HashMap::new(),
            timer_futs: FuturesUnordered::new(),
            // Server watcher's API requires blocking if the first call would return an empty list,
            // so initialize this field with a hash of an empty list.
            last_observed_dns_hash: hash(&Vec::<Ipv6Addr>::new()),
            dns_responder: None,
        };
        let () = client.run_actions(actions).await?;
        Ok(client)
    }

    /// Runs a list of actions sequentially.
    async fn run_actions(
        &mut self,
        actions: dhcpv6_core::client::Actions,
    ) -> Result<(), ClientError> {
        stream::iter(actions)
            .map(Ok)
            .try_fold(self, |client, action| async move {
                match action {
                    dhcpv6_core::client::Action::SendMessage(buf) => {
                        let () = match client.socket.send_to(&buf, client.server_addr).await {
                            Ok(_size) => (),
                            Err(e) => {
                                let () = log::warn!(
                                    "failed to send message to {}: {}; will retransmit later",
                                    client.server_addr,
                                    e
                                );
                            }
                        };
                    }
                    dhcpv6_core::client::Action::ScheduleTimer(timer_type, timeout) => {
                        let () = client.schedule_timer(timer_type, timeout)?;
                    }
                    dhcpv6_core::client::Action::CancelTimer(timer_type) => {
                        let () = client.cancel_timer(timer_type)?;
                    }
                    dhcpv6_core::client::Action::UpdateDnsServers(servers) => {
                        let () = client.maybe_send_dns_server_updates(servers)?;
                    }
                };
                Ok(client)
            })
            .await
            .map(|_: &mut Client<S>| ())
    }

    /// Sends the latest DNS servers iff a watcher is watching, and the latest set of servers are
    /// different from what the watcher has observed last time.
    fn maybe_send_dns_server_updates(&mut self, servers: Vec<Ipv6Addr>) -> Result<(), ClientError> {
        let servers_hash = hash(&servers);
        if servers_hash == self.last_observed_dns_hash {
            Ok(())
        } else {
            Ok(match self.dns_responder.take() {
                Some(responder) => {
                    self.send_dns_server_updates(responder, servers, servers_hash)?
                }
                None => (),
            })
        }
    }

    /// Sends a list of DNS servers to a watcher through the input responder and updates the last
    /// observed hash.
    fn send_dns_server_updates(
        &mut self,
        responder: ClientWatchServersResponder,
        servers: Vec<Ipv6Addr>,
        hash: u64,
    ) -> Result<(), ClientError> {
        let () = responder
            .send(&mut servers.iter().map(|addr| {
                let address = fnet::Ipv6Address { addr: addr.octets() };
                let zone_index =
                    if is_unicast_link_local_strict(&address) { self.interface_id } else { 0 };

                fnetname::DnsServer_ {
                    address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                        address,
                        zone_index,
                        port: DEFAULT_DNS_PORT,
                    })),
                    source: Some(fnetname::DnsServerSource::Dhcpv6(
                        fnetname::Dhcpv6DnsServerSource {
                            source_interface: Some(self.interface_id),
                        },
                    )),
                }
            }))
            // The channel will be closed on error, so return an error to stop the client.
            .map_err(ClientError::Fidl)?;
        self.last_observed_dns_hash = hash;
        Ok(())
    }

    /// Schedules a timer for `timer_type` to fire after `timeout`.
    fn schedule_timer(
        &mut self,
        timer_type: dhcpv6_core::client::ClientTimerType,
        timeout: Duration,
    ) -> Result<(), ClientError> {
        match self.timer_abort_handles.entry(timer_type) {
            Entry::Vacant(entry) => {
                let (handle, reg) = AbortHandle::new_pair();
                let _: &mut AbortHandle = entry.insert(handle);
                let () = self.timer_futs.push(Abortable::new(
                    TimerFut::new(
                        timer_type,
                        fasync::Time::after(
                            i64::try_from(timeout.as_nanos())
                            .map(zx::Duration::from_nanos)
                            .unwrap_or_else(|_: TryFromIntError| {
                                let duration = zx::Duration::from_nanos(i64::MAX);
                                let () = log::warn!("time duration {:?} overflows zx::Duration, truncating to {:?}", timeout, duration);
                                duration
                            }),
                        ),
                    ),
                    reg,
                ));
                Ok(())
            }
            Entry::Occupied(_) => Err(ClientError::TimerAlreadyExist(timer_type)),
        }
    }

    /// Cancels a previously scheduled timer for `timer_type`.
    fn cancel_timer(
        &mut self,
        timer_type: dhcpv6_core::client::ClientTimerType,
    ) -> Result<(), ClientError> {
        match self.timer_abort_handles.entry(timer_type) {
            Entry::Vacant(_) => Err(ClientError::MissingTimer(timer_type)),
            Entry::Occupied(entry) => Ok(entry.remove().abort()),
        }
    }

    /// Handles a timeout.
    async fn handle_timeout(
        &mut self,
        timer_type: dhcpv6_core::client::ClientTimerType,
    ) -> Result<(), ClientError> {
        let () = self.cancel_timer(timer_type)?; // This timer just fired.
        let actions = self.state_machine.handle_timeout(timer_type);
        self.run_actions(actions).await
    }

    /// Handles a received message.
    async fn handle_message_recv(&mut self, mut msg: &[u8]) -> Result<(), ClientError> {
        let msg = match v6::Message::parse(&mut msg, ()) {
            Ok(msg) => msg,
            Err(e) => {
                // Discard invalid messages.
                //
                // https://tools.ietf.org/html/rfc8415#section-16.
                let () = log::warn!("failed to parse received message: {}", e);
                return Ok(());
            }
        };
        let actions = self.state_machine.handle_message_receive(msg);
        self.run_actions(actions).await
    }

    /// Handles a FIDL request sent to this client.
    fn handle_client_request(&mut self, request: ClientRequest) -> Result<(), ClientError> {
        match request {
            ClientRequest::WatchServers { responder } => match self.dns_responder {
                Some(_) => {
                    // Drop the previous responder to close the channel.
                    self.dns_responder = None;
                    // Return an error to stop the client because the channel is closed.
                    Err(ClientError::DoubleWatch())
                }
                None => {
                    let dns_servers = self.state_machine.get_dns_servers();
                    let servers_hash = hash(&dns_servers);
                    if servers_hash != self.last_observed_dns_hash {
                        // Something has changed from the last time, update the watcher.
                        let () =
                            self.send_dns_server_updates(responder, dns_servers, servers_hash)?;
                    } else {
                        // Nothing has changed, update the watcher later.
                        self.dns_responder = Some(responder);
                    }
                    Ok(())
                }
            },
        }
    }

    /// Handles the next event and returns the result.
    ///
    /// Takes a pre-allocated buffer to avoid repeated allocation.
    ///
    /// The returned `Option` is `None` if `request_stream` on the client is closed.
    async fn handle_next_event(&mut self, buf: &mut [u8]) -> Result<Option<()>, ClientError> {
        select! {
            timer_res = self.timer_futs.select_next_some() => {
                match timer_res {
                    Ok(timer_type) => {
                        let () = self.handle_timeout(timer_type).await?;
                        Ok(Some(()))
                    },
                    // The timer was aborted, do nothing.
                    Err(Aborted) => Ok(Some(())),
                }
            },
            recv_from_res = self.socket.recv_from(buf).fuse() => {
                let (size, _addr) = recv_from_res.map_err(ClientError::SocketRecv)?;
                let () = self.handle_message_recv(&buf[..size]).await?;
                Ok(Some(()))
            },
            request = self.request_stream.try_next() => {
                match request {
                    Ok(request) => {
                        request.map(|request| self.handle_client_request(request)).transpose()
                    }
                    Err(e) => {
                        Ok(Some(log::warn!("FIDL client request error: {}", e)))
                    }
                }
            }
        }
    }
}

/// Creates a socket listening on the input address.
async fn create_socket(addr: SocketAddr) -> Result<fasync::net::UdpSocket> {
    let socket = socket2::Socket::new(
        socket2::Domain::ipv6(),
        socket2::Type::dgram(),
        Some(socket2::Protocol::udp()),
    )?;
    // It is possible to run multiple clients on the same address.
    let () = socket.set_reuse_port(true)?;
    let () = socket.bind(&addr.into())?;
    fasync::net::UdpSocket::from_socket(socket.into_udp_socket()).context("converting socket")
}

/// Creates a transaction ID that can be used by the client as defined in [RFC 8415, Section 16.1].
///
/// [RFC 8415, Section 16.1]: https://tools.ietf.org/html/rfc8415#section-16.1
fn transaction_id() -> [u8; 3] {
    let mut id = [0u8; 3];
    let () = thread_rng().fill(&mut id[..]);
    id
}

/// Returns `true` if the input address is a link-local address (`fe80::/64`).
///
/// TODO(https://github.com/rust-lang/rust/issues/27709): use is_unicast_link_local_strict() in
/// stable rust when it's available.
fn is_unicast_link_local_strict(addr: &fnet::Ipv6Address) -> bool {
    addr.addr[..8] == [0xfe, 0x80, 0, 0, 0, 0, 0, 0]
}

/// Starts a client based on `params`.
///
/// `request` will be serviced by the client.
pub(crate) async fn serve_client(
    params: NewClientParams,
    request: ServerEnd<ClientMarker>,
) -> Result<()> {
    if let NewClientParams {
        interface_id: Some(interface_id),
        address: Some(address),
        models: Some(models),
    } = params
    {
        if Ipv6Addr::from(address.address.addr).is_multicast()
            || (is_unicast_link_local_strict(&address.address)
                && address.zone_index != interface_id)
        {
            return request
                .close_with_epitaph(zx::Status::INVALID_ARGS)
                .context("closing request channel with epitaph");
        }

        let fnetext::SocketAddress(addr) = fnet::SocketAddress::Ipv6(address).into();
        let servers_addr = IpAddr::from_str(RELAY_AGENT_AND_SERVER_LINK_LOCAL_MULTICAST_ADDRESS)
            .with_context(|| {
                format!(
                    "{} should be a valid IPv6 address",
                    RELAY_AGENT_AND_SERVER_LINK_LOCAL_MULTICAST_ADDRESS,
                )
            })?;
        let mut client = Client::<fasync::net::UdpSocket>::start(
            transaction_id(),
            models,
            interface_id,
            create_socket(addr).await?,
            SocketAddr::new(servers_addr, RELAY_AGENT_AND_SERVER_PORT),
            request.into_stream().context("getting new client request stream from channel")?,
        )
        .await?;
        let mut buf = vec![0u8; MAX_UDP_DATAGRAM_SIZE];
        loop {
            match client.handle_next_event(&mut buf).await? {
                Some(()) => (),
                None => break Ok(()),
            }
        }
    } else {
        // All param fields are required.
        request
            .close_with_epitaph(zx::Status::INVALID_ARGS)
            .context("closing request channel with epitaph")
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_endpoints,
        fidl_fuchsia_net_dhcpv6::{ClientMarker, OperationalModels, DEFAULT_CLIENT_PORT},
        fuchsia_async as fasync,
        futures::{channel::mpsc, join},
        matches::assert_matches,
        net_declare::{
            fidl_ip_v6, fidl_socket_addr, fidl_socket_addr_v6, std_ip_v6, std_socket_addr,
        },
        packet::serialize::InnerPacketBuilder,
        std::{collections::HashSet, task::Poll},
    };

    /// Creates a test socket bound to an ephemeral port on localhost.
    fn create_test_socket() -> (fasync::net::UdpSocket, SocketAddr) {
        let addr: SocketAddr = std_socket_addr!([::1]:0);
        let socket = std::net::UdpSocket::bind(addr).expect("failed to create test socket");
        let addr = socket.local_addr().expect("failed to get address of test socket");
        (fasync::net::UdpSocket::from_socket(socket).expect("failed to create test socket"), addr)
    }

    /// Asserts `socket` receives an information request from `want_from_addr`.
    async fn assert_received_information_request(
        socket: &fasync::net::UdpSocket,
        want_from_addr: SocketAddr,
    ) {
        let mut buf = vec![0u8; MAX_UDP_DATAGRAM_SIZE];
        let (size, from_addr) =
            socket.recv_from(&mut buf).await.expect("failed to receive on test server socket");
        assert_eq!(from_addr, want_from_addr);
        let buf = &mut &buf[..size]; // Implements BufferView.
        assert_eq!(
            v6::Message::parse(buf, ()).map(|x| x.msg_type()),
            Ok(v6::MessageType::InformationRequest)
        )
    }

    #[test]
    fn test_create_client_with_unsupported_models() {
        assert_matches!(
            create_state_machine([1, 2, 3], OperationalModels::empty()),
            Err(ClientError::UnsupportedModels(OperationalModels { stateless: None }))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_stops_on_channel_close() {
        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");

        let ((), client_res) = join!(
            async { drop(client_proxy) },
            serve_client(
                NewClientParams {
                    interface_id: Some(1),
                    address: Some(fidl_socket_addr_v6!([::1]:546)),
                    models: Some(OperationalModels {
                        stateless: Some(Stateless { options_to_request: None })
                    }),
                },
                server_end,
            ),
        );
        client_res.expect("client future should return with Ok");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_should_return_error_on_double_watch() {
        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");

        let (caller1_res, caller2_res, client_res) = join!(
            client_proxy.watch_servers(),
            client_proxy.watch_servers(),
            serve_client(
                NewClientParams {
                    interface_id: Some(1),
                    address: Some(fidl_socket_addr_v6!([::1]:546)),
                    models: Some(OperationalModels {
                        stateless: Some(Stateless { options_to_request: None })
                    }),
                },
                server_end,
            )
        );

        assert_matches!(
            caller1_res,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. })
        );
        assert_matches!(
            caller2_res,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. })
        );
        assert!(client_res
            .expect_err("client should fail with double watch error")
            .to_string()
            .contains("got watch request while the previous one is pending"));
    }

    #[test]
    fn test_client_starts_with_valid_args() {
        let mut exec = fasync::Executor::new().expect("failed to create test executor");

        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");

        let test_fut = async {
            join!(
                client_proxy.watch_servers(),
                serve_client(
                    NewClientParams {
                        interface_id: Some(1),
                        address: Some(fidl_socket_addr_v6!([::1]:546)),
                        models: Some(OperationalModels {
                            stateless: Some(Stateless { options_to_request: None })
                        }),
                    },
                    server_end
                )
            )
        };
        futures::pin_mut!(test_fut);
        assert_matches!(exec.run_until_stalled(&mut test_fut), Poll::Pending);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_fails_to_start_with_invalid_args() {
        for params in vec![
            // Missing required field.
            NewClientParams {
                interface_id: Some(1),
                address: None,
                models: Some(OperationalModels {
                    stateless: Some(Stateless { options_to_request: None }),
                }),
            },
            // Interface ID and zone index mismatch on link-local address.
            NewClientParams {
                interface_id: Some(2),
                address: Some(fnet::Ipv6SocketAddress {
                    address: fidl_ip_v6!(fe80::1),
                    port: DEFAULT_CLIENT_PORT,
                    zone_index: 1,
                }),
                models: Some(OperationalModels {
                    stateless: Some(Stateless { options_to_request: None }),
                }),
            },
            // Multicast address is invalid.
            NewClientParams {
                interface_id: Some(1),
                address: Some(fnet::Ipv6SocketAddress {
                    address: fidl_ip_v6!(ff01::1),
                    port: DEFAULT_CLIENT_PORT,
                    zone_index: 1,
                }),
                models: Some(OperationalModels {
                    stateless: Some(Stateless { options_to_request: None }),
                }),
            },
        ] {
            let (client_end, server_end) =
                create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
            let () =
                serve_client(params, server_end).await.expect("start server failed unexpectedly");
            // Calling any function on the client proxy should fail due to channel closed with
            // `INVALID_ARGS`.
            assert_matches!(
                client_end.into_proxy().expect("failed to create test proxy").watch_servers().await,
                Err(fidl::Error::ClientChannelClosed { status: zx::Status::INVALID_ARGS, .. })
            );
        }
    }

    #[test]
    fn test_is_unicast_link_local_strict() {
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::)), true);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::1)), true);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::ffff:1:2:3)), true);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::1:0:0:0:0)), false);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe81::)), false);
    }

    fn create_test_dns_server(
        address: fnet::Ipv6Address,
        source_interface: u64,
        zone_index: u64,
    ) -> fnetname::DnsServer_ {
        fnetname::DnsServer_ {
            address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                address,
                zone_index,
                port: DEFAULT_DNS_PORT,
            })),
            source: Some(fnetname::DnsServerSource::Dhcpv6(fnetname::Dhcpv6DnsServerSource {
                source_interface: Some(source_interface),
            })),
        }
    }

    async fn send_reply_with_options(
        socket: &fasync::net::UdpSocket,
        to_addr: SocketAddr,
        transaction_id: [u8; 3],
        options: &[v6::DhcpOption<'_>],
    ) -> Result<()> {
        let builder = v6::MessageBuilder::new(v6::MessageType::Reply, transaction_id, options);
        let mut buf = vec![0u8; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        let _: usize = socket.send_to(&buf, to_addr).await?;
        Ok(())
    }

    #[test]
    fn test_client_should_respond_to_dns_watch_requests() {
        let mut exec = fasync::Executor::new().expect("failed to create test ecexutor");
        let transaction_id = [1, 2, 3];

        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");
        let client_stream = server_end.into_stream().expect("failed to create test request stream");

        let (client_socket, client_addr) = create_test_socket();
        let (server_socket, server_addr) = create_test_socket();
        let mut client = exec
            .run_singlethreaded(Client::<fasync::net::UdpSocket>::start(
                transaction_id,
                OperationalModels { stateless: Some(Stateless { options_to_request: None }) },
                1, /* interface ID */
                client_socket,
                server_addr,
                client_stream,
            ))
            .expect("failed to create test client");

        let (mut signal_client_to_refresh, mut client_should_refresh) = mpsc::channel::<()>(1);

        let client_fut = async {
            let mut buf = vec![0u8; MAX_UDP_DATAGRAM_SIZE];
            loop {
                select! {
                    res = client.handle_next_event(&mut buf).fuse() => {
                        match res.expect("test client failed to handle next event") {
                            Some(()) => (),
                            None => break (),
                        };
                    }
                    _ = client_should_refresh.next() => {
                        // Make the client ready for another reply immediately on signal, so it can
                        // start receiving updates without waiting for the full refresh timeout
                        // which is unrealistic test.
                        if client.timer_abort_handles.contains_key(&dhcpv6_core::client::ClientTimerType::Refresh) {
                            let () = client
                                .handle_timeout(dhcpv6_core::client::ClientTimerType::Refresh)
                                .await
                                .expect("test client failed to handle timeout");
                        } else {
                            panic!("no refresh timer is scheduled and refresh is requested in test");
                        }
                    },
                }
            }
        }.fuse();
        futures::pin_mut!(client_fut);

        macro_rules! build_test_fut {
            ($test_fut:ident) => {
                let $test_fut = async {
                    select! {
                        () = client_fut => panic!("test client returned unexpectly"),
                        r = client_proxy.watch_servers() => r,
                    }
                };
                futures::pin_mut!($test_fut);
            };
        }

        {
            // No DNS configurations received yet.
            build_test_fut!(test_fut);
            assert_matches!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

            // Send an empty list to the client, should not update watcher.
            let () = exec
                .run_singlethreaded(send_reply_with_options(
                    &server_socket,
                    client_addr,
                    transaction_id,
                    &[v6::DhcpOption::DnsServers(Vec::new())],
                ))
                .expect("failed to send test reply");
            assert_matches!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

            // Send a list of DNS servers, the watcher should be updated accordingly.
            let () = signal_client_to_refresh
                .try_send(())
                .expect("failed to signal test client to refresh");
            let () = exec
                .run_singlethreaded(send_reply_with_options(
                    &server_socket,
                    client_addr,
                    transaction_id,
                    &[v6::DhcpOption::DnsServers(vec![std_ip_v6!(fe80::1:2)])],
                ))
                .expect("failed to send test reply");
            let want_servers = vec![create_test_dns_server(
                fidl_ip_v6!(fe80::1:2),
                1, /* source interface */
                1, /* zone index */
            )];
            assert_matches!(
                exec.run_until_stalled(&mut test_fut),
                Poll::Ready(Ok(servers)) if servers == want_servers
            );
        } // drop `test_fut` so `client_fut` is no longer mutably borrowed.

        {
            // No new changes, should not update watcher.
            build_test_fut!(test_fut);
            assert_matches!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

            // Send the same list of DNS servers, should not update watcher.
            let () = signal_client_to_refresh
                .try_send(())
                .expect("failed to signal test client to refresh");
            let () = exec
                .run_singlethreaded(send_reply_with_options(
                    &server_socket,
                    client_addr,
                    transaction_id,
                    &[v6::DhcpOption::DnsServers(vec![std_ip_v6!(fe80::1:2)])],
                ))
                .expect("failed to send test reply");
            assert_matches!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

            // Send a different list of DNS servers, should update watcher.
            let () = signal_client_to_refresh
                .try_send(())
                .expect("failed to signal test client to refresh");
            let () = exec
                .run_singlethreaded(send_reply_with_options(
                    &server_socket,
                    client_addr,
                    transaction_id,
                    &[v6::DhcpOption::DnsServers(vec![
                        std_ip_v6!(fe80::1:2),
                        std_ip_v6!(1234::5:6),
                    ])],
                ))
                .expect("failed to send test reply");
            let want_servers = vec![
                create_test_dns_server(
                    fidl_ip_v6!(fe80::1:2),
                    1, /* source interface */
                    1, /* zone index */
                ),
                // Only set zone index for link local addresses.
                create_test_dns_server(
                    fidl_ip_v6!(1234::5:6),
                    1, /* source interface */
                    0, /* zone index */
                ),
            ];
            assert_matches!(
                exec.run_until_stalled(&mut test_fut),
                Poll::Ready(Ok(servers)) if servers == want_servers
            );
        } // drop `test_fut` so `client_fut` is no longer mutably borrowed.

        {
            // Send an empty list of DNS servers, should update watcher, because this is different from
            // what the watcher has seen last time.
            let () = signal_client_to_refresh
                .try_send(())
                .expect("failed to signal test client to refresh");
            let () = exec
                .run_singlethreaded(send_reply_with_options(
                    &server_socket,
                    client_addr,
                    transaction_id,
                    &[v6::DhcpOption::DnsServers(Vec::new())],
                ))
                .expect("failed to send test reply");
            build_test_fut!(test_fut);
            let want_servers = Vec::<fnetname::DnsServer_>::new();
            assert_matches!(
                exec.run_until_stalled(&mut test_fut),
                Poll::Ready(Ok(servers)) if servers == want_servers
            );
        } // drop `test_fut` so `client_fut` is no longer mutably borrowed.
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_should_respond_with_dns_servers_on_first_watch_if_non_empty() {
        let transaction_id = [1, 2, 3];

        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");
        let client_stream = server_end.into_stream().expect("failed to create test request stream");

        let (client_socket, client_addr) = create_test_socket();
        let (server_socket, server_addr) = create_test_socket();
        let mut client = Client::<fasync::net::UdpSocket>::start(
            transaction_id,
            OperationalModels { stateless: Some(Stateless { options_to_request: None }) },
            1, /* interface ID */
            client_socket,
            server_addr,
            client_stream,
        )
        .await
        .expect("failed to create test client");

        let () = send_reply_with_options(
            &server_socket,
            client_addr,
            transaction_id,
            &[v6::DhcpOption::DnsServers(vec![std_ip_v6!(fe80::1:2), std_ip_v6!(1234::5:6)])],
        )
        .await
        .expect("failed to send test message");

        // Receive non-empty DNS servers before watch.
        let mut buf = vec![0u8; MAX_UDP_DATAGRAM_SIZE];
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        // Emit aborted timer.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));

        let want_servers = vec![
            create_test_dns_server(
                fidl_ip_v6!(fe80::1:2),
                1, /* source interface */
                1, /* zone index */
            ),
            create_test_dns_server(
                fidl_ip_v6!(1234::5:6),
                1, /* source interface */
                0, /* zone index */
            ),
        ];
        assert_matches!(
            join!(client.handle_next_event(&mut buf), client_proxy.watch_servers()),
            (Ok(Some(())), Ok(servers)) if servers == want_servers
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_schedule_and_cancel_timers() {
        let (_client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_stream = server_end.into_stream().expect("failed to create test request stream");

        let (client_socket, _client_addr) = create_test_socket();
        let (_server_socket, server_addr) = create_test_socket();
        let mut client = Client::<fasync::net::UdpSocket>::start(
            [1, 2, 3], /* transaction ID */
            OperationalModels { stateless: Some(Stateless { options_to_request: None }) },
            1, /* interface ID */
            client_socket,
            server_addr,
            client_stream,
        )
        .await
        .expect("failed to create test client");

        // Stateless DHCP client starts by scheduling a retransmission timer.
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        let () = client
            .cancel_timer(dhcpv6_core::client::ClientTimerType::Retransmission)
            .expect("canceling retransmission timer on test client");
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            Vec::<&dhcpv6_core::client::ClientTimerType>::new()
        );

        let () = client
            .schedule_timer(dhcpv6_core::client::ClientTimerType::Refresh, Duration::from_nanos(1))
            .expect("scheduling refresh timer on test client");
        let () = client
            .schedule_timer(
                dhcpv6_core::client::ClientTimerType::Retransmission,
                Duration::from_nanos(2),
            )
            .expect("scheduling retransmission timer on test client");
        assert_eq!(
            client.timer_abort_handles.keys().collect::<HashSet<_>>(),
            vec![
                &dhcpv6_core::client::ClientTimerType::Retransmission,
                &dhcpv6_core::client::ClientTimerType::Refresh
            ]
            .into_iter()
            .collect()
        );

        assert_matches!(
            client.schedule_timer(
                dhcpv6_core::client::ClientTimerType::Refresh,
                Duration::from_nanos(1)
            ),
            Err(ClientError::TimerAlreadyExist(dhcpv6_core::client::ClientTimerType::Refresh))
        );
        assert_matches!(
            client.schedule_timer(
                dhcpv6_core::client::ClientTimerType::Retransmission,
                Duration::from_nanos(2)
            ),
            Err(ClientError::TimerAlreadyExist(
                dhcpv6_core::client::ClientTimerType::Retransmission
            ))
        );

        let () = client
            .cancel_timer(dhcpv6_core::client::ClientTimerType::Refresh)
            .expect("canceling retransmission timer on test client");
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        assert_matches!(
            client.cancel_timer(dhcpv6_core::client::ClientTimerType::Refresh),
            Err(ClientError::MissingTimer(dhcpv6_core::client::ClientTimerType::Refresh))
        );

        let () = client
            .cancel_timer(dhcpv6_core::client::ClientTimerType::Retransmission)
            .expect("canceling retransmission timer on test client");
        assert_eq!(
            client
                .timer_abort_handles
                .keys()
                .collect::<Vec<&dhcpv6_core::client::ClientTimerType>>(),
            Vec::<&dhcpv6_core::client::ClientTimerType>::new()
        );

        assert_matches!(
            client.cancel_timer(dhcpv6_core::client::ClientTimerType::Retransmission),
            Err(ClientError::MissingTimer(dhcpv6_core::client::ClientTimerType::Retransmission))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_next_event_on_client() {
        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");
        let client_stream = server_end.into_stream().expect("failed to create test request stream");

        let (client_socket, client_addr) = create_test_socket();
        let (server_socket, server_addr) = create_test_socket();
        let mut client = Client::<fasync::net::UdpSocket>::start(
            [1, 2, 3], /* transaction ID */
            OperationalModels { stateless: Some(Stateless { options_to_request: None }) },
            1, /* interface ID */
            client_socket,
            server_addr,
            client_stream,
        )
        .await
        .expect("failed to create test client");

        // Starting the client in stateless should send an information request out.
        assert_received_information_request(&server_socket, client_addr).await;
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        let mut buf = vec![0u8; MAX_UDP_DATAGRAM_SIZE];
        // Trigger a retransmission.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        assert_received_information_request(&server_socket, client_addr).await;
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        // Message targeting another transaction ID should be ignored.
        let () = send_reply_with_options(&server_socket, client_addr, [5, 6, 7], &[])
            .await
            .expect("failed to send test message");
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        // Invalid messages should be discarded. Empty buffer is invalid.
        let _: usize =
            server_socket.send_to(&[], client_addr).await.expect("failed to send test message");
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        // Message targeting this client should cause the client to transition state.
        let () = send_reply_with_options(&server_socket, client_addr, [1, 2, 3], &[])
            .await
            .expect("failed to send test message");
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Refresh]
        );
        // Discard aborted retransmission timer.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));

        // Reschedule a shorter timer for Refresh so we don't spend time waiting in test.
        client
            .cancel_timer(dhcpv6_core::client::ClientTimerType::Refresh)
            .expect("failed to cancel timer on test client");
        // Discard cancelled refresh timer.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        client
            .schedule_timer(dhcpv6_core::client::ClientTimerType::Refresh, Duration::from_nanos(1))
            .expect("failed to schedule timer on test client");

        // Trigger a refresh.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        assert_received_information_request(&server_socket, client_addr).await;
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        let test_fut = async {
            assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
            client
                .dns_responder
                .take()
                .expect("test client did not get a channel responder")
                .send(&mut std::iter::once(fnetname::DnsServer_ {
                    address: Some(fidl_socket_addr!([fe01::2:3]:42)),
                    source: Some(fnetname::DnsServerSource::Dhcpv6(
                        fnetname::Dhcpv6DnsServerSource { source_interface: Some(42) },
                    )),
                }))
                .expect("failed to send response on test channel");
        };
        let (watcher_res, ()) = join!(client_proxy.watch_servers(), test_fut);
        let servers = watcher_res.expect("failed to watch servers");
        assert_eq!(
            servers,
            vec![fnetname::DnsServer_ {
                address: Some(fidl_socket_addr!([fe01::2:3]:42)),
                source: Some(fnetname::DnsServerSource::Dhcpv6(fnetname::Dhcpv6DnsServerSource {
                    source_interface: Some(42)
                },)),
            }]
        );

        // Drop the channel should cause `handle_next_event(&mut buf)` to return `None`.
        drop(client_proxy);
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(None));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_next_event_respects_timer_order() {
        let (_client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_stream = server_end.into_stream().expect("failed to create test request stream");

        let (client_socket, client_addr) = create_test_socket();
        let (server_socket, server_addr) = create_test_socket();
        let mut client = Client::<fasync::net::UdpSocket>::start(
            [1, 2, 3], /* transaction ID */
            OperationalModels { stateless: Some(Stateless { options_to_request: None }) },
            1, /* interface ID */
            client_socket,
            server_addr,
            client_stream,
        )
        .await
        .expect("failed to create test client");

        let mut buf = vec![0u8; MAX_UDP_DATAGRAM_SIZE];
        // A retransmission timer is scheduled when starting the client in stateless mode. Cancel
        // it and create a new one with a longer timeout so the test is not flaky.
        let () = client
            .cancel_timer(dhcpv6_core::client::ClientTimerType::Retransmission)
            .expect("failed to cancel timer on test client");
        // Discard cancelled retransmission timer.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        let () = client
            .schedule_timer(
                dhcpv6_core::client::ClientTimerType::Retransmission,
                Duration::from_secs(1_000_000),
            )
            .expect("failed to schedule timer on test client");
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        // Trigger a message receive, the message is later discarded because transaction ID doesn't
        // match.
        let () = send_reply_with_options(&server_socket, client_addr, [5, 6, 7], &[])
            .await
            .expect("failed to send test message");
        // There are now two pending events, the message receive is handled first because the timer
        // is far into the future.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        // The retransmission timer is still here.
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );

        // Inserts a refresh timer that precedes the retransmission.
        let () = client
            .schedule_timer(dhcpv6_core::client::ClientTimerType::Refresh, Duration::from_nanos(1))
            .expect("scheduling refresh timer on test client");
        // This timer is scheduled.
        assert_eq!(
            client.timer_abort_handles.keys().collect::<HashSet<_>>(),
            vec![
                &dhcpv6_core::client::ClientTimerType::Retransmission,
                &dhcpv6_core::client::ClientTimerType::Refresh
            ]
            .into_iter()
            .collect()
        );

        // Now handle_next_event(&mut buf) should trigger a refresh because it precedes
        // retransmission.
        assert_matches!(client.handle_next_event(&mut buf).await, Ok(Some(())));
        // The refresh timer is consumed.
        assert_eq!(
            client.timer_abort_handles.keys().collect::<Vec<_>>(),
            vec![&dhcpv6_core::client::ClientTimerType::Retransmission]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_next_event_fails_on_recv_err() {
        struct StubSocket {}
        impl<'a> AsyncSocket<'a> for StubSocket {
            type RecvFromFut = futures::future::Ready<Result<(usize, SocketAddr), std::io::Error>>;
            type SendToFut = futures::future::Ready<Result<usize, std::io::Error>>;

            fn recv_from(&'a self, _buf: &'a mut [u8]) -> Self::RecvFromFut {
                futures::future::ready(Err(std::io::Error::new(
                    std::io::ErrorKind::Other,
                    "test recv error",
                )))
            }
            fn send_to(&'a self, _buf: &'a [u8], _addr: SocketAddr) -> Self::SendToFut {
                futures::future::ready(Ok(0))
            }
        }

        let (_client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_stream = server_end.into_stream().expect("failed to create test request stream");

        let mut client = Client::<StubSocket>::start(
            [1, 2, 3], /* transaction ID */
            OperationalModels { stateless: Some(Stateless { options_to_request: None }) },
            1, /* interface ID */
            StubSocket {},
            std_socket_addr!([::1]:0),
            client_stream,
        )
        .await
        .expect("failed to create test client");

        assert_matches!(
            client.handle_next_event(&mut [0u8]).await,
            Err(ClientError::SocketRecv(err)) if err.kind() == std::io::ErrorKind::Other
        );
    }
}

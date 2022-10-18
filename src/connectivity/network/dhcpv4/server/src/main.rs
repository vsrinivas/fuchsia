// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    dhcpv4::{
        configuration,
        protocol::{Message, SERVER_PORT},
        server::{
            DataStore, ResponseTarget, Server, ServerAction, ServerDispatcher, ServerError,
            DEFAULT_STASH_ID,
        },
        stash::Stash,
    },
    fuchsia_async::net::UdpSocket,
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    futures::{Future, SinkExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _},
    net_types::ethernet::Mac,
    packet::{serialize::InnerPacketBuilder, Serializer},
    packet_formats::{ipv4::Ipv4PacketBuilder, udp::UdpPacketBuilder},
    std::{
        cell::RefCell,
        collections::HashMap,
        convert::{Infallible, TryInto as _},
        net::{IpAddr, Ipv4Addr, SocketAddr},
    },
    tracing::{debug, error, info, warn},
};

/// A buffer size in excess of the maximum allowable DHCP message size.
const BUF_SZ: usize = 1024;

enum IncomingService {
    Server(fidl_fuchsia_net_dhcp::Server_RequestStream),
}

const DEFAULT_LEASE_DURATION_SECONDS: u32 = 24 * 60 * 60;

fn default_parameters() -> configuration::ServerParameters {
    configuration::ServerParameters {
        server_ips: vec![],
        lease_length: dhcpv4::configuration::LeaseLength {
            default_seconds: DEFAULT_LEASE_DURATION_SECONDS,
            max_seconds: DEFAULT_LEASE_DURATION_SECONDS,
        },
        managed_addrs: dhcpv4::configuration::ManagedAddresses {
            mask: configuration::SubnetMask::new(0).unwrap(),
            pool_range_start: Ipv4Addr::UNSPECIFIED,
            pool_range_stop: Ipv4Addr::UNSPECIFIED,
        },
        permitted_macs: dhcpv4::configuration::PermittedMacs(vec![]),
        static_assignments: dhcpv4::configuration::StaticAssignments(
            std::collections::hash_map::HashMap::new(),
        ),
        arp_probe: false,
        bound_device_names: vec![],
    }
}

/// dhcpd is the Fuchsia DHCPv4 server.
#[derive(argh::FromArgs)]
struct Args {
    /// enables storage of dhcpd lease and configuration state to persistent storage
    #[argh(switch)]
    persistent: bool,
}

#[fuchsia::main()]
async fn main() -> Result<(), Error> {
    info!("starting");

    let Args { persistent } = argh::from_env();
    info!("persistent={}", persistent);
    if persistent {
        let stash = Stash::new(DEFAULT_STASH_ID).context("failed to instantiate stash")?;
        // The server parameters and the client records must be consistent with one another in
        // order to ensure correct server operation. The records cannot be consistent with default
        // parameters, so if parameters fail to load from the stash, then the records should
        // default to empty.
        let (params, options, records) = match stash.load_parameters().await {
            Ok(params) => {
                let options = stash.load_options().await.unwrap_or_else(|e| {
                    warn!("failed to load options from stash: {:?}", e);
                    HashMap::new()
                });
                let records = stash.load_client_records().await.unwrap_or_else(|e| {
                    warn!("failed to load client records from stash: {:?}", e);
                    HashMap::new()
                });
                (params, options, records)
            }
            Err(e) => {
                warn!("failed to load parameters from stash: {:?}", e);
                (default_parameters(), HashMap::new(), HashMap::new())
            }
        };
        let server = match Server::new_from_state(stash.clone(), params, options, records) {
            Ok(v) => v,
            Err(e) => {
                warn!("failed to create server from persistent state: {}", e);
                Server::new(Some(stash), default_parameters())
            }
        };
        Ok(run(server).await?)
    } else {
        Ok(run(Server::<Stash>::new(None, default_parameters())).await?)
    }
}

async fn run<DS: DataStore>(server: Server<DS>) -> Result<(), Error> {
    let server = RefCell::new(ServerDispatcherRuntime::new(server));

    let mut fs = ServiceFs::new_local();
    let _: &mut ServiceFsDir<'_, _> = fs.dir("svc").add_fidl_service(IncomingService::Server);
    let _: &mut ServiceFs<_> = fs
        .take_and_serve_directory_handle()
        .context("service fs failed to take and serve directory handle")?;

    let (mut socket_sink, socket_stream) =
        futures::channel::mpsc::channel::<ServerSocketCollection<UdpSocket>>(1);

    // Attempt to enable the server on startup.
    // NOTE(brunodalbo): Enabling the server on startup should be an explicit
    // configuration loaded from default configs and stash. For now, just mimic
    // existing behavior and try to enable. It'll fail if we don't have a valid
    // configuration from stash/config.
    match server.borrow_mut().enable() {
        Ok(None) => unreachable!("server can't be enabled already"),
        Ok(Some(socket_collection)) => {
            // Sending here should never fail; we just created the stream above.
            let () = socket_sink.try_send(socket_collection)?;
        }
        Err(e @ fuchsia_zircon::Status::INVALID_ARGS) => {
            info!("server not configured for serving leases: {:?}", e)
        }
        Err(e) => warn!("could not enable server on startup: {:?}", e),
    }

    let admin_fut =
        fs.then(futures::future::ok).try_for_each_concurrent(None, |incoming_service| async {
            match incoming_service {
                IncomingService::Server(stream) => {
                    run_server(stream, &server, &default_parameters(), socket_sink.clone())
                        .inspect_err(|e| warn!("run_server failed: {:?}", e))
                        .await?;
                    Ok(())
                }
            }
        });

    let server_fut = define_running_server_fut(&server, socket_stream);

    info!("running");
    let ((), ()) = futures::try_join!(server_fut, admin_fut)?;

    Ok(())
}

trait SocketServerDispatcher: ServerDispatcher {
    type Socket;

    fn create_socket(name: &str, src: Ipv4Addr) -> std::io::Result<Self::Socket>;
    fn dispatch_message(&mut self, msg: Message) -> Result<ServerAction, ServerError>;
    fn create_sockets(
        params: &configuration::ServerParameters,
    ) -> std::io::Result<Vec<SocketWithId<Self::Socket>>>;
}

impl<DS: DataStore> SocketServerDispatcher for Server<DS> {
    type Socket = UdpSocket;

    fn create_socket(name: &str, src: Ipv4Addr) -> std::io::Result<Self::Socket> {
        let socket = socket2::Socket::new(
            socket2::Domain::IPV4,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )?;
        // Since dhcpd may listen to multiple interfaces, we must enable
        // SO_REUSEPORT so that binding the same (address, port) pair to each
        // interface can still succeed.
        let () = socket.set_reuse_port(true)?;
        let () = socket.bind_device(Some(name.as_bytes()))?;
        info!("socket bound to device {}", name);
        let () = socket.set_broadcast(true)?;
        let () = socket.bind(&SocketAddr::new(IpAddr::V4(src), SERVER_PORT).into())?;
        Ok(UdpSocket::from_socket(socket.into())?)
    }

    fn dispatch_message(&mut self, msg: Message) -> Result<ServerAction, ServerError> {
        self.dispatch(msg)
    }

    fn create_sockets(
        params: &configuration::ServerParameters,
    ) -> std::io::Result<Vec<SocketWithId<Self::Socket>>> {
        let configuration::ServerParameters { bound_device_names, .. } = params;
        bound_device_names
            .iter()
            .map(|name| {
                let iface_id =
                    fuchsia_nix::net::if_::if_nametoindex(name.as_str()).map_err(|e| match e {
                        // We use an `as` cast because discriminant enum variants do not have From
                        // or Into implementations, cf. https://github.com/rust-lang/rfcs/pull/3040
                        fuchsia_nix::Error::Sys(e) => std::io::Error::from_raw_os_error(e as i32),
                        e @ fuchsia_nix::Error::InvalidUtf8 => std::io::Error::new(
                            std::io::ErrorKind::InvalidInput,
                            std::string::ToString::to_string(&e),
                        ),
                        e @ fuchsia_nix::Error::InvalidPath
                        | e @ fuchsia_nix::Error::UnsupportedOperation => std::io::Error::new(
                            std::io::ErrorKind::Other,
                            std::string::ToString::to_string(&e),
                        ),
                    })?;
                let socket = Self::create_socket(name, Ipv4Addr::UNSPECIFIED)?;
                Ok(SocketWithId { socket, iface_id: iface_id.into() })
            })
            .collect()
    }
}

/// A wrapper around a [`ServerDispatcher`] that keeps information about the
/// server status through a [`futures::future::AbortHandle`].
struct ServerDispatcherRuntime<S> {
    abort_handle: Option<futures::future::AbortHandle>,
    server: S,
}

impl<S> std::ops::Deref for ServerDispatcherRuntime<S> {
    type Target = S;

    fn deref(&self) -> &Self::Target {
        &self.server
    }
}

impl<S> std::ops::DerefMut for ServerDispatcherRuntime<S> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.server
    }
}

impl<S: SocketServerDispatcher> ServerDispatcherRuntime<S> {
    /// Creates a new runtime with `server`.
    fn new(server: S) -> Self {
        Self { abort_handle: None, server }
    }

    /// Disables the server.
    ///
    /// `disable` will cancel the previous
    /// [`futures::future::AbortRegistration`] returned by `enable`.
    ///
    /// If the server is already disabled, `disable` is a no-op.
    fn disable(&mut self) {
        if let Some(abort_handle) = self.abort_handle.take() {
            let () = abort_handle.abort();
        }
    }

    /// Enables the server.
    ///
    /// Attempts to enable the server, returning a new
    /// [`ServerSocketCollection`] on success. The returned collection contains
    /// the list of sockets where the server can listen on and an abort
    /// registration that is used to cancel the future that listen on the
    /// sockets when [`ServerDispatcherRuntime::disable`] is called.
    ///
    /// Returns an error if the server couldn't be started or if the closure
    /// fails, maintaining the server in the disabled state.
    ///
    /// If the server is already enabled, `enable` returns `Ok(None)`.
    fn enable(
        &mut self,
    ) -> Result<Option<ServerSocketCollection<S::Socket>>, fuchsia_zircon::Status> {
        if self.abort_handle.is_some() {
            // Server already running.
            return Ok(None);
        }
        let params = self.server.try_validate_parameters()?;
        // Provide the closure with an AbortRegistration and a ref to
        // parameters.
        let (abort_handle, abort_registration) = futures::future::AbortHandle::new_pair();

        let sockets = S::create_sockets(params).map_err(|e| {
            let () = match e.raw_os_error() {
                // A short-lived SoftAP interface may be, and frequently is, torn down prior to the
                // full instantiation of its associated dhcpd component. Consequently, binding to
                // the SoftAP interface name will fail with ENODEV. However, such a failure is
                // normal and expected under those circumstances.
                Some(libc::ENODEV) => {
                    warn!("Failed to create server sockets: {}", e)
                }
                Some(_) | None => error!("Failed to create server sockets: {}", e),
            };
            fuchsia_zircon::Status::IO
        })?;
        if sockets.is_empty() {
            error!("No sockets to run server on");
            return Err(fuchsia_zircon::Status::INVALID_ARGS);
        }
        self.abort_handle = Some(abort_handle);
        Ok(Some(ServerSocketCollection { sockets, abort_registration }))
    }

    /// Returns `true` if the server is enabled.
    fn enabled(&self) -> bool {
        self.abort_handle.is_some()
    }

    /// Runs the closure `f` only if the server is currently disabled.
    ///
    /// Returns `BAD_STATE` error otherwise.
    fn if_disabled<R, F: FnOnce(&mut S) -> Result<R, fuchsia_zircon::Status>>(
        &mut self,
        f: F,
    ) -> Result<R, fuchsia_zircon::Status> {
        if self.abort_handle.is_none() {
            f(&mut self.server)
        } else {
            Err(fuchsia_zircon::Status::BAD_STATE)
        }
    }
}

#[derive(Debug, PartialEq)]
struct SocketWithId<S> {
    socket: S,
    iface_id: u64,
}

/// Helper struct to handle buffer data from sockets.
struct MessageHandler<'a, S: SocketServerDispatcher> {
    server: &'a RefCell<ServerDispatcherRuntime<S>>,
}

impl<'a, S: SocketServerDispatcher> MessageHandler<'a, S> {
    /// Creates a new `MessageHandler` for `server`.
    fn new(server: &'a RefCell<ServerDispatcherRuntime<S>>) -> Self {
        Self { server }
    }

    /// Handles `buf` from `sender`.
    ///
    /// Returns `Ok(Some(sock, msg, dst))` if `msg` must be sent to `dst`
    /// over `sock`.
    ///
    /// Returns `Ok(None)` if no action is required and the handler is ready to
    /// receive more messages.
    ///
    /// Returns `Err` if an unrecoverable error occurs and the server must stop
    /// serving.
    fn handle_from_sender(
        &mut self,
        buf: &[u8],
        mut sender: std::net::SocketAddrV4,
    ) -> Result<Option<(std::net::SocketAddrV4, Message, Option<Mac>)>, Error> {
        let msg = match Message::from_buffer(buf) {
            Ok(msg) => {
                debug!("parsed message from {}: {:?}", sender, msg);
                msg
            }
            Err(e) => {
                warn!("failed to parse message from {}: {}", sender, e);
                return Ok(None);
            }
        };

        let typ = msg.get_dhcp_type();
        if sender.ip().is_unspecified() {
            info!("processing {:?} from {}", typ, msg.chaddr);
        } else {
            info!("processing {:?} from {}", typ, sender);
        }

        // This call should not block because the server is single-threaded.
        let result = self.server.borrow_mut().dispatch_message(msg);
        match result {
            Err(e) => {
                error!("error processing client message: {:?}", e);
                Ok(None)
            }
            Ok(ServerAction::AddressRelease(addr)) => {
                info!("released address: {}", addr);
                Ok(None)
            }
            Ok(ServerAction::AddressDecline(addr)) => {
                info!("allocated address: {}", addr);
                Ok(None)
            }
            Ok(ServerAction::SendResponse(message, dst)) => {
                debug!("generated response: {:?}", message);

                let typ = message.get_dhcp_type();
                // Check if server returned an explicit destination ip.
                let (addr, chaddr) = match dst {
                    ResponseTarget::Broadcast => {
                        info!("sending {:?} to {}", typ, Ipv4Addr::BROADCAST);
                        (Ipv4Addr::BROADCAST, None)
                    }
                    ResponseTarget::Unicast(addr, None) => {
                        info!("sending {:?} to {}", typ, addr);
                        (addr, None)
                    }
                    ResponseTarget::Unicast(addr, Some(chaddr)) => {
                        info!("sending {:?} to ip {} chaddr {}", typ, addr, chaddr);
                        (addr, Some(chaddr))
                    }
                };
                sender.set_ip(addr);
                Ok(Some((sender, message, chaddr)))
            }
        }
    }
}

async fn define_msg_handling_loop_future<DS: DataStore>(
    sock: SocketWithId<<Server<DS> as SocketServerDispatcher>::Socket>,
    server: &RefCell<ServerDispatcherRuntime<Server<DS>>>,
) -> Result<Infallible, Error> {
    let SocketWithId { socket, iface_id } = sock;
    let mut handler = MessageHandler::new(server);
    let mut buf = vec![0u8; BUF_SZ];
    loop {
        let (received, sender) =
            socket.recv_from(&mut buf).await.context("failed to read from socket")?;
        let sender = match sender {
            std::net::SocketAddr::V4(sender) => sender,
            std::net::SocketAddr::V6(sender) => {
                return Err(anyhow::anyhow!(
                    "IPv4 socket received datagram from IPv6 sender: {}",
                    sender
                ))
            }
        };
        if let Some((dst, msg, chaddr)) = handler
            .handle_from_sender(&buf[..received], sender)
            .context("failed to handle buffer")?
        {
            let chaddr = if let Some(chaddr) = chaddr {
                chaddr
            } else {
                let response = msg.serialize();
                let sent = socket
                    .send_to(&response, SocketAddr::V4(dst))
                    .await
                    .context("unable to send response")?;
                if sent != response.len() {
                    return Err(anyhow::anyhow!(
                        "sent {} bytes for a message of size {}",
                        sent,
                        response.len()
                    ));
                }
                info!("response sent to {}: {} bytes", dst, sent);
                continue;
            };
            // Packet sockets are necessary here because the on-device Netstack does
            // not yet have a relation linking the `chaddr` MAC address to an IP address.
            let dst_ip: net_types::ip::Ipv4Addr = (*dst.ip()).into();
            // Prefer the ServerIdentifier if set; otherwise use the socket's local address.
            let src_ip: net_types::ip::Ipv4Addr = msg
                .options
                .iter()
                .find_map(|opt| match opt {
                    dhcpv4::protocol::DhcpOption::ServerIdentifier(addr) => Some(addr.clone()),
                    _ => None,
                })
                // TODO(https://fxbug.dev/105402): Eliminate this panic.
                .expect("expect server identifier is always present")
                .into();
            let response = msg.serialize();
            const SERVER_PORT: std::num::NonZeroU16 =
                nonzero_ext::nonzero!(dhcpv4::protocol::SERVER_PORT);
            const CLIENT_PORT: std::num::NonZeroU16 =
                nonzero_ext::nonzero!(dhcpv4::protocol::CLIENT_PORT);
            let udp_builder = UdpPacketBuilder::new(src_ip, dst_ip, Some(SERVER_PORT), CLIENT_PORT);
            // Use the default TTL shared across UNIX systems.
            const TTL: u8 = 64;
            let ipv4_builder = Ipv4PacketBuilder::new(
                src_ip,
                dst_ip,
                TTL,
                packet_formats::ip::Ipv4Proto::Proto(packet_formats::ip::IpProto::Udp),
            );
            let packet = response
                .into_serializer()
                .encapsulate(udp_builder)
                .encapsulate(ipv4_builder)
                .serialize_vec_outer()
                .expect("serialize packet failed")
                .unwrap_b();

            let mut sll_addr = [0; 8];
            (&mut sll_addr[..chaddr.bytes().len()]).copy_from_slice(&chaddr.bytes());
            // TODO(https://fxbug.dev/104557): Add `ETH_P_IP` upstream in the `libc` crate.
            const ETH_P_IP: u16 = 0x0800;
            let sockaddr_ll = libc::sockaddr_ll {
                sll_family: libc::AF_PACKET.try_into().expect("convert sll_family failed"),
                sll_ifindex: iface_id.try_into().expect("convert sll_ifindex failed"),
                // Network order is big endian.
                sll_protocol: ETH_P_IP.to_be(),
                sll_halen: chaddr.bytes().len().try_into().expect("convert chaddr size failed"),
                sll_addr: sll_addr,
                sll_hatype: 0,
                sll_pkttype: 0,
            };

            // TODO(https://fxbug.dev/104559): Move this unsafe code upstream into `socket2`.
            let (_, sock_addr) = unsafe {
                socket2::SockAddr::init(|sockaddr_storage, len_ptr| {
                    (sockaddr_storage as *mut libc::sockaddr_ll).write(sockaddr_ll);
                    len_ptr.write(
                        // Should not panic: libc guarantees that `socklen_t` (which is a `u32`)
                        // can fit the size of any socket address.
                        std::mem::size_of::<libc::sockaddr_ll>()
                            .try_into()
                            .expect("convert sockaddr_ll length failed"),
                    );
                    Ok(())
                })
            }
            .context("initialize socket address failed")?;

            let socket = socket2::Socket::new(
                socket2::Domain::PACKET,
                socket2::Type::DGRAM,
                Some(socket2::Protocol::UDP),
            )
            .context("create packet socket failed")?;
            let sent =
                socket.send_to(packet.as_ref(), &sock_addr).context("unable to send response")?;
            if sent != packet.as_ref().len() {
                return Err(anyhow::anyhow!(
                    "sent {} bytes for a packet of size {}",
                    sent,
                    packet.as_ref().len()
                ));
            }
            info!("response sent to {}: {} bytes", dst, sent);
        }
    }
}

fn define_running_server_fut<'a, S, DS>(
    server: &'a RefCell<ServerDispatcherRuntime<Server<DS>>>,
    socket_stream: S,
) -> impl Future<Output = Result<(), Error>> + 'a
where
    S: futures::Stream<
            Item = ServerSocketCollection<<Server<Stash> as SocketServerDispatcher>::Socket>,
        > + 'static,
    DS: DataStore,
{
    socket_stream.map(Ok).try_for_each(move |socket_collection| async move {
        let ServerSocketCollection { sockets, abort_registration } = socket_collection;
        let msg_loops = futures::future::try_join_all(
            sockets.into_iter().map(|sock| define_msg_handling_loop_future(sock, server)),
        );

        info!("Server starting");
        match futures::future::Abortable::new(msg_loops, abort_registration).await {
            Ok(Ok(v)) => {
                let _: Vec<Infallible> = v;
                Err(anyhow::anyhow!("Server futures finished unexpectedly"))
            }
            Ok(Err(error)) => {
                // There was an error handling the server sockets. Disable the
                // server.
                error!("Server encountered an error: {:?}. Stopping server.", error);
                let () = server.borrow_mut().disable();
                Ok(())
            }
            Err(futures::future::Aborted {}) => {
                info!("Server stopped");
                Ok(())
            }
        }
    })
}

struct ServerSocketCollection<S> {
    sockets: Vec<SocketWithId<S>>,
    abort_registration: futures::future::AbortRegistration,
}

async fn run_server<S, C>(
    stream: fidl_fuchsia_net_dhcp::Server_RequestStream,
    server: &RefCell<ServerDispatcherRuntime<S>>,
    default_params: &dhcpv4::configuration::ServerParameters,
    socket_sink: C,
) -> Result<(), fidl::Error>
where
    S: SocketServerDispatcher,
    C: futures::sink::Sink<ServerSocketCollection<S::Socket>> + Unpin,
    C::Error: std::fmt::Debug,
{
    stream
        .try_fold(socket_sink, |mut socket_sink, request| async move {
            match request {
                fidl_fuchsia_net_dhcp::Server_Request::StartServing { responder } => {
                    responder.send(
                        &mut match server.borrow_mut().enable() {
                            Ok(Some(socket_collection)) => {
                                socket_sink.send(socket_collection).await.map_err(|e| {
                                    error!("Failed to send sockets to sink: {:?}", e);
                                    // Disable the server again to keep a consistent state.
                                    let () = server.borrow_mut().disable();
                                    fuchsia_zircon::Status::INTERNAL
                                })
                            }
                            Ok(None) => {
                                info!("Server already running");
                                Ok(())
                            }
                            Err(status) => Err(status),
                        }
                        .map_err(fuchsia_zircon::Status::into_raw),
                    )
                }
                fidl_fuchsia_net_dhcp::Server_Request::StopServing { responder } => {
                    let () = server.borrow_mut().disable();
                    responder.send()
                }
                fidl_fuchsia_net_dhcp::Server_Request::IsServing { responder } => {
                    responder.send(server.borrow().enabled())
                }
                fidl_fuchsia_net_dhcp::Server_Request::GetOption { code: c, responder: r } => {
                    r.send(&mut server.borrow().dispatch_get_option(c).map_err(|e| e.into_raw()))
                }
                fidl_fuchsia_net_dhcp::Server_Request::GetParameter { name: n, responder: r } => {
                    r.send(&mut server.borrow().dispatch_get_parameter(n).map_err(|e| e.into_raw()))
                }
                fidl_fuchsia_net_dhcp::Server_Request::SetOption { value: v, responder: r } => r
                    .send(
                        &mut server.borrow_mut().dispatch_set_option(v).map_err(|e| e.into_raw()),
                    ),
                fidl_fuchsia_net_dhcp::Server_Request::SetParameter { value: v, responder: r } => r
                    .send(
                        &mut server
                            .borrow_mut()
                            .if_disabled(|s| s.dispatch_set_parameter(v))
                            .map_err(|e| e.into_raw()),
                    ),
                fidl_fuchsia_net_dhcp::Server_Request::ListOptions { responder: r } => {
                    r.send(&mut server.borrow().dispatch_list_options().map_err(|e| e.into_raw()))
                }
                fidl_fuchsia_net_dhcp::Server_Request::ListParameters { responder: r } => r.send(
                    &mut server.borrow().dispatch_list_parameters().map_err(|e| e.into_raw()),
                ),
                fidl_fuchsia_net_dhcp::Server_Request::ResetOptions { responder: r } => r.send(
                    &mut server.borrow_mut().dispatch_reset_options().map_err(|e| e.into_raw()),
                ),
                fidl_fuchsia_net_dhcp::Server_Request::ResetParameters { responder: r } => r.send(
                    &mut server
                        .borrow_mut()
                        .if_disabled(|s| s.dispatch_reset_parameters(&default_params))
                        .map_err(|e| e.into_raw()),
                ),
                fidl_fuchsia_net_dhcp::Server_Request::ClearLeases { responder: r } => r.send(
                    &mut server
                        .borrow_mut()
                        .dispatch_clear_leases()
                        .map_err(fuchsia_zircon::Status::into_raw),
                ),
            }
            .map(|()| socket_sink)
        })
        .await
        // Discard the socket sink.
        .map(|_socket_sink: C| ())
}

#[cfg(test)]
mod tests {
    use super::*;
    use dhcpv4::configuration::ServerParameters;
    use fuchsia_async as fasync;
    use futures::{sink::drain, FutureExt};
    use net_declare::{fidl_ip_v4, std_ip_v4};

    #[derive(Debug, Eq, PartialEq)]
    struct CannedSocket {
        name: String,
        src: Ipv4Addr,
    }

    struct CannedDispatcher {
        params: Option<ServerParameters>,
        mock_leases: u32,
    }

    impl CannedDispatcher {
        fn new() -> Self {
            Self { params: None, mock_leases: 0 }
        }
    }

    impl SocketServerDispatcher for CannedDispatcher {
        type Socket = CannedSocket;

        fn create_socket(name: &str, src: Ipv4Addr) -> std::io::Result<Self::Socket> {
            let name = name.to_string();
            Ok(CannedSocket { name, src })
        }

        fn dispatch_message(&mut self, mut msg: Message) -> Result<ServerAction, ServerError> {
            msg.op = dhcpv4::protocol::OpCode::BOOTREPLY;
            Ok(ServerAction::SendResponse(msg, ResponseTarget::Broadcast))
        }

        fn create_sockets(
            params: &configuration::ServerParameters,
        ) -> std::io::Result<Vec<SocketWithId<Self::Socket>>> {
            let configuration::ServerParameters { bound_device_names, .. } = params;
            bound_device_names
                .iter()
                .map(String::as_str)
                .enumerate()
                .map(|(iface_id, name)| {
                    let iface_id = std::convert::TryInto::try_into(iface_id).map_err(|e| {
                        std::io::Error::new(
                            std::io::ErrorKind::InvalidInput,
                            format!("interface id {} out of range: {}", iface_id, e),
                        )
                    })?;
                    let socket = Self::create_socket(name, Ipv4Addr::UNSPECIFIED)?;
                    Ok(SocketWithId { socket, iface_id })
                })
                .collect()
        }
    }

    impl ServerDispatcher for CannedDispatcher {
        fn try_validate_parameters(&self) -> Result<&ServerParameters, fuchsia_zircon::Status> {
            self.params.as_ref().ok_or(fuchsia_zircon::Status::INVALID_ARGS)
        }

        fn dispatch_get_option(
            &self,
            _code: fidl_fuchsia_net_dhcp::OptionCode,
        ) -> Result<fidl_fuchsia_net_dhcp::Option_, fuchsia_zircon::Status> {
            Ok(fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!("0.0.0.0")))
        }
        fn dispatch_get_parameter(
            &self,
            _name: fidl_fuchsia_net_dhcp::ParameterName,
        ) -> Result<fidl_fuchsia_net_dhcp::Parameter, fuchsia_zircon::Status> {
            Ok(fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: None,
                max: None,
                ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
            }))
        }
        fn dispatch_set_option(
            &mut self,
            _value: fidl_fuchsia_net_dhcp::Option_,
        ) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_set_parameter(
            &mut self,
            _value: fidl_fuchsia_net_dhcp::Parameter,
        ) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_list_options(
            &self,
        ) -> Result<Vec<fidl_fuchsia_net_dhcp::Option_>, fuchsia_zircon::Status> {
            Ok(vec![])
        }
        fn dispatch_list_parameters(
            &self,
        ) -> Result<Vec<fidl_fuchsia_net_dhcp::Parameter>, fuchsia_zircon::Status> {
            Ok(vec![])
        }
        fn dispatch_reset_options(&mut self) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_reset_parameters(
            &mut self,
            _defaults: &dhcpv4::configuration::ServerParameters,
        ) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_clear_leases(&mut self) -> Result<(), fuchsia_zircon::Status> {
            self.mock_leases = 0;
            Ok(())
        }
    }

    const DEFAULT_DEVICE_NAME: &str = "foo13";

    fn default_params() -> dhcpv4::configuration::ServerParameters {
        dhcpv4::configuration::ServerParameters {
            server_ips: vec![std_ip_v4!("192.168.0.1")],
            lease_length: dhcpv4::configuration::LeaseLength {
                default_seconds: 86400,
                max_seconds: 86400,
            },
            managed_addrs: dhcpv4::configuration::ManagedAddresses {
                mask: dhcpv4::configuration::SubnetMask::new(25).unwrap(),
                pool_range_start: std_ip_v4!("192.168.0.0"),
                pool_range_stop: std_ip_v4!("192.168.0.0"),
            },
            permitted_macs: dhcpv4::configuration::PermittedMacs(vec![]),
            static_assignments: dhcpv4::configuration::StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: vec![DEFAULT_DEVICE_NAME.to_string()],
        }
    }

    async fn run_with_server<T, F, Fut>(f: F) -> T
    where
        F: Fn(fidl_fuchsia_net_dhcp::Server_Proxy) -> Fut,
        Fut: Future<Output = T>,
    {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()
                .expect("failed to create proxy");
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        futures::select! {
            res = f(proxy).fuse() => res,
            res = run_server(stream, &server, &defaults, drain()).fuse() => {
                unreachable!("server finished before request: {:?}", res)
            },
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_option_with_subnet_mask_returns_subnet_mask() {
        run_with_server(|proxy| async move {
            assert_eq!(
                proxy
                    .get_option(fidl_fuchsia_net_dhcp::OptionCode::SubnetMask)
                    .await
                    .expect("get_option failed"),
                Ok(fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!("0.0.0.0")))
            );
        })
        .await
    }

    #[fasync::run_until_stalled(test)]
    async fn get_parameter_with_lease_length_returns_lease_length() {
        run_with_server(|proxy| async move {
            assert_eq!(
                proxy
                    .get_parameter(fidl_fuchsia_net_dhcp::ParameterName::LeaseLength)
                    .await
                    .expect("get_parameter failed"),
                Ok(fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                    default: None,
                    max: None,
                    ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
                }))
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_option_with_subnet_mask_returns_unit() {
        run_with_server(|proxy| async move {
            assert_eq!(
                proxy
                    .set_option(&mut fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!(
                        "0.0.0.0"
                    )))
                    .await
                    .expect("set_option failed"),
                Ok(())
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_parameter_with_lease_length_returns_unit() {
        run_with_server(|proxy| async move {
            assert_eq!(
                proxy
                    .set_parameter(&mut fidl_fuchsia_net_dhcp::Parameter::Lease(
                        fidl_fuchsia_net_dhcp::LeaseLength {
                            default: None,
                            max: None,
                            ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
                        },
                    ))
                    .await
                    .expect("set_parameter failed"),
                Ok(())
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_options_returns_empty_vec() {
        run_with_server(|proxy| async move {
            assert_eq!(proxy.list_options().await.expect("list_options failed"), Ok(Vec::new()));
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_parameters_returns_empty_vec() {
        run_with_server(|proxy| async move {
            assert_eq!(
                proxy.list_parameters().await.expect("list_parameters failed"),
                Ok(Vec::new())
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn reset_options_returns_unit() {
        run_with_server(|proxy| async move {
            assert_eq!(proxy.reset_options().await.expect("reset_options failed"), Ok(()));
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn reset_parameters_returns_unit() {
        run_with_server(|proxy| async move {
            assert_eq!(proxy.reset_parameters().await.expect("reset_parameters failed"), Ok(()));
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn clear_leases_returns_unit() {
        run_with_server(|proxy| async move {
            assert_eq!(proxy.clear_leases().await.expect("clear_leases failed"), Ok(()));
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_stop_server() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()
                .expect("failed to create proxy");
        let (socket_sink, mut socket_stream) =
            futures::channel::mpsc::channel::<ServerSocketCollection<CannedSocket>>(1);

        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));
        // Set default parameters to the server so we can create sockets.
        server.borrow_mut().params = Some(default_params());

        let defaults = default_params();

        // Set mock leases that should not change when the server is disabled.
        server.borrow_mut().mock_leases = 1;

        let test_fut = async {
            for () in std::iter::repeat(()).take(3) {
                assert!(
                    !proxy.is_serving().await.expect("query server status request"),
                    "server should not be serving"
                );

                let () = proxy
                    .start_serving()
                    .await
                    .expect("start_serving failed")
                    .map_err(fuchsia_zircon::Status::from_raw)
                    .expect("start_serving returned an error");

                let ServerSocketCollection { sockets, abort_registration } =
                    socket_stream.next().await.expect("Socket stream ended unexpectedly");

                // Assert that the sockets that would be created are correct.
                assert_eq!(
                    sockets,
                    vec![SocketWithId {
                        socket: CannedSocket {
                            name: DEFAULT_DEVICE_NAME.to_string(),
                            src: Ipv4Addr::UNSPECIFIED
                        },
                        iface_id: 0
                    }]
                );

                // Create a dummy future that should be aborted when we disable the
                // server.
                let dummy_fut = futures::future::Abortable::new(
                    futures::future::pending::<()>(),
                    abort_registration,
                );

                assert!(
                    proxy.is_serving().await.expect("query server status request"),
                    "server should be serving"
                );

                let () = proxy.stop_serving().await.expect("stop_serving failed");

                // Dummy future was aborted.
                assert_eq!(dummy_fut.await, Err(futures::future::Aborted {}));
                // Leases were not cleared.
                assert_eq!(server.borrow().mock_leases, 1);

                assert!(
                    !proxy.is_serving().await.expect("query server status request"),
                    "server should no longer be serving"
                );
            }
        };

        let () = futures::select! {
            res = test_fut.fuse() => res,
            res = run_server(stream, &server, &defaults, socket_sink).fuse() => {
                unreachable!("server finished before request: {:?}", res)
            },
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_server_fails_on_bad_params() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()
                .expect("failed to create proxy");
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.start_serving().fuse() => res.expect("start_serving failed"),
            res = run_server(stream, &server, &defaults, drain()).fuse() => {
                unreachable!("server finished before request: {:?}", res)
            },
        }
        .map_err(fuchsia_zircon::Status::from_raw);

        // Must have failed to start the server.
        assert_eq!(res, Err(fuchsia_zircon::Status::INVALID_ARGS));
        // No abort handler must've been set.
        assert!(server.borrow().abort_handle.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_server_fails_on_missing_interface_names() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()
                .expect("failed to create proxy");
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = dhcpv4::configuration::ServerParameters {
            bound_device_names: Vec::new(),
            ..default_params()
        };
        server.borrow_mut().params = Some(defaults.clone());

        let res = futures::select! {
            res = proxy.start_serving().fuse() => res.expect("start_serving failed"),
            res = run_server(stream, &server, &defaults, drain()).fuse() => {
                unreachable!("server finished before request: {:?}", res)
            },
        }
        .map_err(fuchsia_zircon::Status::from_raw);

        // Must have failed to start the server.
        assert_eq!(res, Err(fuchsia_zircon::Status::INVALID_ARGS));
        // No abort handler must've been set.
        assert!(server.borrow().abort_handle.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn disallow_change_parameters_if_enabled() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()
                .expect("failed to create proxy");

        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));
        // Set default parameters to the server so we can create sockets.
        server.borrow_mut().params = Some(default_params());

        let defaults = default_params();

        let test_fut = async {
            let () = proxy
                .start_serving()
                .await
                .expect("start_serving failed")
                .map_err(fuchsia_zircon::Status::from_raw)
                .expect("start_serving returned an error");

            // SetParameter disallowed when the server is enabled.
            assert_eq!(
                proxy
                    .set_parameter(&mut fidl_fuchsia_net_dhcp::Parameter::Lease(
                        fidl_fuchsia_net_dhcp::LeaseLength {
                            default: None,
                            max: None,
                            ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
                        },
                    ))
                    .await
                    .expect("set_parameter FIDL failure")
                    .map_err(fuchsia_zircon::Status::from_raw),
                Err(fuchsia_zircon::Status::BAD_STATE)
            );

            // ResetParameters disallowed when the server is enabled.
            assert_eq!(
                proxy
                    .reset_parameters()
                    .await
                    .expect("reset_parameters FIDL failure")
                    .map_err(fuchsia_zircon::Status::from_raw),
                Err(fuchsia_zircon::Status::BAD_STATE)
            );
        };

        let () = futures::select! {
            res = test_fut.fuse() => res,
            res = run_server(stream, &server, &defaults, drain()).fuse() => {
                unreachable!("server finished before request: {:?}", res)
            },
        };
    }

    /// Test that a malformed message does not cause MessageHandler to return an
    /// error.
    #[test]
    fn handle_failed_parse() {
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));
        let mut handler = MessageHandler::new(&server);
        assert_matches::assert_matches!(
            handler.handle_from_sender(
                &[0xFF, 0x00, 0xBA, 0x03],
                std::net::SocketAddrV4::new(Ipv4Addr::UNSPECIFIED.into(), 0),
            ),
            Ok(None)
        );
    }
}

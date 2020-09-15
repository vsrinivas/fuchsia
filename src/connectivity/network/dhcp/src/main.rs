// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    anyhow::{Context as _, Error},
    argh::FromArgs,
    dhcp::{
        configuration,
        protocol::{Message, SERVER_PORT},
        server::{
            get_server_id_from, Server, ServerAction, ServerDispatcher, ServerError,
            DEFAULT_STASH_ID, DEFAULT_STASH_PREFIX,
        },
    },
    fuchsia_async::{self as fasync, net::UdpSocket, Interval},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::DurationNum,
    futures::{Future, SinkExt, StreamExt, TryFutureExt, TryStreamExt},
    std::{
        cell::RefCell,
        collections::hash_map::Entry,
        collections::HashMap,
        net::{IpAddr, Ipv4Addr, SocketAddr},
        os::unix::io::AsRawFd,
    },
    void::Void,
};

/// A buffer size in excess of the maximum allowable DHCP message size.
const BUF_SZ: usize = 1024;
const DEFAULT_CONFIG_PATH: &str = "/config/data/config.json";
/// The rate in seconds at which expiration DHCP leases are recycled back into the managed address
/// pool. The current value of 5 is meant to facilitate manual testing.
// TODO(atait): Replace with Duration type after it has been updated to const fn.
const EXPIRATION_INTERVAL_SECS: i64 = 5;

enum IncomingService {
    Server(fidl_fuchsia_net_dhcp::Server_RequestStream),
}

/// The Fuchsia DHCP server.
#[derive(Debug, FromArgs)]
#[argh(name = "dhcpd")]
pub struct Args {
    /// the path to the default configuration file consumed by dhcpd if it was unable to access a
    /// fuchsia.stash.Store instance.
    #[argh(option, default = "DEFAULT_CONFIG_PATH.to_string()")]
    pub config: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;
    log::info!("starting");

    let Args { config } = argh::from_env();
    let stash = dhcp::stash::Stash::new(DEFAULT_STASH_ID, DEFAULT_STASH_PREFIX)
        .context("failed to instantiate stash")?;
    let default_params = configuration::load_server_params_from_file(&config)
        .context("failed to load default server parameters from configuration file")?;
    let params = stash.load_parameters().await.unwrap_or_else(|e| {
        log::warn!("failed to load parameters from stash: {:?}", e);
        default_params.clone()
    });

    let options = stash.load_options().await.unwrap_or_else(|e| {
        log::warn!("failed to load options from stash: {:?}", e);
        HashMap::new()
    });
    let cache = stash.load_client_configs().await.unwrap_or_else(|e| {
        log::warn!("failed to load cached client config from stash: {:?}", e);
        HashMap::new()
    });
    let server =
        RefCell::new(ServerDispatcherRuntime::new(Server::new(stash, params, options, cache)));

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Server);
    fs.take_and_serve_directory_handle()?;

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
        Err(e) => log::error!("Failed to start server on startup: {:?}", e),
    }

    let admin_fut =
        fs.then(futures::future::ok).try_for_each_concurrent(None, |incoming_service| async {
            match incoming_service {
                IncomingService::Server(stream) => {
                    run_server(stream, &server, &default_params, socket_sink.clone())
                        .inspect_err(|e| log::warn!("run_server failed: {:?}", e))
                        .await?;
                    Ok(())
                }
            }
        });

    let server_fut = define_running_server_fut(&server, socket_stream);

    log::info!("running");
    let ((), ()) = futures::try_join!(server_fut, admin_fut)?;

    Ok(())
}

trait SocketServerDispatcher: ServerDispatcher {
    type Socket;

    fn create_socket(name: Option<&str>, src: Ipv4Addr) -> Result<Self::Socket, Error>;
    fn dispatch_message(&mut self, msg: Message) -> Result<ServerAction, ServerError>;
}

impl SocketServerDispatcher for Server {
    type Socket = UdpSocket;

    fn create_socket(name: Option<&str>, src: Ipv4Addr) -> Result<Self::Socket, Error> {
        let socket = socket2::Socket::new(
            socket2::Domain::ipv4(),
            socket2::Type::dgram(),
            Some(socket2::Protocol::udp()),
        )?;
        // Since dhcpd may listen to multiple interfaces, we must enable
        // SO_REUSEPORT so that binding the same (address, port) pair to each
        // interface can still succeed.
        let () = socket.set_reuse_port(true)?;
        if let Some(name) = name {
            // There are currently no safe Rust interfaces to set SO_BINDTODEVICE,
            // so we must set it through libc.
            if unsafe {
                libc::setsockopt(
                    socket.as_raw_fd(),
                    libc::SOL_SOCKET,
                    libc::SO_BINDTODEVICE,
                    name.as_ptr() as *const libc::c_void,
                    name.len() as libc::socklen_t,
                )
            } == -1
            {
                return Err(anyhow::format_err!(
                    "setsockopt(SO_BINDTODEVICE) failed for {}: {}",
                    name,
                    std::io::Error::last_os_error()
                ));
            }
        }
        let () = socket.set_broadcast(true)?;
        let () = socket.bind(&SocketAddr::new(IpAddr::V4(src), SERVER_PORT).into())?;
        Ok(UdpSocket::from_socket(socket.into_udp_socket())?)
    }

    fn dispatch_message(&mut self, msg: Message) -> Result<ServerAction, ServerError> {
        self.dispatch(msg)
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

        let sockets = create_sockets_from_params::<S>(params).map_err(|e| {
            log::error!("Failed to create server sockets: {}", e);
            fuchsia_zircon::Status::IO
        })?;
        if sockets.is_empty() {
            log::error!("No sockets to run server on");
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

fn create_sockets_from_params<S: SocketServerDispatcher>(
    params: &configuration::ServerParameters,
) -> Result<Vec<S::Socket>, Error> {
    Ok(if params.bound_device_names.len() > 0 {
        params.bound_device_names.iter().map(String::as_str).try_fold::<_, _, Result<_, Error>>(
            Vec::new(),
            |mut acc, name| {
                let sock = S::create_socket(Some(name), Ipv4Addr::UNSPECIFIED)?;
                let () = acc.push(sock);
                Ok(acc)
            },
        )?
    } else {
        vec![S::create_socket(None, Ipv4Addr::UNSPECIFIED)?]
    })
}

/// Helper struct to handle buffer data from sockets.
struct MessageHandler<'a, S: SocketServerDispatcher> {
    server: &'a RefCell<ServerDispatcherRuntime<S>>,
    send_socks: HashMap<Ipv4Addr, S::Socket>,
}

impl<'a, S: SocketServerDispatcher> MessageHandler<'a, S> {
    /// Creates a new `MessageHandler` for `server`.
    fn new(server: &'a RefCell<ServerDispatcherRuntime<S>>) -> Self {
        Self { server, send_socks: HashMap::new() }
    }

    /// Handles `buf` from `sender`.
    ///
    /// Returns `Ok(Some(sock, dst, data))` if a `data` must be sent to `dst`
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
        mut sender: std::net::SocketAddr,
    ) -> Result<Option<(&S::Socket, std::net::SocketAddr, Vec<u8>)>, Error> {
        let msg = match Message::from_buffer(buf) {
            Ok(msg) => {
                log::debug!("parsed message from {}: {:?}", sender, msg);
                msg
            }
            Err(e) => {
                log::warn!("failed to parse message from {}: {}", sender, e);
                return Ok(None);
            }
        };

        let typ = msg.get_dhcp_type();
        if sender.ip().is_unspecified() {
            log::info!("processing {:?} from {}", typ, msg.chaddr);
        } else {
            log::info!("processing {:?} from {}", typ, sender);
        }

        // This call should not block because the server is single-threaded.
        let result = self.server.borrow_mut().dispatch_message(msg);
        match result {
            Err(e) => {
                log::error!("error processing client message: {:?}", e);
                Ok(None)
            }
            Ok(ServerAction::AddressRelease(addr)) => {
                log::info!("released address: {}", addr);
                Ok(None)
            }
            Ok(ServerAction::AddressDecline(addr)) => {
                log::info!("allocated address: {}", addr);
                Ok(None)
            }
            Ok(ServerAction::SendResponse(message, dst)) => {
                log::debug!("generated response: {:?}", message);

                let typ = message.get_dhcp_type();
                // Check if server returned an explicit destination ip.
                if let Some(addr) = dst {
                    log::info!("sending {:?} to {}", typ, addr);

                    sender.set_ip(IpAddr::V4(addr));
                } else {
                    log::info!("sending {:?} to {}", typ, message.chaddr);
                }
                let src =
                    get_server_id_from(&message).ok_or(ServerError::MissingServerIdentifier)?;
                let response_buffer = message.serialize();
                let sock = match self.send_socks.entry(src) {
                    Entry::Occupied(entry) => entry.into_mut(),
                    Entry::Vacant(entry) => entry.insert(S::create_socket(None, src)?),
                };
                Ok(Some((sock, sender, response_buffer)))
            }
        }
    }
}

async fn define_msg_handling_loop_future(
    sock: UdpSocket,
    server: &RefCell<ServerDispatcherRuntime<Server>>,
) -> Result<Void, Error> {
    let mut handler = MessageHandler::new(server);
    let mut buf = vec![0u8; BUF_SZ];
    loop {
        let (received, sender) =
            sock.recv_from(&mut buf).await.context("failed to read from socket")?;
        if let Some((sock, dst, response)) = handler
            .handle_from_sender(&buf[..received], sender)
            .context("failed to handle buffer")?
        {
            sock.send_to(&response, dst).await.context("unable to send response")?;
            log::info!("response sent to {}: {} bytes", dst, response.len());
        }
    }
}

fn define_lease_expiration_handler_future<'a>(
    server: &'a RefCell<ServerDispatcherRuntime<Server>>,
) -> impl Future<Output = Result<(), Error>> + 'a {
    let expiration_interval = Interval::new(EXPIRATION_INTERVAL_SECS.seconds());
    expiration_interval
        .map(move |()| server.borrow_mut().release_expired_leases())
        .map(|_| Ok(()))
        .try_collect::<()>()
}

fn define_running_server_fut<'a, S>(
    server: &'a RefCell<ServerDispatcherRuntime<Server>>,
    socket_stream: S,
) -> impl Future<Output = Result<(), Error>> + 'a
where
    S: futures::Stream<Item = ServerSocketCollection<<Server as SocketServerDispatcher>::Socket>>
        + 'static,
{
    socket_stream.map(Ok).try_for_each(move |socket_collection| async move {
        let ServerSocketCollection { sockets, abort_registration } = socket_collection;
        let msg_loops = futures::future::try_join_all(
            sockets.into_iter().map(|sock| define_msg_handling_loop_future(sock, server)),
        );
        let lease_expiration_handler = define_lease_expiration_handler_future(server);

        let fut = futures::future::try_join(msg_loops, lease_expiration_handler);

        log::info!("Server starting");
        match futures::future::Abortable::new(fut, abort_registration).await {
            Ok(Ok((_void, ()))) => Err(anyhow::anyhow!("Server futures finished unexpectedly")),
            Ok(Err(error)) => {
                // There was an error handling the server sockets or lease
                // expiration. Disable the server.
                log::error!("Server encountered an error: {}. Stopping server.", error);
                let () = server.borrow_mut().disable();
                Ok(())
            }
            Err(futures::future::Aborted {}) => {
                log::info!("Server stopped");
                Ok(())
            }
        }
    })
}

struct ServerSocketCollection<S> {
    sockets: Vec<S>,
    abort_registration: futures::future::AbortRegistration,
}

async fn run_server<S, C>(
    stream: fidl_fuchsia_net_dhcp::Server_RequestStream,
    server: &RefCell<ServerDispatcherRuntime<S>>,
    default_params: &dhcp::configuration::ServerParameters,
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
                                    log::error!("Failed to send sockets to sink: {:?}", e);
                                    // Disable the server again to keep a consistent state.
                                    let () = server.borrow_mut().disable();
                                    fuchsia_zircon::Status::INTERNAL
                                })
                            }
                            Ok(None) => {
                                log::info!("Server already running");
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
    use dhcp::configuration::ServerParameters;
    use futures::{sink::drain, FutureExt};
    use net_declare::{fidl_ip_v4, std_ip_v4};
    use std::convert::TryFrom;

    #[derive(Debug, Eq, PartialEq)]
    struct CannedSocket {
        name: Option<String>,
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

        fn create_socket(name: Option<&str>, src: Ipv4Addr) -> Result<Self::Socket, Error> {
            Ok(CannedSocket { name: name.map(|s| s.to_string()), src })
        }

        fn dispatch_message(&mut self, mut msg: Message) -> Result<ServerAction, ServerError> {
            msg.op = dhcp::protocol::OpCode::BOOTREPLY;
            Ok(ServerAction::SendResponse(msg, None))
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
            Ok(fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!(0.0.0.0)))
        }
        fn dispatch_get_parameter(
            &self,
            _name: fidl_fuchsia_net_dhcp::ParameterName,
        ) -> Result<fidl_fuchsia_net_dhcp::Parameter, fuchsia_zircon::Status> {
            Ok(fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: None,
                max: None,
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
            _defaults: &dhcp::configuration::ServerParameters,
        ) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_clear_leases(&mut self) -> Result<(), fuchsia_zircon::Status> {
            self.mock_leases = 0;
            Ok(())
        }
    }

    fn default_params() -> dhcp::configuration::ServerParameters {
        dhcp::configuration::ServerParameters {
            server_ips: vec![std_ip_v4!(192.168.0.1)],
            lease_length: dhcp::configuration::LeaseLength {
                default_seconds: 86400,
                max_seconds: 86400,
            },
            managed_addrs: dhcp::configuration::ManagedAddresses {
                network_id: std_ip_v4!(192.168.0.0),
                broadcast: std_ip_v4!(192.168.0.128),
                mask: dhcp::configuration::SubnetMask::try_from(25).unwrap(),
                pool_range_start: std_ip_v4!(192.168.0.0),
                pool_range_stop: std_ip_v4!(192.168.0.0),
            },
            permitted_macs: dhcp::configuration::PermittedMacs(vec![]),
            static_assignments: dhcp::configuration::StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: vec![],
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_option_with_subnet_mask_returns_subnet_mask() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.get_option(fidl_fuchsia_net_dhcp::OptionCode::SubnetMask).fuse() => res.context("get_option failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        let expected_result = Ok(fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!(0.0.0.0)));
        assert_eq!(res, expected_result);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn get_parameter_with_lease_length_returns_lease_length() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.get_parameter(fidl_fuchsia_net_dhcp::ParameterName::LeaseLength).fuse() => res.context("get_parameter failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        let expected_result =
            Ok(fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: None,
                max: None,
            }));
        assert_eq!(res, expected_result);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_option_with_subnet_mask_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.set_option(&mut fidl_fuchsia_net_dhcp::Option_::SubnetMask(
            fidl_ip_v4!(0.0.0.0),
        )).fuse() => res.context("set_option failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_parameter_with_lease_length_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.set_parameter(&mut fidl_fuchsia_net_dhcp::Parameter::Lease(
            fidl_fuchsia_net_dhcp::LeaseLength { default: None, max: None },
        )).fuse() => res.context("set_parameter failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_options_returns_empty_vec() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.list_options().fuse() => res.context("list_options failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(vec![]));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_parameters_returns_empty_vec() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.list_parameters().fuse() => res.context("list_parameters failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(vec![]));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn reset_options_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.reset_options().fuse() => res.context("reset_options failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn reset_parameters_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.reset_parameters().fuse() => res.context("reset_parameters failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn clear_leases_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.clear_leases().fuse() => res.context("clear_leases failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_stop_server() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let (socket_sink, mut socket_stream) =
            futures::channel::mpsc::channel::<ServerSocketCollection<CannedSocket>>(1);

        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));
        // Set default parameters to the server so we can create sockets.
        server.borrow_mut().params = Some(default_params());

        let defaults = default_params();

        // Set mock leases that should not change when the server is disabled.
        server.borrow_mut().mock_leases = 1;

        let test_fut = async {
            for _ in 0..3 {
                assert!(
                    !proxy.is_serving().await.context("query server status request")?,
                    "server should not be serving"
                );

                let () = proxy
                    .start_serving()
                    .await
                    .context("start_serving failed")?
                    .map_err(fuchsia_zircon::Status::from_raw)
                    .context("start_serving returned an error")?;

                let socket_collection = socket_stream
                    .next()
                    .await
                    .ok_or_else(|| anyhow::anyhow!("Socket stream ended unexpectedly"))?;

                // Assert that the sockets that would be created are correct.
                assert_eq!(
                    socket_collection.sockets,
                    vec![CannedSocket { name: None, src: Ipv4Addr::UNSPECIFIED }]
                );

                // Create a dummy future that should be aborted when we disable the
                // server.
                let dummy_fut = futures::future::Abortable::new(
                    futures::future::pending::<()>(),
                    socket_collection.abort_registration,
                );

                assert!(
                    proxy.is_serving().await.context("query server status request")?,
                    "server should be serving"
                );

                let () = proxy.stop_serving().await.context("stop_serving failed")?;

                // Dummy future was aborted.
                assert_eq!(dummy_fut.await, Err(futures::future::Aborted {}));
                // Leases were not cleared.
                assert_eq!(server.borrow().mock_leases, 1);

                assert!(
                    !proxy.is_serving().await.context("query server status request")?,
                    "server should no longer be serving"
                );
            }

            Ok::<(), Error>(())
        };

        let () = futures::select! {
            res = test_fut.fuse() => res.context("test future failed"),
            server_fut = run_server(stream, &server, &defaults, socket_sink).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_server_fails_on_bad_params() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.start_serving().fuse() => res.context("start_serving failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?.map_err(fuchsia_zircon::Status::from_raw);

        // Must have failed to start the server.
        assert_eq!(res, Err(fuchsia_zircon::Status::INVALID_ARGS));
        // No abort handler must've been set.
        assert!(server.borrow().abort_handle.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn disallow_change_parameters_if_enabled() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;

        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));
        // Set default parameters to the server so we can create sockets.
        server.borrow_mut().params = Some(default_params());

        let defaults = default_params();

        let test_fut = async {
            let () = proxy
                .start_serving()
                .await
                .context("start_serving failed")?
                .map_err(fuchsia_zircon::Status::from_raw)
                .context("start_serving returned an error")?;

            // SetParameter disallowed when the server is enabled.
            assert_eq!(
                proxy
                    .set_parameter(&mut fidl_fuchsia_net_dhcp::Parameter::Lease(
                        fidl_fuchsia_net_dhcp::LeaseLength { default: None, max: None },
                    ))
                    .await
                    .context("set_parameter FIDL failure")?
                    .map_err(fuchsia_zircon::Status::from_raw),
                Err(fuchsia_zircon::Status::BAD_STATE)
            );

            // ResetParameters disallowed when the server is enabled.
            assert_eq!(
                proxy
                    .reset_parameters()
                    .await
                    .context("reset_parameters FIDL failure")?
                    .map_err(fuchsia_zircon::Status::from_raw),
                Err(fuchsia_zircon::Status::BAD_STATE)
            );

            Ok::<(), Error>(())
        };

        let () = futures::select! {
            res = test_fut.fuse() => res.context("test future failed"),
            server_fut = run_server(stream, &server, &defaults, drain()).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        Ok(())
    }

    /// Test that a malformed message does not cause MessageHandler to return an
    /// error.
    #[test]
    fn test_handle_failed_parse() {
        let server = RefCell::new(ServerDispatcherRuntime::new(CannedDispatcher::new()));
        let mut handler = MessageHandler::new(&server);
        matches::assert_matches!(
            handler.handle_from_sender(
                &[0xFF, 0x00, 0xBA, 0x03],
                std::net::SocketAddr::new(Ipv4Addr::UNSPECIFIED.into(), 0),
            ),
            Ok(None)
        );
    }
}

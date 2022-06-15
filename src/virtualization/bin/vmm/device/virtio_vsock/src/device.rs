// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/97355): Remove once the device is complete.
#![allow(dead_code)]

use {
    crate::connection::{VsockConnection, VsockConnectionKey},
    crate::connection_states::StateAction,
    crate::port_manager::PortManager,
    crate::wire::{OpType, VirtioVsockConfig, VirtioVsockHeader, VsockType, LE64},
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_virtualization::{
        HostVsockAcceptorProxy, HostVsockEndpointConnect2Responder,
        HostVsockEndpointListenResponder,
    },
    fuchsia_syslog as syslog, fuchsia_zircon as zx,
    std::{cell::RefCell, collections::HashMap, convert::TryFrom, io::Read, mem, rc::Rc},
    virtio_device::{chain::ReadableChain, mem::DriverMem, queue::DriverNotify},
    zerocopy::FromBytes,
};

pub struct VsockDevice {
    // Device configuration. This currently only stores the guest CID, which should not change
    // during the lifetime of this device after being set during startup.
    pub config: RefCell<VirtioVsockConfig>,

    // Active connections in all states. Connections are uniquely keyed by guest/host port, and
    // multiple connections can be multiplexed over the guest or host port as long as the pair is
    // unique.
    pub connections: RefCell<HashMap<VsockConnectionKey, Rc<VsockConnection>>>,

    // Acceptors registered by clients listening on a given host port. When a guest initiates a
    // connection on a host port, a client must already be listening on that port.
    pub listeners: RefCell<HashMap<u32, HostVsockAcceptorProxy>>,

    // Tracks port usage and allocation. The port manager will allow multiplexing over a single
    // port, but disallow identical connections.
    pub port_manager: RefCell<PortManager>,
}

impl VsockDevice {
    pub fn new() -> Rc<Self> {
        Rc::new(Self {
            config: RefCell::new(VirtioVsockConfig::new_with_default_cid()),
            connections: RefCell::new(HashMap::new()),
            listeners: RefCell::new(HashMap::new()),
            port_manager: RefCell::new(PortManager::new()),
        })
    }

    // Set the guest context ID. This should not change over the lifetime of this device as we
    // do not support migrations, and is set via the Start VirtioVsock FIDL protocol.
    pub fn set_guest_cid(&self, guest_cid: u32) -> Result<(), Error> {
        if VsockDevice::is_reserved_guest_cid(guest_cid) {
            return Err(anyhow!("{} is reserved and cannot be used as the guest CID", guest_cid));
        }

        self.config.borrow_mut().guest_cid = LE64::new(guest_cid.into());
        Ok(())
    }

    pub fn guest_cid(&self) -> u32 {
        // The upper 32 bits of this u64 are reserved, and unused.
        self.config.borrow().guest_cid.get() as u32
    }

    // Handles a TX readable chain. The device is responsible for extracting the header and then
    // 1) Creating new connections
    // 2) Resetting failed connections
    // 3) Delegating sending TX data from the guest to the client to an existing connection
    //
    // Note that if there is an error that is recoverable, we log it, reset the offending
    // connection, and return Ok to avoid stopping the device.
    pub async fn handle_tx_queue<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), anyhow::Error> {
        let header = match VsockDevice::read_header(&mut chain) {
            Ok(header) => header,
            Err(err) => {
                syslog::fx_log_err!("{}", err);
                return Ok(());
            }
        };

        let key = VsockConnectionKey::new(header.dst_port.get(), header.src_port.get());

        let result = if let Err(err) = self.validate_incoming_header(&header) {
            Err(anyhow!("Received invalid header {:?} with error {}", header, err))
        } else {
            match OpType::try_from(header.op.get())? {
                OpType::Request => match chain.return_complete() {
                    Ok(()) => self
                        .guest_initiated_connect(key)
                        .await
                        .context("Failed guest initiated connect"),
                    Err(err) => Err(anyhow!("Failed to complete chain: {}", err)),
                },
                op => {
                    if let Some(connection) = self.connections.borrow().get(&key) {
                        connection.handle_guest_tx(op, header, chain)
                    } else {
                        Err(anyhow!("Received packet for non-existent connection: {:?}", key))
                    }
                }
            }
        };

        // The device treats all guest TX errors except header deserialization failures as
        // recoverable, and so simply closes the connection and allows the guest to restart it.
        if let Err(err) = result {
            syslog::fx_log_err!(
                "Failed to handle tx packet for connection {:?} with error {}",
                key,
                err
            );
            self.force_close_connection(key);
        }

        Ok(())
    }

    // Listens on a given host port via the Listen HostVsockEndpoint FIDL protocol. There can only
    // be a single listener per host port. If there is already a listener this will respond to the
    // client with zx::Status::ALREADY_BOUND.
    pub async fn listen(
        &self,
        host_port: u32,
        acceptor: HostVsockAcceptorProxy,
        responder: HostVsockEndpointListenResponder,
    ) -> Result<(), fidl::Error> {
        if let Err(err) = self.port_manager.borrow_mut().add_listener(host_port) {
            return responder.send(&mut Err(err.into_raw()));
        }

        let closed = acceptor.on_closed().extend_lifetime();
        if let Some(_) = self.listeners.borrow_mut().insert(host_port, acceptor) {
            panic!("Client already listening on port {} but the port was untracked", host_port);
        };
        responder.send(&mut Ok(()))?;

        if let Err(err) = closed.await {
            panic!("Failed to wait on peer closed signal: {}", err);
        };

        if let None = self.listeners.borrow_mut().remove(&host_port) {
            panic!("Port {} not found in listening list when attempting to remove", host_port);
        }

        self.port_manager.borrow_mut().remove_listener(host_port);
        Ok(())
    }

    // Creates a client initiated connection via the Connect2 HostVsockEndpoint FIDL protocol. May
    // respond with:
    // - A zx::socket if the guest allows the connection
    // - zx::Status::NO_RESOURCES if a host port cannot be allocated
    // - zx::Status::CONNECTION_REFUSED if the guest refuses the connection
    pub async fn client_initiated_connect(
        &self,
        guest_port: u32,
        responder: HostVsockEndpointConnect2Responder,
    ) -> Result<(), fidl::Error> {
        let connection = {
            let host_port = self.port_manager.borrow_mut().find_unused_ephemeral_port();
            if let Err(err) = host_port {
                syslog::fx_log_err!(
                    "Exhausted all ephemeral ports when handling a client initiated connection"
                );
                return responder.send(&mut Err(err.into_raw()));
            }

            let key = VsockConnectionKey::new(host_port.unwrap(), guest_port);
            if let Err(_) = self.register_connection_ports(key) {
                panic!(
                    "Client initiated connections should never be duplicates \
                since the device chooses the host port: {:?}",
                    key
                );
            }

            let connection = Rc::new(VsockConnection::new_client_initiated(key, responder));
            self.connections.borrow_mut().insert(key, connection.clone());
            connection
        };

        // TODO(fxb/97355): Notify RX select! about a new connection.

        // This will not return until it removes the connection from the active connection set.
        self.poll_connection_for_actions(connection).await;

        Ok(())
    }

    // Creates a guest initiated connection, which is done via the guest TX queue. This requires
    // that a client is already listening on the specified host port.
    async fn guest_initiated_connect(&self, key: VsockConnectionKey) -> Result<(), Error> {
        let connection = {
            let listeners = self.listeners.borrow();
            let acceptor = match listeners.get(&key.host_port) {
                Some(acceptor) => acceptor,
                None => {
                    return Err(anyhow!("No client listening on host port: {}", key.host_port));
                }
            };

            if self.register_connection_ports(key).is_err() {
                return Err(anyhow!("Connection already exists: {:?}", key));
            }

            let response = acceptor.accept(self.guest_cid(), key.guest_port, key.host_port);
            let connection = Rc::new(VsockConnection::new_guest_initiated(key, response));
            self.connections.borrow_mut().insert(key, connection.clone());
            connection
        };

        // TODO(fxb/97355): Notify RX select! about a new connection.

        // This will not return until it removes the connection from the active connection set.
        self.poll_connection_for_actions(connection).await;

        Ok(())
    }

    async fn poll_connection_for_actions(&self, connection: Rc<VsockConnection>) {
        match connection.handle_state_action().await {
            StateAction::UpdateState(_) | StateAction::ContinueAwaiting => {
                panic!(
                    "A connection should never ask the device to handle UpdateState or \
                    ContinueAwaiting"
                )
            }
            StateAction::CleanShutdown => {
                self.port_manager.borrow_mut().remove_connection(connection.key());
                if let None = self.connections.borrow_mut().remove(&connection.key()) {
                    panic!("Device lost track of connection: {:?}", connection.key());
                }
            }
            StateAction::ForcedShutdown => {
                self.port_manager.borrow_mut().remove_connection_unclean(connection.key());
                if let None = self.connections.borrow_mut().remove(&connection.key()) {
                    panic!("Device lost track of connection: {:?}", connection.key());
                }
            }
        };
    }

    // Read a VirtioVsockHeader from the chain. Note that this header may be spread across
    // multiple descriptors.
    fn read_header<'a, 'b, N: DriverNotify, M: DriverMem>(
        chain: &mut ReadableChain<'a, 'b, N, M>,
    ) -> Result<VirtioVsockHeader, Error> {
        let mut header_buf = [0u8; mem::size_of::<VirtioVsockHeader>()];
        chain.read_exact(&mut header_buf).context(format!(
            "Failed to read {} bytes for the header",
            mem::size_of::<VirtioVsockHeader>(),
        ))?;
        match VirtioVsockHeader::read_from(header_buf.as_slice()) {
            Some(header) => Ok(header),
            None => Err(anyhow!("Failed to deserialize VirtioVsockHeader")),
        }
    }

    // Move the connection into a forced shutdown state. If the connection doesn't exist, sends
    // a reset packet for that connection.
    fn force_close_connection(&self, key: VsockConnectionKey) {
        if let Some(connection) = self.connections.borrow().get(&key) {
            connection.force_close_connection();
        } else {
            self.send_reset_packet(key);
        }
    }

    // Validates the incoming header for the basic supported fields. When a connection handles
    // a specific OpType, it may enforce additional validation.
    fn validate_incoming_header(&self, header: &VirtioVsockHeader) -> Result<(), Error> {
        if header.src_cid.get() != self.guest_cid().into() {
            return Err(anyhow!(
                "src_cid {} does not match guest cid {}",
                header.src_cid.get(),
                self.guest_cid()
            ));
        }

        if header.dst_cid.get() != fidl_fuchsia_virtualization::HOST_CID.into() {
            return Err(anyhow!(
                "dst_cid {} does not match host cid {}",
                header.dst_cid.get(),
                fidl_fuchsia_virtualization::HOST_CID
            ));
        }

        let vsock_type = VsockType::try_from(header.vsock_type.get())?;
        if vsock_type != VsockType::Stream {
            // TODO(fxb/97355): Add SeqSequence support.
            return Err(anyhow!("The vsock device only supports Stream, not SeqSequence"));
        }

        let op = OpType::try_from(header.op.get())?;
        if op == OpType::Invalid {
            return Err(anyhow!("Recevied OpType::Invalid"));
        }

        Ok(())
    }

    // Attempts to reserve the requested ports for a new connection. If the ports are already
    // reserved, returns zx::Status::ALREADY_EXISTS.
    fn register_connection_ports(&self, key: VsockConnectionKey) -> Result<(), zx::Status> {
        let connection_tracked = self.connections.borrow().contains_key(&key);
        let ports_available = self.port_manager.borrow_mut().add_connection(key);

        if connection_tracked && ports_available.is_ok() {
            // Connections will always stop being tracked before ports are released (or at the same
            // time for clean disconnects).
            panic!("Connection {:?} is being tracked but the ports are not marked as in use.", key);
        }

        ports_available
    }

    // Send a reset packet for a given connection key. Only used if there's no matching connection,
    // as the connection will send a reset itself when it's forced disconnected.
    fn send_reset_packet(&self, _key: VsockConnectionKey) {
        // TODO(fxb/97355): Implement this.
    }

    fn is_reserved_guest_cid(guest_cid: u32) -> bool {
        // 5.10.4 Device configuration layout
        //
        // The following CIDs are reserved and cannot be used as the guest's context ID.
        let reserved = [0, 1, 2, 0xffffffff];
        reserved.contains(&guest_cid)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::wire::{LE16, LE32},
        fidl::endpoints::{create_proxy_and_stream, create_request_stream},
        fidl_fuchsia_virtualization::{
            HostVsockAcceptorMarker, HostVsockEndpointMarker, HostVsockEndpointProxy,
            HostVsockEndpointRequest,
        },
        fuchsia_async as fasync,
        futures::{StreamExt, TryStreamExt},
        std::task::Poll,
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
        zerocopy::AsBytes,
    };

    async fn handle_host_vsock_endpoint_stream(
        device: Rc<VsockDevice>,
        request: HostVsockEndpointRequest,
    ) {
        let device_ = device.clone();
        match request {
            HostVsockEndpointRequest::Listen { port, acceptor, responder } => device_
                .listen(port, acceptor.into_proxy().unwrap(), responder)
                .await
                .expect("failed to respond to listen request"),
            HostVsockEndpointRequest::Connect2 { guest_port, responder } => device_
                .client_initiated_connect(guest_port, responder)
                .await
                .expect("failed to respond to client initiated connect"),
        }
    }

    fn serve_host_vsock_endpoints(device: Rc<VsockDevice>) -> HostVsockEndpointProxy {
        let (proxy, stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");
        fasync::Task::local(async move {
            stream
                .for_each_concurrent(None, |request| {
                    handle_host_vsock_endpoint_stream(
                        device.clone(),
                        request.expect("failed to get request"),
                    )
                })
                .await
        })
        .detach();
        proxy
    }

    fn simple_header(src_port: u32, host_port: u32, len: u32, op: OpType) -> VirtioVsockHeader {
        VirtioVsockHeader {
            src_cid: LE64::new(fidl_fuchsia_virtualization::DEFAULT_GUEST_CID.into()),
            dst_cid: LE64::new(fidl_fuchsia_virtualization::HOST_CID.into()),
            src_port: LE32::new(src_port),
            dst_port: LE32::new(host_port),
            len: LE32::new(len),
            vsock_type: LE16::new(VsockType::Stream.into()),
            op: LE16::new(op.into()),
            flags: LE32::new(0),
            buf_alloc: LE32::new(64),
            fwd_cnt: LE32::new(0),
        }
    }

    #[fuchsia::test]
    async fn check_reserved_cids() {
        // The host CID should be reserved, while the default guest CID should not be.
        assert!(VsockDevice::is_reserved_guest_cid(fidl_fuchsia_virtualization::HOST_CID));
        assert!(!VsockDevice::is_reserved_guest_cid(
            fidl_fuchsia_virtualization::DEFAULT_GUEST_CID
        ));
    }

    #[fuchsia::test]
    async fn parse_header_from_multiple_descriptors_in_chain() {
        let header = simple_header(1, 2, 0, OpType::Request);
        let header_bytes = header.as_bytes();
        let header_size = header_bytes.len();

        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        // Split the header over three non-equally sized descriptors. The guest is free to
        // arbitrarily fragment the header in this chain.
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .readable(&header_bytes[..header_size / 4], &mem)
                    .readable(&header_bytes[header_size / 4..header_size / 2], &mem)
                    .readable(&header_bytes[header_size / 2..], &mem)
                    .build(),
            )
            .expect("failed to publish readable chains");

        let parsed_header = VsockDevice::read_header(&mut ReadableChain::new(
            state.queue.next_chain().expect("failed to get next chain"),
            &mem,
        ))
        .expect("failed to read header from chain");
        assert_eq!(parsed_header, header);
    }

    #[fuchsia::test]
    async fn chain_does_not_contain_header() {
        let header = simple_header(1, 2, 0, OpType::Request);
        let header_bytes = header.as_bytes();

        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        // This chain doesn't contain a full header.
        state
            .fake_queue
            .publish(
                ChainBuilder::new().readable(&header_bytes[..header_bytes.len() / 2], &mem).build(),
            )
            .expect("failed to publish readable chain");

        let result = VsockDevice::read_header(&mut ReadableChain::new(
            state.queue.next_chain().expect("failed to get next chain"),
            &mem,
        ));
        assert!(result.is_err());
    }

    #[test]
    fn register_client_listener_and_connect_on_port() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create test executor");
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        let device = VsockDevice::new();

        let (client_end, mut client_stream) = create_request_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");

        let mut listen_fut = proxy.listen(12345, client_end);
        assert!(executor.run_until_stalled(&mut listen_fut).is_pending());

        let responder_fut =
            if let Poll::Ready(val) = executor.run_until_stalled(&mut stream.try_next()) {
                handle_host_vsock_endpoint_stream(device.clone(), val.unwrap().unwrap())
            } else {
                panic!("Expected future to be ready")
            };
        futures::pin_mut!(responder_fut);
        assert!(executor.run_until_stalled(&mut responder_fut).is_pending());

        if let Poll::Ready(val) = executor.run_until_stalled(&mut listen_fut) {
            assert!(val.unwrap().is_ok());
        } else {
            panic!("Expected future to be ready");
        };

        // Attempt and fail to connect on a port without a listener.
        let connect_fut = device.guest_initiated_connect(VsockConnectionKey::new(54321, 1));
        futures::pin_mut!(connect_fut);
        if let Poll::Ready(val) = executor.run_until_stalled(&mut connect_fut) {
            assert!(val.is_err());
        } else {
            panic!("Expected future to be ready");
        };
        assert!(device.connections.borrow().is_empty());

        // Successfully connect on a port with a listener.
        let connect_fut = device.guest_initiated_connect(VsockConnectionKey::new(12345, 1));
        futures::pin_mut!(connect_fut);
        assert!(executor.run_until_stalled(&mut connect_fut).is_pending());

        // Respond to the guest's connection request from the client's acceptor.
        let (_client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create sockets");
        if let Poll::Ready(val) = executor.run_until_stalled(&mut client_stream.try_next()) {
            let (src_cid, src_port, port, responder) = val
                .unwrap()
                .unwrap()
                .into_accept()
                .expect("failed to parse message as an Accept call");
            assert_eq!(src_cid, fidl_fuchsia_virtualization::DEFAULT_GUEST_CID);
            assert_eq!(src_port, 1);
            assert_eq!(port, 12345);
            responder.send(&mut Ok(device_socket)).expect("failed to send response to device");
        } else {
            panic!("Expected future to be ready");
        };

        // TODO(fxb/97355): Check that the device sent a reply to the guest instead.
        assert!(device.connections.borrow().contains_key(&VsockConnectionKey::new(12345, 1)));
    }

    #[fuchsia::test]
    async fn register_client_listener_twice_on_same_port() {
        let device = VsockDevice::new();
        let device_proxy = serve_host_vsock_endpoints(device.clone());

        let (client_end1, client_stream1) = create_request_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");
        let result = device_proxy
            .listen(12345, client_end1)
            .await
            .expect("failed to respond to listen request");
        assert!(result.is_ok());

        // Already listening on port 12345.
        let (client_end2, _client_stream2) = create_request_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");
        let result = device_proxy
            .listen(12345, client_end2)
            .await
            .expect("failed to respond to listen request");
        assert_eq!(zx::Status::from_raw(result.unwrap_err()), zx::Status::ALREADY_BOUND);

        // Closing the HostVsockAcceptor server should remove the original listener from the device.
        drop(client_stream1);

        // Now that the first client has stopped listening on the port, another client can register
        // as a listener.
        let (client_end3, _client_stream3) = create_request_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");
        let result = device_proxy
            .listen(12345, client_end3)
            .await
            .expect("failed to respond to listen request");
        assert!(result.is_ok());
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::connection::{VsockConnection, VsockConnectionKey, WantRxChainResult},
    crate::connection_states::{StateAction, VsockConnectionState},
    crate::port_manager::PortManager,
    crate::wire::{OpType, VirtioVsockConfig, VirtioVsockHeader, VsockType, LE64},
    anyhow::{anyhow, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_virtualization::{
        HostVsockAcceptorProxy, HostVsockEndpointConnectResponder, HOST_CID,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self, UnboundedReceiver, UnboundedSender},
        future::{self, LocalBoxFuture},
        select,
        stream::FuturesUnordered,
        StreamExt,
    },
    machina_virtio_device::{GuestMem, WrappedDescChainStream},
    std::{
        cell::{Cell, RefCell},
        collections::HashMap,
        convert::TryFrom,
        io::{Read, Write},
        mem,
        rc::Rc,
    },
    virtio_device::{
        chain::{ReadableChain, WritableChain},
        mem::DriverMem,
        queue::DriverNotify,
    },
    zerocopy::{AsBytes, FromBytes},
};

pub struct VsockDevice {
    // Device configuration. This currently only stores the guest CID, which should not change
    // during the lifetime of this device after being set during startup.
    config: RefCell<VirtioVsockConfig>,

    // Active connections in all states. Connections are uniquely keyed by guest/host port, and
    // multiple connections can be multiplexed over the guest or host port as long as the pair is
    // unique.
    connections: RefCell<HashMap<VsockConnectionKey, Rc<VsockConnection>>>,

    // Acceptors registered by clients listening on a given host port. When a guest initiates a
    // connection on a host port, a client must already be listening on that port.
    listeners: RefCell<HashMap<u32, HostVsockAcceptorProxy>>,

    // Tracks port usage and allocation. The port manager will allow multiplexing over a single
    // port, but disallow identical connections.
    port_manager: RefCell<PortManager>,

    // Multi-producer single-consumer queue for sending header only control packets from the device
    // and connections to the guest. This queue will be drained before regular data packets are
    // put on the guest RX queue.
    control_packet_tx: UnboundedSender<VirtioVsockHeader>,
    control_packet_rx: Cell<Option<UnboundedReceiver<VirtioVsockHeader>>>,

    // Multi-producer single-consumer queue for notifying the RX loop that there is a new
    // connection available to await on.
    new_connection_tx: UnboundedSender<Rc<VsockConnection>>,
    new_connection_rx: Cell<Option<UnboundedReceiver<Rc<VsockConnection>>>>,
}

impl VsockDevice {
    pub fn new() -> Rc<Self> {
        let (control_tx, control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let (connect_tx, connect_rx) = mpsc::unbounded::<Rc<VsockConnection>>();

        Rc::new(Self {
            config: RefCell::new(VirtioVsockConfig::new_with_default_cid()),
            connections: RefCell::new(HashMap::new()),
            listeners: RefCell::new(HashMap::new()),
            port_manager: RefCell::new(PortManager::new()),
            control_packet_tx: control_tx,
            control_packet_rx: Cell::new(Some(control_rx)),
            new_connection_tx: connect_tx,
            new_connection_rx: Cell::new(Some(connect_rx)),
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

    // Handles the TX queue stream, pulling chains off of the stream sequentially and forwarding
    // them to the correct connection. This should only be invoked once, and will return when the
    // stream is closed.
    pub async fn handle_tx_stream<'a, 'b, N: DriverNotify>(
        self: &Rc<Self>,
        mut tx_stream: WrappedDescChainStream<'a, 'b, N>,
        guest_mem: &'a GuestMem,
    ) -> Result<(), Error> {
        while let Some(chain) = tx_stream.next().await {
            let readable_chain = ReadableChain::new(chain, guest_mem);
            self.handle_tx_queue(readable_chain).await?;
        }

        Ok(())
    }

    // Handles a TX readable chain. The device is responsible for extracting the header and then
    // 1) Creating new connections
    // 2) Resetting failed connections
    // 3) Delegating sending TX data from the guest to the client via an existing connection
    //
    // Note that if there is an error that is recoverable, we log it, reset the offending
    // connection, and return Ok to avoid stopping the device.
    async fn handle_tx_queue<'a, 'b, N: DriverNotify, M: DriverMem>(
        self: &Rc<Self>,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), anyhow::Error> {
        let header = match VsockDevice::read_header(&mut chain) {
            Ok(header) => header,
            Err(err) => {
                tracing::error!(%err);
                return Ok(());
            }
        };

        // 5.10.6 Device Operation
        //
        // The upper 32 bits of src_cid and dst_cid are reserved and zeroed.
        let key = VsockConnectionKey::new(
            header.dst_cid.get() as u32,
            header.dst_port.get(),
            header.src_cid.get() as u32,
            header.src_port.get(),
        );

        let result = if let Err(err) = self.validate_incoming_header(&header) {
            Err(anyhow!("Received invalid header {:?} with error {}", header, err))
        } else {
            match OpType::try_from(header.op.get())? {
                OpType::Request => match chain.return_complete() {
                    Ok(()) => match self.guest_initiated_connect(key, &header) {
                        Ok(connection) => {
                            let device = self.clone();
                            fasync::Task::local(async move {
                                device.poll_connection_for_actions(connection).await
                            })
                            .detach();
                            Ok(())
                        }
                        Err(err) => Err(anyhow!("Failed guest initiated connect: {}", err)),
                    },
                    Err(err) => Err(anyhow!("Failed to complete chain: {}", err)),
                },
                op => {
                    let connection = self.connections.borrow().get(&key).map(|conn| conn.clone());
                    if let Some(connection) = connection {
                        connection.handle_guest_tx(op, header, chain).await
                    } else {
                        if op == OpType::Reset {
                            // Ignore spurious Reset packets for non-existent connections.
                            Ok(())
                        } else {
                            Err(anyhow!(
                                "Received spurious {:?} packet for non-existent connection: {:?}",
                                op,
                                key
                            ))
                        }
                    }
                }
            }
        };

        // The device treats all runtime TX errors as recoverable, and so simply closes the
        // connection and allows the guest to restart it.
        if let Err(err) = result {
            tracing::error!(connection_key = ?key, %err, "Failed to handle tx packet");
            self.force_close_connection(key).await;
        }

        Ok(())
    }

    // Handles the RX queue stream, pulling chains off of the stream sequentially and forwarding
    // them to a ready connection. This should only be invoked once, and will return when the
    // stream is closed.
    pub async fn handle_rx_stream<'a, 'b, N: DriverNotify>(
        &self,
        mut rx_stream: WrappedDescChainStream<'a, 'b, N>,
        guest_mem: &'a GuestMem,
    ) -> Result<(), Error> {
        let mut control_packets = self
            .control_packet_rx
            .take()
            .expect("No control packet rx channel; handle_rx_queue was called multiple times");
        let mut new_connections = self
            .new_connection_rx
            .take()
            .expect("No new connection rx channel; handle_rx_queue was called multiple times");

        let mut rx_waiters: FuturesUnordered<LocalBoxFuture<'_, WantRxChainResult>> =
            FuturesUnordered::new();

        // One future which will never return, preventing this from resolving to Poll::Ready(None).
        rx_waiters.push(Box::pin(future::pending::<WantRxChainResult>()));

        while let Some(chain) = rx_stream.next().await {
            let writable_chain = match WritableChain::new(chain, guest_mem) {
                Ok(chain) => chain,
                Err(err) => {
                    // Ignore this chain and continue processing.
                    tracing::error!(%err, "Device received a bad chain on the RX queue");
                    continue;
                }
            };

            self.handle_rx_chain(
                writable_chain,
                &mut control_packets,
                &mut new_connections,
                &mut rx_waiters,
            )
            .await?;
        }

        Ok(())
    }

    // Handles an RX writable chain. Control packets are sent first, followed by data packets.
    // If multiple connections are ready to transmit, the least recently serviced connection is
    // selected.
    //
    // Returns Err only on an unrecoverable error.
    async fn handle_rx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: WritableChain<'a, 'b, N, M>,
        control_packets: &mut UnboundedReceiver<VirtioVsockHeader>,
        new_connections: &mut UnboundedReceiver<Rc<VsockConnection>>,
        rx_waiters: &mut FuturesUnordered<LocalBoxFuture<'_, WantRxChainResult>>,
    ) -> Result<(), Error> {
        let result = match chain.remaining() {
            Ok(remaining) => {
                if remaining.bytes < mem::size_of::<VirtioVsockHeader>() {
                    Err(anyhow!(
                        "Writable chain ({}) is smaller than a vsock header ({})",
                        remaining.bytes,
                        mem::size_of::<VirtioVsockHeader>()
                    ))
                } else {
                    Ok(())
                }
            }
            err => {
                err.map_err(|err| anyhow!("Failed to read bytes remaining: {}", err)).map(|_| ())
            }
        };

        if let Err(err) = result {
            tracing::error!(%err, "Device received bad writable chain");
            return Ok(());
        }

        // Prioritize any control packets. Control packets can be time sensitive (such as
        // completing the shutdown handshake), or be blocking guest TX (credit updates and
        // connection establishments). If RX chains are limited, servicing control packets
        // as soon as possible with what chains are available will allow the most net throughput.
        let control_packet = match control_packets.try_next() {
            Ok(header) => match header {
                None => Err(anyhow!("Unexpected end of control packet stream")),
                header => Ok(header),
            },
            Err(_) => {
                // It's expected that the queue may be empty of control packets.
                Ok(None)
            }
        }?;

        let result = if let Some(header) = control_packet {
            VsockDevice::write_header_only_packet(header, chain)
        } else {
            loop {
                select! {
                    header = control_packets.next() => {
                        break VsockDevice::write_header_only_packet(
                            header.expect("Unexpected end of control packet stream"), chain);
                    },
                    conn = new_connections.next() => {
                        let conn = conn.expect("Unexpected end of new connection stream");
                        rx_waiters.push(Box::pin(conn.want_rx_chain()));
                    },
                    fut = rx_waiters.next() => {
                        let fut = fut.expect("Should never resolve to Poll::Ready(None)");
                        // If the future returns with ReadyForChain, this connection wants this
                        // chain and wants to be rescheduled for future polling. Otherwise, this
                        // connection is dropped and the reference count is decremented.
                        if let WantRxChainResult::ReadyForChain(conn) = fut {
                            let result = conn.handle_guest_rx_chain(chain).await;
                            if result.is_ok() {
                                rx_waiters.push(Box::pin(conn.want_rx_chain()));
                            } else {
                                self.force_close_connection(conn.key()).await;
                            }

                            break result
                        }
                    }
                }
            }
        };

        // Log the error, but return Ok so avoid stopping the device.
        if let Err(err) = result {
            tracing::error!(%err, "Failed to service RX queue");
        }

        Ok(())
    }

    // Listens on a given host port via the Listen HostVsockEndpoint FIDL protocol. There can only
    // be a single listener per host port. If there is already a listener bound to a host port,
    // this will return zx::Status::ALREADY_BOUND.
    pub fn listen(
        self: &Rc<Self>,
        host_port: u32,
        acceptor: HostVsockAcceptorProxy,
    ) -> Result<(), zx::Status> {
        self.port_manager.borrow_mut().add_listener(host_port)?;

        let closed = acceptor.on_closed().extend_lifetime();
        if let Some(_) = self.listeners.borrow_mut().insert(host_port, acceptor) {
            panic!("Client already listening on port {} but the port was untracked", host_port);
        };

        // Remove the Listener when the client closes the acceptor.
        let device = self.clone();
        fasync::Task::local(async move {
            if let Err(err) = closed.await {
                panic!("Failed to wait on peer closed signal: {}", err);
            };

            if let None = device.listeners.borrow_mut().remove(&host_port) {
                panic!("Port {} not found in listening list when attempting to remove", host_port);
            }

            device.port_manager.borrow_mut().remove_listener(host_port);
        })
        .detach();

        Ok(())
    }

    // Creates a client initiated connection via the Connect HostVsockEndpoint FIDL protocol. May
    // respond with:
    // - A zx::socket if the guest allows the connection
    // - zx::Status::NO_RESOURCES if a host port cannot be allocated
    // - zx::Status::CONNECTION_REFUSED if the guest refuses the connection
    pub async fn client_initiated_connect(
        &self,
        guest_port: u32,
        responder: HostVsockEndpointConnectResponder,
    ) -> Result<(), fidl::Error> {
        let connection = {
            let host_port = self.port_manager.borrow_mut().find_unused_ephemeral_port();
            if let Err(err) = host_port {
                tracing::error!(
                    "Exhausted all ephemeral ports when handling a client initiated connection"
                );
                return responder.send(&mut Err(err.into_raw()));
            }

            let key =
                VsockConnectionKey::new(HOST_CID, host_port.unwrap(), self.guest_cid(), guest_port);
            if let Err(_) = self.register_connection_ports(key) {
                panic!(
                    "Client initiated connections should never be duplicates \
                since the device chooses the host port: {:?}",
                    key
                );
            }

            let connection = Rc::new(VsockConnection::new_client_initiated(
                key,
                responder,
                self.control_packet_tx.clone(),
            ));
            self.connections.borrow_mut().insert(key, connection.clone());

            self.new_connection_tx
                .clone()
                .unbounded_send(connection.clone())
                .expect("New connection tx end should never be closed");

            connection
        };

        // This will not return until it removes the connection from the active connection set.
        self.poll_connection_for_actions(connection).await;

        Ok(())
    }

    // Creates a guest initiated connection, which is done via the guest TX queue. This requires
    // that a client is already listening on the specified host port.
    fn guest_initiated_connect(
        &self,
        key: VsockConnectionKey,
        header: &VirtioVsockHeader,
    ) -> Result<Rc<VsockConnection>, Error> {
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
        let connection = Rc::new(VsockConnection::new_guest_initiated(
            key,
            response,
            header,
            self.control_packet_tx.clone(),
        ));
        self.connections.borrow_mut().insert(key, connection.clone());

        self.new_connection_tx
            .clone()
            .unbounded_send(connection.clone())
            .expect("New connection tx end should never be closed");

        Ok(connection)
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
        chain.read_exact(&mut header_buf).map_err(|err| {
            anyhow!(
                "Failed to read {} bytes for the header: {}",
                mem::size_of::<VirtioVsockHeader>(),
                err
            )
        })?;
        match VirtioVsockHeader::read_from(header_buf.as_slice()) {
            Some(header) => Ok(header),
            None => Err(anyhow!("Failed to deserialize VirtioVsockHeader")),
        }
    }

    // Write a header only packet to the chain, and then complete the chain.
    fn write_header_only_packet<'a, 'b, N: DriverNotify, M: DriverMem>(
        header: VirtioVsockHeader,
        mut chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        chain
            .write_all(header.as_bytes())
            .map_err(|err| anyhow!("failed to write to chain: {}", err))
    }

    // Move the connection into a forced shutdown state. If the connection doesn't exist, sends
    // a reset packet for that connection.
    async fn force_close_connection(&self, key: VsockConnectionKey) {
        let connection = self.connections.borrow().get(&key).map(|conn| conn.clone());
        if let Some(connection) = connection {
            connection.force_close_connection(self.control_packet_tx.clone()).await;
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

        if header.dst_cid.get() != HOST_CID.into() {
            return Err(anyhow!(
                "dst_cid {} does not match host cid {}",
                header.dst_cid.get(),
                HOST_CID
            ));
        }

        let vsock_type = VsockType::try_from(header.vsock_type.get())?;
        if vsock_type != VsockType::Stream {
            // TODO(fxbug.dev/108415): Add SeqPacket support.
            return Err(anyhow!("The vsock device only supports Stream, not SeqPacket"));
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
    fn send_reset_packet(&self, key: VsockConnectionKey) {
        VsockConnectionState::send_reset_packet(key, self.control_packet_tx.clone());
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
        async_utils::PollExt,
        fidl::endpoints::{create_proxy_and_stream, create_request_stream},
        fidl_fuchsia_virtualization::{
            HostVsockAcceptorMarker, HostVsockEndpointMarker, HostVsockEndpointProxy,
            HostVsockEndpointRequest, DEFAULT_GUEST_CID,
        },
        futures::{FutureExt, TryStreamExt},
        std::task::Poll,
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
    };

    async fn handle_host_vsock_endpoint_stream(
        device: Rc<VsockDevice>,
        request: HostVsockEndpointRequest,
    ) {
        match request {
            HostVsockEndpointRequest::Listen { port, acceptor, responder } => {
                let result = device.listen(port, acceptor.into_proxy().unwrap());
                responder
                    .send(&mut result.map_err(|err| err.into_raw()))
                    .expect("failed to send listen response");
            }
            HostVsockEndpointRequest::Connect { guest_port, responder } => device
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

    fn simple_guest_to_host_header(
        src_port: u32,
        host_port: u32,
        len: u32,
        op: OpType,
    ) -> VirtioVsockHeader {
        VirtioVsockHeader {
            src_cid: LE64::new(DEFAULT_GUEST_CID.into()),
            dst_cid: LE64::new(HOST_CID.into()),
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
        assert!(VsockDevice::is_reserved_guest_cid(HOST_CID));
        assert!(!VsockDevice::is_reserved_guest_cid(DEFAULT_GUEST_CID));
    }

    #[fuchsia::test]
    async fn parse_header_from_multiple_descriptors_in_chain() {
        let header = simple_guest_to_host_header(1, 2, 0, OpType::Request);
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
        let header = simple_guest_to_host_header(1, 2, 0, OpType::Request);
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
    fn malformed_rx_chain_doesnt_consume_packet() {
        let guest_port = 123;
        let header_size = mem::size_of::<VirtioVsockHeader>() as u32;
        let mut executor = fasync::TestExecutor::new().expect("failed to create test executor");
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        let device = VsockDevice::new();

        let mut control_packets =
            device.control_packet_rx.take().expect("No control packet rx channel");
        let mut new_connections =
            device.new_connection_rx.take().expect("No new connection rx channel");

        let mut request_fut = proxy.connect(guest_port);
        assert!(executor.run_until_stalled(&mut request_fut).is_pending());

        let (port, responder) = if let Poll::Ready(val) =
            executor.run_until_stalled(&mut stream.try_next())
        {
            val.unwrap().unwrap().into_connect().expect("received unexpected response on stream")
        } else {
            panic!("Expected future to be ready")
        };
        assert_eq!(port, guest_port);

        let connect_fut = device.client_initiated_connect(port, responder);
        futures::pin_mut!(connect_fut);
        assert!(executor.run_until_stalled(&mut connect_fut).is_pending());

        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        // This is a bad chain that is smaller than a vsock header. The device can't
        // do anything useful with this, so it just drops it without writing anything. Note that
        // this validation should happen before pulling any packet off of the control packet queue.
        state
            .fake_queue
            .publish(ChainBuilder::new().writable(header_size / 2, &mem).build())
            .unwrap();
        let chain = WritableChain::new(state.queue.next_chain().unwrap(), &mem).unwrap();
        device
            .handle_rx_chain(
                chain,
                &mut control_packets,
                &mut new_connections,
                &mut FuturesUnordered::new(),
            )
            .now_or_never()
            .expect("future should have completed")
            .expect("failed to handle rx queue");

        let used_chain = state.fake_queue.next_used().unwrap();
        assert_eq!(used_chain.written(), 0);

        // Publish a chain that fits a virtio header. The device should write the connection
        // request packet into this.
        state.fake_queue.publish(ChainBuilder::new().writable(header_size, &mem).build()).unwrap();
        let chain = WritableChain::new(state.queue.next_chain().unwrap(), &mem).unwrap();
        device
            .handle_rx_chain(
                chain,
                &mut control_packets,
                &mut new_connections,
                &mut FuturesUnordered::new(),
            )
            .now_or_never()
            .expect("future should have completed")
            .expect("failed to handle rx queue");

        let used_chain = state.fake_queue.next_used().unwrap();
        assert_eq!(used_chain.written(), header_size);
        let (data, len) = used_chain.data_iter().next().unwrap();
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };

        let header = VirtioVsockHeader::read_from(slice).unwrap();

        assert_eq!(header.src_cid.get(), HOST_CID.into());
        assert_eq!(header.dst_cid.get(), DEFAULT_GUEST_CID.into());
        assert_eq!(header.dst_port.get(), guest_port);
        assert_eq!(VsockType::try_from(header.vsock_type.get()).unwrap(), VsockType::Stream);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Request);
    }

    #[fuchsia::test]
    async fn send_reset_packet_for_unknown_connection() {
        let host_port = 123;
        let guest_port = 456;
        let header_size = mem::size_of::<VirtioVsockHeader>() as u32;
        let device = VsockDevice::new();

        let mut control_packets =
            device.control_packet_rx.take().expect("No control packet rx channel");
        let mut new_connections =
            device.new_connection_rx.take().expect("No new connection rx channel");

        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        // Send a readable chain with a Response for a non-existent connection. This should result
        // in a reset packet being sent to the guest via the control packet queue.
        let header = simple_guest_to_host_header(guest_port, host_port, 0, OpType::Response);
        let header_bytes = header.as_bytes();
        state.fake_queue.publish(ChainBuilder::new().readable(header_bytes, &mem).build()).unwrap();
        let chain = ReadableChain::new(state.queue.next_chain().unwrap(), &mem);
        device.handle_tx_queue(chain).await.unwrap();
        state.fake_queue.next_used().unwrap();

        state.fake_queue.publish(ChainBuilder::new().writable(header_size, &mem).build()).unwrap();
        let chain = WritableChain::new(state.queue.next_chain().unwrap(), &mem).unwrap();
        device
            .handle_rx_chain(
                chain,
                &mut control_packets,
                &mut new_connections,
                &mut FuturesUnordered::new(),
            )
            .await
            .expect("failed to handle rx queue");

        let used_chain = state.fake_queue.next_used().unwrap();
        assert_eq!(used_chain.written(), header_size);
        let (data, len) = used_chain.data_iter().next().unwrap();
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };

        let header = VirtioVsockHeader::read_from(slice).unwrap();

        assert_eq!(header.src_cid.get(), HOST_CID.into());
        assert_eq!(header.dst_cid.get(), DEFAULT_GUEST_CID.into());
        assert_eq!(header.src_port.get(), host_port);
        assert_eq!(header.dst_port.get(), guest_port);
        assert_eq!(VsockType::try_from(header.vsock_type.get()).unwrap(), VsockType::Stream);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Reset);
    }

    #[test]
    fn guest_rx_from_two_client_initiated_connections() {
        let guest_port = 12345;
        let header_size = mem::size_of::<VirtioVsockHeader>() as u32;
        let mut executor = fasync::TestExecutor::new().expect("failed to create test executor");
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        let device = VsockDevice::new();

        let mut control_packets =
            device.control_packet_rx.take().expect("No control packet rx channel");
        let mut new_connections =
            device.new_connection_rx.take().expect("No new connection rx channel");
        let mut rx_waiters: FuturesUnordered<LocalBoxFuture<'_, WantRxChainResult>> =
            FuturesUnordered::new();
        rx_waiters.push(Box::pin(future::pending::<WantRxChainResult>()));

        let mut get_client_initiated_connection = |guest_port: u32| -> zx::Socket {
            let mut request_fut = proxy.connect(guest_port);
            assert!(executor.run_until_stalled(&mut request_fut).is_pending());

            // Resolve the server side of each FIDL call, acquiring the responder.
            let (port, responder) = executor
                .run_until_stalled(&mut stream.try_next())
                .expect("future should be ready")
                .unwrap()
                .unwrap()
                .into_connect()
                .expect("received unexpected request on stream");
            assert_eq!(port, guest_port);

            // Do a client initiated connection. This future may advance when polled but will
            // ultimately return pending until the connection is actually closed.
            let connect_fut = device.client_initiated_connect(port, responder);
            futures::pin_mut!(connect_fut);
            assert!(executor.run_until_stalled(&mut connect_fut).is_pending());

            let mem = IdentityDriverMem::new();
            let mut state = TestQueue::new(32, &mem);

            // A client initiated connection will result in an OpType::Request packet sent to the
            // guest, so fetch that request with a writable chain.
            state
                .fake_queue
                .publish(ChainBuilder::new().writable(header_size, &mem).build())
                .expect("failed to publish writable chain");

            let chain = WritableChain::new(state.queue.next_chain().unwrap(), &mem)
                .expect("failed to get next chain");

            device
                .handle_rx_chain(chain, &mut control_packets, &mut new_connections, &mut rx_waiters)
                .now_or_never()
                .expect("future should have completed")
                .expect("failed to handle rx queue");

            let used_chain = state.fake_queue.next_used().expect("no next used chain");
            assert_eq!(used_chain.written(), header_size);
            let (data, len) = used_chain.data_iter().next().expect("nothing on chain");
            let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };

            let header =
                VirtioVsockHeader::read_from(slice).expect("failed to read header from slice");
            assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Request);

            // Respond to the request, which requires the automatically chosen host port.
            let response_header = simple_guest_to_host_header(
                header.dst_port.get(),
                header.src_port.get(),
                0,
                OpType::Response,
            );
            let header_bytes = response_header.as_bytes();
            state
                .fake_queue
                .publish(ChainBuilder::new().readable(header_bytes, &mem).build())
                .unwrap();
            let chain = ReadableChain::new(state.queue.next_chain().unwrap(), &mem);

            let handle_tx_fut = device.handle_tx_queue(chain);
            futures::pin_mut!(handle_tx_fut);
            assert!(executor.run_until_stalled(&mut handle_tx_fut).is_pending());
            assert!(executor.run_until_stalled(&mut connect_fut).is_pending());
            assert!(!executor.run_until_stalled(&mut handle_tx_fut).is_pending());

            executor
                .run_until_stalled(&mut request_fut)
                .expect("future should be ready")
                .expect("expected response")
                .expect("expected socket in response")
        };

        let socket1 = get_client_initiated_connection(guest_port);
        let socket2 = get_client_initiated_connection(guest_port);

        // Helper function that writes the given bytes to a socket, creates an appropriately
        // sized writable chain to read them from the device, and then compares the values.
        let mut send_then_read_bytes = |bytes: &[u8], socket: &zx::Socket| {
            let mem = IdentityDriverMem::new();
            let mut state = TestQueue::new(32, &mem);
            state
                .fake_queue
                .publish(
                    ChainBuilder::new().writable(header_size + bytes.len() as u32, &mem).build(),
                )
                .expect("failed to publish writable chain");

            let chain = WritableChain::new(state.queue.next_chain().unwrap(), &mem)
                .expect("failed to get next chain");
            let rx_fut = device.handle_rx_chain(
                chain,
                &mut control_packets,
                &mut new_connections,
                &mut rx_waiters,
            );

            // Nothing to actually send yet, so the chain waits to be used.
            futures::pin_mut!(rx_fut);
            assert!(executor.run_until_stalled(&mut rx_fut).is_pending());

            assert_eq!(socket.write(bytes).expect("failed to write bytes"), bytes.len());

            // A connection has data to send, so this future now completes.
            assert!(!executor.run_until_stalled(&mut rx_fut).is_pending());

            let used_chain = state.fake_queue.next_used().expect("no next used chain");

            assert_eq!(used_chain.written(), (header_size + bytes.len() as u32));
            let (data, len) =
                used_chain.data_iter().next().expect("there should be one filled descriptor");
            let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };
            assert_eq!(&slice[header_size as usize..], bytes);
        };

        send_then_read_bytes(&[1u8, 2, 3, 4, 5], &socket1);
        send_then_read_bytes(&[1u8, 3, 5, 7], &socket2);
        send_then_read_bytes(&[6u8, 7, 8], &socket1);
        send_then_read_bytes(&[4u8, 3, 2, 1], &socket1);
        send_then_read_bytes(&[7u8, 5, 3, 1], &socket2);
    }

    #[fuchsia::test]
    async fn register_client_listener_and_connect_on_port() {
        let host_port = 12345;
        let guest_port = 5;
        let invalid_host_port = 54321;

        let device = VsockDevice::new();
        let device_proxy = serve_host_vsock_endpoints(device.clone());

        let (client_end, mut client_stream) = create_request_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");

        device_proxy
            .listen(host_port, client_end)
            .await
            .expect("failed to get response to listen request")
            .expect("listen unexpectedly failed");

        // Attempt and fail to connect on a port without a listener.
        let result = device.guest_initiated_connect(
            VsockConnectionKey::new(HOST_CID, invalid_host_port, DEFAULT_GUEST_CID, guest_port),
            &VirtioVsockHeader::default(),
        );
        assert!(result.is_err());

        let mut control_packets =
            device.control_packet_rx.take().expect("No control packet rx channel");
        let mut new_connections =
            device.new_connection_rx.take().expect("No new connection rx channel");

        // Device didn't report this invalid connection.
        assert!(new_connections.try_next().is_err());
        assert!(device.connections.borrow().is_empty());

        // Successfully connect on a port with a listener.
        let guest_connection = device
            .guest_initiated_connect(
                VsockConnectionKey::new(HOST_CID, host_port, DEFAULT_GUEST_CID, guest_port),
                &VirtioVsockHeader::default(),
            )
            .expect("expected connection");

        let device_clone = device.clone();
        fasync::Task::local(async move {
            device_clone.poll_connection_for_actions(guest_connection).await
        })
        .detach();

        // Device reported this new connection.
        let reported_key =
            new_connections.next().await.expect("expected a new connection key").key();
        assert_eq!(
            reported_key,
            VsockConnectionKey::new(HOST_CID, host_port, DEFAULT_GUEST_CID, guest_port)
        );

        // Respond to the guest's connection request from the client's acceptor.
        let (src_cid, src_port, port, responder) = client_stream
            .try_next()
            .await
            .unwrap()
            .unwrap()
            .into_accept()
            .expect("failed to parse message as an Accept call");
        assert_eq!(src_cid, DEFAULT_GUEST_CID);
        assert_eq!(src_port, guest_port);
        assert_eq!(port, host_port);

        let (_client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create sockets");
        responder.send(&mut Ok(device_socket)).expect("failed to send response to device");

        // The device sent a reply to the guest.
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        let header_size = mem::size_of::<VirtioVsockHeader>() as u32;
        state
            .fake_queue
            .publish(ChainBuilder::new().writable(header_size, &mem).build())
            .expect("failed to publish writable chain");
        let chain = WritableChain::new(state.queue.next_chain().unwrap(), &mem)
            .expect("failed to get next chain");

        let mut rx_waiters: FuturesUnordered<LocalBoxFuture<'_, WantRxChainResult>> =
            FuturesUnordered::new();

        // One future which will never return, preventing this from resolving to Poll::Ready(None).
        rx_waiters.push(Box::pin(future::pending::<WantRxChainResult>()));

        device
            .handle_rx_chain(chain, &mut control_packets, &mut new_connections, &mut rx_waiters)
            .await
            .expect("failed to handle rx queue");

        let used_chain = state.fake_queue.next_used().expect("no next used chain");
        assert_eq!(used_chain.written(), header_size);
        let (data, len) = used_chain.data_iter().next().expect("nothing on chain");
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };

        let header = VirtioVsockHeader::read_from(slice).expect("failed to read header from slice");

        assert_eq!(header.src_port.get(), host_port);
        assert_eq!(header.src_cid.get(), HOST_CID.into());
        assert_eq!(header.dst_port.get(), guest_port);
        assert_eq!(header.dst_cid.get(), DEFAULT_GUEST_CID.into());
        assert_eq!(header.len.get(), 0);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Response);
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

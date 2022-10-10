// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::connection_states::{
        ClientInitiated, GuestInitiated, ShutdownForced, StateAction, VsockConnectionState,
    },
    crate::wire::{OpType, VirtioVsockFlags, VirtioVsockHeader},
    anyhow::{anyhow, Error},
    async_lock::{Mutex, RwLock},
    fidl::client::QueryResponseFut,
    fidl_fuchsia_virtualization::HostVsockEndpointConnectResponder,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::UnboundedSender,
        future::{AbortHandle, Abortable, Aborted},
    },
    std::{convert::TryFrom, mem, rc::Rc},
    virtio_device::{
        chain::{ReadableChain, WritableChain},
        mem::DriverMem,
        queue::DriverNotify,
    },
};

#[derive(Clone, Copy, Default, Debug)]
pub struct ConnectionCredit {
    // Running count of bytes transmitted to and from the guest counted by the device. The
    // direction is from the perspective of the guest.
    rx_count: u32,
    tx_count: u32,

    // Snapshot of the guest's view into the device state. If the guest believes that the device
    // has no buffer space available but it does, the device must inform the guest of the actual
    // state with a credit update.
    last_reported_total_buf: u32,
    last_reported_fwd_count: u32,

    // Amount of guest buffer space reported by the guest, and a running count of received bytes.
    guest_buf_alloc: u32,
    guest_fwd_count: u32,
}

impl ConnectionCredit {
    // Running count of bytes sent by the device to the guest. This is allowed to overflow.
    pub fn increment_rx_count(&mut self, count: u32) {
        self.rx_count = self.rx_count.wrapping_add(count);
    }

    // Running count of bytes received by the device. This is allowed to overflow.
    pub fn increment_tx_count(&mut self, count: u32) {
        self.tx_count = self.tx_count.wrapping_add(count);
    }

    // Maximum non-header bytes that can be sent from the device to the guest for this connection.
    pub fn peer_free_bytes(&self) -> u32 {
        self.guest_buf_alloc.saturating_sub(self.rx_count.wrapping_sub(self.guest_fwd_count))
    }

    // Whether the guest thinks that there is no TX buffer available. If "true" and if there is
    // buffer available, the connection must send an unsolicited credit update to the guest.
    pub fn guest_believes_no_tx_buffer_available(&self) -> bool {
        self.last_reported_total_buf
            .saturating_sub(self.tx_count.wrapping_sub(self.last_reported_fwd_count))
            == 0
    }

    // Read guest credit from the header. This controls the amount of data this connection can
    // put on the guest RX queue to avoid starving other connections.
    pub fn read_credit(&mut self, header: &VirtioVsockHeader) {
        self.guest_buf_alloc = header.buf_alloc.get();
        self.guest_fwd_count = header.fwd_cnt.get();
    }

    // Write credit to the header.
    //
    // Errors are non-recoverable, and will result in the connection being reset.
    pub fn write_credit(
        &mut self,
        socket: &fasync::Socket,
        header: &mut VirtioVsockHeader,
    ) -> Result<(), zx::Status> {
        let socket_info = socket.as_ref().info()?;

        let socket_tx_max =
            u32::try_from(socket_info.tx_buf_max).map_err(|_| zx::Status::OUT_OF_RANGE)?;
        let socket_tx_current =
            u32::try_from(socket_info.tx_buf_size).map_err(|_| zx::Status::OUT_OF_RANGE)?;

        // Set the maximum host socket buffer size, and the running count of sent bytes. Note that
        // tx_count is from the perspective of the guest, so fwd_cnt is the number of bytes sent
        // from the guest to the device, minus the bytes sitting in the socket. The guest should
        // not have more outstanding bytes to send than the socket has total buffer space.
        self.last_reported_total_buf = socket_tx_max;
        header.buf_alloc = self.last_reported_total_buf.into();

        self.last_reported_fwd_count = self.tx_count.wrapping_sub(socket_tx_current);
        header.fwd_cnt = self.last_reported_fwd_count.into();

        Ok(())
    }
}

// 5.10.6.2 Addressing
//
// Flows are identified by a (source, destination) address tuple. An address consists of a
// (cid, port number) tuple. This virtio-vsock device only supports a single guest per device,
// and only guest <-> host communication, so host_cid and guest_cid are effectively static.
#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct VsockConnectionKey {
    pub host_cid: u32,
    pub host_port: u32,
    pub guest_cid: u32,
    pub guest_port: u32,
}

impl VsockConnectionKey {
    pub fn new(host_cid: u32, host_port: u32, guest_cid: u32, guest_port: u32) -> Self {
        VsockConnectionKey { host_cid, host_port, guest_cid, guest_port }
    }
}

#[derive(Debug)]
pub struct VsockConnection {
    key: VsockConnectionKey,

    // The current connection state. This carries state specific data for the connection.
    state: RwLock<VsockConnectionState>,

    // Control handles for state dependent futures that the device can wait on. When potentially
    // transitioning states, all futures are cancelled and restarted on the new state.
    rx_abort: Mutex<Option<AbortHandle>>,
    state_action_abort: Mutex<Option<AbortHandle>>,
}

#[derive(Debug)]
pub enum WantRxChainResult {
    // This connection wants the next available RX chain. The connection is returned so that
    // the device can reschedule polling this connection after it is serviced.
    ReadyForChain(Rc<VsockConnection>),

    // This connection will never want another RX chain.
    StopAwaiting,
}

impl PartialEq for WantRxChainResult {
    fn eq(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }
}

impl Drop for VsockConnection {
    fn drop(&mut self) {
        tracing::info!(connection_key = ?self.key, "Destroyed connection");
    }
}

impl VsockConnection {
    // Creates a new guest initiated connection. Requires a listening client, and for the client
    // to respond with a valid zx::socket.
    pub fn new_guest_initiated(
        key: VsockConnectionKey,
        listener_response: QueryResponseFut<Result<zx::Socket, i32>>,
        header: &VirtioVsockHeader,
        control_packets: UnboundedSender<VirtioVsockHeader>,
    ) -> Self {
        VsockConnection::new(
            key,
            VsockConnectionState::GuestInitiated(GuestInitiated::new(
                listener_response,
                control_packets,
                key,
                header,
            )),
        )
    }

    // Creates a new client initiated connection. Requires sending this connection request to the
    // guest, waiting for an affirmative reply, and creating a zx::socket pair for the device
    // and client.
    pub fn new_client_initiated(
        key: VsockConnectionKey,
        responder: HostVsockEndpointConnectResponder,
        control_packets: UnboundedSender<VirtioVsockHeader>,
    ) -> Self {
        VsockConnection::new(
            key,
            VsockConnectionState::ClientInitiated(ClientInitiated::new(
                responder,
                control_packets,
                key,
            )),
        )
    }

    fn new(key: VsockConnectionKey, state: VsockConnectionState) -> Self {
        tracing::info!(connection_key = ?key, "Created connection");
        VsockConnection {
            key,
            state: RwLock::new(state),
            rx_abort: Mutex::new(None),
            state_action_abort: Mutex::new(None),
        }
    }

    pub fn key(&self) -> VsockConnectionKey {
        self.key
    }

    // Handles a TX chain by delegating the chain to the current state after applying the
    // chain's operation to allow for state transitions. Returning an error will cause
    // this connection to be force dropped and a reset packet to be sent to the guest.
    pub async fn handle_guest_tx<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        op: OpType,
        header: VirtioVsockHeader,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let flags = VirtioVsockFlags::from_bits(header.flags.get())
            .ok_or(anyhow!("Unrecognized VirtioVsockFlags in header: {:#b}", header.flags.get()))?;

        // Cancel any pending async tasks as the state may be updated. The tasks waiting on these
        // tasks will re-wait on them.
        let mut state = self.cancel_pending_tasks_and_get_mutable_state().await;

        // Apply the operation to the current state, which may cause a state transition.
        let new_state = mem::take(&mut *state).handle_operation(op, flags, &header);
        *state = new_state;

        // Consume the readable chain. If the chain was not fully walked (such as if the guest
        // thinks the connection is in a different state than it actually is), this will return
        // an error.
        state.handle_tx_chain(op, header, chain)
    }

    // Cancels all abortable futures, acquires a write lock, and returns it. This is the only
    // way a write lock on the current state should be acquired and is safe because:
    // 1) The aborted futures are the only tasks that hold a read lock on the state
    // 2) A RwLock prioritizes writes, so nothing will reacquire a read lock on this state
    //    before this write lock is acquired
    async fn cancel_pending_tasks_and_get_mutable_state(
        &self,
    ) -> async_lock::RwLockWriteGuard<'_, VsockConnectionState> {
        let mut rx_abort = self.rx_abort.lock().await;
        let mut state_action_abort = self.state_action_abort.lock().await;

        if let Some(handle) = rx_abort.take() {
            handle.abort();
        }
        if let Some(handle) = state_action_abort.take() {
            handle.abort();
        }

        self.state.write().await
    }

    // Used by the device in the case of a failure, this immediately moves the connection into
    // a forced shutdown state to be cleaned up.
    pub async fn force_close_connection(
        &self,
        control_packets: UnboundedSender<VirtioVsockHeader>,
    ) {
        *self.cancel_pending_tasks_and_get_mutable_state().await =
            VsockConnectionState::ShutdownForced(ShutdownForced::new(self.key, control_packets));
    }

    // Returns ReadyForChain if this connection wants an RX chain immediately, and StopAwaiting if
    // the device should stop polling this connection for whether it wants an RX chain.
    pub async fn want_rx_chain(self: Rc<Self>) -> WantRxChainResult {
        loop {
            let (abort_handle, abort_registration) = AbortHandle::new_pair();
            *self.rx_abort.lock().await = Some(abort_handle);

            let result = {
                let state = self.state.read().await;
                Abortable::new(state.wants_rx_chain(), abort_registration).await
            };

            match result {
                Ok(true) => {
                    return WantRxChainResult::ReadyForChain(self);
                }
                Ok(false) => {
                    // There may be a state action associated with stopping RX processing such as
                    // a transition into a shutdown state.
                    if let Some(handle) = self.state_action_abort.lock().await.take() {
                        handle.abort();
                    }

                    return WantRxChainResult::StopAwaiting;
                }
                Err(Aborted) => {
                    // Wait on this state again.
                    continue;
                }
            };
        }
    }

    // Handles an RX chain by delegating the chain to the current state. A connection should only
    // receive a writable chain after returning ReadyForChain in response to want_rx_chain.
    pub async fn handle_guest_rx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // Prefer a read lock to avoid cancelling pending tasks, but acquire a write lock if
        // necessary to prevent TX from starving RX as this lock is write preferring.
        if let Some(state) = self.state.try_read() {
            state.handle_rx_chain(chain)
        } else {
            self.cancel_pending_tasks_and_get_mutable_state().await.handle_rx_chain(chain)
        }
    }

    pub async fn handle_state_action(&self) -> StateAction {
        loop {
            let (abort_handle, abort_registration) = AbortHandle::new_pair();
            *self.state_action_abort.lock().await = Some(abort_handle);

            let result = {
                let state = self.state.read().await;
                Abortable::new(state.do_state_action(), abort_registration).await
            };

            match result {
                Ok(result) => {
                    match result {
                        StateAction::UpdateState(new_state) => {
                            *self.cancel_pending_tasks_and_get_mutable_state().await = new_state;
                        }
                        StateAction::ContinueAwaiting => {
                            // The state did something internally.
                            continue;
                        }
                        state => {
                            // The device must handle this result.
                            return state;
                        }
                    }
                }
                Err(Aborted) => {
                    // Wait on this state again.
                    continue;
                }
            };
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::wire::{LE16, LE32},
        async_utils::PollExt,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::{
            HostVsockAcceptorMarker, HostVsockEndpointMarker, DEFAULT_GUEST_CID, HOST_CID,
        },
        futures::{channel::mpsc, TryStreamExt},
        std::{convert::TryInto, io::Read, task::Poll},
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
    };

    // Helper function to simulate putting a header only packet on the guest's TX queue and
    // forwarding it to the given connection. Use for state transitions without any additional data
    // to transmit.
    async fn send_header_to_connection(header: VirtioVsockHeader, connection: &VsockConnection) {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        // Chains cannot initially be zero length, so write a byte and read it off. Note that
        // in practice these chains would have been sizeof(VirtioVsockHeader) but the header
        // has already been removed to choose the correct connection.
        state.fake_queue.publish(ChainBuilder::new().readable(&[0u8], &mem).build()).unwrap();
        let mut empty_chain = ReadableChain::new(state.queue.next_chain().unwrap(), &mem);
        let mut buffer = [0u8; 1];
        empty_chain.read_exact(&mut buffer).expect("failed to read bytes from chain");

        connection
            .handle_guest_tx(
                OpType::try_from(header.op.get()).expect("unknown optype"),
                header,
                empty_chain,
            )
            .await
            .expect("failed to handle an empty tx chain");
    }

    #[fuchsia::test]
    async fn client_initiated_connection_dropped_without_response() {
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        // Drop the connection immediately after creating it. This simulates the guest and device
        // going away before the connection is established.
        fasync::Task::local(async move {
            let (guest_port, responder) = stream
                .try_next()
                .await
                .unwrap()
                .unwrap()
                .into_connect()
                .expect("received unexpected message on stream");
            let connection = VsockConnection::new_client_initiated(
                VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, guest_port),
                responder,
                control_tx,
            );
            drop(connection)
        })
        .detach();

        let result = proxy.connect(12345).await.expect("failed to respond to connect FIDL call");
        assert_eq!(zx::Status::from_raw(result.unwrap_err()), zx::Status::CONNECTION_REFUSED);
    }

    #[fuchsia::test]
    async fn write_credit_with_credit_available() {
        let mut credit = ConnectionCredit::default();
        let (_remote, local) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");

        // Shove some data into the local socket, but less than the maximum.
        let max_tx_bytes = local.info().unwrap().tx_buf_max;
        let tx_bytes_to_use = max_tx_bytes / 2;
        let data = vec![0u8; tx_bytes_to_use];
        assert_eq!(tx_bytes_to_use, local.write(&data).expect("failed to write to socket"));

        let async_local =
            fasync::Socket::from_socket(local).expect("failed to create async socket");

        // Use a mock tx_count, which is normally incremented when the guest transfers data to the
        // device. This is a running total of bytes received by the device, and the difference
        // between this and the bytes pending on the host socket is the bytes actually transferred
        // to a client.
        let bytes_actually_transferred = 100;
        credit.tx_count = (tx_bytes_to_use as u32) + bytes_actually_transferred;

        let mut header = VirtioVsockHeader::default();
        credit.write_credit(&async_local, &mut header).expect("failed to query socket info");

        assert_eq!(header.buf_alloc.get(), max_tx_bytes as u32);
        assert_eq!(header.fwd_cnt.get(), bytes_actually_transferred);
    }

    #[fuchsia::test]
    async fn write_credit_with_no_credit_available() {
        let mut credit = ConnectionCredit::default();
        let (remote, local) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");

        // Max out the host socket, leaving zero bytes available.
        let max_tx_bytes = local.info().unwrap().tx_buf_max;
        let data = vec![0u8; max_tx_bytes];
        assert_eq!(max_tx_bytes, local.write(&data).expect("failed to write to socket"));

        let async_local =
            fasync::Socket::from_socket(local).expect("failed to create async socket");

        // The tx_count being equal to the bytes pending on the socket means that no data has been
        // actually transferred to the client.
        credit.tx_count = max_tx_bytes as u32;

        let mut header = VirtioVsockHeader::default();
        credit.write_credit(&async_local, &mut header).expect("failed to query socket info");

        assert_eq!(header.buf_alloc.get(), max_tx_bytes as u32);
        assert_eq!(header.fwd_cnt.get(), 0); // tx_count == the bytes pending on the socket

        // Read 5 bytes from the socket. Without sending a packet to the guest, the guest still
        // thinks the socket buffer is full even though there is now space.
        let mut buf = [0u8; 5];
        assert_eq!(buf.len(), remote.read(&mut buf).expect("failed to read 5 bytes from socket"));
        assert!(credit.guest_believes_no_tx_buffer_available());

        // Writing the credit to an RX packet updates the guest's view of the device, so now it
        // knows there is some TX buffer available.
        credit.write_credit(&async_local, &mut header).expect("failed to query socket info");
        assert!(!credit.guest_believes_no_tx_buffer_available());
    }

    #[fuchsia::test]
    async fn connection_credit_handles_rx_overflow() {
        let mut credit = ConnectionCredit::default();

        credit.increment_rx_count(u32::MAX - 5);
        credit.read_credit(&VirtioVsockHeader {
            buf_alloc: LE32::new(256),
            fwd_cnt: LE32::new(u32::MAX - 5),
            ..VirtioVsockHeader::default()
        });
        credit.increment_rx_count(10);

        // RX count should wrap around, and peer free should be based on the wrapped difference.
        assert_eq!(credit.peer_free_bytes(), 246);
    }

    #[fuchsia::test]
    async fn connection_credit_handles_tx_overflow() {
        let mut credit = ConnectionCredit::default();
        let (_remote, local) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");

        let max_tx_bytes = local.info().unwrap().tx_buf_max;
        let async_local =
            fasync::Socket::from_socket(local).expect("failed to create async socket");

        credit.increment_tx_count(u32::MAX - 5);
        let mut header = VirtioVsockHeader::default();
        credit.write_credit(&async_local, &mut header).expect("failed to write credit");
        assert!(!credit.guest_believes_no_tx_buffer_available());

        // TX count should have wrapped, and guest_believes_no_tx_buffer_available should be
        // based on this new value.
        credit.increment_tx_count(max_tx_bytes as u32);

        // As far as the guest knows, the socket buffer is now full.
        assert!(credit.guest_believes_no_tx_buffer_available());

        // Guest now knows there's credit available.
        credit.write_credit(&async_local, &mut header).expect("failed to write credit");
        assert!(!credit.guest_believes_no_tx_buffer_available());

        // Simulate the total buffer decreasing.
        credit.increment_tx_count(100);
        credit.last_reported_total_buf = 50;

        // A saturating subtract should allow for buffer size changes.
        assert!(credit.guest_believes_no_tx_buffer_available());
    }

    #[test]
    fn guest_initiated_and_client_closed_connection() {
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");

        let response_fut = proxy.accept(1, 2, 3);
        let connection = Rc::new(VsockConnection::new_guest_initiated(
            VsockConnectionKey::new(HOST_CID, 3, DEFAULT_GUEST_CID, 2),
            response_fut,
            &VirtioVsockHeader::default(),
            control_tx,
        ));

        // State transition relies on client acceptor response.
        let state_action_fut = connection.handle_state_action();
        futures::pin_mut!(state_action_fut);
        assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());

        // Respond to the guest's connection request from the client's acceptor.
        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        if let Poll::Ready(val) = executor.run_until_stalled(&mut stream.try_next()) {
            let (_, _, _, responder) =
                val.unwrap().unwrap().into_accept().expect("received unexpected message on stream");
            responder.send(&mut Ok(device_socket)).expect("failed to send response");
        } else {
            panic!("Expected future to be ready");
        };

        // Device sends response to the guest after receiving a socket from the client.
        assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Response);

        // After transitioning to the read-write state, the device immediately sends a credit
        // to the guest since the guest has no idea what the credit status of the client is and so
        // cannot send any packets.
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::CreditUpdate);

        // Close connection by closing the client socket.
        drop(client_socket);

        let want_rx_fut = connection.clone().want_rx_chain();
        futures::pin_mut!(want_rx_fut);
        assert_eq!(
            executor.run_until_stalled(&mut want_rx_fut).expect("future should have completed"),
            WantRxChainResult::StopAwaiting
        );
    }

    // This test case is testing the following:
    // 1) A client is using the Connect FIDL protocol to initiate a connection to the guest.
    // 2) The guest replies with a Response header, moving the connection to a ready state.
    // 3) The client's responder gets a socket, created by the device.
    // 4) A two descriptor chain with 5 bytes is put on the guest's TX queue, and written into the
    //    local socket.
    // 5) The updated credit is read and verified.
    // 5) The 5 bytes are read off of the remote socket.
    // 6) The guest sends a partial shutdown blocking receive (remote -> local).
    // 7) Writing to the remote socket fails, writing to the local socket succeeds.
    // 8) Another partial shutdown is sent blocking write. This is now a full shutdown, moving the
    //    connection into a guest intiated shutdown state.
    // 9) The device sends the guest a reset packet in confirmation, moving this into a clean
    //    shutdown.
    #[test]
    fn client_initiated_connection_write_data_to_port() {
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        // Stall on waiting for a FIDL response.
        let mut connect_fut = proxy.connect(12345);
        assert!(executor.run_until_stalled(&mut connect_fut).is_pending());

        let (guest_port, responder) = if let Poll::Ready(val) =
            executor.run_until_stalled(&mut stream.try_next())
        {
            val.unwrap().unwrap().into_connect().expect("received unexpected response on stream")
        } else {
            panic!("Expected future to be ready")
        };

        let connection = VsockConnection::new_client_initiated(
            VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, guest_port),
            responder,
            control_tx,
        );

        // A client initiated connection sends a request to the guest which must be responded to.
        let state_action_fut = connection.handle_state_action();
        futures::pin_mut!(state_action_fut);
        assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());
        let header = control_rx.try_next().unwrap().unwrap();
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Request);

        // Transition into a ReadWrite state.
        let send_fut = send_header_to_connection(
            VirtioVsockHeader {
                op: LE16::new(OpType::Response.into()),
                ..VirtioVsockHeader::default()
            },
            &connection,
        );
        futures::pin_mut!(send_fut);
        assert!(executor.run_until_stalled(&mut send_fut).is_pending());
        assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());
        assert!(!executor.run_until_stalled(&mut send_fut).is_pending());

        // Now that the connection state has changed from ClientInitiated, there should be a socket
        // available on the client's FIDL response.
        let socket = if let Poll::Ready(val) = executor.run_until_stalled(&mut connect_fut) {
            val.unwrap().unwrap()
        } else {
            panic!("Expected future to be ready")
        };

        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        // Create a chain with 5 bytes of data to be transmitted by this connection.
        let buf_alloc = 1000;
        let fwd_cnt = 2000;
        let data = [1u8, 2u8, 3u8, 4u8, 5u8];
        let header = VirtioVsockHeader {
            len: LE32::new(data.len().try_into().unwrap()),
            buf_alloc: LE32::new(buf_alloc),
            fwd_cnt: LE32::new(fwd_cnt),
            ..VirtioVsockHeader::default()
        };

        // Split the 5 elements into two descriptors.
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .readable(&data[0..data.len() / 2], &mem)
                    .readable(&data[data.len() / 2..], &mem)
                    .build(),
            )
            .expect("failed to publish readable chains");

        let send_fut = connection.handle_guest_tx(
            OpType::ReadWrite,
            header,
            ReadableChain::new(state.queue.next_chain().unwrap(), &mem),
        );
        futures::pin_mut!(send_fut);
        if let Poll::Ready(val) = executor.run_until_stalled(&mut send_fut) {
            val.expect("failed to handle guest tx chain")
        } else {
            panic!("Expected future to be ready")
        };

        // Remote socket now has data available on it. Validate the amount of bytes written, and
        // that they match the expected values.
        let mut remote_data = [0u8; 5];
        assert_eq!(socket.read(&mut remote_data).unwrap(), data.len());
        assert_eq!(0, socket.outstanding_read_bytes().unwrap());
        assert_eq!(remote_data, data);

        // Close the RX half of the connection from the perspective of the guest. This is not a
        // full shutdown, and so does not transition to a shutdown state.
        let send_fut = send_header_to_connection(
            VirtioVsockHeader {
                op: LE16::new(OpType::Shutdown.into()),
                flags: LE32::new(VirtioVsockFlags::SHUTDOWN_RECIEVE.bits()),
                ..VirtioVsockHeader::default()
            },
            &connection,
        );
        futures::pin_mut!(send_fut);
        assert!(!executor.run_until_stalled(&mut send_fut).is_pending());

        // Cannot write to the remote socket since it's now half closed, but we can happily
        // write to the device socket and read from the remote socket.
        assert_eq!(socket.write(&[0u8]).unwrap_err(), zx::Status::BAD_STATE);

        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        let header = VirtioVsockHeader {
            len: LE32::new(data.len().try_into().unwrap()),
            buf_alloc: LE32::new(buf_alloc),
            fwd_cnt: LE32::new(fwd_cnt),
            ..VirtioVsockHeader::default()
        };

        state
            .fake_queue
            .publish(ChainBuilder::new().readable(&data, &mem).build())
            .expect("failed to publish readable chain");

        let send_fut = connection.handle_guest_tx(
            OpType::ReadWrite,
            header,
            ReadableChain::new(state.queue.next_chain().unwrap(), &mem),
        );
        futures::pin_mut!(send_fut);
        if let Poll::Ready(val) = executor.run_until_stalled(&mut send_fut) {
            val.expect("failed to handle guest tx chain")
        } else {
            panic!("Expected future to be ready")
        };

        let mut remote_data = [0u8; 5];
        assert_eq!(socket.read(&mut remote_data).unwrap(), data.len());
        assert_eq!(0, socket.outstanding_read_bytes().unwrap());
        assert_eq!(remote_data, data);

        // Finally transition to a guest initiated shutdown by closing both halves of this
        // connection.
        let send_fut = send_header_to_connection(
            VirtioVsockHeader {
                op: LE16::new(OpType::Shutdown.into()),
                flags: LE32::new(VirtioVsockFlags::SHUTDOWN_SEND.bits()),
                ..VirtioVsockHeader::default()
            },
            &connection,
        );
        futures::pin_mut!(send_fut);
        assert!(!executor.run_until_stalled(&mut send_fut).is_pending());

        let state_action_fut = connection.handle_state_action();
        futures::pin_mut!(state_action_fut);
        if let Poll::Ready(val) = executor.run_until_stalled(&mut state_action_fut) {
            assert_eq!(StateAction::CleanShutdown, val);
        } else {
            panic!("Expected future to be ready");
        }

        // The connection should have transitioned through a guest initiated shutdown state
        // and into a clean shutdown, replying to the guest with a Reset packet.
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Reset);
    }
}

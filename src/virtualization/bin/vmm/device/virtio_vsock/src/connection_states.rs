// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file defines the possible states for a vsock connection. States can only transition
//! in a single direction denoted by the arrow, and transitions happen either by the state
//! achieving some action such as GuestInitiated getting a response from a client, or by
//! interactions with the guest such as ClientInitiated getting a response from the guest on
//! the TX queue.
//!
//! A brief summary of each state:
//!
//! GuestInitiated:         Guest sent a connection request for a listening client. Waits on a
//!                         client response to transition to ReadWrite.
//!
//! ClientInitiated:        Client sent a connection request to the guest. Waits on a guest response
//!                         to transition to ReadWrite.
//!
//! ReadWrite:              The "active connection" state. Sends data between the client and guest.
//!
//! GuestInitiatedShutdown: Entered when the guest closes both sides of a connection via Shutdown
//!                         packet. The device sends the guest a reset packet and transitions to
//!                         a clean shutdown.
//! ClientInititedShutdown: Entered when the client closes both sides of its socket. The state
//!                         sends the guest a Shutdown packet, and waits for a reset packet.
//!
//! ShutdownClean:          Device removes this connection at the next opportunity.
//!
//! ShutdownForced:         Device removes this connection at the next opportunity, and refuses
//!                         any connections on this port pair for 10s. Any state can transition
//!                         directly to this state.
//!
//!       ┌────────────────────────────────────────────────────────────┐
//!       │                                                            │
//!       │          ┌────────────────┐    ┌─────────────────┐         │
//!       │          │ GuestInitiated │    │ ClientInitiated │         │
//!       │          └──────────────┬─┘    └─┬───────────────┘         │
//!       │                         │        │                         │
//!       │                       ┌─▼────────▼─┐                       │
//!       │                       │ ReadWrite  │                       │
//!       │                       └─┬────────┬─┘                       │
//!       │                         │        │                         │
//!       │    ┌────────────────────▼─┐     ┌▼─────────────────────┐   │
//!       │    │GuestInitiatedShutdown│     │ClientInititedShutdown│   │
//!       │    └────────────────────┬─┘     └┬─────────────────────┘   │
//!       │                         │        │                         │
//!       │                      ┌──▼────────▼──┐                      │
//!       │                      │ ShutdownClean│                      │
//!       │                      └──────────────┘                      │
//!       │                                                            │
//!       └────┬───────────────────────────────────────────────────────┘
//!            │
//!            │
//! ┌──────────▼────┐
//! │ ShutdownForced│
//! └───────────────┘

use {
    crate::connection::{ConnectionCredit, VsockConnectionKey},
    crate::wire::{OpType, VirtioVsockFlags, VirtioVsockHeader, VsockType, LE16, LE32, LE64},
    anyhow::{anyhow, Error},
    fidl::client::QueryResponseFut,
    fidl_fuchsia_virtualization::HostVsockEndpointConnectResponder,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::UnboundedSender,
        future::{self, poll_fn},
    },
    std::{
        cell::{Cell, RefCell},
        convert::TryFrom,
        io::Write,
    },
    virtio_device::{
        chain::{ReadableChain, WritableChain},
        mem::DriverMem,
        queue::DriverNotify,
    },
    zerocopy::AsBytes,
};

#[derive(Debug)]
pub struct GuestInitiated {
    listener_response: RefCell<Option<QueryResponseFut<Result<zx::Socket, i32>>>>,
    initial_credit: ConnectionCredit,
    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl GuestInitiated {
    pub fn new(
        listener_response: QueryResponseFut<Result<zx::Socket, i32>>,
        control_packets: UnboundedSender<VirtioVsockHeader>,
        key: VsockConnectionKey,
        header: &VirtioVsockHeader,
    ) -> Self {
        let mut initial_credit = ConnectionCredit::default();
        initial_credit.read_credit(header);

        GuestInitiated {
            listener_response: RefCell::new(Some(listener_response)),
            initial_credit,
            key,
            control_packets,
        }
    }

    fn next(self, op: OpType) -> VsockConnectionState {
        // A guest initiated connection is waiting for a client response, so the only valid
        // transitions are to a clean or forced shutdown.
        match op {
            OpType::Shutdown => VsockConnectionState::GuestInitiatedShutdown(
                GuestInitiatedShutdown::new(self.key, self.control_packets.clone()),
            ),
            op => {
                tracing::error!("Unsupported GuestInitiated operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
        }
    }

    async fn do_state_action(&self) -> StateAction {
        let response = self.listener_response.borrow_mut().as_mut().unwrap().await;

        // This async function may be aborted, so only take the listen_response after the await
        // returns. Nothing after this should await.
        self.listener_response.take();

        let get_socket = || -> Result<fasync::Socket, Error> {
            let socket = response?.map_err(zx::Status::from_raw)?;
            let local_async = fasync::Socket::from_socket(socket)?;
            Ok(local_async)
        };

        match get_socket() {
            Ok(socket) => {
                let mut packet = VsockConnectionState::get_header_to_guest_with_defaults(self.key);
                packet.op = LE16::new(OpType::Response.into());
                self.control_packets
                    .clone()
                    .unbounded_send(packet)
                    .expect("Control packet tx end should never be closed");

                StateAction::UpdateState(VsockConnectionState::ReadWrite(ReadWrite::new(
                    socket,
                    self.key,
                    self.initial_credit,
                    self.control_packets.clone(),
                )))
            }
            Err(err) => {
                tracing::error!(%err, "Failed to transition from GuestInitiated with error");
                StateAction::UpdateState(VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                )))
            }
        }
    }
}

#[derive(Debug)]
pub struct ClientInitiated {
    sent_request_to_guest: Cell<bool>,
    responder: Option<HostVsockEndpointConnectResponder>,

    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl ClientInitiated {
    pub fn new(
        responder: HostVsockEndpointConnectResponder,
        control_packets: UnboundedSender<VirtioVsockHeader>,
        key: VsockConnectionKey,
    ) -> Self {
        ClientInitiated {
            sent_request_to_guest: Cell::new(false),
            responder: Some(responder),
            key,
            control_packets,
        }
    }

    fn next(mut self, op: OpType, header: &VirtioVsockHeader) -> VsockConnectionState {
        match op {
            OpType::Response => {
                let mut get_socket = || -> Result<fasync::Socket, Error> {
                    if self.responder.is_none() {
                        panic!("ClientInitiated responder was consumed before state transition");
                    }

                    if !self.sent_request_to_guest.get() {
                        return Err(anyhow!(
                            "Guest sent a response before the device sent a request"
                        ));
                    }

                    let (client, device) = zx::Socket::create(zx::SocketOpts::STREAM)?;
                    let local_async = fasync::Socket::from_socket(device)?;
                    self.responder.take().unwrap().send(&mut Ok(client))?;
                    Ok(local_async)
                };

                let mut credit = ConnectionCredit::default();
                credit.read_credit(header);

                match get_socket() {
                    Ok(socket) => VsockConnectionState::ReadWrite(ReadWrite::new(
                        socket,
                        self.key,
                        credit,
                        self.control_packets.clone(),
                    )),
                    Err(err) => {
                        tracing::error!(%err, "Failed to transition out of ClientInitiated");
                        VsockConnectionState::ShutdownForced(ShutdownForced::new(
                            self.key,
                            self.control_packets.clone(),
                        ))
                    }
                }
            }
            OpType::Shutdown => VsockConnectionState::GuestInitiatedShutdown(
                GuestInitiatedShutdown::new(self.key, self.control_packets.clone()),
            ),
            op => {
                tracing::error!("Unsupported ClientInitiated operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
        }
    }

    async fn do_state_action(&self) -> StateAction {
        if !self.sent_request_to_guest.get() {
            let mut packet = VsockConnectionState::get_header_to_guest_with_defaults(self.key);
            packet.op = LE16::new(OpType::Request.into());
            self.control_packets
                .clone()
                .unbounded_send(packet)
                .expect("Control packet tx end should never be closed");
            self.sent_request_to_guest.set(true);
        } else {
            // This connection is now waiting on the guest to respond to a connection request.
            future::pending::<()>().await;
        }

        StateAction::ContinueAwaiting
    }
}

impl Drop for ClientInitiated {
    fn drop(&mut self) {
        if self.responder.is_some() {
            if let Err(err) = self
                .responder
                .take()
                .unwrap()
                .send(&mut Err(zx::Status::CONNECTION_REFUSED.into_raw()))
            {
                tracing::error!(%err,"Connection failed to send closing message");
            }
        }
    }
}

#[derive(Debug)]
pub struct ReadWrite {
    // Device local socket end. The remote socket end is held by the client.
    socket: fasync::Socket,

    // Tracks the credit for this connection to prevent the guest from saturating the socket,
    // and the device from saturating to the RX queue.
    credit: RefCell<ConnectionCredit>,

    // Cumulative flags seen for this connection. These flags are permanent once seen, and cannot
    // be reset without resetting this connection. Note that these flags are from the perspective
    // of the guest.
    conn_flags: RefCell<VirtioVsockFlags>,

    // The time when guest TX must stop after VirtioVsockFlags::SHUTDOWN_SEND is seen. This allows
    // the device to ignore packets already placed on the TX queue by the guest when the guest
    // received a shutdown notice.
    tx_shutdown_leeway: RefCell<fasync::Time>,

    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

enum SocketState {
    // The socket is either ready for RX or TX, depending on what was queried.
    Ready,

    // The client closed its socket, but there are bytes remaining on the device socket that
    // have yet to be sent to the guest.
    ClosedWithBytesOutstanding,

    // Socket had a read signal but has no bytes outstanding. This should happen at most one time
    // consecutively.
    SpuriousWakeup,

    // The socket is closed with no bytes pending to be sent to the guest. The connection
    // can safely transition from the read write state.
    Closed,
}

impl ReadWrite {
    fn new(
        socket: fasync::Socket,
        key: VsockConnectionKey,
        initial_credit: ConnectionCredit,
        control_packets: UnboundedSender<VirtioVsockHeader>,
    ) -> Self {
        ReadWrite {
            socket,
            credit: RefCell::new(initial_credit),
            conn_flags: RefCell::new(VirtioVsockFlags::default()),
            tx_shutdown_leeway: RefCell::new(fasync::Time::now()),
            key,
            control_packets,
        }
    }

    fn read_credit(&self, header: &VirtioVsockHeader) {
        self.credit.borrow_mut().read_credit(&header)
    }

    fn write_credit(&self, header: &mut VirtioVsockHeader) -> Result<(), Error> {
        self.credit
            .borrow_mut()
            .write_credit(&self.socket, header)
            .map_err(|err| anyhow!("Failed to write current credit to header: {}", err))
    }

    fn send_credit_update(&self) -> Result<(), Error> {
        let mut packet = VsockConnectionState::get_header_to_guest_with_defaults(self.key);
        match self.write_credit(&mut packet) {
            Ok(()) => {
                packet.op = LE16::new(OpType::CreditUpdate.into());
                self.control_packets
                    .clone()
                    .unbounded_send(packet)
                    .expect("Control packet tx end should never be closed");
                Ok(())
            }
            err => err,
        }
    }

    fn send_half_shutdown_packet(&self) {
        if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_BOTH) {
            // Only send half shutdown packets (shutting down one direction of the connection) as
            // the connection will still be in a read-write state.
            //
            // If both sides of the connection have been shut down, this state will transition to a
            // shutdown state asynchronously which will send the closing packet and be able to
            // immediately handle the response.
            return;
        }

        let mut packet = VsockConnectionState::get_header_to_guest_with_defaults(self.key);
        packet.op = LE16::new(OpType::Shutdown.into());
        packet.flags = LE32::new(self.conn_flags.borrow().bits());

        self.control_packets
            .clone()
            .unbounded_send(packet)
            .expect("Control packet tx end should never be closed");
    }

    fn next(self, op: OpType, flags: VirtioVsockFlags) -> VsockConnectionState {
        if flags.contains(VirtioVsockFlags::SHUTDOWN_RECIEVE)
            && !self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_RECIEVE)
        {
            if let Err(err) = self.socket.as_ref().half_close() {
                tracing::error!(
                    %err,
                    "Failed to half close remote socket upon receiving \
                    VirtioVsockFlags::SHUTDOWN_RECIEVE",
                );
                return VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ));
            }
        }

        // Flags are cumulative once seen.
        let flags = self.conn_flags.borrow().union(flags);
        *self.conn_flags.borrow_mut() = flags;

        match op {
            OpType::ReadWrite | OpType::CreditRequest | OpType::CreditUpdate => {
                VsockConnectionState::ReadWrite(self)
            }
            OpType::Shutdown => {
                if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_BOTH) {
                    // This connection is now able to begin transitioning to a clean guest
                    // initiated shutdown.
                    VsockConnectionState::GuestInitiatedShutdown(GuestInitiatedShutdown::new(
                        self.key,
                        self.control_packets.clone(),
                    ))
                } else {
                    VsockConnectionState::ReadWrite(self)
                }
            }
            op => {
                tracing::error!("Unsupported ReadWrite operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
        }
    }

    async fn send_credit_update_when_credit_available(&self) -> Result<SocketState, Error> {
        // By default async sockets cache signals until a read or write failure, but the
        // device requires an accurate signal before sending the guest a credit update.
        self.socket.reacquire_write_signal()?;
        match poll_fn(move |cx| self.socket.poll_write_task(cx)).await {
            Ok(closed) => {
                if closed {
                    self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
                    self.send_half_shutdown_packet();
                    Ok(SocketState::Closed)
                } else {
                    self.send_credit_update()
                        .map(|()| SocketState::Ready)
                        .map_err(|err| anyhow!("failed to send credit update: {}", err))
                }
            }
            Err(err) => Err(anyhow!("failed to poll write task: {}", err)),
        }
    }

    async fn do_state_action(&self) -> StateAction {
        if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_BOTH) {
            return StateAction::UpdateState(VsockConnectionState::ClientInitiatedShutdown(
                ClientInitiatedShutdown::new(self.key, self.control_packets.clone()),
            ));
        }

        // If the outgoing socket buffer is full the device must wait on space available, and
        // inform the guest with a credit update. Otherwise, this state does nothing until this
        // future is aborted and re-run.
        if self.credit.borrow().guest_believes_no_tx_buffer_available()
            && !self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_SEND)
        {
            if let Err(err) = self.send_credit_update_when_credit_available().await {
                tracing::error!(%err);
                return StateAction::UpdateState(VsockConnectionState::ShutdownForced(
                    ShutdownForced::new(self.key, self.control_packets.clone()),
                ));
            }
        } else {
            // This state has nothing to do at the moment. When a state action occurs (such as
            // TX or RX) this future is aborted and re-run.
            future::pending::<()>().await;
        }

        StateAction::ContinueAwaiting
    }

    async fn get_rx_socket_state(&self) -> SocketState {
        if let Err(_) = self.socket.reacquire_read_signal() {
            return SocketState::Closed;
        }

        match poll_fn(move |cx| self.socket.poll_read_task(cx)).await {
            Ok(closed) if !closed => {
                if self.socket.as_ref().outstanding_read_bytes().unwrap_or(0) == 0 {
                    SocketState::SpuriousWakeup
                } else {
                    SocketState::Ready
                }
            }
            _ => {
                match self.socket.as_ref().outstanding_read_bytes() {
                    Ok(size) if size > 0 => {
                        // The socket peer is closed, but bytes remain on the local socket that
                        // have yet to be transmitted to the guest. Signal to the guest not to
                        // send any more data if not already done.
                        if !self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_SEND) {
                            *self.tx_shutdown_leeway.borrow_mut() =
                                fasync::Time::after(zx::Duration::from_seconds(1));
                            self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
                            self.send_half_shutdown_packet();
                        }
                        SocketState::ClosedWithBytesOutstanding
                    }
                    _ => SocketState::Closed,
                }
            }
        }
    }

    // Returns true when the connection wants the next available RX chain. Returns false if the
    // device should stop waiting on this connection, such as if the connection is transitioning to
    // a closed state.
    async fn wants_rx_chain(&self) -> bool {
        if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_RECIEVE) {
            return false;
        }

        // Optimization when data is available on a fully open socket, preventing the device from
        // needing to reacquire a read signal from the kernel. This is the steady state when a
        // connection is bottlenecked on the guest refilling the RX queue.
        if self.socket.as_ref().outstanding_read_bytes().unwrap_or(0) > 0
            && !self.socket.is_closed()
            && self.credit.borrow().peer_free_bytes() > 0
        {
            return true;
        }

        let mut num_spurious_wakeups = 0;
        loop {
            match self.get_rx_socket_state().await {
                SocketState::SpuriousWakeup => {
                    num_spurious_wakeups += 1;
                    if num_spurious_wakeups > 1 {
                        // TODO(fxbug.dev/108416): Investigate consecutive spurious wakeups.
                        tracing::error!(
                            "Connection saw {} consecutive spurious wakeups",
                            num_spurious_wakeups
                        );
                    }
                }
                SocketState::Ready | SocketState::ClosedWithBytesOutstanding => {
                    if self.credit.borrow().peer_free_bytes() == 0 {
                        // This connection is waiting on a credit update via guest TX.
                        break future::pending::<bool>().await;
                    } else {
                        break true;
                    }
                }
                SocketState::Closed => {
                    // This connection will transmit no more bytes, either due to error or a closed
                    // and drained peer.
                    self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_BOTH, true);
                    self.send_half_shutdown_packet();
                    break false;
                }
            }
        }
    }

    pub fn handle_rx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let header_length = std::mem::size_of::<VirtioVsockHeader>();
        let chain_bytes = chain
            .remaining()
            .map_err(|err| anyhow!("Failed to query chain for remaining bytes: {}", err))?;

        if chain_bytes <= header_length {
            // This chain is too small to transmit data. This is the fault of the guest, not this
            // connection.
            return Ok(());
        }

        // The device fills the chain sequentially, so the header must be constructed first
        // with the anticipated size of the packet.
        let usable_chain_bytes = chain_bytes - header_length;
        let bytes_on_socket = self.socket.as_ref().outstanding_read_bytes()?;
        let peer_buffer_available = usize::try_from(self.credit.borrow().peer_free_bytes())?;

        let bytes_to_send = std::cmp::min(
            peer_buffer_available,
            std::cmp::min(usable_chain_bytes, bytes_on_socket),
        );

        // Spurious wakeups should not consume an RX chain if there's nothing to send.
        assert!(bytes_to_send > 0);

        self.credit.borrow_mut().increment_rx_count(bytes_to_send.try_into()?);

        let mut header = VirtioVsockHeader {
            src_cid: LE64::new(self.key.host_cid.into()),
            dst_cid: LE64::new(self.key.guest_cid.into()),
            src_port: LE32::new(self.key.host_port),
            dst_port: LE32::new(self.key.guest_port),
            len: LE32::new(bytes_to_send.try_into()?),
            vsock_type: LE16::new(VsockType::Stream.into()),
            op: LE16::new(OpType::ReadWrite.into()),
            flags: LE32::new(self.conn_flags.borrow().bits()),

            // Set by write_credit below.
            buf_alloc: LE32::new(0),
            fwd_cnt: LE32::new(0),
        };
        self.write_credit(&mut header)?;

        chain
            .write_all(header.as_bytes())
            .map_err(|err| anyhow!("failed to write to chain: {}", err))?;

        let mut bytes_remaining_to_send = bytes_to_send;
        while let Some(range) = chain
            .next_with_limit(bytes_remaining_to_send)
            .transpose()
            .map_err(|err| anyhow!("Failed to iterate over chain: {}", err))?
        {
            // A note on safety:
            //   * No references (mutable or unmutable) to this range are held elsewhere. Other
            //     pointers may exist but will not be dereferenced while this slice is held.
            //   * This is a u8 pointer which has no alignment constraints.
            let slice = unsafe {
                std::slice::from_raw_parts_mut(range.try_mut_ptr().unwrap(), range.len())
            };

            // The slice lengths have been chosen to always sum up to less than the available
            // bytes on this socket, so this read should never pend. This allows us to drop down
            // to the underlying synchronous socket to allow for using a non-mutable reference
            // during the read.
            match self.socket.as_ref().read(slice) {
                Ok(read) => {
                    if read != slice.len() {
                        Err(anyhow!("Read returned fewer bytes than should have been available"))
                    } else {
                        Ok(())
                    }
                }
                Err(err) => Err(anyhow!("Failed to read from socket: {}", err)),
            }?;

            chain.add_written(slice.len().try_into()?);
            bytes_remaining_to_send -= slice.len();

            if bytes_remaining_to_send == 0 {
                break;
            }
        }
        assert_eq!(0, bytes_remaining_to_send);

        Ok(())
    }

    fn handle_tx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        op: OpType,
        header: VirtioVsockHeader,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let header_bytes = header.len.get();
        let chain_bytes = chain
            .remaining()
            .map_err(|err| anyhow!("Failed to query chain for remaining bytes: {}", err))?;
        if chain_bytes != usize::try_from(header_bytes).unwrap() {
            return Err(anyhow!(
                "Expected {} bytes from reading header, but found {} bytes on chain",
                header_bytes,
                chain_bytes
            ));
        }

        if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_SEND) {
            if *self.tx_shutdown_leeway.borrow() < fasync::Time::now() {
                return Err(anyhow!("Guest sent TX when connection is in state SHUTDOWN_SEND"));
            } else {
                // Don't complete the chain as it will have readable sections unwalked. The guest
                // may have had packets in the TX queue when it received a shutdown notice from
                // the device.
                return Ok(());
            }
        }

        self.read_credit(&header);
        if op == OpType::CreditRequest {
            self.send_credit_update()?;
        }

        while let Some(range) = chain
            .next()
            .transpose()
            .map_err(|err| anyhow!("Failed to iterate over chain: {}", err))?
        {
            // A note on safety:
            //   * No references (mutable or unmutable) to this range are held elsewhere. Other
            //     pointers may exist but will not be dereferenced while this slice is held.
            //   * This is a u8 pointer which has no alignment constraints.
            let slice =
                unsafe { std::slice::from_raw_parts(range.try_ptr().unwrap(), range.len()) };

            // The credit system means that this write should never pend, so drop down to the
            // underlying synchronous socket to allow for using a non-mutable reference during
            // the write.
            match self.socket.as_ref().write(slice) {
                Ok(written) => {
                    if written != slice.len() {
                        // 5.10.6.3.1 Driver Requirements: Device Operation: Buffer Space Management
                        //
                        // VIRTIO_VSOCK_OP_RW data packets MUST only be transmitted when the peer
                        // has sufficient free buffer space for the payload.
                        Err(anyhow!("Guest is not honoring the reported socket credit"))
                    } else {
                        Ok(())
                    }
                }
                Err(_) => {
                    *self.tx_shutdown_leeway.borrow_mut() =
                        fasync::Time::after(zx::Duration::from_seconds(1));
                    self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
                    self.send_half_shutdown_packet();
                    return Ok(());
                }
            }?;

            self.credit.borrow_mut().increment_tx_count(u32::try_from(slice.len()).unwrap());
        }

        chain.return_complete().map_err(|err| anyhow!("Failed to complete chain: {}", err))
    }
}

#[derive(Debug)]
pub struct GuestInitiatedShutdown {
    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl GuestInitiatedShutdown {
    fn new(key: VsockConnectionKey, control_packets: UnboundedSender<VirtioVsockHeader>) -> Self {
        GuestInitiatedShutdown { key, control_packets }
    }

    fn next(self, op: OpType) -> VsockConnectionState {
        // It's fine for the guest to send multiple shutdown packets, but any other type of
        // packet is unexpected.
        match op {
            OpType::Shutdown => VsockConnectionState::GuestInitiatedShutdown(self),
            op => {
                tracing::error!("Unsupported GuestInitiatedShutdown operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
        }
    }

    async fn do_state_action(&self) -> StateAction {
        VsockConnectionState::send_reset_packet(self.key, self.control_packets.clone());
        StateAction::UpdateState(VsockConnectionState::ShutdownClean(ShutdownClean::new(
            self.key,
            self.control_packets.clone(),
        )))
    }
}

#[derive(Debug)]
pub struct ClientInitiatedShutdown {
    // The device sends one full shutdown packet to the guest, and then waits for a reply.
    sent_full_shutdown: Cell<bool>,

    // A client initiated shutdown will wait this long for a guest to reply with a reset
    // packet.
    timeout: Cell<fasync::Time>,

    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl ClientInitiatedShutdown {
    fn new(key: VsockConnectionKey, control_packets: UnboundedSender<VirtioVsockHeader>) -> Self {
        ClientInitiatedShutdown {
            sent_full_shutdown: Cell::new(false),
            timeout: Cell::new(fasync::Time::INFINITE),
            key,
            control_packets,
        }
    }

    fn next(self, op: OpType) -> VsockConnectionState {
        match op {
            OpType::Shutdown => {
                // A guest sending the device shutdown after itself being sent shutdown makes this
                // difficult to ensure a clean disconnect, so simply force shutdown and briefly
                // quarantine the ports.
                tracing::info!("Guest sent shutdown while being asked to shutdown");
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
            OpType::Reset => VsockConnectionState::ShutdownClean(ShutdownClean::new(
                self.key,
                self.control_packets.clone(),
            )),
            OpType::ReadWrite | OpType::CreditUpdate | OpType::CreditRequest => {
                // The guest may have already had pending TX packets on the queue when it received
                // a shutdown notice, so these can be dropped.
                VsockConnectionState::ClientInitiatedShutdown(self)
            }
            op => {
                tracing::error!("Unsupported ClientInitiatedShutdown operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
        }
    }

    fn handle_tx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        _op: OpType,
        _header: VirtioVsockHeader,
        _chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // Drop the chain without walking it. The client is asking for the guest to initiate
        // shutdown, but the guest may have already written packets to its TX queue.
        Ok(())
    }

    async fn do_state_action(&self) -> StateAction {
        if !self.sent_full_shutdown.get() {
            let mut packet = VsockConnectionState::get_header_to_guest_with_defaults(self.key);
            packet.op = LE16::new(OpType::Shutdown.into());
            packet.flags = LE32::new(VirtioVsockFlags::SHUTDOWN_BOTH.bits());

            self.control_packets
                .clone()
                .unbounded_send(packet)
                .expect("Control packet tx end should never be closed");

            self.sent_full_shutdown.set(true);
            self.timeout.set(fasync::Time::after(zx::Duration::from_seconds(5)));
        }

        // Returns once the state has waited 5s from creation for a guest response.
        fasync::Timer::new(self.timeout.get()).await;

        tracing::error!("Guest didn't send a clean disconnect within 5s");
        StateAction::UpdateState(VsockConnectionState::ShutdownForced(ShutdownForced::new(
            self.key,
            self.control_packets.clone(),
        )))
    }
}

#[derive(Debug)]
pub struct ShutdownClean {
    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl ShutdownClean {
    fn new(key: VsockConnectionKey, control_packets: UnboundedSender<VirtioVsockHeader>) -> Self {
        ShutdownClean { key, control_packets }
    }

    fn next(self, op: OpType) -> VsockConnectionState {
        tracing::error!(
            "Connection in ShutdownClean expected no more packets, \
            but received operation {:?}",
            op
        );
        VsockConnectionState::ShutdownForced(ShutdownForced::new(
            self.key,
            self.control_packets.clone(),
        ))
    }

    async fn do_state_action(&self) -> StateAction {
        StateAction::CleanShutdown
    }
}

#[derive(Debug)]
pub struct ShutdownForced {
    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl ShutdownForced {
    pub fn new(
        key: VsockConnectionKey,

        control_packets: UnboundedSender<VirtioVsockHeader>,
    ) -> Self {
        ShutdownForced { key, control_packets }
    }

    fn next(self, op: OpType) -> VsockConnectionState {
        tracing::info!("Connection is in ShutdownForced, disregarding operation {:?}", op);
        VsockConnectionState::ShutdownForced(self)
    }

    async fn do_state_action(&self) -> StateAction {
        VsockConnectionState::send_reset_packet(self.key, self.control_packets.clone());
        StateAction::ForcedShutdown
    }
}

// States should be polled so that they can do state specific actions when available, such as
// transitioning between states or removing the connection once entering shutdown.
#[derive(Debug)]
pub enum StateAction {
    ContinueAwaiting,
    UpdateState(VsockConnectionState),
    CleanShutdown,
    ForcedShutdown,
}

impl PartialEq for StateAction {
    fn eq(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }
}

#[derive(Default, Debug)]
pub enum VsockConnectionState {
    #[default]
    Invalid, // Used only as a transitionary state.
    GuestInitiated(GuestInitiated),
    ClientInitiated(ClientInitiated),
    ReadWrite(ReadWrite),
    GuestInitiatedShutdown(GuestInitiatedShutdown),
    ClientInitiatedShutdown(ClientInitiatedShutdown),
    ShutdownClean(ShutdownClean),
    ShutdownForced(ShutdownForced),
}

impl PartialEq for VsockConnectionState {
    fn eq(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }
}

impl VsockConnectionState {
    pub fn handle_operation(
        self,
        op: OpType,
        flags: VirtioVsockFlags,
        header: &VirtioVsockHeader,
    ) -> Self {
        match self {
            VsockConnectionState::Invalid => panic!("Connection entered an impossible state"),
            VsockConnectionState::GuestInitiated(state) => state.next(op),
            VsockConnectionState::ClientInitiated(state) => state.next(op, header),
            VsockConnectionState::ReadWrite(state) => state.next(op, flags),
            VsockConnectionState::GuestInitiatedShutdown(state) => state.next(op),
            VsockConnectionState::ClientInitiatedShutdown(state) => state.next(op),
            VsockConnectionState::ShutdownClean(state) => state.next(op),
            VsockConnectionState::ShutdownForced(state) => state.next(op),
        }
    }

    pub async fn do_state_action(&self) -> StateAction {
        match self {
            VsockConnectionState::Invalid => panic!("Connection entered an impossible state"),
            VsockConnectionState::ClientInitiated(state) => state.do_state_action().await,
            VsockConnectionState::ClientInitiatedShutdown(state) => state.do_state_action().await,
            VsockConnectionState::GuestInitiated(state) => state.do_state_action().await,
            VsockConnectionState::GuestInitiatedShutdown(state) => state.do_state_action().await,
            VsockConnectionState::ReadWrite(state) => state.do_state_action().await,
            VsockConnectionState::ShutdownClean(state) => state.do_state_action().await,
            VsockConnectionState::ShutdownForced(state) => state.do_state_action().await,
        }
    }

    pub async fn wants_rx_chain(&self) -> bool {
        match self {
            VsockConnectionState::Invalid => panic!("Connection entered an impossible state"),
            VsockConnectionState::ClientInitiated(_) | VsockConnectionState::GuestInitiated(_) => {
                // States that may eventually transition to the read write simply return pending
                // so that they continue being tracked without consuming chains.
                future::pending::<bool>().await
            }
            VsockConnectionState::ReadWrite(state) => {
                // Will return true when data is available on the socket, and false if there will
                // be no additional RX to the guest.
                state.wants_rx_chain().await
            }
            VsockConnectionState::GuestInitiatedShutdown(_)
            | VsockConnectionState::ClientInitiatedShutdown(_)
            | VsockConnectionState::ShutdownClean(_)
            | VsockConnectionState::ShutdownForced(_) => {
                // These states will never transition to a state which requires a writable chain,
                // so this connection can stop being polled for that purpose by the device.
                false
            }
        }
    }

    pub fn handle_rx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        match self {
            VsockConnectionState::Invalid => panic!("Connection entered an impossible state"),
            VsockConnectionState::ClientInitiated(_) | VsockConnectionState::GuestInitiated(_) => {
                // This is a fatal logic error. The device should only offer states writable chains
                // when they request one.
                panic!("Device gave an invalid state a writable chain")
            }
            VsockConnectionState::ReadWrite(state) => state.handle_rx_chain(chain),
            VsockConnectionState::GuestInitiatedShutdown(_)
            | VsockConnectionState::ClientInitiatedShutdown(_)
            | VsockConnectionState::ShutdownClean(_)
            | VsockConnectionState::ShutdownForced(_) => {
                // There was a state transition between when the connection requested a writable
                // chain, and when it actually received one. This should be rare, so we can just
                // drop this chain.
                Ok(())
            }
        }
    }

    pub fn handle_tx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        op: OpType,
        header: VirtioVsockHeader,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        match self {
            VsockConnectionState::Invalid => panic!("Connection entered an impossible state"),
            VsockConnectionState::ReadWrite(state) => state.handle_tx_chain(op, header, chain),
            VsockConnectionState::ClientInitiatedShutdown(state) => {
                state.handle_tx_chain(op, header, chain)
            }
            _ => {
                // Consume the chain. The default behaviour for states is to validate that the
                // chain has been fully walked, which will be true for states that only expect a
                // header on the chain.
                chain.return_complete().map_err(|err| anyhow!("Failed to complete chain: {}", err))
            }
        }
    }

    // Helper function for sending a common reset packet for a given connection key.
    pub fn send_reset_packet(
        key: VsockConnectionKey,
        control_packets: UnboundedSender<VirtioVsockHeader>,
    ) {
        let mut packet = VsockConnectionState::get_header_to_guest_with_defaults(key);
        packet.op = LE16::new(OpType::Reset.into());

        control_packets
            .unbounded_send(packet)
            .expect("Control packet tx end should never be closed");
    }

    fn get_header_to_guest_with_defaults(key: VsockConnectionKey) -> VirtioVsockHeader {
        VirtioVsockHeader {
            src_cid: LE64::new(key.host_cid.into()),
            dst_cid: LE64::new(key.guest_cid.into()),
            src_port: LE32::new(key.host_port),
            dst_port: LE32::new(key.guest_port),
            vsock_type: LE16::new(VsockType::Stream.into()),
            ..VirtioVsockHeader::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_utils::PollExt,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::{
            HostVsockAcceptorMarker, HostVsockEndpointMarker, DEFAULT_GUEST_CID, HOST_CID,
        },
        fuchsia_async::TestExecutor,
        futures::{channel::mpsc, FutureExt, TryStreamExt},
        rand::{distributions::Standard, Rng},
        std::{collections::HashSet, io::Read, pin::Pin, task::Poll},
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
        zerocopy::FromBytes,
    };

    fn send_header_to_rw_state(header: VirtioVsockHeader, state: &ReadWrite) {
        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        // Chains cannot initially be zero length, so write a byte and read it off. Note that
        // in practice these chains would have been sizeof(VirtioVsockHeader) but the header
        // has already been removed to choose the correct connection.
        queue_state.fake_queue.publish(ChainBuilder::new().readable(&[0u8], &mem).build()).unwrap();
        let mut empty_chain = ReadableChain::new(queue_state.queue.next_chain().unwrap(), &mem);
        let mut buffer = [0u8];
        empty_chain.read_exact(&mut buffer).expect("failed to read bytes from chain");

        state
            .handle_tx_chain(
                OpType::try_from(header.op.get()).expect("invalid optype"),
                header,
                empty_chain,
            )
            .expect("failed to handle an empty tx chain");
    }

    async fn guest_initiated_generator() -> VsockConnectionState {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let (proxy, _stream) = create_proxy_and_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");

        let response_fut = proxy.accept(DEFAULT_GUEST_CID, key.guest_port, key.host_port);
        VsockConnectionState::GuestInitiated(GuestInitiated::new(
            response_fut,
            control_tx.clone(),
            key,
            &VirtioVsockHeader::default(),
        ))
    }

    async fn client_initiated_generator() -> VsockConnectionState {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        fasync::Task::local(async move {
            let _ = proxy.connect(10).await;
        })
        .detach();

        let (_guest_port, responder) = stream
            .try_next()
            .await
            .unwrap()
            .unwrap()
            .into_connect()
            .expect("received unexpected request on stream");
        let state = ClientInitiated::new(responder, control_tx, key);
        _ = state.do_state_action().await;
        VsockConnectionState::ClientInitiated(state)
    }

    async fn read_write_generator() -> VsockConnectionState {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (_client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        VsockConnectionState::ReadWrite(ReadWrite::new(
            socket,
            key,
            ConnectionCredit::default(),
            control_tx,
        ))
    }

    async fn guest_shutdown_generator() -> VsockConnectionState {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        VsockConnectionState::GuestInitiatedShutdown(GuestInitiatedShutdown::new(key, control_tx))
    }

    async fn client_shutdown_generator() -> VsockConnectionState {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        VsockConnectionState::ClientInitiatedShutdown(ClientInitiatedShutdown::new(key, control_tx))
    }

    async fn shutdown_clean_generator() -> VsockConnectionState {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        VsockConnectionState::ShutdownClean(ShutdownClean::new(key, control_tx))
    }

    async fn shutdown_forced_generator() -> VsockConnectionState {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        VsockConnectionState::ShutdownForced(ShutdownForced::new(key, control_tx))
    }

    #[fuchsia::test]
    async fn valid_optype_for_state() {
        // Each test case contains a generator which creates an initial state, and a list of all
        // operations that will transition this state to another valid state. Any operation not
        // in valid_ops should transition the given state to a failed state.
        //
        // See the ASCII diagram at the top of this file to see a visual representation of the
        // supported state transitions.
        struct ValidOp {
            op: OpType,
            flags: VirtioVsockFlags,
            generator:
                Box<dyn FnMut() -> Pin<Box<dyn futures::Future<Output = VsockConnectionState>>>>,
        }

        struct TestCase {
            generator:
                Box<dyn FnMut() -> Pin<Box<dyn futures::Future<Output = VsockConnectionState>>>>,
            valid_ops: Vec<ValidOp>,
        }

        // Whenever a state attempts an invalid state transition, it instead transitions to a
        // shutdown forced state.
        let failed_transition = shutdown_forced_generator().await;

        // Used to determine what op types are invalid for a given state.
        let all_op_types = HashSet::from([
            OpType::Invalid,
            OpType::Request,
            OpType::Response,
            OpType::Reset,
            OpType::Shutdown,
            OpType::ReadWrite,
            OpType::CreditUpdate,
            OpType::CreditRequest,
        ]);

        let test_cases = vec![
            TestCase {
                generator: Box::new(move || Box::pin(guest_initiated_generator())),
                valid_ops: vec![ValidOp {
                    op: OpType::Shutdown,
                    flags: VirtioVsockFlags::default(),
                    generator: Box::new(move || Box::pin(guest_shutdown_generator())),
                }],
            },
            TestCase {
                generator: Box::new(move || Box::pin(client_initiated_generator())),
                valid_ops: vec![
                    ValidOp {
                        op: OpType::Shutdown,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(guest_shutdown_generator())),
                    },
                    ValidOp {
                        op: OpType::Response,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(read_write_generator())),
                    },
                ],
            },
            TestCase {
                generator: Box::new(move || Box::pin(read_write_generator())),
                valid_ops: vec![
                    ValidOp {
                        op: OpType::Shutdown,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(read_write_generator())),
                    },
                    ValidOp {
                        op: OpType::Shutdown,
                        flags: VirtioVsockFlags::SHUTDOWN_BOTH,
                        generator: Box::new(move || Box::pin(guest_shutdown_generator())),
                    },
                    ValidOp {
                        op: OpType::CreditRequest,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(read_write_generator())),
                    },
                    ValidOp {
                        op: OpType::CreditUpdate,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(read_write_generator())),
                    },
                    ValidOp {
                        op: OpType::ReadWrite,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(read_write_generator())),
                    },
                ],
            },
            TestCase {
                generator: Box::new(move || Box::pin(guest_shutdown_generator())),
                valid_ops: vec![ValidOp {
                    op: OpType::Shutdown,
                    flags: VirtioVsockFlags::default(),
                    generator: Box::new(move || Box::pin(guest_shutdown_generator())),
                }],
            },
            TestCase {
                generator: Box::new(move || Box::pin(client_shutdown_generator())),
                valid_ops: vec![
                    ValidOp {
                        op: OpType::Reset,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(shutdown_clean_generator())),
                    },
                    ValidOp {
                        op: OpType::CreditRequest,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(client_shutdown_generator())),
                    },
                    ValidOp {
                        op: OpType::CreditUpdate,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(client_shutdown_generator())),
                    },
                    ValidOp {
                        op: OpType::ReadWrite,
                        flags: VirtioVsockFlags::default(),
                        generator: Box::new(move || Box::pin(client_shutdown_generator())),
                    },
                ],
            },
            TestCase {
                generator: Box::new(move || Box::pin(shutdown_clean_generator())),
                valid_ops: vec![],
            },
            TestCase {
                generator: Box::new(move || Box::pin(shutdown_forced_generator())),
                valid_ops: vec![],
            },
        ];

        for mut test in test_cases {
            let supported_ops: HashSet<OpType> = test.valid_ops.iter().map(|val| val.op).collect();

            // Supported state transitions.
            for mut valid_op in test.valid_ops {
                let initial_state = (test.generator)().await;
                let expected_state = (valid_op.generator)().await;
                let new_state = initial_state.handle_operation(
                    valid_op.op,
                    valid_op.flags,
                    &VirtioVsockHeader::default(),
                );
                assert_eq!(expected_state, new_state);
            }

            // Unsupported state transitions.
            let unsupported_ops: HashSet<_> = all_op_types.difference(&supported_ops).collect();
            for op in unsupported_ops {
                let initial_state = (test.generator)().await;
                let new_state = initial_state.handle_operation(
                    *op,
                    VirtioVsockFlags::default(),
                    &VirtioVsockHeader::default(),
                );
                assert_eq!(failed_transition, new_state);
            }
        }
    }

    #[fuchsia::test]
    async fn send_reset_packet() {
        let host_port = 123;
        let guest_port = 456;

        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        VsockConnectionState::send_reset_packet(
            VsockConnectionKey::new(HOST_CID, host_port, DEFAULT_GUEST_CID, guest_port),
            control_tx,
        );
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        assert_eq!(header.src_cid.get(), HOST_CID.into());
        assert_eq!(header.dst_cid.get(), DEFAULT_GUEST_CID.into());
        assert_eq!(header.src_port.get(), host_port);
        assert_eq!(header.dst_port.get(), guest_port);
        assert_eq!(VsockType::try_from(header.vsock_type.get()).unwrap(), VsockType::Stream);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Reset);
    }

    #[test]
    fn guest_initiated_client_returned_failure() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");

        let response_fut = proxy.accept(DEFAULT_GUEST_CID, key.guest_port, key.host_port);
        let state =
            GuestInitiated::new(response_fut, control_tx, key, &VirtioVsockHeader::default());

        let fut = state.do_state_action();
        futures::pin_mut!(fut);
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        if let Poll::Ready(val) = executor.run_until_stalled(&mut stream.try_next()) {
            let (_, _, _, responder) =
                val.unwrap().unwrap().into_accept().expect("received unexpected message on stream");
            responder
                .send(&mut Err(zx::Status::CONNECTION_REFUSED.into_raw()))
                .expect("failed to send response");
        } else {
            panic!("Expected future to be ready");
        };

        if let Poll::Ready(StateAction::UpdateState(state)) = executor.run_until_stalled(&mut fut) {
            let result =
                state.do_state_action().now_or_never().expect("task should have completed");
            assert_eq!(result, StateAction::ForcedShutdown);
        } else {
            panic!("Expected future to be ready");
        };
    }

    #[test]
    fn guest_initiated_client_returned_socket() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockAcceptorMarker>()
            .expect("failed to create HostVsockAcceptor request stream");

        let response_fut = proxy.accept(DEFAULT_GUEST_CID, key.guest_port, key.host_port);
        let state =
            GuestInitiated::new(response_fut, control_tx, key, &VirtioVsockHeader::default());

        let fut = state.do_state_action();
        futures::pin_mut!(fut);
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let (_client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        if let Poll::Ready(val) = executor.run_until_stalled(&mut stream.try_next()) {
            let (_, _, _, responder) =
                val.unwrap().unwrap().into_accept().expect("received unexpected message on stream");
            responder.send(&mut Ok(device_socket)).expect("failed to send response");
        } else {
            panic!("Expected future to be ready");
        };

        if let Poll::Ready(StateAction::UpdateState(state)) = executor.run_until_stalled(&mut fut) {
            match state {
                VsockConnectionState::ReadWrite(_state) => (),
                _ => panic!("Expected transition to ReadWrite state"),
            };
        } else {
            panic!("Expected future to be ready");
        };
    }

    #[test]
    fn client_initiated_guest_responded_before_request() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        let mut fut = proxy.connect(key.guest_port);
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let (_guest_port, responder) = if let Poll::Ready(val) =
            executor.run_until_stalled(&mut stream.try_next())
        {
            val.unwrap().unwrap().into_connect().expect("received unexpected response on stream")
        } else {
            panic!("Expected future to be ready")
        };

        // Guest responded before the client actually made a request. This implies that the
        // two are out of sync.
        let state = ClientInitiated::new(responder, control_tx, key);
        match state.next(OpType::Response, &VirtioVsockHeader::default()) {
            VsockConnectionState::ShutdownForced(_state) => (),
            _ => panic!("Expected transition to ShutdownForced state"),
        };

        // Device forwarded the guest rejection to the client.
        if let Poll::Ready(result) = executor.run_until_stalled(&mut fut) {
            assert_eq!(
                zx::Status::from_raw(result.expect("failed to get any response").unwrap_err()),
                zx::Status::CONNECTION_REFUSED
            );
        } else {
            panic!("Expected future to be ready");
        };
    }

    #[test]
    fn client_initiated_guest_acceptance() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>()
            .expect("failed to create HostVsockEndpoint proxy/stream");

        let mut fut = proxy.connect(key.guest_port);
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let (_guest_port, responder) = if let Poll::Ready(val) =
            executor.run_until_stalled(&mut stream.try_next())
        {
            val.unwrap().unwrap().into_connect().expect("received unexpected response on stream")
        } else {
            panic!("Expected future to be ready")
        };

        let state = ClientInitiated::new(responder, control_tx, key);

        // Send a request to the guest. If the guest replies before a request is sent, the device
        // assumes that we are out of sync and drops the connection.
        state.do_state_action().now_or_never().expect("task should have completed");
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        assert_eq!(header.src_port.get(), key.host_port);
        assert_eq!(header.dst_port.get(), key.guest_port);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Request);

        match state.next(OpType::Response, &VirtioVsockHeader::default()) {
            VsockConnectionState::ReadWrite(_state) => (),
            _ => panic!("Expected transition to ReadWrite state"),
        };

        // Device created a socket pair and sent one half to the client.
        if let Poll::Ready(result) = executor.run_until_stalled(&mut fut) {
            result
                .expect("failed to get any response")
                .expect("device should have provided a socket")
        } else {
            panic!("Expected future to be ready");
        };
    }

    #[test]
    fn read_write_basic_tx_data_integrity_validation() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        // The RW state will immediately send an unsolicited credit update upon creation as it
        // knows that the guest thinks there's no credit available.
        let state_action_fut = state.do_state_action();
        futures::pin_mut!(state_action_fut);
        assert!(!executor.run_until_stalled(&mut state_action_fut).is_pending());

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::CreditUpdate);

        // Create a vector of random bytes that will entirely exhaust the TX credit.
        let num_bytes_to_send: usize = header.buf_alloc.get().try_into().unwrap();
        let random_bytes: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(num_bytes_to_send).collect();

        // Each time send_bytes is called with a slice, a chain is created with the slice spread
        // across three descriptors.
        let send_bytes = |slice: &[u8]| {
            let mem = IdentityDriverMem::new();
            let mut queue_state = TestQueue::new(32, &mem);

            // Chunk the array into three descriptors.
            queue_state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        .readable(&slice[..slice.len() / 4], &mem)
                        .readable(&slice[slice.len() / 4..slice.len() / 2], &mem)
                        .readable(&slice[slice.len() / 2..], &mem)
                        .build(),
                )
                .expect("failed to publish readable chain");

            state
                .handle_tx_chain(
                    OpType::ReadWrite,
                    VirtioVsockHeader {
                        len: LE32::new(slice.len() as u32),
                        ..VirtioVsockHeader::default()
                    },
                    ReadableChain::new(
                        queue_state.queue.next_chain().expect("failed to get next chain"),
                        &mem,
                    ),
                )
                .expect("failed to write chain to socket");
        };

        // Send the bytes in 20 equally sized chains. The data in each chain is split across three
        // descriptors.
        let chunks: Vec<&[u8]> = random_bytes.chunks(20).collect();
        for chunk in chunks {
            send_bytes(chunk);
        }

        // Once TX completes, this data should be immediately readable from the client side socket.
        let mut received_bytes = vec![0u8; num_bytes_to_send];
        assert_eq!(
            client_socket.read(&mut received_bytes).expect("failed to read bytes"),
            received_bytes.len()
        );
        assert_eq!(received_bytes, random_bytes);
    }

    #[test]
    fn read_write_basic_rx_data_integrity_validation() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        // Tell the device that there's u32::MAX credit available, as this test should not be
        // credit limited.
        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditUpdate.into()),
                buf_alloc: LE32::new(u32::MAX),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        // Max out the client socket's TX buffer with random bytes.
        let num_bytes_to_send = client_socket.info().expect("failed to get info").tx_buf_max;
        let random_bytes: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(num_bytes_to_send).collect();
        let mut received_bytes: Vec<u8> = Vec::new();

        assert_eq!(
            client_socket.write(&random_bytes).expect("failed to write bytes"),
            num_bytes_to_send
        );

        let min_chain_size = std::mem::size_of::<VirtioVsockHeader>() as u32;
        loop {
            // A state will return true for wants_rx_chain as long as there's data pending on the
            // socket, and will return pending when there's not yet data. This should never return
            // false, as that would suggest a state change.
            let want_rx_chain_fut = state.wants_rx_chain();
            futures::pin_mut!(want_rx_chain_fut);
            let wants_chain = match executor.run_until_stalled(&mut want_rx_chain_fut) {
                Poll::Pending => {
                    // No remaining bytes to read.
                    break;
                }
                Poll::Ready(wants_chain) => wants_chain,
            };
            assert!(wants_chain);

            let mem = IdentityDriverMem::new();
            let mut queue_state = TestQueue::new(32, &mem);

            // Repeatedly send writable chains to the device with 6 descriptors, with each
            // descriptor between 1 and 100 bytes (except the first which will always have room
            // for the header).
            queue_state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        .writable(rand::thread_rng().gen_range(min_chain_size..100), &mem)
                        .writable(rand::thread_rng().gen_range(1..100), &mem)
                        .writable(rand::thread_rng().gen_range(1..100), &mem)
                        .writable(rand::thread_rng().gen_range(1..100), &mem)
                        .writable(rand::thread_rng().gen_range(1..100), &mem)
                        .writable(rand::thread_rng().gen_range(1..100), &mem)
                        .build(),
                )
                .expect("failed to publish writable chain");

            state
                .handle_rx_chain(
                    WritableChain::new(
                        queue_state.queue.next_chain().expect("failed to get next chain"),
                        &mem,
                    )
                    .expect("failed to get writable chain"),
                )
                .expect("failed to handle rx chain");

            let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");

            // Coalesce the returned chain into one contiguous buffer.
            let mut result = Vec::new();
            let mut iter = used_chain.data_iter();
            while let Some((data, len)) = iter.next() {
                let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };
                result.extend_from_slice(slice);
            }

            // For each chain, parse and validate the header, and then append the data section to
            // the received_bytes vector for eventual comparison.
            assert_eq!(result.len(), usize::try_from(used_chain.written()).unwrap());
            let header =
                VirtioVsockHeader::read_from(&result[..std::mem::size_of::<VirtioVsockHeader>()])
                    .expect("failed to read header");

            assert_eq!(header.src_cid.get(), key.host_cid.into());
            assert_eq!(header.dst_cid.get(), key.guest_cid.into());
            assert_eq!(header.src_port.get(), key.host_port);
            assert_eq!(header.dst_port.get(), key.guest_port);
            assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::ReadWrite);
            assert_eq!(
                usize::try_from(header.len.get()).unwrap(),
                (result.len() - std::mem::size_of::<VirtioVsockHeader>())
            );

            received_bytes.extend_from_slice(&result[std::mem::size_of::<VirtioVsockHeader>()..]);
        }

        assert_eq!(received_bytes, random_bytes);
    }

    #[test]
    fn require_fresh_read_signal_in_read_write_state() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);
        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditUpdate.into()),
                buf_alloc: LE32::new(u32::MAX),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        // Nothing sent via the client socket, so the state has no need for an RX chain.
        let want_rx_chain_fut = state.wants_rx_chain();
        futures::pin_mut!(want_rx_chain_fut);
        assert!(executor.run_until_stalled(&mut want_rx_chain_fut).is_pending());

        let bytes = vec![0u8; 5];
        assert_eq!(
            client_socket.write(&bytes).expect("failed to write to client socket"),
            bytes.len()
        );

        // There are 5 bytes pending on the device socket, so now it wants a chain.
        assert!(!executor.run_until_stalled(&mut want_rx_chain_fut).is_pending());
    }

    #[test]
    fn require_fresh_write_signal_in_read_write_state() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        // The read-write state knows that the guest thinks there's no credit, so immediately
        // sends an unsolicited credit update.
        let state_action_fut = state.do_state_action();
        futures::pin_mut!(state_action_fut);
        assert!(!executor.run_until_stalled(&mut state_action_fut).is_pending());

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::CreditUpdate);

        // Using the credit update, create a buffer that will exhaust the TX socket buffer.
        let max_tx_bytes = header.buf_alloc.get();
        let data = vec![0u8; max_tx_bytes.try_into().unwrap()];

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&data, &mem).build())
            .expect("failed to publish readable chain");
        state
            .handle_tx_chain(
                OpType::ReadWrite,
                VirtioVsockHeader { len: LE32::new(max_tx_bytes), ..VirtioVsockHeader::default() },
                ReadableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                ),
            )
            .expect("failed to write chain to socket");

        // The state will signal when credit is available with a credit update. As we require
        // socket signals to be fresh instead of cached (the default behaviour), this will pend
        // until the client reads from the socket.
        let state_action_fut = state.do_state_action();
        futures::pin_mut!(state_action_fut);
        assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());

        let mut read_buf = vec![0u8; 5];
        assert_eq!(
            client_socket.read(&mut read_buf).expect("failed to read bytes"),
            read_buf.len()
        );

        // The device socket is now writable, so the state sends an unsolicited credit update
        // to the guest.
        assert!(!executor.run_until_stalled(&mut state_action_fut).is_pending());
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::CreditUpdate);
        assert_eq!(header.fwd_cnt.get(), read_buf.len() as u32);
    }

    #[fuchsia::test]
    async fn read_write_guest_half_close_remote_socket() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        assert_eq!(client_socket.write(b"success!").expect("failed to write to socket"), 8);

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);
        match state.next(OpType::Shutdown, VirtioVsockFlags::SHUTDOWN_RECIEVE) {
            VsockConnectionState::ReadWrite(_state) => {
                // Socket is half closed so unable to transmit data.
                assert_eq!(client_socket.write(b"failure"), Err(zx::Status::BAD_STATE));
            }
            _ => panic!("Expected state to remain in ReadWrite state"),
        }
    }

    #[test]
    fn read_write_client_close_socket_rx_bytes_outstanding() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        // Tell the device that there's u32::MAX credit available, as this test should not be
        // credit limited.
        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditUpdate.into()),
                buf_alloc: LE32::new(u32::MAX),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        // Write 5 bytes to the client socket, and then drop it. The socket now has a peer closed
        // signal.
        let bytes = vec![1u8, 2, 3, 4, 5];
        assert_eq!(client_socket.write(&bytes).expect("failed to write bytes"), bytes.len());
        drop(client_socket);

        // State wants a chain even though the socket was closed, as there are bytes outstanding.
        let want_rx_chain_fut = state.wants_rx_chain();
        futures::pin_mut!(want_rx_chain_fut);
        if let Poll::Ready(wants_chain) = executor.run_until_stalled(&mut want_rx_chain_fut) {
            assert!(wants_chain)
        } else {
            panic!("Expected future to be ready")
        };

        // The guest is instructed to stop sending packets, but receive is left open until the
        // socket is drained.
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        let flags = VirtioVsockFlags::from_bits(header.flags.get()).expect("unrecognized flag");
        assert_eq!(flags, VirtioVsockFlags::SHUTDOWN_SEND);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Shutdown);

        // Device still wants a chain as it has 5 bytes to sent before transitioning to a shutdown
        // state.
        let want_rx_chain_fut = state.wants_rx_chain();
        futures::pin_mut!(want_rx_chain_fut);
        if let Poll::Ready(wants_chain) = executor.run_until_stalled(&mut want_rx_chain_fut) {
            assert!(wants_chain)
        } else {
            panic!("Expected future to be ready")
        };

        let header_size = std::mem::size_of::<VirtioVsockHeader>();
        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().writable(header_size as u32 + 50, &mem).build())
            .expect("failed to publish writable chain");

        state
            .handle_rx_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .expect("failed to handle rx chain");

        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");

        assert_eq!(used_chain.written(), (header_size + bytes.len()) as u32);
        let (data, len) =
            used_chain.data_iter().next().expect("there should be one filled descriptor");
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };
        assert_eq!(&slice[header_size..], &bytes);

        // Socket has been drained, so the state no longer wants any chains.
        let want_rx_chain_fut = state.wants_rx_chain();
        futures::pin_mut!(want_rx_chain_fut);
        if let Poll::Ready(wants_chain) = executor.run_until_stalled(&mut want_rx_chain_fut) {
            assert!(!wants_chain)
        } else {
            panic!("Expected future to be ready")
        };

        // The state can transition to a full shutdown now that the socket has been drained.
        let state_action_fut = state.do_state_action();
        futures::pin_mut!(state_action_fut);
        let action =
            executor.run_until_stalled(&mut state_action_fut).expect("expected future to be ready");
        if let StateAction::UpdateState(new_state) = action {
            match new_state {
                VsockConnectionState::ClientInitiatedShutdown(state) => {
                    // Run the client initiated shutdown state to send a full shutdown packet to
                    // the guest.
                    let state_action_fut = state.do_state_action();
                    futures::pin_mut!(state_action_fut);
                    assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());
                }
                _ => panic!("Expected transition to client initiated shutdown"),
            }
        } else {
            panic!("Expected a change of state")
        }

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        let flags = VirtioVsockFlags::from_bits(header.flags.get()).expect("unrecognized flag");
        assert_eq!(flags, VirtioVsockFlags::SHUTDOWN_BOTH);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Shutdown);
    }

    #[test]
    fn read_write_rx_obeys_guest_credit() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        // The guest informs the device that it only has 5 bytes of buffer available (the guest
        // RX credit).
        let guest_buf_alloc = 5;
        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditUpdate.into()),
                buf_alloc: LE32::new(guest_buf_alloc),
                fwd_cnt: LE32::new(0),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        // Client writes 9 bytes to the socket. Note that this is above the guest credit, but this
        // is fine as the device will enforce this when writing to the chain.
        let mut received_bytes: Vec<u8> = Vec::new();
        let random_bytes: Vec<u8> = rand::thread_rng().sample_iter(Standard).take(9).collect();
        assert_eq!(
            client_socket.write(&random_bytes).expect("failed to write bytes"),
            random_bytes.len()
        );

        let header_size = std::mem::size_of::<VirtioVsockHeader>();
        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        // Guest credit allows for 5 bytes, so wants_rx_chain future completes true.
        let want_rx_chain_fut = state.wants_rx_chain();
        futures::pin_mut!(want_rx_chain_fut);
        if let Poll::Ready(wants_chain) = executor.run_until_stalled(&mut want_rx_chain_fut) {
            assert!(wants_chain)
        } else {
            panic!("Expected future to be ready")
        };

        // This chain has a single descriptor which is big enough for the 9 byte payload, but
        // only 5 bytes (plus the header) will be written due to the guest credit requirement.
        queue_state
            .fake_queue
            .publish(ChainBuilder::new().writable(header_size as u32 + 50, &mem).build())
            .expect("failed to publish writable chain");

        state
            .handle_rx_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .expect("failed to handle rx chain");

        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");

        assert_eq!(used_chain.written(), header_size as u32 + 5);
        let (data, len) =
            used_chain.data_iter().next().expect("there should be one filled descriptor");
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };

        // Only part of the chain was filled due to the allowable guest credit.
        let header =
            VirtioVsockHeader::read_from(&slice[..header_size]).expect("failed to read header");
        assert_eq!(header.len.get(), 5);
        received_bytes.extend_from_slice(&slice[header_size..]);

        // No guest credit, so the connection doesn't request an RX chain.
        let want_rx_chain_fut = state.wants_rx_chain();
        futures::pin_mut!(want_rx_chain_fut);
        assert!(executor.run_until_stalled(&mut want_rx_chain_fut).is_pending());

        // Increasing fwd_cnt to 5 means the guest has processed all bytes in its buffer, so the
        // device can fill another chain.
        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditUpdate.into()),
                buf_alloc: LE32::new(guest_buf_alloc),
                fwd_cnt: LE32::new(5),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        // Device wants a chain again now that guest credit is available.
        let want_rx_chain_fut = state.wants_rx_chain();
        futures::pin_mut!(want_rx_chain_fut);
        if let Poll::Ready(wants_chain) = executor.run_until_stalled(&mut want_rx_chain_fut) {
            assert!(wants_chain)
        } else {
            panic!("Expected future to be ready")
        };

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().writable(header_size as u32 + 50, &mem).build())
            .expect("failed to publish writable chain");

        state
            .handle_rx_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .expect("failed to handle rx chain");

        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");

        // Once again the chain is only partially filled, but this time it is because the socket
        // had no more data.
        assert_eq!(used_chain.written(), header_size as u32 + 4);
        let (data, len) =
            used_chain.data_iter().next().expect("there should be one filled descriptor");
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };

        let header =
            VirtioVsockHeader::read_from(&slice[..header_size]).expect("failed to read header");
        assert_eq!(header.len.get(), 4);
        received_bytes.extend_from_slice(&slice[header_size..]);

        assert_eq!(received_bytes, random_bytes);
    }

    #[test]
    fn read_write_guest_send_packet_after_shutdown_send_leeway() {
        let mut executor =
            TestExecutor::new_with_fake_time().expect("failed to create test executor");
        executor.set_fake_time(fuchsia_async::Time::now());

        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditUpdate.into()),
                buf_alloc: LE32::new(u32::MAX),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        // Write some data to the buffer so that dropping the socket doesn't immediately transition
        // to a shutdown state. The device will try and send this data to the guest first.
        let data = vec![1, 2, 3, 4, 5];
        assert_eq!(data.len(), client_socket.write(&data).expect("failed to write to socket"));
        drop(client_socket);

        let fut = state.wants_rx_chain();
        futures::pin_mut!(fut);
        if let Poll::Ready(wants_chain) = executor.run_until_stalled(&mut fut) {
            assert!(wants_chain)
        } else {
            panic!("Expected future to be ready")
        };

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        let flags = VirtioVsockFlags::from_bits(header.flags.get()).expect("unrecognized flag");
        assert_eq!(flags, VirtioVsockFlags::SHUTDOWN_SEND);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Shutdown);

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        let data = [1u8, 2, 3];
        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&data, &mem).build())
            .expect("failed to publish readable chain");
        state
            .handle_tx_chain(
                OpType::ReadWrite,
                VirtioVsockHeader { len: LE32::new(3), ..VirtioVsockHeader::default() },
                ReadableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                ),
            )
            .expect("chain should be accepted but ignored as send is disabled but within leeway");

        // After 1s the TX queue for this connection should be empty, and the guest should have
        // refrained from adding any new chains.
        executor.set_fake_time(fuchsia_async::Time::after(
            zx::Duration::from_seconds(1) + zx::Duration::from_nanos(1),
        ));

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&data, &mem).build())
            .expect("failed to publish readable chain");

        let result = state.handle_tx_chain(
            OpType::ReadWrite,
            VirtioVsockHeader { len: LE32::new(3), ..VirtioVsockHeader::default() },
            ReadableChain::new(
                queue_state.queue.next_chain().expect("failed to get next chain"),
                &mem,
            ),
        );

        // Guest is still sending TX chains even though send is disabled.
        assert!(result.is_err());
    }

    #[fuchsia::test]
    async fn read_write_handle_credit_request() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        // Put 5 bytes on the socket.
        let data = [1u8, 2, 3, 4, 5];
        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&data, &mem).build())
            .expect("failed to publish readable chain");
        state
            .handle_tx_chain(
                OpType::ReadWrite,
                VirtioVsockHeader { len: LE32::new(5), ..VirtioVsockHeader::default() },
                ReadableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                ),
            )
            .expect("state rejected valid tx chain");

        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditRequest.into()),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        // All bytes still pending on client.
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::CreditUpdate);
        assert_eq!(header.fwd_cnt.get(), 0);

        let mut bytes = [0u8; 3];
        assert_eq!(client_socket.read(&mut bytes).expect("failed to read socket"), bytes.len());

        send_header_to_rw_state(
            VirtioVsockHeader {
                op: LE16::new(OpType::CreditRequest.into()),
                ..VirtioVsockHeader::default()
            },
            &state,
        );

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        // Three bytes net transmitted to client.
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::CreditUpdate);
        assert_eq!(header.fwd_cnt.get(), 3);
    }

    #[test]
    fn read_write_unsolicited_credit_update() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (_client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        // The read-write state knows that the device thinks there's no client credit while there
        // actually is, so immediately sends an unsolicited credit update.
        let state = ReadWrite::new(socket, key, ConnectionCredit::default(), control_tx);

        let state_action_fut = state.do_state_action();
        futures::pin_mut!(state_action_fut);
        if let Poll::Ready(action) = executor.run_until_stalled(&mut state_action_fut) {
            assert_eq!(StateAction::ContinueAwaiting, action)
        } else {
            panic!("Expected future to be ready")
        };

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        assert_eq!(header.src_port.get(), key.host_port);
        assert_eq!(header.dst_port.get(), key.guest_port);
        assert_ne!(header.buf_alloc.get(), 0);
        assert_eq!(header.fwd_cnt.get(), 0);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::CreditUpdate);
    }

    #[fuchsia::test]
    async fn guest_initiated_shutdown_device_sends_immediate_reset_packet() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let state = GuestInitiatedShutdown::new(key, control_tx);
        if let StateAction::UpdateState(new_state) =
            state.do_state_action().now_or_never().expect("task should have completed")
        {
            let header = control_rx
                .try_next()
                .expect("expected control packet")
                .expect("control stream should not close");

            assert_eq!(header.src_port.get(), key.host_port);
            assert_eq!(header.dst_port.get(), key.guest_port);
            assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Reset);

            let result =
                new_state.do_state_action().now_or_never().expect("task should have completed");
            assert_eq!(result, StateAction::CleanShutdown);
        } else {
            panic!("Expected state transition");
        }
    }

    #[test]
    fn client_initiated_shutdown_timeout_due_to_no_guest_reply() {
        let mut executor =
            TestExecutor::new_with_fake_time().expect("failed to create test executor");
        executor.set_fake_time(fuchsia_async::Time::now());

        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let state = ClientInitiatedShutdown::new(key, control_tx);
        let state_action_fut = state.do_state_action();
        futures::pin_mut!(state_action_fut);

        // Pending waiting for a reset packet from the guest to confirm a clean shutdown.
        assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());

        // Shutdown packet was sent to the guest.
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        let flags = VirtioVsockFlags::from_bits(header.flags.get()).expect("unrecognized flag");
        assert_eq!(flags, VirtioVsockFlags::SHUTDOWN_BOTH);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Shutdown);

        executor.set_fake_time(fuchsia_async::Time::after(zx::Duration::from_seconds(5)));
        assert!(executor.wake_expired_timers());

        // 5 seconds have passed, so the connection is no longer willing to wait for a guest reply.
        // This instead transitions into a forced shutdown state.
        let action_result =
            if let Poll::Ready(action) = executor.run_until_stalled(&mut state_action_fut) {
                action
            } else {
                panic!("Expected future to be ready")
            };

        if let StateAction::UpdateState(new_state) = action_result {
            let result =
                new_state.do_state_action().now_or_never().expect("task should have completed");
            assert_eq!(result, StateAction::ForcedShutdown);
        } else {
            panic!("Expected state change from state action");
        }
    }

    #[fuchsia::test]
    async fn client_initiated_shutdown_guest_sends_reset_for_clean_shutdown() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        // A client initiated shutdown is the only time a Reset packet doesn't send the state
        // into a forced shutdown.
        let state = ClientInitiatedShutdown::new(key, control_tx);
        let new_state = state.next(OpType::Reset);

        let result =
            new_state.do_state_action().now_or_never().expect("task should have completed");
        assert_eq!(result, StateAction::CleanShutdown);
    }

    #[fuchsia::test]
    async fn shutdown_clean_incorrectly_receives_additional_packets() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let state = ShutdownClean::new(key, control_tx);

        // Was in a clean shutdown state.
        let result = state.do_state_action().now_or_never().expect("task should have completed");
        assert_eq!(result, StateAction::CleanShutdown);

        // Guest sent additional packets.
        let new_state = state.next(OpType::CreditRequest);

        // Now in a forced shutdown state.
        let result =
            new_state.do_state_action().now_or_never().expect("task should have completed");
        assert_eq!(result, StateAction::ForcedShutdown);
    }

    #[fuchsia::test]
    async fn shutdown_forced_sends_reset_packet() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let state = ShutdownForced::new(key, control_tx);
        let result = state.do_state_action().now_or_never().expect("task should have completed");

        assert_eq!(result, StateAction::ForcedShutdown);
        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        assert_eq!(header.src_port.get(), key.host_port);
        assert_eq!(header.dst_port.get(), key.guest_port);
        assert_eq!(OpType::try_from(header.op.get()).unwrap(), OpType::Reset);
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/97355): Remove once the device is complete.
#![allow(dead_code)]

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
    fidl_fuchsia_virtualization::HostVsockEndpointConnect2Responder,
    fuchsia_async as fasync, fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::UnboundedSender,
        future::{self, poll_fn},
        AsyncWriteExt, FutureExt,
    },
    std::{
        cell::{Cell, RefCell},
        convert::TryFrom,
    },
    virtio_device::{chain::ReadableChain, mem::DriverMem, queue::DriverNotify},
};

#[derive(Debug)]
pub struct GuestInitiated {
    listener_response: RefCell<Option<QueryResponseFut<Result<zx::Socket, i32>>>>,

    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl GuestInitiated {
    pub fn new(
        listener_response: QueryResponseFut<Result<zx::Socket, i32>>,
        control_packets: UnboundedSender<VirtioVsockHeader>,
        key: VsockConnectionKey,
    ) -> Self {
        GuestInitiated {
            listener_response: RefCell::new(Some(listener_response)),
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
                syslog::fx_log_err!("Unsupported GuestInitiated operation: {:?}", op);
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
                    self.control_packets.clone(),
                )))
            }
            Err(err) => {
                syslog::fx_log_err!("Failed to transition from GuestInitiated with error: {}", err);
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
    responder: Option<HostVsockEndpointConnect2Responder>,

    key: VsockConnectionKey,

    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl ClientInitiated {
    pub fn new(
        responder: HostVsockEndpointConnect2Responder,
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

    fn next(mut self, op: OpType) -> VsockConnectionState {
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

                match get_socket() {
                    Ok(socket) => VsockConnectionState::ReadWrite(ReadWrite::new(
                        socket,
                        self.key,
                        self.control_packets.clone(),
                    )),
                    Err(err) => {
                        syslog::fx_log_err!("Failed to transition out of ClientInitiated: {}", err);
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
                syslog::fx_log_err!("Unsupported ClientInitiated operation: {:?}", op);
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
                syslog::fx_log_err!("Connection failed to send closing message: {}", err);
            }
        }
    }
}

#[derive(Debug)]
pub struct ReadWrite {
    // Device local socket end. The remote socket end is held by the client.
    socket: RefCell<fasync::Socket>,

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

impl ReadWrite {
    fn new(
        socket: fasync::Socket,
        key: VsockConnectionKey,
        control_packets: UnboundedSender<VirtioVsockHeader>,
    ) -> Self {
        ReadWrite {
            socket: RefCell::new(socket),
            credit: RefCell::new(ConnectionCredit::default()),
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
            .write_credit(&self.socket.borrow(), header)
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

    fn send_shutdown_packet(&self) {
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
            if let Err(err) = self.socket.borrow().as_ref().half_close() {
                syslog::fx_log_err!(
                    "Failed to half close remote socket upon receiving \
                    VirtioVsockFlags::SHUTDOWN_RECIEVE: {}",
                    err
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
                syslog::fx_log_err!("Unsupported ReadWrite operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
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
            let result = match poll_fn(move |cx| self.socket.borrow().poll_write_task(cx)).await {
                Ok(closed) => {
                    if closed {
                        self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
                        self.send_shutdown_packet();
                        Ok(())
                    } else {
                        self.send_credit_update()
                            .map_err(|err| anyhow!("failed to send credit update: {}", err))
                    }
                }
                Err(err) => Err(anyhow!("failed to poll write task: {}", err)),
            };

            if let Err(err) = result {
                syslog::fx_log_err!("{}", err);
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

    // Returns true when the connection wants the next available RX chain. Returns false if the
    // device should stop waiting on this connection, such as if the connection is transitioning to
    // a closed state.
    async fn wants_rx_chain(&self) -> bool {
        if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_RECIEVE) {
            return false;
        }

        match poll_fn(move |cx| self.socket.borrow().poll_read_task(cx)).await {
            Ok(closed) if !closed => true,
            _ => {
                match self.socket.borrow().as_ref().outstanding_read_bytes() {
                    Ok(size) if size > 0 => {
                        // The socket peer is closed, but bytes remain on the local socket that
                        // have yet to be transmitted to the guest. Signal to the guest not to
                        // send any more data if not already done.
                        if !self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_SEND) {
                            *self.tx_shutdown_leeway.borrow_mut() =
                                fasync::Time::after(zx::Duration::from_seconds(1));
                            self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
                            self.send_shutdown_packet();
                        }

                        // Connection still needs RX.
                        true
                    }
                    _ => {
                        // The socket is closed with no bytes remaining.
                        self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_BOTH, true);
                        self.send_shutdown_packet();

                        // Client has closed the socket and the RX buffer has been drained.
                        false
                    }
                }
            }
        }
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
            match self.socket.borrow_mut().write_all(slice).now_or_never() {
                Some(result) => match result {
                    Err(_)
                        if !self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_SEND) =>
                    {
                        *self.tx_shutdown_leeway.borrow_mut() =
                            fasync::Time::after(zx::Duration::from_seconds(1));
                        self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
                        self.send_shutdown_packet();
                        return Ok(());
                    }
                    result => result
                        .map_err(|err| anyhow!("Failed to write to socket with error: {}", err)),
                },
                None => {
                    // 5.10.6.3.1 Driver Requirements: Device Operation: Buffer Space Management
                    //
                    // VIRTIO_VSOCK_OP_RW data packets MUST only be transmitted when the peer has
                    // sufficient free buffer space for the payload.
                    Err(anyhow!(
                        "Socket write tried to block. Guest is not honoring the reported \
                        socket credit"
                    ))
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
                syslog::fx_log_err!("Unsupported GuestInitiatedShutdown operation: {:?}", op);
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
    // A client initiated shutdown will wait this long for a guest to reply with a reset
    // packet.
    timeout: fasync::Time,

    key: VsockConnectionKey,
    control_packets: UnboundedSender<VirtioVsockHeader>,
}

impl ClientInitiatedShutdown {
    fn new(key: VsockConnectionKey, control_packets: UnboundedSender<VirtioVsockHeader>) -> Self {
        ClientInitiatedShutdown {
            timeout: fasync::Time::after(zx::Duration::from_seconds(5)),
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
                syslog::fx_log_err!("Guest sent shutdown while being asked to shutdown");
                VsockConnectionState::ShutdownForced(ShutdownForced::new(
                    self.key,
                    self.control_packets.clone(),
                ))
            }
            OpType::Reset => VsockConnectionState::ShutdownClean(ShutdownClean::new(
                self.key,
                self.control_packets.clone(),
            )),
            _ => {
                // The guest may have already had pending TX packets on the queue when it received
                // a shutdown notice, so these can be dropped.
                VsockConnectionState::ClientInitiatedShutdown(self)
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
        // Returns once the state has waited 5s from creation for a guest response.
        fasync::Timer::new(self.timeout).await;

        syslog::fx_log_err!("Guest didn't send a clean disconnect within 5s");
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
        syslog::fx_log_err!(
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
        syslog::fx_log_err!("Connection is in ShutdownForced, disregarding operation {:?}", op);
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

impl VsockConnectionState {
    pub fn handle_operation(self, op: OpType, flags: VirtioVsockFlags) -> Self {
        match self {
            VsockConnectionState::Invalid => panic!("Connection entered an impossible state"),
            VsockConnectionState::GuestInitiated(state) => state.next(op),
            VsockConnectionState::ClientInitiated(state) => state.next(op),
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
            VsockConnectionState::GuestInitiated(state) => state.do_state_action().await,
            VsockConnectionState::GuestInitiatedShutdown(state) => state.do_state_action().await,
            VsockConnectionState::ReadWrite(state) => state.do_state_action().await,
            VsockConnectionState::ShutdownClean(state) => state.do_state_action().await,
            VsockConnectionState::ShutdownForced(state) => state.do_state_action().await,
            _ => {
                // Some states have no actions, and are waiting on guest instruction passed via
                // TX queue.
                future::pending::<StateAction>().await
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
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::{
            HostVsockAcceptorMarker, HostVsockEndpointMarker, DEFAULT_GUEST_CID, HOST_CID,
        },
        fuchsia_async::TestExecutor,
        futures::{channel::mpsc, TryStreamExt},
        std::{io::Read, task::Poll},
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
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

    #[fuchsia::test]
    async fn valid_optype_for_state() {
        // TODO(fxb/97355): Check which op types passed to a state result in valid state transitions.
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
        let state = GuestInitiated::new(response_fut, control_tx, key);

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
        let state = GuestInitiated::new(response_fut, control_tx, key);

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

        let mut fut = proxy.connect2(key.guest_port);
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let (_guest_port, responder) = if let Poll::Ready(val) =
            executor.run_until_stalled(&mut stream.try_next())
        {
            val.unwrap().unwrap().into_connect2().expect("received unexpected response on stream")
        } else {
            panic!("Expected future to be ready")
        };

        // Guest responded before the client actually made a request. This implies that the
        // two are out of sync.
        let state = ClientInitiated::new(responder, control_tx, key);
        match state.next(OpType::Response) {
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

        let mut fut = proxy.connect2(key.guest_port);
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let (_guest_port, responder) = if let Poll::Ready(val) =
            executor.run_until_stalled(&mut stream.try_next())
        {
            val.unwrap().unwrap().into_connect2().expect("received unexpected response on stream")
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

        match state.next(OpType::Response) {
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

    #[fuchsia::test]
    async fn read_write_guest_half_close_remote_socket() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        assert_eq!(client_socket.write(b"success!").expect("failed to write to socket"), 8);

        let state = ReadWrite::new(socket, key, control_tx);
        match state.next(OpType::Shutdown, VirtioVsockFlags::SHUTDOWN_RECIEVE) {
            VsockConnectionState::ReadWrite(_state) => {
                // Socket is half closed so unable to transmit data.
                assert_eq!(client_socket.write(b"failure"), Err(zx::Status::BAD_STATE));
            }
            _ => panic!("Expected state to remain in ReadWrite state"),
        }
    }

    #[fuchsia::test]
    async fn read_write_client_close_socket_rx_bytes_outstanding() {
        // TODO(fxb/97355): Write test once we have RX support.
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

        let state = ReadWrite::new(socket, key, control_tx);

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

        let state = ReadWrite::new(socket, key, control_tx);

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

    #[fuchsia::test]
    async fn read_write_unsolicited_credit_update() {
        let key = VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 10);
        let (control_tx, mut control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let (_client_socket, device_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let socket =
            fasync::Socket::from_socket(device_socket).expect("failed to create async socket");

        // The read-write state knows that the device thinks there's no client credit while there
        // actually is, so immediately sends an unsolicited credit update.
        let state = ReadWrite::new(socket, key, control_tx);
        assert_eq!(
            StateAction::ContinueAwaiting,
            state.do_state_action().now_or_never().expect("task should have completed")
        );

        let header = control_rx
            .try_next()
            .expect("expected control packet")
            .expect("control stream should not close");

        assert_eq!(header.src_port.get(), key.host_port);
        assert_eq!(header.dst_port.get(), key.guest_port);
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
        let (control_tx, _control_rx) = mpsc::unbounded::<VirtioVsockHeader>();

        let state = ClientInitiatedShutdown::new(key, control_tx);
        let state_action_fut = state.do_state_action();
        futures::pin_mut!(state_action_fut);

        // Pending waiting for a reset packet from the guest to confirm a clean shutdown.
        assert!(executor.run_until_stalled(&mut state_action_fut).is_pending());

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

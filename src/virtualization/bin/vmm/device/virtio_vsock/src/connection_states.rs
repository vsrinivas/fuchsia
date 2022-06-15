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
    crate::connection::{ConnectionCredit, CreditState},
    crate::wire::{OpType, VirtioVsockFlags, VirtioVsockHeader},
    anyhow::{anyhow, Error},
    fidl::client::QueryResponseFut,
    fidl_fuchsia_virtualization::HostVsockEndpointConnect2Responder,
    fuchsia_async as fasync, fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::{
        future::{self, poll_fn},
        AsyncWriteExt, FutureExt,
    },
    std::{cell::RefCell, convert::TryFrom},
    virtio_device::{chain::ReadableChain, mem::DriverMem, queue::DriverNotify},
};

#[derive(Debug)]
pub struct GuestInitiated {
    listener_response: RefCell<Option<QueryResponseFut<Result<zx::Socket, i32>>>>,
}

impl GuestInitiated {
    pub fn new(listener_response: QueryResponseFut<Result<zx::Socket, i32>>) -> Self {
        GuestInitiated { listener_response: RefCell::new(Some(listener_response)) }
    }

    fn next(self, op: OpType) -> VsockConnectionState {
        // A guest initiated connection is waiting for a client response, so the only valid
        // transitions are to a clean or forced shutdown.
        match op {
            OpType::Shutdown => {
                VsockConnectionState::GuestInitiatedShutdown(GuestInitiatedShutdown)
            }
            op => {
                syslog::fx_log_err!("Unsupported GuestInitiated operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced)
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
                StateAction::UpdateState(VsockConnectionState::ReadWrite(ReadWrite::new(socket)))
            }
            Err(err) => {
                syslog::fx_log_err!("Failed to transition from GuestInitiated with error: {}", err);
                StateAction::UpdateState(VsockConnectionState::ShutdownForced(ShutdownForced))
            }
        }
    }
}

#[derive(Debug)]
pub struct ClientInitiated {
    sent_request_to_guest: bool,
    responder: Option<HostVsockEndpointConnect2Responder>,
}

impl ClientInitiated {
    pub fn new(responder: HostVsockEndpointConnect2Responder) -> Self {
        ClientInitiated { sent_request_to_guest: false, responder: Some(responder) }
    }

    fn next(mut self, op: OpType) -> VsockConnectionState {
        match op {
            OpType::Response => {
                let mut get_socket = || -> Result<fasync::Socket, Error> {
                    if self.responder.is_none() {
                        panic!("ClientInitiated responder was consumed before state transition");
                    }

                    if self.sent_request_to_guest {
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
                    Ok(socket) => VsockConnectionState::ReadWrite(ReadWrite::new(socket)),
                    Err(err) => {
                        syslog::fx_log_err!("Failed to transition out of ClientInitiated: {}", err);
                        VsockConnectionState::ShutdownForced(ShutdownForced)
                    }
                }
            }
            OpType::Shutdown => {
                VsockConnectionState::GuestInitiatedShutdown(GuestInitiatedShutdown)
            }
            op => {
                syslog::fx_log_err!("Unsupported ClientInitiated operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced)
            }
        }
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
}

impl ReadWrite {
    fn new(socket: fasync::Socket) -> Self {
        ReadWrite {
            socket: RefCell::new(socket),
            credit: RefCell::new(ConnectionCredit::default()),
            conn_flags: RefCell::new(VirtioVsockFlags::default()),
        }
    }

    fn read_credit(&self, header: &VirtioVsockHeader) {
        self.credit.borrow_mut().read_credit(&header)
    }

    fn write_credit(&self, header: &mut VirtioVsockHeader) -> Result<CreditState, Error> {
        self.credit
            .borrow_mut()
            .write_credit(&self.socket.borrow(), header)
            .map_err(|err| anyhow!("Failed to write current credit to header: {}", err))
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
                return VsockConnectionState::ShutdownForced(ShutdownForced);
            }
        }

        // Flags are cumulative once seen.
        let flags = self.conn_flags.borrow().union(flags);
        *self.conn_flags.borrow_mut() = flags;

        match op {
            OpType::ReadWrite => VsockConnectionState::ReadWrite(self),
            OpType::Shutdown => {
                if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_BOTH) {
                    // This connection is now able to begin transitioning to a clean guest
                    // initiated shutdown.
                    VsockConnectionState::GuestInitiatedShutdown(GuestInitiatedShutdown)
                } else {
                    VsockConnectionState::ReadWrite(self)
                }
            }
            OpType::CreditRequest => {
                // TODO(fxb/97355): Handle credit request.
                VsockConnectionState::ReadWrite(self)
            }
            OpType::CreditUpdate => {
                // TODO(fxb/97355): Handle credit update.
                VsockConnectionState::ReadWrite(self)
            }
            op => {
                syslog::fx_log_err!("Unsupported ReadWrite operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced)
            }
        }
    }

    async fn do_state_action(&self) -> StateAction {
        if self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_BOTH) {
            return StateAction::UpdateState(VsockConnectionState::ClientInitiatedShutdown(
                ClientInitiatedShutdown,
            ));
        }

        // If the outgoing socket buffer is full the device must wait on space available, and
        // inform the guest with a credit update. Otherwise, this state does nothing until this
        // future is aborted and re-run.
        if self.credit.borrow().informed_guest_buffer_full()
            && !self.conn_flags.borrow().contains(VirtioVsockFlags::SHUTDOWN_SEND)
        {
            if let Ok(closed) = poll_fn(move |cx| self.socket.borrow().poll_write_task(cx)).await {
                if closed {
                    self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
                } else {
                    let mut header = VirtioVsockHeader::default();
                    if let Err(err) = self.write_credit(&mut header) {
                        syslog::fx_log_err!("Failed to write credit to header: {}", err);
                        return StateAction::UpdateState(VsockConnectionState::ShutdownForced(
                            ShutdownForced,
                        ));
                    }
                    // TODO(fxb/97355): Send credit update.
                }
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
                        // The socket peer is closed for writing, but bytes remain on the local
                        // socket that have yet to be transmitted to the guest.
                        true
                    }
                    _ => {
                        // The socket is closed with no bytes remaining.
                        self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_BOTH, true);
                        false
                    }
                }
            }
        }
    }

    fn handle_tx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        header: VirtioVsockHeader,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        self.read_credit(&header);

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

        // TODO(fxb/97355): Let the guest send a few packets after VirtioVsockFlags::SHUTDOWN_SEND
        // to account for when the client half closes the socket but the guest already had
        // packets on thw TX queue.

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
                        // TODO(fxb/97355): Send VirtioVsockFlags::SHUTDOWN_SEND to guest.
                        self.conn_flags.borrow_mut().set(VirtioVsockFlags::SHUTDOWN_SEND, true);
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
pub struct GuestInitiatedShutdown;

impl GuestInitiatedShutdown {
    fn next(self, op: OpType) -> VsockConnectionState {
        // It's fine for the guest to send multiple shutdown packets, but any other type of
        // packet is unexpected.
        match op {
            OpType::Shutdown => VsockConnectionState::GuestInitiatedShutdown(self),
            op => {
                syslog::fx_log_err!("Unsupported GuestInitiatedShutdown operation: {:?}", op);
                VsockConnectionState::ShutdownForced(ShutdownForced)
            }
        }
    }

    async fn do_state_action(&self) -> StateAction {
        // TODO(fxb/97355): Send reset packet to guest.
        StateAction::UpdateState(VsockConnectionState::ShutdownClean(ShutdownClean))
    }
}

#[derive(Debug)]
pub struct ClientInitiatedShutdown;

impl ClientInitiatedShutdown {
    fn next(self, op: OpType) -> VsockConnectionState {
        match op {
            OpType::Shutdown => {
                // A guest sending the device shutdown after itself being sent shutdown makes this
                // difficult to ensure a clean disconnect, so simply force shutdown and briefly
                // quarantine the ports.
                syslog::fx_log_err!("Guest sent shutdown while being asked to shutdown");
                VsockConnectionState::ShutdownForced(ShutdownForced)
            }
            OpType::Reset => VsockConnectionState::ShutdownClean(ShutdownClean),
            _ => {
                // The guest may have already had pending TX packets on the queue when it received
                // a shutdown notice, so these can be dropped.
                VsockConnectionState::ClientInitiatedShutdown(self)
            }
        }
    }

    fn handle_tx_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        _header: VirtioVsockHeader,
        _chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // Drop the chain without walking it. The client is asking for the guest to initiate
        // shutdown, but the guest may have already written packets to its TX queue.
        Ok(())
    }

    async fn do_state_action(&self) -> StateAction {
        // TODO(fxb/97355): Give the guest 10s to reply with a reset before force disconnecting.
        syslog::fx_log_err!("Guest didn't send a clean disconnect within 10s");
        StateAction::UpdateState(VsockConnectionState::ShutdownForced(ShutdownForced))
    }
}

#[derive(Debug)]
pub struct ShutdownClean;

impl ShutdownClean {
    fn next(self, op: OpType) -> VsockConnectionState {
        syslog::fx_log_err!(
            "Connection in ShutdownClean expected no more packets, \
            but received operation {:?}",
            op
        );
        VsockConnectionState::ShutdownForced(ShutdownForced)
    }

    async fn do_state_action(&self) -> StateAction {
        StateAction::CleanShutdown
    }
}

#[derive(Debug)]
pub struct ShutdownForced;

impl ShutdownForced {
    fn next(self, op: OpType) -> VsockConnectionState {
        syslog::fx_log_err!("Connection is in ShutdownForced, disregarding operation {:?}", op);
        VsockConnectionState::ShutdownForced(self)
    }

    async fn do_state_action(&self) -> StateAction {
        // TODO(fxb/97355): Send reset packet.
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
        &mut self,
        header: VirtioVsockHeader,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        match self {
            VsockConnectionState::Invalid => panic!("Connection entered an impossible state"),
            VsockConnectionState::ReadWrite(state) => state.handle_tx_chain(header, chain),
            VsockConnectionState::ClientInitiatedShutdown(state) => {
                state.handle_tx_chain(header, chain)
            }
            _ => {
                // Consume the chain. The default behaviour for states is to validate that the
                // chain has been fully walked, which will be true for states that only expect a
                // header on the chain.
                chain.return_complete().map_err(|err| anyhow!("Failed to complete chain: {}", err))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn valid_optype_for_state() {
        // TODO(fxb/97355): Check which op types passed to a state result in valid state transitions.
    }
}

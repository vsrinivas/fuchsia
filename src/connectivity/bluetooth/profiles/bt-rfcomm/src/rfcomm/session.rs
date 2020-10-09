// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Channel, PeerId},
    futures::{
        channel::mpsc,
        future::BoxFuture,
        select,
        task::{noop_waker_ref, Context},
        Future, FutureExt, SinkExt, StreamExt,
    },
    log::{error, info, trace, warn},
    std::{collections::HashMap, convert::TryInto},
};

use crate::rfcomm::channel::SessionChannel;
use crate::rfcomm::frame::{
    mux_commands::{CreditBasedFlowHandshake, MuxCommand, MuxCommandParams},
    Encodable, Frame, FrameData, UIHData,
};
use crate::rfcomm::types::{
    CommandResponse, RfcommError, Role, ServerChannel, DLCI, MAX_RFCOMM_FRAME_SIZE,
};

/// A function used to relay an opened RFCOMM channel to a client.
type ChannelOpenedFn =
    Box<dyn Fn(ServerChannel, Channel) -> BoxFuture<'static, Result<(), Error>> + Send + Sync>;

/// The parameters associated with this Session.
#[derive(Clone, Copy, Debug, PartialEq)]
struct SessionParameters {
    /// Whether credit-based flow control is being used for this session.
    credit_based_flow: bool,

    /// The max MTU size of a frame.
    max_frame_size: usize,
}

impl SessionParameters {
    /// Combines the current session parameters with the `other` parameters and returns
    /// a negotiated SessionParameters.
    fn negotiated(&self, other: &SessionParameters) -> Self {
        // Our implementation is OK with credit based flow. We choose whatever the new
        // configuration requests.
        let credit_based_flow = other.credit_based_flow;
        // Use the smaller (i.e more restrictive) max frame size.
        let max_frame_size = std::cmp::min(self.max_frame_size, other.max_frame_size);
        Self { credit_based_flow, max_frame_size }
    }

    /// Returns true if credit-based flow control is set.
    fn credit_based_flow(&self) -> bool {
        self.credit_based_flow
    }
}

impl Default for SessionParameters {
    fn default() -> Self {
        // Credit based flow must always be preferred - see RFCOMM 5.5.3.
        Self { credit_based_flow: true, max_frame_size: MAX_RFCOMM_FRAME_SIZE }
    }
}

/// The current state of the session parameters.
// TODO(fxbug.dev/59585): When the INT role is supported, introduce the `Negotiating` state for
// the case of us waiting for a parameter negotiation response.
#[derive(Clone, Copy, PartialEq)]
enum ParameterNegotiationState {
    /// Parameters have not been negotiated.
    NotNegotiated,
    /// Parameters have been negotiated.
    Negotiated(SessionParameters),
}

impl ParameterNegotiationState {
    /// Returns the current parameters.
    ///
    /// If the parameters have not been negotiated, then the default is returned.
    fn parameters(&self) -> SessionParameters {
        match self {
            Self::Negotiated(params) => *params,
            Self::NotNegotiated => SessionParameters::default(),
        }
    }

    /// Returns true if the parameters have been negotiated.
    fn is_negotiated(&self) -> bool {
        match self {
            Self::Negotiated(_) => true,
            Self::NotNegotiated => false,
        }
    }

    /// Negotiates the `new` parameters with the (potentially) current parameters. Returns
    /// the parameters that were set.
    fn negotiate(&mut self, new: SessionParameters) -> SessionParameters {
        let updated = self.parameters().negotiated(&new);
        *self = Self::Negotiated(updated);
        updated
    }
}

/// The `SessionMultiplexer` manages channels over the range of valid User-DLCIs. It is responsible
/// for maintaining the current state of the RFCOMM Session, and provides an API to create,
/// establish, and relay user data over the multiplexed channels.
///
/// The `SessionMultiplexer` is considered "started" when its Role has been assigned.
/// The parameters for the multiplexer must be negotiated before the first DLCI has
/// been established. RFCOMM 5.5.3 states that renegotiation of parameters is optional - this
/// multiplexer will simply echo the current parameters in the event a negotiation request is
/// received after the first DLC is opened and established.
struct SessionMultiplexer {
    /// The role for the multiplexer.
    role: Role,

    /// The parameters for the multiplexer.
    parameters: ParameterNegotiationState,

    /// Local opened RFCOMM channels for this session.
    channels: HashMap<DLCI, SessionChannel>,
}

impl SessionMultiplexer {
    fn create() -> Self {
        Self {
            role: Role::Unassigned,
            parameters: ParameterNegotiationState::NotNegotiated,
            channels: HashMap::new(),
        }
    }

    fn role(&self) -> Role {
        self.role
    }

    fn set_role(&mut self, role: Role) {
        self.role = role;
    }

    /// Returns true if credit-based flow control is enabled.
    fn credit_based_flow(&self) -> bool {
        self.parameters().credit_based_flow()
    }

    /// Returns true if the session parameters have been negotiated.
    fn parameters_negotiated(&self) -> bool {
        self.parameters.is_negotiated()
    }

    /// Returns the parameters associated with this session.
    fn parameters(&self) -> SessionParameters {
        self.parameters.parameters()
    }

    /// Negotiates the parameters associated with this session - returns the session parameters
    /// that were set.
    fn negotiate_parameters(
        &mut self,
        new_session_parameters: SessionParameters,
    ) -> SessionParameters {
        // The session parameters can only be modified if no DLCs have been established.
        if self.dlc_established() {
            warn!(
                "Received negotiation request when at least one DLC has already been established"
            );
            return self.parameters();
        }

        // Otherwise, it is OK to negotiate the multiplexer parameters.
        let updated = self.parameters.negotiate(new_session_parameters);
        trace!("Updated Session parameters: {:?}", updated);
        updated
    }

    /// Returns true if the multiplexer has started.
    fn started(&self) -> bool {
        self.role.is_multiplexer_started()
    }

    /// Starts the session multiplexer and assumes the provided `role`. Returns Ok(()) if mux
    /// startup is successful.
    fn start(&mut self, role: Role) -> Result<(), RfcommError> {
        // Re-starting the multiplexer is not valid, as this would invalidate any opened
        // RFCOMM channels.
        if self.started() {
            return Err(RfcommError::MultiplexerAlreadyStarted);
        }

        // Role must be a valid started role.
        if !role.is_multiplexer_started() {
            return Err(RfcommError::InvalidRole(role));
        }

        self.set_role(role);
        trace!("Session multiplexer started with role: {:?}", role);
        Ok(())
    }

    /// Returns true if at least one DLC has been established.
    fn dlc_established(&self) -> bool {
        self.channels
            .iter()
            .fold(false, |acc, (_, session_channel)| acc | session_channel.is_established())
    }

    /// Finds or initializes a new SessionChannel for the provided `dlci`. Returns a mutable
    /// reference to the channel.
    fn find_or_create_session_channel(&mut self, dlci: DLCI) -> &mut SessionChannel {
        let channel = self.channels.entry(dlci).or_insert(SessionChannel::new(dlci));
        channel
    }

    /// Attempts to establish a SessionChannel for the provided `dlci`. Returns the remote end of
    /// the channel on success.
    fn establish_session_channel(&mut self, dlci: DLCI) -> Result<Channel, RfcommError> {
        // If the session parameters have not been negotiated, set them to the default.
        if !self.parameters_negotiated() {
            self.negotiate_parameters(SessionParameters::default());
        }

        // Potentially reserve a new SessionChannel for the provided DLCI.
        let channel = self.find_or_create_session_channel(dlci);
        if channel.is_established() {
            return Err(RfcommError::ChannelAlreadyEstablished(dlci));
        }

        // Create endpoints for the multiplexed channel. Establish the local end and
        // return the remote end.
        let (local, remote) = Channel::create();
        channel.establish(local);
        Ok(remote)
    }
}

/// An RFCOMM Session that multiplexes multiple channels over a single channel. This object
/// handles the business logic for an RFCOMM Session. Namely, it parses and handles RFCOMM
/// frames, modifies the state and role of the Session, and multiplexes any opened
/// RFCOMM channels.
///
/// A `SessionInner` is represented by a processing task `run()` which processes incoming bytes
/// from the provided `data_receiver`.
/// An owner of the `SessionInner` should use `SessionInner::create()` to start a new RFCOMM
/// Session over the provided `data_receiver`.
pub struct SessionInner {
    /// The session multiplexer that manages the current state of the session and any opened
    /// RFCOMM channels.
    multiplexer: SessionMultiplexer,

    /// The channel opened callback that is called anytime a new RFCOMM channel is opened. The
    /// `SessionInner` will relay the client end of the channel to this closure.
    channel_opened_fn: ChannelOpenedFn,
}

impl SessionInner {
    /// Creates a new RFCOMM SessionInner and returns a Future that processes data over the
    /// provided `data_receiver`.
    /// `frame_sender` is used to relay RFCOMM frames to be sent to the remote peer.
    /// `channel_opened_fn` is used by the SessionInner to relay opened RFCOMM channels to
    /// local clients.
    pub fn create(
        data_receiver: mpsc::Receiver<Vec<u8>>,
        frame_sender: mpsc::Sender<Frame>,
        channel_opened_fn: ChannelOpenedFn,
    ) -> impl Future<Output = Result<(), Error>> {
        let session = Self { multiplexer: SessionMultiplexer::create(), channel_opened_fn };
        session.run(data_receiver, frame_sender)
    }

    fn multiplexer(&mut self) -> &mut SessionMultiplexer {
        &mut self.multiplexer
    }

    fn role(&self) -> Role {
        self.multiplexer.role()
    }

    /// Returns true if credit-based flow control is enabled for this session.
    fn credit_based_flow(&self) -> bool {
        self.multiplexer.credit_based_flow()
    }

    #[cfg(test)]
    fn session_parameters(&self) -> SessionParameters {
        self.multiplexer.parameters()
    }

    #[cfg(test)]
    fn session_parameters_negotiated(&self) -> bool {
        self.multiplexer.parameters_negotiated()
    }

    /// Establishes the SessionChannel for the provided `dlci`. Returns true if establishment
    /// is successful.
    async fn establish_session_channel(&mut self, dlci: DLCI) -> bool {
        match self.multiplexer().establish_session_channel(dlci) {
            Ok(channel) => {
                if let Err(e) =
                    self.relay_channel_to_client(dlci.try_into().unwrap(), channel).await
                {
                    warn!("Couldn't relay channel to client: {:?}", e);
                    return false;
                }
                trace!("Established RFCOMM Channel with DLCI: {:?}", dlci);
                true
            }
            Err(e) => {
                warn!("Couldn't establish DLCI {:?}: {:?}", dlci, e);
                false
            }
        }
    }

    /// Relays the `channel` opened for the provided `server_channel` to the local clients
    /// of the session.
    async fn relay_channel_to_client(
        &self,
        server_channel: ServerChannel,
        channel: Channel,
    ) -> Result<(), RfcommError> {
        (self.channel_opened_fn)(server_channel, channel)
            .await
            .map_err(|e| format_err!("{:?}", e).into())
    }

    /// Handles a SABM command over the given `dlci`. Returns a response Frame for the command.
    ///
    /// There are two important cases:
    /// 1) Mux Control DLCI - indicates request to start up the session multiplexer.
    /// 2) User DLCI - indicates request to establish up an RFCOMM channel over the provided `dlci`.
    async fn handle_sabm_command(&mut self, dlci: DLCI) -> Frame {
        trace!("Handling SABM with DLCI: {:?}", dlci);
        if dlci.is_mux_control() {
            let response = match &self.role() {
                Role::Unassigned => {
                    // Remote device has requested to start up the multiplexer, respond positively
                    // and assume the Responder role.
                    match self.multiplexer().start(Role::Responder) {
                        Ok(_) => Frame::make_ua_response(self.role(), dlci),
                        Err(e) => {
                            warn!("Mux startup failed: {:?}", e);
                            Frame::make_dm_response(self.role(), dlci)
                        }
                    }
                }
                Role::Negotiating => {
                    // We're currently negotiating the multiplexer role. We should send a DM, and
                    // attempt to restart the multiplexer after a random interval. See RFCOMM 5.2.1
                    Frame::make_dm_response(self.role(), dlci)
                    // TODO(fxbug.dev/59585): When we support the INT role, we should attempt to
                    // restart the multiplexer.
                }
                _role => {
                    // Remote device incorrectly trying to start up the multiplexer when it has
                    // already started. This is invalid - send a DM to respond negatively.
                    warn!("Received SABM when multiplexer already started");
                    Frame::make_dm_response(self.role(), dlci)
                }
            };
            return response;
        }

        // Otherwise, it's a request to open a user channel. Attempt to establish the session
        // channel for the given DLCI. If this fails, reply with a DM response for the `dlci`.
        match dlci.validate(self.role()) {
            Err(e) => {
                warn!("Received SABM with invalid DLCI: {:?}", e);
                Frame::make_dm_response(self.role(), dlci)
            }
            Ok(_) => {
                if self.establish_session_channel(dlci).await {
                    Frame::make_ua_response(self.role(), dlci)
                } else {
                    Frame::make_dm_response(self.role(), dlci)
                }
            }
        }
    }

    /// Handles a multiplexer command over the Mux Control DLCI. Returns a response Frame
    /// or an Error if the command cannot be handled for any reason.
    fn handle_mux_command(&mut self, mux_command: &MuxCommand) -> Result<Frame, RfcommError> {
        trace!("Handling MuxCommand: {:?}", mux_command);
        // TODO(fxbug.dev/59585): Handle response frames when Initiator role is supported.
        if mux_command.command_response == CommandResponse::Response {
            trace!("Received MuxCommand response: {:?}", mux_command);
            return Err(RfcommError::NotImplemented);
        }

        match &mux_command.params {
            MuxCommandParams::ParameterNegotiation(pn_command) => {
                if !pn_command.dlci.is_user() {
                    warn!("Received PN command over invalid DLCI: {:?}", pn_command.dlci);
                    let dm_response = Frame::make_dm_response(self.role(), pn_command.dlci);
                    return Ok(dm_response);
                }

                // Update the session-specific parameters.
                let requested_parameters = SessionParameters {
                    credit_based_flow: pn_command.credit_based_flow_handshake
                        == CreditBasedFlowHandshake::SupportedRequest,
                    max_frame_size: usize::from(pn_command.max_frame_size),
                };
                let updated_parameters =
                    self.multiplexer().negotiate_parameters(requested_parameters);

                // Reserve the DLCI if it doesn't exist.
                self.multiplexer().find_or_create_session_channel(pn_command.dlci);
                // TODO(fxbug.dev/58668): Modify the reserved channel with the parsed credits when
                // credit-based flow control is implemented.

                // Reply back with the negotiated parameters as a response - most parameters
                // are simply echoed. Only credit-based flow control and max frame size are
                // negotiated in this implementation.
                let mut pn_response = pn_command.clone();
                pn_response.credit_based_flow_handshake = if updated_parameters.credit_based_flow {
                    CreditBasedFlowHandshake::SupportedResponse
                } else {
                    CreditBasedFlowHandshake::Unsupported
                };
                pn_response.max_frame_size = updated_parameters.max_frame_size as u16;
                let mux_response = MuxCommand {
                    params: MuxCommandParams::ParameterNegotiation(pn_response),
                    command_response: CommandResponse::Response,
                };
                Ok(Frame::make_mux_command_response(self.role(), mux_response))
            }
            command_type => {
                trace!("Received unsupported Mux Command: {:?}", command_type);
                Err(RfcommError::NotImplemented)
            }
        }
    }

    /// Handles an incoming Frame received from the peer. Returns a response
    /// Frame to be sent, or an error if the Frame couldn't be processed.
    async fn handle_frame(&mut self, frame: Frame) -> Result<Frame, RfcommError> {
        match frame.data {
            FrameData::SetAsynchronousBalancedMode => {
                Ok(self.handle_sabm_command(frame.dlci).await)
            }
            FrameData::UnnumberedAcknowledgement | FrameData::DisconnectedMode => {
                // TODO(fxbug.dev/59585): Handle UA and DM responses when the initiator role is
                // supported.
                Err(RfcommError::NotImplemented)
            }
            FrameData::Disconnect => {
                // TODO(fxbug.dev/59940): Implement Session cleanup. This depends on the DLCI.
                Err(RfcommError::NotImplemented)
            }
            FrameData::UnnumberedInfoHeaderCheck(UIHData::Mux(data)) => {
                self.handle_mux_command(&data)
            }
            FrameData::UnnumberedInfoHeaderCheck(UIHData::User(_)) => {
                // TODO(fxbug.dev/59942): Handle user data frames and relay to the appropriate
                // multiplexed `SessionChannel`.
                Err(RfcommError::NotImplemented)
            }
        }
    }

    /// Starts the processing task for this RFCOMM Session.
    /// `data_receiver` is a stream of incoming packets from the remote peer.
    /// `frame_sender` is used to relay Frames to the remote peer.
    ///
    /// The lifetime of this task is tied to the `data_receiver`.
    async fn run(
        mut self,
        mut data_receiver: mpsc::Receiver<Vec<u8>>,
        mut frame_sender: mpsc::Sender<Frame>,
    ) -> Result<(), Error> {
        loop {
            select! {
                incoming_bytes = data_receiver.next() => {
                    let bytes = match incoming_bytes {
                        Some(bytes) => bytes,
                        None => {
                            // The `data_receiver` has closed, indicating peer disconnection.
                            return Ok(());
                        }
                    };

                    match Frame::parse(self.role().opposite_role(), self.credit_based_flow(), &bytes[..]) {
                        Ok(f) => {
                            trace!("Parsed frame from peer: {:?}", f);
                            match self.handle_frame(f).await {
                                Ok(response) => {
                                    // Result of this send doesn't matter since failure indicates
                                    // peer disconnection.
                                    let _ = frame_sender.send(response).await;
                                }
                                Err(e) => warn!("Error handling RFCOMM frame: {:?}", e),
                            }
                        },
                        Err(e) => {
                            // TODO(fxbug.dev/60106): In the case that frame parsing fails due to an
                            // unidentified MuxCommand type, we should send a NonSupportedCommand
                            // response.
                            error!("Error parsing frame: {:?}", e);
                        }
                    };
                }
                complete => { return Ok(()); }
            }
        }
    }
}

/// An RFCOMM Session that multiplexes multiple channels over a single L2CAP channel.
///
/// A `Session` is represented by a processing task which processes incoming bytes
/// from the remote peer. Any multiplexed RFCOMM channels will be delivered to the
/// `clients` of the Session.
pub struct Session {
    task: fasync::Task<()>,
}

impl Session {
    /// Creates a new RFCOMM Session with peer `id` over the `l2cap_channel`. Any multiplexed
    /// RFCOMM channels will be relayed using the `channel_opened_callback`.
    pub fn create(
        id: PeerId,
        l2cap_channel: Channel,
        channel_opened_callback: ChannelOpenedFn,
    ) -> Self {
        let task =
            fasync::Task::spawn(Session::session_task(id, l2cap_channel, channel_opened_callback));
        Self { task }
    }

    /// Processing task that drives the work for an RFCOMM Session with a peer.
    ///
    /// 1) Drives the RFCOMM SessionInner task - this task is responsible for
    ///    RFCOMM related functionality: parsing & handling frames, modifying internal state, and
    ///    multiplexing RFCOMM channels.
    /// 2) Drives the peer processing task which handles incoming packets from the `l2cap_channel`.
    ///    This task also handles the sending of outgoing frames to the remote peer.
    ///
    /// The lifetime of this task is tied to the provided `l2cap` channel. When the remote peer
    /// disconnects, the `l2cap` channel will close, and therefore the task will terminate.
    async fn session_task(id: PeerId, l2cap: Channel, channel_opened_callback: ChannelOpenedFn) {
        // The `session_inner_task` communicates with the `peer_processing_task` using two mpsc
        // channels.

        // The `peer_processing_task` relays incoming packets from the remote peer to the
        // `session_inner_task` using this channel.
        let (data_sender, data_receiver) = mpsc::channel(0);

        // The `session_inner_task` relays outgoing packets (to be sent to the remote peer) to the
        // `peer_processing_task` using this channel.
        let (frame_sender, frame_receiver) = mpsc::channel(0);

        // Processes packets of data to/from the remote peer.
        let peer_processing_task =
            Session::peer_processing_task(l2cap, frame_receiver, data_sender).boxed().fuse();
        // Business logic of the RFCOMM session - parsing and handling frames, modifying the state
        // of the session, and multiplexing RFCOMM channels.
        let session_inner_task =
            SessionInner::create(data_receiver, frame_sender, channel_opened_callback)
                .boxed()
                .fuse();

        let _ = futures::future::select(session_inner_task, peer_processing_task).await;
        info!("Session with peer {:?} ended", id);
    }

    /// Processes incoming data from the `l2cap_channel` with the remote peer and
    /// relays it using the `data_sender`.
    /// Processes frames-to-be-sent from the `pending_writes` queue and sends them to the
    /// remote peer.
    async fn peer_processing_task(
        mut l2cap_channel: Channel,
        mut pending_writes: mpsc::Receiver<Frame>,
        mut data_sender: mpsc::Sender<Vec<u8>>,
    ) {
        loop {
            select! {
                incoming_bytes = l2cap_channel.next().fuse() => {
                    match incoming_bytes {
                        Some(Ok(bytes)) => {
                            trace!("Received packet from peer: {:?}", bytes);
                            if let Err(e) = data_sender.send(bytes).await {
                                error!("Couldn't send bytes to main run task");
                            }
                        },
                        Some(Err(e)) => {
                            error!("Error reading bytes from l2cap channel: {:?}", e);
                        },
                        None => {
                            info!("Peer closed L2CAP connection, exiting");
                            return;
                        }
                    }
                }
                frame_to_be_written = pending_writes.select_next_some() => {
                    trace!("Sending frame to remote: {:?}", frame_to_be_written);
                    let mut buf = vec![0; frame_to_be_written.encoded_len()];
                    if let Err(e) = frame_to_be_written.encode(&mut buf[..]) {
                        error!("Couldn't encode frame: {:?}", e);
                        continue;
                    }
                    // The result of this send is irrelevant, as failure would
                    // indicate peer disconnection.
                    let _ = l2cap_channel.as_ref().write(&buf);
                }
                complete => { return; }
            }
        }
    }

    /// Returns true if the Session is currently active - namely, it's processing `task` is
    /// still active.
    pub fn is_active(&mut self) -> bool {
        // The usage of `noop_waker_ref` is contingent on the `task` not being polled
        // elsewhere.
        // Each RFCOMM Session is stored as a spawned fasync::Task which runs independently.
        // The `task` itself is never polled directly anywhere else as there is no need to
        // drive it to completion. Thus, `is_active()` is the only location in which
        // the `task` is polled to determine if the RFCOMM Session processing task is ready
        // or not.
        let mut ctx = Context::from_waker(noop_waker_ref());
        return self.task.poll_unpin(&mut ctx).is_pending();
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use fuchsia_async as fasync;
    use futures::{lock::Mutex, pin_mut, task::Poll, Future};
    use std::{convert::TryFrom, sync::Arc};

    use crate::rfcomm::frame::mux_commands::*;

    pub(crate) fn make_sabm_command(role: Role, dlci: DLCI) -> Frame {
        Frame {
            role,
            dlci,
            data: FrameData::SetAsynchronousBalancedMode,
            poll_final: true,
            command_response: CommandResponse::Command,
            credits: None,
        }
    }

    /// Makes a DLC PN frame with arbitrary command parameters.
    /// `command_response` indicates whether the frame should be a command or response.
    /// `credit_flow` indicates whether credit-based flow control should be set or not.
    /// `max_frame_size` indicates the max frame size to use for the PN.
    fn make_dlc_pn_frame(
        command_response: CommandResponse,
        credit_flow: bool,
        max_frame_size: u16,
    ) -> Frame {
        let credit_based_flow_handshake = match (credit_flow, command_response) {
            (false, _) => CreditBasedFlowHandshake::Unsupported,
            (true, CommandResponse::Command) => CreditBasedFlowHandshake::SupportedRequest,
            (true, CommandResponse::Response) => CreditBasedFlowHandshake::SupportedResponse,
        };
        let pn_command = MuxCommand {
            params: MuxCommandParams::ParameterNegotiation(ParameterNegotiationParams {
                dlci: DLCI::try_from(3).unwrap(), // Random User DLCI
                credit_based_flow_handshake,
                priority: 12,
                max_frame_size,
                initial_credits: 3,
            }),
            command_response,
        };
        Frame {
            role: Role::Initiator,
            dlci: DLCI::MUX_CONTROL_DLCI,
            data: FrameData::UnnumberedInfoHeaderCheck(UIHData::Mux(pn_command)),
            poll_final: false,
            command_response,
            credits: None,
        }
    }

    /// Creates and returns the SessionInner processing task. Uses a channel_opened_fn that
    /// indiscriminately accepts all opened RFCOMM channels.
    fn setup_session_task() -> (impl Future<Output = ()>, Channel) {
        let (local, remote) = Channel::create();
        let channel_opened_fn = Box::new(|_server_channel, _channel| async { Ok(()) }.boxed());
        let session_fut = Session::session_task(PeerId(1), local, channel_opened_fn);
        (session_fut, remote)
    }

    /// Creates and returns a new SessionInner - the channel_opened_fn will indiscriminately accept
    /// all opened RFCOMM channels.
    fn setup_session() -> SessionInner {
        let channel_opened_fn = Box::new(|_server_channel, _channel| async { Ok(()) }.boxed());
        let session = SessionInner { multiplexer: SessionMultiplexer::create(), channel_opened_fn };
        session
    }

    #[test]
    fn test_register_l2cap_channel() {
        let mut exec = fasync::Executor::new().unwrap();

        let (processing_fut, remote) = setup_session_task();
        pin_mut!(processing_fut);
        assert!(exec.run_until_stalled(&mut processing_fut).is_pending());

        drop(remote);
        assert!(exec.run_until_stalled(&mut processing_fut).is_ready());
    }

    #[test]
    fn test_receiving_data_is_ok() {
        let mut exec = fasync::Executor::new().unwrap();

        let (processing_fut, remote) = setup_session_task();
        pin_mut!(processing_fut);
        assert!(exec.run_until_stalled(&mut processing_fut).is_pending());

        // Remote sends us some data. Even though this is an invalid Frame,
        // the `processing_fut` should still be OK.
        let frame_bytes = [0x03, 0x3F, 0x01, 0x1C];
        remote.as_ref().write(&frame_bytes[..]).expect("Should send");

        assert!(exec.run_until_stalled(&mut processing_fut).is_pending());
    }

    #[test]
    fn test_receiving_user_sabm_before_mux_startup_is_rejected() {
        let mut exec = fasync::Executor::new().unwrap();

        let mut session = setup_session();
        assert_eq!(session.role(), Role::Unassigned);

        let sabm = make_sabm_command(Role::Initiator, DLCI::try_from(3).unwrap());
        let mut handle_fut = Box::pin(session.handle_frame(sabm));

        // Expect a DM response due to user DLCI SABM before Mux DLCI SABM.
        match exec.run_until_stalled(&mut handle_fut) {
            Poll::Ready(Ok(frame)) => {
                assert_eq!(frame.data, FrameData::DisconnectedMode);
            }
            x => panic!("Expected a frame but got {:?}", x),
        }
    }

    #[test]
    fn test_receiving_mux_sabm_starts_multiplexer_with_ua_response() {
        let mut exec = fasync::Executor::new().unwrap();

        let mut session = setup_session();

        {
            // Remote sends us an SABM command.
            let sabm = make_sabm_command(Role::Unassigned, DLCI::MUX_CONTROL_DLCI);
            let mut handle_fut = Box::pin(session.handle_frame(sabm));
            // We expect to respond with a UA.
            match exec.run_until_stalled(&mut handle_fut) {
                Poll::Ready(Ok(frame)) => {
                    assert_eq!(frame.data, FrameData::UnnumberedAcknowledgement);
                }
                x => panic!("Expected a frame but got {:?}", x),
            }
        }

        // The multiplexer for this session should be started and assume the Responder role.
        assert!(session.multiplexer().started());
        assert_eq!(session.role(), Role::Responder);
    }

    #[test]
    fn test_receiving_mux_sabm_after_mux_startup_is_rejected() {
        let mut exec = fasync::Executor::new().unwrap();

        let mut session = setup_session();
        assert!(session.multiplexer().start(Role::Responder).is_ok());

        let sabm = make_sabm_command(Role::Initiator, DLCI::MUX_CONTROL_DLCI);
        let mut handle_fut = Box::pin(session.handle_frame(sabm));

        // Expect a DM response due to Mux control SABM being sent after multiplexer already
        // started.
        match exec.run_until_stalled(&mut handle_fut) {
            Poll::Ready(Ok(frame)) => {
                assert_eq!(frame.data, FrameData::DisconnectedMode);
            }
            x => panic!("Expected a frame but got {:?}", x),
        }
    }

    #[test]
    fn test_receiving_multiple_pn_commands_results_in_set_parameters() {
        let mut exec = fasync::Executor::new().unwrap();

        let mut session = setup_session();
        assert!(!session.session_parameters_negotiated());
        assert!(session.multiplexer().start(Role::Responder).is_ok());

        // Remote initiates DLCPN.
        {
            let dlcpn = make_dlc_pn_frame(CommandResponse::Command, false, 64);
            let mut handle_fut = Box::pin(session.handle_frame(dlcpn));

            // Expect a DLCPN response.
            let expected_frame = make_dlc_pn_frame(CommandResponse::Response, false, 64);
            match exec.run_until_stalled(&mut handle_fut) {
                Poll::Ready(Ok(frame)) => {
                    assert_eq!(frame.data, expected_frame.data);
                }
                x => panic!("Expected a frame but got {:?}", x),
            }
        }

        // The global session parameters should be set.
        let expected_parameters =
            SessionParameters { credit_based_flow: false, max_frame_size: 64 };
        assert_eq!(session.session_parameters(), expected_parameters);

        // Multiple DLC PN requests before a DLC is established is OK - new parameters.
        {
            let dlcpn = make_dlc_pn_frame(CommandResponse::Command, true, 11);
            let mut handle_fut = Box::pin(session.handle_frame(dlcpn));
            assert!(exec.run_until_stalled(&mut handle_fut).is_ready());
        }

        // The global session parameters should be updated.
        let expected_parameters = SessionParameters { credit_based_flow: true, max_frame_size: 11 };
        assert_eq!(session.session_parameters(), expected_parameters);
    }

    #[test]
    fn test_dlcpn_renegotiation_does_not_update_parameters() {
        let mut exec = fasync::Executor::new().unwrap();

        let mut session = setup_session();
        assert!(session.multiplexer().start(Role::Responder).is_ok());

        // Remote peer initiates DLCPN.
        {
            let dlcpn = make_dlc_pn_frame(CommandResponse::Command, true, 100);
            let mut handle_fut = Box::pin(session.handle_frame(dlcpn));

            // Expect a DLCPN response.
            let expected_frame = make_dlc_pn_frame(CommandResponse::Response, true, 100);
            match exec.run_until_stalled(&mut handle_fut) {
                Poll::Ready(Ok(frame)) => {
                    assert_eq!(frame.data, expected_frame.data);
                }
                x => panic!("Expected a frame but got {:?}", x),
            }
        }

        // The global session parameters should be set.
        let expected_parameters =
            SessionParameters { credit_based_flow: true, max_frame_size: 100 };
        assert_eq!(session.session_parameters(), expected_parameters);

        // Remote peer sends SABM over a user DLCI - this will establish the DLCI.
        let generic_dlci = 6;
        let user_dlci = DLCI::try_from(generic_dlci).unwrap();
        {
            let user_sabm = make_sabm_command(Role::Initiator, user_dlci);
            let mut handle_fut = Box::pin(session.handle_frame(user_sabm));
            // Expect a request to send a positive UA response frame since we have a
            // profile client registered.
            match exec.run_until_stalled(&mut handle_fut) {
                Poll::Ready(Ok(frame)) => {
                    assert_eq!(frame.data, FrameData::UnnumberedAcknowledgement);
                    assert_eq!(frame.dlci, user_dlci);
                }
                x => panic!("Expected a frame but got {:?}", x),
            }
        }

        // There should be an established DLC.
        assert!(session.multiplexer().dlc_established());

        // Remote tries to re-negotiate the session parameters.
        {
            let dlcpn = make_dlc_pn_frame(CommandResponse::Command, true, 90);
            let mut handle_fut = Box::pin(session.handle_frame(dlcpn));
            assert!(exec.run_until_stalled(&mut handle_fut).is_ready());
        }
        // The global session parameters should not be updated since the first DLC has
        // already been established.
        assert_eq!(session.session_parameters(), expected_parameters);
    }

    #[test]
    fn test_establish_dlci_request_relays_channel_to_channel_open_fn() {
        let mut exec = fasync::Executor::new().unwrap();

        // Create the session.
        let mut session = setup_session();

        // Use a channel_open_fn that increments a shared `count` every time it is used.
        let count: Arc<Mutex<usize>> = Arc::new(Mutex::new(0));
        let count_clone = count.clone();
        session.channel_opened_fn = Box::new(move |_server_channel, _channel| {
            let count_local = count_clone.clone();
            async move {
                let mut w_count = count_local.lock().await;
                *w_count += 1;
                Ok(())
            }
            .boxed()
        });
        assert!(session.multiplexer().start(Role::Responder).is_ok());

        // Remote peer sends SABM over a user DLCI - we expect a UA response.
        let random_dlci = 8;
        let user_dlci = DLCI::try_from(random_dlci).unwrap();
        {
            let user_sabm = make_sabm_command(Role::Initiator, user_dlci);
            let mut handle_fut = Box::pin(session.handle_frame(user_sabm));
            match exec.run_until_stalled(&mut handle_fut) {
                Poll::Ready(Ok(frame)) => {
                    assert_eq!(frame.data, FrameData::UnnumberedAcknowledgement);
                }
                x => panic!("Expected a frame but got {:?}", x),
            }
        }
        // We expect the `channel_opened_fn` to be called once.
        let mut read_fut = Box::pin(count.lock());
        let res = exec.run_singlethreaded(&mut read_fut);
        assert_eq!(*res, 1);
    }

    #[test]
    fn test_no_registered_clients_rejects_establish_dlci_request() {
        let mut exec = fasync::Executor::new().unwrap();

        // Create the session - set the channel_send_fn to unanimously reject
        // channels, to simulate failure.
        let mut session = setup_session();
        session.channel_opened_fn = Box::new(|_server_channel, _channel| {
            async { Err(format_err!("Always rejecting")) }.boxed()
        });
        assert!(session.multiplexer().start(Role::Responder).is_ok());

        // Remote peer sends SABM over a user DLCI - this should be rejected with a
        // DM response frame because channel delivery failed.
        let user_dlci = DLCI::try_from(6).unwrap();
        {
            let user_sabm = make_sabm_command(Role::Initiator, user_dlci);
            let mut handle_fut = Box::pin(session.handle_frame(user_sabm));
            match exec.run_until_stalled(&mut handle_fut) {
                Poll::Ready(Ok(frame)) => {
                    assert_eq!(frame.data, FrameData::DisconnectedMode);
                }
                x => panic!("Expected a frame but got {:?}", x),
            }
        }
    }
}

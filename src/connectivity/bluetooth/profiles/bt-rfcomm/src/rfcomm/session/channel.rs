// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::Channel,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, IValue, Inspect},
    futures::{
        channel::{mpsc, oneshot},
        future::{BoxFuture, Shared},
        select, FutureExt, SinkExt, StreamExt,
    },
    log::{error, info, trace},
    std::convert::TryInto,
};

use crate::rfcomm::{
    frame::{Frame, UserData},
    inspect::{DuplexDataStreamInspect, SessionChannelInspect, FLOW_CONTROLLER},
    types::{RfcommError, Role, SignaledTask, DLCI},
};

/// Upper bound for the number of credits we allow a remote device to have.
/// This is used to determine the number of credits to include in frames sent to the peer
/// after space frees up in the local received frame buffer.
/// This value is arbitrarily chosen and not derived from the GSM or RFCOMM specifications.
const HIGH_CREDIT_WATER_MARK: usize = u8::MAX as usize;

/// Threshold indicating that the number of credits is low on a remote device.
/// This is used as an indicator to send an empty frame to replenish the remote's credits.
/// This value is arbitrarily chosen and not derived from the GSM or RFCOMM specifications.
const LOW_CREDIT_WATER_MARK: usize = 10;

/// Tracks the credits for each device connected to a given RFCOMM channel.
#[derive(Debug, Default, Inspect)]
pub struct Credits {
    /// The local credits that we currently have. Determines the number of frames
    /// we can send to the remote peer.
    #[inspect(rename = "local_credits")]
    local: IValue<usize>,
    /// The remote peer's current credit count. Determines the number of frames the
    /// remote may send to us.
    #[inspect(rename = "remote_credits")]
    remote: IValue<usize>,
}

impl Credits {
    pub fn new(local: usize, remote: usize) -> Self {
        Self { local: local.into(), remote: remote.into() }
    }

    /// Makes a copy of the current credit counts because IValue<T> does not derive Clone.
    fn as_new(&self) -> Self {
        Self::new(*self.local, *self.remote)
    }

    pub fn local(&self) -> usize {
        *self.local
    }

    pub fn remote(&self) -> usize {
        *self.remote
    }

    /// Returns the number of credits to replenish the remote with. Returns None if the remote
    /// has sufficient credits and does not need any more.
    fn remote_replenish_amount(&self) -> Option<std::num::NonZeroU8> {
        HIGH_CREDIT_WATER_MARK
            .checked_sub(*self.remote)
            .map(|c| c.try_into().unwrap())
            .and_then(std::num::NonZeroU8::new)
    }

    /// Increases the local credit amount by `credits`.
    fn replenish_local(&mut self, credits: usize) {
        self.local.iset(*self.local + credits);
    }

    /// Increases the remote credit amount by `credits`.
    fn replenish_remote(&mut self, credits: usize) {
        self.remote.iset(*self.remote + credits);
    }

    /// Reduces the local credit count by 1.
    fn decrement_local(&mut self) {
        self.local.iset(self.local.checked_sub(1).unwrap_or(0));
    }

    /// Reduces the remote credit count by 1. Returns true if the remote credit count is
    /// low, false otherwise.
    fn decrement_remote(&mut self) -> bool {
        self.remote.iset(self.remote.checked_sub(1).unwrap_or(0));
        *self.remote <= LOW_CREDIT_WATER_MARK
    }
}

/// Represents an interface to manage the flow control of an RFCOMM channel between a remote peer
/// and a local RFCOMM client.
#[async_trait]
trait FlowController {
    /// Send `data` received from the local RFCOMM client to the remote peer.
    async fn send_data_to_peer(&mut self, data: UserData);

    /// Receive `data` from the remote peer to be relayed to the provided RFCOMM `client`.
    async fn receive_data_from_peer(&mut self, data: FlowControlledData, client: &fidl::Socket);
}

/// A flow controller that indiscriminantly relays frames between a remote peer and an RFCOMM
/// requesting client. There is no flow control.
struct SimpleController {
    role: Role,
    dlci: DLCI,
    /// Used to send frames to the remote peer.
    outgoing_sender: mpsc::Sender<Frame>,
    /// Inspect object for tracking total bytes sent/received & the current transfer speeds.
    stream_inspect: DuplexDataStreamInspect,
    /// The inspect node for this controller.
    inspect: inspect::Node,
}

impl Inspect for &mut SimpleController {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect = parent.create_child(name);
        self.inspect.record_string("controller_type", "simple");
        self.stream_inspect.iattach(&self.inspect, "data_stream")?;
        self.stream_inspect.start();
        Ok(())
    }
}

impl SimpleController {
    fn new(role: Role, dlci: DLCI, outgoing_sender: mpsc::Sender<Frame>) -> Self {
        Self {
            role,
            dlci,
            outgoing_sender,
            stream_inspect: DuplexDataStreamInspect::default(),
            inspect: inspect::Node::default(),
        }
    }
}

#[async_trait]
impl FlowController for SimpleController {
    async fn send_data_to_peer(&mut self, data: UserData) {
        let num_bytes = data.information.len();
        let user_data_frame = Frame::make_user_data_frame(self.role, self.dlci, data, None);
        if self.outgoing_sender.send(user_data_frame).await.is_ok() {
            self.stream_inspect.record_outbound_transfer(num_bytes, fasync::Time::now());
        };
    }

    async fn receive_data_from_peer(&mut self, data: FlowControlledData, client: &fidl::Socket) {
        if let Ok(num_bytes) = client.write(&data.user_data.information[..]) {
            self.stream_inspect.record_inbound_transfer(num_bytes, fasync::Time::now());
        }
    }
}

/// Handles the credit-based flow control scheme defined in RFCOMM 6.5.
///
/// Provides an API to send and receive frames between the remote peer and a local
/// RFCOMM client. Uses a credit based system to control the flow of data. In general,
/// when credit-based flow control is in use, each frame contains an extra credit octet,
/// signaling the amount of credits the sending device is "giving" to the receiving device.
///
/// When the remote's credit count is low (i.e the remote will soon need more credits
/// before they can send frames), we preemptively send an empty user-data frame with a
/// replenished credit amount. See the last paragraph of RFCOMM 6.5 which states:
/// "...always allowed to send frames containing no user data (length field = 0)". This is
/// the mechanism for replenishing the credit count.
struct CreditFlowController {
    role: Role,
    dlci: DLCI,
    /// Used to send frames to the remote peer.
    outgoing_sender: mpsc::Sender<Frame>,
    /// Queued user data awaiting local credits.
    outgoing_data_pending_credits: Vec<UserData>,
    /// The current credit count for this controller.
    credits: Credits,
    /// Inspect object for tracking total bytes sent/received & the current transfer speeds.
    stream_inspect: DuplexDataStreamInspect,
    /// The inspect node for this controller.
    inspect: inspect::Node,
}

impl Inspect for &mut CreditFlowController {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect = parent.create_child(name);
        self.inspect.record_string("controller_type", "credit_flow");
        self.credits.iattach(&self.inspect, "credits")?;
        self.stream_inspect.iattach(&self.inspect, "data_stream")?;
        self.stream_inspect.start();
        Ok(())
    }
}

impl CreditFlowController {
    fn new(
        role: Role,
        dlci: DLCI,
        outgoing_sender: mpsc::Sender<Frame>,
        initial_credits: Credits,
    ) -> Self {
        trace!("Creating credit flow controller with initial credits: {:?}", initial_credits);
        Self {
            role,
            dlci,
            outgoing_sender,
            outgoing_data_pending_credits: Vec::new(),
            credits: initial_credits,
            stream_inspect: DuplexDataStreamInspect::default(),
            inspect: inspect::Node::default(),
        }
    }

    /// Sends the `user_data` directly to the remote peer - queues the data to be sent
    /// later if there is an insufficient number of credits.
    async fn send_user_data(&mut self, user_data: UserData) {
        let data_size = user_data.information.len();
        if data_size != 0 && self.credits.local() == 0 {
            self.outgoing_data_pending_credits.push(user_data);
            return;
        }

        let credits_to_replenish =
            self.credits.remote_replenish_amount().map(std::num::NonZeroU8::get);
        let frame =
            Frame::make_user_data_frame(self.role, self.dlci, user_data, credits_to_replenish);
        if self.outgoing_sender.send(frame).await.is_ok() {
            if data_size != 0 {
                self.credits.decrement_local();
                self.stream_inspect.record_outbound_transfer(data_size, fasync::Time::now());
            }
            self.credits.replenish_remote(credits_to_replenish.unwrap_or(0) as usize);
        }
    }

    /// Handles receiving credits in a frame from the remote peer. If a nonzero number of credits
    /// were received, attempts to send any frames pending credits.
    async fn handle_received_credits(&mut self, received_credits: u8) {
        let need_to_send_queued = self.credits.local() == 0;
        self.credits.replenish_local(received_credits as usize);

        // Note: It's possible that the number of local credits is less than the number of
        // queued frames pending credits. This is OK. We will simply send up to the number of
        // available credits worth of frames, and queue the rest for later.
        if need_to_send_queued && self.credits.local() > 0 {
            let user_data_frames = std::mem::take(&mut self.outgoing_data_pending_credits);
            for user_data in user_data_frames {
                self.send_user_data(user_data).await;
            }
        }
    }
}

#[async_trait]
impl FlowController for CreditFlowController {
    async fn send_data_to_peer(&mut self, data: UserData) {
        self.send_user_data(data).await;
    }

    async fn receive_data_from_peer(&mut self, data: FlowControlledData, client: &fidl::Socket) {
        let FlowControlledData { user_data, credits } = data;
        self.handle_received_credits(credits.unwrap_or(0)).await;

        if !user_data.is_empty() {
            let _ = client.write(&user_data.information[..]);
            self.stream_inspect
                .record_inbound_transfer(user_data.information.len(), fasync::Time::now());

            if self.credits.decrement_remote() {
                trace!("Remote credit count is low. Sending empty frame");
                let _ = self.send_user_data(UserData { information: vec![] }).await;
            }
        }
    }
}

/// The flow control method for the channel.
#[derive(Debug)]
pub enum FlowControlMode {
    /// Credit-based flow control with the provided initial credits.
    CreditBased(Credits),
    /// No flow control is being used.
    None,
}

impl FlowControlMode {
    /// Makes a copy of the FlowControlMode.
    fn as_new(&self) -> Self {
        match self {
            Self::CreditBased(credits) => Self::CreditBased(credits.as_new()),
            Self::None => Self::None,
        }
    }
}

/// User data frames with optional credits to be used for flow control.
pub struct FlowControlledData {
    pub user_data: UserData,
    pub credits: Option<u8>,
}

/// The processing task associated with an established SessionChannel.
struct SessionChannelTask {
    /// The processing task.
    _task: fasync::Task<()>,
    /// The sender used to relay user data received from the peer to the `_task`.
    user_data_queue: mpsc::Sender<FlowControlledData>,
    /// Shared future indicating termination status of the `_task`.
    terminated: Shared<BoxFuture<'static, ()>>,
}

/// The RFCOMM Channel associated with a DLCI for an RFCOMM Session. This channel is a client-
/// interfacing channel - the remote end of the channel is held by a profile as an RFCOMM channel.
///
/// Typical usage looks like:
///   let (local, remote) = Channel::create();
///   let session_channel = SessionChannel::new(role, dlci);
///   session_channel.establish(local, frame_sender);
///   // Give remote to some client requesting RFCOMM.
///   pass_channel_to_rfcomm_client(remote);
///
///   let _ = session_channel.receive_user_data(FlowControlledData { user_data_buf1, credits1 }).await;
///   let _ = session_channel.receive_user_data(FlowControlledData { user_data_buf2, credits2 }).await;
pub struct SessionChannel {
    /// The DLCI associated with this channel.
    dlci: DLCI,
    /// The local role assigned to us.
    role: Role,
    /// The flow control used for this SessionChannel. If unset during establishment, the channel
    /// will default to credit-based flow control.
    flow_control: Option<FlowControlMode>,
    /// The processing task associated with the channel. This is set through
    /// `self.establish()`, and indicates whether this SessionChannel is
    /// currently active.
    processing_task: Option<SessionChannelTask>,
    /// The inspect node for this channel.
    inspect: SessionChannelInspect,
}

impl Inspect for &mut SessionChannel {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect.iattach(parent, name)?;
        self.inspect.set_dlci(self.dlci);
        Ok(())
    }
}

impl SessionChannel {
    pub fn new(dlci: DLCI, role: Role) -> Self {
        Self {
            dlci,
            role,
            flow_control: None,
            processing_task: None,
            inspect: SessionChannelInspect::default(),
        }
    }

    /// Returns true if this SessionChannel has been established. Namely, `self.establish()`
    /// has been called there is an active processing task.
    pub fn is_established(&self) -> bool {
        self.finished().now_or_never().is_none()
    }

    /// Returns true if the parameters for this SessionChannel have been negotiated.
    pub fn parameters_negotiated(&self) -> bool {
        self.flow_control.is_some()
    }

    /// Sets the flow control mode for this channel. Returns an Error if the channel
    /// has already been established, as the flow control mode cannot be changed after
    /// the channel has been opened and established.
    ///
    /// Note: It is valid to call `set_flow_control()` multiple times before the
    /// channel has been established (usually due to back and forth during parameter negotiation).
    /// The most recent `flow_control` will be used.
    pub fn set_flow_control(&mut self, flow_control: FlowControlMode) -> Result<(), RfcommError> {
        if self.is_established() {
            return Err(RfcommError::ChannelAlreadyEstablished(self.dlci));
        }
        self.flow_control = Some(flow_control.as_new());
        self.inspect.set_flow_control(flow_control);
        Ok(())
    }

    /// A user data relay task that reads data from the `channel` and relays to the
    /// `frame_sender`.
    /// The task also processes user data received from the peer in the `pending_writes` queue
    /// and sends it to the `channel`.
    /// The user data relay is flow controlled by the provided `flow_control` mode.
    async fn user_data_relay(
        dlci: DLCI,
        role: Role,
        termination_sender: oneshot::Sender<()>,
        mut flow_controller: Box<dyn FlowController>,
        mut channel: Channel,
        mut pending_writes: mpsc::Receiver<FlowControlledData>,
        mut frame_sender: mpsc::Sender<Frame>,
    ) {
        loop {
            select! {
                // The fuse() is within the loop because `channel` is both borrowed in its stream
                // form, and in the receive_data_from_peer() call. This fuse() is OK because when
                // the stream terminates, `data_from_client` will be None, and we will break
                // out of the loop - `channel.next()` will never be polled again.
                data_from_client = channel.next().fuse() => {
                    match data_from_client {
                        Some(Ok(bytes)) => {
                            trace!("Sending user-data packet for DLCI {:?}: {:?}", dlci, bytes);
                            let user_data = UserData { information: bytes };
                            flow_controller.send_data_to_peer(user_data).await;
                        }
                        Some(Err(e)) => {
                            error!("Error receiving data from client {:?}", e);
                        }
                        None => {
                            trace!("RFCOMM channel closed by profile");
                            // The client has disconnected the RFCOMM channel. We should relay
                            // a Disconnect frame to the remote peer.
                            let disc = Frame::make_disc_command(role, dlci);
                            // The result of the send is irrelevant, as failure would indicate
                            // the Session is closed.
                            let _ = frame_sender.send(disc).await;
                            break;
                        }
                    };
                },
                data_from_peer = pending_writes.select_next_some() => {
                    flow_controller.receive_data_from_peer(data_from_peer, channel.as_ref()).await;
                },
                complete => break,
            }
        }
        info!("Profile-client processing task for DLCI {:?} finished", dlci);
        // Client closed the `channel` - notify listener of termination.
        let _ = termination_sender.send(());
    }

    /// Starts the processing task over the provided `channel`. The processing task will:
    /// 1) Read bytes received from the `channel` and relay user data to the `frame_sender`.
    /// 2) Read packets from the `pending_writes` queue, and send the data to the client
    ///    end of the `channel`.
    ///
    /// While unusual, it is OK to call establish() multiple times. The currently active
    /// processing task will be dropped, and a new one will be spawned using the provided
    /// new channel. If the flow control has not been negotiated, the channel will default
    /// to using credit-based flow control.
    pub fn establish(&mut self, channel: Channel, frame_sender: mpsc::Sender<Frame>) {
        let (user_data_queue, pending_writes) = mpsc::channel(0);
        let flow_controller: Box<dyn FlowController> =
            match self.flow_control.get_or_insert(FlowControlMode::CreditBased(Credits::default()))
            {
                FlowControlMode::CreditBased(credits) => {
                    let mut controller = CreditFlowController::new(
                        self.role,
                        self.dlci,
                        frame_sender.clone(),
                        credits.as_new(),
                    );
                    let _ = controller.iattach(self.inspect.node(), FLOW_CONTROLLER);
                    Box::new(controller)
                }
                FlowControlMode::None => {
                    let mut controller =
                        SimpleController::new(self.role, self.dlci, frame_sender.clone());
                    let _ = controller.iattach(self.inspect.node(), FLOW_CONTROLLER);
                    Box::new(controller)
                }
            };
        let (termination_sender, receiver) = oneshot::channel();
        let _task = fasync::Task::local(Self::user_data_relay(
            self.dlci,
            self.role,
            termination_sender,
            flow_controller,
            channel,
            pending_writes,
            frame_sender,
        ));
        let terminated = receiver.map(|_| ()).boxed().shared();
        self.processing_task = Some(SessionChannelTask { _task, user_data_queue, terminated });
        trace!("Established SessionChannel for DLCI {:?}", self.dlci);
    }

    /// Receive a user data payload from the remote peer, which will be queued to be sent
    /// to the local profile client.
    pub fn receive_user_data(&mut self, data: FlowControlledData) -> Result<(), RfcommError> {
        if !self.is_established() {
            return Err(RfcommError::ChannelNotEstablished(self.dlci));
        }

        // This unwrap is safe because the task is guaranteed to be set if the channel
        // has been established.
        let queue = &mut self.processing_task.as_mut().unwrap().user_data_queue;
        queue.try_send(data).map_err(|e| anyhow::format_err!("{:?}", e).into())
    }
}

impl SignaledTask for SessionChannel {
    fn finished(&self) -> BoxFuture<'static, ()> {
        self.processing_task.as_ref().map_or_else(
            || futures::future::ready(()).boxed(),
            |task| task.terminated.clone().boxed(),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async::DurationExt;
    use fuchsia_inspect_derive::WithInspect;
    use fuchsia_zircon::DurationNum;
    use futures::{channel::mpsc, pin_mut, task::Poll};
    use matches::assert_matches;
    use std::convert::TryFrom;

    use crate::rfcomm::frame::FrameData;
    use crate::rfcomm::test_util::{expect_frame, expect_pending, expect_user_data_frame};

    fn establish_channel(channel: &mut SessionChannel) -> (Channel, mpsc::Receiver<Frame>) {
        let (local, remote) = Channel::create();
        let (frame_sender, frame_receiver) = mpsc::channel(0);
        channel.establish(local, frame_sender);
        (remote, frame_receiver)
    }

    /// Creates and establishes a SessionChannel. If provided, sets the flow control mode to
    /// `flow_control`.
    /// Returns the SessionChannel, the client end of the fuchsia_bluetooth::Channel, and a frame
    /// receiver that can be used to verify outgoing (i.e to the remote peer) frames being sent
    /// correctly.
    fn create_and_establish(
        role: Role,
        dlci: DLCI,
        flow_control: Option<FlowControlMode>,
    ) -> (inspect::Inspector, SessionChannel, Channel, mpsc::Receiver<Frame>) {
        let inspect = inspect::Inspector::new();
        let mut session_channel =
            SessionChannel::new(dlci, role).with_inspect(inspect.root(), "channel_").unwrap();
        assert!(!session_channel.is_established());
        if let Some(flow_control) = flow_control {
            assert!(session_channel.set_flow_control(flow_control).is_ok());
        }

        let (remote, frame_receiver) = establish_channel(&mut session_channel);
        assert!(session_channel.is_established());

        (inspect, session_channel, remote, frame_receiver)
    }

    /// Creates and returns a CreditFlowController with the initial `credits`. Uses
    /// an executor set at the provided `time`.
    fn setup_credit_flow_controller(
        time: fasync::Time,
        credits: Credits,
    ) -> (fasync::Executor, inspect::Inspector, CreditFlowController, mpsc::Receiver<Frame>) {
        let exec = fasync::Executor::new_with_fake_time().unwrap();
        exec.set_fake_time(time);
        let inspect = inspect::Inspector::new();

        let (frame_sender, outgoing_frames) = mpsc::channel(0);
        let flow_controller = CreditFlowController::new(
            Role::Initiator,             // Random fixed Role.
            DLCI::try_from(19).unwrap(), // Random fixed user DLCI.
            frame_sender,
            credits,
        )
        .with_inspect(inspect.root(), FLOW_CONTROLLER)
        .unwrap();

        (exec, inspect, flow_controller, outgoing_frames)
    }

    #[test]
    fn test_establish_channel_and_send_data_with_no_flow_control() {
        let mut exec = fasync::Executor::new().unwrap();

        let role = Role::Responder;
        let dlci = DLCI::try_from(8).unwrap();
        let (_inspect, mut session_channel, mut client, mut outgoing_frames) =
            create_and_establish(role, dlci, Some(FlowControlMode::None));
        // Trying to change the flow control mode after establishment should fail.
        assert!(session_channel
            .set_flow_control(FlowControlMode::CreditBased(Credits::default()))
            .is_err());

        let data_received_by_client = client.next();
        pin_mut!(data_received_by_client);
        assert!(exec.run_until_stalled(&mut data_received_by_client).is_pending());

        expect_pending(&mut exec, &mut outgoing_frames);

        // Receive user data to be relayed to the profile-client - no credits.
        let user_data = UserData { information: vec![0x01, 0x02, 0x03] };
        assert!(session_channel
            .receive_user_data(FlowControlledData { user_data: user_data.clone(), credits: None })
            .is_ok());

        // Data should be relayed to the client.
        match exec.run_until_stalled(&mut data_received_by_client) {
            Poll::Ready(Some(Ok(buf))) => {
                assert_eq!(buf, user_data.information);
            }
            x => panic!("Expected ready with data but got: {:?}", x),
        }

        // Client responds with some user data.
        let client_data = vec![0xff, 0x00, 0xaa, 0x0bb];
        assert!(client.as_ref().write(&client_data).is_ok());

        // Data should be processed by the SessionChannel, packed into an RFCOMM frame, and relayed
        // to peer.
        expect_user_data_frame(
            &mut exec,
            &mut outgoing_frames,
            UserData { information: client_data },
            None, // No credits since no flow control.
        );

        // Profile client no longer needs the RFCOMM channel - expect a Disc frame.
        drop(client);
        expect_frame(&mut exec, &mut outgoing_frames, FrameData::Disconnect, Some(dlci));
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(!session_channel.is_established());
    }

    /// Tests the SessionChannel relay with default flow control parameters (credit-based
    /// flow control with 0 initial credits). This is a fairly common case since we only
    /// do PN once, so most RFCOMM channels that are established will not have negotiated
    /// credits.
    #[test]
    fn test_session_channel_with_default_credit_flow_control() {
        let mut exec = fasync::Executor::new().unwrap();

        let role = Role::Responder;
        let dlci = DLCI::try_from(8).unwrap();
        let (_inspect, mut session_channel, mut client, mut outgoing_frames) =
            create_and_establish(role, dlci, None);

        {
            let mut data_received_by_client = Box::pin(client.next());
            assert!(exec.run_until_stalled(&mut data_received_by_client).is_pending());
        }

        expect_pending(&mut exec, &mut outgoing_frames);

        // Receive user data from remote peer be relayed to the profile-client - 0 credits issued.
        let user_data = UserData { information: vec![0x01, 0x02, 0x03] };
        assert!(session_channel
            .receive_user_data(FlowControlledData {
                user_data: user_data.clone(),
                credits: Some(0)
            })
            .is_ok());

        // Even though the remote's credit count is 0 (the default), the data should be relayed
        // to the client. However, we expect to immediately send an outgoing empty data frame
        // to replenish the remote's credits.
        {
            let mut data_received_by_client = Box::pin(client.next());
            assert!(exec.run_until_stalled(&mut data_received_by_client).is_ready());
            expect_user_data_frame(
                &mut exec,
                &mut outgoing_frames,
                UserData { information: vec![] },
                Some(HIGH_CREDIT_WATER_MARK as u8),
            );
        }

        // Client wants to send data to the remote peer. We have no local credits, so it should
        // be queued for later.
        let client_data = vec![0xaa, 0xcc];
        assert!(client.as_ref().write(&client_data).is_ok());
        expect_pending(&mut exec, &mut outgoing_frames);

        // Remote peer sends more user data with a refreshed credit amount.
        let user_data = UserData { information: vec![0x00, 0x00, 0x00, 0x00, 0x00] };
        assert!(session_channel
            .receive_user_data(FlowControlledData {
                user_data: user_data.clone(),
                credits: Some(50)
            })
            .is_ok());

        // The previously queued outgoing frame should be finally sent due to the credit refresh.
        // The received user data frame should be relayed to the client.
        expect_user_data_frame(
            &mut exec,
            &mut outgoing_frames,
            UserData { information: client_data },
            None, // Don't expect to replenish remote.
        );
        let mut data_received_by_client = Box::pin(client.next());
        assert!(exec.run_until_stalled(&mut data_received_by_client).is_ready());
    }

    #[test]
    fn test_session_channel_with_negotiated_credit_flow_control() {
        let mut exec = fasync::Executor::new().unwrap();

        let role = Role::Responder;
        let dlci = DLCI::try_from(8).unwrap();
        let (_inspect, mut session_channel, mut client, mut outgoing_frames) = create_and_establish(
            role,
            dlci,
            Some(FlowControlMode::CreditBased(Credits::new(
                12, // Arbitrary
                12, // Arbitrary
            ))),
        );

        let data_received_by_client = client.next();
        pin_mut!(data_received_by_client);
        assert!(exec.run_until_stalled(&mut data_received_by_client).is_pending());

        expect_pending(&mut exec, &mut outgoing_frames);

        // Receive user data to be relayed to the profile-client - random amount of credits.
        let user_data = UserData { information: vec![0x01, 0x02, 0x03] };
        assert!(session_channel
            .receive_user_data(FlowControlledData {
                user_data: user_data.clone(),
                credits: Some(6)
            })
            .is_ok());

        // Data should be relayed to the client.
        assert!(exec.run_until_stalled(&mut data_received_by_client).is_ready());

        // Client responds with some user data.
        let client_data = vec![0xff, 0x00, 0xaa];
        assert!(client.as_ref().write(&client_data).is_ok());

        // Data should be processed by the SessionChannel, packed into an RFCOMM frame, and relayed
        // using the `frame_sender`. There should be credits associated with the frame - we always
        // attempt to replenish with the (`HIGH_CREDIT_WATER_MARK` - remote_credits) amount.
        expect_user_data_frame(
            &mut exec,
            &mut outgoing_frames,
            UserData { information: client_data },
            Some(244), // = 255 (max) - (12 (initial) - 1 (sent frame))
        );
    }

    /// This test exercises the full credit-based flow control path.
    /// 1. Tests receiving user data from a remote peer is correctly received,
    /// credits updated, and sent to the local client.
    /// 2. Tests local client sending user data to the remote peer with insufficient
    /// credits - the frame should be queued for later.
    /// 3. Tests receiving an empty user data frame from the remote peer, replenishing our
    /// credits, and validates the frame from 2. is sent correctly.
    #[test]
    fn test_credit_flow_controller() {
        let initial_credits = Credits::new(2, 7);
        let (mut exec, _inspect, mut flow_controller, mut outgoing_frames) =
            setup_credit_flow_controller(
                /* initial_time= */ fasync::Time::from_nanos(0),
                initial_credits,
            );
        // The endpoints of the RFCOMM channel. The local end is managed by the RFCOMM
        // component, and the client end is relayed to the RFCOMM-requesting profile.
        let (local, mut client) = Channel::create();
        let mut data_received_by_client = Box::pin(client.next());

        // 1. Remote peer sends us user data - they do not provide any new credits.
        let data1 = UserData { information: vec![0xaa, 0xbb, 0xcc, 0xdd, 0xee] };
        {
            let mut receive_fut = Box::pin(flow_controller.receive_data_from_peer(
                FlowControlledData { user_data: data1, credits: Some(0) },
                local.as_ref(),
            ));
            assert!(exec.run_until_stalled(&mut receive_fut).is_pending());
            // The user data should be relayed to the RFCOMM client.
            assert!(exec.run_until_stalled(&mut data_received_by_client).is_ready());
            // Because the remote credit count is low (see `initial_credits`), we expect to
            // preemptively send an empty data frame to the peer to refresh their credits.
            expect_user_data_frame(
                &mut exec,
                &mut outgoing_frames,
                UserData { information: vec![] },
                Some(249), // 255 (Max) - (7 (initial) - 1 (received))
            );
            assert!(exec.run_until_stalled(&mut receive_fut).is_ready());
        }

        // 2a. RFCOMM client responds with its own data.
        let data2a = UserData { information: vec![0x45, 0x34] };
        {
            let mut send_fut = Box::pin(flow_controller.send_data_to_peer(data2a.clone()));
            assert!(exec.run_until_stalled(&mut send_fut).is_pending());
            // We have sufficient local credits (2), so frame should be sent. No credits to refresh.
            expect_user_data_frame(&mut exec, &mut outgoing_frames, data2a, None);
            assert!(exec.run_until_stalled(&mut send_fut).is_ready());
        }
        // 2b. RFCOMM client sends more data.
        let data2b = UserData { information: vec![0x99] };
        {
            let mut send_fut = Box::pin(flow_controller.send_data_to_peer(data2b.clone()));
            assert!(exec.run_until_stalled(&mut send_fut).is_pending());
            // We have sufficient local credits (1), so frame should be sent. No credits to refresh.
            expect_user_data_frame(&mut exec, &mut outgoing_frames, data2b, None);
            assert!(exec.run_until_stalled(&mut send_fut).is_ready());
        }

        // 2c. RFCOMM client sends more data.
        let data2c = UserData { information: vec![0xff, 0x00, 0xaa] };
        {
            let mut send_fut = Box::pin(flow_controller.send_data_to_peer(data2c.clone()));
            // Because we have no local credits, the data should be queued for later.
            assert!(exec.run_until_stalled(&mut send_fut).is_ready());
            // No frame should be sent to the peer.
            expect_pending(&mut exec, &mut outgoing_frames);
        }

        // 2d. RFCOMM client sends more data.
        let data2d = UserData { information: vec![0x04, 0x02, 0x00] };
        {
            let mut send_fut = Box::pin(flow_controller.send_data_to_peer(data2d.clone()));
            // Because we have no local credits, the data should be queued for later.
            assert!(exec.run_until_stalled(&mut send_fut).is_ready());
            // No frame should be sent to the peer.
            expect_pending(&mut exec, &mut outgoing_frames);
        }

        // 3. Remote peer sends an empty frame to "refresh" our credits.
        let data3 = UserData { information: vec![] };
        {
            let mut receive_fut = Box::pin(flow_controller.receive_data_from_peer(
                FlowControlledData { user_data: data3, credits: Some(10) },
                local.as_ref(),
            ));
            assert!(exec.run_until_stalled(&mut receive_fut).is_pending());
            // Now that we've received 10 credits, we should send the queued user data
            // frames from 2c & 2d.
            expect_user_data_frame(
                &mut exec,
                &mut outgoing_frames,
                data2c,
                None, // Don't expect to replenish (remote = MAX).
            );
            assert!(exec.run_until_stalled(&mut receive_fut).is_pending());
            expect_user_data_frame(
                &mut exec,
                &mut outgoing_frames,
                data2d,
                None, // Don't expect to replenish (remote = MAX).
            );

            // `data3` should not be relayed to the client since it's empty.
            assert!(exec.run_until_stalled(&mut data_received_by_client).is_pending());
            assert!(exec.run_until_stalled(&mut receive_fut).is_ready());
        }
    }

    #[test]
    fn finished_signal_resolves_when_session_channel_dropped() {
        let mut exec = fasync::Executor::new().unwrap();

        let (_inspect, session_channel, _client, _outgoing_frames) =
            create_and_establish(Role::Initiator, DLCI::try_from(8).unwrap(), None);

        // Finished signal is pending while the channel is active.
        let mut finished = session_channel.finished();
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Pending);

        // SessionChannel is dropped (usually due to peer disconnection). Polling the finished
        // signal thereafter is OK - should resolve immediately.
        drop(session_channel);
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Ready(_));
    }

    #[test]
    fn finished_signal_resolves_when_client_disconnects() {
        let mut exec = fasync::Executor::new().unwrap();

        let dlci = DLCI::try_from(19).unwrap();
        let (_inspect, session_channel, _client, mut outgoing_frames) =
            create_and_establish(Role::Responder, dlci, None);

        // Finished signal is pending while the channel is active.
        let mut finished = session_channel.finished();
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Pending);

        // Client closes its end of the RFCOMM channel - expect the outgoing Disconnect frame.
        drop(_client);
        expect_frame(&mut exec, &mut outgoing_frames, FrameData::Disconnect, Some(dlci));
        // Polling the finished signal thereafter is OK - should resolve.
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Ready(_));
        assert!(!session_channel.is_established());

        // Checking again should resolve immediately.
        let mut finished = session_channel.finished();
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Ready(_));
    }

    #[test]
    fn finished_signal_before_establishment_resolves_immediately() {
        let mut exec = fasync::Executor::new().unwrap();

        let session_channel = SessionChannel::new(DLCI::try_from(19).unwrap(), Role::Initiator);
        // Checking termination multiple times is OK.
        assert!(!session_channel.is_established());
        let mut finished = session_channel.finished();
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Ready(_));
        assert!(!session_channel.is_established());
    }

    #[test]
    fn finish_signal_resolves_with_multiple_establishments() {
        let mut exec = fasync::Executor::new().unwrap();

        let dlci = DLCI::try_from(19).unwrap();
        let (_inspect, mut session_channel, _client, _outgoing_frames) =
            create_and_establish(Role::Responder, dlci, None);

        let mut finished = session_channel.finished();
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Pending);

        // Establishment occurs again.
        let (_remote, _frame_receiver) = establish_channel(&mut session_channel);

        // Finished signal from previous establishment should resolve.
        assert_matches!(exec.run_until_stalled(&mut finished), Poll::Ready(_));
        // New signal should be pending still.
        let mut finished2 = session_channel.finished();
        assert_matches!(exec.run_until_stalled(&mut finished2), Poll::Pending);
    }

    #[test]
    fn session_channel_inspect_updates_with_new_flow_control() {
        let inspect = inspect::Inspector::new();

        // Set up a channel with inspect.
        let dlci = DLCI::try_from(8).unwrap();
        let mut channel = SessionChannel::new(dlci, Role::Initiator)
            .with_inspect(inspect.root(), "channel_")
            .unwrap();
        // Default tree.
        inspect::assert_inspect_tree!(inspect, root: {
            channel_: {
                dlci: 8u64,
            },
        });

        let flow_control = FlowControlMode::CreditBased(Credits::new(
            12, // Arbitrary
            15, // Arbitrary
        ));
        assert!(channel.set_flow_control(flow_control).is_ok());
        // Inspect tree should be updated with the initial credits.
        inspect::assert_inspect_tree!(inspect, root: {
            channel_: {
                dlci: 8u64,
                initial_local_credits: 12u64,
                initial_remote_credits: 15u64,
            },
        });
    }

    #[test]
    fn session_channel_inspect_updates_when_established() {
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(7_000_000));

        // Set up and establish a channel with inspect.
        let dlci = DLCI::try_from(8).unwrap();
        let flow_control = FlowControlMode::CreditBased(Credits::new(
            12, // Arbitrary
            15, // Arbitrary
        ));
        let (inspect, _channel, _client, mut outgoing_frames) =
            create_and_establish(Role::Initiator, DLCI::try_from(8).unwrap(), Some(flow_control));
        // Upon establishment, the inspect tree should have a `flow_controller` node.
        inspect::assert_inspect_tree!(inspect, root: {
            channel_: contains {
                flow_controller: {
                    controller_type: "credit_flow",
                    local_credits: 12u64, // Local same as initial local amount.
                    remote_credits: 15u64, // Remote same as initial remote amount.
                    inbound_stream: {
                        bytes_per_second_current: 0u64,
                        start_time: 7_000_000i64,
                        total_bytes: 0u64,
                    },
                    outbound_stream: {
                        bytes_per_second_current: 0u64,
                        start_time: 7_000_000i64,
                        total_bytes: 0u64,
                    },
                },
            },
        });

        // Client closes its end of the RFCOMM channel - expect the outgoing Disconnect frame.
        drop(_client);
        expect_frame(&mut exec, &mut outgoing_frames, FrameData::Disconnect, Some(dlci));
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        // The flow controller node should be removed indicating the inactiveness of the channel.
        inspect::assert_inspect_tree!(inspect, root: {
            channel_: {
                dlci: 8u64,
                initial_local_credits: 12u64,
                initial_remote_credits: 15u64,
            },
        });
    }

    #[test]
    fn credit_flow_controller_inspect_updates_after_data_exchange() {
        let initial_time = fasync::Time::from_nanos(987_000_000);
        let initial_credits = Credits::new(50, 70);
        let (mut exec, inspect, mut flow_controller, mut outgoing_frames) =
            setup_credit_flow_controller(initial_time, initial_credits);
        let (local, mut client) = Channel::create();
        let mut data_received_by_client = Box::pin(client.next());

        // After some time, the peer sends us user data with credits.
        exec.set_fake_time(1.seconds().after_now());
        let data1 = UserData { information: vec![0x12, 0x34, 0x56, 0x78, 0x90] };
        {
            let mut receive_fut = Box::pin(flow_controller.receive_data_from_peer(
                FlowControlledData { user_data: data1, credits: Some(10) },
                local.as_ref(),
            ));
            assert!(exec.run_until_stalled(&mut receive_fut).is_ready());
            // The user data should be relayed to the RFCOMM client.
            assert!(exec.run_until_stalled(&mut data_received_by_client).is_ready());
        }
        // Flow controller inspect node should be updated.
        inspect::assert_inspect_tree!(inspect, root: {
            flow_controller: {
                controller_type: "credit_flow",
                local_credits: 60u64, // 50 (initial) + 10 (Replenished in `data1`)
                remote_credits: 69u64, // 70 (initial) - 1 (Received frame)
                inbound_stream: {
                    bytes_per_second_current: 5u64,
                    start_time: 987_000_000i64,
                    total_bytes: 5u64, // Received 5 bytes from peer.
                },
                outbound_stream: {
                    bytes_per_second_current: 0u64,
                    start_time: 987_000_000i64,
                    total_bytes: 0u64,
                },
            },
        });

        // After some time, the client holding the RFCOMM channel responds with data.
        exec.set_fake_time(2.seconds().after_now());
        let data2 = UserData { information: vec![0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22] };
        {
            let mut send_fut = Box::pin(flow_controller.send_data_to_peer(data2.clone()));
            assert!(exec.run_until_stalled(&mut send_fut).is_pending());
            // Frame should be sent to the peer (Credits = 255 (max) - 69 (current remote)).
            expect_user_data_frame(&mut exec, &mut outgoing_frames, data2, Some(186));
            assert!(exec.run_until_stalled(&mut send_fut).is_ready());
        }
        // Flow controller inspect node should be updated.
        inspect::assert_inspect_tree!(inspect, root: {
            flow_controller: {
                controller_type: "credit_flow",
                local_credits: 59u64, // 60 (previous amount) - 1 (sent frame)
                remote_credits: 255u64, // We replenished the remote with the max.
                inbound_stream: {
                    bytes_per_second_current: 5u64,
                    start_time: 987_000_000i64,
                    total_bytes: 5u64,
                },
                outbound_stream: {
                    bytes_per_second_current: 2u64, // 8 (bytes) / 3 (seconds)
                    start_time: 987_000_000i64,
                    total_bytes: 8u64, // Sent 8 bytes to peer.
                },
            },
        });
    }
}

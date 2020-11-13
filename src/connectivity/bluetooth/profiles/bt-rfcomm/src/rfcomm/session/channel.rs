// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::Channel,
    futures::{channel::mpsc, select, FutureExt, SinkExt, StreamExt},
    log::{error, info, trace},
    std::convert::TryInto,
};

use crate::rfcomm::{
    frame::{Frame, UserData},
    types::{RfcommError, Role, DLCI},
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
#[derive(Clone, Copy, Debug, Default)]
pub struct Credits {
    /// The local credits that we currently have. Determines the number of frames
    /// we can send to the remote peer.
    local: usize,
    /// The remote peer's current credit count. Determines the number of frames the
    /// remote may send to us.
    remote: usize,
}

impl Credits {
    pub fn new(local: usize, remote: usize) -> Self {
        Self { local, remote }
    }

    /// Returns the number of credits to replenish the remote with. Returns None if the remote
    /// has sufficient credits and does not need any more.
    fn remote_replenish_amount(&self) -> Option<std::num::NonZeroU8> {
        HIGH_CREDIT_WATER_MARK
            .checked_sub(self.remote)
            .map(|c| c.try_into().unwrap())
            .and_then(std::num::NonZeroU8::new)
    }

    /// Reduces the local credit count by 1.
    fn decrement_local(&mut self) {
        self.local = self.local.checked_sub(1).unwrap_or(0)
    }

    /// Reduces the remote credit count by 1. Returns true if the remote credit count is
    /// low, false otherwise.
    fn decrement_remote(&mut self) -> bool {
        self.remote = self.remote.checked_sub(1).unwrap_or(0);
        self.remote <= LOW_CREDIT_WATER_MARK
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
}

impl SimpleController {
    fn new(role: Role, dlci: DLCI, outgoing_sender: mpsc::Sender<Frame>) -> Self {
        Self { role, dlci, outgoing_sender }
    }
}

#[async_trait]
impl FlowController for SimpleController {
    async fn send_data_to_peer(&mut self, data: UserData) {
        let user_data_frame = Frame::make_user_data_frame(self.role, self.dlci, data, None);
        let _ = self.outgoing_sender.send(user_data_frame).await;
    }

    async fn receive_data_from_peer(&mut self, data: FlowControlledData, client: &fidl::Socket) {
        let _ = client.write(&data.user_data.information[..]);
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
        }
    }

    /// Sends the `user_data` directly to the remote peer - queues the data to be sent
    /// later if there is an insufficient number of credits.
    async fn send_user_data(&mut self, user_data: UserData) {
        let contains_data = !user_data.is_empty();
        if contains_data && self.credits.local == 0 {
            self.outgoing_data_pending_credits.push(user_data);
            return;
        }

        let credits_to_replenish =
            self.credits.remote_replenish_amount().map(std::num::NonZeroU8::get);
        let frame =
            Frame::make_user_data_frame(self.role, self.dlci, user_data, credits_to_replenish);
        if self.outgoing_sender.send(frame).await.is_ok() {
            if contains_data {
                self.credits.decrement_local();
            }
            self.credits.remote += credits_to_replenish.unwrap_or(0) as usize;
        }
    }

    /// Handles receiving credits in a frame from the remote peer. If a nonzero number of credits
    /// were received, attempts to send any frames pending credits.
    async fn handle_received_credits(&mut self, received_credits: u8) {
        let need_to_send_queued = self.credits.local == 0;
        self.credits.local += received_credits as usize;

        // Note: It's possible that the number of local credits is less than the number of
        // queued frames pending credits. This is OK. We will simply send up to the number of
        // available credits worth of frames, and queue the rest for later.
        if need_to_send_queued && self.credits.local > 0 {
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

            if self.credits.decrement_remote() {
                trace!("Remote credit count is low. Sending empty frame");
                let _ = self.send_user_data(UserData { information: vec![] }).await;
            }
        }
    }
}

/// The flow control method for the channel.
#[derive(Debug, Copy, Clone)]
pub enum FlowControlMode {
    /// Credit-based flow control with the provided initial credits.
    CreditBased(Credits),
    /// No flow control is being used.
    None,
}

/// User data frames with optional credits to be used for flow control.
pub struct FlowControlledData {
    pub user_data: UserData,
    pub credits: Option<u8>,
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
    processing_task: Option<fasync::Task<()>>,
    /// The sender used to push user data to be written in the `processing_task`.
    user_data_queue: Option<mpsc::Sender<FlowControlledData>>,
}

impl SessionChannel {
    pub fn new(dlci: DLCI, role: Role) -> Self {
        Self { dlci, role, flow_control: None, processing_task: None, user_data_queue: None }
    }

    /// Returns true if this SessionChannel has been established. Namely, `self.establish()`
    /// has been called, and a processing task started up.
    pub fn is_established(&self) -> bool {
        self.processing_task.is_some()
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
        self.flow_control = Some(flow_control);
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
        flow_control: FlowControlMode,
        mut channel: Channel,
        mut pending_writes: mpsc::Receiver<FlowControlledData>,
        mut frame_sender: mpsc::Sender<Frame>,
    ) {
        let mut flow_controller: Box<dyn FlowController> = match flow_control {
            FlowControlMode::CreditBased(credits) => {
                Box::new(CreditFlowController::new(role, dlci, frame_sender.clone(), credits))
            }
            FlowControlMode::None => {
                Box::new(SimpleController::new(role, dlci, frame_sender.clone()))
            }
        };
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
        self.flow_control =
            self.flow_control.or(Some(FlowControlMode::CreditBased(Credits::default())));
        let processing_task = fasync::Task::local(Self::user_data_relay(
            self.dlci,
            self.role,
            self.flow_control.unwrap(),
            channel,
            pending_writes,
            frame_sender,
        ));
        self.user_data_queue = Some(user_data_queue);
        self.processing_task = Some(processing_task);
        trace!("Established SessionChannel for DLCI {:?}", self.dlci);
    }

    /// Receive a user data payload from the remote peer, which will be queued to be sent
    /// to the local profile client.
    pub fn receive_user_data(&mut self, data: FlowControlledData) -> Result<(), RfcommError> {
        if !self.is_established() {
            return Err(RfcommError::ChannelNotEstablished(self.dlci));
        }

        // This unwrap is safe because the sender is guaranteed to be set if the channel
        // has been established.
        let queue = self.user_data_queue.as_mut().unwrap();
        queue.try_send(data).map_err(|e| anyhow::format_err!("{:?}", e).into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::rfcomm::frame::{FrameData, UIHData};

    use futures::{channel::mpsc, pin_mut, task::Poll};
    use std::convert::TryFrom;

    /// Creates and establishes a SessionChannel. If provided, sets the flow control mode to
    /// `flow_control`.
    /// Returns the SessionChannel, the client end of the fuchsia_bluetooth::Channel, and a frame
    /// receiver that can be used to verify outgoing (i.e to the remote peer) frames being sent
    /// correctly.
    fn create_and_establish(
        role: Role,
        dlci: DLCI,
        flow_control: Option<FlowControlMode>,
    ) -> (SessionChannel, Channel, mpsc::Receiver<Frame>) {
        let mut session_channel = SessionChannel::new(dlci, role);
        assert!(!session_channel.is_established());
        if let Some(flow_control) = flow_control {
            assert!(session_channel.set_flow_control(flow_control).is_ok());
        }

        let (local, remote) = Channel::create();
        let (frame_sender, frame_receiver) = mpsc::channel(0);
        session_channel.establish(local, frame_sender);
        assert!(session_channel.is_established());

        (session_channel, remote, frame_receiver)
    }

    #[track_caller]
    fn expect_pending(exec: &mut fasync::Executor, outgoing_frames: &mut mpsc::Receiver<Frame>) {
        let mut fut = Box::pin(outgoing_frames.next());
        assert!(exec.run_until_stalled(&mut fut).is_pending());
    }

    #[track_caller]
    fn expect_frame_with_credits(
        exec: &mut fasync::Executor,
        outgoing_frames: &mut mpsc::Receiver<Frame>,
        expected_data: UserData,
        expected_credits: Option<u8>,
    ) {
        let mut fut = Box::pin(outgoing_frames.next());
        match exec.run_until_stalled(&mut fut) {
            Poll::Ready(Some(frame)) => match frame.data {
                FrameData::UnnumberedInfoHeaderCheck(UIHData::User(data)) => {
                    assert_eq!(data, expected_data);
                    assert_eq!(frame.credits, expected_credits);
                }
                x => panic!("Expected User frame but got: {:?}", x),
            },
            x => panic!("Expected ready frame but got: {:?}", x),
        }
    }

    #[test]
    fn test_establish_channel_and_send_data_with_no_flow_control() {
        let mut exec = fasync::Executor::new().unwrap();

        let role = Role::Responder;
        let dlci = DLCI::try_from(8).unwrap();
        let (mut session_channel, mut client, mut outgoing_frames) =
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
        expect_frame_with_credits(
            &mut exec,
            &mut outgoing_frames,
            UserData { information: client_data },
            None, // No credits since no flow control.
        );

        // Profile client no longer needs the RFCOMM channel - expect a Disc frame.
        drop(client);
        let mut fut = Box::pin(outgoing_frames.next());
        match exec.run_until_stalled(&mut fut) {
            Poll::Ready(Some(frame)) => {
                assert_eq!(frame.data, FrameData::Disconnect);
            }
            x => panic!("Expected ready with frame but got {:?}", x),
        }
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
        let (mut session_channel, mut client, mut outgoing_frames) =
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
            expect_frame_with_credits(
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
        expect_frame_with_credits(
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
        let (mut session_channel, mut client, mut outgoing_frames) = create_and_establish(
            role,
            dlci,
            Some(FlowControlMode::CreditBased(Credits {
                local: 12,  // Arbitrary
                remote: 12, // Arbitrary
            })),
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
        expect_frame_with_credits(
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
        let mut exec = fasync::Executor::new().unwrap();

        let user_dlci = DLCI::try_from(9).unwrap();
        let (frame_sender, mut outgoing_frames) = mpsc::channel(0);
        let initial_credits = Credits { local: 2, remote: 7 };
        let mut flow_controller =
            CreditFlowController::new(Role::Initiator, user_dlci, frame_sender, initial_credits);
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
            expect_frame_with_credits(
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
            expect_frame_with_credits(&mut exec, &mut outgoing_frames, data2a, None);
            assert!(exec.run_until_stalled(&mut send_fut).is_ready());
        }
        // 2b. RFCOMM client sends more data.
        let data2b = UserData { information: vec![0x99] };
        {
            let mut send_fut = Box::pin(flow_controller.send_data_to_peer(data2b.clone()));
            assert!(exec.run_until_stalled(&mut send_fut).is_pending());
            // We have sufficient local credits (1), so frame should be sent. No credits to refresh.
            expect_frame_with_credits(&mut exec, &mut outgoing_frames, data2b, None);
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
            expect_frame_with_credits(
                &mut exec,
                &mut outgoing_frames,
                data2c,
                None, // Don't expect to replenish (remote = MAX).
            );
            assert!(exec.run_until_stalled(&mut receive_fut).is_pending());
            expect_frame_with_credits(
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
}

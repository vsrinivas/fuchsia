// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::Channel,
    futures::{channel::mpsc, select, FutureExt, SinkExt, StreamExt},
    log::{error, info, trace},
};

use crate::rfcomm::{
    frame::{Frame, UserData},
    types::{RfcommError, Role, DLCI},
};

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
///   let _ = session_channel.receive_user_data(user_data_buf1).await;
///   let _ = session_channel.receive_user_data(user_data_buf2).await;
pub struct SessionChannel {
    /// The DLCI associated with this channel.
    dlci: DLCI,
    /// The local role assigned to us.
    role: Role,
    /// The processing task associated with the channel. This is set through
    /// `self.establish()`, and indicates whether this SessionChannel is
    /// currently active.
    processing_task: Option<fasync::Task<()>>,
    /// The sender used to push user-data to be written in the `processing_task`.
    user_data_queue: Option<mpsc::Sender<UserData>>,
}

impl SessionChannel {
    pub fn new(dlci: DLCI, role: Role) -> Self {
        Self { dlci, role, processing_task: None, user_data_queue: None }
    }

    /// Returns true if this SessionChannel has been established. Namely, `self.establish()`
    /// has been called, and a processing task started up.
    pub fn is_established(&self) -> bool {
        self.processing_task.is_some()
    }

    /// A user data relay task that reads data from the `channel` and relays to the
    /// `frame_sender`.
    /// The task also processes user data in the `pending_writes` queue and sends it
    /// to the `channel`.
    async fn user_data_relay(
        dlci: DLCI,
        role: Role,
        mut channel: Channel,
        mut pending_writes: mpsc::Receiver<UserData>,
        mut frame_sender: mpsc::Sender<Frame>,
    ) {
        loop {
            select! {
                user_data = channel.next().fuse() => {
                    match user_data {
                        Some(Ok(bytes)) => {
                            trace!("Received user-data packet for DLCI {:?}: {:?}", dlci, bytes);
                            let frame = Frame::make_user_data_frame(role, dlci, UserData { information: bytes });
                            let _ = frame_sender.send(frame).await;
                        }
                        Some(Err(e)) => {
                            error!("Error receiving data from client {:?}", e);
                            continue;
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
                }
                user_data_to_be_written = pending_writes.select_next_some() => {
                    let _ = channel.as_ref().write(&user_data_to_be_written.information[..]);
                }
                complete => break,
            }
        }
        info!("Profile-client processing task for DLCI {:?} finished", dlci);
    }

    /// Starts the processing task over the provided `channel`. The processing task will:
    /// 1) Read bytes received from the `channel` and relay user data to the `frame_sender`.
    /// 2) Read packets from the `pending_writes` queue, and send the data to the other
    ///    end of the `channel`.
    ///
    /// While unusual, it is OK to call establish() multiple times. The currently active
    /// processing task will be dropped, and a new one will be spawned using the provided
    /// new channel.
    pub fn establish(&mut self, channel: Channel, frame_sender: mpsc::Sender<Frame>) {
        let (user_data_queue, pending_writes) = mpsc::channel(0);
        let processing_task = fasync::Task::spawn(Self::user_data_relay(
            self.dlci,
            self.role,
            channel,
            pending_writes,
            frame_sender,
        ));
        self.user_data_queue = Some(user_data_queue);
        self.processing_task = Some(processing_task);
        trace!("Established SessionChannel for DLCI {:?}", self.dlci);
    }

    /// Receive a user data packet from the peer, which will be queued to be sent to the
    /// local profile client.
    pub fn receive_user_data(&mut self, user_data: UserData) -> Result<(), RfcommError> {
        if !self.is_established() {
            return Err(RfcommError::ChannelNotEstablished(self.dlci));
        }

        // This unwrap is safe because the sender is guaranteed to be set if the channel
        // has been established.
        let queue = self.user_data_queue.as_mut().unwrap();
        queue.try_send(user_data).map_err(|e| anyhow::format_err!("{:?}", e).into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::rfcomm::frame::FrameData;

    use futures::{channel::mpsc, pin_mut, task::Poll};
    use std::convert::TryFrom;

    /// Creates and establishes a SessionChannel. Returns the SessionChannel, the remote end
    /// of the fuchsia_bluetooth::Channel, and a frame receiver that can be used to verify
    /// frames being sent correctly.
    fn create_and_establish(
        role: Role,
        dlci: DLCI,
    ) -> (SessionChannel, Channel, mpsc::Receiver<Frame>) {
        let mut session_channel = SessionChannel::new(dlci, role);
        assert!(!session_channel.is_established());

        let (local, remote) = Channel::create();
        let (frame_sender, frame_receiver) = mpsc::channel(0);
        session_channel.establish(local, frame_sender);
        assert!(session_channel.is_established());

        (session_channel, remote, frame_receiver)
    }

    #[test]
    fn test_establish_channel_and_send_data() {
        let mut exec = fasync::Executor::new().unwrap();

        let role = Role::Responder;
        let dlci = DLCI::try_from(8).unwrap();
        let (mut session_channel, mut client, mut frame_receiver) =
            create_and_establish(role, dlci);

        let profile_client_fut = client.next();
        pin_mut!(profile_client_fut);
        assert!(exec.run_until_stalled(&mut profile_client_fut).is_pending());

        let frame_fut = frame_receiver.next();
        pin_mut!(frame_fut);
        assert!(exec.run_until_stalled(&mut frame_fut).is_pending());

        // Session sends user data to be relayed to the profile-client.
        let user_data = UserData { information: vec![0x01, 0x02, 0x03] };
        assert!(session_channel.receive_user_data(user_data.clone()).is_ok());

        // Data should be relayed to the client.
        match exec.run_until_stalled(&mut profile_client_fut) {
            Poll::Ready(Some(Ok(buf))) => {
                assert_eq!(buf, user_data.information);
            }
            x => panic!("Expected ready with data but got: {:?}", x),
        }

        // Client responds with some user data.
        let client_data = vec![0xff, 0x00, 0xaa, 0x0bb];
        assert!(client.as_ref().write(&client_data).is_ok());

        // Data should be processed by the SessionChannel, packed into an RFCOMM frame, and relayed
        // using the `frame_sender`.
        let expected_frame =
            Frame::make_user_data_frame(role, dlci, UserData { information: client_data });
        match exec.run_until_stalled(&mut frame_fut) {
            Poll::Ready(Some(frame)) => {
                assert_eq!(frame, expected_frame);
            }
            x => panic!("Expected ready with frame but got: {:?}", x),
        }

        // Profile client no longer needs the RFCOMM channel - expect a Disc frame.
        drop(client);
        match exec.run_until_stalled(&mut frame_fut) {
            Poll::Ready(Some(frame)) => {
                assert_eq!(frame.data, FrameData::Disconnect);
            }
            x => panic!("Expected ready with frame but got {:?}", x),
        }
    }
}

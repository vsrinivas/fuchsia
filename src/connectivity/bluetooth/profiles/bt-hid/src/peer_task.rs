// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_helpers::maybe_stream::MaybeStream,
    async_utils::channel::TrySend,
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::profile::{psm_from_protocol, Psm},
    fuchsia_bluetooth::types::{Channel, PeerId},
    futures::channel::mpsc,
    futures::{select, FutureExt, StreamExt},
    profile_client::ProfileEvent,
    std::convert::TryInto,
    std::default::Default,
    std::future::Future,
    std::pin::Pin,
    std::task::{Context, Poll},
    tracing::{debug, error, info, warn},
};

use crate::peer_info::PeerInfo;

pub struct PeerTask {
    peer_id: PeerId,
    // TODO(fxb/111451) Refactor PeerTask to use fewer MPSC channels.
    profile_event_sender: mpsc::Sender<ProfileEvent>,
    task: fasync::Task<()>,
}

pub struct PeerTaskInner {
    peer_id: PeerId,
    peer_info: PeerInfo,
    profile_event_receiver: mpsc::Receiver<ProfileEvent>,
    profile_proxy: bredr::ProfileProxy,
    // Bluetooth HID v1.1 5.2.2 The interrupt channel must torn down before the control channel.
    // These will be dropped in this order because of the order they appear in the struct definition.
    interrupt_channel: MaybeStream<Channel>,
    control_channel: MaybeStream<Channel>,
}

impl PeerTask {
    pub fn spawn_new(peer_id: PeerId, profile_proxy: bredr::ProfileProxy) -> Self {
        let (profile_event_sender, profile_event_receiver) = mpsc::channel(0);

        let peer_task_inner = PeerTaskInner {
            peer_id,
            peer_info: PeerInfo::default(),
            profile_event_receiver,
            profile_proxy,
            control_channel: MaybeStream::default(),
            interrupt_channel: MaybeStream::default(),
        };
        let task = fasync::Task::local(peer_task_inner.run());

        Self { peer_id, profile_event_sender, task }
    }

    pub async fn handle_profile_event(&mut self, event: ProfileEvent) -> Result<(), ProfileEvent> {
        self.profile_event_sender.try_send_fut(event).await
    }
}

/// When a task finishes, this future returns the id of the associated peer.
impl Future for PeerTask {
    type Output = PeerId;

    fn poll(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        self.task.poll_unpin(ctx).map(|_| self.peer_id)
    }
}

impl PeerTaskInner {
    // Run run_inner and drop any errors, which already should have been logged.
    async fn run(mut self) {
        info!("Starting task for peer {:}.", self.peer_id);
        let _ = self.run_inner().await;
        info!("Ending task for peer {:}", self.peer_id);
    }

    async fn run_inner(&mut self) -> Result<(), ()> {
        // We can't handle any reports from the device until we have the SDP record containing the input descriptors.
        debug!("Waiting for search results for peer {:}", self.peer_id);
        self.wait_for_search_result().await?;

        debug!("Waiting for search results for peer {:}", self.peer_id);
        self.set_up_channels_and_fidl().await?;

        debug!("Starting main loop for peer {:}", self.peer_id);
        self.main_loop().await?;

        Ok(())
    }

    async fn wait_for_search_result(&mut self) -> Result<(), ()> {
        loop {
            let profile_event_option = self.profile_event_receiver.next().await;
            let profile_event = match profile_event_option {
                None => {
                    warn!("Profile event channel for peer {:} closed while waitng for search results.", self.peer_id);
                    return Err(());
                }
                Some(pe) => pe,
            };

            let profile_event_is_search_result =
                Self::is_profile_event_search_result(&profile_event);

            self.handle_profile_event(profile_event, /* waiting_for_search_result = */ true)?;

            if profile_event_is_search_result {
                break;
            }
        }

        return Ok(());
    }

    fn is_profile_event_search_result(profile_event: &ProfileEvent) -> bool {
        match profile_event {
            ProfileEvent::SearchResult { .. } => true,
            _ => false,
        }
    }

    async fn set_up_channels_and_fidl(&mut self) -> Result<(), ()> {
        // Bluetooth HID v1.1 5.2.2 The control channel must be set up before the interrupt channel.
        if !self.control_channel.is_some() {
            let channel = self.connect_channel(bredr::PSM_HID_CONTROL).await?;
            self.control_channel.set(channel);
        }

        if !self.interrupt_channel.is_some() {
            let channel = self.connect_channel(bredr::PSM_HID_INTERRUPT).await?;
            self.interrupt_channel.set(channel);
        }

        // TODO(fxb/106071) Set up FIDL connections to input device stack.

        Ok(())
    }

    async fn main_loop(&mut self) -> Result<(), ()> {
        loop {
            select! {
                profile_event_option = self.profile_event_receiver.next().fuse() => {
                    match profile_event_option {
                        None => {
                            error!("Profile event channel closed unexpectedly for peer {:} in main loop.", self.peer_id);
                            break;
                        }
                        Some(profile_event) => {
                            debug!("Got profile event for Peer {:}: {:?}", self.peer_id, profile_event);
                            let profile_event_result = self.handle_profile_event(profile_event, /* waiting_for_search_result = */ false);
                            if profile_event_result.is_err() {
                                // Already emitted a log.
                                break;
                            }
                        }
                    }
                }
                control_message = self.control_channel.next().fuse() => {
                    debug!("Got control message for Peer {:}: {:?}", self.peer_id, control_message);
                    match control_message {
                        Some(Ok(control_message)) => self.handle_control_message(control_message),
                        message => {
                            debug!("Control channel closed for Peer {:} with {:?}", self.peer_id, message);
                            break;
                        }
                    }
                }
                interrupt_message = self.interrupt_channel.next().fuse() => {
                    debug!("Got interrupt message for Peer {:}: {:?}", self.peer_id, interrupt_message);
                    match interrupt_message {
                        Some(Ok(interrupt_message)) => self.handle_interrupt_message(interrupt_message),
                        message => {
                            debug!("Interrupt channel closed for Peer {:} with {:?}", self.peer_id, message);
                            break;
                        }
                    }
                }
                // TODO(fxb/106071) Handle FIDL messages here.
                complete => {
                    debug!("Received nothing for Peer {:}", self.peer_id);
                    break;
                }
            }
        }

        Ok(())
    }

    fn handle_profile_event(
        &mut self,
        event: ProfileEvent,
        waiting_for_search_result: bool,
    ) -> Result<(), ()> {
        debug!("Handling profile event {:?} for peer {:}", event, self.peer_id);
        match (event, waiting_for_search_result) {
            (ProfileEvent::SearchResult { protocol, attributes, .. }, true) => {
                self.handle_search_result(protocol, attributes)
            }
            (ProfileEvent::SearchResult { .. }, false) => {
                warn!("Got unexpected search result for known peer {:}", self.peer_id);
                Ok(())
            }
            (ProfileEvent::PeerConnected { channel, protocol, .. }, _) => {
                self.handle_peer_connected(channel, protocol);
                Ok(())
            }
        }
    }

    // Returns Ok if the SDP record was succesfully parsed, Err otherwise.
    fn handle_search_result(
        &mut self,
        protocol: Option<Vec<bredr::ProtocolDescriptor>>,
        attributes: Vec<bredr::Attribute>,
    ) -> Result<(), ()> {
        debug!(
            "Handling search result {:?}, {:?} for peer {:}",
            protocol, attributes, self.peer_id
        );
        match PeerInfo::new_from_sdp_record(&protocol, &attributes, self.peer_id) {
            Ok(peer_info) => {
                debug!(
                    "Succefully parsed SDP record for peer {:} into {:?}",
                    self.peer_id, peer_info,
                );
                self.peer_info = peer_info;
                Ok(())
            }
            Err(error) => {
                warn!("Failed to parse SDP record for peer {:} with {:?}", self.peer_id, error);
                Err(())
            }
        }
    }

    fn handle_peer_connected(
        &mut self,
        channel: Channel,
        protocol_descriptors: Vec<bredr::ProtocolDescriptor>,
    ) {
        debug!("Handling peer connected {:?} for peer {:}", protocol_descriptors, self.peer_id);

        let protocol = protocol_descriptors.iter().map(Into::into).collect();
        match psm_from_protocol(&protocol) {
            Some(psm) if psm == Psm::HID_CONTROL => {
                let was_set = self.control_channel.is_some();
                self.control_channel.set(channel);
                debug!(
                    "Set control channel for peer {:?}; channel previously existed: {:}",
                    self.peer_id, was_set
                )
            }
            Some(psm) if psm == Psm::HID_INTERRUPT => {
                let was_set = self.interrupt_channel.is_some();
                self.interrupt_channel.set(channel);
                debug!(
                    "Set interrupt channel for peer {:?}; channel previously existed: {:}",
                    self.peer_id, was_set
                )
            }
            psm => {
                warn!(
                    "Got peer connected for peer {:} with unexpected PSM {:?}",
                    self.peer_id, psm
                );
            }
        }
    }

    fn handle_control_message(&self, _: Vec<u8>) {}

    fn handle_interrupt_message(&self, _: Vec<u8>) {}

    async fn connect_channel(&self, psm: u16) -> Result<Channel, ()> {
        debug!("Creating channel to peer {:} with PSM {:}", self.peer_id, psm);
        let channel_parameters = bredr::ChannelParameters {
            channel_mode: None,
            max_rx_sdu_size: None,
            // TODO(fxb/96996) Consider security requirements.
            security_requirements: None,
            // HID v1.1 5.2.7 The L2CAP Flush timeout parameter should be set to 0xFFFF.
            flush_timeout: Some(0xFFFF),
            ..bredr::ChannelParameters::EMPTY
        };
        let mut connect_parameters = bredr::ConnectParameters::L2cap(bredr::L2capParameters {
            psm: Some(psm),
            parameters: Some(channel_parameters),
            ..bredr::L2capParameters::EMPTY
        });

        let connect_result_result =
            self.profile_proxy.connect(&mut self.peer_id.into(), &mut connect_parameters).await;
        match connect_result_result {
            Ok(Ok(chan)) => match chan.try_into() {
                Ok(chan) => {
                    debug!(
                        "Sucessfully created channel to peer {:} with PSM {:}",
                        self.peer_id, psm
                    );
                    Ok(chan)
                }
                Err(err) => {
                    warn!("Unable to convert bredr::Channel {:#x} to peer {:} into Channel with error {:?}", psm, self.peer_id, err);
                    Err(())
                }
            },
            Ok(Err(err)) => {
                warn!(
                    "Unable to create channel {:#x} to peer {:} with Bluetooth error code {:?}.",
                    psm, self.peer_id, err
                );
                Err(())
            }
            Err(err) => {
                warn!(
                    "Unable to create channel {:#x} to peer {:} with FIDL error {:?}.",
                    psm, self.peer_id, err
                );
                Err(())
            }
        }
    }
}

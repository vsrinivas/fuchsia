// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::packets::VendorCommand;

#[derive(Debug, PartialEq)]
pub enum PeerChannel<T> {
    Connected(Arc<T>),
    Connecting,
    Disconnected,
}

impl<T> PeerChannel<T> {
    pub fn connection(&self) -> Option<Arc<T>> {
        match self {
            PeerChannel::Connected(t) => Some(t.clone()),
            _ => None,
        }
    }
}

/// Internal object to manage a remote peer
#[derive(Debug)]
pub struct RemotePeer {
    pub peer_id: PeerId,

    /// Contains the remote peer's target profile.
    pub target_descriptor: RwLock<Option<AvrcpService>>,

    /// Contains the remote peer's controller profile.
    pub controller_descriptor: RwLock<Option<AvrcpService>>,

    /// Control channel to the remote device.
    pub control_channel: PeerChannel<AvcPeer>,

    // TODO(BT-2221): add browse channel.
    // browse_channel: PeerChannel<AvtcpPeer>,
    //
    /// Contains a vec of all event stream listeners obtained by any Controllers around this peer
    /// that are listening for events from this peer from this peer.
    controller_listeners: Vec<mpsc::Sender<ControllerEvent>>,

    /// Processes commands received as AVRCP target and holds state for continuations and requested
    /// notifications for the control channel. Only set once we have enough information to determine
    /// our role based on the peer's SDP record.
    pub command_handler: Mutex<Option<ControlChannelHandler>>,
}

impl RemotePeer {
    pub fn new(peer_id: PeerId) -> Self {
        Self {
            peer_id,
            control_channel: PeerChannel::Disconnected,
            // TODO(BT-2221): add browse channel.
            //browse_channel: PeerChannel::Disconnected,
            controller_listeners: Vec::new(),
            target_descriptor: RwLock::new(None),
            controller_descriptor: RwLock::new(None),
            command_handler: Mutex::new(None),
        }
    }

    /// Enumerates all listening controller_listeners queues and sends a clone of the event to each
    pub fn broadcast_event(&mut self, event: ControllerEvent) {
        // remove all the dead listeners from the list.
        self.controller_listeners.retain(|i| !i.is_closed());
        for sender in self.controller_listeners.iter_mut() {
            if let Err(send_error) = sender.try_send(event.clone()) {
                fx_log_err!(
                    "unable to send event to peer controller stream for {} {:?}",
                    self.peer_id,
                    send_error
                );
            }
        }
    }

    pub fn add_control_listener(&mut self, sender: mpsc::Sender<ControllerEvent>) {
        self.controller_listeners.push(sender)
    }

    /// For the FIDL test controller. Informational only and intended for logging only. The state is
    /// inherently racey.
    pub fn is_connected(&self) -> bool {
        match self.control_channel {
            PeerChannel::Connected(_) => true,
            _ => false,
        }
    }

    // Hold the write lock on control_channel before calling this.
    pub fn reset_command_handler(&self) {
        let mut cmd_handler = self.command_handler.lock();
        *cmd_handler = None;
    }

    pub fn reset_connection(&mut self) {
        self.reset_command_handler();
        self.control_channel = PeerChannel::Disconnected;
    }

    pub fn get_control_connection(&self) -> Result<Arc<AvcPeer>, Error> {
        self.control_channel.connection().ok_or(Error::RemoteNotFound)
    }

    pub fn set_control_connection(&mut self, peer: AvcPeer) {
        self.reset_command_handler();
        self.control_channel = PeerChannel::Connected(Arc::new(peer));
    }

    /// Send a generic vendor dependent command and returns the result as a future.
    /// This method encodes the `command` packet, awaits and decodes all responses, will issue
    /// continuation commands for incomplete responses (eg "get_element_attributes" command), and
    /// will return a result of the decoded packet or an error for any non stable response received
    pub async fn send_vendor_dependent_command(
        peer: Arc<RwLock<Self>>,
        command: &(impl VendorDependent + VendorCommand),
    ) -> Result<Vec<u8>, Error> {
        let avc_peer = peer.read().get_control_connection()?;
        let mut buf = vec![];
        let packet = command.encode_packet().expect("unable to encode packet");
        let mut stream =
            avc_peer.send_vendor_dependent_command(command.command_type(), &packet[..])?;

        loop {
            let response = loop {
                let result = stream.next().await.ok_or(Error::CommandFailed)?;
                let response: AvcCommandResponse = result.map_err(|e| Error::AvctpError(e))?;
                fx_vlog!(tag: "avrcp", 1, "vendor response {:#?}", response);
                match (response.response_type(), command.command_type()) {
                    (AvcResponseType::Interim, _) => continue,
                    (AvcResponseType::NotImplemented, _) => return Err(Error::CommandNotSupported),
                    (AvcResponseType::Rejected, _) => return Err(Error::CommandFailed),
                    (AvcResponseType::InTransition, _) => return Err(Error::UnexpectedResponse),
                    (AvcResponseType::Changed, _) => return Err(Error::UnexpectedResponse),
                    (AvcResponseType::Accepted, AvcCommandType::Control) => break response.1,
                    (AvcResponseType::ImplementedStable, AvcCommandType::Status) => {
                        break response.1
                    }
                    _ => return Err(Error::UnexpectedResponse),
                }
            };

            match VendorDependentPreamble::decode(&response[..]) {
                Ok(preamble) => {
                    buf.extend_from_slice(&response[preamble.encoded_len()..]);
                    match preamble.packet_type() {
                        PacketType::Single | PacketType::Stop => {
                            break;
                        }
                        // Still more to decode. Queue up a continuation call.
                        _ => {}
                    }
                }
                Err(e) => {
                    fx_log_info!("Unable to parse vendor dependent preamble: {:?}", e);
                    return Err(Error::PacketError(e));
                }
            };

            let command = RequestContinuingResponseCommand::new(u8::from(&command.pdu_id()));
            let packet = command.encode_packet().expect("unable to encode packet");

            stream = avc_peer.send_vendor_dependent_command(command.command_type(), &packet[..])?;
        }
        Ok(buf)
    }

    /// Sends a single passthrough keycode over the control channel.
    pub async fn send_avc_passthrough(
        peer: Arc<RwLock<Self>>,
        payload: &[u8; 2],
    ) -> Result<(), Error> {
        let avc_peer = peer.read().get_control_connection()?;
        let response = avc_peer.send_avc_passthrough_command(payload).await;
        match response {
            Ok(AvcCommandResponse(AvcResponseType::Accepted, _)) => {
                return Ok(());
            }
            Ok(AvcCommandResponse(AvcResponseType::Rejected, _)) => {
                fx_log_info!("avrcp command rejected {}: {:?}", peer.read().peer_id, response);
                return Err(Error::CommandNotSupported);
            }
            Err(e) => {
                fx_log_err!("error sending avc command to {}: {:?}", peer.read().peer_id, e);
                return Err(Error::CommandFailed);
            }
            _ => {
                fx_log_err!(
                    "error sending avc command. unhandled response {}: {:?}",
                    peer.read().peer_id,
                    response
                );
                return Err(Error::CommandFailed);
            }
        }
    }

    /// Retrieve the events supported by the peer by issuing a GetCapabilities command.
    pub async fn get_supported_events(
        peer: Arc<RwLock<Self>>,
    ) -> Result<Vec<NotificationEventId>, Error> {
        let cmd = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::EventsId);
        fx_vlog!(tag: "avrcp", 1, "get_capabilities(events) send command {:#?}", cmd);
        let buf = Self::send_vendor_dependent_command(peer.clone(), &cmd).await?;
        let capabilities =
            GetCapabilitiesResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        let mut event_ids = vec![];
        for event_id in capabilities.event_ids() {
            event_ids.push(NotificationEventId::try_from(event_id)?);
        }
        Ok(event_ids)
    }
}

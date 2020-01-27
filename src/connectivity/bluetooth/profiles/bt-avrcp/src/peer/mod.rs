// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    bt_avctp::{
        AvcCommand, AvcCommandResponse, AvcCommandType, AvcOpCode, AvcPacketType, AvcPeer,
        AvcResponseType, Error as AvctpError,
    },
    fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_avrcp::{AvcPanelCommand, MediaAttributes, PlayStatus},
    fidl_fuchsia_bluetooth_bredr::PSM_AVCTP,
    fuchsia_async as fasync,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_vlog},
    futures::{
        self,
        channel::mpsc,
        future::{AbortHandle, Abortable, FutureExt},
        stream::{FusedStream, SelectAll, StreamExt, TryStreamExt},
        Future, Stream,
    },
    parking_lot::{Mutex, RwLock},
    pin_utils::pin_mut,
    std::{
        collections::HashMap,
        convert::TryFrom,
        mem::{discriminant, Discriminant},
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

mod controller;
mod handlers;
mod tasks;

use crate::{
    packets::{Error as PacketError, VendorCommand, *},
    peer_manager::TargetDelegate,
    profile::{AvrcpService, ProfileService},
    types::StateChangeListener,
    types::{PeerError as Error, PeerId},
};

pub use controller::{Controller, ControllerEvent, ControllerEventStream};
use handlers::ControlChannelHandler;

#[derive(Debug, PartialEq)]
pub enum PeerChannel<T> {
    Connected(Arc<T>),
    Connecting,
    Disconnected,
}

impl<T> PeerChannel<T> {
    pub fn connection(&self) -> Option<&Arc<T>> {
        match self {
            PeerChannel::Connected(t) => Some(&t),
            _ => None,
        }
    }
}

/// Internal object to manage a remote peer
#[derive(Debug)]
struct RemotePeer {
    peer_id: PeerId,

    /// Contains the remote peer's target profile.
    target_descriptor: Option<AvrcpService>,

    /// Contains the remote peer's controller profile.
    controller_descriptor: Option<AvrcpService>,

    /// Control channel to the remote device.
    control_channel: PeerChannel<AvcPeer>,

    /// Profile service. Used by RemotePeer to make outgoing L2CAP connections.
    profile_svc: Arc<Box<dyn ProfileService + Send + Sync>>,

    /// All stream listeners obtained by any `Controller`s around this peer that are listening for
    /// events from this peer.
    controller_listeners: Vec<mpsc::Sender<ControllerEvent>>,

    /// Processes commands received as AVRCP target and holds state for continuations and requested
    /// notifications for the control channel.
    command_handler: Arc<Mutex<ControlChannelHandler>>,

    /// Used to signal state changes and to notify and wake the state change observer currently
    /// processing this peer.
    state_change_listener: StateChangeListener,

    /// Set true to let the state watcher know that it should attempt to make outgoing l2cap
    /// connection to the peer. Set to false after a failed connection attempt so that we don't
    /// attempt to connect again immediately.
    attempt_connection: bool,

    /// Most recent notification values from the peer. Used to notify new controller listeners to
    /// the current state of the peer.
    notification_cache: HashMap<Discriminant<ControllerEvent>, ControllerEvent>,
}

impl RemotePeer {
    fn new(
        peer_id: PeerId,
        target_delegate: Arc<TargetDelegate>,
        profile_svc: Arc<Box<dyn ProfileService + Send + Sync>>,
    ) -> RemotePeer {
        Self {
            peer_id: peer_id.clone(),
            target_descriptor: None,
            controller_descriptor: None,
            control_channel: PeerChannel::Disconnected,
            controller_listeners: Vec::new(),
            profile_svc,
            command_handler: Arc::new(Mutex::new(ControlChannelHandler::new(
                &peer_id,
                target_delegate,
            ))),
            state_change_listener: StateChangeListener::new(),
            attempt_connection: true,
            notification_cache: HashMap::new(),
        }
    }

    /// Caches the current value of this controller notification event for future controller event
    /// listeners and forwards the event to current controller listeners queues.
    fn handle_new_controller_notification_event(&mut self, event: ControllerEvent) {
        self.notification_cache.insert(discriminant(&event), event.clone());

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

    fn connected(&self) -> bool {
        match self.control_channel {
            PeerChannel::Connected(_) => true,
            _ => false,
        }
    }

    /// Reset all known state about the remote peer to default values.
    fn reset_peer_state(&mut self) {
        fx_vlog!(tag: "avrcp", 2, "reset_peer_state {:?}", self.peer_id);
        self.notification_cache.clear();
        self.command_handler.lock().reset();
    }

    /// `attempt_reconnection` will cause state_watcher to attempt to make an outgoing connection when
    /// woken.
    fn reset_connection(&mut self, attempt_reconnection: bool) {
        fx_vlog!(tag: "avrcp", 2, "reset_connection {:?}", self.peer_id);
        self.reset_peer_state();
        self.control_channel = PeerChannel::Disconnected;
        self.attempt_connection = attempt_reconnection;
        self.wake_state_watcher();
    }

    fn control_connection(&mut self) -> Result<Arc<AvcPeer>, Error> {
        // if we are not connected, try to reconnect the next time we want to send a command.
        if !self.connected() {
            self.attempt_connection = true;
            self.wake_state_watcher();
        }

        match self.control_channel.connection() {
            Some(peer) => Ok(peer.clone()),
            None => Err(Error::RemoteNotFound),
        }
    }

    fn set_control_connection(&mut self, peer: AvcPeer) {
        fx_vlog!(tag: "avrcp", 2, "set_control_connection {:?}", self.peer_id);
        self.reset_peer_state();
        self.control_channel = PeerChannel::Connected(Arc::new(peer));
        self.wake_state_watcher();
    }

    fn set_target_descriptor(&mut self, service: AvrcpService) {
        fx_vlog!(tag: "avrcp", 2, "set_target_descriptor {:?}", self.peer_id);
        self.target_descriptor = Some(service);
        self.attempt_connection = true;
        self.wake_state_watcher();
    }

    fn set_controller_descriptor(&mut self, service: AvrcpService) {
        fx_vlog!(tag: "avrcp", 2, "set_controller_descriptor {:?}", self.peer_id);
        self.controller_descriptor = Some(service);
        self.attempt_connection = true;
        self.wake_state_watcher();
    }

    fn wake_state_watcher(&self) {
        fx_vlog!(tag: "avrcp", 2, "wake_state_watcher {:?}", self.peer_id);
        self.state_change_listener.state_changed();
    }
}

impl Drop for RemotePeer {
    fn drop(&mut self) {
        // Stop any stream processors that are currently running on this remote peer.
        self.state_change_listener.terminate();
    }
}

async fn send_vendor_dependent_command_internal(
    peer: Arc<RwLock<RemotePeer>>,
    command: &(impl VendorDependent + VendorCommand),
) -> Result<Vec<u8>, Error> {
    let avc_peer = peer.write().control_connection()?;
    let mut buf = vec![];
    let packet = command.encode_packet().expect("unable to encode packet");
    let mut stream = avc_peer.send_vendor_dependent_command(command.command_type(), &packet[..])?;

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
                (AvcResponseType::ImplementedStable, AvcCommandType::Status) => break response.1,
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

/// Retrieve the events supported by the peer by issuing a GetCapabilities command.
async fn get_supported_events_internal(
    peer: Arc<RwLock<RemotePeer>>,
) -> Result<Vec<NotificationEventId>, Error> {
    let cmd = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::EventsId);
    fx_vlog!(tag: "avrcp", 1, "get_capabilities(events) send command {:#?}", cmd);
    let buf = send_vendor_dependent_command_internal(peer.clone(), &cmd).await?;
    let capabilities =
        GetCapabilitiesResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
    let mut event_ids = vec![];
    for event_id in capabilities.event_ids() {
        event_ids.push(NotificationEventId::try_from(event_id)?);
    }
    Ok(event_ids)
}

#[derive(Debug, Clone)]
pub struct RemotePeerHandle {
    peer: Arc<RwLock<RemotePeer>>,
}

impl RemotePeerHandle {
    /// Create a remote peer and spawns the state watcher tasks around it.
    /// Should only be called by peer manager.
    pub fn spawn_peer(
        peer_id: PeerId,
        target_delegate: Arc<TargetDelegate>,
        profile_svc: Arc<Box<dyn ProfileService + Send + Sync>>,
    ) -> RemotePeerHandle {
        let remote_peer =
            Arc::new(RwLock::new(RemotePeer::new(peer_id, target_delegate, profile_svc)));

        fasync::spawn(tasks::state_watcher(remote_peer.clone()));

        RemotePeerHandle { peer: remote_peer }
    }

    pub fn set_control_connection(&self, peer: AvcPeer) {
        self.peer.write().set_control_connection(peer);
    }

    pub fn set_target_descriptor(&self, service: AvrcpService) {
        self.peer.write().set_target_descriptor(service);
    }

    pub fn set_controller_descriptor(&self, service: AvrcpService) {
        self.peer.write().set_controller_descriptor(service);
    }

    pub fn is_connected(&self) -> bool {
        self.peer.read().connected()
    }

    /// Sends a single passthrough keycode over the control channel.
    pub fn send_avc_passthrough<'a>(
        &self,
        payload: &'a [u8; 2],
    ) -> impl Future<Output = Result<(), Error>> + 'a {
        let peer_id = self.peer.read().peer_id.clone();
        let avc_peer_result = self.peer.write().control_connection();
        async move {
            let avc_peer = avc_peer_result?;
            let response = avc_peer.send_avc_passthrough_command(payload).await;
            match response {
                Ok(AvcCommandResponse(AvcResponseType::Accepted, _)) => Ok(()),
                Ok(AvcCommandResponse(AvcResponseType::Rejected, _)) => {
                    fx_log_info!("avrcp command rejected {}: {:?}", peer_id, response);
                    Err(Error::CommandNotSupported)
                }
                Err(e) => {
                    fx_log_err!("error sending avc command to {}: {:?}", peer_id, e);
                    Err(Error::CommandFailed)
                }
                _ => {
                    fx_log_err!(
                        "error sending avc command. unhandled response {}: {:?}",
                        peer_id,
                        response
                    );
                    Err(Error::CommandFailed)
                }
            }
        }
    }

    /// Send a generic vendor dependent command and returns the result as a future.
    /// This method encodes the `command` packet, awaits and decodes all responses, will issue
    /// continuation commands for incomplete responses (eg "get_element_attributes" command), and
    /// will return a result of the decoded packet or an error for any non stable response received
    pub fn send_vendor_dependent_command<'a>(
        &self,
        command: &'a (impl VendorDependent + VendorCommand),
    ) -> impl Future<Output = Result<Vec<u8>, Error>> + 'a {
        send_vendor_dependent_command_internal(self.peer.clone(), command)
    }

    /// Retrieve the events supported by the peer by issuing a GetCapabilities command.
    pub fn get_supported_events(
        &self,
    ) -> impl Future<Output = Result<Vec<NotificationEventId>, Error>> + '_ {
        get_supported_events_internal(self.peer.clone())
    }

    /// Adds new controller listener to this remote peer. The controller listener is immediately
    /// sent the current state of all notification values.
    pub fn add_control_listener(&self, mut sender: mpsc::Sender<ControllerEvent>) {
        let mut peer_guard = self.peer.write();
        for (_, event) in &peer_guard.notification_cache {
            if let Err(send_error) = sender.try_send(event.clone()) {
                fx_log_err!(
                    "unable to send event to peer controller stream for {} {:?}",
                    peer_guard.peer_id,
                    send_error
                );
            }
        }
        peer_guard.controller_listeners.push(sender)
    }

    /// Used by peer manager to get
    pub fn get_controller(&self) -> Controller {
        Controller::new(self.clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profile::{AvcrpTargetFeatures, AvrcpProtocolVersion};
    use anyhow::Error;
    use fuchsia_zircon as zx;

    #[fuchsia_async::run_singlethreaded(test)]
    // Check that the remote will attempt to connect to a peer if we have a profile.
    async fn trigger_connection_test() -> Result<(), Error> {
        let (local, remote) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let (profile_service, _) = crate::tests::MockProfileService::new(None, Some(Ok(local)));

        let target_delegate = Arc::new(TargetDelegate::new());

        let remote_peer = AvcPeer::new(remote)?;

        let remote_command_stream = remote_peer.take_command_stream().fuse();
        pin_mut!(remote_command_stream);

        let peer_handle = RemotePeerHandle::spawn_peer(
            "foobarbaz".to_string(),
            target_delegate.clone(),
            Arc::new(Box::new(profile_service)),
        );

        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvcrpTargetFeatures::CATEGORY1,
            psm: PSM_AVCTP as u16,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_connected());

        loop {
            futures::select! {
                command = remote_command_stream.select_next_some() => {
                    // we got a command!
                    assert!(peer_handle.is_connected());
                    return Ok(());
                }
            }
        }
    }
}

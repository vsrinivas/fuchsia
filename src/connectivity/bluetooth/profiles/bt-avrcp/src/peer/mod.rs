// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    bt_avctp::{
        AvcCommand, AvcCommandResponse, AvcCommandType, AvcOpCode, AvcPacketType, AvcPeer,
        AvcResponseType, AvctpPeer, Error as AvctpError,
    },
    fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_avrcp::{AvcPanelCommand, MediaAttributes, PlayStatus},
    fidl_fuchsia_bluetooth_bredr::{ProfileProxy, PSM_AVCTP},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_zircon as zx,
    futures::{
        self,
        channel::mpsc,
        future::{AbortHandle, Abortable, FutureExt},
        stream::{FusedStream, SelectAll, StreamExt, TryStreamExt},
        Future, Stream,
    },
    log::{error, info, trace},
    parking_lot::RwLock,
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
    packets::{Error as PacketError, *},
    peer_manager::TargetDelegate,
    profile::AvrcpService,
    types::PeerError as Error,
    types::StateChangeListener,
};

pub use controller::{Controller, ControllerEvent, ControllerEventStream};
use handlers::{browse_channel::BrowseChannelHandler, ControlChannelHandler};

/// The minimum amount of time to wait before establishing an AVCTP connection.
/// This is used during connection establishment when both devices attempt to establish
/// a connection at the same time.
/// See AVRCP 1.6.2, Section 4.1.1 for more details.
const MIN_CONNECTION_EST_TIME: zx::Duration = zx::Duration::from_millis(100);

/// The maximum amount of time to wait before establishing an AVCTP connection.
/// This is used during connection establishment when both devices attempt to establish
/// a connection at the same time.
/// See AVRCP 1.6.2, Section 4.1.1 for more details.
const MAX_CONNECTION_EST_TIME: zx::Duration = zx::Duration::from_millis(1000);

/// Arbitrary threshold amount of time used to determine if two connections were
/// established at "the same time".
/// This was chosen in between the `MIN_CONNECTION_EST_TIME` and `MAX_CONNECTION_EST_TIME`
/// to account for radio delay and other factors that impede connection establishment.
const CONNECTION_THRESHOLD: zx::Duration = zx::Duration::from_millis(750);

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

    /// Browse channel to the remote device.
    browse_channel: PeerChannel<AvctpPeer>,

    /// Profile service. Used by RemotePeer to make outgoing L2CAP connections.
    profile_proxy: ProfileProxy,

    /// All stream listeners obtained by any `Controller`s around this peer that are listening for
    /// events from this peer.
    controller_listeners: Vec<mpsc::Sender<ControllerEvent>>,

    /// Processes commands received as AVRCP target and holds state for continuations and requested
    /// notifications for the control channel.
    control_command_handler: ControlChannelHandler,

    /// Processes commands received as AVRCP target over the browse channel.
    browse_command_handler: BrowseChannelHandler,

    /// Used to signal state changes and to notify and wake the state change observer currently
    /// processing this peer.
    state_change_listener: StateChangeListener,

    /// Set true to let the state watcher know that it should attempt to make outgoing l2cap
    /// connection to the peer. Set to false after a failed connection attempt so that we don't
    /// attempt to connect again immediately.
    attempt_connection: bool,

    /// Set true to let the state watcher know that any outstanding `control_channel` processing
    /// tasks should be canceled and state cleaned up. Set to false after successfully canceling
    /// the tasks.
    cancel_tasks: bool,

    /// The timestamp of the last known control connection. Used to resolve simultaneous control
    /// channel connections.
    last_connected_time: Option<fasync::Time>,

    /// Most recent notification values from the peer. Used to notify new controller listeners to
    /// the current state of the peer.
    notification_cache: HashMap<Discriminant<ControllerEvent>, ControllerEvent>,
}

impl RemotePeer {
    fn new(
        peer_id: PeerId,
        target_delegate: Arc<TargetDelegate>,
        profile_proxy: ProfileProxy,
    ) -> RemotePeer {
        Self {
            peer_id: peer_id.clone(),
            target_descriptor: None,
            controller_descriptor: None,
            control_channel: PeerChannel::Disconnected,
            browse_channel: PeerChannel::Disconnected,
            controller_listeners: Vec::new(),
            profile_proxy,
            control_command_handler: ControlChannelHandler::new(&peer_id, target_delegate.clone()),
            browse_command_handler: BrowseChannelHandler::new(target_delegate),
            state_change_listener: StateChangeListener::new(),
            attempt_connection: true,
            cancel_tasks: false,
            last_connected_time: None,
            notification_cache: HashMap::new(),
        }
    }

    fn id(&self) -> PeerId {
        self.peer_id
    }

    /// Caches the current value of this controller notification event for future controller event
    /// listeners and forwards the event to current controller listeners queues.
    fn handle_new_controller_notification_event(&mut self, event: ControllerEvent) {
        self.notification_cache.insert(discriminant(&event), event.clone());

        // remove all the dead listeners from the list.
        self.controller_listeners.retain(|i| !i.is_closed());
        for sender in self.controller_listeners.iter_mut() {
            if let Err(send_error) = sender.try_send(event.clone()) {
                error!(
                    "unable to send event to peer controller stream for {} {:?}",
                    self.peer_id, send_error
                );
            }
        }
    }

    fn control_connected(&self) -> bool {
        match self.control_channel {
            PeerChannel::Connected(_) => true,
            _ => false,
        }
    }

    /// Reset all known state about the remote peer to default values.
    fn reset_peer_state(&mut self) {
        trace!("reset_peer_state {:?}", self.peer_id);
        self.notification_cache.clear();
        self.browse_command_handler.reset();
        self.control_command_handler.reset();
    }

    /// `attempt_reconnection` will cause state_watcher to attempt to make an outgoing connection when
    /// woken.
    fn reset_connection(&mut self, attempt_reconnection: bool) {
        trace!("reset_connection {:?}", self.peer_id);
        self.reset_peer_state();
        self.browse_channel = PeerChannel::Disconnected;
        self.control_channel = PeerChannel::Disconnected;
        self.attempt_connection = attempt_reconnection;
        self.cancel_tasks = true;
        self.last_connected_time = None;
        self.wake_state_watcher();
    }

    fn control_connection(&mut self) -> Result<Arc<AvcPeer>, Error> {
        // if we are not connected, try to reconnect the next time we want to send a command.
        if !self.control_connected() {
            self.attempt_connection = true;
            self.wake_state_watcher();
        }

        match self.control_channel.connection() {
            Some(peer) => Ok(peer.clone()),
            None => Err(Error::RemoteNotFound),
        }
    }

    fn set_control_connection(&mut self, peer: AvcPeer) {
        let current_time = fasync::Time::now();
        trace!("set_control_connection {:?} at {}", self.peer_id, current_time.into_nanos());

        // If the current connection establishment is within a threshold amount of time from the
        // most recent connection establishment, both connections should be dropped, and should
        // wait a random amount of time before re-establishment.
        if let Some(previous_time) = self.last_connected_time.take() {
            let diff = (current_time - previous_time).into_nanos().abs();
            if diff < CONNECTION_THRESHOLD.into_nanos() {
                trace!("Control connection establishment collision. Time diff: {}", diff);
                self.reset_connection(true);
                return;
            }
        }

        self.reset_peer_state();
        self.last_connected_time = Some(current_time);
        self.control_channel = PeerChannel::Connected(Arc::new(peer));
        self.cancel_tasks = true;
        self.wake_state_watcher();
    }

    fn set_browse_connection(&mut self, peer: AvctpPeer) {
        trace!("set_browse_connection {:?}", self.peer_id);
        let browse_peer = Arc::new(peer);
        self.browse_channel = PeerChannel::Connected(browse_peer);
        self.wake_state_watcher();
    }

    fn set_target_descriptor(&mut self, service: AvrcpService) {
        trace!("set_target_descriptor {:?}", self.peer_id);
        self.target_descriptor = Some(service);
        self.attempt_connection = true;
        self.wake_state_watcher();
    }

    fn set_controller_descriptor(&mut self, service: AvrcpService) {
        trace!("set_controller_descriptor {:?}", self.peer_id);
        self.controller_descriptor = Some(service);
        self.attempt_connection = true;
        self.wake_state_watcher();
    }

    fn wake_state_watcher(&self) {
        trace!("wake_state_watcher {:?}", self.peer_id);
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
    command: &(impl VendorDependentPdu + PacketEncodable + VendorCommand),
) -> Result<Vec<u8>, Error> {
    let avc_peer = peer.write().control_connection()?;
    let mut buf = vec![];
    let packet = command.encode_packet().expect("unable to encode packet");
    let mut stream = avc_peer.send_vendor_dependent_command(command.command_type(), &packet[..])?;

    loop {
        let response = loop {
            let result = stream.next().await.ok_or(Error::CommandFailed)?;
            let response: AvcCommandResponse = result.map_err(|e| Error::AvctpError(e))?;
            trace!("vendor response {:?}", response);
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
                info!("Unable to parse vendor dependent preamble: {:?}", e);
                return Err(Error::PacketError(e));
            }
        };

        let command = RequestContinuingResponseCommand::new(&command.pdu_id());
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
    trace!("get_capabilities(events) send command {:?}", cmd);
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
        profile_proxy: ProfileProxy,
    ) -> RemotePeerHandle {
        let remote_peer =
            Arc::new(RwLock::new(RemotePeer::new(peer_id, target_delegate, profile_proxy)));

        fasync::Task::spawn(tasks::state_watcher(remote_peer.clone())).detach();

        RemotePeerHandle { peer: remote_peer }
    }

    pub fn set_control_connection(&self, peer: AvcPeer) {
        self.peer.write().set_control_connection(peer);
    }

    pub fn set_browse_connection(&self, peer: AvctpPeer) {
        self.peer.write().set_browse_connection(peer);
    }

    pub fn set_target_descriptor(&self, service: AvrcpService) {
        self.peer.write().set_target_descriptor(service);
    }

    pub fn set_controller_descriptor(&self, service: AvrcpService) {
        self.peer.write().set_controller_descriptor(service);
    }

    pub fn is_connected(&self) -> bool {
        self.peer.read().control_connected()
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
                    info!("avrcp command rejected {:?}: {:?}", peer_id, response);
                    Err(Error::CommandNotSupported)
                }
                Err(e) => {
                    error!("error sending avc command to {:?}: {:?}", peer_id, e);
                    Err(Error::CommandFailed)
                }
                _ => {
                    error!(
                        "error sending avc command. unhandled response {:?}: {:?}",
                        peer_id, response
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
        command: &'a (impl PacketEncodable + VendorCommand),
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
                error!(
                    "unable to send event to peer controller stream for {} {:?}",
                    peer_guard.peer_id, send_error
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
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileRequest, ProfileRequestStream};
    use fuchsia_async::{self as fasync, DurationExt, TimeoutExt};
    use fuchsia_bluetooth::types::Channel;
    use fuchsia_zircon::{self as zx, DurationNum};
    use futures::{pin_mut, task::Poll};
    use std::convert::TryInto;

    fn setup_remote_peer(
        id: PeerId,
    ) -> Result<(RemotePeerHandle, Arc<TargetDelegate>, ProfileRequestStream), Error> {
        let (profile_proxy, profile_requests) = create_proxy_and_stream::<ProfileMarker>()?;
        let target_delegate = Arc::new(TargetDelegate::new());
        let peer_handle = RemotePeerHandle::spawn_peer(id, target_delegate.clone(), profile_proxy);

        Ok((peer_handle, target_delegate, profile_requests))
    }

    // Check that the remote will attempt to connect to a peer if we have a profile.
    #[test]
    fn trigger_connection_test() -> Result<(), Error> {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(1);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvcrpTargetFeatures::CATEGORY1,
            psm: PSM_AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_connected());

        let next_request_fut =
            profile_requests.next().on_timeout(1005.millis().after_now(), || None);
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        exec.wake_expired_timers();

        // Peer should have requested a connection.
        let (_remote, channel) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should be put in place.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_connected());
        Ok(())
    }

    /// Tests initial connection establishment to a peer.
    /// Tests peer reconnection correctly terminates the old processing task, including the
    /// underlying channel, and spawns a new task to handle incoming requests.
    #[test]
    fn test_peer_reconnection() -> Result<(), Error> {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(123);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvcrpTargetFeatures::CATEGORY1,
            psm: PSM_AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        exec.wake_expired_timers();

        // Peer should have requested a connection.
        let (remote, channel) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // Peer should be connected.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_connected());

        // Should be able to send data over the channel.
        match remote.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected data write but got {:?} instead", x),
        }

        // Advance time by some arbitrary amount before peer decides to reconnect.
        exec.set_fake_time(5.seconds().after_now());
        exec.wake_expired_timers();

        // Peer reconnects with a new l2cap connection. Keep the old one alive to validate that it's
        // closed.
        let (remote2, channel2) = Channel::create();
        let reconnect_peer = AvcPeer::new(channel2);
        peer_handle.set_control_connection(reconnect_peer);

        // Run to update watcher state. Peer should be connected.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_connected());

        // Shouldn't be able to send data over the old channel.
        match remote.as_ref().write(&[0; 1]) {
            Err(zx::Status::PEER_CLOSED) => {}
            x => panic!("Expected PEER_CLOSED but got {:?}", x),
        }

        // Should be able to send data over the new channel.
        match remote2.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected data write but got {:?} instead", x),
        }

        Ok(())
    }

    /// Tests that when inbound and outbound connections are established at the same
    /// time, AVRCP drops both, and attempts to reconnect.
    #[test]
    fn test_simultaneous_peer_connections() -> Result<(), Error> {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(123);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvcrpTargetFeatures::CATEGORY1,
            psm: PSM_AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        exec.wake_expired_timers();

        // We expect to initiate an outbound connection through the profile server.
        let (remote, channel) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // The inbound connection is accepted (since there were no previous connections).
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_connected());

        // Should be able to send data over the channel.
        match remote.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected data write but got {:?} instead", x),
        }

        // Advance time by LESS than the CONNECTION_THRESHOLD amount.
        let advance_time = CONNECTION_THRESHOLD.into_nanos() - 100;
        exec.set_fake_time(advance_time.nanos().after_now());
        exec.wake_expired_timers();

        // Simulate inbound connection.
        let (remote2, channel2) = Channel::create();
        let reconnect_peer = AvcPeer::new(channel2);
        peer_handle.set_control_connection(reconnect_peer);

        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // Both the inbound and outbound-initiated channels should be dropped.
        // Sending data should not work.
        match remote.as_ref().write(&[0; 1]) {
            Err(zx::Status::PEER_CLOSED) => {}
            x => panic!("Expected PEER_CLOSED but got {:?}", x),
        }
        match remote2.as_ref().write(&[0; 1]) {
            Err(zx::Status::PEER_CLOSED) => {}
            x => panic!("Expected PEER_CLOSED but got {:?}", x),
        }

        // We expect to attempt to reconnect.
        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        exec.wake_expired_timers();

        let (remote3, channel3) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel3.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_connected());

        // New channel should be good to go.
        match remote3.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected successful write but got {:?}", x),
        }

        Ok(())
    }
}

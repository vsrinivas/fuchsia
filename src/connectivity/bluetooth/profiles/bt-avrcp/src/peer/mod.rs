// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::{AvcCommandResponse, AvcCommandType, AvcPeer, AvcResponseType, AvctpPeer},
    derivative::Derivative,
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::{profile::Psm, types::PeerId},
    fuchsia_inspect::Property,
    fuchsia_inspect_derive::{AttachError, Inspect},
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future::FutureExt, stream::StreamExt, Future},
    packet_encoding::{Decodable, Encodable},
    parking_lot::RwLock,
    pin_utils::pin_mut,
    std::{
        collections::{HashMap, HashSet},
        convert::TryFrom,
        mem::{discriminant, Discriminant},
        sync::Arc,
    },
    tracing::{info, trace, warn},
};

mod controller;
mod handlers;
mod inspect;
mod tasks;

use crate::{
    metrics::MetricsNode,
    packets::{Error as PacketError, *},
    peer_manager::TargetDelegate,
    profile::AvrcpService,
    types::PeerError as Error,
    types::StateChangeListener,
};

pub use controller::{Controller, ControllerEvent, ControllerEventStream};
use handlers::{browse_channel::BrowseChannelHandler, ControlChannelHandler};
use inspect::RemotePeerInspect;

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
pub enum PeerChannelState<T> {
    Connected(Arc<T>),
    Connecting,
    Disconnected,
}

#[derive(Debug)]
pub struct PeerChannel<T> {
    /// The state of this channel.
    state: PeerChannelState<T>,
    inspect: fuchsia_inspect::StringProperty,
}

impl<T> PeerChannel<T> {
    pub fn connected(&mut self, channel: Arc<T>) {
        self.state = PeerChannelState::Connected(channel);
        self.inspect.set("Connected");
    }

    pub fn connecting(&mut self) {
        self.state = PeerChannelState::Connecting;
        self.inspect.set("Connecting");
    }

    pub fn is_connecting(&self) -> bool {
        matches!(self.state, PeerChannelState::Connecting)
    }

    pub fn disconnect(&mut self) {
        self.state = PeerChannelState::Disconnected;
        self.inspect.set("Disconnected");
    }

    pub fn state(&self) -> &PeerChannelState<T> {
        &self.state
    }

    pub fn connection(&self) -> Option<&Arc<T>> {
        match &self.state {
            PeerChannelState::Connected(t) => Some(t),
            _ => None,
        }
    }
}

impl<T> Default for PeerChannel<T> {
    fn default() -> Self {
        Self {
            state: PeerChannelState::Disconnected,
            inspect: fuchsia_inspect::StringProperty::default(),
        }
    }
}

impl<T> Inspect for &mut PeerChannel<T> {
    fn iattach(
        self,
        parent: &fuchsia_inspect::Node,
        name: impl AsRef<str>,
    ) -> Result<(), AttachError> {
        self.inspect = parent.create_string(name.as_ref(), "Disconnected");
        Ok(())
    }
}

/// Internal object to manage a remote peer
#[derive(Derivative)]
#[derivative(Debug)]
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
    profile_proxy: bredr::ProfileProxy,

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

    /// The inspect node for this peer.
    #[derivative(Debug = "ignore")]
    inspect: RemotePeerInspect,
}

impl Inspect for &mut RemotePeer {
    fn iattach(
        self,
        parent: &fuchsia_inspect::Node,
        name: impl AsRef<str>,
    ) -> Result<(), AttachError> {
        self.inspect.iattach(parent, name.as_ref())?;
        self.control_channel.iattach(&self.inspect.node(), "control")?;
        self.browse_channel.iattach(&self.inspect.node(), "browse")?;
        Ok(())
    }
}

impl RemotePeer {
    fn new(
        peer_id: PeerId,
        target_delegate: Arc<TargetDelegate>,
        profile_proxy: bredr::ProfileProxy,
    ) -> RemotePeer {
        Self {
            peer_id: peer_id.clone(),
            target_descriptor: None,
            controller_descriptor: None,
            control_channel: PeerChannel::default(),
            browse_channel: PeerChannel::default(),
            controller_listeners: Vec::new(),
            profile_proxy,
            control_command_handler: ControlChannelHandler::new(&peer_id, target_delegate.clone()),
            browse_command_handler: BrowseChannelHandler::new(target_delegate),
            state_change_listener: StateChangeListener::new(),
            attempt_connection: true,
            cancel_tasks: false,
            last_connected_time: None,
            notification_cache: HashMap::new(),
            inspect: RemotePeerInspect::new(peer_id),
        }
    }

    fn id(&self) -> PeerId {
        self.peer_id
    }

    fn inspect(&self) -> &RemotePeerInspect {
        &self.inspect
    }

    /// Returns true if this peer is considered discovered, i.e. has service descriptors for
    /// either controller or target.
    fn discovered(&self) -> bool {
        self.target_descriptor.is_some() || self.controller_descriptor.is_some()
    }

    /// Returns the L2CAP PSM associated with the AVRCP service for this peer. Defaults to PSM_AVCTP
    /// if the peer advertises both Controller & Target services.
    fn service_psm(&self) -> Psm {
        match (&self.target_descriptor, &self.controller_descriptor) {
            (Some(AvrcpService::Target { psm, .. }), None) => *psm,
            (None, Some(AvrcpService::Controller { psm, .. })) => *psm,
            _ => {
                info!("Defaulting to PSM_AVCTP");
                Psm::AVCTP
            }
        }
    }

    /// Caches the current value of this controller notification event for future controller event
    /// listeners and forwards the event to current controller listeners queues.
    fn handle_new_controller_notification_event(&mut self, event: ControllerEvent) {
        let _ = self.notification_cache.insert(discriminant(&event), event.clone());

        // remove all the dead listeners from the list.
        self.controller_listeners.retain(|i| !i.is_closed());
        for sender in self.controller_listeners.iter_mut() {
            if let Err(send_error) = sender.try_send(event.clone()) {
                warn!(
                    "Error sending event to listener for peer {}: {:?}",
                    self.peer_id, send_error
                );
            }
        }
    }

    fn control_connected(&self) -> bool {
        self.control_channel.connection().is_some()
    }

    /// Reset all known state about the remote peer to default values.
    fn reset_peer_state(&mut self) {
        trace!("Resetting peer state for {}", self.peer_id);
        self.notification_cache.clear();
        self.browse_command_handler.reset();
        self.control_command_handler.reset();
    }

    /// `attempt_reconnection` will cause state_watcher to attempt to make an outgoing connection when
    /// woken.
    fn reset_connection(&mut self, attempt_reconnection: bool) {
        info!(
            "Disconnecting peer {}, will {}attempt to reconnect",
            self.peer_id,
            if attempt_reconnection { "" } else { "not " }
        );
        self.reset_peer_state();
        self.browse_channel.disconnect();
        self.control_channel.disconnect();
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
        trace!("Set control connection for {} at: {}", self.peer_id, current_time.into_nanos());

        // If the current connection establishment is within a threshold amount of time from the
        // most recent connection establishment, both connections should be dropped, and should
        // wait a random amount of time before re-establishment.
        if let Some(previous_time) = self.last_connected_time.take() {
            let diff = (current_time - previous_time).into_nanos().abs();
            if diff < CONNECTION_THRESHOLD.into_nanos() {
                trace!(
                    "Collision in control connection establishment for {}. Time diff: {}",
                    self.peer_id,
                    diff
                );
                self.inspect.metrics().control_collision();
                self.reset_connection(true);
                return;
            }
        }

        self.reset_peer_state();
        info!("{} connected control channel", self.peer_id);
        self.last_connected_time = Some(current_time);
        self.control_channel.connected(Arc::new(peer));
        self.cancel_tasks = true;
        self.inspect.record_connected(current_time);
        self.wake_state_watcher();
    }

    fn set_browse_connection(&mut self, peer: AvctpPeer) {
        info!("{} connected browse channel", self.peer_id);
        self.browse_channel.connected(Arc::new(peer));
        self.inspect.metrics().browse_connection();
        self.wake_state_watcher();
    }

    fn set_target_descriptor(&mut self, service: AvrcpService) {
        trace!("Set target descriptor for {}", self.peer_id);
        self.target_descriptor = Some(service);
        self.attempt_connection = true;
        // Record inspect target features.
        self.inspect.record_target_features(service);
        self.wake_state_watcher();
    }

    fn set_controller_descriptor(&mut self, service: AvrcpService) {
        trace!("Set controller descriptor for {}", self.peer_id);
        self.controller_descriptor = Some(service);
        self.attempt_connection = true;
        // Record inspect controller features.
        self.inspect.record_controller_features(service);
        self.wake_state_watcher();
    }

    fn set_metrics_node(&mut self, node: MetricsNode) {
        self.inspect.set_metrics_node(node);
    }

    fn wake_state_watcher(&self) {
        trace!("Waking state watcher for {}", self.peer_id);
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
                warn!("Unable to parse vendor dependent preamble: {:?}", e);
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
) -> Result<HashSet<NotificationEventId>, Error> {
    let cmd = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::EventsId);
    trace!("Getting supported events: {:?}", cmd);
    let buf = send_vendor_dependent_command_internal(peer.clone(), &cmd).await?;
    let capabilities =
        GetCapabilitiesResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
    let mut event_ids = HashSet::new();
    for event_id in capabilities.event_ids() {
        let _ = event_ids.insert(NotificationEventId::try_from(event_id)?);
    }
    Ok(event_ids)
}

#[derive(Debug, Clone)]
pub struct RemotePeerHandle {
    peer: Arc<RwLock<RemotePeer>>,
}

impl Inspect for &mut RemotePeerHandle {
    fn iattach(
        self,
        parent: &fuchsia_inspect::Node,
        name: impl AsRef<str>,
    ) -> Result<(), AttachError> {
        self.peer.write().iattach(parent, name.as_ref())
    }
}

impl RemotePeerHandle {
    /// Create a remote peer and spawns the state watcher tasks around it.
    /// Should only be called by peer manager.
    pub fn spawn_peer(
        peer_id: PeerId,
        target_delegate: Arc<TargetDelegate>,
        profile_proxy: bredr::ProfileProxy,
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

    pub fn set_metrics_node(&self, node: MetricsNode) {
        self.peer.write().set_metrics_node(node);
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
                    info!("Command rejected for {:?}: {:?}", peer_id, response);
                    Err(Error::CommandNotSupported)
                }
                Err(e) => {
                    warn!("Error sending avc command to {:?}: {:?}", peer_id, e);
                    Err(Error::CommandFailed)
                }
                _ => {
                    warn!("Unhandled response for {:?}: {:?}", peer_id, response);
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
    ) -> impl Future<Output = Result<HashSet<NotificationEventId>, Error>> + '_ {
        get_supported_events_internal(self.peer.clone())
    }

    /// Adds new controller listener to this remote peer. The controller listener is immediately
    /// sent the current state of all notification values.
    pub fn add_control_listener(&self, mut sender: mpsc::Sender<ControllerEvent>) {
        let mut peer_guard = self.peer.write();
        for (_, event) in &peer_guard.notification_cache {
            if let Err(send_error) = sender.try_send(event.clone()) {
                warn!(
                    "Error sending cached event to listener for {}: {:?}",
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
    use crate::profile::{AvrcpProtocolVersion, AvrcpTargetFeatures};

    use {
        anyhow::Error,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_bluetooth::ErrorCode,
        fidl_fuchsia_bluetooth_bredr::{
            ConnectParameters, L2capParameters, ProfileMarker, ProfileRequest, ProfileRequestStream,
        },
        fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
        fuchsia_bluetooth::types::Channel,
        fuchsia_inspect::assert_data_tree,
        fuchsia_inspect_derive::WithInspect,
        fuchsia_zircon::DurationNum,
        futures::{pin_mut, task::Poll},
        std::convert::TryInto,
    };

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
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(1);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer - while unusual, this peer
        // advertises a non-standard PSM.
        let peer_psm = Psm::new(37); // OTS PSM
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1,
            psm: peer_psm,
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
        let _ = exec.wake_expired_timers();

        // We should have requested a connection.
        let (_remote, channel) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, connection, .. }))) => {
                let channel = channel.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
                // The connect request should be for the PSM advertised by the remote peer.
                match connection {
                    ConnectParameters::L2cap(L2capParameters { psm: Some(v), .. }) => {
                        assert_eq!(v, peer_psm.into());
                    }
                    x => panic!("Expected L2CAP parameters but got: {:?}", x),
                }
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
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(123);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer.
        let peer_psm = Psm::new(bredr::PSM_AVCTP);
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1,
            psm: peer_psm,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // Peer should have requested a connection.
        let (remote, channel) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, connection, .. }))) => {
                let channel = channel.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
                match connection {
                    ConnectParameters::L2cap(L2capParameters { psm: Some(v), .. }) => {
                        assert_eq!(v, peer_psm.into());
                    }
                    x => panic!("Expected L2CAP parameters but got: {:?}", x),
                }
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
        let _ = exec.wake_expired_timers();

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
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(123);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

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
        let _ = exec.wake_expired_timers();

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
        let _ = exec.wake_expired_timers();

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

    fn attach_inspect_with_metrics(
        peer: &mut RemotePeerHandle,
    ) -> (fuchsia_inspect::Inspector, MetricsNode) {
        let inspect = fuchsia_inspect::Inspector::new();
        let metrics_node = MetricsNode::default().with_inspect(inspect.root(), "metrics").unwrap();
        peer.iattach(inspect.root(), "peer").unwrap();
        peer.set_metrics_node(metrics_node.clone());
        (inspect, metrics_node)
    }

    #[test]
    fn outgoing_connection_error_updates_inspect() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(30789);
        let (mut peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;
        let (inspect, _metrics_node) = attach_inspect_with_metrics(&mut peer_handle);

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        let mut next_request_fut = Box::pin(profile_requests.next());
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We expect to initiate an outbound connection through the profile server. Simulate error.
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                responder.send(&mut Err(ErrorCode::Failed)).expect("Fidl response should be ok");
            }
            x => panic!("Expected ready profile connection, but got: {:?}", x),
        };

        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // Inspect tree should be updated with the connection error count.
        assert_data_tree!(inspect, root: {
            peer: contains {},
            metrics: contains {
                connection_errors: 1u64,
                control_connections: 0u64,
            }
        });
        Ok(())
    }

    #[test]
    fn successful_inbound_connection_updates_inspect_metrics() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let id = PeerId(842);
        let (mut peer_handle, _target_delegate, _profile_requests) = setup_remote_peer(id)?;
        let (inspect, _metrics_node) = attach_inspect_with_metrics(&mut peer_handle);

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });
        // Peer initiates connection to us.
        let (_remote, channel) = Channel::create();
        let peer = AvcPeer::new(channel);
        peer_handle.set_control_connection(peer);
        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // Inspect tree should be updated with the connection.
        assert_data_tree!(inspect, root: {
            peer: contains {
                control: "Connected",
            },
            metrics: contains {
                connection_errors: 0u64,
                control_connections: 1u64,
                browse_connections: 0u64,
            }
        });

        // Peer initiates a browse connection.
        let (_remote1, channel1) = Channel::create();
        let peer1 = AvctpPeer::new(channel1);
        peer_handle.set_browse_connection(peer1);
        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        // Inspect tree should be updated with the browse connection.
        assert_data_tree!(inspect, root: {
            peer: contains {
                control: "Connected",
            },
            metrics: contains {
                connection_errors: 0u64,
                control_connections: 1u64,
                browse_connections: 1u64,
            }
        });

        // Peer disconnects.
        drop(_remote);
        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        // Inspect tree should be updated with the disconnection.
        assert_data_tree!(inspect, root: {
            peer: contains {
                control: "Disconnected",
            },
            metrics: contains {
                connection_errors: 0u64,
                control_connections: 1u64,
                browse_connections: 1u64,
            }
        });
        Ok(())
    }
}

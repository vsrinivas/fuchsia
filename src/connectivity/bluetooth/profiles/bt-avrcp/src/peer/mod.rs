// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::{AvcCommandResponse, AvcCommandType, AvcPeer, AvcResponseType, AvctpPeer},
    derivative::Derivative,
    fidl_fuchsia_bluetooth, fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::{profile::Psm, types::Channel, types::PeerId},
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

pub use controller::{Controller, ControllerEvent};
pub use handlers::{browse_channel::BrowseChannelHandler, ControlChannelHandler};
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

#[derive(Copy, Clone, Debug)]
pub enum AVCTPConnectionType {
    Control,
    #[allow(dead_code)]
    Browse,
}

impl AVCTPConnectionType {
    pub fn psm(
        &self,
        controller_desc: &Option<AvrcpService>,
        target_desc: &Option<AvrcpService>,
    ) -> Psm {
        match self {
            AVCTPConnectionType::Control => match (target_desc, controller_desc) {
                (Some(AvrcpService::Target { psm, .. }), None) => *psm,
                (None, Some(AvrcpService::Controller { psm, .. })) => *psm,
                _ => {
                    info!("PSM for undiscovered peer, defaulting to PSM_AVCTP");
                    Psm::AVCTP
                }
            },
            AVCTPConnectionType::Browse => Psm::AVCTP_BROWSE,
        }
    }

    pub fn parameters(&self) -> bredr::ChannelParameters {
        // TODO(fxbug.dev/101260): set minimum MTU to 335.
        match self {
            AVCTPConnectionType::Control => {
                bredr::ChannelParameters { ..bredr::ChannelParameters::EMPTY }
            }
            AVCTPConnectionType::Browse => bredr::ChannelParameters {
                channel_mode: Some(bredr::ChannelMode::EnhancedRetransmission),
                ..bredr::ChannelParameters::EMPTY
            },
        }
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

    /// Set true to let the state watcher know that it should attempt to make
    /// outgoing l2cap connection to the peer for control channel. Set to false
    /// after a failed connection attempt so that we don't attempt to connect
    /// again immediately.
    attempt_control_connection: bool,

    /// Set true to let the state watcher know that it should attempt to make
    /// outgoing l2cap browse connection to the peer for browse channel. Set
    /// to false after a failed connection attempt so that we don't attempt to
    /// connect again immediately.
    attempt_browse_connection: bool,

    /// Set true to let the state watcher know that any outstanding `control_channel` processing
    /// tasks should be canceled and state cleaned up. Set to false after successfully canceling
    /// the tasks.
    cancel_control_task: bool,

    /// Set true to let the state watcher know that any outstanding `browse_channel` processing
    /// tasks should be canceled and state cleaned up. Set to false after successfully canceling
    /// the tasks.
    cancel_browse_task: bool,

    /// The timestamp of the last known control connection. Used to resolve simultaneous control
    /// channel connections.
    last_control_connected_time: Option<fasync::Time>,

    /// The timestamp of the last known browse connection. Used to resolve simultaneous browse
    /// channel connections.
    last_browse_connected_time: Option<fasync::Time>,

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
            attempt_control_connection: true,
            attempt_browse_connection: true,
            cancel_control_task: false,
            cancel_browse_task: false,
            last_control_connected_time: None,
            last_browse_connected_time: None,
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

    fn browse_connected(&self) -> bool {
        self.browse_channel.connection().is_some()
    }

    /// Reset all known state about the remote peer to default values.
    fn reset_peer_state(&mut self) {
        trace!("Resetting peer state for {}", self.peer_id);
        self.notification_cache.clear();
        self.browse_command_handler.reset();
        self.control_command_handler.reset();
    }

    /// Reset browse channel.
    fn reset_browse_connection(&mut self, attempt_reconnection: bool) {
        info!(
            "Disconnecting browse connection peer {}, will {}attempt to reconnect",
            self.peer_id,
            if attempt_reconnection { "" } else { "not " }
        );
        self.browse_command_handler.reset();
        self.browse_channel.disconnect();
        self.attempt_browse_connection = attempt_reconnection;
        self.cancel_browse_task = true;
        self.last_browse_connected_time = None;
        self.wake_state_watcher();
    }

    /// Reset both browse and control channels.
    /// `attempt_reconnection` will cause state_watcher to attempt to make an outgoing connection when
    /// woken.
    fn reset_connections(&mut self, attempt_reconnection: bool) {
        info!(
            "Disconnecting control connection to peer {}, will {}attempt to reconnect",
            self.peer_id,
            if attempt_reconnection { "" } else { "not " }
        );
        self.reset_peer_state();
        self.browse_channel.disconnect();
        self.control_channel.disconnect();
        self.attempt_control_connection = attempt_reconnection;
        self.attempt_browse_connection = attempt_reconnection;
        self.cancel_browse_task = true;
        self.cancel_control_task = true;
        self.last_control_connected_time = None;
        self.last_browse_connected_time = None;
        self.wake_state_watcher();
    }

    /// Method for initiating outbound connection request.
    /// Returns None if control/browse channel is not in connecting mode.
    pub fn connect(
        &mut self,
        conn_type: AVCTPConnectionType,
    ) -> Option<
        impl Future<
            Output = Result<Result<bredr::Channel, fidl_fuchsia_bluetooth::ErrorCode>, fidl::Error>,
        >,
    > {
        match conn_type {
            AVCTPConnectionType::Control => {
                if !self.control_channel.is_connecting() {
                    return None;
                }
            }
            AVCTPConnectionType::Browse => {
                if !self.browse_channel.is_connecting() {
                    return None;
                }
            }
        }

        // Depending on AVCTPConnectionType, define the L2CAP channel parameters
        // (basic for control, enhanced retransmission for browsing)
        return Some(self.profile_proxy.connect(
            &mut self.peer_id.into(),
            &mut bredr::ConnectParameters::L2cap(bredr::L2capParameters {
                psm: Some(
                    conn_type.psm(&self.controller_descriptor, &self.target_descriptor).into(),
                ),
                parameters: Some(conn_type.parameters()),
                ..bredr::L2capParameters::EMPTY
            }),
        ));
    }

    /// Called when outgoing L2CAP connection was successfully established.
    pub fn connected(&mut self, conn_type: AVCTPConnectionType, channel: Channel) {
        // Set up the appropriate connections. If an incoming l2cap connection
        // was made while we were making an outgoing one, reset the channel(s).
        match conn_type {
            AVCTPConnectionType::Control => {
                if self.control_channel.is_connecting() {
                    let peer = AvcPeer::new(channel);
                    return self.set_control_connection(peer);
                }
                self.reset_connections(true)
            }
            AVCTPConnectionType::Browse => {
                if self.browse_channel.is_connecting() {
                    let peer = AvctpPeer::new(channel);
                    return self.set_browse_connection(peer);
                }
                self.reset_browse_connection(true)
            }
        }
    }

    /// Called when outgoing L2CAP connection was not established successfully.
    /// When always_reset is true, connections are reset unconditionally.
    /// Otherwise, they are only reset if the channel was in `is_connecting`
    /// state.
    pub fn connect_failed(&mut self, conn_type: AVCTPConnectionType, always_reset: bool) {
        self.inspect().metrics().connection_error();
        match conn_type {
            AVCTPConnectionType::Control => {
                if always_reset || self.control_channel.is_connecting() {
                    self.reset_connections(false)
                }
            }
            AVCTPConnectionType::Browse => {
                if always_reset || self.browse_channel.is_connecting() {
                    self.reset_browse_connection(false)
                }
            }
        }
    }

    fn control_connection(&mut self) -> Result<Arc<AvcPeer>, Error> {
        // if we are not connected, try to reconnect the next time we want to send a command.
        if !self.control_connected() {
            self.attempt_control_connection = true;
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
        if let Some(previous_time) = self.last_control_connected_time.take() {
            let diff = (current_time - previous_time).into_nanos().abs();
            if diff < CONNECTION_THRESHOLD.into_nanos() {
                trace!(
                    "Collision in control connection establishment for {}. Time diff: {}",
                    self.peer_id,
                    diff
                );
                self.inspect.metrics().control_collision();
                self.reset_connections(true);
                return;
            }
        }

        if self.browse_connected() {
            // Browse channel was already established.
            // This indicates that this is a new control channel connection.
            // Reset pre-existing channels before setting a new control channel.
            self.reset_connections(false);
        } else {
            // Just reset peer state and cancel the current tasks.
            self.reset_peer_state();
            self.cancel_control_task = true;
        }

        info!("{} connected control channel", self.peer_id);
        self.last_control_connected_time = Some(current_time);
        self.control_channel.connected(Arc::new(peer));
        // Since control connection was newly established, allow browse
        // connection to be attempted.
        self.attempt_browse_connection = true;
        self.inspect.record_connected(current_time);
        self.wake_state_watcher();
    }

    fn browse_connection(&mut self) -> Result<Arc<AvctpPeer>, Error> {
        // If we are not connected, try to reconnect the next time we want to send a command.
        if !self.browse_connected() {
            if !self.control_connected() {
                self.attempt_control_connection = true;
                self.attempt_browse_connection = true;
            }
            self.wake_state_watcher();
        }

        match self.browse_channel.connection() {
            Some(peer) => Ok(peer.clone()),
            None => Err(Error::RemoteNotFound),
        }
    }

    fn set_browse_connection(&mut self, peer: AvctpPeer) {
        let current_time = fasync::Time::now();
        trace!("Set browse connection for {} at: {}", self.peer_id, current_time.into_nanos());

        // If the current connection establishment is within a threshold amount of time from the
        // most recent connection establishment, both connections should be dropped, and should
        // wait a random amount of time before re-establishment.
        if let Some(previous_time) = self.last_browse_connected_time.take() {
            let diff = (current_time - previous_time).into_nanos().abs();
            if diff < CONNECTION_THRESHOLD.into_nanos() {
                trace!(
                    "Collision in browse connection establishment for {}. Time diff: {}",
                    self.peer_id,
                    diff
                );
                self.inspect.metrics().browse_collision();
                self.reset_browse_connection(true);
                return;
            }
        }

        if self.control_connected() {
            info!("{} connected browse channel", self.peer_id);
            self.last_browse_connected_time = Some(current_time);
            self.browse_channel.connected(Arc::new(peer));
            self.inspect.metrics().browse_connection();
            self.wake_state_watcher();
            return;
        }

        // If control channel was not already established, don't set the
        // browse channel and instead reset all connections.
        self.reset_connections(true);
    }

    fn set_target_descriptor(&mut self, service: AvrcpService) {
        trace!("Set target descriptor for {}", self.peer_id);
        self.target_descriptor = Some(service);
        self.attempt_control_connection = true;
        // Record inspect target features.
        self.inspect.record_target_features(service);
        self.wake_state_watcher();
    }

    fn set_controller_descriptor(&mut self, service: AvrcpService) {
        trace!("Set controller descriptor for {}", self.peer_id);
        self.controller_descriptor = Some(service);
        self.attempt_control_connection = true;
        // Record inspect controller features.
        self.inspect.record_controller_features(service);
        self.wake_state_watcher();
    }

    fn supports_browsing(&self) -> bool {
        self.target_descriptor.map_or(false, |desc| desc.supports_browsing())
            || self.controller_descriptor.map_or(false, |desc| desc.supports_browsing())
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
            let response = stream.next().await.ok_or(Error::CommandFailed)??;
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

async fn send_browsing_command_internal(
    peer: Arc<RwLock<RemotePeer>>,
    pdu_id: u8,
    command: BrowsePreamble,
) -> Result<Vec<u8>, Error> {
    let avctp_peer = peer.write().browse_connection()?;

    let mut buf = vec![0; command.encoded_len()];
    let _ = command.encode(&mut buf[..])?;
    let mut stream = avctp_peer.send_command(&buf[..])?;

    // Wait for result.
    let result = stream.next().await.ok_or(Error::CommandFailed)?;
    let response = result.map_err(|e| Error::AvctpError(e))?;
    trace!("AVRCP response {:?}", response);

    match BrowsePreamble::decode(response.body()) {
        Ok(preamble) => {
            if preamble.pdu_id != pdu_id {
                return Err(Error::UnexpectedResponse);
            }
            Ok(preamble.body)
        }
        Err(e) => {
            warn!("Unable to parse browse preamble: {:?}", e);
            Err(Error::PacketError(e))
        }
    }
}

/// Retrieve the events supported by the peer by issuing a GetCapabilities command.
async fn get_supported_events_internal(
    peer: Arc<RwLock<RemotePeer>>,
) -> Result<HashSet<NotificationEventId>, Error> {
    let cmd = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::EventsId);
    trace!("Getting supported events: {:?}", cmd);
    let buf = send_vendor_dependent_command_internal(peer.clone(), &cmd).await?;
    let capabilities = GetCapabilitiesResponse::decode(&buf[..])?;
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

    pub fn is_control_connected(&self) -> bool {
        self.peer.read().control_connected()
    }

    pub fn is_browse_connected(&self) -> bool {
        self.peer.read().browse_connected()
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

    /// Send AVRCP specific browsing commands as a AVCTP message. This method
    /// first encodes the specific AVRCP command message as a browse preamble
    /// message, which then gets encoded as part of a non-fragmented AVCTP
    /// message. Once it receives a AVCTP response message, it will decode it
    /// into a browse preamble and will return its parameters, so that the
    /// upstream can further decode the message into a specific AVRCP response
    /// message.
    pub fn send_browsing_command<'a>(
        &self,
        pdu_id: u8,
        command: BrowsePreamble,
    ) -> impl Future<Output = Result<Vec<u8>, Error>> + 'a {
        send_browsing_command_internal(self.peer.clone(), pdu_id, command)
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
        fuchsia_async::{self as fasync, DurationExt},
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

        assert!(!peer_handle.is_control_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for control.
        let (_remote, channel) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, connection, .. }))) => {
                let channel = channel.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
                // The connect request should be for the PSM advertised by the remote peer.
                match connection {
                    ConnectParameters::L2cap(L2capParameters { psm: Some(v), .. }) => {
                        assert_eq!(v, u16::from(peer_psm));
                    }
                    x => panic!("Expected L2CAP parameters but got: {:?}", x),
                }
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should be put in place.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_control_connected());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // Since peer does not support browsing, verify that outgoing
        // browsing connection was not initiated.
        let mut next_request_fut = profile_requests.next();
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());
        assert!(!peer_handle.is_browse_connected());

        Ok(())
    }

    // Check that the remote will attempt to connect to a peer for both control
    // and browsing.
    #[test]
    fn trigger_connections_test() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(1);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer - while unusual, this peer
        // advertises a non-standard PSM.
        let peer_psm = Psm::new(23); // AVCTP PSM
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1 | AvrcpTargetFeatures::SUPPORTSBROWSING,
            psm: peer_psm,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_control_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for control.
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
        assert!(peer_handle.is_control_connected());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for browse.
        let (_remote2, channel2) = Channel::create();

        let mut next_request_fut = profile_requests.next();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, connection, .. }))) => {
                let channel = channel2.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
                // The connect request should be for the PSM advertised by the remote peer.
                match connection {
                    ConnectParameters::L2cap(L2capParameters {
                        psm: Some(v),
                        parameters: Some(params),
                        ..
                    }) => {
                        assert_eq!(v, u16::from(Psm::new(27))); // AVCTP_BROWSE
                        assert_eq!(
                            params.channel_mode.expect("channel mode should not be None"),
                            bredr::ChannelMode::EnhancedRetransmission
                        );
                    }
                    x => panic!("Expected L2CAP parameters but got: {:?}", x),
                }
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should be put in place.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_browse_connected());

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

        assert!(!peer_handle.is_control_connected());

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
                        assert_eq!(v, u16::from(peer_psm));
                    }
                    x => panic!("Expected L2CAP parameters but got: {:?}", x),
                }
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // Peer should be connected.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_control_connected());

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
        assert!(peer_handle.is_control_connected());

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

    /// Tests that when inbound and outbound control connections are
    /// established at the same time, AVRCP drops both, and attempts to
    /// reconnect.
    #[test]
    fn test_simultaneous_control_connections() -> Result<(), Error> {
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

        assert!(!peer_handle.is_control_connected());

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
        assert!(peer_handle.is_control_connected());

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
        assert!(!peer_handle.is_control_connected());

        // Both the inbound and outbound-initiated control channels should be
        // dropped. Sending data should not work.
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

        let mut next_request_fut = profile_requests.next();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel3.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_control_connected());

        // New channel should be good to go.
        match remote3.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected successful write but got {:?}", x),
        }

        Ok(())
    }

    /// Tests that when connection fails, we don't infinitely retry.
    #[test]
    fn test_connection_no_retries() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(123);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1 | AvrcpTargetFeatures::SUPPORTSBROWSING,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_control_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for control.
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                // Trigger connection failure.
                responder
                    .send(&mut Err(fidl_fuchsia_bluetooth::ErrorCode::Failed))
                    .expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should have failed.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(!peer_handle.is_control_connected());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We shouldn't have requested retry.
        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Set control channel manually to test browse channel connection retry
        let (_remote, channel) = Channel::create();
        let peer = AvcPeer::new(channel);
        peer_handle.set_control_connection(peer);

        // Run to update watcher state. Control channel should be connected,
        // but browse is still not connected,
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_control_connected());
        assert!(!peer_handle.is_browse_connected());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for browse.
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { connection, responder, .. }))) => {
                // Trigger failure.
                responder
                    .send(&mut Err(fidl_fuchsia_bluetooth::ErrorCode::Failed))
                    .expect("FIDL response should work");

                // Verify that request is for browse.
                match connection {
                    ConnectParameters::L2cap(L2capParameters {
                        psm: Some(v),
                        parameters: Some(params),
                        ..
                    }) => {
                        assert_eq!(v, u16::from(Psm::new(27))); // AVCTP_BROWSE
                        assert_eq!(
                            params.channel_mode.expect("channel mode should not be None"),
                            bredr::ChannelMode::EnhancedRetransmission
                        );
                    }
                    x => panic!("Expected L2CAP parameters but got: {:?}", x),
                }
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should have failed.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(!peer_handle.is_browse_connected());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We shouldn't have requested retry.
        let mut next_request_fut = profile_requests.next();
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        Ok(())
    }

    /// Tests that when inbound and outbound browse connections are
    /// established at the same time, AVRCP drops both, and attempts to
    /// reconnect.
    #[test]
    fn test_simultaneous_browse_connections() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(123);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1 | AvrcpTargetFeatures::SUPPORTSBROWSING,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_control_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for control.
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
        assert!(peer_handle.is_control_connected());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for browse.
        let (remote2, channel2) = Channel::create();

        let mut next_request_fut = profile_requests.next();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel2.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should be put in place.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_browse_connected());

        // Should be able to send data over the channel.
        match remote2.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected data write but got {:?} instead", x),
        }

        // Advance time by LESS than the CONNECTION_THRESHOLD amount.
        let advance_time = CONNECTION_THRESHOLD.into_nanos() - 200;
        exec.set_fake_time(advance_time.nanos().after_now());
        let _ = exec.wake_expired_timers();

        // Simulate inbound browse connection.
        let (remote3, channel3) = Channel::create();
        let reconnect_peer = AvctpPeer::new(channel3);
        peer_handle.set_browse_connection(reconnect_peer);

        // Run to update watcher state. Browse channel should be disconnected,
        // but control channel should remain connected.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(!peer_handle.is_browse_connected());
        assert!(peer_handle.is_control_connected());

        // Both the inbound and outbound-initiated browse channels should be
        // dropped. Sending data should not work.
        match remote2.as_ref().write(&[0; 1]) {
            Err(zx::Status::PEER_CLOSED) => {}
            x => panic!("Expected PEER_CLOSED but got {:?}", x),
        }
        match remote3.as_ref().write(&[0; 1]) {
            Err(zx::Status::PEER_CLOSED) => {}
            x => panic!("Expected PEER_CLOSED but got {:?}", x),
        }

        // We expect to attempt to reconnect.
        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        let (remote4, channel4) = Channel::create();

        let mut next_request_fut = profile_requests.next();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel4.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // Run to update watcher state.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_browse_connected());

        // New channel should be good to go.
        match remote4.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected successful write but got {:?}", x),
        }

        Ok(())
    }

    /// Tests that when new inbound control connection comes in, previous
    /// control and browse connections are dropped.
    #[test]
    fn incoming_channel_resets_connections() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(1);
        let (peer_handle, _target_delegate, mut profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer - while unusual, this peer
        // advertises a non-standard PSM.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1 | AvrcpTargetFeatures::SUPPORTSBROWSING,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_control_connected());

        let next_request_fut = profile_requests.next();
        pin_mut!(next_request_fut);
        assert!(exec.run_until_stalled(&mut next_request_fut).is_pending());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for control.
        let (remote, channel) = Channel::create();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should be put in place.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_control_connected());

        // Advance time by the maximum amount of time it would take to establish
        // a connection.
        exec.set_fake_time(MAX_CONNECTION_EST_TIME.after_now());
        let _ = exec.wake_expired_timers();

        // We should have requested a connection for browse.
        let (remote2, channel2) = Channel::create();

        let mut next_request_fut = profile_requests.next();
        match exec.run_until_stalled(&mut next_request_fut) {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { responder, .. }))) => {
                let channel = channel2.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("FIDL response should work");
            }
            x => panic!("Expected Profile connection request to be ready, got {:?} instead.", x),
        };

        // run until stalled, the connection should be put in place.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_browse_connected());

        // Should be able to send data over the channels.
        match remote.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected data write but got {:?} instead", x),
        }
        match remote2.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected data write but got {:?} instead", x),
        }

        // Advance time by some arbitrary amount before peer decides to reconnect.
        exec.set_fake_time(5.seconds().after_now());
        let _ = exec.wake_expired_timers();

        // After some time, remote peer sends incoming a new l2cap connection
        // for control channel. Keep the old one alive to validate that it's closed.
        let (remote3, channel3) = Channel::create();
        let reconnect_peer = AvcPeer::new(channel3);
        peer_handle.set_control_connection(reconnect_peer);

        // Run to update watcher state. Control channel should be connected,
        // but browse channel that was previously set should have closed.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(peer_handle.is_control_connected());
        assert!(!peer_handle.is_browse_connected());

        // Shouldn't be able to send data over the old channels.
        match remote.as_ref().write(&[0; 1]) {
            Err(zx::Status::PEER_CLOSED) => {}
            x => panic!("Expected PEER_CLOSED but got {:?}", x),
        }
        match remote2.as_ref().write(&[0; 1]) {
            Err(zx::Status::PEER_CLOSED) => {}
            x => panic!("Expected PEER_CLOSED but got {:?}", x),
        }
        // Should be able to send data over the new channel.
        match remote3.as_ref().write(&[0; 1]) {
            Ok(1) => {}
            x => panic!("Expected data write but got {:?} instead", x),
        }

        Ok(())
    }

    /// Tests that when new inbound control connection comes in, previous
    /// control and browse connections are dropped.
    #[test]
    fn incoming_browse_channel_dropped() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(5_000000000));

        let id = PeerId(1);
        let (peer_handle, _target_delegate, mut _profile_requests) = setup_remote_peer(id)?;

        // Set the descriptor to simulate service found for peer - while unusual, this peer
        // advertises a non-standard PSM.
        peer_handle.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        assert!(!peer_handle.is_control_connected());
        assert!(!peer_handle.is_browse_connected());

        // Peer connects with a new l2cap connection for browse channel.
        // Since control channel was not already connected, verify that
        // browse channel was dropped.
        let (_remote, channel) = Channel::create();
        let connect_peer = AvctpPeer::new(channel);
        peer_handle.set_browse_connection(connect_peer);
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(!peer_handle.is_browse_connected());

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

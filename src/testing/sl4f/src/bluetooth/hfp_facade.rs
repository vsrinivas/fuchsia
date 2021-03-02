// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_utils::hanging_get::client::HangingGetStream;
use derivative::Derivative;
use fidl_fuchsia_bluetooth::PeerId;
use fidl_fuchsia_bluetooth_hfp::{
    CallManagerMarker, CallManagerProxy, CallMarker, CallRequest, CallRequestStream,
    CallState as FidlCallState, CallWatchStateResponder, DtmfCode, HeadsetGainProxy, HfpMarker,
    NetworkInformation, PeerHandlerRequest, PeerHandlerRequestStream,
    PeerHandlerWaitForCallResponder, PeerHandlerWatchNetworkInformationResponder, SignalStrength,
};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use futures::{lock::Mutex, stream::StreamExt, FutureExt};
use serde::Serialize;
use std::{collections::HashMap, sync::Arc};

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};

type CallId = u64;

#[derive(Debug)]
/// State associated with the call manager (client) end of the HFP fidl service.
struct ManagerState {
    proxy: Option<CallManagerProxy>,
    network: NetworkInformation,
    reported_network: Option<NetworkInformation>,
    network_responder: Option<PeerHandlerWatchNetworkInformationResponder>,
    operator: String,
    subscriber_numbers: Vec<String>,
    nrec_support: bool,
}

impl Default for ManagerState {
    fn default() -> Self {
        Self {
            proxy: None,
            network: NetworkInformation::EMPTY,
            reported_network: None,
            network_responder: None,
            operator: String::new(),
            subscriber_numbers: vec![],
            nrec_support: true,
        }
    }
}

/// State associated with a single Peer HF device.
#[derive(Derivative, Default)]
#[derivative(Debug)]
struct PeerState {
    nrec_enabled: bool,
    battery_level: u8,
    speaker_gain: u8,
    requested_speaker_gain: Option<u8>,
    microphone_gain: u8,
    requested_microphone_gain: Option<u8>,
    #[derivative(Debug = "ignore")]
    gain_control_watcher: Option<fasync::Task<()>>,
    gain_control: Option<HeadsetGainProxy>,
    call_responder: Option<PeerHandlerWaitForCallResponder>,
    // The tasks for managing a peer's call actions is owned by the peer.
    // This task is separate from the manager's view of the call's state.
    #[derivative(Debug = "ignore")]
    call_tasks: HashMap<CallId, fasync::Task<()>>,
}

/// State associated with a single Call.
#[derive(Derivative)]
#[derivative(Debug)]
struct CallState {
    remote: String,
    peer_id: Option<PeerId>,
    responder: Option<CallWatchStateResponder>,
    state: FidlCallState,
    reported_state: Option<FidlCallState>,
    dtmf_codes: Vec<DtmfCode>,
}

impl CallState {
    /// Update the `state` and report the state if it is a new state and there is a
    /// responder to report with.
    pub fn update_state(&mut self, state: FidlCallState) -> Result<(), Error> {
        self.state = state;
        if self.reported_state != Some(state) {
            let responder = self.responder.take().ok_or(format_err!("No call responder"))?;
            responder.send(state)?;
            self.reported_state = Some(state);
        }
        Ok(())
    }
}

#[derive(Serialize)]
pub struct StateSer {
    manager: ManagerStateSer,
    peers: HashMap<u64, PeerStateSer>,
    calls: HashMap<CallId, CallStateSer>,
}

#[derive(Serialize)]
pub struct ManagerStateSer {
    network: NetworkInformationSer,
    reported_network: Option<NetworkInformationSer>,
    operator: String,
    subscriber_numbers: Vec<String>,
    nrec_support: bool,
}

#[derive(Serialize)]
pub struct NetworkInformationSer {
    service_available: Option<bool>,
    signal_strength: Option<u8>,
    roaming: Option<bool>,
}

impl From<NetworkInformation> for NetworkInformationSer {
    fn from(info: NetworkInformation) -> Self {
        let signal_strength = info.signal_strength.map(|strength| match strength {
            SignalStrength::None => 0,
            SignalStrength::VeryLow => 1,
            SignalStrength::Low => 2,
            SignalStrength::Medium => 3,
            SignalStrength::High => 4,
            SignalStrength::VeryHigh => 5,
        });
        Self { service_available: info.service_available, signal_strength, roaming: info.roaming }
    }
}

impl From<&ManagerState> for ManagerStateSer {
    fn from(state: &ManagerState) -> Self {
        Self {
            network: state.network.clone().into(),
            reported_network: state.reported_network.clone().map(Into::into),
            operator: state.operator.clone(),
            subscriber_numbers: state.subscriber_numbers.clone(),
            nrec_support: state.nrec_support.clone(),
        }
    }
}

#[derive(Serialize)]
struct PeerStateSer {
    nrec_enabled: bool,
    battery_level: u8,
    speaker_gain: u8,
    requested_speaker_gain: Option<u8>,
    microphone_gain: u8,
    requested_microphone_gain: Option<u8>,
}

impl From<&PeerState> for PeerStateSer {
    fn from(state: &PeerState) -> Self {
        Self {
            nrec_enabled: state.nrec_enabled,
            battery_level: state.battery_level,
            speaker_gain: state.speaker_gain,
            requested_speaker_gain: state.requested_speaker_gain,
            microphone_gain: state.microphone_gain,
            requested_microphone_gain: state.requested_microphone_gain,
        }
    }
}

#[derive(Serialize)]
struct CallStateSer {
    remote: String,
    state: String,
    reported_state: Option<String>,
    dtmf_codes: Vec<String>,
}

impl From<&CallState> for CallStateSer {
    fn from(state: &CallState) -> Self {
        Self {
            remote: state.remote.clone(),
            state: format!("{:?}", state.state),
            reported_state: state.reported_state.clone().map(|s| format!("{:?}", s)),
            dtmf_codes: state.dtmf_codes.iter().map(|code| format!("{:?}", code)).collect(),
        }
    }
}

#[derive(Derivative, Default)]
#[derivative(Debug)]
struct HfpFacadeInner {
    /// Running instance of the bt-hfp-audio-gateway component.
    #[derivative(Debug = "ignore")]
    hfp_component: Option<client::App>,
    /// Call manager state that is not associated with a particular peer or call.
    manager: ManagerState,
    /// Most commands are directed at a single peer. These commands are sent to the `active_peer`.
    active_peer: Option<PeerId>,
    /// State for all connected peer devices.
    peers: HashMap<PeerId, PeerState>,
    /// The next CallId to be assigned to a new call
    next_call_id: CallId,
    /// State for all ongoing calls
    #[derivative(Debug = "ignore")]
    calls: HashMap<CallId, CallState>,
}

impl HfpFacadeInner {
    pub fn active_peer_mut(&mut self) -> Option<&mut PeerState> {
        if let Some(id) = &self.active_peer {
            Some(self.peers.get_mut(id).expect("Active peer must exist in peers map"))
        } else {
            None
        }
    }
}

#[derive(Debug, Clone)]
pub struct HfpFacade {
    inner: Arc<Mutex<HfpFacadeInner>>,
}

/// Perform Bluetooth HFP functions by acting as the call manager (client) side of the
/// fuchsia.bluetooth.hfp.Hfp protocol.
impl HfpFacade {
    pub fn new() -> HfpFacade {
        HfpFacade { inner: Arc::new(Mutex::new(HfpFacadeInner::default())) }
    }

    /// Return an HFP CallManagerProxy. If one does not exist, create it and store it in the Facade.
    async fn hfp_service_proxy(&self) -> Result<CallManagerProxy, Error> {
        let tag = "HfpFacade::hfp_service_proxy";
        let mut inner = self.inner.lock().await;
        let needs_watcher = inner.manager.proxy.is_none();
        let proxy = match inner.manager.proxy.clone() {
            Some(proxy) => proxy,
            None => {
                fx_log_info!(tag: &with_line!(tag), "Launching HFP and setting new service proxy");
                let launcher = match client::launcher() {
                    Ok(r) => r,
                    Err(err) => fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to get launcher service: {}", err)
                    ),
                };
                let bt_hfp = client::launch(
                    &launcher,
                    "fuchsia-pkg://fuchsia.com/bt-hfp-audio-gateway-default#meta/bt-hfp-audio-gateway.cmx"
                        .to_string(),
                    None,
                )?;

                let hfp_service_proxy = match bt_hfp.connect_to_service::<HfpMarker>() {
                    Ok(hfp_svc) => hfp_svc,
                    Err(err) => {
                        fx_err_and_bail!(
                            &with_line!(tag),
                            format_err!("Failed to create HFP service proxy: {}", err)
                        );
                    }
                };
                inner.hfp_component = Some(bt_hfp);
                let (proxy, server_end) = fidl::endpoints::create_proxy::<CallManagerMarker>()?;
                hfp_service_proxy.register(server_end)?;
                inner.manager.proxy = Some(proxy.clone());
                proxy
            }
        };
        if needs_watcher {
            fasync::Task::spawn(
                self.clone().watch_for_peers(proxy.clone()).map(|f| f.unwrap_or_else(|_| {})),
            )
            .detach();
        }
        Ok(proxy)
    }

    /// Initialize the HFP service
    pub async fn init_hfp_service_proxy(&self) -> Result<(), Error> {
        self.hfp_service_proxy().await?;
        Ok(())
    }

    /// Return a list of Calls by call id and remote number.
    pub async fn list_calls(&self) -> Result<Vec<(u64, String)>, Error> {
        let inner = self.inner.lock().await;
        Ok(inner.calls.iter().map(|(&id, state)| (id, state.remote.clone())).collect())
    }

    /// Notify HFP of an ongoing call. Simulates a new call from the network in the
    /// "incoming ringing" state.
    ///
    /// Arguments:
    ///     `remote`: The number associated with the remote party. This can be any string formatted
    ///     number (e.g. +1-555-555-5555).
    ///     `fidl_state`: The state to assign to the newly created call.
    async fn new_call(&self, remote: &str, fidl_state: FidlCallState) -> Result<CallId, Error> {
        let mut inner = self.inner.lock().await;
        let call_id = inner.next_call_id;
        inner.next_call_id += 1;
        let mut state = CallState {
            remote: remote.into(),
            peer_id: None,
            responder: None,
            state: fidl_state,
            reported_state: None,
            dtmf_codes: vec![],
        };

        if let Some(peer_id) = inner.active_peer.clone() {
            let peer = inner.peers.get_mut(&peer_id).expect("Active peer must exist in peers map");

            let (client_end, stream) = fidl::endpoints::create_request_stream::<CallMarker>()
                .map_err(|e| format_err!("Error creating fidl endpoints: {}", e))?;
            // This does not handle the case where there is no peer responder
            let responder = peer
                .call_responder
                .take()
                .ok_or(format_err!("No peer call responder for {:?}", peer_id))?;
            if let Ok(()) = responder.send(client_end, remote, fidl_state) {
                let task = fasync::Task::local(self.clone().manage_call(peer_id, call_id, stream));
                peer.call_tasks.insert(call_id, task);
                state.peer_id = Some(peer_id);
            }
        }

        inner.calls.insert(call_id, state);
        Ok(call_id)
    }

    /// Notify HFP of an incoming call. Simulates a new call from the network in the
    /// "incoming ringing" state.
    ///
    /// Arguments:
    ///     `remote`: The number associated with the remote party. This can be any string formatted
    ///     number (e.g. +1-555-555-5555).
    pub async fn incoming_call(&self, remote: &str) -> Result<CallId, Error> {
        self.new_call(remote, FidlCallState::IncomingRinging).await
    }

    /// Notify HFP of an outgoing call. Simulates a new call to the network in the
    /// "outgoing notifying" state.
    ///
    /// Arguments:
    ///     `remote`: The number associated with the remote party. This can be any string formatted
    ///     number (e.g. +1-555-555-5555).
    pub async fn outgoing_call(&self, remote: &str) -> Result<CallId, Error> {
        self.new_call(remote, FidlCallState::OutgoingDialing).await
    }

    /// Notify HFP of an update to the state of an ongoing call.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    ///     `fidl_state`: The state to assign to the call.
    async fn update_call(&self, call_id: CallId, fidl_state: FidlCallState) -> Result<(), Error> {
        // TODO: do not allow invalid state transitions (e.g. Terminated to Active)
        self.inner
            .lock()
            .await
            .calls
            .get_mut(&call_id)
            .ok_or(format_err!("Unknown Call Id {}", call_id))
            .and_then(|call| call.update_state(fidl_state))
    }

    /// Notify HFP that a call is now active.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_active(&self, call_id: CallId) -> Result<(), Error> {
        let tag = "HfpFacade::set_call_active";
        match self.update_call(call_id, FidlCallState::OngoingActive).await {
            Ok(()) => Ok(()),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("Failed to set call active: {}", e))
            }
        }
    }

    /// Notify HFP that a call is now held.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_held(&self, call_id: CallId) -> Result<(), Error> {
        let tag = "HfpFacade::set_call_held";
        match self.update_call(call_id, FidlCallState::OngoingHeld).await {
            Ok(()) => Ok(()),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("Failed to set call active: {}", e))
            }
        }
    }

    /// Notify HFP that a call is now terminated.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_terminated(&self, call_id: CallId) -> Result<(), Error> {
        let tag = "HfpFacade::set_call_terminated";
        match self.update_call(call_id, FidlCallState::Terminated).await {
            Ok(()) => Ok(()),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("Failed to terminate call: {}", e))
            }
        }
    }

    /// Notify HFP that a call's audio is now transferred to the Audio Gateway.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_transferred_to_ag(&self, call_id: CallId) -> Result<(), Error> {
        let tag = "HfpFacade::set_call_transferred_to_ag";
        match self.update_call(call_id, FidlCallState::TransferredToAg).await {
            Ok(()) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to transfer call to ag: {}", e)
            ),
        }
    }

    /// Return a list of HFP peers along with a boolean specifying whether each peer is the
    /// "active" peer.
    pub async fn list_peers(&self) -> Result<Vec<(u64, bool)>, Error> {
        let inner = self.inner.lock().await;
        Ok(inner
            .peers
            .keys()
            .map(|&id| (id.value, inner.active_peer.map(|active| active == id).unwrap_or(false)))
            .collect())
    }

    /// Set the active peer with the provided id. All future commands will be directed towards that
    /// peer.
    ///
    /// Arguments:
    ///     `id`: The unique id for the peer that initiated the request.
    pub async fn set_active_peer(&self, id: u64) -> Result<(), Error> {
        let tag = "HfpFacade::set_active_peer";
        let id = PeerId { value: id };
        let mut inner = self.inner.lock().await;
        if inner.peers.contains_key(&id) {
            inner.active_peer = Some(id);
        } else {
            fx_err_and_bail!(&with_line!(tag), format_err!("Peer {:?} not connected", id));
        }
        Ok(())
    }

    /// Handle a peer `request`. Most requests are handled by immediately responding with the
    /// relevant data which is stored in the facade's state.
    ///
    /// Arguments:
    ///     `id`: The unique id for the peer that initiated the request.
    ///     `request`: A request made by a client of the fuchsia.bluetooth.hfp.PeerHandler protocol.
    async fn handle_peer_request(
        &mut self,
        id: PeerId,
        request: PeerHandlerRequest,
    ) -> Result<(), Error> {
        fx_log_info!("Received Peer Handler request for {:?}: {:?}", id, request);
        match request {
            PeerHandlerRequest::WatchNetworkInformation { responder, .. } => {
                let mut inner = self.inner.lock().await;
                if Some(inner.manager.network.clone()) == inner.manager.reported_network {
                    inner.manager.network_responder = Some(responder);
                } else {
                    responder.send(inner.manager.network.clone())?;
                    inner.manager.reported_network = Some(inner.manager.network.clone());
                }
            }
            PeerHandlerRequest::WaitForCall { responder, .. } => {
                let mut inner = self.inner.lock().await;
                let peer = inner.peers.get_mut(&id).ok_or(format_err!("peer removed: {:?}", id))?;
                if peer.call_responder.is_none() {
                    peer.call_responder = Some(responder);
                } else {
                    let err = format_err!("double hanging get call on PeerHandler::WatchForCall");
                    fx_log_err!("{}", err);
                    *inner = HfpFacadeInner::default();
                    return Err(err);
                }
            }
            PeerHandlerRequest::InitiateOutgoingCall { action: _, responder: _, .. } => {
                unimplemented!();
            }
            PeerHandlerRequest::QueryOperator { responder, .. } => {
                responder.send(Some(&self.inner.lock().await.manager.operator))?;
            }
            PeerHandlerRequest::SubscriberNumberInformation { responder, .. } => {
                responder.send(
                    &mut self
                        .inner
                        .lock()
                        .await
                        .manager
                        .subscriber_numbers
                        .iter()
                        .map(AsRef::as_ref),
                )?;
            }
            PeerHandlerRequest::SetNrecMode { enabled, responder, .. } => {
                let mut inner = self.inner.lock().await;
                if inner.manager.nrec_support {
                    let peer =
                        inner.peers.get_mut(&id).ok_or(format_err!("peer removed: {:?}", id))?;
                    peer.nrec_enabled = enabled;
                    responder.send(&mut Ok(()))?;
                } else {
                    responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))?;
                }
            }
            PeerHandlerRequest::ReportHeadsetBatteryLevel { level, .. } => {
                self.inner
                    .lock()
                    .await
                    .peers
                    .get_mut(&id)
                    .ok_or(format_err!("peer removed: {:?}", id))?
                    .battery_level = level;
            }
            PeerHandlerRequest::GainControl { control, .. } => {
                let this = self.clone();
                let proxy = control.into_proxy()?;
                let proxy_ = proxy.clone();
                let task = fasync::Task::spawn(async move {
                    let proxy_ = proxy.clone();
                    let mut speaker_gain_stream =
                        HangingGetStream::new(Box::new(move || Some(proxy_.watch_speaker_gain())));
                    let proxy_ = proxy.clone();
                    let mut microphone_gain_stream = HangingGetStream::new(Box::new(move || {
                        Some(proxy_.watch_microphone_gain())
                    }));

                    loop {
                        futures::select! {
                            gain = speaker_gain_stream.next() => {
                                let mut inner = this.inner.lock().await;
                                let peer = inner.peers.get_mut(&id);
                                match (peer, gain) {
                                    (Some(peer), Some(Ok(gain))) => peer.speaker_gain = gain,
                                    _ => break,
                                }
                            }
                            gain = microphone_gain_stream.next() => {
                                let mut inner = this.inner.lock().await;
                                let peer = inner.peers.get_mut(&id);
                                match (peer, gain) {
                                    (Some(peer), Some(Ok(gain))) => peer.microphone_gain = gain,
                                    _ => break,
                                }
                            }
                        }
                    }
                    fx_log_info!("Headset gain control channel for peer {:?} closed", id);
                });
                let mut inner = self.inner.lock().await;
                let peer = inner.peers.get_mut(&id).ok_or(format_err!("peer removed: {:?}", id))?;
                if let Some(requested) = peer.requested_speaker_gain.take() {
                    proxy_.set_speaker_gain(requested)?;
                }
                if let Some(requested) = peer.requested_microphone_gain.take() {
                    proxy_.set_microphone_gain(requested)?;
                }
                peer.gain_control_watcher = Some(task);
                peer.gain_control = Some(proxy_);
            }
        }
        Ok(())
    }

    /// Handle all PeerHandlerRequests for a peer, removing the peer after the request stream
    /// is closed.
    ///
    /// Arguments:
    ///     `id`: The unique id for the peer that initiated the stream.
    ///     `stream`: A stream of requests associated with a single peer.
    async fn manage_peer(mut self, id: PeerId, mut stream: PeerHandlerRequestStream) {
        while let Some(Ok(request)) = stream.next().await {
            if let Err(e) = self.handle_peer_request(id, request).await {
                fx_log_err!("{}", e);
                break;
            };
        }
        self.inner.lock().await.peers.remove(&id);
    }

    /// Watch for new Hands Free peer devices that connect to the DUT.
    ///
    /// Spawns a new task to manage each peer that connects.
    ///
    /// Arguments:
    ///     `proxy`: The client end of the CallManager protocol which is used to watch for new
    ///     Bluetooth peers.
    async fn watch_for_peers(self, proxy: CallManagerProxy) -> Result<(), fidl::Error> {
        // Entries are only removed from the map when they are replaced, so the size of `peers`
        // will grow as the total number of unique peers grows. This is acceptable as the number of
        // unique peers that connect to a DUT is expected to be relatively small.
        let mut peers = HashMap::new();
        loop {
            let (id, server) = proxy.watch_for_peer().await?;
            let stream = server.into_stream()?;
            fx_log_info!("Handling Peer: {:?}", id);
            {
                let mut inner = self.inner.lock().await;
                inner.peers.insert(id, PeerState::default());
                inner.active_peer = Some(id);
            }

            let task = fasync::Task::spawn(self.clone().manage_peer(id, stream));
            peers.insert(id, task);
        }
    }

    /// Manage an ongoing call that is being routed to the Hands Free peer. Handle any requests
    /// made by the peer that are associated with the individual call.
    ///
    /// Arguments:
    ///     `peer_id`: The unique id of the peer as assigned by the Bluetooth stack.
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    ///     `stream`: A stream of requests associated with a single call.
    async fn manage_call(self, peer_id: PeerId, call_id: CallId, mut stream: CallRequestStream) {
        while let Some(request) = stream.next().await {
            let mut inner = self.inner.lock().await;
            let state = if let Some(state) = inner.calls.get_mut(&call_id) {
                state
            } else {
                fx_log_info!("Call management by {:?} ended: {:?}", peer_id, call_id);
                break;
            };
            match request {
                Ok(CallRequest::WatchState { responder, .. }) => {
                    if state.responder.is_some() {
                        fx_log_warn!(
                            "Call client sent multiple WatchState requests. Closing channel"
                        );
                        break;
                    }
                    state.responder = Some(responder);
                    // Trigger an update with the existing state to send it on the responder if
                    // necessary.
                    if let Err(e) = state.update_state(state.state) {
                        fx_log_info!("Call ended: {}", e);
                        break;
                    }
                }
                Ok(CallRequest::RequestHold { .. }) => {
                    if let Err(e) = state.update_state(FidlCallState::OngoingHeld) {
                        fx_log_info!("Call ended: {}", e);
                        break;
                    }
                }
                Ok(CallRequest::RequestActive { .. }) => {
                    if let Err(e) = state.update_state(FidlCallState::OngoingActive) {
                        fx_log_info!("Call ended: {}", e);
                        break;
                    }
                }
                Ok(CallRequest::RequestTerminate { .. }) => {
                    if let Err(e) = state.update_state(FidlCallState::Terminated) {
                        fx_log_info!("Call ended: {}", e);
                        break;
                    }
                }
                Ok(CallRequest::RequestTransferAudio { .. }) => {
                    if let Err(e) = state.update_state(FidlCallState::TransferredToAg) {
                        fx_log_info!("Call ended: {}", e);
                        break;
                    }
                }
                Ok(CallRequest::SendDtmfCode { code, responder, .. }) => {
                    state.dtmf_codes.push(code);
                    if let Err(e) = responder.send(&mut Ok(())) {
                        fx_log_info!("Call ended: {}", e);
                        break;
                    }
                }
                Err(e) => {
                    fx_log_warn!("Call fidl channel error: {}", e);
                }
            }
        }

        // Cleanup before exiting call task
        let mut inner = self.inner.lock().await;
        inner.calls.get_mut(&call_id).map(|call| call.peer_id = None);
        inner.peers.get_mut(&peer_id).map(|peer| peer.call_tasks.remove(&call_id));
    }

    /// Request that the active peer's speaker gain be set to `value`.
    ///
    /// Arguments:
    ///     `value`: must be between 0-15 inclusive.
    pub async fn set_speaker_gain(&self, value: u64) -> Result<(), Error> {
        let value = value as u8;
        let mut inner = self.inner.lock().await;
        let peer = inner.active_peer_mut().ok_or(format_err!("No active peer"))?;
        if peer.speaker_gain != value {
            if let Some(gain_control) = &peer.gain_control {
                gain_control.set_speaker_gain(value)?;
            } else {
                peer.requested_speaker_gain = Some(value);
            }
        }
        Ok(())
    }

    /// Request that the active peer's microphone gain be set to `value`.
    ///
    /// Arguments:
    ///     `value`: must be between 0-15 inclusive.
    pub async fn set_microphone_gain(&self, value: u64) -> Result<(), Error> {
        let value = value as u8;
        let mut inner = self.inner.lock().await;
        let peer = inner.active_peer_mut().ok_or(format_err!("No active peer"))?;
        if peer.microphone_gain != value {
            if let Some(gain_control) = &peer.gain_control {
                gain_control.set_microphone_gain(value)?;
            } else {
                peer.requested_microphone_gain = Some(value);
            }
        }
        Ok(())
    }

    /// Update the facade's network information with the provided `network`.
    /// Any fields in `network` that are `None` will not be updated.
    ///
    /// Arguments:
    ///     `network`: The updated network information fields.
    pub async fn update_network_information(
        &self,
        network: NetworkInformation,
    ) -> Result<(), Error> {
        let mut inner = self.inner.lock().await;

        // Update network state
        network
            .service_available
            .map(|update| inner.manager.network.service_available = Some(update));
        network.signal_strength.map(|update| inner.manager.network.signal_strength = Some(update));
        network.roaming.map(|update| inner.manager.network.roaming = Some(update));

        // Update the client if a responder is present
        if Some(inner.manager.network.clone()) != inner.manager.reported_network {
            if let Some(responder) = inner.manager.network_responder.take() {
                responder.send(inner.manager.network.clone())?;
                inner.manager.reported_network = Some(inner.manager.network.clone());
            }
        }

        Ok(())
    }

    pub async fn set_subscriber_number(&self, number: &str) {
        self.inner.lock().await.manager.subscriber_numbers = vec![number.to_owned()];
    }

    pub async fn set_operator(&self, value: &str) {
        self.inner.lock().await.manager.operator = value.to_owned();
    }

    pub async fn set_nrec_support(&self, value: bool) {
        self.inner.lock().await.manager.nrec_support = value;
    }

    pub async fn get_state(&self) -> StateSer {
        let inner = self.inner.lock().await;
        StateSer {
            manager: (&inner.manager).into(),
            peers: inner
                .peers
                .iter()
                .map(|(&PeerId { value: id }, peer)| (id, peer.into()))
                .collect(),
            calls: inner.calls.iter().map(|(&id, call)| (id, call.into())).collect(),
        }
    }

    /// Cleanup any HFP related objects.
    pub async fn cleanup(&self) {
        *self.inner.lock().await = HfpFacadeInner::default();
    }
}

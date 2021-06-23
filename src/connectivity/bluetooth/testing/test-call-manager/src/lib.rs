// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, format_err, Error};
use async_utils::hanging_get::client::HangingGetStream;
use derivative::Derivative;
use fidl_fuchsia_bluetooth::PeerId;
use fidl_fuchsia_bluetooth_hfp::{
    CallAction, CallDirection, CallManagerMarker, CallManagerRequest, CallManagerRequestStream,
    CallMarker, CallRequest, CallRequestStream, CallState as FidlCallState,
    CallWatchStateResponder, DtmfCode, HeadsetGainProxy, HfpMarker, HfpProxy, NetworkInformation,
    NextCall, PeerHandlerRequest, PeerHandlerRequestStream,
    PeerHandlerWatchNetworkInformationResponder, PeerHandlerWatchNextCallResponder, SignalStrength,
};
use fidl_fuchsia_bluetooth_hfp_test::{ConnectionBehavior, HfpTestMarker, HfpTestProxy};
use fuchsia_async as fasync;
use fuchsia_component::client;
// TODO (fxbug.dev/72691): Replace usage with `log` macros.
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use futures::{lock::Mutex, stream::StreamExt, FutureExt, TryStreamExt};
use serde::Serialize;
use std::{
    collections::{HashMap, VecDeque},
    sync::Arc,
};

pub const HFP_AG_URL: &str =
    "fuchsia-pkg://fuchsia.com/bt-hfp-audio-gateway-default#meta/bt-hfp-audio-gateway.cmx";

type CallId = u64;
type Number = String;
type Memory = String;

/// Handles call actions initiated by the Hands Free.
#[derive(Debug, Default, Clone)]
struct Dialer {
    /// The last dialed Number if one exists.
    last_dialed: Option<Number>,
    /// A map of Memory locations to Numbers.
    address_book: HashMap<Memory, Number>,
    /// The result that should be returned from a request to dial a Number.
    dial_result: HashMap<Number, zx::Status>,
}

impl Dialer {
    /// Performs an outgoing call initiation action, simulating a request to the network.
    /// If the request was a success, the number of the outgoing call is returned.
    /// If the request failed, the failure status is returned.
    /// Defaults to failure with `zx::Status::NOT_FOUND` if the number associated with the call
    /// action has not been explicitly set to return a result.
    ///
    /// Panics if `action` is a `CallAction::TransferActive`.
    pub fn dial(&mut self, action: CallAction) -> Result<Number, zx::Status> {
        let number = match &action {
            CallAction::TransferActive(_) => panic!("TransferActive action passed to dial"),
            CallAction::DialFromNumber(number) => Ok(number),
            CallAction::DialFromLocation(location) => {
                self.address_book.get(location).ok_or(zx::Status::NOT_FOUND)
            }
            CallAction::RedialLast(_) => self.last_dialed.as_ref().ok_or(zx::Status::NOT_FOUND),
        }?
        .to_owned();

        let result = self.dial_result.get(&number).cloned().unwrap_or(zx::Status::NOT_FOUND);
        fx_log_info!("Dial action result: {:?} - {:?}", action, result);
        if result == zx::Status::OK {
            self.last_dialed = Some(number.clone());
            Ok(number)
        } else {
            Err(result)
        }
    }
}

#[derive(Derivative)]
#[derivative(Debug)]
/// State associated with the call manager (client) end of the HFP fidl service.
struct ManagerState {
    #[derivative(Debug = "ignore")]
    peer_watcher: Option<fasync::Task<()>>,
    network: NetworkInformation,
    operator: String,
    subscriber_numbers: Vec<String>,
    nrec_support: bool,
    battery_level: Option<u8>,
    dialer: Dialer,
}

impl Default for ManagerState {
    fn default() -> Self {
        Self {
            peer_watcher: None,
            network: NetworkInformation::EMPTY,
            operator: String::new(),
            subscriber_numbers: vec![],
            nrec_support: true,
            battery_level: None,
            dialer: Dialer::default(),
        }
    }
}

/// State associated with a single Peer HF device.
#[derive(Derivative)]
#[derivative(Debug, Default)]
struct PeerState {
    reported_network: Option<NetworkInformation>,
    network_responder: Option<PeerHandlerWatchNetworkInformationResponder>,
    // nrec is enabled by default when a peer connects
    #[derivative(Default(value = "true"))]
    nrec_enabled: bool,
    battery_level: u8,
    speaker_gain: u8,
    requested_speaker_gain: Option<u8>,
    microphone_gain: u8,
    requested_microphone_gain: Option<u8>,
    #[derivative(Debug = "ignore")]
    gain_control_watcher: Option<fasync::Task<()>>,
    gain_control: Option<HeadsetGainProxy>,
    call_responder: Option<PeerHandlerWatchNextCallResponder>,
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
    direction: CallDirection,
    reported_state: Option<FidlCallState>,
    dtmf_codes: Vec<DtmfCode>,
}

impl CallState {
    /// Update the `state` and report the state if it is a new state and there is a
    /// responder to report with.
    pub fn update_state(&mut self, state: FidlCallState) -> Result<(), Error> {
        self.state = state;
        if self.reported_state != Some(state) && self.responder.is_some() {
            let responder =
                self.responder.take().expect("responder must be some after checking for presence");
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
    operator: String,
    subscriber_numbers: Vec<String>,
    nrec_support: bool,
    battery_level: Option<u8>,
    dialer: DialerSer,
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
            operator: state.operator.clone(),
            subscriber_numbers: state.subscriber_numbers.clone(),
            nrec_support: state.nrec_support.clone(),
            battery_level: state.battery_level.clone(),
            dialer: state.dialer.clone().into(),
        }
    }
}

#[derive(Serialize)]
struct PeerStateSer {
    reported_network: Option<NetworkInformationSer>,
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
            reported_network: state.reported_network.clone().map(Into::into),
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
    direction: String,
    state: String,
    reported_state: Option<String>,
    dtmf_codes: Vec<String>,
}

impl From<&CallState> for CallStateSer {
    fn from(state: &CallState) -> Self {
        Self {
            remote: state.remote.clone(),
            direction: format!("{:?}", state.direction),
            state: format!("{:?}", state.state),
            reported_state: state.reported_state.clone().map(|s| format!("{:?}", s)),
            dtmf_codes: state.dtmf_codes.iter().map(|code| format!("{:?}", code)).collect(),
        }
    }
}

#[derive(Serialize)]
struct DialerSer {
    /// The last dialed Number if one exists.
    last_dialed: Option<Number>,
    /// A map of Memory locations to Numbers.
    address_book: HashMap<Memory, Number>,
    /// The result that should be returned from a request to dial a Number.
    dial_result: HashMap<Number, String>,
}

impl From<Dialer> for DialerSer {
    fn from(dialer: Dialer) -> Self {
        Self {
            last_dialed: dialer.last_dialed,
            address_book: dialer.address_book,
            dial_result: dialer
                .dial_result
                .into_iter()
                .map(|(k, v)| (k, format!("{:?}", v)))
                .collect(),
        }
    }
}

#[derive(Derivative, Default)]
#[derivative(Debug)]
struct TestCallManagerInner {
    /// Running instance of the bt-hfp-audio-gateway component.
    #[derivative(Debug = "ignore")]
    hfp_component: Option<client::App>,
    /// Connection to HFP Test interface
    #[derivative(Debug = "ignore")]
    test_proxy: Option<HfpTestProxy>,
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
    /// Unreported calls
    unreported_calls: VecDeque<CallId>,
}

impl TestCallManagerInner {
    /// Remove a peer by `id` and all references to that peer.
    pub fn remove_peer(&mut self, id: PeerId) {
        let _ = self.peers.remove(&id);

        for call in self.calls.values_mut() {
            if call.peer_id == Some(id) {
                call.peer_id = None;
            }
        }

        if self.active_peer == Some(id) {
            self.active_peer = None;
        }
    }

    pub fn active_peer_mut(&mut self) -> Option<&mut PeerState> {
        if let Some(id) = &self.active_peer {
            Some(self.peers.get_mut(id).expect("Active peer must exist in peers map"))
        } else {
            None
        }
    }
}

#[derive(Debug, Clone)]
pub struct TestCallManager {
    inner: Arc<Mutex<TestCallManagerInner>>,
}

/// Perform Bluetooth HFP functions by acting as the call manager (client) side of the
/// fuchsia.bluetooth.hfp.Hfp protocol.
impl TestCallManager {
    pub fn new() -> TestCallManager {
        TestCallManager { inner: Arc::new(Mutex::new(TestCallManagerInner::default())) }
    }

    pub async fn set_request_stream(&self, stream: CallManagerRequestStream) {
        let task = fasync::Task::spawn(
            self.clone().watch_for_peers(stream).map(|f| f.unwrap_or_else(|_| {})),
        );
        self.inner.lock().await.manager.peer_watcher = Some(task);
    }

    pub async fn register_manager(&self, proxy: HfpProxy) -> Result<(), Error> {
        let (client_end, stream) = fidl::endpoints::create_request_stream::<CallManagerMarker>()?;
        proxy.register(client_end)?;
        self.set_request_stream(stream).await;
        Ok(())
    }

    /// Initialize the HFP service.
    pub async fn init_hfp_service(&self) -> Result<(), Error> {
        let mut inner = self.inner.lock().await;

        if inner.manager.peer_watcher.is_none() {
            fx_log_info!("Launching HFP and setting new service proxy");
            let launcher = client::launcher()
                .map_err(|err| format_err!("Failed to get launcher service: {}", err))?;
            let bt_hfp = client::launch(&launcher, HFP_AG_URL.to_string(), None)?;

            let hfp_service_proxy = bt_hfp
                .connect_to_protocol::<HfpMarker>()
                .map_err(|err| format_err!("Failed to create HFP service proxy: {}", err))?;

            inner.test_proxy =
                Some(bt_hfp.connect_to_protocol::<HfpTestMarker>().map_err(|err| {
                    format_err!("Failed to create HFP Test service proxy: {}", err)
                })?);

            inner.hfp_component = Some(bt_hfp);
            let (client_end, stream) =
                fidl::endpoints::create_request_stream::<CallManagerMarker>()?;
            hfp_service_proxy.register(client_end)?;

            let task = fasync::Task::spawn(
                self.clone().watch_for_peers(stream).map(|f| f.unwrap_or_else(|_| {})),
            );

            inner.manager.peer_watcher = Some(task);
        }
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
    pub async fn new_call(
        &self,
        remote: &str,
        fidl_state: FidlCallState,
        direction: CallDirection,
    ) -> Result<CallId, Error> {
        let remote = remote.to_string();
        let mut inner = self.inner.lock().await;
        let call_id = inner.next_call_id;
        inner.next_call_id += 1;
        let mut state = CallState {
            remote: remote.clone(),
            peer_id: None,
            responder: None,
            state: fidl_state,
            direction,
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
                .ok_or_else(|| format_err!("No peer call responder for {:?}", peer_id))?;
            let next_call = NextCall {
                call: Some(client_end),
                remote: Some(remote),
                state: Some(fidl_state),
                direction: Some(direction),
                ..NextCall::EMPTY
            };
            if let Ok(()) = responder.send(next_call) {
                let task = fasync::Task::local(self.clone().manage_call(peer_id, call_id, stream));
                peer.call_tasks.insert(call_id, task);
                state.peer_id = Some(peer_id);
            }
        } else {
            inner.unreported_calls.push_back(call_id);
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
        self.new_call(remote, FidlCallState::IncomingRinging, CallDirection::MobileTerminated).await
    }

    /// Notify HFP of an outgoing call. Simulates a new call to the network in the
    /// "outgoing notifying" state.
    ///
    /// Arguments:
    ///     `remote`: The number associated with the remote party. This can be any string formatted
    ///     number (e.g. +1-555-555-5555).
    pub async fn outgoing_call(&self, remote: &str) -> Result<CallId, Error> {
        self.new_call(remote, FidlCallState::OutgoingDialing, CallDirection::MobileOriginated).await
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
            .ok_or_else(|| format_err!("Unknown Call Id {}", call_id))
            .and_then(|call| call.update_state(fidl_state))
    }

    /// Notify HFP that a call is now active.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_active(&self, call_id: CallId) -> Result<(), Error> {
        match self.update_call(call_id, FidlCallState::OngoingActive).await {
            Ok(()) => Ok(()),
            Err(e) => Err(format_err!("Failed to set call active: {}", e)),
        }
    }

    /// Notify HFP that a call is now held.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_held(&self, call_id: CallId) -> Result<(), Error> {
        match self.update_call(call_id, FidlCallState::OngoingHeld).await {
            Ok(()) => Ok(()),
            Err(e) => Err(format_err!("Failed to set call active: {}", e)),
        }
    }

    /// Notify HFP that a call is now terminated.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_terminated(&self, call_id: CallId) -> Result<(), Error> {
        match self.update_call(call_id, FidlCallState::Terminated).await {
            Ok(()) => Ok(()),
            Err(e) => Err(format_err!("Failed to terminate call: {}", e)),
        }
    }

    /// Notify HFP that a call's audio is now transferred to the Audio Gateway.
    ///
    /// Arguments:
    ///     `call_id`: The unique id of the call as assigned by the call manager.
    pub async fn set_call_transferred_to_ag(&self, call_id: CallId) -> Result<(), Error> {
        match self.update_call(call_id, FidlCallState::TransferredToAg).await {
            Ok(()) => Ok(()),
            Err(e) => Err(format_err!("Failed to transfer call to ag: {}", e)),
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
        let id = PeerId { value: id };
        let mut inner = self.inner.lock().await;
        if inner.peers.contains_key(&id) {
            inner.active_peer = Some(id);
            Ok(())
        } else {
            Err(format_err!("Peer {:?} not connected", id))
        }
    }

    // Report all calls to the given peer. Panics if `id` does not point to a valid peer in the
    // peers map.
    fn report_calls(
        self,
        id: PeerId,
        mut inner: futures::lock::MutexGuard<'_, TestCallManagerInner>,
    ) -> Result<(), Error> {
        // keep popping from unreported until we get one
        // that is also found in the calls map.
        while let Some(cid) = inner.unreported_calls.pop_front() {
            if let Some(call) = inner.calls.get(&cid) {
                let remote = call.remote.clone();
                let state = call.state;
                let direction = call.direction;
                drop(call);
                let (client_end, stream) =
                    fidl::endpoints::create_request_stream::<CallMarker>()
                        .map_err(|e| format_err!("Error creating fidl endpoints: {}", e))?;
                let peer = inner.peers.get_mut(&id).expect("peer just added");
                let next_call = NextCall {
                    call: Some(client_end),
                    remote: Some(remote),
                    state: Some(state),
                    direction: Some(direction),
                    ..NextCall::EMPTY
                };
                let res = peer.call_responder.take().expect("just put here").send(next_call);
                if let Ok(()) = res {
                    let task = fasync::Task::local(self.manage_call(id, cid, stream));
                    peer.call_tasks.insert(cid, task);
                    inner.calls.get_mut(&cid).expect("still here").peer_id = Some(id);
                }
                break;
            }
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
                let current_network = inner.manager.network.clone();
                let peer = inner
                    .peers
                    .get_mut(&id)
                    .ok_or_else(|| format_err!("peer removed: {:?}", id))?;

                if Some(&current_network) == peer.reported_network.as_ref() {
                    peer.network_responder = Some(responder);
                } else {
                    responder.send(current_network.clone())?;
                    peer.reported_network = Some(current_network);
                }
            }
            PeerHandlerRequest::WatchNextCall { responder, .. } => {
                let this = self.clone();
                let mut inner = self.inner.lock().await;
                let peer = inner
                    .peers
                    .get_mut(&id)
                    .ok_or_else(|| format_err!("peer removed: {:?}", id))?;
                if peer.call_responder.is_none() {
                    peer.call_responder = Some(responder);
                    this.report_calls(id, inner)?;
                } else {
                    let err = format_err!("double hanging get call on PeerHandler::WatchNextCall");
                    fx_log_err!("{}", err);
                    *inner = TestCallManagerInner::default();
                    return Err(err);
                }
            }
            PeerHandlerRequest::RequestOutgoingCall { action, responder } => {
                if let CallAction::TransferActive(_) = action {
                    let inner = self.inner.lock().await;
                    match inner
                        .calls
                        .iter()
                        .find(|(_, call)| call.state == FidlCallState::OngoingActive)
                    {
                        Some((&id, _)) => {
                            drop(inner);
                            // result can be ignored because id was just found in the call map.
                            let _ = self.update_call(id, FidlCallState::TransferredToAg).await;
                        }
                        None => drop(responder.send(&mut Err(zx::Status::NOT_FOUND.into_raw()))),
                    };
                } else {
                    // Simulate dialing action and then respond to any outstanding WatchForCall
                    // requests.
                    let mut result = match {
                        // Only hold onto the lock while using it to "dial" the number.
                        // Holding the lock past this point would cause a deadlock when
                        // calling `outgoing_call`.
                        let mut inner = self.inner.lock().await;
                        inner.manager.dialer.dial(action)
                    } {
                        Ok(number) => match self.outgoing_call(&number).await {
                            Ok(id) => {
                                fx_log_info!(
                                    "Initiated outgoing call to {}. CallId: {}",
                                    number,
                                    id
                                );
                                Ok(())
                            }
                            Err(e) => {
                                fx_log_err!("Could not initiate outgoing call action: {}", e);
                                Err(zx::Status::INTERNAL.into_raw())
                            }
                        },
                        Err(status) => Err(status.into_raw()),
                    };
                    fx_log_info!("sending result to peer: {:?}", result);

                    // Once dialing and hanging gets have been handled, send response.
                    let _ = responder.send(&mut result);
                }
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
                    let peer = inner
                        .peers
                        .get_mut(&id)
                        .ok_or_else(|| format_err!("peer removed: {:?}", id))?;
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
                    .ok_or_else(|| format_err!("peer removed: {:?}", id))?
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
                let peer = inner
                    .peers
                    .get_mut(&id)
                    .ok_or_else(|| format_err!("peer removed: {:?}", id))?;
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
        self.inner.lock().await.remove_peer(id);
    }

    /// Watch for new Hands Free peer devices that connect to the DUT.
    ///
    /// Spawns a new task to manage each peer that connects.
    ///
    /// Arguments:
    ///     `proxy`: The client end of the CallManager protocol which is used to watch for new
    ///     Bluetooth peers.
    async fn watch_for_peers(
        self,
        mut stream: CallManagerRequestStream,
    ) -> Result<(), fidl::Error> {
        // Entries are only removed from the map when they are replaced, so the size of `peers`
        // will grow as the total number of unique peers grows. This is acceptable as the number of
        // unique peers that connect to a DUT is expected to be relatively small.
        let mut peers = HashMap::new();
        while let Some(CallManagerRequest::PeerConnected { id, handle, responder }) =
            stream.try_next().await?
        {
            let stream = handle.into_stream()?;
            fx_log_info!("Handling Peer: {:?}", id);
            {
                let mut inner = self.inner.lock().await;
                inner.peers.insert(id, PeerState::default());
                inner.active_peer = Some(id);
            }

            let task = fasync::Task::spawn(self.clone().manage_peer(id, stream));
            peers.insert(id, task);
            let _ = responder.send();
        }
        Ok(())
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
            fx_log_info!("Got call request: {:?} {:?} -> {:?}", peer_id, call_id, request);
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
        let peer = inner.active_peer_mut().ok_or_else(|| format_err!("No active peer"))?;
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
        let peer = inner.active_peer_mut().ok_or_else(|| format_err!("No active peer"))?;
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
        let current_network = inner.manager.network.clone();

        for peer in inner.peers.values_mut() {
            // Update the client if a responder is present
            if Some(&current_network) != peer.reported_network.as_ref() {
                if let Some(responder) = peer.network_responder.take() {
                    responder.send(current_network.clone())?;
                    peer.reported_network = Some(current_network.clone());
                }
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

    pub async fn set_battery_level(&self, value: u64) -> Result<(), Error> {
        if value > 5 {
            bail!("Value out of range: {}. Battery level must be 0-5.", value);
        }
        let mut inner = self.inner.lock().await;
        let proxy = inner
            .test_proxy
            .as_ref()
            .ok_or_else(|| format_err!("Cannot set battery without HfpTest proxy"))?;
        proxy.battery_indicator(value as u8)?;
        inner.manager.battery_level = Some(value as u8);
        Ok(())
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

    /// Set the simulated "last dialed" number.
    ///
    /// Arguments:
    ///     `number`: Number to be set. To clear the last dialed number, set `number` to `None`.
    pub async fn set_last_dialed(&self, number: Option<Number>) {
        self.inner.lock().await.manager.dialer.last_dialed = number;
    }

    /// Store a number at a specific location in address book memory.
    ///
    /// Arguments:
    ///     `location`: The key used to look up a specific number in address book memory.
    ///     `number`: Number to be set. To remove the address book entry, set `number` to `None`.
    pub async fn set_memory_location(&self, location: Memory, number: Option<Number>) {
        let _ = match number {
            Some(number) => {
                self.inner.lock().await.manager.dialer.address_book.insert(location, number)
            }
            None => self.inner.lock().await.manager.dialer.address_book.remove(&location),
        };
    }

    /// Set the simulated result that will be returned after HFP requests an outgoing call.
    /// This result is used regardless of whether a number was specified directly through
    /// CallAction::dial_from_number or indirectly through either CallAction::dial_from_location or
    /// CallAction::redial_last.
    ///
    /// Arguments:
    ///     `number`: Number that maps to a simulated result.
    ///     `status`: The simulated result value for `number`.
    pub async fn set_dial_result(&self, number: Number, status: zx::Status) {
        let _ = self.inner.lock().await.manager.dialer.dial_result.insert(number, status);
    }

    /// Configure the connection behavior when the component receives new search results from
    /// the bredr.Profile protocol.
    ///
    /// Arguments:
    ///     `autoconnect`: determine whether the component should automatically attempt to
    ///                    make a new RFCOMM connection.
    pub async fn set_connection_behavior(&self, autoconnect: bool) -> Result<(), Error> {
        let inner = self.inner.lock().await;
        let proxy = inner.test_proxy.as_ref().ok_or_else(|| {
            format_err!("Cannot set slc connection behavior on command without HfpTest proxy")
        })?;
        let () = proxy.set_connection_behavior(ConnectionBehavior {
            autoconnect: Some(autoconnect),
            ..ConnectionBehavior::EMPTY
        })?;
        Ok(())
    }

    /// Cleanup any HFP related objects.
    pub async fn cleanup(&self) {
        *self.inner.lock().await = TestCallManagerInner::default();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth_hfp::PeerHandlerMarker;

    #[fuchsia::test]
    async fn outgoing_call_does_not_deadlock() {
        let manager = TestCallManager::new();

        // set up the dial result so that an outgoing call request will be a success.
        manager.set_dial_result("123".to_string(), zx::Status::OK).await;

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();

        // Create a background task to manage a peer channel.
        fasync::Task::local({
            let manager = manager.clone();
            async move {
                manager.manage_peer(PeerId { value: 1 }, stream).await;
            }
        })
        .detach();

        // requesting an outgoing call should complete successfully
        let result =
            proxy.request_outgoing_call(&mut CallAction::DialFromNumber("123".to_string())).await;
        assert!(result.is_ok());
    }
}

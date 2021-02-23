// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_utils::hanging_get::client::HangingGetStream;
use derivative::Derivative;
use fidl_fuchsia_bluetooth::PeerId;
use fidl_fuchsia_bluetooth_hfp::{
    CallManagerMarker, CallManagerProxy, CallMarker, CallState, HeadsetGainProxy, HfpMarker,
    NetworkInformation, PeerHandlerRequest, PeerHandlerRequestStream,
    PeerHandlerWaitForCallResponder,
};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use futures::{lock::Mutex, stream::StreamExt, FutureExt};
use std::{collections::HashMap, sync::Arc};

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};

type CallId = u64;

#[derive(Debug)]
/// State associated with the call manager (client) end of the HFP fidl service.
struct ManagerState {
    proxy: Option<CallManagerProxy>,
    network: NetworkInformation,
    operator: String,
    subscriber_numbers: Vec<String>,
    nrec_support: bool,
}

impl Default for ManagerState {
    fn default() -> Self {
        Self {
            proxy: None,
            network: NetworkInformation::EMPTY,
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
    microphone_gain: u8,
    #[derivative(Debug = "ignore")]
    gain_control_watcher: Option<fasync::Task<()>>,
    gain_control: Option<HeadsetGainProxy>,
    call_responder: Option<PeerHandlerWaitForCallResponder>,
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
    calls: HashMap<CallId, (PeerId, fasync::Task<()>)>,
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

    /// Notify HFP of an incoming call. Simulates a new call from the network in the
    /// "incoming ringing" state.
    ///
    /// Arguments:
    ///     `remote`: The number associated with the remote party. This can be any string formatted
    ///     number (e.g. +1-555-555-5555).
    pub async fn incoming_call(&self, remote: &str) -> Result<CallId, Error> {
        let mut inner = self.inner.lock().await;
        let active_peer = inner.active_peer.ok_or(format_err!("No active peer"))?;
        let peer = inner.peers.get_mut(&active_peer).expect("Active peer must be in peers map");
        let (client_end, mut stream) = fidl::endpoints::create_request_stream::<CallMarker>()
            .map_err(|e| format_err!("Error creating fidl endpoints: {}", e))?;
        let state = CallState::IncomingRinging;
        let responder = peer.call_responder.take().unwrap();
        responder.send(client_end, remote, state).unwrap();
        let task =
            fasync::Task::local(async move { while let Some(_request) = stream.next().await {} });
        let call_id = inner.next_call_id;
        inner.next_call_id += 1;
        inner.calls.insert(call_id, (active_peer, task));

        Ok(call_id)
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
                responder.send(self.inner.lock().await.manager.network.clone())?;
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

    /// Cleanup any HFP related objects.
    pub async fn cleanup(&self) {
        *self.inner.lock().await = HfpFacadeInner::default();
    }
}

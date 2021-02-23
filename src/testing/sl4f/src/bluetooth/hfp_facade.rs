// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use derivative::Derivative;
use fidl_fuchsia_bluetooth::PeerId;
use fidl_fuchsia_bluetooth_hfp::{
    CallManagerMarker, CallManagerProxy, HeadsetGainProxy, HfpMarker, NetworkInformation,
    PeerHandlerRequest, PeerHandlerRequestStream, PeerHandlerWaitForCallResponder,
};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use futures::{lock::Mutex, stream::StreamExt, FutureExt};
use std::{collections::HashMap, sync::Arc};

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};

/// State associated with the call manager (client) end of the HFP fidl service.
#[derive(Debug)]
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
    /// State for all connected peer devices.
    peers: HashMap<PeerId, PeerState>,
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

    /// Creates an HFP CallManagerProxy and starts an HFP component. Returns the existing proxy if
    /// the component is already running.
    async fn create_hfp_service_proxy(&self) -> Result<CallManagerProxy, Error> {
        let tag = "HfpFacade::create_hfp_service_proxy";
        let mut inner = self.inner.lock().await;
        match inner.manager.proxy.clone() {
            Some(proxy) => {
                fx_log_info!(
                    tag: &with_line!(tag),
                    "Current Hfp Call Manager service proxy: {:?}",
                    proxy
                );
                Ok(proxy)
            }
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
                Ok(proxy)
            }
        }
    }

    /// Initialize the HFP service
    pub async fn init_hfp_service_proxy(&self) -> Result<(), Error> {
        let proxy = self.create_hfp_service_proxy().await?;

        self.inner.lock().await.manager.proxy = Some(proxy.clone());
        // Spawn a task to watch for peer hands free devices.
        fasync::Task::spawn(self.clone().watch_for_peers(proxy).map(|f| {
            f.unwrap_or_else(|e| {
                fx_log_err!("Watch for Peers task stopped with an error: {}", e);
            })
        }))
        .detach();
        Ok(())
    }

    /// Handle a peer `request`. Most requests are handled by immediately responding with the
    /// relevant data which is stored in the facade's state.
    ///
    /// Arguments:
    ///     `id`: The unique id for the peer that initiated the request.
    ///     `request`: A request made by a client of the fuchsia.bluetooth.hfp.PeerHandler protocol.
    async fn handle_peer_request(&mut self, id: PeerId, request: PeerHandlerRequest) {
        fx_log_info!("Received Peer Handler request for {:?}: {:?}", id, request);
        match request {
            PeerHandlerRequest::WatchNetworkInformation { responder, .. } => {
                let _ = responder.send(self.inner.lock().await.manager.network.clone());
            }
            PeerHandlerRequest::WaitForCall { responder, .. } => {
                let mut inner = self.inner.lock().await;
                let peer = inner.peers.get_mut(&id).expect("peer to be found in manager state");
                if peer.call_responder.is_none() {
                    peer.call_responder = Some(responder);
                } else {
                    fx_log_err!("double hanging get call on PeerHandler::WatchForCall");
                    *inner = HfpFacadeInner::default();
                }
            }
            PeerHandlerRequest::InitiateOutgoingCall { action: _, responder: _, .. } => {
                unimplemented!();
            }
            PeerHandlerRequest::QueryOperator { responder, .. } => {
                let _ = responder.send(Some(&self.inner.lock().await.manager.operator));
            }
            PeerHandlerRequest::SubscriberNumberInformation { responder, .. } => {
                let _ = responder.send(
                    &mut self
                        .inner
                        .lock()
                        .await
                        .manager
                        .subscriber_numbers
                        .iter()
                        .map(AsRef::as_ref),
                );
            }
            PeerHandlerRequest::SetNrecMode { enabled, responder, .. } => {
                let mut inner = self.inner.lock().await;
                if inner.manager.nrec_support {
                    let peer = inner.peers.get_mut(&id).expect("peer to be found in manager state");
                    peer.nrec_enabled = enabled;
                    let _ = responder.send(&mut Ok(()));
                } else {
                    let _ = responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()));
                }
            }
            PeerHandlerRequest::ReportHeadsetBatteryLevel { level, .. } => {
                self.inner
                    .lock()
                    .await
                    .peers
                    .get_mut(&id)
                    .expect("peer to be found in manager state")
                    .battery_level = level;
            }
            PeerHandlerRequest::GainControl { control, .. } => {
                let this = self.clone();
                let proxy = control.into_proxy().expect("convert client end to gain control proxy");
                let proxy_ = proxy.clone();
                let task = fasync::Task::spawn(async move {
                    loop {
                        futures::select! {
                            gain = proxy.watch_speaker_gain() => {
                                let gain = gain.unwrap();
                                this.inner.lock().await.peers
                                    .get_mut(&id)
                                    .expect("peer to be found in manager state")
                                    .speaker_gain = gain;
                            }
                            gain = proxy.watch_microphone_gain() => {
                                let gain = gain.unwrap();
                                this.inner.lock().await.peers
                                    .get_mut(&id)
                                    .expect("peer to be found in manager state")
                                    .microphone_gain = gain;
                            }
                        }
                    }
                });
                let mut inner = self.inner.lock().await;
                let peer = inner.peers.get_mut(&id).expect("peer to be found in manager state");
                peer.gain_control_watcher = Some(task);
                peer.gain_control = Some(proxy_);
            }
        }
    }

    /// Handle all PeerHandlerRequests for a peer, removing the peer after the request stream
    /// is closed.
    ///
    /// Arguments:
    ///     `id`: The unique id for the peer that initiated the stream.
    ///     `stream`: A stream of requests associated with a single peer.
    async fn manage_peer(mut self, id: PeerId, mut stream: PeerHandlerRequestStream) {
        while let Some(Ok(request)) = stream.next().await {
            self.handle_peer_request(id, request).await;
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
            self.inner.lock().await.peers.insert(id, PeerState::default());
            let task = fasync::Task::spawn(self.clone().manage_peer(id, stream));
            peers.insert(id, task);
        }
    }

    /// Cleanup any HFP related objects.
    pub async fn cleanup(&self) {
        *self.inner.lock().await = HfpFacadeInner::default();
    }
}

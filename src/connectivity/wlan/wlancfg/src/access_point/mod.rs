// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{mode_management::phy_manager::PhyManagerApi, util::listener},
    anyhow::{format_err, Error},
    fidl::endpoints::create_proxy,
    fidl::epitaph::ChannelEpitaphExt,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_device_service,
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::BoxFuture,
        lock::Mutex as FutureMutex,
        select,
        sink::SinkExt,
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
        FutureExt, TryFutureExt,
    },
    log::{error, info},
    parking_lot::Mutex,
    std::sync::Arc,
};

pub mod state_machine;
pub mod types;

#[derive(Debug)]
struct AccessPointInner {
    proxy: Option<fidl_sme::ApSmeProxy>,
}

impl From<fidl_sme::ApSmeProxy> for AccessPointInner {
    fn from(proxy: fidl_sme::ApSmeProxy) -> Self {
        Self { proxy: Some(proxy) }
    }
}

// Wrapper around an AP interface allowing a watcher to set the underlying SME and the policy API
// servicing routines to utilize the SME.
#[derive(Clone)]
pub(crate) struct AccessPoint {
    inner: Arc<Mutex<AccessPointInner>>,
    phy_manager: Arc<FutureMutex<dyn PhyManagerApi + Send>>,
    device_service: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    update_sender: Option<listener::ApListenerMessageSender>,
    iface_id: Arc<Mutex<Option<u16>>>,
}

// This number was chosen arbitrarily.
const MAX_CONCURRENT_LISTENERS: usize = 1000;

type ApRequests = fidl::endpoints::ServerEnd<fidl_policy::AccessPointControllerMarker>;

impl AccessPoint {
    /// Creates a new, empty AccessPoint. The returned AccessPoint effectively represents the state
    /// in which no AP interface is available.
    pub fn new_empty(
        phy_manager: Arc<FutureMutex<dyn PhyManagerApi + Send>>,
        device_service: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    ) -> Self {
        Self {
            inner: Arc::new(Mutex::new(AccessPointInner { proxy: None })),
            phy_manager,
            device_service,
            update_sender: None,
            iface_id: Arc::new(Mutex::new(None)),
        }
    }

    /// Set the SME when a new AP interface is created.
    fn set_sme(&mut self, new_proxy: fidl_sme::ApSmeProxy) {
        self.inner.lock().proxy = Some(new_proxy);
    }

    /// Allows the policy service to set the update sender when starting the AP service.
    pub fn set_update_sender(&mut self, update_sender: listener::ApListenerMessageSender) {
        self.update_sender = Some(update_sender);
    }

    /// Accesses the AP interface's SME.
    /// Returns None if no AP interface is available.
    fn access_sme(&self) -> Option<fidl_sme::ApSmeProxy> {
        self.inner.lock().proxy.clone()
    }

    fn send_listener_message(&self, message: listener::ApMessage) -> Result<(), Error> {
        match self.update_sender.as_ref() {
            Some(update_sender) => update_sender
                .clone()
                .unbounded_send(message)
                .or_else(|e| Err(format_err!("failed to send state update: {}", e))),
            None => Err(format_err!("no update sender is available")),
        }
    }

    fn set_iface_id(&mut self, iface_id: Option<u16>) {
        let mut inner_iface_id = self.iface_id.lock();
        *inner_iface_id = iface_id;
    }

    fn access_iface_id(&mut self) -> Option<u16> {
        self.iface_id.lock().clone()
    }

    /// Serves the AccessPointProvider protocol.  Only one caller is allowed to interact with an
    /// AccessPointController.  This routine ensures that one active user has access at a time.
    /// Additional requests are terminated immediately.
    pub async fn serve_provider_requests(
        mut self,
        mut requests: fidl_policy::AccessPointProviderRequestStream,
    ) {
        let mut pending_response_queue =
            FuturesUnordered::<BoxFuture<'static, Result<Response, Error>>>::new();
        let (internal_messages_sink, mut internal_messages_stream) = mpsc::channel(0);
        let mut provider_reqs = FuturesUnordered::new();

        loop {
            select! {
                // Progress controller requests.
                _ = provider_reqs.select_next_some() => (),
                // Process provider requests.
                req = requests.select_next_some() => match req {
                    Ok(req) => {
                        // If there is an active controller - reject new requests.
                        if provider_reqs.is_empty() {
                            let mut ap = self.clone();
                            let sink_copy = internal_messages_sink.clone();
                            let fut = async move {
                                ap.handle_provider_request(
                                    sink_copy,
                                    req
                                ).await
                            };
                            provider_reqs.push(fut);
                        } else {
                            if let Err(e) = reject_provider_request(req) {
                                error!("error sending rejection epitaph");
                            }
                        }
                    }
                    Err(e) => error!("encountered and error while serving provider requests: {}", e)
                },
                complete => break,
                msg = internal_messages_stream.select_next_some() => pending_response_queue.push(msg),
                resp = pending_response_queue.select_next_some() => match resp {
                    Ok(Response::StartResponse(result)) => {
                        self.handle_sme_start_response(result)
                    },
                    Ok(Response::StopResponse(result)) => {
                        self.handle_sme_stop_response(result).await
                    },
                    Err(e) => error!("error while processing AP requests: {}", e)
                }
            }
        }
    }

    /// Handles any incoming requests for the AccessPointProvider protocol.
    async fn handle_provider_request(
        &mut self,
        internal_msg_sink: mpsc::Sender<BoxFuture<'static, Result<Response, Error>>>,
        req: fidl_policy::AccessPointProviderRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            fidl_policy::AccessPointProviderRequest::GetController {
                requests, updates, ..
            } => {
                self.register_listener(updates.into_proxy()?);
                self.handle_ap_requests(internal_msg_sink, requests).await?;
                Ok(())
            }
        }
    }

    /// Serves the AccessPointListener protocol.
    pub async fn serve_listener_requests(
        self,
        requests: fidl_policy::AccessPointListenerRequestStream,
    ) {
        let serve_fut = requests
            .try_for_each_concurrent(MAX_CONCURRENT_LISTENERS, |req| {
                self.handle_listener_request(req)
            })
            .unwrap_or_else(|e| error!("error serving Client Listener API: {}", e));
        serve_fut.await;
    }

    /// Creates an AP SME proxy from a given interface ID.
    async fn create_ap_sme(&mut self) -> Result<fidl_sme::ApSmeProxy, Error> {
        // Begin listening for new WLAN device updates.
        let (watcher_proxy, watcher_server_end) = fidl::endpoints::create_proxy()?;
        self.device_service.watch_devices(watcher_server_end)?;
        let mut event_stream = watcher_proxy.take_event_stream();

        // If the PhyManager has a PHY that can operate as an AP, request an AP interface from it
        // and create an AP SME proxy.
        let mut phy_manager = self.phy_manager.lock().await;
        let iface_id = match phy_manager.create_or_get_ap_iface().await? {
            Some(iface_id) => iface_id,
            None => return Err(format_err!("no available PHYs can function as APs")),
        };

        // Wait until and OnIfaceAdded update with the desired interface ID is seen.  This update
        // indicates that the interface is ready and that clients can proceed with acquiring an SME
        // proxy.
        while let Some(event) = event_stream.try_next().await? {
            match event {
                fidl_fuchsia_wlan_device_service::DeviceWatcherEvent::OnIfaceAdded {
                    iface_id: new_iface_id,
                } => {
                    if new_iface_id == iface_id {
                        break;
                    }
                }
                _ => continue,
            }
        }

        let (sme, remote) = create_proxy()?;
        let status = self.device_service.get_ap_sme(iface_id, remote).await?;
        zx::ok(status)?;
        drop(phy_manager);

        self.set_sme(sme.clone());
        self.set_iface_id(Some(iface_id));
        Ok(sme)
    }

    /// Handles all requests of the AccessPointController.
    async fn handle_ap_requests(
        &mut self,
        mut internal_msg_sink: mpsc::Sender<BoxFuture<'static, Result<Response, Error>>>,
        requests: ApRequests,
    ) -> Result<(), fidl::Error> {
        let mut request_stream = requests.into_stream()?;
        while let Some(request) = request_stream.try_next().await? {
            log_ap_request(&request);
            match request {
                fidl_policy::AccessPointControllerRequest::StartAccessPoint {
                    config,
                    mode,
                    band,
                    responder,
                } => {
                    let sme = match self.access_sme() {
                        Some(sme) => sme,
                        None => match self.create_ap_sme().await {
                            Ok(sme) => sme,
                            Err(e) => {
                                error!("could not start AP: {}", e);
                                responder
                                    .send(fidl_common::RequestStatus::RejectedIncompatibleMode)?;
                                continue;
                            }
                        },
                    };

                    let mut ap_config = match derive_ap_config(&config, band) {
                        Ok(config) => config,
                        Err(e) => {
                            error!("Could not start AP: {}", e);
                            responder.send(fidl_common::RequestStatus::RejectedNotSupported)?;
                            continue;
                        }
                    };

                    // The AP will not take a new configuration on start unless it is stopped.
                    // Issue an initial stop command before attempting to start the AP.
                    match sme.stop().await {
                        Ok(()) => {
                            let fut = sme.start(&mut ap_config).map(move |result| {
                                Ok(Response::StartResponse(StartParameters {
                                    config,
                                    mode,
                                    band,
                                    result,
                                }))
                            });
                            if let Err(e) = internal_msg_sink.send(fut.boxed()).await {
                                error!("Failed to send internal message: {:?}", e)
                            }
                            responder.send(fidl_common::RequestStatus::Acknowledged)?;
                        }
                        Err(e) => {
                            error!("could not stop AP before starting: {}", e);
                            responder.send(fidl_common::RequestStatus::RejectedIncompatibleMode)?;
                        }
                    }
                }
                fidl_policy::AccessPointControllerRequest::StopAccessPoint {
                    config,
                    responder,
                } => {
                    let sme = match self.access_sme() {
                        Some(sme) => sme,
                        None => {
                            responder.send(fidl_common::RequestStatus::RejectedIncompatibleMode)?;
                            continue;
                        }
                    };

                    let fut = sme.stop().map(move |result| {
                        Ok(Response::StopResponse(StopParameters { config: Some(config), result }))
                    });
                    if let Err(e) = internal_msg_sink.send(fut.boxed()).await {
                        error!("Failed to send internal message: {:?}", e)
                    }
                    responder.send(fidl_common::RequestStatus::Acknowledged)?;
                }
                fidl_policy::AccessPointControllerRequest::StopAllAccessPoints { .. } => {
                    let sme = match self.access_sme() {
                        Some(sme) => sme,
                        None => continue,
                    };

                    let fut = sme.stop().map(move |result| {
                        Ok(Response::StopResponse(StopParameters { config: None, result }))
                    });
                    if let Err(e) = internal_msg_sink.send(fut.boxed()).await {
                        error!("Failed to send internal message: {:?}", e)
                    }
                }
            }
        }
        Ok(())
    }

    /// Registers a new update listener.
    /// The client's current state will be send to the newly added listener immediately.
    fn register_listener(&self, listener: fidl_policy::AccessPointStateUpdatesProxy) {
        if let Err(e) = self.send_listener_message(listener::Message::NewListener(listener)) {
            error!("failed to register new listener: {}", e);
        }
    }

    fn handle_sme_start_response(&self, result: StartParameters) {
        let state = match result.result {
            Ok(result_code) => match result_code {
                fidl_sme::StartApResultCode::Success
                | fidl_sme::StartApResultCode::AlreadyStarted => types::OperatingState::Active,
                ref state => {
                    error!("AP did not start: {:?}", state);
                    types::OperatingState::Failed
                }
            },
            Err(e) => {
                error!("Failed to communicate with AP SME: {}", e);
                types::OperatingState::Failed
            }
        };
        let update = listener::ApStatesUpdate {
            access_points: vec![listener::ApStateUpdate::new(
                state,
                types::ConnectivityMode::from(result.mode),
                types::OperatingBand::from(result.band),
            )],
        };
        match self.send_listener_message(listener::Message::NotifyListeners(update)) {
            Ok(()) => {}
            Err(e) => error!("Failed to send AP start update: {}", e),
        }
    }

    async fn handle_sme_stop_response(&mut self, result: StopParameters) {
        if let Some(iface_id) = self.access_iface_id() {
            let mut phy_manager = self.phy_manager.lock().await;
            if let Err(e) = phy_manager.destroy_ap_iface(iface_id).await {
                error!("failed to delete AP iface: {}", e);
            } else {
                drop(phy_manager);
                self.inner.lock().proxy = None;
                self.set_iface_id(None);
            }
        }

        let update = listener::ApStatesUpdate {
            access_points: match result.result {
                Ok(()) => vec![],
                Err(e) => {
                    error!("Failed to stop AP: {}", e);
                    vec![listener::ApStateUpdate {
                        state: types::OperatingState::Failed,
                        mode: None,
                        band: None,
                        frequency: None,
                        clients: None,
                    }]
                }
            },
        };
        match self.send_listener_message(listener::Message::NotifyListeners(update)) {
            Ok(()) => {}
            Err(e) => error!("Failed to send AP stop update: {}", e),
        }
    }

    /// Handle inbound requests to register an additional AccessPointStateUpdates listener.
    async fn handle_listener_request(
        &self,
        req: fidl_policy::AccessPointListenerRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            fidl_policy::AccessPointListenerRequest::GetListener { updates, .. } => {
                self.register_listener(updates.into_proxy()?);
                Ok(())
            }
        }
    }
}

fn reject_provider_request(
    req: fidl_policy::AccessPointProviderRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::AccessPointProviderRequest::GetController { requests, updates, .. } => {
            info!("Rejecting new access point controller request because a controller is in use");
            requests.into_channel().close_with_epitaph(zx::Status::ALREADY_BOUND)?;
            updates.into_channel().close_with_epitaph(zx::Status::ALREADY_BOUND)?;
            Ok(())
        }
    }
}

// The NetworkConfigs will be used in the future to aggregate state in crate::util::listener.
struct StartParameters {
    #[allow(unused)]
    config: fidl_policy::NetworkConfig,
    mode: fidl_policy::ConnectivityMode,
    band: fidl_policy::OperatingBand,
    result: Result<fidl_sme::StartApResultCode, fidl::Error>,
}
struct StopParameters {
    #[allow(unused)]
    config: Option<fidl_policy::NetworkConfig>,
    result: Result<(), fidl::Error>,
}

enum Response {
    StartResponse(StartParameters),
    StopResponse(StopParameters),
}

fn derive_ap_config(
    config: &fidl_policy::NetworkConfig,
    band: fidl_policy::OperatingBand,
) -> Result<fidl_sme::ApConfig, Error> {
    let ssid = match config.id.as_ref() {
        Some(id) => id.ssid.to_vec(),
        None => return Err(format_err!("Missing SSID")),
    };
    let password = match config.credential.as_ref() {
        Some(credential) => match credential {
            fidl_policy::Credential::None(fidl_policy::Empty) => b"".to_vec(),
            fidl_policy::Credential::Password(bytes) => bytes.to_vec(),
            fidl_policy::Credential::Psk(bytes) => bytes.to_vec(),
            credential => {
                return Err(format_err!("Unrecognized credential: {:?}", credential));
            }
        },
        None => b"".to_vec(),
    };
    let channel = match band {
        fidl_policy::OperatingBand::Any => 11,
        fidl_policy::OperatingBand::Only24Ghz => 11,
        fidl_policy::OperatingBand::Only5Ghz => 36,
    };
    let radio_cfg = fidl_sme::RadioConfig {
        override_phy: true,
        phy: fidl_common::Phy::Ht,
        override_cbw: true,
        cbw: fidl_common::Cbw::Cbw20,
        override_primary_chan: true,
        primary_chan: channel,
    };

    Ok(fidl_sme::ApConfig { ssid, password, radio_cfg })
}

/// Logs incoming AccessPointController requests.
fn log_ap_request(request: &fidl_policy::AccessPointControllerRequest) {
    info!(
        "Received policy AP request {}",
        match request {
            fidl_policy::AccessPointControllerRequest::StartAccessPoint { .. } => {
                "StartAccessPoint"
            }
            fidl_policy::AccessPointControllerRequest::StopAccessPoint { .. } => {
                "StopAccessPoint"
            }
            fidl_policy::AccessPointControllerRequest::StopAllAccessPoints { .. } => {
                "StopAllAccessPoints"
            }
        }
    );
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{mode_management::phy_manager::PhyManagerError, util::logger::set_logger_for_test},
        async_trait::async_trait,
        fidl::endpoints::{create_proxy, create_request_stream, RequestStream},
        fuchsia_async as fasync,
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    const TEST_AP_IFACE_ID: u16 = 1;

    #[derive(Debug)]
    struct FakePhyManager {
        ap_iface: Option<u16>,
    }

    impl FakePhyManager {
        fn new() -> Self {
            FakePhyManager { ap_iface: Some(TEST_AP_IFACE_ID) }
        }

        fn set_iface(&mut self, ap_iface: Option<u16>) {
            self.ap_iface = ap_iface;
        }
    }

    #[async_trait]
    impl PhyManagerApi for FakePhyManager {
        async fn add_phy(&mut self, _phy_id: u16) -> Result<(), PhyManagerError> {
            Ok(())
        }

        fn remove_phy(&mut self, _phy_id: u16) {}

        async fn on_iface_added(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            Ok(())
        }

        fn on_iface_removed(&mut self, _iface_id: u16) {}

        async fn create_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            Ok(())
        }

        async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            Ok(())
        }

        fn get_client(&mut self) -> Option<u16> {
            None
        }

        async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError> {
            Ok(self.ap_iface)
        }

        async fn destroy_ap_iface(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            Ok(())
        }

        async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError> {
            Ok(())
        }

        fn suggest_ap_mac(&mut self, _mac: eui48::MacAddress) {}
    }

    /// Requests a new AccessPointController from the given AccessPointProvider.
    fn request_controller(
        provider: &fidl_policy::AccessPointProviderProxy,
    ) -> (fidl_policy::AccessPointControllerProxy, fidl_policy::AccessPointStateUpdatesRequestStream)
    {
        let (controller, requests) = create_proxy::<fidl_policy::AccessPointControllerMarker>()
            .expect("failed to create ClientController proxy");
        let (update_sink, update_stream) =
            create_request_stream::<fidl_policy::AccessPointStateUpdatesMarker>()
                .expect("failed to create ClientStateUpdates proxy");
        provider.get_controller(requests, update_sink).expect("error getting controller");
        (controller, update_stream)
    }

    /// Creates an AccessPoint wrapper.
    fn create_ap(
        phy_manager: Arc<FutureMutex<dyn PhyManagerApi + Send>>,
        device_service_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    ) -> (AccessPoint, fidl_sme::ApSmeRequestStream) {
        let (ap_sme, remote) =
            create_proxy::<fidl_sme::ApSmeMarker>().expect("error creating proxy");
        let mut ap = AccessPoint::new_empty(phy_manager, device_service_proxy);
        ap.set_sme(ap_sme);
        (ap, remote.into_stream().expect("failed to create stream"))
    }

    struct TestValues {
        provider: fidl_policy::AccessPointProviderProxy,
        requests: fidl_policy::AccessPointProviderRequestStream,
        device_service_stream: fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream,
        sender: mpsc::UnboundedSender<listener::ApMessage>,
        receiver: mpsc::UnboundedReceiver<listener::ApMessage>,
        ap: AccessPoint,
        sme_stream: fidl_sme::ApSmeRequestStream,
        phy_manager: Arc<FutureMutex<FakePhyManager>>,
    }

    /// Setup channels and proxies needed for the tests to use use the AP Provider and
    /// AP Controller APIs in tests.
    fn test_setup() -> TestValues {
        let (provider, requests) = create_proxy::<fidl_policy::AccessPointProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let mut phy_manager = FakePhyManager::new();
        phy_manager.set_iface(Some(TEST_AP_IFACE_ID));
        let phy_manager = Arc::new(FutureMutex::new(phy_manager));

        let (device_service_proxy, device_service_requests) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceServiceMarker>()
                .expect("failed to create SeviceService proxy");
        let device_service_stream =
            device_service_requests.into_stream().expect("failed to create stream");

        let (ap, sme_stream) = create_ap(phy_manager.clone(), device_service_proxy);
        let (sender, receiver) = mpsc::unbounded();
        set_logger_for_test();
        TestValues {
            provider,
            requests,
            device_service_stream,
            sender,
            receiver,
            ap,
            sme_stream,
            phy_manager,
        }
    }

    /// Tests the case where StartAccessPoint is called and there is a valid interface to service
    /// the request and the request succeeds.
    #[test]
    fn test_start_access_point_with_iface_succeeds() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Clear the initial state upate.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(_))
        );

        // Issue StartAP request.
        let network_id = fidl_policy::NetworkIdentifier {
            ssid: b"test".to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let network_config = fidl_policy::NetworkConfig { id: Some(network_id), credential: None };
        let connectivity_mode = fidl_policy::ConnectivityMode::LocalOnly;
        let operating_band = fidl_policy::OperatingBand::Any;
        let start_fut =
            controller.start_access_point(network_config, connectivity_mode, operating_band);
        pin_mut!(start_fut);

        // Process start request and the initial request to stop.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Stop { responder}))) => {
                assert!(responder.send().is_ok());
            }
        );

        // Process start request and verify start response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        // Verify that the SME received the start request and send back a response
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Start { config: _, responder}))) => {
                assert!(responder.send(fidl_sme::StartApResultCode::Success).is_ok());
            }
        );

        // Run the service so that it gets the response and sends an update
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify that the listener sees the udpate
        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );

        assert_eq!(summary.access_points[0].state, types::OperatingState::Active);
    }

    /// Tests the case where StartAccessPoint is called and there is a valid interface to service
    /// the request, but the request fails.
    #[test]
    fn test_start_access_point_with_iface_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Clear the initial state upate.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(_))
        );

        // Issue StartAP request.
        let network_id = fidl_policy::NetworkIdentifier {
            ssid: b"test".to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let network_config = fidl_policy::NetworkConfig { id: Some(network_id), credential: None };
        let connectivity_mode = fidl_policy::ConnectivityMode::LocalOnly;
        let operating_band = fidl_policy::OperatingBand::Any;
        let start_fut =
            controller.start_access_point(network_config, connectivity_mode, operating_band);
        pin_mut!(start_fut);

        // Process start request and the initial request to stop.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Stop { responder}))) => {
                assert!(responder.send().is_ok());
            }
        );

        // Verify the start response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        // Verify that the SME received the start request and send back a response
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Start { config: _, responder}))) => {
                assert!(responder.send(fidl_sme::StartApResultCode::InternalError).is_ok());
            }
        );

        // Run the service so that it gets the response and sends an update
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify that the listener sees the udpate
        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );

        assert_eq!(summary.access_points[0].state, types::OperatingState::Failed);
    }

    /// Tests the case where there are no interfaces available to handle a StartAccessPoint
    /// request.
    #[test]
    fn test_start_access_point_no_iface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Set up the AccessPoint so that there is no SME.
        {
            test_values.ap.inner.lock().proxy = None;
        }

        // Setup the PhyManager so that there are no available AP ifaces.
        {
            let mut fut = test_values.phy_manager.lock();
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(mut phy_manager) => {
                phy_manager.set_iface(None);
            });
        }

        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue StartAP request.
        let network_config = fidl_policy::NetworkConfig { id: None, credential: None };
        let connectivity_mode = fidl_policy::ConnectivityMode::LocalOnly;
        let operating_band = fidl_policy::OperatingBand::Any;
        let start_fut =
            controller.start_access_point(network_config, connectivity_mode, operating_band);
        pin_mut!(start_fut);

        // Process start request and verify start response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedIncompatibleMode))
        );
    }

    /// Tests the case where the AccessPoint initially does not have an interface but is able to
    /// create one.
    #[test]
    fn test_start_access_point_create_iface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Set up the AccessPoint so that there is no SME.
        {
            test_values.ap.inner.lock().proxy = None;
        }

        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Clear the initial state upate.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(_))
        );

        // Issue StartAP request.
        let network_id = fidl_policy::NetworkIdentifier {
            ssid: b"test".to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let network_config = fidl_policy::NetworkConfig { id: Some(network_id), credential: None };
        let connectivity_mode = fidl_policy::ConnectivityMode::LocalOnly;
        let operating_band = fidl_policy::OperatingBand::Any;
        let start_fut =
            controller.start_access_point(network_config, connectivity_mode, operating_band);
        pin_mut!(start_fut);

        // Process start request.  The FakePhyManager will indicate that there is an available
        // iface that can function as an AP.  The service will then wait to be notified that the
        // iface has been created.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Send back a response notifying that the iface has been created.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.device_service_stream.next()),
            Poll::Ready(Some(Ok(
                fidl_fuchsia_wlan_device_service::DeviceServiceRequest::WatchDevices{ watcher, control_handle: _ }
            ))) => {
                let stream = watcher.into_stream().unwrap();
                let handle = stream.control_handle();
                assert!(handle.send_on_iface_added(TEST_AP_IFACE_ID).is_ok());
            }
        );

        // Wait for a request and then send back an SME proxy.
        let mut sme_remote_end: fidl_fuchsia_wlan_sme::ApSmeRequestStream;

        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.device_service_stream.next()),
            Poll::Ready(Some(Ok(
                fidl_fuchsia_wlan_device_service::DeviceServiceRequest::GetApSme { iface_id, sme, responder }
            ))) => {
                assert_eq!(iface_id, TEST_AP_IFACE_ID);
                sme_remote_end = sme.into_stream().unwrap();
                assert!(responder.send(zx::Status::OK.into_raw()).is_ok());
            }
        );

        // Verify the SME's request to initially stop the AP.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut sme_remote_end.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Stop { responder }))) => {
                assert!(responder.send().is_ok());
            }
        );

        // Verify that the caller got back an acknowledgement.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        // Verify the SME's request to start the AP.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut sme_remote_end.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Start { config: _, responder}))) => {
                assert!(responder.send(fidl_sme::StartApResultCode::Success).is_ok());
            }
        );

        // Verify that the listener sees the state update.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );

        assert_eq!(summary.access_points[0].state, types::OperatingState::Active);
    }

    /// Tests the case where StopAccessPoint is called and there is a valid interface to handle the
    /// request and the request succeeds.
    #[test]
    fn test_stop_access_point_with_iface_succeeds() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Clear the initial state upate.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(_))
        );

        // Issue StopAP request.
        let network_config = fidl_policy::NetworkConfig { id: None, credential: None };
        let stop_fut = controller.stop_access_point(network_config);
        pin_mut!(stop_fut);

        // Process stop request and verify stop response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut stop_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        // Verify that the SME received the stop request and send back a response
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Stop { responder}))) => {
                assert!(responder.send().is_ok());
            }
        );

        // Run the service so that it gets the response and sends an update
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify that the listener sees the udpate
        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );

        assert_eq!(summary.access_points.len(), 0);
    }

    /// Tests the case where StopAccessPoint is called and there is a valid interface to service
    /// the request, but the request fails.
    #[test]
    fn test_stop_access_point_with_iface_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Clear the initial state upate.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(_))
        );

        // Drop the serving end of the SME proxy.
        drop(test_values.sme_stream);

        // Issue StopAP request.
        let network_config = fidl_policy::NetworkConfig { id: None, credential: None };
        let stop_fut = controller.stop_access_point(network_config);
        pin_mut!(stop_fut);

        // Process stop request and verify stop response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut stop_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        // Run the service so that it gets the response and sends an update
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify that the listener sees the udpate
        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );

        assert_eq!(summary.access_points[0].state, types::OperatingState::Failed);
    }

    /// Tests the case where StopAccessPoint is called, but there are no interfaces to service the
    /// request.
    #[test]
    fn test_stop_access_point_no_iface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Set up the AccessPoint so that there is no SME.
        {
            test_values.ap.inner.lock().proxy = None;
        }

        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue StopAP request.
        let network_config = fidl_policy::NetworkConfig { id: None, credential: None };
        let stop_fut = controller.stop_access_point(network_config);
        pin_mut!(stop_fut);

        // Process stop request and verify stop response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut stop_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedIncompatibleMode))
        );
    }

    /// Tests the case where StopAccessPoints is called, there is a valid interface to handle the
    /// request, and the request succeeds.
    #[test]
    fn test_stop_all_access_points_succeeds() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Clear the initial state upate.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(_))
        );

        // Issue StopAllAps request.
        let stop_result = controller.stop_all_access_points();
        assert!(stop_result.is_ok());

        // Verify that the SME received the stop request and send back a response
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Stop { responder}))) => {
                assert!(responder.send().is_ok());
            }
        );

        // Verify that the listener sees the udpate
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );

        assert_eq!(summary.access_points.len(), 0);
    }

    /// Tests the case where StopAccessPoints is called and there is a valid interface to handle
    /// the request, but the request fails.
    #[test]
    fn test_stop_all_access_points_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Drop the serving end of the SME proxy.
        drop(test_values.sme_stream);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Clear the initial state upate.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(_))
        );

        // Issue StopAllAps request.
        let stop_result = controller.stop_all_access_points();
        assert!(stop_result.is_ok());

        // Verify that the listener sees the udpate
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.receiver.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );

        assert_eq!(summary.access_points[0].state, types::OperatingState::Failed);
    }

    #[test]
    fn test_multiple_api_clients() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        test_values.ap.set_update_sender(test_values.sender);
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a controller.
        let (_controller1, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request another controller.
        let (controller2, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        let chan = controller2.into_channel().expect("error turning proxy into channel");
        let mut buffer = zx::MessageBuf::new();
        let epitaph_fut = chan.recv_msg(&mut buffer);
        pin_mut!(epitaph_fut);
        assert_variant!(exec.run_until_stalled(&mut epitaph_fut), Poll::Ready(Ok(_)));

        // Verify Epitaph was received.
        use fidl::encoding::{decode_transaction_header, Decodable, Decoder, EpitaphBody};
        let (header, tail) =
            decode_transaction_header(buffer.bytes()).expect("failed decoding header");
        let mut msg = Decodable::new_empty();
        Decoder::decode_into::<EpitaphBody>(&header, tail, &mut [], &mut msg)
            .expect("failed decoding body");
        assert_eq!(msg.error, zx::Status::ALREADY_BOUND);
        assert!(chan.is_closed());
    }
}

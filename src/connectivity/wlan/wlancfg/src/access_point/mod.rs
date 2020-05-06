// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::listener,
    anyhow::{format_err, Error},
    fidl::epitaph::ChannelEpitaphExt,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::BoxFuture,
        select,
        sink::SinkExt,
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
        FutureExt, TryFutureExt,
    },
    log::{error, info},
    parking_lot::Mutex,
    std::sync::Arc,
};

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
pub struct AccessPoint {
    inner: Arc<Mutex<AccessPointInner>>,
    update_sender: Option<listener::ApMessageSender>,
}

// This number was chosen arbitrarily.
const MAX_CONCURRENT_LISTENERS: usize = 1000;

type ApRequests = fidl::endpoints::ServerEnd<fidl_policy::AccessPointControllerMarker>;

impl AccessPoint {
    /// Creates a new, empty AccessPoint. The returned AccessPoint effectively represents the state
    /// in which no AP interface is available.
    pub fn new_empty() -> Self {
        Self { inner: Arc::new(Mutex::new(AccessPointInner { proxy: None })), update_sender: None }
    }

    /// Allows the device watcher service to set the SME when a new AP interface is observed.
    pub fn set_sme(&mut self, new_proxy: fidl_sme::ApSmeProxy) {
        self.inner.lock().proxy = Some(new_proxy);
    }

    /// Allows the policy service to set the update sender when starting the AP service.
    pub fn set_update_sender(&mut self, update_sender: listener::ApMessageSender) {
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

    /// Serves the AccessPointProvider protocol.  Only one caller is allowed to interact with an
    /// AccessPointController.  This routine ensures that one active user has access at a time.
    /// Additional requests are terminated immediately.
    pub async fn serve_provider_requests(
        self,
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
                            let fut = self.clone().handle_provider_request(
                                internal_messages_sink.clone(),
                                req
                            );
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
                        self.handle_sme_stop_response(result)
                    },
                    Err(e) => error!("error while processing AP requests: {}", e)
                }
            }
        }
    }

    /// Handles any incoming requests for the AccessPointProvider protocol.
    async fn handle_provider_request(
        self,
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

    /// Handles all requests of the AccessPointController.
    async fn handle_ap_requests(
        &self,
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
                        None => {
                            responder.send(fidl_common::RequestStatus::RejectedIncompatibleMode)?;
                            continue;
                        }
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
                | fidl_sme::StartApResultCode::AlreadyStarted => {
                    fidl_policy::OperatingState::Active
                }
                ref state => {
                    error!("AP did not start: {:?}", state);
                    fidl_policy::OperatingState::Failed
                }
            },
            Err(e) => {
                error!("Failed to communicate with AP SME: {}", e);
                fidl_policy::OperatingState::Failed
            }
        };
        let update = fidl_policy::AccessPointState {
            state: Some(state),
            mode: Some(result.mode),
            band: Some(result.band),
            frequency: None,
            clients: None,
        };
        match self.send_listener_message(listener::Message::NotifyListeners(update)) {
            Ok(()) => {}
            Err(e) => error!("Failed to send AP start update: {}", e),
        }
    }

    fn handle_sme_stop_response(&self, result: StopParameters) {
        let state = match result.result {
            Ok(()) => None,
            Err(e) => {
                error!("Failed to stop AP: {}", e);
                Some(fidl_policy::OperatingState::Failed)
            }
        };
        let update = fidl_policy::AccessPointState {
            state: state,
            mode: None,
            band: None,
            frequency: None,
            clients: None,
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
        fidl_policy::OperatingBand::Only5Ghz => 52,
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
        crate::util::logger::set_logger_for_test,
        fidl::endpoints::{create_proxy, create_request_stream},
        fuchsia_async as fasync,
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

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
    fn create_ap() -> (AccessPoint, fidl_sme::ApSmeRequestStream) {
        let (ap_sme, remote) =
            create_proxy::<fidl_sme::ApSmeMarker>().expect("error creating proxy");
        let mut ap = AccessPoint::new_empty();
        ap.set_sme(ap_sme);
        (ap, remote.into_stream().expect("failed to create stream"))
    }

    struct TestValues {
        provider: fidl_policy::AccessPointProviderProxy,
        requests: fidl_policy::AccessPointProviderRequestStream,
        sender: mpsc::UnboundedSender<listener::ApMessage>,
        receiver: mpsc::UnboundedReceiver<listener::ApMessage>,
        ap: AccessPoint,
        sme_stream: fidl_sme::ApSmeRequestStream,
    }

    /// Setup channels and proxies needed for the tests to use use the AP Provider and
    /// AP Controller APIs in tests.
    fn test_setup() -> TestValues {
        let (provider, requests) = create_proxy::<fidl_policy::AccessPointProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");
        let (ap, sme_stream) = create_ap();
        let (sender, receiver) = mpsc::unbounded();
        set_logger_for_test();
        TestValues { provider, requests, sender, receiver, ap, sme_stream }
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

        assert_eq!(summary.state.unwrap(), fidl_policy::OperatingState::Active);
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

        assert_eq!(summary.state.unwrap(), fidl_policy::OperatingState::Failed);
    }

    /// Tests the case where there are no interfaces available to handle a StartAccessPoint
    /// request.
    #[test]
    fn test_start_access_point_no_iface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut ap = AccessPoint::new_empty();
        ap.set_update_sender(test_values.sender);
        let serve_fut = ap.serve_provider_requests(test_values.requests);
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

        assert!(summary.state.is_none());
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

        assert_eq!(summary.state.unwrap(), fidl_policy::OperatingState::Failed);
    }

    /// Tests the case where StopAccessPoint is called, but there are no interfaces to service the
    /// request.
    #[test]
    fn test_stop_access_point_no_iface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut ap = AccessPoint::new_empty();
        ap.set_update_sender(test_values.sender);
        let serve_fut = ap.serve_provider_requests(test_values.requests);
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

        assert!(summary.state.is_none());
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

        assert_eq!(summary.state.unwrap(), fidl_policy::OperatingState::Failed);
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

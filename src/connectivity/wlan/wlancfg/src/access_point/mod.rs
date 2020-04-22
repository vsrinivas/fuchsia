// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::epitaph::ChannelEpitaphExt,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_zircon as zx,
    futures::{
        select,
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
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
}

type ApRequests = fidl::endpoints::ServerEnd<fidl_policy::AccessPointControllerMarker>;

impl AccessPoint {
    /// Creates a new, empty AccessPoint. The returned AccessPoint effectively represents the state
    /// in which no AP interface is available.
    pub fn new_empty() -> Self {
        Self { inner: Arc::new(Mutex::new(AccessPointInner { proxy: None })) }
    }

    /// Allows the device watcher service to set the SME when a new AP interface is observed.
    #[cfg(test)]
    pub fn set_sme(&mut self, new_proxy: fidl_sme::ApSmeProxy) {
        self.inner.lock().proxy = Some(new_proxy);
    }

    /// Accesses the AP interface's SME.
    /// Returns None if no AP interface is available.
    #[allow(unused)]
    fn access_sme(&self) -> Option<fidl_sme::ApSmeProxy> {
        self.inner.lock().proxy.clone()
    }

    /// Serves the AccessPointProvider protocol.  Only one caller is allowed to interact with an
    /// AccessPointController.  This routine ensures that one active user has access at a time.
    /// Additional requests are terminated immediately.
    pub async fn serve_provider_requests(
        self,
        mut requests: fidl_policy::AccessPointProviderRequestStream,
    ) {
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
                            let fut = self.clone().handle_provider_request(req);
                            provider_reqs.push(fut);
                        } else {
                            if let Err(e) = reject_provider_request(req) {
                                error!("error sending rejection epitaph");
                            }
                        }
                    }
                    Err(e) => error!("encountered and error while serving provider requests: {}", e)
                },
            }
        }
    }

    /// Handles any incoming requests for the AccessPointProvider protocol.
    async fn handle_provider_request(
        self,
        req: fidl_policy::AccessPointProviderRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            fidl_policy::AccessPointProviderRequest::GetController {
                requests, updates: _, ..
            } => {
                self.handle_ap_requests(requests).await?;
                Ok(())
            }
        }
    }

    /// Handles all requests of the AccessPointController.
    async fn handle_ap_requests(self, requests: ApRequests) -> Result<(), fidl::Error> {
        let mut request_stream = requests.into_stream()?;
        while let Some(request) = request_stream.try_next().await? {
            log_ap_request(&request);
            match request {
                fidl_policy::AccessPointControllerRequest::StartAccessPoint {
                    config: _,
                    responder,
                    ..
                } => {
                    responder.send(fidl_common::RequestStatus::RejectedNotSupported)?;
                }
                fidl_policy::AccessPointControllerRequest::StopAccessPoint {
                    config: _,
                    responder,
                } => {
                    responder.send(fidl_common::RequestStatus::RejectedNotSupported)?;
                }
                fidl_policy::AccessPointControllerRequest::StopAllAccessPoints { .. } => {}
            }
        }
        Ok(())
    }
}

/// Rejects a provider request by sending epitaphs to the request and update channels.
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
        ap: AccessPoint,
        #[allow(unused)]
        sme_stream: fidl_sme::ApSmeRequestStream,
    }

    // Setup channels and proxies needed for the tests to use use the AP Provider and
    // AP Controller APIs in tests.
    fn test_setup() -> TestValues {
        let (provider, requests) = create_proxy::<fidl_policy::AccessPointProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");
        let (ap, sme_stream) = create_ap();
        set_logger_for_test();
        TestValues { provider, requests, ap, sme_stream }
    }

    #[test]
    fn test_start_access_point() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
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

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedNotSupported))
        );
    }

    #[test]
    fn test_stop_access_point() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
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

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut stop_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedNotSupported))
        );
    }

    #[test]
    fn test_stop_all_access_points() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let serve_fut = test_values.ap.serve_provider_requests(test_values.requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue StopAllAps request.
        let stop_result = controller.stop_all_access_points();
        assert!(stop_result.is_ok());
    }

    #[test]
    fn test_multiple_api_clients() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
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

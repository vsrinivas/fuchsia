// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mode_management::phy_manager::PhyManagerApi,
    anyhow::{bail, Context, Error},
    fidl_fuchsia_location_namedplace::RegulatoryRegionWatcherProxy,
    fidl_fuchsia_wlan_device_service::{DeviceServiceProxy, SetCountryRequest},
    fuchsia_zircon::ok as zx_ok,
    futures::{
        lock::Mutex,
        stream::{self, StreamExt, TryStreamExt},
    },
    std::sync::Arc,
};

pub(crate) struct RegulatoryManager<P: PhyManagerApi> {
    regulatory_service: RegulatoryRegionWatcherProxy,
    device_service: DeviceServiceProxy,
    phy_manager: Arc<Mutex<P>>,
}

const REGION_CODE_LEN: usize = 2;

impl<P: PhyManagerApi> RegulatoryManager<P> {
    pub fn new(
        regulatory_service: RegulatoryRegionWatcherProxy,
        device_service: DeviceServiceProxy,
        phy_manager: Arc<Mutex<P>>,
    ) -> Self {
        RegulatoryManager { regulatory_service, device_service, phy_manager }
    }

    pub async fn run(&self) -> Result<(), Error> {
        loop {
            let region_string =
                self.regulatory_service.get_update().await.context("failed to get_update()")?;

            let mut region_array = [0u8; REGION_CODE_LEN];
            if region_string.len() != region_array.len() {
                bail!("Region code {:?} does not have length {}", region_string, REGION_CODE_LEN);
            }
            region_array.copy_from_slice(region_string.as_bytes());

            // TODO(46413): Stop clients and APs using IfaceManager.

            let phy_ids = self.phy_manager.lock().await.get_phy_ids();
            let _ = stream::iter(phy_ids)
                .map(|phy_id| Ok(phy_id))
                .try_for_each(|phy_id| async move {
                    self.device_service
                        .set_country(&mut SetCountryRequest { phy_id, alpha2: region_array })
                        .await
                        .context("failed to set_country() due to FIDL error")
                        .and_then(|service_status| {
                            zx_ok(service_status)
                                .context("failed to set_country() due to service error")
                        })
                })
                .await?;

            // TODO(46413): Resume clients using IfaceManager.

            // TODO(49634): Have new PHYs respect current country.
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{Arc, Mutex, PhyManagerApi, RegulatoryManager, SetCountryRequest},
        crate::mode_management::phy_manager::PhyManagerError,
        async_trait::async_trait,
        eui48::MacAddress,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_location_namedplace::{
            RegulatoryRegionWatcherMarker, RegulatoryRegionWatcherRequest,
            RegulatoryRegionWatcherRequestStream,
        },
        fidl_fuchsia_wlan_device_service::{
            DeviceServiceMarker, DeviceServiceRequest, DeviceServiceRequestStream,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::sys::{ZX_ERR_NOT_FOUND, ZX_OK},
        futures::{stream::StreamExt, task::Poll},
        pin_utils::pin_mut,
        std::unimplemented,
        wlan_common::assert_variant,
    };

    /// Holds all of the boilerplate required for testing RegulatoryManager.
    struct TestContext {
        executor: fasync::Executor,
        regulatory_manager: RegulatoryManager<StubPhyManager>,
        device_service_requests: DeviceServiceRequestStream,
        regulatory_region_requests: RegulatoryRegionWatcherRequestStream,
    }

    impl TestContext {
        fn new(phy_manager: StubPhyManager) -> Self {
            let executor = fasync::Executor::new().expect("failed to create an executor");
            let (device_service_proxy, device_server_channel) =
                create_proxy::<DeviceServiceMarker>()
                    .expect("failed to create DeviceService proxy");
            let (regulatory_region_proxy, regulatory_region_server_channel) =
                create_proxy::<RegulatoryRegionWatcherMarker>()
                    .expect("failed to create RegulatoryRegionWatcher proxy");
            let regulatory_manager = RegulatoryManager::new(
                regulatory_region_proxy,
                device_service_proxy,
                Arc::new(Mutex::new(phy_manager)),
            );
            let device_service_requests =
                device_server_channel.into_stream().expect("failed to create DeviceService stream");
            let regulatory_region_requests = regulatory_region_server_channel
                .into_stream()
                .expect("failed to create RegulatoryRegionWatcher stream");
            Self {
                executor,
                regulatory_manager,
                device_service_requests,
                regulatory_region_requests,
            }
        }
    }

    #[test]
    fn returns_error_on_short_region_code() {
        let mut context = TestContext::new(make_default_stub_phy_manager());
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        responder.send("U").expect("failed to send response");
        assert_variant!(
            context.executor.run_until_stalled(&mut regulatory_fut),
            Poll::Ready(Err(_))
        );
    }

    #[test]
    fn returns_error_on_long_region_code() {
        let mut context = TestContext::new(make_default_stub_phy_manager());
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        responder.send("USA").expect("failed to send response");
        assert_variant!(
            context.executor.run_until_stalled(&mut regulatory_fut),
            Poll::Ready(Err(_))
        );
    }

    #[test]
    fn propagates_update_to_device_service_on_region_code_with_valid_length() {
        let mut context = TestContext::new(make_default_stub_phy_manager());
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let device_service_request_fut = &mut context.device_service_requests.next();
        let (request_params, device_service_responder) = assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Ready(Some(Ok(
                DeviceServiceRequest::SetCountry{req, responder}))) => (req, responder)
        );
        assert_eq!(request_params, SetCountryRequest { phy_id: 0, alpha2: *b"US" });
        device_service_responder.send(ZX_OK).expect("failed to send device service response");
    }

    #[test]
    fn does_not_propagate_invalid_length_region_code_to_device_service() {
        let mut context = TestContext::new(make_default_stub_phy_manager());
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("U").expect("failed to send region response");

        // Drive the RegulatoryManager until stalled, then verify that RegulatoryManager did not
        // send a request to the DeviceService. Note that we deliberately ignore the state of
        // `regulatory_fut`, as that is validated in the `returns_error_on_*` tests above.
        let _ = context.executor.run_until_stalled(&mut regulatory_fut);
        let device_service_request_fut = &mut context.device_service_requests.next();
        assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Pending
        );
    }

    #[test]
    fn returns_error_when_region_code_with_valid_length_is_rejected_by_device_service() {
        let mut context = TestContext::new(make_default_stub_phy_manager());
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");

        // Drive the RegulatoryManager until stalled, then reply to the `DeviceServiceRequest`, and
        // validate handling of the reply. Note that we deliberately ignore the state of
        // `regulatory_fut`, as that is validated in the
        // `propagates_request_on_region_code_with_valid_length` test above.
        let _ = context.executor.run_until_stalled(&mut regulatory_fut);
        let device_service_request_fut = &mut context.device_service_requests.next();
        let device_service_responder = assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Ready(Some(Ok(DeviceServiceRequest::SetCountry{req: _, responder}))) => responder
        );
        device_service_responder
            .send(ZX_ERR_NOT_FOUND)
            .expect("failed to send device service response");
        assert_variant!(
            context.executor.run_until_stalled(&mut regulatory_fut),
            Poll::Ready(Err(_))
        );
    }

    #[test]
    fn propagates_multiple_valid_region_code_updates_to_device_service() {
        let mut context = TestContext::new(make_default_stub_phy_manager());
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);

        // Receive first `RegulatoryRegionWatcher` update, and propagate it to `DeviceService`.
        {
            assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

            let region_request_fut = &mut context.regulatory_region_requests.next();
            let region_responder = assert_variant!(
                context.executor.run_until_stalled(region_request_fut),
                Poll::Ready(Some(Ok(
                    RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
            );
            region_responder.send("US").expect("failed to send region response");
            assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

            let device_service_request_fut = &mut context.device_service_requests.next();
            let (request_params, device_service_responder) = assert_variant!(
                context.executor.run_until_stalled(device_service_request_fut),
                Poll::Ready(Some(Ok(
                    DeviceServiceRequest::SetCountry{req, responder}))) => (req, responder)
            );
            assert_eq!(request_params, SetCountryRequest { phy_id: 0, alpha2: *b"US" });
            device_service_responder.send(ZX_OK).expect("failed to send device service response");
        }

        // Receive second `RegulatoryRegionWatcher` update, and propagate it to `DeviceService`.
        {
            assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

            let region_request_fut = &mut context.regulatory_region_requests.next();
            let region_responder = assert_variant!(
                context.executor.run_until_stalled(region_request_fut),
                Poll::Ready(Some(Ok(
                    RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
            );
            region_responder.send("CA").expect("failed to send region response");
            assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

            let device_service_request_fut = &mut context.device_service_requests.next();
            let (request_params, device_service_responder) = assert_variant!(
                context.executor.run_until_stalled(device_service_request_fut),
                Poll::Ready(Some(Ok(
                    DeviceServiceRequest::SetCountry{req, responder}))) => (req, responder)
            );
            assert_eq!(request_params, SetCountryRequest { phy_id: 0, alpha2: *b"CA" });
            device_service_responder.send(ZX_OK).expect("failed to send device service response");
        }
    }

    #[test]
    fn propagates_single_update_to_multiple_phys() {
        let mut context = TestContext::new(StubPhyManager { phy_ids: vec![0, 1] });
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let device_service_request_fut = &mut context.device_service_requests.next();
        let (request_params, device_service_responder) = assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Ready(Some(Ok(
                DeviceServiceRequest::SetCountry{req, responder}))) => (req, responder)
        );
        assert_eq!(request_params, SetCountryRequest { phy_id: 0, alpha2: *b"US" });
        device_service_responder.send(ZX_OK).expect("failed to send device service response");
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let device_service_request_fut = &mut context.device_service_requests.next();
        let (request_params, device_service_responder) = assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Ready(Some(Ok(
                DeviceServiceRequest::SetCountry{req, responder}))) => (req, responder)
        );
        assert_eq!(request_params, SetCountryRequest { phy_id: 1, alpha2: *b"US" });
        device_service_responder.send(ZX_OK).expect("failed to send device service response");
    }

    #[test]
    fn keeps_running_after_update_with_empty_phy_list() {
        let mut context = TestContext::new(StubPhyManager { phy_ids: Vec::new() });
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());
    }

    #[test]
    fn does_not_send_device_service_request_when_phy_list_is_empty() {
        let mut context = TestContext::new(StubPhyManager { phy_ids: Vec::new() });
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");

        // Drive the RegulatoryManager until stalled, then verify that RegulatoryManager did not
        // send a request to the DeviceService. Note that we deliberately ignore the status of
        // `regulatory_fut`, as that is validated in the
        // `keeps_running_after_update_with_empty_phy_list` test above.
        let _ = context.executor.run_until_stalled(&mut regulatory_fut);
        let device_service_request_fut = &mut context.device_service_requests.next();
        assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Pending
        );
    }

    #[test]
    fn does_not_attempt_to_configure_second_phy_when_first_fails_to_configure() {
        let mut context = TestContext::new(StubPhyManager { phy_ids: vec![0, 1] });
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        let device_service_request_fut = &mut context.device_service_requests.next();
        let device_service_responder = assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Ready(Some(Ok(
                DeviceServiceRequest::SetCountry{req: _, responder}))) => responder
        );
        device_service_responder
            .send(ZX_ERR_NOT_FOUND)
            .expect("failed to send device service response");

        // Drive the RegulatoryManager until stalled, then verify that RegulatoryManager did not
        // send a new request to the DeviceService. Note that we deliberately ignore the status of
        // `regulatory_fut`, as that is validated in the
        // `returns_error_when_region_code_with_valid_length_is_rejected_by_device_service` test
        // above.
        let _ = context.executor.run_until_stalled(&mut regulatory_fut).is_pending();
        let device_service_request_fut = &mut context.device_service_requests.next();
        assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Pending
        );
    }

    struct StubPhyManager {
        phy_ids: Vec<u16>,
    }

    /// A default StubPhyManager has one Phy, with ID 0.
    fn make_default_stub_phy_manager() -> StubPhyManager {
        StubPhyManager { phy_ids: vec![0] }
    }

    #[async_trait]
    impl PhyManagerApi for StubPhyManager {
        async fn add_phy(&mut self, _phy_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn remove_phy(&mut self, _phy_id: u16) {
            unimplemented!();
        }

        async fn on_iface_added(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn on_iface_removed(&mut self, _iface_id: u16) {
            unimplemented!();
        }

        async fn create_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn get_client(&mut self) -> Option<u16> {
            unimplemented!();
        }

        async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError> {
            unimplemented!();
        }

        async fn destroy_ap_iface(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn suggest_ap_mac(&mut self, _mac: MacAddress) {
            unimplemented!();
        }

        fn get_phy_ids(&self) -> Vec<u16> {
            self.phy_ids.clone()
        }
    }
}

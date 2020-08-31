// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mode_management::{iface_manager_api::IfaceManagerApi, phy_manager::PhyManagerApi},
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

pub(crate) struct RegulatoryManager<I: IfaceManagerApi + ?Sized, P: PhyManagerApi> {
    regulatory_service: RegulatoryRegionWatcherProxy,
    device_service: DeviceServiceProxy,
    phy_manager: Arc<Mutex<P>>,
    iface_manager: Arc<Mutex<I>>,
}

const REGION_CODE_LEN: usize = 2;

impl<I: IfaceManagerApi + ?Sized, P: PhyManagerApi> RegulatoryManager<I, P> {
    pub fn new(
        regulatory_service: RegulatoryRegionWatcherProxy,
        device_service: DeviceServiceProxy,
        phy_manager: Arc<Mutex<P>>,
        iface_manager: Arc<Mutex<I>>,
    ) -> Self {
        RegulatoryManager { regulatory_service, device_service, phy_manager, iface_manager }
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

            // TODO(49637): Improve handling of concurrent operations on `IfaceManager`.
            // TODO(46413): Stop APs using `IfaceManager`.
            self.iface_manager.lock().await.stop_client_connections().await?;
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

            // TODO(49632): Respect the initial state of client connections, instead of
            // restarting them unconditionally.
            self.iface_manager.lock().await.start_client_connections().await?;

            // TODO(49634): Have new PHYs respect current country.
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{Arc, IfaceManagerApi, Mutex, PhyManagerApi, RegulatoryManager, SetCountryRequest},
        crate::{
            access_point::state_machine as ap_fsm, client::state_machine as client_fsm,
            mode_management::phy_manager::PhyManagerError,
        },
        anyhow::{format_err, Error},
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
        futures::{
            channel::{mpsc, oneshot},
            future::{self, FutureExt},
            stream::{self, Stream, StreamExt},
            task::Poll,
        },
        pin_utils::pin_mut,
        std::unimplemented,
        wlan_common::assert_variant,
    };

    /// Holds all of the boilerplate required for testing RegulatoryManager.
    struct TestContext<
        S: Stream<Item = Result<(), Error>> + Send + Unpin,
        T: Stream<Item = Result<(), Error>> + Send + Unpin,
    > {
        executor: fasync::Executor,
        iface_manager: Arc<Mutex<StubIfaceManager<S, T>>>,
        regulatory_manager: RegulatoryManager<StubIfaceManager<S, T>, StubPhyManager>,
        device_service_requests: DeviceServiceRequestStream,
        regulatory_region_requests: RegulatoryRegionWatcherRequestStream,
    }

    impl<S, T> TestContext<S, T>
    where
        S: Stream<Item = Result<(), Error>> + Send + Unpin,
        T: Stream<Item = Result<(), Error>> + Send + Unpin,
    {
        fn new(
            phy_manager: StubPhyManager,
            iface_manager: StubIfaceManager<S, T>,
        ) -> TestContext<
            impl Stream<Item = Result<(), Error>> + Send + Unpin,
            impl Stream<Item = Result<(), Error>> + Send + Unpin,
        > {
            let executor = fasync::Executor::new().expect("failed to create an executor");
            let (device_service_proxy, device_server_channel) =
                create_proxy::<DeviceServiceMarker>()
                    .expect("failed to create DeviceService proxy");
            let (regulatory_region_proxy, regulatory_region_server_channel) =
                create_proxy::<RegulatoryRegionWatcherMarker>()
                    .expect("failed to create RegulatoryRegionWatcher proxy");
            let iface_manager = Arc::new(Mutex::new(iface_manager));
            let regulatory_manager = RegulatoryManager::new(
                regulatory_region_proxy,
                device_service_proxy,
                Arc::new(Mutex::new(phy_manager)),
                iface_manager.clone(),
            );
            let device_service_requests =
                device_server_channel.into_stream().expect("failed to create DeviceService stream");
            let regulatory_region_requests = regulatory_region_server_channel
                .into_stream()
                .expect("failed to create RegulatoryRegionWatcher stream");
            Self {
                executor,
                iface_manager,
                regulatory_manager,
                device_service_requests,
                regulatory_region_requests,
            }
        }
    }

    #[test]
    fn returns_error_on_short_region_code() {
        let mut context =
            TestContext::new(make_default_stub_phy_manager(), make_default_stub_iface_manager());
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
        let mut context =
            TestContext::new(make_default_stub_phy_manager(), make_default_stub_iface_manager());
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
        let mut context =
            TestContext::new(make_default_stub_phy_manager(), make_default_stub_iface_manager());
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
        let mut context =
            TestContext::new(make_default_stub_phy_manager(), make_default_stub_iface_manager());
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
        let mut context =
            TestContext::new(make_default_stub_phy_manager(), make_default_stub_iface_manager());
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
        let mut context =
            TestContext::new(make_default_stub_phy_manager(), make_default_stub_iface_manager());
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
        let mut context = TestContext::new(
            StubPhyManager { phy_ids: vec![0, 1] },
            make_default_stub_iface_manager(),
        );
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
        let mut context = TestContext::new(
            StubPhyManager { phy_ids: Vec::new() },
            make_default_stub_iface_manager(),
        );
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
        let mut context = TestContext::new(
            StubPhyManager { phy_ids: Vec::new() },
            make_default_stub_iface_manager(),
        );
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
        let mut context = TestContext::new(
            StubPhyManager { phy_ids: vec![0, 1] },
            make_default_stub_iface_manager(),
        );

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

    #[test]
    fn waits_for_stop_client_connections_response_before_changing_country() {
        let (mut stop_client_connections_responder, stop_client_connections_response_stream) =
            mpsc::channel(0);
        let mut context = TestContext::new(
            StubPhyManager { phy_ids: vec![0] },
            StubIfaceManager {
                was_start_client_connections_called: false,
                stop_client_connections_response_stream,
                start_client_connections_response_stream: stream::unfold((), |_| async {
                    Some((Ok(()), ()))
                })
                .boxed(),
            },
        );
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        // Drive the RegulatoryManager to request an update from RegulatoryRegionWatcher,
        // and deliver a RegulatoryRegion update.
        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        // Verify that no requests have been sent to `DeviceService`.
        let device_service_request_fut = &mut context.device_service_requests.next();
        assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Pending
        );

        // Respond to the `stop_client_connections()` call, then verify that
        // `DeviceService.SetCountry` is called.
        stop_client_connections_responder
            .try_send(Ok(()))
            .expect("internal error: failed to send fake response to StubIfaceManager");
        let _ = context.executor.run_until_stalled(&mut regulatory_fut);
        assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Ready(Some(Ok(DeviceServiceRequest::SetCountry{..})))
        );
    }

    #[test]
    fn waits_for_set_country_response_before_starting_client_connections() {
        let mut context =
            TestContext::new(make_default_stub_phy_manager(), make_default_stub_iface_manager());

        // Drive the RegulatoryManager to request an update from RegulatoryRegionWatcher.
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        // Deliver a RegulatoryRegion update.
        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");

        // Drive the RegulatoryManager to send a SetCountryRequest to DeviceService.
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        // Fetch the SetCountryRequest.
        let device_service_request_fut = &mut context.device_service_requests.next();
        let device_service_responder =
            assert_variant!(
                context.executor.run_until_stalled(device_service_request_fut),
                Poll::Ready(Some(Ok(DeviceServiceRequest::SetCountry{req:_, responder}))) => responder
            );

        // Verify that RegulatoryManager has _not_ requested client connections be started yet.
        let mut has_start_been_called_fut = context
            .iface_manager
            .lock()
            .then(|if_manager| future::ready(if_manager.was_start_client_connections_called));
        assert_eq!(
            context.executor.run_until_stalled(&mut has_start_been_called_fut),
            Poll::Ready(false)
        );

        // Reply to the SetCountryRequest, and drive the RegulatoryManager forward again.
        device_service_responder.send(ZX_OK).expect("failed to send device service response");
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        // Verify that RegulatoryManager _has_ requested client connections be started now.
        let mut has_start_been_called_fut = context
            .iface_manager
            .lock()
            .then(|if_manager| future::ready(if_manager.was_start_client_connections_called));
        assert_eq!(
            context.executor.run_until_stalled(&mut has_start_been_called_fut),
            Poll::Ready(true)
        );
    }

    #[test]
    fn propagates_error_from_stop_client_connections() {
        let mut context = TestContext::new(
            StubPhyManager { phy_ids: vec![0] },
            StubIfaceManager {
                was_start_client_connections_called: false,
                stop_client_connections_response_stream: stream::unfold((), |_| async {
                    Some((Err(format_err!("failed to stop client connections")), ()))
                })
                .boxed(),
                start_client_connections_response_stream: stream::unfold((), |_| async {
                    Some((Ok(()), ()))
                })
                .boxed(),
            },
        );
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        // Drive the RegulatoryManager to request an update from RegulatoryRegionWatcher,
        // and deliver a RegulatoryRegion update.
        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");

        // After receiving the RegulatoryRegion update, the RegulatoryManager should call
        // `IfaceManager.stop_client_connections()`. That call should fail, and RegulatoryManager
        // should propagate the error.
        assert_variant!(
            context.executor.run_until_stalled(&mut regulatory_fut),
            Poll::Ready(Err(_))
        );
    }

    #[test]
    fn propagates_error_from_start_client_connections() {
        let mut context = TestContext::new(
            StubPhyManager { phy_ids: vec![0] },
            StubIfaceManager {
                was_start_client_connections_called: false,
                stop_client_connections_response_stream: stream::unfold((), |_| async {
                    Some((Ok(()), ()))
                })
                .boxed(),
                start_client_connections_response_stream: stream::unfold((), |_| async {
                    Some((Err(format_err!("failed to start client connections")), ()))
                })
                .boxed(),
            },
        );
        let regulatory_fut = context.regulatory_manager.run();
        pin_mut!(regulatory_fut);
        assert!(context.executor.run_until_stalled(&mut regulatory_fut).is_pending());

        // Drive the RegulatoryManager to request an update from RegulatoryRegionWatcher,
        // and deliver a RegulatoryRegion update.
        let region_request_fut = &mut context.regulatory_region_requests.next();
        let region_responder = assert_variant!(
            context.executor.run_until_stalled(region_request_fut),
            Poll::Ready(Some(Ok(RegulatoryRegionWatcherRequest::GetUpdate{responder}))) => responder
        );
        region_responder.send("US").expect("failed to send region response");

        // Drive the RegulatoryManager to issue the `DeviceService.SetCountry()` request,
        // and respond to that request.
        let _ = context.executor.run_until_stalled(&mut regulatory_fut);
        let device_service_request_fut = &mut context.device_service_requests.next();
        let device_service_responder = assert_variant!(
            context.executor.run_until_stalled(device_service_request_fut),
            Poll::Ready(Some(Ok(DeviceServiceRequest::SetCountry{req:_, responder}))) => responder
        );
        device_service_responder.send(ZX_OK).expect("failed to send device service response");

        // After seeing `SetCountry()` succeed, the RegulatoryManager should call
        // `IfaceManager.start_client_connections()`. That call should fail, and RegulatoryManager
        // should propagate the error.
        assert_variant!(
            context.executor.run_until_stalled(&mut regulatory_fut),
            Poll::Ready(Err(_))
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

    struct StubIfaceManager<
        S: Stream<Item = Result<(), Error>> + Send + Unpin,
        T: Stream<Item = Result<(), Error>> + Send + Unpin,
    > {
        was_start_client_connections_called: bool,
        stop_client_connections_response_stream: S,
        start_client_connections_response_stream: T,
    }

    /// A default StubIfaceManager
    /// * immediately returns Ok() in response to stop_client_connections(), and
    /// * immediately returns Ok() in response to start_client_connections()
    fn make_default_stub_iface_manager() -> StubIfaceManager<
        impl Stream<Item = Result<(), Error>> + Send + Unpin,
        impl Stream<Item = Result<(), Error>> + Send + Unpin,
    > {
        StubIfaceManager {
            was_start_client_connections_called: false,
            stop_client_connections_response_stream: stream::unfold((), |_| async {
                Some((Ok(()), ()))
            })
            .boxed(),
            start_client_connections_response_stream: stream::unfold((), |_| async {
                Some((Ok(()), ()))
            })
            .boxed(),
        }
    }

    #[async_trait]
    impl<
            S: Stream<Item = Result<(), Error>> + Send + Unpin,
            T: Stream<Item = Result<(), Error>> + Send + Unpin,
        > IfaceManagerApi for StubIfaceManager<S, T>
    {
        async fn disconnect(
            &mut self,
            _network_id: fidl_fuchsia_wlan_policy::NetworkIdentifier,
        ) -> Result<(), Error> {
            unimplemented!();
        }

        async fn connect(
            &mut self,
            _connect_req: client_fsm::ConnectRequest,
        ) -> Result<oneshot::Receiver<()>, Error> {
            unimplemented!();
        }

        async fn record_idle_client(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!();
        }

        async fn has_idle_client(&mut self) -> Result<bool, Error> {
            unimplemented!();
        }

        async fn handle_added_iface(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!();
        }

        async fn handle_removed_iface(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!();
        }

        async fn scan(
            &mut self,
            _timeout: u8,
            _scan_type: fidl_fuchsia_wlan_common::ScanType,
        ) -> Result<fidl_fuchsia_wlan_sme::ScanTransactionProxy, Error> {
            unimplemented!();
        }

        async fn stop_client_connections(&mut self) -> Result<(), Error> {
            self.stop_client_connections_response_stream
                .next()
                .await
                .expect("internal error: failed to receive fake response from test case")
        }

        async fn start_client_connections(&mut self) -> Result<(), Error> {
            self.was_start_client_connections_called = true;
            self.start_client_connections_response_stream
                .next()
                .await
                .expect("internal error: failed to receive fake response from test case")
        }

        async fn start_ap(
            &mut self,
            _config: ap_fsm::ApConfig,
        ) -> Result<oneshot::Receiver<fidl_fuchsia_wlan_sme::StartApResultCode>, Error> {
            unimplemented!();
        }

        async fn stop_ap(&mut self, _ssid: Vec<u8>, _password: Vec<u8>) -> Result<(), Error> {
            unimplemented!();
        }

        async fn stop_all_aps(&mut self) -> Result<(), Error> {
            unimplemented!();
        }
    }
}

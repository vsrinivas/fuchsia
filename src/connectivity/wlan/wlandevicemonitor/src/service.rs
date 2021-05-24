// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::PhyMap,
    anyhow::{Context, Error},
    fidl::epitaph::ChannelEpitaphExt,
    fidl_fuchsia_wlan_device as fidl_dev,
    fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceMonitorRequest},
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    log::error,
    std::sync::Arc,
};

pub(crate) async fn serve_monitor_requests(
    mut req_stream: fidl_svc::DeviceMonitorRequestStream,
    phys: Arc<PhyMap>,
) -> Result<(), Error> {
    while let Some(req) = req_stream.try_next().await.context("error running DeviceService")? {
        match req {
            DeviceMonitorRequest::ListPhys { responder } => responder.send(&mut list_phys(&phys)),
            DeviceMonitorRequest::GetDevPath { phy_id: _, responder } => responder.send(None),
            DeviceMonitorRequest::GetSupportedMacRoles { phy_id: _, responder } => {
                responder.send(None)
            }
            DeviceMonitorRequest::WatchDevices { watcher, control_handle: _ } => {
                watcher.into_channel().close_with_epitaph(zx::Status::NOT_SUPPORTED)
            }
            DeviceMonitorRequest::GetCountry { phy_id, responder } => responder
                .send(&mut get_country(&phys, phy_id).await.map_err(|status| status.into_raw())),
            DeviceMonitorRequest::SetCountry { req, responder } => {
                let status = set_country(&phys, req).await;
                responder.send(status.into_raw())
            }
            DeviceMonitorRequest::ClearCountry { req, responder } => {
                let status = clear_country(&phys, req).await;
                responder.send(status.into_raw())
            }
            DeviceMonitorRequest::CreateIface { req: _, responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw(), None)
            }
            DeviceMonitorRequest::DestroyIface { req: _, responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw())
            }
        }?;
    }

    Ok(())
}

fn list_phys(phys: &PhyMap) -> Vec<u16> {
    phys.get_snapshot().iter().map(|(phy_id, _)| *phy_id).collect()
}

async fn get_country(
    phys: &PhyMap,
    phy_id: u16,
) -> Result<fidl_svc::GetCountryResponse, zx::Status> {
    let phy = phys.get(&phy_id).ok_or(Err(zx::Status::NOT_FOUND))?;
    match phy.proxy.get_country().await {
        Ok(result) => match result {
            Ok(country_code) => Ok(fidl_svc::GetCountryResponse { alpha2: country_code.alpha2 }),
            Err(status) => Err(zx::Status::from_raw(status)),
        },
        Err(e) => {
            error!("Error sending 'GetCountry' request to phy #{}: {}", phy_id, e);
            Err(zx::Status::INTERNAL)
        }
    }
}

async fn set_country(phys: &PhyMap, req: fidl_svc::SetCountryRequest) -> zx::Status {
    let phy_id = req.phy_id;
    let phy = match phys.get(&req.phy_id) {
        None => return zx::Status::NOT_FOUND,
        Some(p) => p,
    };

    let mut phy_req = fidl_dev::CountryCode { alpha2: req.alpha2 };
    match phy.proxy.set_country(&mut phy_req).await {
        Ok(status) => zx::Status::from_raw(status),
        Err(e) => {
            error!("Error sending SetCountry set_country request to phy #{}: {}", phy_id, e);
            zx::Status::INTERNAL
        }
    }
}

async fn clear_country(phys: &PhyMap, req: fidl_svc::ClearCountryRequest) -> zx::Status {
    let phy = match phys.get(&req.phy_id) {
        None => return zx::Status::NOT_FOUND,
        Some(p) => p,
    };

    match phy.proxy.clear_country().await {
        Ok(status) => zx::Status::from_raw(status),
        Err(e) => {
            error!(
                "Error sending ClearCountry clear_country request to phy #{}: {}",
                req.phy_id, e
            );
            zx::Status::INTERNAL
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{device::PhyDevice, watchable_map},
        fidl::endpoints::{create_proxy, Proxy},
        fuchsia_async as fasync,
        futures::{channel::mpsc, task::Poll, StreamExt},
        pin_utils::pin_mut,
        wlan_common::assert_variant,
        wlan_dev::DeviceEnv,
    };

    struct TestValues {
        proxy: fidl_svc::DeviceMonitorProxy,
        req_stream: fidl_svc::DeviceMonitorRequestStream,
        phys: Arc<PhyMap>,
        _phy_events: mpsc::UnboundedReceiver<watchable_map::MapEvent<u16, PhyDevice>>,
    }

    fn test_setup() -> TestValues {
        let (proxy, requests) = create_proxy::<fidl_svc::DeviceMonitorMarker>()
            .expect("failed to create DeviceMonitor proxy");
        let req_stream = requests.into_stream().expect("failed to create request stream");
        let (phys, _phy_events) = PhyMap::new();

        TestValues { proxy, req_stream, phys: Arc::new(phys), _phy_events }
    }

    fn fake_phy(path: &str) -> (PhyDevice, fidl_dev::PhyRequestStream) {
        let (proxy, server) =
            create_proxy::<fidl_dev::PhyMarker>().expect("fake_phy: create_proxy() failed");
        let device = wlan_dev::RealDeviceEnv::device_from_path(path)
            .expect(&format!("fake_phy: failed to open {}", path));
        let stream = server.into_stream().expect("fake_phy: failed to create stream");
        (PhyDevice { proxy, device }, stream)
    }

    fn fake_alpha2() -> [u8; 2] {
        let mut alpha2: [u8; 2] = [0, 0];
        alpha2.copy_from_slice("MX".as_bytes());
        alpha2
    }

    #[test]
    fn test_list_phys() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream, test_values.phys.clone());
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Request the list of available PHYs.
        let list_fut = test_values.proxy.list_phys();
        pin_mut!(list_fut);
        assert_variant!(exec.run_until_stalled(&mut list_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to list the PHYs should complete and no PHYs should be present.
        assert_variant!(exec.run_until_stalled(&mut list_fut),Poll::Ready(Ok(phys)) => {
            assert!(phys.is_empty())
        });

        // Add a PHY to the PhyMap.
        let (phy, _req_stream) = fake_phy("/dev/null");
        test_values.phys.insert(0, phy);

        // Request the list of available PHYs.
        let list_fut = test_values.proxy.list_phys();
        pin_mut!(list_fut);
        assert_variant!(exec.run_until_stalled(&mut list_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to list the PHYs should complete and the PHY should be present.
        assert_variant!(exec.run_until_stalled(&mut list_fut), Poll::Ready(Ok(phys)) => {
            assert_eq!(vec![0u16], phys);
        });

        // Remove the PHY from the map.
        test_values.phys.remove(&0);

        // Request the list of available PHYs.
        let list_fut = test_values.proxy.list_phys();
        pin_mut!(list_fut);
        assert_variant!(exec.run_until_stalled(&mut list_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to list the PHYs should complete and no PHYs should be present.
        assert_variant!(exec.run_until_stalled(&mut list_fut), Poll::Ready(Ok(phys)) => {
            assert!(phys.is_empty())
        });
    }

    #[test]
    fn test_get_dev_path() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream, test_values.phys);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Query a PHY's dev path.
        let query_fut = test_values.proxy.get_dev_path(0);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The attempt to query the PHY's information should fail.
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Ready(Ok(None)));
    }

    #[test]
    fn test_query_phy() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream, test_values.phys);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Query a PHY's MAC roles.
        let query_fut = test_values.proxy.get_supported_mac_roles(0);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The attempt to query the PHY's information should fail.
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Ready(Ok(None)));
    }

    #[test]
    fn test_watch_devices() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream, test_values.phys);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Watch for new devices.
        let (watcher_proxy, watcher_server_end) =
            fidl::endpoints::create_proxy().expect("failed to create watcher proxy");
        test_values.proxy.watch_devices(watcher_server_end).expect("failed to watch devices");

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The channel should be terminated with an epitaph.
        let chan =
            watcher_proxy.into_channel().expect("failed to convert watcher proxy into channel");
        let mut buffer = zx::MessageBuf::new();
        let epitaph_fut = chan.recv_msg(&mut buffer);
        pin_mut!(epitaph_fut);
        assert_variant!(exec.run_until_stalled(&mut epitaph_fut), Poll::Ready(Ok(_)));

        use fidl::encoding::{decode_transaction_header, Decodable, Decoder, EpitaphBody};
        let (header, tail) =
            decode_transaction_header(buffer.bytes()).expect("failed decoding header");
        let mut msg = Decodable::new_empty();
        Decoder::decode_into::<EpitaphBody>(&header, tail, &mut [], &mut msg)
            .expect("failed decoding body");
        assert_eq!(msg.error, zx::Status::NOT_SUPPORTED);
        assert!(chan.is_closed());
    }

    #[test]
    fn test_set_country_succeeds() {
        // Setup environment
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        test_values.phys.insert(phy_id, phy);
        let alpha2 = fake_alpha2();

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::SetCountryRequest()
        let req_msg = fidl_svc::SetCountryRequest { phy_id, alpha2: alpha2.clone() };
        let req_fut = super::set_country(&test_values.phys, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::SetCountry { req, responder }))) => {
                assert_eq!(req.alpha2, alpha2.clone());
                // Pretend to be a WLAN PHY to return the result.
                responder.send(zx::Status::OK.into_raw())
                    .expect("failed to send the response to SetCountry");
            }
        );

        // req_fut should have completed by now. Test the result.
        assert_eq!(exec.run_until_stalled(&mut req_fut), Poll::Ready(zx::Status::OK));
    }

    #[test]
    fn test_set_country_fails() {
        // Setup environment
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        test_values.phys.insert(phy_id, phy);
        let alpha2 = fake_alpha2();

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::SetCountryRequest()
        let req_msg = fidl_svc::SetCountryRequest { phy_id, alpha2: alpha2.clone() };
        let req_fut = super::set_country(&test_values.phys, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::SetCountry { req, responder }))) => (req, responder)
        );
        assert_eq!(req.alpha2, alpha2.clone());

        // Failure case #1: WLAN PHY not responding
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        // Failure case #2: WLAN PHY has not implemented the feature.
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));
        let resp = zx::Status::NOT_SUPPORTED.into_raw();
        responder.send(resp).expect("failed to send the response to SetCountry");
        assert_eq!(Poll::Ready(zx::Status::NOT_SUPPORTED), exec.run_until_stalled(&mut req_fut));
    }

    #[test]
    fn test_get_country_succeeds() {
        // Setup environment
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();

        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        test_values.phys.insert(phy_id, phy);
        let alpha2 = fake_alpha2();

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::SetCountryRequest()
        let req_fut = super::get_country(&test_values.phys, phy_id);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::GetCountry { responder }))) => {
                // Pretend to be a WLAN PHY to return the result.
                responder.send(
                    &mut Ok(fidl_dev::CountryCode { alpha2 })
                ).expect("failed to send the response to SetCountry");
            }
        );

        assert_eq!(
            exec.run_until_stalled(&mut req_fut),
            Poll::Ready(Ok(fidl_svc::GetCountryResponse { alpha2 }))
        );
    }

    #[test]
    fn test_get_country_fails() {
        // Setup environment
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        test_values.phys.insert(phy_id, phy);

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::GetCountryRequest()
        let req_fut = super::get_country(&test_values.phys, phy_id);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::GetCountry { responder }))) => {
                // Pretend to be a WLAN PHY to return the result.
                // Right now the returned country code is not optional, so we just return garbage.
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                    .expect("failed to send the response to SetCountry");
            }
        );

        assert_variant!(exec.run_until_stalled(&mut req_fut), Poll::Ready(Err(_)));
    }

    #[test]
    fn test_clear_country_succeeds() {
        // Setup environment
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        test_values.phys.insert(phy_id, phy);

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::ClearCountryRequest()
        let req_msg = fidl_svc::ClearCountryRequest { phy_id };
        let req_fut = super::clear_country(&test_values.phys, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::ClearCountry { responder }))) => {
                // Pretend to be a WLAN PHY to return the result.
                responder.send(zx::Status::OK.into_raw())
                    .expect("failed to send the response to ClearCountry");
            }
        );

        // req_fut should have completed by now. Test the result.
        assert_eq!(exec.run_until_stalled(&mut req_fut), Poll::Ready(zx::Status::OK));
    }

    #[test]
    fn test_clear_country_fails() {
        // Setup environment
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        test_values.phys.insert(phy_id, phy);

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::ClearCountryRequest()
        let req_msg = fidl_svc::ClearCountryRequest { phy_id };
        let req_fut = super::clear_country(&test_values.phys, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        let responder = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::ClearCountry { responder }))) => responder
        );

        // Failure case #1: WLAN PHY not responding
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        // Failure case #2: WLAN PHY has not implemented the feature.
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));
        let resp = zx::Status::NOT_SUPPORTED.into_raw();
        responder.send(resp).expect("failed to send the response to ClearCountry");
        assert_eq!(Poll::Ready(zx::Status::NOT_SUPPORTED), exec.run_until_stalled(&mut req_fut));
    }

    #[test]
    fn test_create_iface() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream, test_values.phys);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Create an interface for a PHY.
        let mut req = fidl_svc::CreateIfaceRequest {
            phy_id: 0,
            role: fidl_dev::MacRole::Client,
            mac_addr: None,
        };
        let create_fut = test_values.proxy.create_iface(&mut req);
        pin_mut!(create_fut);
        assert_variant!(exec.run_until_stalled(&mut create_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to create the interface should fail.
        assert_variant!(
            exec.run_until_stalled(&mut create_fut),
            Poll::Ready(Ok((status, None))) => {
                assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            }
        );
    }

    #[test]
    fn test_destroy_iface() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream, test_values.phys);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Request the destruction of an interface
        let mut req = fidl_svc::DestroyIfaceRequest { iface_id: 0 };
        let destroy_fut = test_values.proxy.destroy_iface(&mut req);
        pin_mut!(destroy_fut);
        assert_variant!(exec.run_until_stalled(&mut destroy_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to create the interface should fail.
        assert_variant!(
            exec.run_until_stalled(&mut destroy_fut),
            Poll::Ready(Ok(status)) => {
                assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            }
        );
    }
}

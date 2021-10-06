// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        device::{self, IfaceDevice, IfaceMap, NewIface, PhyDevice, PhyMap},
        watcher_service,
    },
    anyhow::{Context, Error},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_wlan_device as fidl_dev,
    fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceMonitorRequest},
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    futures::TryStreamExt,
    log::{error, info},
    std::sync::Arc,
};

pub(crate) async fn serve_monitor_requests(
    mut req_stream: fidl_svc::DeviceMonitorRequestStream,
    phys: Arc<PhyMap>,
    ifaces: Arc<IfaceMap>,
    watcher_service: watcher_service::WatcherService<PhyDevice, IfaceDevice>,
    dev_svc: fidl_svc::DeviceServiceProxy,
) -> Result<(), Error> {
    while let Some(req) = req_stream.try_next().await.context("error running DeviceService")? {
        match req {
            DeviceMonitorRequest::ListPhys { responder } => responder.send(&mut list_phys(&phys)),
            DeviceMonitorRequest::GetDevPath { phy_id, responder } => {
                responder.send(get_dev_path(&phys, phy_id).as_deref())
            }
            DeviceMonitorRequest::GetSupportedMacRoles { phy_id, responder } => {
                match query_phy(&phys, phy_id).await {
                    Some(mut info) => responder.send(Some(&mut info.supported_mac_roles.drain(..))),
                    None => responder.send(None),
                }
            }
            DeviceMonitorRequest::WatchDevices { watcher, control_handle: _ } => {
                watcher_service
                    .add_watcher(watcher)
                    .unwrap_or_else(|e| error!("error registering a device watcher: {}", e));
                Ok(())
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
            DeviceMonitorRequest::CreateIface { req, responder } => {
                match create_iface(&dev_svc, &phys, req).await {
                    Ok(new_iface) => {
                        info!("iface #{} started ({:?})", new_iface.id, new_iface.phy_ownership);
                        ifaces.insert(
                            new_iface.id,
                            IfaceDevice { phy_ownership: new_iface.phy_ownership },
                        );

                        let resp = fidl_svc::CreateIfaceResponse { iface_id: new_iface.id };
                        responder.send(zx::sys::ZX_OK, Some(resp).as_mut())
                    }
                    Err(status) => responder.send(status.into_raw(), None),
                }
            }
            DeviceMonitorRequest::DestroyIface { req, responder } => {
                let result = destroy_iface(&phys, &ifaces, req.iface_id).await;
                let status = into_status_and_opt(result).0;
                responder.send(status.into_raw())
            }
        }?;
    }

    Ok(())
}

fn list_phys(phys: &PhyMap) -> Vec<u16> {
    phys.get_snapshot().iter().map(|(phy_id, _)| *phy_id).collect()
}

fn get_dev_path(phys: &PhyMap, phy_id: u16) -> Option<String> {
    let phy = phys.get(&phy_id)?;
    Some(phy.device.path().to_string_lossy().to_string())
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

async fn query_phy(phys: &PhyMap, id: u16) -> Option<fidl_dev::PhyInfo> {
    let phy = phys.get(&id)?;
    let query_result = phy
        .proxy
        .query()
        .await
        .map_err(move |e| {
            error!("query_phy(id = {}): error sending 'Query' request to phy: {}", id, e);
        })
        .ok()?;
    zx::Status::ok(query_result.status)
        .map_err(move |e| {
            error!("query_phy(id = {}): returned an error: {}", id, e);
        })
        .ok()?;
    Some(query_result.info)
}

async fn create_iface(
    dev_svc: &fidl_svc::DeviceServiceProxy,
    phys: &PhyMap,
    req: fidl_svc::CreateIfaceRequest,
) -> Result<NewIface, zx::Status> {
    let phy_id = req.phy_id;
    let phy = phys.get(&req.phy_id).ok_or(zx::Status::NOT_FOUND)?;

    let (mlme_client, mlme_server) = create_endpoints::<fidl_mlme::MlmeMarker>()
        .map_err(|e| {
            error!("failed to create MlmeProxy: {}", e);
            zx::Status::INTERNAL
        })
        .map(|(p, c)| (p, Some(c.into_channel())))?;

    let mut phy_req = fidl_dev::CreateIfaceRequest {
        role: req.role,
        mlme_channel: mlme_server,
        init_sta_addr: req.sta_addr,
    };
    let r = phy.proxy.create_iface(&mut phy_req).await.map_err(move |e| {
        error!("Error sending 'CreateIface' request to phy #{}: {}", phy_id, e);
        zx::Status::INTERNAL
    })?;
    zx::Status::ok(r.status)?;

    let (status, iface_id) = dev_svc
        .add_iface(&mut fidl_svc::AddIfaceRequest {
            phy_id,
            assigned_iface_id: r.iface_id,
            iface: mlme_client,
        })
        .await
        .map_err(|e| {
            error!("failed to add interfaces to DeviceService: {:?}", e);
            zx::Status::INTERNAL
        })?;
    zx::Status::ok(status)?;
    let added_iface = iface_id.ok_or(zx::Status::INTERNAL)?;

    Ok(NewIface {
        id: added_iface.iface_id,
        phy_ownership: device::PhyOwnership { phy_id, phy_assigned_id: r.iface_id },
    })
}

async fn destroy_iface(phys: &PhyMap, ifaces: &IfaceMap, id: u16) -> Result<(), zx::Status> {
    info!("destroy_iface(id = {})", id);
    let iface = ifaces.get(&id).ok_or(zx::Status::NOT_FOUND)?;
    let phy_ownership = &iface.phy_ownership;
    let phy = phys.get(&phy_ownership.phy_id).ok_or(zx::Status::NOT_FOUND)?;
    let mut phy_req = fidl_dev::DestroyIfaceRequest { id: phy_ownership.phy_assigned_id };
    let r = phy.proxy.destroy_iface(&mut phy_req).await.map_err(move |e| {
        error!("Error sending 'DestroyIface' request to phy {:?}: {}", phy_ownership, e);
        zx::Status::INTERNAL
    })?;

    // If the removal is successful or the interface cannot be found, update the internal
    // accounting.
    match r.status {
        zx::sys::ZX_OK => ifaces.remove(&id),
        zx::sys::ZX_ERR_NOT_FOUND => {
            if ifaces.get_snapshot().contains_key(&id) {
                info!(
                    "Encountered NOT_FOUND while removing iface #{} potentially due to recovery.",
                    id
                );
                ifaces.remove(&id);
            }
        }
        _ => {}
    }
    zx::Status::ok(r.status)
}

fn into_status_and_opt<T>(r: Result<T, zx::Status>) -> (zx::Status, Option<T>) {
    match r {
        Ok(x) => (zx::Status::OK, Some(x)),
        Err(status) => (status, None),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::device::PhyOwnership,
        fidl::endpoints::create_proxy,
        fuchsia_async as fasync,
        futures::{future::BoxFuture, task::Poll, StreamExt},
        ieee80211::NULL_MAC_ADDR,
        pin_utils::pin_mut,
        std::fs::File,
        tempfile,
        test_case::test_case,
        void::Void,
        wlan_common::assert_variant,
    };

    struct TestValues {
        monitor_proxy: fidl_svc::DeviceMonitorProxy,
        monitor_req_stream: fidl_svc::DeviceMonitorRequestStream,
        dev_proxy: fidl_svc::DeviceServiceProxy,
        #[allow(unused)]
        dev_req_stream: fidl_svc::DeviceServiceRequestStream,
        phys: Arc<PhyMap>,
        ifaces: Arc<IfaceMap>,
        watcher_service: watcher_service::WatcherService<PhyDevice, IfaceDevice>,
        watcher_fut: BoxFuture<'static, Result<Void, Error>>,
    }

    fn test_setup() -> TestValues {
        let (monitor_proxy, requests) = create_proxy::<fidl_svc::DeviceMonitorMarker>()
            .expect("failed to create DeviceMonitor proxy");
        let monitor_req_stream = requests.into_stream().expect("failed to create request stream");
        let (dev_proxy, requests) = create_proxy::<fidl_svc::DeviceServiceMarker>()
            .expect("failed to create DeviceMonitor proxy");
        let dev_req_stream = requests.into_stream().expect("failed to create request stream");
        let (phys, phy_events) = PhyMap::new();
        let phys = Arc::new(phys);

        let (ifaces, iface_events) = IfaceMap::new();
        let ifaces = Arc::new(ifaces);

        let (watcher_service, watcher_fut) =
            watcher_service::serve_watchers(phys.clone(), ifaces.clone(), phy_events, iface_events);

        TestValues {
            monitor_proxy,
            monitor_req_stream,
            dev_proxy,
            dev_req_stream,
            phys,
            ifaces,
            watcher_service,
            watcher_fut: Box::pin(watcher_fut),
        }
    }

    fn fake_phy() -> (PhyDevice, fidl_dev::PhyRequestStream) {
        let (proxy, server) =
            create_proxy::<fidl_dev::PhyMarker>().expect("fake_phy: create_proxy() failed");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let test_path = temp_dir.path().join("test");
        let file = File::create(test_path.clone()).expect("failed to open file");
        let device = wlan_dev::Device { node: file, path: test_path };
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
        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys.clone(),
            test_values.ifaces.clone(),
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Request the list of available PHYs.
        let list_fut = test_values.monitor_proxy.list_phys();
        pin_mut!(list_fut);
        assert_variant!(exec.run_until_stalled(&mut list_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to list the PHYs should complete and no PHYs should be present.
        assert_variant!(exec.run_until_stalled(&mut list_fut),Poll::Ready(Ok(phys)) => {
            assert!(phys.is_empty())
        });

        // Add a PHY to the PhyMap.
        let (phy, _req_stream) = fake_phy();
        test_values.phys.insert(0, phy);

        // Request the list of available PHYs.
        let list_fut = test_values.monitor_proxy.list_phys();
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
        let list_fut = test_values.monitor_proxy.list_phys();
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
    fn test_get_dev_path_success() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, _) = fake_phy();

        let expected_path = phy
            .device
            .path
            .as_path()
            .as_os_str()
            .to_os_string()
            .into_string()
            .expect("could not convert path to string.");

        test_values.phys.insert(10u16, phy);

        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys,
            test_values.ifaces,
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Initiate a GetDevPath request. The returned future should not be able
        // to produce a result immediately
        let query_fut = test_values.monitor_proxy.get_dev_path(10u16);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Our original future should complete, and return the dev path.
        assert_variant!(
            exec.run_until_stalled(&mut query_fut),
            Poll::Ready(Ok(Some(path))) => {
                assert_eq!(path, expected_path);
            }
        );
    }

    #[test]
    fn test_get_dev_path_phy_not_found() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys,
            test_values.ifaces.clone(),
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Query a PHY's dev path.
        let query_fut = test_values.monitor_proxy.get_dev_path(0);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The attempt to query the PHY's information should fail.
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Ready(Ok(None)));
    }

    #[test]
    fn test_get_mac_roles_success() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, mut phy_stream) = fake_phy();
        test_values.phys.insert(10u16, phy);

        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys,
            test_values.ifaces,
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Initiate a GetMacRoles request. The returned future should not be able
        // to produce a result immediately
        let query_fut = test_values.monitor_proxy.get_supported_mac_roles(10u16);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // The call above should trigger a Query message to the phy.
        // Pretend that we are the phy and read the message from the other side.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);
        let responder = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::Query { responder }))) => responder
        );

        // Reply with a fake phy info
        let mut phy_info = fake_phy_info();
        phy_info.supported_mac_roles.push(fidl_dev::MacRole::Client);
        responder
            .send(&mut fidl_dev::QueryResponse { status: zx::sys::ZX_OK, info: phy_info })
            .expect("failed to send QueryResponse");
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Our original future should complete now and the client role should be reported.
        assert_variant!(
            exec.run_until_stalled(&mut query_fut),
            Poll::Ready(Ok(Some(roles))) => {
                assert_eq!(roles.len(), 1);
                assert_eq!(roles[0], fidl_dev::MacRole::Client);
            }
        );
    }

    #[test]
    fn test_get_mac_roles_phy_not_found() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys,
            test_values.ifaces.clone(),
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Query a PHY's dev path.
        let query_fut = test_values.monitor_proxy.get_supported_mac_roles(0);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The attempt to query the PHY's information should fail.
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Ready(Ok(None)));
    }

    #[test]
    fn test_watch_devices_add_remove_phy() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let watcher_fut = test_values.watcher_fut;
        pin_mut!(watcher_fut);

        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys.clone(),
            test_values.ifaces.clone(),
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Watch for new devices.
        let (watcher_proxy, watcher_server_end) =
            fidl::endpoints::create_proxy().expect("failed to create watcher proxy");
        test_values
            .monitor_proxy
            .watch_devices(watcher_server_end)
            .expect("failed to watch devices");

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Initially there should be no devices and the future should be pending.
        let mut events_fut = watcher_proxy.take_event_stream();
        let next_fut = events_fut.try_next();
        pin_mut!(next_fut);
        assert_variant!(exec.run_until_stalled(&mut next_fut), Poll::Pending);

        // Add a PHY and make sure the update is received.
        let (phy, _phy_stream) = fake_phy();
        test_values.phys.insert(0, phy);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnPhyAdded { phy_id: 0 })))
        );

        // Remove the PHY and make sure the update is received.
        test_values.phys.remove(&0);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnPhyRemoved { phy_id: 0 })))
        );
    }

    #[test]
    fn test_watch_devices_remove_existing_phy() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let watcher_fut = test_values.watcher_fut;
        pin_mut!(watcher_fut);

        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys.clone(),
            test_values.ifaces.clone(),
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Add a PHY before beginning to watch for devices.
        let (phy, _phy_stream) = fake_phy();
        test_values.phys.insert(0, phy);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);

        // Watch for new devices.
        let (watcher_proxy, watcher_server_end) =
            fidl::endpoints::create_proxy().expect("failed to create watcher proxy");
        test_values
            .monitor_proxy
            .watch_devices(watcher_server_end)
            .expect("failed to watch devices");

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Start listening for device events.
        let mut events_fut = watcher_proxy.take_event_stream();
        let next_fut = events_fut.try_next();
        pin_mut!(next_fut);
        assert_variant!(exec.run_until_stalled(&mut next_fut), Poll::Pending);

        // We should be notified of the existing PHY.
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnPhyAdded { phy_id: 0 })))
        );

        // Remove the PHY and make sure the update is received.
        test_values.phys.remove(&0);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnPhyRemoved { phy_id: 0 })))
        );
    }

    #[test]
    fn test_watch_devices_add_remove_iface() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let watcher_fut = test_values.watcher_fut;
        pin_mut!(watcher_fut);

        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys.clone(),
            test_values.ifaces.clone(),
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Watch for new devices.
        let (watcher_proxy, watcher_server_end) =
            fidl::endpoints::create_proxy().expect("failed to create watcher proxy");
        test_values
            .monitor_proxy
            .watch_devices(watcher_server_end)
            .expect("failed to watch devices");

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Initially there should be no devices and the future should be pending.
        let mut events_fut = watcher_proxy.take_event_stream();
        let next_fut = events_fut.try_next();
        pin_mut!(next_fut);
        assert_variant!(exec.run_until_stalled(&mut next_fut), Poll::Pending);

        // Add an interface and make sure the update is received.
        let fake_iface =
            IfaceDevice { phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 } };
        test_values.ifaces.insert(0, fake_iface);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnIfaceAdded { iface_id: 0 })))
        );

        // Remove the PHY and make sure the update is received.
        test_values.ifaces.remove(&0);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnIfaceRemoved { iface_id: 0 })))
        );
    }

    #[test]
    fn test_watch_devices_remove_existing_iface() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let watcher_fut = test_values.watcher_fut;
        pin_mut!(watcher_fut);

        let service_fut = serve_monitor_requests(
            test_values.monitor_req_stream,
            test_values.phys.clone(),
            test_values.ifaces.clone(),
            test_values.watcher_service,
            test_values.dev_proxy,
        );
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Add an interface before beginning to watch for devices.
        let fake_iface =
            IfaceDevice { phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 } };
        test_values.ifaces.insert(0, fake_iface);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);

        // Watch for new devices.
        let (watcher_proxy, watcher_server_end) =
            fidl::endpoints::create_proxy().expect("failed to create watcher proxy");
        test_values
            .monitor_proxy
            .watch_devices(watcher_server_end)
            .expect("failed to watch devices");

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Start listening for device events.
        let mut events_fut = watcher_proxy.take_event_stream();
        let next_fut = events_fut.try_next();
        pin_mut!(next_fut);
        assert_variant!(exec.run_until_stalled(&mut next_fut), Poll::Pending);

        // We should be notified of the existing interface.
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnIfaceAdded { iface_id: 0 })))
        );

        // Remove the interface and make sure the update is received.
        test_values.ifaces.remove(&0);
        assert_variant!(exec.run_until_stalled(&mut watcher_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut next_fut),
            Poll::Ready(Ok(Some(fidl_svc::DeviceWatcherEvent::OnIfaceRemoved { iface_id: 0 })))
        );
    }

    #[test]
    fn test_set_country_succeeds() {
        // Setup environment
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let (phy, mut phy_stream) = fake_phy();
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
        let (phy, mut phy_stream) = fake_phy();
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

        let (phy, mut phy_stream) = fake_phy();
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
        let (phy, mut phy_stream) = fake_phy();
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
        let (phy, mut phy_stream) = fake_phy();
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
        let (phy, mut phy_stream) = fake_phy();
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

    fn fake_phy_info() -> fidl_dev::PhyInfo {
        fidl_dev::PhyInfo {
            id: 10,
            dev_path: Some("/dev/null".to_string()),
            supported_mac_roles: Vec::new(),
        }
    }

    #[test_case(false, false, false; "CreateIface without MACsucceeds")]
    #[test_case(true, false, false; "CreateIface with MAC succeeds")]
    #[test_case(false, true, false; "CreateIface fails on interface creation")]
    #[test_case(false, false, true; "CreateIface fails on interface addition")]
    fn test_create_iface(with_mac: bool, create_iface_fails: bool, add_iface_fails: bool) {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let mut test_values = test_setup();

        let (phy, mut phy_stream) = fake_phy();
        test_values.phys.insert(10, phy);

        // Initiate a CreateIface request. The returned future should not be able
        // to produce a result immediately
        let create_fut = super::create_iface(
            &test_values.dev_proxy,
            &test_values.phys,
            fidl_svc::CreateIfaceRequest {
                phy_id: 10,
                role: fidl_dev::MacRole::Client,
                sta_addr: if with_mac { [0, 1, 2, 3, 4, 5] } else { NULL_MAC_ADDR },
            },
        );
        pin_mut!(create_fut);
        assert_variant!(exec.run_until_stalled(&mut create_fut), Poll::Pending);

        // Validate the PHY request
        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::CreateIface { req, responder }))) => {
                assert_eq!(fidl_dev::MacRole::Client, req.role);

                if with_mac {
                    assert_eq!(req.init_sta_addr, [0, 1, 2, 3, 4, 5]);
                }

                let mut response = if create_iface_fails {
                    fidl_dev::CreateIfaceResponse { status: zx::sys::ZX_ERR_NOT_SUPPORTED, iface_id: 0 }
                } else {
                    fidl_dev::CreateIfaceResponse { status: zx::sys::ZX_OK, iface_id: 123 }
                };

                responder.send(&mut response).expect("failed to send CreateIfaceResponse");
            }
        );

        // If this case should fail on interface creation, the future should complete here with an
        // error.
        if create_iface_fails {
            assert_variant!(
                exec.run_until_stalled(&mut create_fut),
                Poll::Ready(Err(zx::Status::NOT_SUPPORTED))
            );
            return;
        }

        // Progress the interface creation process so that the new MLME channel is passed to the
        // device service.
        assert_variant!(exec.run_until_stalled(&mut create_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.dev_req_stream.next()),
            Poll::Ready(Some(Ok(fidl_svc::DeviceServiceRequest::AddIface { req, responder}))) => {
                assert_eq!(req.phy_id, 10);
                assert_eq!(req.assigned_iface_id, 123);

                let (status, mut response) = if add_iface_fails {
                    (zx::Status::NOT_SUPPORTED.into_raw(), None)
                } else {
                    (zx::Status::OK.into_raw(), Some(fidl_svc::AddIfaceResponse { iface_id: 5 }))
                };
                responder.send(status, response.as_mut()).expect("failed to send AddIface response");
            }
        );

        // The original future should resolve into a response.
        assert_variant!(exec.run_until_stalled(&mut create_fut),
            Poll::Ready(response) => {
                if add_iface_fails {
                    assert_variant!(response, Err(zx::Status::NOT_SUPPORTED));
                } else {
                    let response = response.expect("CreateIface failed unexpectedly");
                    assert_eq!(5, response.id);
                    assert_eq!(
                        device::PhyOwnership { phy_id: 10, phy_assigned_id: 123 },
                        response.phy_ownership
                    );
                }
            }
        );
    }

    #[test]
    fn create_iface_on_invalid_phy_id() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();

        let fut = super::create_iface(
            &test_values.dev_proxy,
            &test_values.phys,
            fidl_svc::CreateIfaceRequest {
                phy_id: 10,
                role: fidl_dev::MacRole::Client,
                sta_addr: NULL_MAC_ADDR,
            },
        );
        pin_mut!(fut);
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Err(zx::Status::NOT_FOUND)),
            "expected failure on invalid PHY"
        );
    }

    fn fake_destroy_iface_env(
        phy_map: &PhyMap,
        iface_map: &IfaceMap,
    ) -> fidl_dev::PhyRequestStream {
        let (phy, phy_stream) = fake_phy();
        phy_map.insert(10, phy);
        iface_map.insert(
            42,
            device::IfaceDevice { phy_ownership: PhyOwnership { phy_id: 10, phy_assigned_id: 0 } },
        );
        phy_stream
    }

    #[test]
    fn destroy_iface_success() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let mut phy_stream = fake_destroy_iface_env(&test_values.phys, &test_values.ifaces);

        let destroy_fut = super::destroy_iface(&test_values.phys, &test_values.ifaces, 42);
        pin_mut!(destroy_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut destroy_fut));

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::DestroyIface { req, responder }))) => (req, responder)
        );

        // Verify the destroy iface request to the corresponding PHY is correct.
        assert_eq!(0, req.id);

        responder
            .send(&mut fidl_dev::DestroyIfaceResponse { status: zx::sys::ZX_OK })
            .expect("failed to send DestroyIfaceResponse");
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut destroy_fut));

        // Verify iface was removed from available ifaces.
        assert!(test_values.ifaces.get(&42u16).is_none(), "iface expected to be deleted");
    }

    #[test]
    fn destroy_iface_failure() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let mut phy_stream = fake_destroy_iface_env(&test_values.phys, &test_values.ifaces);

        let destroy_fut = super::destroy_iface(&test_values.phys, &test_values.ifaces, 42);
        pin_mut!(destroy_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut destroy_fut));

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::DestroyIface { req, responder }))) => (req, responder)
        );

        // Verify the destroy iface request to the corresponding PHY is correct.
        assert_eq!(0, req.id);

        responder
            .send(&mut fidl_dev::DestroyIfaceResponse { status: zx::sys::ZX_ERR_INTERNAL })
            .expect("failed to send DestroyIfaceResponse");
        assert_eq!(
            Poll::Ready(Err(zx::Status::INTERNAL)),
            exec.run_until_stalled(&mut destroy_fut)
        );

        // Verify iface was not removed from available ifaces.
        assert!(test_values.ifaces.get(&42u16).is_some(), "iface expected to not be deleted");
    }

    #[test]
    fn destroy_iface_recovery() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let mut phy_stream = fake_destroy_iface_env(&test_values.phys, &test_values.ifaces);

        let destroy_fut = super::destroy_iface(&test_values.phys, &test_values.ifaces, 42);
        pin_mut!(destroy_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut destroy_fut));

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(fidl_dev::PhyRequest::DestroyIface { req, responder }))) => (req, responder)
        );

        // Verify the destroy iface request to the corresponding PHY is correct.
        assert_eq!(0, req.id);

        // In the recovery scenario, the interface will have already been destroyed and the PHY
        // will have no record of it and will reply to the destruction request with a
        // ZX_ERR_NOT_FOUND.  In this case, we should verify that the internal accounting is still
        // updated.
        responder
            .send(&mut fidl_dev::DestroyIfaceResponse { status: zx::sys::ZX_ERR_NOT_FOUND })
            .expect("failed to send DestroyIfaceResponse");
        assert_eq!(
            Poll::Ready(Err(zx::Status::NOT_FOUND)),
            exec.run_until_stalled(&mut destroy_fut)
        );

        // Verify iface was removed from available ifaces.
        assert!(test_values.ifaces.get(&42u16).is_none(), "iface should have been removed.");
    }

    #[test]
    fn destroy_iface_not_found() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let test_values = test_setup();
        let _phy_stream = fake_destroy_iface_env(&test_values.phys, &test_values.ifaces);

        let fut = super::destroy_iface(&test_values.phys, &test_values.ifaces, 43);
        pin_mut!(fut);
        assert_eq!(Poll::Ready(Err(zx::Status::NOT_FOUND)), exec.run_until_stalled(&mut fut));
    }
}

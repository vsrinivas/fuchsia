// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use core::sync::atomic::AtomicUsize;
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceServiceRequest};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MinstrelStatsResponse};
use fuchsia_async as fasync;
use fuchsia_inspect_contrib::{auto_persist, inspect_log};
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, prelude::*};
use log::{error, info, warn};
use std::sync::{atomic::Ordering, Arc};
use wlan_sme::serve::SmeServer;

use crate::device::{self, IfaceMap};
use crate::inspect;
use crate::ServiceCfg;

/// Thread-safe counter for spawned ifaces.
pub struct IfaceCounter(AtomicUsize);

impl IfaceCounter {
    pub fn new() -> Self {
        Self(AtomicUsize::new(0))
    }

    /// Provides the caller with a new unique id.
    pub fn next_iface_id(&self) -> usize {
        self.0.fetch_add(1, Ordering::SeqCst)
    }
}

pub async fn serve_device_requests(
    iface_counter: Arc<IfaceCounter>,
    cfg: ServiceCfg,
    ifaces: Arc<IfaceMap>,
    mut req_stream: fidl_svc::DeviceServiceRequestStream,
    inspect_tree: Arc<inspect::WlanstackTree>,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    persistence_req_sender: auto_persist::PersistenceReqSender,
) -> Result<(), anyhow::Error> {
    while let Some(req) = req_stream.try_next().await.context("error running DeviceService")? {
        // Note that errors from responder.send() are propagated intentionally.
        // If we fail to send a response, the only way to recover is to stop serving the
        // client and close the channel. Otherwise, the client would be left hanging
        // forever.
        match req {
            DeviceServiceRequest::ListIfaces { responder } => {
                responder.send(&mut list_ifaces(&ifaces))
            }
            DeviceServiceRequest::QueryIface { iface_id, responder } => {
                let result = query_iface(&ifaces, iface_id);
                let (status, mut response) = into_status_and_opt(result);
                responder.send(status.into_raw(), response.as_mut())
            }
            DeviceServiceRequest::AddIface { req, responder } => {
                let mut add_iface_result = add_iface(
                    req,
                    &cfg,
                    &ifaces,
                    &iface_counter,
                    &inspect_tree,
                    dev_monitor_proxy.clone(),
                    persistence_req_sender.clone(),
                )
                .await;
                responder.send(add_iface_result.status, add_iface_result.iface_id.as_mut())?;
                let serve_sme_fut = add_iface_result.result?;
                fasync::Task::spawn(serve_sme_fut).detach();
                Ok(())
            }
            DeviceServiceRequest::GetClientSme { iface_id, sme, responder } => {
                let status = get_client_sme(&ifaces, iface_id, sme);
                responder.send(status.into_raw())
            }
            DeviceServiceRequest::GetApSme { iface_id, sme, responder } => {
                let status = get_ap_sme(&ifaces, iface_id, sme);
                responder.send(status.into_raw())
            }
            DeviceServiceRequest::GetMeshSme { iface_id, sme, responder } => {
                let status = get_mesh_sme(&ifaces, iface_id, sme);
                responder.send(status.into_raw())
            }
            DeviceServiceRequest::GetIfaceStats { .. } => {
                // TODO(fxbug.dev/82654) - Remove this API call
                warn!("GetIfaceStats is no longer supported");
                Ok(())
            }
            DeviceServiceRequest::GetIfaceCounterStats { iface_id, responder } => {
                let mut resp = match get_iface_counter_stats(&ifaces, iface_id).await {
                    Ok(resp) => match resp {
                        fidl_mlme::GetIfaceCounterStatsResponse::Stats(stats) => {
                            fidl_svc::GetIfaceCounterStatsResponse::Stats(stats)
                        }
                        fidl_mlme::GetIfaceCounterStatsResponse::ErrorStatus(status) => {
                            fidl_svc::GetIfaceCounterStatsResponse::ErrorStatus(status)
                        }
                    },
                    Err(status) => {
                        fidl_svc::GetIfaceCounterStatsResponse::ErrorStatus(status.into_raw())
                    }
                };
                responder.send(&mut resp)
            }
            DeviceServiceRequest::GetIfaceHistogramStats { iface_id, responder } => {
                let mut resp = match get_iface_histogram_stats(&ifaces, iface_id).await {
                    Ok(resp) => match resp {
                        fidl_mlme::GetIfaceHistogramStatsResponse::Stats(stats) => {
                            fidl_svc::GetIfaceHistogramStatsResponse::Stats(stats)
                        }
                        fidl_mlme::GetIfaceHistogramStatsResponse::ErrorStatus(status) => {
                            fidl_svc::GetIfaceHistogramStatsResponse::ErrorStatus(status)
                        }
                    },
                    Err(status) => {
                        fidl_svc::GetIfaceHistogramStatsResponse::ErrorStatus(status.into_raw())
                    }
                };
                responder.send(&mut resp)
            }
            DeviceServiceRequest::GetMinstrelList { iface_id, responder } => {
                let (status, mut peers) = list_minstrel_peers(&ifaces, iface_id).await;
                responder.send(status.into_raw(), &mut peers)
            }
            DeviceServiceRequest::GetMinstrelStats { iface_id, peer_addr, responder } => {
                let (status, mut peer) = get_minstrel_stats(&ifaces, iface_id, peer_addr).await;
                responder.send(status.into_raw(), peer.as_deref_mut())
            }
        }?;
    }
    Ok(())
}

fn into_status_and_opt<T>(r: Result<T, zx::Status>) -> (zx::Status, Option<T>) {
    match r {
        Ok(x) => (zx::Status::OK, Some(x)),
        Err(status) => (status, None),
    }
}

fn list_ifaces(ifaces: &IfaceMap) -> fidl_svc::ListIfacesResponse {
    let list = ifaces
        .get_snapshot()
        .iter()
        .map(|(iface_id, _iface)| fidl_svc::IfaceListItem { iface_id: *iface_id })
        .collect();
    fidl_svc::ListIfacesResponse { ifaces: list }
}

fn query_iface(ifaces: &IfaceMap, id: u16) -> Result<fidl_svc::QueryIfaceResponse, zx::Status> {
    info!("query_iface(id = {})", id);
    let iface = ifaces.get(&id).ok_or(zx::Status::NOT_FOUND)?;

    let role = match iface.device_info.role {
        fidl_common::WlanMacRole::Client => fidl_common::WlanMacRole::Client,
        fidl_common::WlanMacRole::Ap => fidl_common::WlanMacRole::Ap,
        fidl_common::WlanMacRole::Mesh => fidl_common::WlanMacRole::Mesh,
    };

    let phy_id = iface.phy_ownership.phy_id;
    let phy_assigned_id = iface.phy_ownership.phy_assigned_id;
    let sta_addr = iface.device_info.sta_addr;
    let driver_features = iface.device_info.driver_features.clone();
    Ok(fidl_svc::QueryIfaceResponse {
        role,
        id,
        sta_addr,
        phy_id,
        phy_assigned_id,
        driver_features,
    })
}

fn get_client_sme(
    ifaces: &IfaceMap,
    iface_id: u16,
    endpoint: wlan_sme::serve::client::Endpoint,
) -> zx::Status {
    let iface = ifaces.get(&iface_id);
    let server = match iface {
        None => return zx::Status::NOT_FOUND,
        Some(ref iface) => match iface.sme_server {
            SmeServer::Client(ref server) => server,
            _ => return zx::Status::NOT_SUPPORTED,
        },
    };
    match server.unbounded_send(endpoint) {
        Ok(()) => zx::Status::OK,
        Err(e) => {
            error!("error sending an endpoint to the SME server future: {}", e);
            zx::Status::INTERNAL
        }
    }
}

fn get_ap_sme(
    ifaces: &IfaceMap,
    iface_id: u16,
    endpoint: wlan_sme::serve::ap::Endpoint,
) -> zx::Status {
    let iface = ifaces.get(&iface_id);
    let server = match iface {
        None => return zx::Status::NOT_FOUND,
        Some(ref iface) => match iface.sme_server {
            SmeServer::Ap(ref server) => server,
            _ => return zx::Status::NOT_SUPPORTED,
        },
    };
    match server.unbounded_send(endpoint) {
        Ok(()) => zx::Status::OK,
        Err(e) => {
            error!("error sending an endpoint to the SME server future: {}", e);
            zx::Status::INTERNAL
        }
    }
}

fn get_mesh_sme(
    ifaces: &IfaceMap,
    iface_id: u16,
    endpoint: wlan_sme::serve::mesh::Endpoint,
) -> zx::Status {
    let iface = ifaces.get(&iface_id);
    let server = match iface {
        None => return zx::Status::NOT_FOUND,
        Some(ref iface) => match iface.sme_server {
            SmeServer::Mesh(ref server) => server,
            _ => return zx::Status::NOT_SUPPORTED,
        },
    };
    match server.unbounded_send(endpoint) {
        Ok(()) => zx::Status::OK,
        Err(e) => {
            error!("error sending an endpoint to the SME server future: {}", e);
            zx::Status::INTERNAL
        }
    }
}

pub async fn get_iface_counter_stats(
    ifaces: &IfaceMap,
    iface_id: u16,
) -> Result<fidl_mlme::GetIfaceCounterStatsResponse, zx::Status> {
    let iface = ifaces.get(&iface_id).ok_or(zx::Status::NOT_FOUND)?;
    iface.mlme_proxy.get_iface_counter_stats().await.map_err(|e| {
        warn!("get_iface_counter_stats failed: {}", e);
        zx::Status::INTERNAL
    })
}

pub async fn get_iface_histogram_stats(
    ifaces: &IfaceMap,
    iface_id: u16,
) -> Result<fidl_mlme::GetIfaceHistogramStatsResponse, zx::Status> {
    let iface = ifaces.get(&iface_id).ok_or(zx::Status::NOT_FOUND)?;
    iface.mlme_proxy.get_iface_histogram_stats().await.map_err(|e| {
        warn!("get_iface_histogram_stats failed: {}", e);
        zx::Status::INTERNAL
    })
}

async fn list_minstrel_peers(
    ifaces: &IfaceMap,
    iface_id: u16,
) -> (zx::Status, fidl_fuchsia_wlan_minstrel::Peers) {
    let empty_peer_list = fidl_fuchsia_wlan_minstrel::Peers { addrs: vec![] };
    let iface = match ifaces.get(&iface_id) {
        Some(iface) => iface,
        None => return (zx::Status::NOT_FOUND, empty_peer_list),
    };
    match iface.mlme_proxy.list_minstrel_peers().await {
        Ok(resp) => (zx::Status::OK, resp.peers),
        Err(_) => (zx::Status::INTERNAL, empty_peer_list),
    }
}

async fn get_minstrel_stats(
    ifaces: &IfaceMap,
    iface_id: u16,
    peer_addr: [u8; 6],
) -> (zx::Status, Option<Box<fidl_fuchsia_wlan_minstrel::Peer>>) {
    let iface = match ifaces.get(&iface_id) {
        Some(iface) => iface,
        None => return (zx::Status::NOT_FOUND, None),
    };
    let mut req = fidl_mlme::MinstrelStatsRequest { peer_addr };
    match iface.mlme_proxy.get_minstrel_stats(&mut req).await {
        Ok(MinstrelStatsResponse { peer }) => (zx::Status::OK, peer),
        Err(_) => (zx::Status::INTERNAL, None),
    }
}

struct AddIfaceResult {
    result: Result<BoxFuture<'static, ()>, anyhow::Error>,
    status: i32,
    iface_id: Option<fidl_svc::AddIfaceResponse>,
}

impl AddIfaceResult {
    fn from_error(e: anyhow::Error, status: i32) -> Self {
        AddIfaceResult { result: Err(e), status, iface_id: None }
    }

    fn ok(fut: BoxFuture<'static, ()>, iface_id: u16) -> Self {
        AddIfaceResult {
            result: Ok(fut),
            status: zx::sys::ZX_OK,
            iface_id: Some(fidl_svc::AddIfaceResponse { iface_id }),
        }
    }
}

async fn add_iface(
    req: fidl_svc::AddIfaceRequest,
    cfg: &ServiceCfg,
    ifaces: &Arc<IfaceMap>,
    iface_counter: &Arc<IfaceCounter>,
    inspect_tree: &Arc<inspect::WlanstackTree>,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    persistence_req_sender: auto_persist::PersistenceReqSender,
) -> AddIfaceResult {
    // Utilize the provided MLME channel to construct a future to serve the SME.
    let mlme_channel = match fasync::Channel::from_channel(req.iface.into_channel()) {
        Ok(channel) => channel,
        Err(e) => return AddIfaceResult::from_error(e.into(), zx::sys::ZX_ERR_INTERNAL),
    };
    let mlme_proxy = fidl_mlme::MlmeProxy::new(mlme_channel);

    let id = iface_counter.next_iface_id() as u16;
    let phy_ownership =
        device::PhyOwnership { phy_id: req.phy_id, phy_assigned_id: req.assigned_iface_id };
    info!("iface #{} added ({:?})", id, phy_ownership);

    let inspect_tree = inspect_tree.clone();
    let iface_tree_holder = inspect_tree.create_iface_child(id);

    let device_info = match mlme_proxy.query_device_info().await {
        Ok(device_info) => device_info,
        Err(e) => return AddIfaceResult::from_error(e.into(), zx::sys::ZX_ERR_PEER_CLOSED),
    };

    let serve_sme_fut = match device::create_and_serve_sme(
        cfg.clone(),
        id,
        phy_ownership,
        mlme_proxy,
        ifaces.clone(),
        inspect_tree.clone(),
        iface_tree_holder,
        device_info,
        dev_monitor_proxy,
        persistence_req_sender,
    ) {
        Ok(fut) => fut,
        Err(e) => return AddIfaceResult::from_error(e, zx::sys::ZX_ERR_INTERNAL),
    };

    // Handle the Result returned by the SME future.  This enables some final cleanup and metrics
    // logging and also makes the spawned task detachable.
    let serve_sme_fut = serve_sme_fut.map(move |result| {
        let msg = match result {
            Ok(()) => {
                let msg = format!("iface {} shutdown gracefully", id);
                info!("{}", msg);
                msg
            }
            Err(e) => {
                let msg = format!("error serving iface {}: {}", id, e);
                error!("{}", msg);
                msg
            }
        };
        inspect_log!(inspect_tree.device_events.lock().get_mut(), msg: msg);
        inspect_tree.notify_iface_removed(id);
    });

    AddIfaceResult::ok(Box::pin(serve_sme_fut), id)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_helper;
    use fidl::endpoints::{create_endpoints, create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_wlan_device_service::IfaceListItem;
    use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeMarker};
    use fidl_fuchsia_wlan_sme as fidl_sme;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc;
    use futures::task::Poll;
    use pin_utils::pin_mut;
    use std::pin::Pin;
    use wlan_common::{assert_variant, channel::Cbw, RadioConfig};

    use crate::device::IfaceDevice;

    const IFACE_ID: u16 = 10;

    #[test]
    fn iface_counter() {
        let iface_counter = IfaceCounter::new();
        assert_eq!(0, iface_counter.next_iface_id());
        assert_eq!(1, iface_counter.next_iface_id());
        assert_eq!(2, iface_counter.next_iface_id());
        assert_eq!(3, iface_counter.next_iface_id());
    }

    #[test]
    fn list_two_ifaces() {
        let _exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let iface_null = fake_client_iface();
        let iface_zero = fake_client_iface();
        iface_map.insert(10u16, iface_null.iface);
        iface_map.insert(20u16, iface_zero.iface);
        let mut list = super::list_ifaces(&iface_map).ifaces;
        list.sort_by_key(|p| p.iface_id);
        assert_eq!(
            vec![IfaceListItem { iface_id: 10u16 }, IfaceListItem { iface_id: 20u16 },],
            list
        )
    }

    #[test]
    fn query_iface_success() {
        let _exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let iface = fake_client_iface();
        iface_map.insert(10, iface.iface);

        let response = super::query_iface(&iface_map, 10).expect("querying iface failed");
        let expected = fake_device_info();
        assert_eq!(response.role, fidl_common::WlanMacRole::Client);
        assert_eq!(response.sta_addr, expected.sta_addr);
        assert_eq!(response.id, 10);
        assert_eq!(response.driver_features, expected.driver_features);
    }

    #[test]
    fn query_iface_not_found() {
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let status = super::query_iface(&iface_map, 10u16).expect_err("querying iface succeeded");
        assert_eq!(zx::Status::NOT_FOUND, status);
    }

    #[test]
    fn get_client_sme_success() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let mut iface = fake_client_iface();
        iface_map.insert(10, iface.iface);

        let (proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::OK, super::get_client_sme(&iface_map, 10, server));

        // Expect to get a new FIDL client in the stream
        let endpoint = iface
            .new_sme_clients
            .try_next()
            .expect("expected a message in new_sme_clients")
            .expect("didn't expect new_sme_clients stream to end");
        let mut sme_stream = endpoint.into_stream().expect("failed to create stream for endpoint");

        // Verify that `proxy` is indeed connected to `sme_stream`
        let (_scan_proxy, scan_txn) =
            create_proxy().expect("failed to create a pair of scan txn endpoints");
        proxy.scan(&mut fake_scan_request(), scan_txn).expect("failed to send a scan request");

        assert_variant!(exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan { req, .. }))) => {
                assert_eq!(fake_scan_request(), req)
            }
        );
    }

    #[test]
    fn get_client_sme_not_found() {
        let mut _exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_FOUND, super::get_client_sme(&iface_map, 10, server));
    }

    #[test]
    fn get_client_sme_wrong_role() {
        let mut _exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let iface = fake_ap_iface();
        iface_map.insert(10, iface.iface);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_SUPPORTED, super::get_client_sme(&iface_map, 10, server));
    }

    #[test]
    fn get_ap_sme_success() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let mut iface = fake_ap_iface();
        iface_map.insert(10, iface.iface);

        let (proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::OK, super::get_ap_sme(&iface_map, 10, server));

        // Expect to get a new FIDL client in the stream
        let endpoint = iface
            .new_sme_clients
            .try_next()
            .expect("expected a message in new_sme_clients")
            .expect("didn't expect new_sme_clients stream to end");
        let mut sme_stream = endpoint.into_stream().expect("failed to create stream for endpoint");

        // Verify that `proxy` is indeed connected to `sme_stream`
        let mut fut = fidl_sme::ApSmeProxyInterface::start(&proxy, &mut fake_ap_config());

        assert_variant!(exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Start { config, responder }))) => {
                assert_eq!(fake_ap_config(), config);
                responder
                    .send(fidl_sme::StartApResultCode::Success)
                    .expect("failed to send response");
            }
        );

        let fut_result = exec.run_until_stalled(&mut fut);
        assert_variant!(fut_result, Poll::Ready(Ok(fidl_sme::StartApResultCode::Success)));
    }

    #[test]
    fn get_ap_sme_not_found() {
        let mut _exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_FOUND, super::get_ap_sme(&iface_map, 10, server));
    }

    #[test]
    fn get_ap_sme_wrong_role() {
        let mut _exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let iface = fake_client_iface();
        iface_map.insert(10, iface.iface);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_SUPPORTED, super::get_ap_sme(&iface_map, 10, server));
    }

    // Debug is required for assert_variant.
    impl std::fmt::Debug for AddIfaceResult {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.debug_tuple("")
                .field(&self.result.is_ok())
                .field(&self.status)
                .field(&self.iface_id)
                .finish()
        }
    }

    #[test]
    fn test_add_iface() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        // Boilerplate for adding a new interface.
        let iface_map = Arc::new(IfaceMap::new());
        let iface_counter = Arc::new(IfaceCounter::new());
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();

        // Construct the request.
        let (mlme_channel, mlme_receiver) =
            create_endpoints().expect("failed to create fake MLME proxy");
        let mut mlme_stream = mlme_receiver.into_stream().expect("failed to create MLME stream");
        let req =
            fidl_svc::AddIfaceRequest { phy_id: 123, assigned_iface_id: 456, iface: mlme_channel };
        let fut = add_iface(
            req,
            &cfg,
            &iface_map,
            &iface_counter,
            &inspect_tree,
            dev_monitor_proxy,
            persistence_req_sender,
        );
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the PHY's information.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QueryDeviceInfo { responder }))) => {
            let mut device_info = fake_device_info();
            responder.send(&mut device_info).expect("failed to send MLME response");
        });

        // The future should complete successfully.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(result) => {
            assert!(result.result.is_ok());
            assert_eq!(result.status, zx::sys::ZX_OK);
            assert_eq!(result.iface_id, Some(fidl_svc::AddIfaceResponse { iface_id: 0 }));
        });
    }

    #[test]
    fn test_add_iface_query_fails() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        // Boilerplate for adding a new interface.
        let iface_map = Arc::new(IfaceMap::new());
        let iface_counter = Arc::new(IfaceCounter::new());
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();

        // Construct the request.
        let (mlme_channel, mlme_receiver) =
            create_endpoints().expect("failed to create fake MLME proxy");

        // Drop the receiver so that the initial device info query fails.
        drop(mlme_receiver);

        let req =
            fidl_svc::AddIfaceRequest { phy_id: 123, assigned_iface_id: 456, iface: mlme_channel };
        let fut = add_iface(
            req,
            &cfg,
            &iface_map,
            &iface_counter,
            &inspect_tree,
            dev_monitor_proxy,
            persistence_req_sender,
        );
        pin_mut!(fut);

        // The future should have returned bad status here.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(result) => {
            assert!(result.result.is_err());
            assert_eq!(result.status, zx::sys::ZX_ERR_PEER_CLOSED);
            assert!(result.iface_id.is_none());
        });
    }

    #[test]
    fn test_add_iface_create_sme_fails() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        // Boilerplate for adding a new interface.
        let iface_map = Arc::new(IfaceMap::new());
        let iface_counter = Arc::new(IfaceCounter::new());
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();

        // Construct the request.
        let (mlme_channel, mlme_receiver) =
            create_endpoints().expect("failed to create fake MLME proxy");
        let mut mlme_stream = mlme_receiver.into_stream().expect("failed to create MLME stream");

        let req =
            fidl_svc::AddIfaceRequest { phy_id: 123, assigned_iface_id: 456, iface: mlme_channel };
        let fut = add_iface(
            req,
            &cfg,
            &iface_map,
            &iface_counter,
            &inspect_tree,
            dev_monitor_proxy,
            persistence_req_sender,
        );
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the PHY's information.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QueryDeviceInfo { responder }))) => {
            // Add the Synth feature without the TempSoftmac feature since this represents an
            // invalid configuration.
            let mut device_info = fake_device_info();
            device_info.driver_features.push(fidl_common::DriverFeature::Synth);
            responder.send(&mut device_info).expect("failed to send MLME response");
        });

        // The device information should be invalid and the future should report the failure here.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(result) => {
            assert!(result.result.is_err());
            assert_eq!(result.status, zx::sys::ZX_ERR_INTERNAL);
            assert!(result.iface_id.is_none());
        });
    }

    #[test]
    fn test_get_iface_counter_stats_success() {
        let (mut test_helper, mut test_fut) = setup_test();

        let get_stats_fut = test_helper.dev_svc_proxy.get_iface_counter_stats(IFACE_ID);
        pin_mut!(get_stats_fut);
        assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Pending);
        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // Verify the GetIfaceCounterStats request is forwarded to MLME
        let responder = assert_variant!(
            test_helper.exec.run_until_stalled(&mut test_helper.mlme_req_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::GetIfaceCounterStats { responder }))) => responder
        );

        // (mlme -> wlanstack) Respond with mock counter stats
        let stats = fidl_fuchsia_wlan_stats::IfaceCounterStats {
            rx_unicast_total: 20,
            rx_unicast_drop: 3,
            rx_multicast: 5,
            tx_total: 10,
            tx_drop: 1,
        };
        let mut mlme_resp = fidl_mlme::GetIfaceCounterStatsResponse::Stats(stats.clone());
        responder.send(&mut mlme_resp).expect("failed to send stats");

        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let resp = assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Ready(resp) => resp);
        assert_variant!(resp, Ok(fidl_svc::GetIfaceCounterStatsResponse::Stats(actual_stats)) => {
            assert_eq!(actual_stats, stats);
        });
    }

    #[test]
    fn test_get_iface_counter_stats_failure() {
        let (mut test_helper, mut test_fut) = setup_test();

        let get_stats_fut = test_helper.dev_svc_proxy.get_iface_counter_stats(IFACE_ID);
        pin_mut!(get_stats_fut);
        assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Pending);
        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // Verify the GetIfaceCounterStats request is forwarded to MLME
        let responder = assert_variant!(
            test_helper.exec.run_until_stalled(&mut test_helper.mlme_req_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::GetIfaceCounterStats { responder }))) => responder
        );

        // (mlme -> wlanstack) Respond with error status
        let mut mlme_resp = fidl_mlme::GetIfaceCounterStatsResponse::ErrorStatus(1);
        responder.send(&mut mlme_resp).expect("failed to send stats");

        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let resp = assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Ready(resp) => resp);
        assert_variant!(resp, Ok(fidl_svc::GetIfaceCounterStatsResponse::ErrorStatus(1)));
    }

    #[test]
    fn test_get_iface_histogram_stats_success() {
        let (mut test_helper, mut test_fut) = setup_test();

        let get_stats_fut = test_helper.dev_svc_proxy.get_iface_histogram_stats(IFACE_ID);
        pin_mut!(get_stats_fut);
        assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Pending);
        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // Verify the GetIfaceHistogramStats request is forwarded to MLME
        let responder = assert_variant!(
            test_helper.exec.run_until_stalled(&mut test_helper.mlme_req_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::GetIfaceHistogramStats { responder }))) => responder
        );

        // (mlme -> wlanstack) Respond with mock histogram stats
        let stats = fake_histogram_stats();
        let mut mlme_resp = fidl_mlme::GetIfaceHistogramStatsResponse::Stats(stats.clone());
        responder.send(&mut mlme_resp).expect("failed to send stats");

        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let resp = assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Ready(resp) => resp);
        assert_variant!(resp, Ok(fidl_svc::GetIfaceHistogramStatsResponse::Stats(actual_stats)) => {
            assert_eq!(actual_stats, stats);
        });
    }

    #[test]
    fn test_get_iface_histogram_stats_failure() {
        let (mut test_helper, mut test_fut) = setup_test();

        let get_stats_fut = test_helper.dev_svc_proxy.get_iface_histogram_stats(IFACE_ID);
        pin_mut!(get_stats_fut);
        assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Pending);
        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);

        // Verify the GetIfaceHistogramStats request is forwarded to MLME
        let responder = assert_variant!(
            test_helper.exec.run_until_stalled(&mut test_helper.mlme_req_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::GetIfaceHistogramStats { responder }))) => responder
        );

        // (mlme -> wlanstack) Respond with error status
        let mut mlme_resp = fidl_mlme::GetIfaceHistogramStatsResponse::ErrorStatus(1);
        responder.send(&mut mlme_resp).expect("failed to send stats");

        assert_variant!(test_helper.exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let resp = assert_variant!(test_helper.exec.run_until_stalled(&mut get_stats_fut), Poll::Ready(resp) => resp);
        assert_variant!(resp, Ok(fidl_svc::GetIfaceHistogramStatsResponse::ErrorStatus(1)));
    }

    struct TestHelper {
        dev_svc_proxy: fidl_svc::DeviceServiceProxy,
        mlme_req_stream: fidl_mlme::MlmeRequestStream,
        exec: fasync::TestExecutor,
    }

    fn setup_test() -> (TestHelper, Pin<Box<impl Future<Output = Result<(), anyhow::Error>>>>) {
        let mut exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(0));

        let iface_map = Arc::new(IfaceMap::new());
        let iface = fake_client_iface();
        iface_map.insert(IFACE_ID, iface.iface);
        let iface_counter = Arc::new(IfaceCounter::new());
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };

        let (dev_svc_proxy, dev_svc_req_stream) =
            create_proxy_and_stream::<fidl_svc::DeviceServiceMarker>()
                .expect("failed to create DeviceService proxy");
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();
        let mut test_fut = Box::pin(serve_device_requests(
            iface_counter,
            cfg,
            iface_map,
            dev_svc_req_stream,
            inspect_tree,
            dev_monitor_proxy,
            persistence_req_sender,
        ));
        assert_variant!(exec.run_until_stalled(&mut test_fut), Poll::Pending);

        let test_helper =
            TestHelper { dev_svc_proxy, mlme_req_stream: iface.mlme_req_stream, exec };
        (test_helper, test_fut)
    }

    fn fake_histogram_stats() -> fidl_fuchsia_wlan_stats::IfaceHistogramStats {
        fidl_fuchsia_wlan_stats::IfaceHistogramStats {
            noise_floor_histograms: vec![fidl_fuchsia_wlan_stats::NoiseFloorHistogram {
                hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
                antenna_id: Some(Box::new(fidl_fuchsia_wlan_stats::AntennaId {
                    freq: fidl_fuchsia_wlan_stats::AntennaFreq::Antenna2G,
                    index: 0,
                })),
                noise_floor_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                    bucket_index: 200,
                    num_samples: 999,
                }],
                invalid_samples: 44,
            }],
            rssi_histograms: vec![fidl_fuchsia_wlan_stats::RssiHistogram {
                hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
                antenna_id: Some(Box::new(fidl_fuchsia_wlan_stats::AntennaId {
                    freq: fidl_fuchsia_wlan_stats::AntennaFreq::Antenna2G,
                    index: 0,
                })),
                rssi_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                    bucket_index: 230,
                    num_samples: 999,
                }],
                invalid_samples: 55,
            }],
            rx_rate_index_histograms: vec![
                fidl_fuchsia_wlan_stats::RxRateIndexHistogram {
                    hist_scope: fidl_fuchsia_wlan_stats::HistScope::Station,
                    antenna_id: None,
                    rx_rate_index_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                        bucket_index: 99,
                        num_samples: 1400,
                    }],
                    invalid_samples: 22,
                },
                fidl_fuchsia_wlan_stats::RxRateIndexHistogram {
                    hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
                    antenna_id: Some(Box::new(fidl_fuchsia_wlan_stats::AntennaId {
                        freq: fidl_fuchsia_wlan_stats::AntennaFreq::Antenna5G,
                        index: 1,
                    })),
                    rx_rate_index_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                        bucket_index: 100,
                        num_samples: 1500,
                    }],
                    invalid_samples: 33,
                },
            ],
            snr_histograms: vec![fidl_fuchsia_wlan_stats::SnrHistogram {
                hist_scope: fidl_fuchsia_wlan_stats::HistScope::PerAntenna,
                antenna_id: Some(Box::new(fidl_fuchsia_wlan_stats::AntennaId {
                    freq: fidl_fuchsia_wlan_stats::AntennaFreq::Antenna2G,
                    index: 0,
                })),
                snr_samples: vec![fidl_fuchsia_wlan_stats::HistBucket {
                    bucket_index: 30,
                    num_samples: 999,
                }],
                invalid_samples: 11,
            }],
        }
    }

    struct FakeClientIface {
        iface: IfaceDevice,
        new_sme_clients: mpsc::UnboundedReceiver<wlan_sme::serve::client::Endpoint>,
        mlme_req_stream: fidl_mlme::MlmeRequestStream,
    }

    fn fake_client_iface() -> FakeClientIface {
        let (sme_sender, sme_receiver) = mpsc::unbounded();
        let (mlme_proxy, mlme_req_stream) =
            create_proxy_and_stream::<MlmeMarker>().expect("Error creating proxy");
        let (shutdown_sender, _) = mpsc::channel(1);
        let device_info = fake_device_info();
        let iface = IfaceDevice {
            phy_ownership: device::PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
            sme_server: SmeServer::Client(sme_sender),
            mlme_proxy,
            device_info,
            shutdown_sender,
        };
        FakeClientIface { iface, new_sme_clients: sme_receiver, mlme_req_stream }
    }

    struct FakeApIface {
        iface: IfaceDevice,
        new_sme_clients: mpsc::UnboundedReceiver<wlan_sme::serve::ap::Endpoint>,
    }

    fn fake_ap_iface() -> FakeApIface {
        let (sme_sender, sme_receiver) = mpsc::unbounded();
        let (mlme_proxy, _server) = create_proxy::<MlmeMarker>().expect("Error creating proxy");
        let (shutdown_sender, _) = mpsc::channel(1);
        let device_info = fake_device_info();
        let iface = IfaceDevice {
            phy_ownership: device::PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
            sme_server: SmeServer::Ap(sme_sender),
            mlme_proxy,
            device_info,
            shutdown_sender,
        };
        FakeApIface { iface, new_sme_clients: sme_receiver }
    }

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_common::WlanMacRole::Client,
            bands: vec![],
            sta_addr: [0xAC; 6],
            driver_features: vec![
                fidl_common::DriverFeature::ScanOffload,
                fidl_common::DriverFeature::SaeSmeAuth,
            ],
            softmac_hardware_capability: 0,
            qos_capable: false,
        }
    }

    fn fake_scan_request() -> fidl_sme::ScanRequest {
        fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {})
    }

    fn fake_ap_config() -> fidl_sme::ApConfig {
        fidl_sme::ApConfig {
            ssid: b"qwerty".to_vec(),
            password: vec![],
            radio_cfg: RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6).into(),
        }
    }
}

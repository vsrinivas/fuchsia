// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use core::sync::atomic::AtomicUsize;
use fidl_fuchsia_wlan_device as fidl_wlan_dev;
use fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceServiceRequest};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MinstrelStatsResponse};
use fuchsia_async as fasync;
use fuchsia_cobalt::{self, CobaltSender};
use fuchsia_inspect_contrib::inspect_log;
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, prelude::*};
use log::{error, info};
use std::sync::{atomic::Ordering, Arc};

use crate::device::{self, IfaceMap};
use crate::inspect;
use crate::station;
use crate::stats_scheduler::StatsRef;
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
    cobalt_sender: CobaltSender,
    cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
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
                    &cobalt_sender,
                    cobalt_1dot1_proxy.clone(),
                    dev_monitor_proxy.clone(),
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
            DeviceServiceRequest::GetIfaceStats { iface_id, responder } => {
                match get_iface_stats(&ifaces, iface_id).await {
                    Ok(stats_ref) => {
                        let mut stats = stats_ref.lock();
                        responder.send(zx::sys::ZX_OK, Some(&mut stats))
                    }
                    Err(status) => responder.send(status.into_raw(), None),
                }
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
        fidl_mlme::MacRole::Client => fidl_wlan_dev::MacRole::Client,
        fidl_mlme::MacRole::Ap => fidl_wlan_dev::MacRole::Ap,
        fidl_mlme::MacRole::Mesh => fidl_wlan_dev::MacRole::Mesh,
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
    endpoint: station::client::Endpoint,
) -> zx::Status {
    let iface = ifaces.get(&iface_id);
    let server = match iface {
        None => return zx::Status::NOT_FOUND,
        Some(ref iface) => match iface.sme_server {
            device::SmeServer::Client(ref server) => server,
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

fn get_ap_sme(ifaces: &IfaceMap, iface_id: u16, endpoint: station::ap::Endpoint) -> zx::Status {
    let iface = ifaces.get(&iface_id);
    let server = match iface {
        None => return zx::Status::NOT_FOUND,
        Some(ref iface) => match iface.sme_server {
            device::SmeServer::Ap(ref server) => server,
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

fn get_mesh_sme(ifaces: &IfaceMap, iface_id: u16, endpoint: station::mesh::Endpoint) -> zx::Status {
    let iface = ifaces.get(&iface_id);
    let server = match iface {
        None => return zx::Status::NOT_FOUND,
        Some(ref iface) => match iface.sme_server {
            device::SmeServer::Mesh(ref server) => server,
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

pub async fn get_iface_stats(ifaces: &IfaceMap, iface_id: u16) -> Result<StatsRef, zx::Status> {
    let iface = ifaces.get(&iface_id).ok_or(zx::Status::NOT_FOUND)?;
    iface.stats_sched.get_stats().await
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
    match iface.mlme_query.get_minstrel_list().await {
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
    match iface.mlme_query.get_minstrel_peer(peer_addr).await {
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
    cobalt_sender: &CobaltSender,
    cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
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
        cobalt_sender.clone(),
        cobalt_1dot1_proxy,
        device_info,
        dev_monitor_proxy,
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
    use fidl::endpoints::{create_endpoints, create_proxy};
    use fidl_fuchsia_wlan_device as fidl_dev;
    use fidl_fuchsia_wlan_device_service::IfaceListItem;
    use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeMarker};
    use fidl_fuchsia_wlan_sme as fidl_sme;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc;
    use futures::task::Poll;
    use pin_utils::pin_mut;
    use wlan_common::{
        assert_variant,
        channel::{Cbw, Phy},
        RadioConfig,
    };

    use crate::{
        device::IfaceDevice,
        mlme_query_proxy::MlmeQueryProxy,
        stats_scheduler::{self, StatsRequest},
    };

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
        assert_eq!(response.role, fidl_dev::MacRole::Client);
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
        let (sender, _receiver) = mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        let (cobalt_1dot1_proxy, _) =
            create_proxy::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy");
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };

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
            &cobalt_sender,
            cobalt_1dot1_proxy,
            dev_monitor_proxy,
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
        let (sender, _receiver) = mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        let (cobalt_1dot1_proxy, _) =
            create_proxy::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy");
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };

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
            &cobalt_sender,
            cobalt_1dot1_proxy,
            dev_monitor_proxy,
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
        let (sender, _receiver) = mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        let (cobalt_1dot1_proxy, _) =
            create_proxy::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy");
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };

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
            &cobalt_sender,
            cobalt_1dot1_proxy,
            dev_monitor_proxy,
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
            device_info.driver_features.push(fidl_fuchsia_wlan_common::DriverFeature::Synth);
            responder.send(&mut device_info).expect("failed to send MLME response");
        });

        // The device information should be invalid and the future should report the failure here.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(result) => {
            assert!(result.result.is_err());
            assert_eq!(result.status, zx::sys::ZX_ERR_INTERNAL);
            assert!(result.iface_id.is_none());
        });
    }
    struct FakeClientIface<St: Stream<Item = StatsRequest>> {
        iface: IfaceDevice,
        _stats_requests: St,
        new_sme_clients: mpsc::UnboundedReceiver<station::client::Endpoint>,
    }

    fn fake_client_iface() -> FakeClientIface<impl Stream<Item = StatsRequest>> {
        let (sme_sender, sme_receiver) = mpsc::unbounded();
        let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
        let (proxy, _server) = create_proxy::<MlmeMarker>().expect("Error creating proxy");
        let (shutdown_sender, _) = mpsc::channel(1);
        let mlme_query = MlmeQueryProxy::new(proxy);
        let device_info = fake_device_info();
        let iface = IfaceDevice {
            phy_ownership: device::PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
            sme_server: device::SmeServer::Client(sme_sender),
            stats_sched,
            mlme_query,
            device_info,
            shutdown_sender,
        };
        FakeClientIface { iface, _stats_requests: stats_requests, new_sme_clients: sme_receiver }
    }

    struct FakeApIface<St: Stream<Item = StatsRequest>> {
        iface: IfaceDevice,
        _stats_requests: St,
        new_sme_clients: mpsc::UnboundedReceiver<station::ap::Endpoint>,
    }

    fn fake_ap_iface() -> FakeApIface<impl Stream<Item = StatsRequest>> {
        let (sme_sender, sme_receiver) = mpsc::unbounded();
        let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
        let (proxy, _server) = create_proxy::<MlmeMarker>().expect("Error creating proxy");
        let mlme_query = MlmeQueryProxy::new(proxy);
        let (shutdown_sender, _) = mpsc::channel(1);
        let device_info = fake_device_info();
        let iface = IfaceDevice {
            phy_ownership: device::PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
            sme_server: device::SmeServer::Ap(sme_sender),
            stats_sched,
            mlme_query,
            device_info,
            shutdown_sender,
        };
        FakeApIface { iface, _stats_requests: stats_requests, new_sme_clients: sme_receiver }
    }

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_mlme::MacRole::Client,
            bands: vec![],
            sta_addr: [0xAC; 6],
            driver_features: vec![
                fidl_fuchsia_wlan_common::DriverFeature::ScanOffload,
                fidl_fuchsia_wlan_common::DriverFeature::SaeSmeAuth,
            ],
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
            radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6).to_fidl(),
        }
    }
}

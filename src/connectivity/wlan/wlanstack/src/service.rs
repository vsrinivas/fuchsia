// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::ResultExt;
use fidl::encoding::OutOfLine;
use fidl_fuchsia_wlan_device as fidl_wlan_dev;
use fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceServiceRequest};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MinstrelStatsResponse};
use fuchsia_zircon as zx;
use futures::prelude::*;
use log::{error, info};
use std::sync::Arc;

use crate::device::{self, IfaceDevice, IfaceMap, PhyDevice, PhyMap};
use crate::station;
use crate::stats_scheduler::StatsRef;
use crate::watcher_service::WatcherService;

pub async fn serve_device_requests(
    phys: Arc<PhyMap>,
    ifaces: Arc<IfaceMap>,
    watcher_service: WatcherService<PhyDevice, IfaceDevice>,
    mut req_stream: fidl_svc::DeviceServiceRequestStream,
) -> Result<(), failure::Error> {
    while let Some(req) = await!(req_stream.try_next()).context("error running DeviceService")? {
        await!(handle_fidl_request(req, phys.clone(), ifaces.clone(), watcher_service.clone()))?;
    }
    Ok(())
}

async fn handle_fidl_request(
    request: fidl_svc::DeviceServiceRequest,
    phys: Arc<PhyMap>,
    ifaces: Arc<IfaceMap>,
    watcher_service: WatcherService<PhyDevice, IfaceDevice>,
) -> Result<(), fidl::Error> {
    // Note that errors from responder.send() are propagated intentionally.
    // If we fail to send a response, the only way to recover is to stop serving the
    // client and close the channel. Otherwise, the client would be left hanging
    // forever.
    match request {
        DeviceServiceRequest::ListPhys { responder } => responder.send(&mut list_phys(&phys)),
        DeviceServiceRequest::QueryPhy { req, responder } => {
            let result = await!(query_phy(&phys, req.phy_id));
            let (status, mut response) = into_status_and_opt(result);
            responder.send(status.into_raw(), response.as_mut().map(OutOfLine))
        }
        DeviceServiceRequest::ListIfaces { responder } => responder.send(&mut list_ifaces(&ifaces)),
        DeviceServiceRequest::QueryIface { iface_id, responder } => {
            let result = query_iface(&ifaces, iface_id);
            let (status, mut response) = into_status_and_opt(result);
            responder.send(status.into_raw(), response.as_mut().map(OutOfLine))
        }
        DeviceServiceRequest::CreateIface { req, responder } => {
            let result = await!(create_iface(&phys, req));
            let (status, mut response) = into_status_and_opt(result);
            responder.send(status.into_raw(), response.as_mut().map(OutOfLine))
        }
        DeviceServiceRequest::DestroyIface { req: _, responder: _ } => unimplemented!(),
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
            match await!(get_iface_stats(&ifaces, iface_id)) {
                Ok(stats_ref) => {
                    let mut stats = stats_ref.lock();
                    responder.send(zx::sys::ZX_OK, Some(OutOfLine(&mut stats)))
                }
                Err(status) => responder.send(status.into_raw(), None),
            }
        }
        DeviceServiceRequest::GetMinstrelList { iface_id, responder } => {
            let (status, mut peers) = await!(list_minstrel_peers(&ifaces, iface_id));
            responder.send(status.into_raw(), &mut peers)
        }
        DeviceServiceRequest::GetMinstrelStats { iface_id, peer_addr, responder } => {
            let (status, mut peer) = await!(get_minstrel_stats(&ifaces, iface_id, peer_addr));
            responder.send(status.into_raw(), peer.as_mut().map(|x| OutOfLine(x.as_mut())))
        }
        DeviceServiceRequest::WatchDevices { watcher, control_handle: _ } => {
            watcher_service
                .add_watcher(watcher)
                .unwrap_or_else(|e| error!("error registering a device watcher: {}", e));
            Ok(())
        }
    }
}

fn into_status_and_opt<T>(r: Result<T, zx::Status>) -> (zx::Status, Option<T>) {
    match r {
        Ok(x) => (zx::Status::OK, Some(x)),
        Err(status) => (status, None),
    }
}

fn list_phys(phys: &PhyMap) -> fidl_svc::ListPhysResponse {
    let list = phys
        .get_snapshot()
        .iter()
        .map(|(phy_id, phy)| fidl_svc::PhyListItem {
            phy_id: *phy_id,
            path: phy.device.path().to_string_lossy().into_owned(),
        })
        .collect();
    fidl_svc::ListPhysResponse { phys: list }
}

async fn query_phy(phys: &PhyMap, id: u16) -> Result<fidl_svc::QueryPhyResponse, zx::Status> {
    info!("query_phy(id = {})", id);
    let phy = phys.get(&id).ok_or(zx::Status::NOT_FOUND)?;
    let query_result = await!(phy.proxy.query()).map_err(move |e| {
        error!("query_phy(id = {}): error sending 'Query' request to phy: {}", id, e);
        zx::Status::INTERNAL
    })?;
    info!("query_phy(id = {}): received a 'QueryResult' from device", id);
    zx::Status::ok(query_result.status)?;
    let mut info = query_result.info;
    info.id = id;
    info.dev_path = Some(phy.device.path().to_string_lossy().into_owned());
    Ok(fidl_svc::QueryPhyResponse { info })
}

fn list_ifaces(ifaces: &IfaceMap) -> fidl_svc::ListIfacesResponse {
    let list = ifaces
        .get_snapshot()
        .iter()
        .map(|(iface_id, iface)| fidl_svc::IfaceListItem {
            iface_id: *iface_id,
            path: iface.device.path().to_string_lossy().into_owned(),
        })
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
    let dev_path = iface.device.path().to_string_lossy().into_owned();
    let mac_addr = iface.device_info.mac_addr;
    Ok(fidl_svc::QueryIfaceResponse { role, id, dev_path, mac_addr })
}

async fn create_iface(
    phys: &PhyMap,
    req: fidl_svc::CreateIfaceRequest,
) -> Result<fidl_svc::CreateIfaceResponse, zx::Status> {
    let phy = phys.get(&req.phy_id).ok_or(zx::Status::NOT_FOUND)?;
    let mut phy_req = fidl_wlan_dev::CreateIfaceRequest { role: req.role, sme_channel: None };
    let r = await!(phy.proxy.create_iface(&mut phy_req)).map_err(move |e| {
        error!("Error sending 'CreateIface' request to phy #{}: {}", req.phy_id, e);
        zx::Status::INTERNAL
    })?;
    zx::Status::ok(r.status)?;
    // TODO(gbonik): this is not the ID that we want to return
    Ok(fidl_svc::CreateIfaceResponse { iface_id: r.iface_id })
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

async fn get_iface_stats(ifaces: &IfaceMap, iface_id: u16) -> Result<StatsRef, zx::Status> {
    let iface = ifaces.get(&iface_id).ok_or(zx::Status::NOT_FOUND)?;
    await!(iface.stats_sched.get_stats())
}

async fn list_minstrel_peers(
    ifaces: &IfaceMap,
    iface_id: u16,
) -> (zx::Status, fidl_fuchsia_wlan_minstrel::Peers) {
    let empty_peer_list = fidl_fuchsia_wlan_minstrel::Peers { peers: vec![] };
    let iface = match ifaces.get(&iface_id) {
        Some(iface) => iface,
        None => return (zx::Status::NOT_FOUND, empty_peer_list),
    };
    match await!(iface.mlme_query.get_minstrel_list()) {
        Ok(resp) => (zx::Status::OK, resp.peers),
        Err(_) => (zx::Status::INTERNAL, empty_peer_list),
    }
}

async fn get_minstrel_stats(
    ifaces: &IfaceMap,
    iface_id: u16,
    mac_addr: [u8; 6],
) -> (zx::Status, Option<Box<fidl_fuchsia_wlan_minstrel::Peer>>) {
    let iface = match ifaces.get(&iface_id) {
        Some(iface) => iface,
        None => return (zx::Status::NOT_FOUND, None),
    };
    match await!(iface.mlme_query.get_minstrel_peer(mac_addr)) {
        Ok(MinstrelStatsResponse { peer }) => (zx::Status::OK, peer),
        Err(_) => (zx::Status::INTERNAL, None),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_wlan_common as fidl_common;
    use fidl_fuchsia_wlan_device::{self as fidl_dev, PhyRequest, PhyRequestStream};
    use fidl_fuchsia_wlan_device_service::{IfaceListItem, PhyListItem};
    use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeMarker};
    use fidl_fuchsia_wlan_sme as fidl_sme;
    use fuchsia_async as fasync;
    use fuchsia_wlan_dev as wlan_dev;
    use futures::channel::mpsc;
    use futures::task::Poll;
    use pin_utils::pin_mut;
    use wlan_common::{
        channel::{Cbw, Phy},
        RadioConfig,
    };

    use crate::{
        mlme_query_proxy::MlmeQueryProxy,
        stats_scheduler::{self, StatsRequest},
    };

    #[test]
    fn list_two_phys() {
        let _exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy_null, _phy_null_stream) = fake_phy("/dev/null");
        let (phy_zero, _phy_zero_stream) = fake_phy("/dev/zero");
        phy_map.insert(10u16, phy_null);
        phy_map.insert(20u16, phy_zero);
        let mut list = super::list_phys(&phy_map).phys;
        list.sort_by_key(|p| p.phy_id);
        assert_eq!(
            vec![
                PhyListItem { phy_id: 10u16, path: "/dev/null".to_string() },
                PhyListItem { phy_id: 20u16, path: "/dev/zero".to_string() },
            ],
            list
        )
    }

    #[test]
    fn query_phy_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        phy_map.insert(10u16, phy);

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        let query_fut = super::query_phy(&phy_map, 10u16);
        pin_mut!(query_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut query_fut));

        // The call above should trigger a Query message to the phy.
        // Pretend that we are the phy and read the message from the other side.
        let responder = match exec.run_until_stalled(&mut phy_stream.next()) {
            Poll::Ready(Some(Ok(PhyRequest::Query { responder }))) => responder,
            _ => panic!("phy_stream returned unexpected result"),
        };

        // Reply with a fake phy info
        responder
            .send(&mut fidl_wlan_dev::QueryResponse {
                status: zx::sys::ZX_OK,
                info: fake_phy_info(),
            })
            .expect("failed to send QueryResponse");

        // Our original future should complete now, and return the same phy info
        let response = match exec.run_until_stalled(&mut query_fut) {
            Poll::Ready(Ok(response)) => response,
            other => panic!("query_fut returned unexpected result: {:?}", other),
        };
        assert_eq!(fake_phy_info(), response.info);
    }

    #[test]
    fn query_phy_not_found() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);

        let query_fut = super::query_phy(&phy_map, 10u16);
        pin_mut!(query_fut);
        assert_eq!(Poll::Ready(Err(zx::Status::NOT_FOUND)), exec.run_until_stalled(&mut query_fut));
    }

    #[test]
    fn list_two_ifaces() {
        let _exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let iface_null = fake_client_iface("/dev/null");
        let iface_zero = fake_client_iface("/dev/zero");
        iface_map.insert(10u16, iface_null.iface);
        iface_map.insert(20u16, iface_zero.iface);
        let mut list = super::list_ifaces(&iface_map).ifaces;
        list.sort_by_key(|p| p.iface_id);
        assert_eq!(
            vec![
                IfaceListItem { iface_id: 10u16, path: "/dev/null".to_string() },
                IfaceListItem { iface_id: 20u16, path: "/dev/zero".to_string() },
            ],
            list
        )
    }

    #[test]
    fn query_iface_success() {
        let _exec = fasync::Executor::new().expect("Failed to create an executor");

        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let iface = fake_client_iface("/dev/null");
        iface_map.insert(10, iface.iface);

        let response = super::query_iface(&iface_map, 10).expect("querying iface failed");
        let expected = fake_device_info();
        assert_eq!(response.role, fidl_dev::MacRole::Client);
        assert_eq!(response.mac_addr, expected.mac_addr);
        assert_eq!(response.id, 10);
        assert_eq!(response.dev_path, "/dev/null");
    }

    #[test]
    fn query_iface_not_found() {
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let status = super::query_iface(&iface_map, 10u16).expect_err("querying iface succeeded");
        assert_eq!(zx::Status::NOT_FOUND, status);
    }

    #[test]
    fn create_iface_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);

        let (phy, mut phy_stream) = fake_phy("/dev/null");
        phy_map.insert(10u16, phy);

        // Initiate a CreateIface request. The returned future should not be able
        // to produce a result immediately
        let create_fut = super::create_iface(
            &phy_map,
            fidl_svc::CreateIfaceRequest { phy_id: 10, role: fidl_wlan_dev::MacRole::Client },
        );
        pin_mut!(create_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut create_fut));

        // The call above should trigger a CreateIface message to the phy.
        // Pretend that we are the phy and read the message from the other side.
        let (req, responder) = match exec.run_until_stalled(&mut phy_stream.next()) {
            Poll::Ready(Some(Ok(PhyRequest::CreateIface { req, responder }))) => (req, responder),
            _ => panic!("phy_stream returned unexpected result"),
        };

        // Since we requested the Client role, the request to the phy should also have
        // the Client role
        assert_eq!(fidl_wlan_dev::MacRole::Client, req.role);

        // Pretend that we created an interface device with id 123 and send a response
        responder
            .send(&mut fidl_wlan_dev::CreateIfaceResponse { status: zx::sys::ZX_OK, iface_id: 123 })
            .expect("failed to send CreateIfaceResponse");

        // Now, our original future should resolve into a response
        let response = match exec.run_until_stalled(&mut create_fut) {
            Poll::Ready(Ok(response)) => response,
            other => panic!("create_fut returned unexpected result: {:?}", other),
        };
        // This assertion likely needs to change once we figure out a solution
        // to the iface id problem.
        assert_eq!(123, response.iface_id);
    }

    #[test]
    fn create_iface_not_found() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);

        let fut = super::create_iface(
            &phy_map,
            fidl_svc::CreateIfaceRequest { phy_id: 10, role: fidl_wlan_dev::MacRole::Client },
        );
        pin_mut!(fut);
        assert_eq!(Poll::Ready(Err(zx::Status::NOT_FOUND)), exec.run_until_stalled(&mut fut));
    }

    #[test]
    fn get_client_sme_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let mut iface = fake_client_iface("/dev/null");
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

        let req = match exec.run_until_stalled(&mut sme_stream.next()) {
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan { req, .. }))) => req,
            _ => panic!("sme_stream returned unexpected result"),
        };
        assert_eq!(fake_scan_request(), req);
    }

    #[test]
    fn get_client_sme_not_found() {
        let mut _exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_FOUND, super::get_client_sme(&iface_map, 10, server));
    }

    #[test]
    fn get_client_sme_wrong_role() {
        let mut _exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let iface = fake_ap_iface("/dev/null");
        iface_map.insert(10, iface.iface);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_SUPPORTED, super::get_client_sme(&iface_map, 10, server));
    }

    #[test]
    fn get_ap_sme_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let mut iface = fake_ap_iface("/dev/null");
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

        match exec.run_until_stalled(&mut sme_stream.next()) {
            Poll::Ready(Some(Ok(fidl_sme::ApSmeRequest::Start { config, responder }))) => {
                assert_eq!(fake_ap_config(), config);
                responder
                    .send(fidl_sme::StartApResultCode::Success)
                    .expect("failed to send response");
            }
            _ => panic!("sme_stream returned unexpected result"),
        };
        match exec.run_until_stalled(&mut fut) {
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success)) => {}
            other => panic!("expected a successful response, got {:?}", other),
        }
    }

    #[test]
    fn get_ap_sme_not_found() {
        let mut _exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_FOUND, super::get_ap_sme(&iface_map, 10, server));
    }

    #[test]
    fn get_ap_sme_wrong_role() {
        let mut _exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let iface = fake_client_iface("/dev/null");
        iface_map.insert(10, iface.iface);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_SUPPORTED, super::get_ap_sme(&iface_map, 10, server));
    }

    fn fake_phy(path: &str) -> (PhyDevice, PhyRequestStream) {
        let (proxy, server) =
            create_proxy::<fidl_wlan_dev::PhyMarker>().expect("fake_phy: create_proxy() failed");
        let device =
            wlan_dev::Device::new(path).expect(&format!("fake_phy: failed to open {}", path));
        let stream = server.into_stream().expect("fake_phy: failed to create stream");
        (PhyDevice { proxy, device }, stream)
    }

    struct FakeClientIface<St: Stream<Item = StatsRequest>> {
        iface: IfaceDevice,
        _stats_requests: St,
        new_sme_clients: mpsc::UnboundedReceiver<station::client::Endpoint>,
    }

    fn fake_client_iface(path: &str) -> FakeClientIface<impl Stream<Item = StatsRequest>> {
        let device = wlan_dev::Device::new(path)
            .expect(&format!("fake_client_iface: failed to open {}", path));
        let (sme_sender, sme_receiver) = mpsc::unbounded();
        let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
        let (proxy, _server) = create_proxy::<MlmeMarker>().expect("Error creating proxy");
        let mlme_query = MlmeQueryProxy::new(proxy);
        let device_info = fake_device_info();
        let iface = IfaceDevice {
            sme_server: device::SmeServer::Client(sme_sender),
            stats_sched,
            device,
            mlme_query,
            device_info,
        };
        FakeClientIface { iface, _stats_requests: stats_requests, new_sme_clients: sme_receiver }
    }

    struct FakeApIface<St: Stream<Item = StatsRequest>> {
        iface: IfaceDevice,
        _stats_requests: St,
        new_sme_clients: mpsc::UnboundedReceiver<station::ap::Endpoint>,
    }

    fn fake_ap_iface(path: &str) -> FakeApIface<impl Stream<Item = StatsRequest>> {
        let device = wlan_dev::Device::new(path)
            .expect(&format!("fake_client_iface: failed to open {}", path));
        let (sme_sender, sme_receiver) = mpsc::unbounded();
        let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
        let (proxy, _server) = create_proxy::<MlmeMarker>().expect("Error creating proxy");
        let mlme_query = MlmeQueryProxy::new(proxy);
        let device_info = fake_device_info();
        let iface = IfaceDevice {
            sme_server: device::SmeServer::Ap(sme_sender),
            stats_sched,
            device,
            mlme_query,
            device_info,
        };
        FakeApIface { iface, _stats_requests: stats_requests, new_sme_clients: sme_receiver }
    }

    fn fake_phy_info() -> fidl_wlan_dev::PhyInfo {
        fidl_wlan_dev::PhyInfo {
            id: 10,
            dev_path: Some("/dev/null".to_string()),
            hw_mac_address: [0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6b],
            supported_phys: Vec::new(),
            driver_features: Vec::new(),
            mac_roles: Vec::new(),
            caps: Vec::new(),
            bands: Vec::new(),
        }
    }

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_mlme::MacRole::Client,
            bands: vec![],
            mac_addr: [0xAC; 6],
            driver_features: vec![],
        }
    }

    fn fake_scan_request() -> fidl_sme::ScanRequest {
        fidl_sme::ScanRequest { timeout: 41, scan_type: fidl_common::ScanType::Passive }
    }

    fn fake_ap_config() -> fidl_sme::ApConfig {
        fidl_sme::ApConfig {
            ssid: b"qwerty".to_vec(),
            password: vec![],
            radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6).to_fidl(),
        }
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use core::sync::atomic::AtomicUsize;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_wlan_device as fidl_wlan_dev;
use fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceServiceRequest};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MinstrelStatsResponse, MlmeMarker};
use fuchsia_async as fasync;
use fuchsia_cobalt::{self, CobaltSender};
use fuchsia_inspect_contrib::inspect_log;
use fuchsia_zircon as zx;
use futures::prelude::*;
use log::{error, info};
use std::sync::{atomic::Ordering, Arc};

use crate::device::{self, IfaceDevice, IfaceMap, NewIface, PhyDevice, PhyMap};
use crate::inspect;
use crate::station;
use crate::stats_scheduler::StatsRef;
use crate::watcher_service::WatcherService;
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

    #[cfg(test)]
    pub fn new_with_value(v: usize) -> Self {
        Self(AtomicUsize::new(v))
    }
}

pub async fn serve_device_requests(
    iface_counter: Arc<IfaceCounter>,
    cfg: ServiceCfg,
    phys: Arc<PhyMap>,
    ifaces: Arc<IfaceMap>,
    watcher_service: WatcherService<PhyDevice, IfaceDevice>,
    mut req_stream: fidl_svc::DeviceServiceRequestStream,
    inspect_tree: Arc<inspect::WlanstackTree>,
    cobalt_sender: CobaltSender,
) -> Result<(), anyhow::Error> {
    while let Some(req) = req_stream.try_next().await.context("error running DeviceService")? {
        // Note that errors from responder.send() are propagated intentionally.
        // If we fail to send a response, the only way to recover is to stop serving the
        // client and close the channel. Otherwise, the client would be left hanging
        // forever.
        match req {
            DeviceServiceRequest::ListPhys { responder } => responder.send(&mut list_phys(&phys)),
            DeviceServiceRequest::QueryPhy { req, responder } => {
                let result = query_phy(&phys, req.phy_id).await;
                let (status, mut response) = into_status_and_opt(result);
                responder.send(status.into_raw(), response.as_mut())
            }
            DeviceServiceRequest::ListIfaces { responder } => {
                responder.send(&mut list_ifaces(&ifaces))
            }
            DeviceServiceRequest::QueryIface { iface_id, responder } => {
                let result = query_iface(&ifaces, iface_id);
                let (status, mut response) = into_status_and_opt(result);
                responder.send(status.into_raw(), response.as_mut())
            }
            DeviceServiceRequest::CreateIface { req, responder } => {
                match create_iface(&iface_counter, &phys, req).await {
                    Ok(new_iface) => {
                        info!("iface #{} started ({:?})", new_iface.id, new_iface.phy_ownership);
                        let iface_id = new_iface.id;

                        let inspect_tree = inspect_tree.clone();
                        let iface_tree_holder = inspect_tree.create_iface_child(iface_id);

                        let device_info = match new_iface.mlme_channel.query_device_info().await {
                            Ok(device_info) => device_info,
                            Err(e) => {
                                responder.send(zx::sys::ZX_ERR_PEER_CLOSED, None.as_mut())?;
                                return Err(e.into());
                            }
                        };

                        let serve_sme_fut = device::create_and_serve_sme(
                            cfg.clone(),
                            iface_id,
                            new_iface.phy_ownership,
                            new_iface.mlme_channel,
                            ifaces.clone(),
                            inspect_tree.clone(),
                            iface_tree_holder,
                            cobalt_sender.clone(),
                            device_info,
                        )?;

                        let resp = fidl_svc::CreateIfaceResponse { iface_id };
                        responder.send(zx::sys::ZX_OK, Some(resp).as_mut())?;

                        let serve_sme_fut = serve_sme_fut.map(move |result| {
                            let msg = match result {
                                Ok(()) => {
                                    let msg = format!("iface {} shutdown gracefully", iface_id);
                                    info!("{}", msg);
                                    msg
                                }
                                Err(e) => {
                                    let msg = format!("error serving iface {}: {}", iface_id, e);
                                    error!("{}", msg);
                                    msg
                                }
                            };
                            inspect_log!(inspect_tree.device_events.lock(), msg: msg);
                            inspect_tree.notify_iface_removed(iface_id);
                        });
                        fasync::Task::spawn(serve_sme_fut).detach();
                        Ok(())
                    }
                    Err(status) => responder.send(status.into_raw(), None),
                }
            }
            DeviceServiceRequest::DestroyIface { req, responder } => {
                let result = destroy_iface(&phys, &ifaces, req.iface_id).await;
                let status = into_status_and_opt(result).0;
                responder.send(status.into_raw())
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
            DeviceServiceRequest::WatchDevices { watcher, control_handle: _ } => {
                watcher_service
                    .add_watcher(watcher)
                    .unwrap_or_else(|e| error!("error registering a device watcher: {}", e));
                Ok(())
            }
            DeviceServiceRequest::GetCountry { phy_id, responder } => responder
                .send(&mut get_country(&phys, phy_id).await.map_err(|status| status.into_raw())),
            DeviceServiceRequest::SetCountry { req, responder } => {
                let status = set_country(&phys, req).await;
                responder.send(status.into_raw())
            }
            DeviceServiceRequest::ClearCountry { req, responder } => {
                let status = clear_country(&phys, req).await;
                responder.send(status.into_raw())
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
    let query_result = phy.proxy.query().await.map_err(move |e| {
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
        .map(|(iface_id, _iface)| fidl_svc::IfaceListItem { iface_id: *iface_id })
        .collect();
    fidl_svc::ListIfacesResponse { ifaces: list }
}

async fn destroy_iface<'a>(
    phys: &'a PhyMap,
    ifaces: &'a IfaceMap,
    id: u16,
) -> Result<(), zx::Status> {
    info!("destroy_iface(id = {})", id);
    let iface = ifaces.get(&id).ok_or(zx::Status::NOT_FOUND)?;
    let phy_ownership = &iface.phy_ownership;

    // Shutdown the corresponding SME first. We don't want to send requests to MLME while we're mid-shutdown.
    if let Err(e) = iface.shutdown_sender.clone().send(()).await {
        error!("Error shutting down SME before iface removal: {:?}", e);
    }

    let phy = phys.get(&phy_ownership.phy_id).ok_or(zx::Status::NOT_FOUND)?;
    let mut phy_req = fidl_wlan_dev::DestroyIfaceRequest { id: phy_ownership.phy_assigned_id };
    let r = phy.proxy.destroy_iface(&mut phy_req).await.map_err(move |e| {
        error!("Error sending 'DestroyIface' request to phy {:?}: {}", phy_ownership, e);
        zx::Status::INTERNAL
    })?;
    let () = zx::Status::ok(r.status)?;

    ifaces.remove(&id);
    Ok(())
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
    let mac_addr = iface.device_info.mac_addr;
    Ok(fidl_svc::QueryIfaceResponse { role, id, mac_addr, phy_id, phy_assigned_id })
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

    let mut phy_req = fidl_wlan_dev::CountryCode { alpha2: req.alpha2 };
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

async fn create_iface<'a>(
    iface_counter: &'a IfaceCounter,
    phys: &'a PhyMap,
    req: fidl_svc::CreateIfaceRequest,
) -> Result<NewIface, zx::Status> {
    let phy_id = req.phy_id;
    let phy = phys.get(&req.phy_id).ok_or(zx::Status::NOT_FOUND)?;

    let (mlme_channel, sme_channel) = create_proxy::<MlmeMarker>()
        .map_err(|e| {
            error!("failed to create MlmeProxy: {}", e);
            zx::Status::INTERNAL
        })
        .map(|(p, c)| (p, Some(c.into_channel())))?;

    let mut phy_req = fidl_wlan_dev::CreateIfaceRequest {
        role: req.role,
        sme_channel,
        init_mac_addr: req.mac_addr,
    };
    let r = phy.proxy.create_iface(&mut phy_req).await.map_err(move |e| {
        error!("Error sending 'CreateIface' request to phy #{}: {}", phy_id, e);
        zx::Status::INTERNAL
    })?;
    zx::Status::ok(r.status)?;

    Ok(NewIface {
        id: iface_counter.next_iface_id() as u16,
        phy_ownership: device::PhyOwnership { phy_id, phy_assigned_id: r.iface_id },
        mlme_channel,
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
    let empty_peer_list = fidl_fuchsia_wlan_minstrel::Peers { peers: vec![] };
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
    mac_addr: [u8; 6],
) -> (zx::Status, Option<Box<fidl_fuchsia_wlan_minstrel::Peer>>) {
    let iface = match ifaces.get(&iface_id) {
        Some(iface) => iface,
        None => return (zx::Status::NOT_FOUND, None),
    };
    match iface.mlme_query.get_minstrel_peer(mac_addr).await {
        Ok(MinstrelStatsResponse { peer }) => (zx::Status::OK, peer),
        Err(_) => (zx::Status::INTERNAL, None),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::ServerEnd;
    use fidl_fuchsia_wlan_common as fidl_common;
    use fidl_fuchsia_wlan_device::{self as fidl_dev, PhyRequest, PhyRequestStream};
    use fidl_fuchsia_wlan_device_service::{IfaceListItem, PhyListItem};
    use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeMarker};
    use fidl_fuchsia_wlan_sme as fidl_sme;
    use fuchsia_async as fasync;
    use fuchsia_inspect::Inspector;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc;
    use futures::future::BoxFuture;
    use futures::task::Poll;
    use pin_utils::pin_mut;
    use wlan_common::{
        assert_variant,
        channel::{Cbw, Phy},
        RadioConfig,
    };
    use wlan_dev::DeviceEnv;

    use crate::{
        mlme_query_proxy::MlmeQueryProxy,
        stats_scheduler::{self, StatsRequest},
        watcher_service,
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
        let responder = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::Query { responder }))) => responder
        );

        // Reply with a fake phy info
        responder
            .send(&mut fidl_wlan_dev::QueryResponse {
                status: zx::sys::ZX_OK,
                info: fake_phy_info(),
            })
            .expect("failed to send QueryResponse");

        // Our original future should complete now, and return the same phy info
        let response = assert_variant!(exec.run_until_stalled(&mut query_fut),
            Poll::Ready(Ok(response)) => response
        );
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
        let _exec = fasync::Executor::new().expect("Failed to create an executor");

        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let iface = fake_client_iface();
        iface_map.insert(10, iface.iface);

        let response = super::query_iface(&iface_map, 10).expect("querying iface failed");
        let expected = fake_device_info();
        assert_eq!(response.role, fidl_dev::MacRole::Client);
        assert_eq!(response.mac_addr, expected.mac_addr);
        assert_eq!(response.id, 10);
    }

    #[test]
    fn destroy_iface_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (mut phy_map, _phy_map_events) = PhyMap::new();
        let (mut iface_map, _iface_map_events) = IfaceMap::new();
        let mut phy_stream = fake_destroy_iface_env(&mut phy_map, &mut iface_map);

        let destroy_fut = super::destroy_iface(&phy_map, &iface_map, 42);
        pin_mut!(destroy_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut destroy_fut));

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::DestroyIface { req, responder }))) => (req, responder)
        );

        // Verify the destroy iface request to the corresponding PHY is correct.
        assert_eq!(13, req.id);

        responder
            .send(&mut fidl_wlan_dev::DestroyIfaceResponse { status: zx::sys::ZX_OK })
            .expect("failed to send DestroyIfaceResponse");
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut destroy_fut));

        // Verify iface was removed from available ifaces.
        assert!(iface_map.get(&42u16).is_none(), "iface expected to be deleted");
    }

    #[test]
    fn destroy_iface_failure() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (mut phy_map, _phy_map_events) = PhyMap::new();
        let (mut iface_map, _iface_map_events) = IfaceMap::new();
        let mut phy_stream = fake_destroy_iface_env(&mut phy_map, &mut iface_map);

        let destroy_fut = super::destroy_iface(&phy_map, &iface_map, 42);
        pin_mut!(destroy_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut destroy_fut));

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::DestroyIface { req, responder }))) => (req, responder)
        );

        // Verify the destroy iface request to the corresponding PHY is correct.
        assert_eq!(13, req.id);

        responder
            .send(&mut fidl_wlan_dev::DestroyIfaceResponse { status: zx::sys::ZX_ERR_INTERNAL })
            .expect("failed to send DestroyIfaceResponse");
        assert_eq!(
            Poll::Ready(Err(zx::Status::INTERNAL)),
            exec.run_until_stalled(&mut destroy_fut)
        );

        // Verify iface was not removed from available ifaces.
        assert!(iface_map.get(&42u16).is_some(), "iface expected to not be deleted");
    }

    #[test]
    fn destroy_iface_not_found() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (mut phy_map, _phy_map_events) = PhyMap::new();
        let (mut iface_map, _iface_map_events) = IfaceMap::new();
        let _phy_stream = fake_destroy_iface_env(&mut phy_map, &mut iface_map);

        let fut = super::destroy_iface(&phy_map, &iface_map, 43);
        pin_mut!(fut);
        assert_eq!(Poll::Ready(Err(zx::Status::NOT_FOUND)), exec.run_until_stalled(&mut fut));
    }

    #[test]
    fn query_iface_not_found() {
        let (iface_map, _iface_map_events) = IfaceMap::new();
        let iface_map = Arc::new(iface_map);

        let status = super::query_iface(&iface_map, 10u16).expect_err("querying iface succeeded");
        assert_eq!(zx::Status::NOT_FOUND, status);
    }

    #[test]
    fn create_iface_without_mac_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);

        let (phy, mut phy_stream) = fake_phy("/dev/null");
        phy_map.insert(10, phy);

        // Initiate a CreateIface request. The returned future should not be able
        // to produce a result immediately
        let iface_counter = IfaceCounter::new_with_value(5);
        let create_fut = super::create_iface(
            &iface_counter,
            &phy_map,
            fidl_svc::CreateIfaceRequest {
                phy_id: 10,
                role: fidl_wlan_dev::MacRole::Client,
                mac_addr: None,
            },
        );
        pin_mut!(create_fut);
        let fut_result = exec.run_until_stalled(&mut create_fut);
        assert_variant!(fut_result, Poll::Pending);

        // Continue running create iface request.

        let fut_result = exec.run_until_stalled(&mut create_fut);
        assert_variant!(fut_result, Poll::Pending);

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::CreateIface { req, responder }))) => (req, responder)
        );

        // Since we requested the Client role, the request to the phy should also have
        // the Client role
        assert_eq!(fidl_wlan_dev::MacRole::Client, req.role);

        // Pretend that the interface was created with local id 123.
        responder
            .send(&mut fidl_wlan_dev::CreateIfaceResponse { status: zx::sys::ZX_OK, iface_id: 123 })
            .expect("failed to send CreateIfaceResponse");

        // The original future should resolve into a response.
        let response = assert_variant!(exec.run_until_stalled(&mut create_fut),
            Poll::Ready(Ok(response)) => response
        );

        assert_eq!(5, response.id);
        assert_eq!(
            device::PhyOwnership { phy_id: 10, phy_assigned_id: 123 },
            response.phy_ownership
        );

        // TODO(fxbug.dev/29547): response.mlme_channel use and talk to it
    }

    #[test]
    fn create_iface_with_mac_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);

        let (phy, mut phy_stream) = fake_phy("/dev/null");
        phy_map.insert(10, phy);

        // Initiate a CreateIface request. The returned future should not be able
        // to produce a result immediately
        let iface_counter = IfaceCounter::new_with_value(5);
        let mac_addr = Some(vec![1, 2, 3, 4, 5, 6]);

        let create_fut = super::create_iface(
            &iface_counter,
            &phy_map,
            fidl_svc::CreateIfaceRequest { phy_id: 10, role: fidl_wlan_dev::MacRole::Ap, mac_addr },
        );
        pin_mut!(create_fut);
        assert_variant!(exec.run_until_stalled(&mut create_fut), Poll::Pending);

        // Continue running create iface request.
        assert_variant!(exec.run_until_stalled(&mut create_fut), Poll::Pending);

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::CreateIface { req, responder }))) => (req, responder)
        );

        // Since we requested the Ap role, the request to the phy should also have
        // the Ap role
        assert_eq!(fidl_wlan_dev::MacRole::Ap, req.role);
        let res = match req.init_mac_addr {
            None => false,
            Some(mac_addr) => {
                assert_eq!(mac_addr, [1, 2, 3, 4, 5, 6]);
                true
            }
        };
        assert!(res);

        // Pretend that the interface was created with local id 123.
        responder
            .send(&mut fidl_wlan_dev::CreateIfaceResponse { status: zx::sys::ZX_OK, iface_id: 123 })
            .expect("failed to send CreateIfaceResponse");

        // The original future should resolve into a response.
        let response = assert_variant!(exec.run_until_stalled(&mut create_fut),
            Poll::Ready(Ok(response)) => response
        );

        assert_eq!(5, response.id);
        assert_eq!(
            device::PhyOwnership { phy_id: 10, phy_assigned_id: 123 },
            response.phy_ownership
        );
    }

    #[test]
    fn create_iface_not_found() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);

        let iface_counter = IfaceCounter::new_with_value(2);
        let fut = super::create_iface(
            &iface_counter,
            &phy_map,
            fidl_svc::CreateIfaceRequest {
                phy_id: 10,
                role: fidl_wlan_dev::MacRole::Client,
                mac_addr: None,
            },
        );
        pin_mut!(fut);
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Err(zx::Status::NOT_FOUND)),
            "expected failure on invalid PHY"
        );
    }

    #[test]
    fn get_client_sme_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
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

        let iface = fake_ap_iface();
        iface_map.insert(10, iface.iface);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_SUPPORTED, super::get_client_sme(&iface_map, 10, server));
    }

    #[test]
    fn get_ap_sme_success() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (iface_map, _iface_map_events) = IfaceMap::new();
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

        let iface = fake_client_iface();
        iface_map.insert(10, iface.iface);

        let (_proxy, server) = create_proxy().expect("failed to create a pair of SME endpoints");
        assert_eq!(zx::Status::NOT_SUPPORTED, super::get_ap_sme(&iface_map, 10, server));
    }

    #[test]
    fn test_set_country() {
        // Setup environment
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        phy_map.insert(phy_id, phy);
        let alpha2 = fake_alpha2();

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::SetCountryRequest()
        let req_msg = fidl_svc::SetCountryRequest { phy_id, alpha2: alpha2.clone() };
        let req_fut = super::set_country(&phy_map, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::SetCountry { req, responder }))) => {
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
    fn test_set_country_failure() {
        // Setup environment
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        phy_map.insert(phy_id, phy);
        let alpha2 = fake_alpha2();

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::SetCountryRequest()
        let req_msg = fidl_svc::SetCountryRequest { phy_id, alpha2: alpha2.clone() };
        let req_fut = super::set_country(&phy_map, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        let (req, responder) = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::SetCountry { req, responder }))) => (req, responder)
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
    fn test_get_country() {
        // Setup environment
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        phy_map.insert(phy_id, phy);
        let alpha2 = fake_alpha2();

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::SetCountryRequest()
        let req_fut = super::get_country(&phy_map, phy_id);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::GetCountry { responder }))) => {
                // Pretend to be a WLAN PHY to return the result.
                responder.send(
                    &mut Ok(fidl_wlan_dev::CountryCode { alpha2 })
                ).expect("failed to send the response to SetCountry");
            }
        );

        assert_eq!(
            exec.run_until_stalled(&mut req_fut),
            Poll::Ready(Ok(fidl_svc::GetCountryResponse { alpha2 }))
        );
    }

    #[test]
    fn test_get_country_failure() {
        // Setup environment
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        phy_map.insert(phy_id, phy);

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::GetCountryRequest()
        let req_fut = super::get_country(&phy_map, phy_id);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::GetCountry { responder }))) => {
                // Pretend to be a WLAN PHY to return the result.
                // Right now the returned country code is not optional, so we just return garbage.
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                    .expect("failed to send the response to SetCountry");
            }
        );

        assert_variant!(exec.run_until_stalled(&mut req_fut), Poll::Ready(Err(_)));
    }

    #[test]
    fn test_clear_country() {
        // Setup environment
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        phy_map.insert(phy_id, phy);

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::ClearCountryRequest()
        let req_msg = fidl_svc::ClearCountryRequest { phy_id };
        let req_fut = super::clear_country(&phy_map, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::ClearCountry { responder }))) => {
                // Pretend to be a WLAN PHY to return the result.
                responder.send(zx::Status::OK.into_raw())
                    .expect("failed to send the response to ClearCountry");
            }
        );

        // req_fut should have completed by now. Test the result.
        assert_eq!(exec.run_until_stalled(&mut req_fut), Poll::Ready(zx::Status::OK));
    }

    #[test]
    fn test_clear_country_failure() {
        // Setup environment
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phy_map, _phy_map_events) = PhyMap::new();
        let phy_map = Arc::new(phy_map);
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        let phy_id = 10u16;
        phy_map.insert(phy_id, phy);

        // Initiate a QueryPhy request. The returned future should not be able
        // to produce a result immediately
        // Issue service.fidl::ClearCountryRequest()
        let req_msg = fidl_svc::ClearCountryRequest { phy_id };
        let req_fut = super::clear_country(&phy_map, req_msg);
        pin_mut!(req_fut);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        let responder = assert_variant!(exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::ClearCountry { responder }))) => responder
        );

        // Failure case #1: WLAN PHY not responding
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));

        // Failure case #2: WLAN PHY has not implemented the feature.
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut req_fut));
        let resp = zx::Status::NOT_SUPPORTED.into_raw();
        responder.send(resp).expect("failed to send the response to ClearCountry");
        assert_eq!(Poll::Ready(zx::Status::NOT_SUPPORTED), exec.run_until_stalled(&mut req_fut));
    }

    fn setup_create_iface_test() -> (
        fasync::Executor,
        BoxFuture<'static, Result<(), anyhow::Error>>,
        BoxFuture<'static, Result<(i32, Option<Box<fidl_svc::CreateIfaceResponse>>), fidl::Error>>,
        impl Future<Output = Result<void::Void, anyhow::Error>>,
        fidl_wlan_dev::CreateIfaceRequest,
    ) {
        let fake_phy_id = 10;
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (phys, phy_events) = device::PhyMap::new();
        let (ifaces, iface_events) = device::IfaceMap::new();

        let iface_counter = Arc::new(IfaceCounter::new());
        let cfg: ServiceCfg = argh::from_env();
        let phys = Arc::new(phys);
        let ifaces = Arc::new(ifaces);
        let (watcher_service, watcher_fut) =
            watcher_service::serve_watchers(phys.clone(), ifaces.clone(), phy_events, iface_events);

        // Insert a fake PHY
        let (phy, mut phy_stream) = fake_phy("/dev/null");
        phys.insert(fake_phy_id, phy);

        // Create a CobaltSender with a dangling receiver end.
        let (cobalt_sender, _cobalt_receiver) = mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(cobalt_sender);

        // Create an inspector, but don't serve.
        let inspect_tree = Arc::new(inspect::WlanstackTree::new(Inspector::new()));

        let (proxy, marker) =
            create_proxy::<fidl_svc::DeviceServiceMarker>().expect("failed to create proxy");
        let req_stream = marker.into_stream().expect("could not create request stream");

        let fut = serve_device_requests(
            iface_counter,
            cfg,
            phys,
            ifaces,
            watcher_service,
            req_stream,
            inspect_tree,
            cobalt_sender,
        );

        let mut fut = Box::pin(fut);

        // Make the CreateIface request
        let mut create_iface_request = fidl_svc::CreateIfaceRequest {
            phy_id: fake_phy_id,
            role: fidl_wlan_dev::MacRole::Ap,
            mac_addr: None,
        };
        let create_iface_fut = proxy.create_iface(&mut create_iface_request);
        let mut create_iface_fut = Box::pin(create_iface_fut);

        assert_variant!(exec.run_until_stalled(&mut create_iface_fut), Poll::Pending);

        // Advance the server so that it gets the CreateIfaceRequest
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // There should be a pending CreateIfaceRequest. Send it a response and capture its request
        // so that the MLME channel associated with the request can be used to inject a successful
        // MLME response when the PHY's information is queried.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        let (phy_create_iface_req, responder) = assert_variant!(
            exec.run_until_stalled(&mut phy_stream.next()),
            Poll::Ready(Some(Ok(PhyRequest::CreateIface { req, responder }))) => (req, responder)
        );
        responder
            .send(&mut fidl_wlan_dev::CreateIfaceResponse { status: zx::sys::ZX_OK, iface_id: 0 })
            .expect("failed to send CreateIfaceResponse");

        (exec, fut, create_iface_fut, watcher_fut, phy_create_iface_req)
    }

    #[test]
    fn test_query_device_info_succeeds() {
        let (mut exec, mut fut, mut create_iface_fut, _watcher_fut, phy_create_iface_req) =
            setup_create_iface_test();

        // Use the channel that is included in the CreateIfaceRequest to create an MLME request
        // stream.
        let mlme_channel =
            phy_create_iface_req.sme_channel.expect("no mlme stream found in iface request");
        let mut mlme_stream = ServerEnd::<fidl_mlme::MlmeMarker>::new(mlme_channel)
            .into_stream()
            .expect("could not create MLME event stream");

        // Run the server's future so that it can query device information.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QueryDeviceInfo{ responder } ))) => {
                assert!(responder.send(&mut fake_device_info()).is_ok());
            }
        );

        // Run the query future to completion and expect an Ok result.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut create_iface_fut),
            Poll::Ready(Ok((status, Some(response)))) => {
                assert_eq!(status, zx::sys::ZX_OK);
                assert_eq!(*response, fidl_svc::CreateIfaceResponse { iface_id: 0 });
            }
        );
    }

    #[test]
    fn test_query_device_info_fails() {
        // Drop the CreateIfaceRequest to terminate the serving end of the MLME transaction.
        let (mut exec, mut fut, mut create_iface_fut, _watcher_fut, create_iface_req) =
            setup_create_iface_test();
        drop(create_iface_req);

        // Run the server's future so that it can fail to query the device information.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        assert_variant!(
            exec.run_until_stalled(&mut create_iface_fut),
            Poll::Ready(Ok((zx::sys::ZX_ERR_PEER_CLOSED, None)))
        );
    }

    fn fake_destroy_iface_env(phy_map: &mut PhyMap, iface_map: &mut IfaceMap) -> PhyRequestStream {
        let (phy, phy_stream) = fake_phy("/dev/null");
        phy_map.insert(10, phy);

        // Insert device which does not support destruction.
        let iface = fake_client_iface();
        iface_map.insert(10, iface.iface);

        // Insert device which does support destruction.
        let iface = fake_client_iface();
        let iface = FakeClientIface {
            iface: IfaceDevice {
                phy_ownership: device::PhyOwnership { phy_id: 10, phy_assigned_id: 13 },
                ..iface.iface
            },
            ..iface
        };
        iface_map.insert(42, iface.iface);

        phy_stream
    }

    fn fake_phy(path: &str) -> (PhyDevice, PhyRequestStream) {
        let (proxy, server) =
            create_proxy::<fidl_wlan_dev::PhyMarker>().expect("fake_phy: create_proxy() failed");
        let device = wlan_dev::RealDeviceEnv::device_from_path(path)
            .expect(&format!("fake_phy: failed to open {}", path));
        let stream = server.into_stream().expect("fake_phy: failed to create stream");
        (PhyDevice { proxy, device }, stream)
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
            qos_capable: false,
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

    fn fake_alpha2() -> [u8; 2] {
        let mut alpha2: [u8; 2] = [0, 0];
        alpha2.copy_from_slice("MX".as_bytes());
        alpha2
    }
}

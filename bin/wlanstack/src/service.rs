// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use device::{self, PhyDevice, PhyMap, IfaceDevice, IfaceMap};
use failure;
use fidl::encoding2::OutOfLine;
use fidl::endpoints2::RequestStream;
use futures::channel::mpsc::UnboundedReceiver;
use futures::{future, Future, FutureExt, Never, stream};
use futures::prelude::*;
use station;
use stats_scheduler::StatsRef;
use std::sync::Arc;
use watchable_map::MapEvent;
use watcher_service;
use wlan_service::{self, DeviceServiceRequest};
use wlan;
use zx;

many_futures!(ServiceFut,
 [ListPhys, QueryPhy, CreateIface, GetClientSme, GetIfaceStats, WatchDevices]);

pub fn device_service<S>(phys: Arc<PhyMap>, ifaces: Arc<IfaceMap>,
                         phy_events: UnboundedReceiver<MapEvent<u16, PhyDevice>>,
                         iface_events: UnboundedReceiver<MapEvent<u16, IfaceDevice>>,
                         new_clients: S)
    -> impl Future<Item = Never, Error = failure::Error>
    where S: Stream<Item = async::Channel, Error = Never>
{
    let (watcher_service, watcher_fut) = watcher_service::serve_watchers(
        phys.clone(), ifaces.clone(), phy_events, iface_events);
    let server = new_clients
        .map_err(|e| e.never_into())
        .chain(stream::once(Err(format_err!("new client stream in device_service ended unexpectedly"))))
        .for_each_concurrent(move |channel| {
            serve_channel(phys.clone(), ifaces.clone(), watcher_service.clone(), channel)
                .map_err(|e| e.never_into())
        })
        .and_then(|_| Err(format_err!("device_service server future exited unexpectedly")));
    server.join(watcher_fut)
        .map(|x: (Never, Never)| x.0)
}

fn serve_channel(phys: Arc<PhyMap>, ifaces: Arc<IfaceMap>,
                 watcher_service: watcher_service::WatcherService<PhyDevice, IfaceDevice>,
                 channel: async::Channel)
    -> impl Future<Item = (), Error = Never>
{
    // Note that errors from responder.send() are propagated intentionally.
    // If we fail to send a response, the only way to recover is to stop serving the client
    // and close the channel. Otherwise, the client would be left hanging forever.
    wlan_service::DeviceServiceRequestStream::from_channel(channel)
        .for_each_concurrent(move |request| match request {
            DeviceServiceRequest::ListPhys{ responder } => ServiceFut::ListPhys({
                responder.send(&mut list_phys(&phys)).into_future()
            }),
            DeviceServiceRequest::QueryPhy { req, responder } => ServiceFut::QueryPhy({
                query_phy(&phys, req.phy_id)
                    .map_err(|e| e.never_into())
                    .and_then(move |(status, mut r)| {
                        responder.send(status.into_raw(), r.as_mut().map(OutOfLine))
                    })
            }),
            DeviceServiceRequest::ListIfaces { responder: _ } => unimplemented!(),
            DeviceServiceRequest::CreateIface { req, responder } => ServiceFut::CreateIface({
                create_iface(&phys, req)
                    .map_err(|e| e.never_into())
                    .and_then(move |(status, mut r)| {
                        responder.send(status.into_raw(), r.as_mut().map(OutOfLine))
                    })
            }),
            DeviceServiceRequest::DestroyIface{ req: _, responder: _ } => unimplemented!(),
            DeviceServiceRequest::GetClientSme{ iface_id, sme, responder } => ServiceFut::GetClientSme({
                let status = get_client_sme(&ifaces, iface_id, sme);
                responder.send(status.into_raw()).into_future()
            }),
            DeviceServiceRequest::GetIfaceStats{ iface_id, responder } => ServiceFut::GetIfaceStats({
                get_iface_stats(&ifaces, iface_id)
                    .then(move |res| match res {
                        Ok(stats_ref) => {
                            let mut stats = stats_ref.lock();
                            responder.send(zx::sys::ZX_OK, Some(OutOfLine(&mut stats)))
                        }
                        Err(status) => responder.send(status.into_raw(), None)
                    })
            }),
            DeviceServiceRequest::WatchDevices{ watcher, control_handle: _ } => ServiceFut::WatchDevices({
                watcher_service.add_watcher(watcher)
                    .unwrap_or_else(|e| eprintln!("error registering a device watcher: {}", e));
                future::ok(())
            })
        })
        .map(|_| ())
        .recover(|e| eprintln!("error serving a DeviceService client: {}", e))
}

fn list_phys(phys: &Arc<PhyMap>) -> wlan_service::ListPhysResponse {
    let list = phys
        .get_snapshot()
        .iter()
        .map(|(phy_id, phy)| {
            wlan_service::PhyListItem {
                phy_id: *phy_id,
                path: phy.device.path().to_string_lossy().into_owned(),
            }
        })
        .collect();
    wlan_service::ListPhysResponse { phys: list }
}

fn query_phy(phys: &Arc<PhyMap>, id: u16)
    -> impl Future<Item = (zx::Status, Option<wlan_service::QueryPhyResponse>), Error = Never>
{
    let phy = phys.get(&id)
        .map(|phy| (phy.device.path().to_string_lossy().into_owned(), phy.proxy.clone()))
        .ok_or(zx::Status::NOT_FOUND);
    phy.into_future()
        .and_then(move |(path, proxy)| {
            proxy.query()
                .map_err(move |e| {
                    eprintln!("Error sending 'Query' request to phy #{}: {}", id, e);
                    zx::Status::INTERNAL
                })
                .and_then(move |query_result| {
                    zx::Status::ok(query_result.status)
                        .map(|()| {
                            let mut info = query_result.info;
                            info.id = id;
                            info.dev_path = Some(path);
                            info
                        })
                })
        })
        .then(|r| match r {
            Ok(phy_info) => {
                let resp = wlan_service::QueryPhyResponse { info: phy_info };
                Ok((zx::Status::OK, Some(resp)))
            },
            Err(status) => Ok((status, None)),
        })
}

fn create_iface(phys: &Arc<PhyMap>, req: wlan_service::CreateIfaceRequest)
    -> impl Future<Item = (zx::Status, Option<wlan_service::CreateIfaceResponse>),
                   Error = Never>
{
    phys.get(&req.phy_id)
        .map(|phy| phy.proxy.clone())
        .ok_or(zx::Status::NOT_FOUND)
        .into_future()
        .and_then(move |proxy| {
            let mut phy_req = wlan::CreateIfaceRequest { role: req.role };
            proxy.create_iface(&mut phy_req)
                .map_err(move |e| {
                    eprintln!("Error sending 'CreateIface' request to phy #{}: {}", req.phy_id, e);
                    zx::Status::INTERNAL
                })
                .and_then(|r| zx::Status::ok(r.status).map(move |()| r.info))
        })
        .then(|r| match r {
            Ok(info) => {
                // TODO(gbonik): this is not the ID that we want to return
                let resp = wlan_service::CreateIfaceResponse { iface_id: info.id };
                Ok((zx::Status::OK, Some(resp)))
            },
            Err(status) => Ok((status, None)),
        })
}

fn get_client_sme(ifaces: &Arc<IfaceMap>, iface_id: u16,
                  endpoint: station::ClientSmeEndpoint)
    -> zx::Status
{
    let iface = ifaces.get(&iface_id);
    let server = match iface {
        None => return zx::Status::NOT_FOUND,
        Some(ref iface) => match iface.sme_server {
            device::SmeServer::Client(ref server) => server,
            _ => return zx::Status::NOT_SUPPORTED
        }
    };
    match server.unbounded_send(endpoint) {
        Ok(()) => zx::Status::OK,
        Err(e) => {
            eprintln!("error sending an endpoint to the SME server future: {}", e);
            zx::Status::INTERNAL
        }
    }
}

fn get_iface_stats(ifaces: &Arc<IfaceMap>, iface_id: u16)
    -> impl Future<Item = StatsRef, Error = zx::Status>
{
    ifaces.get(&iface_id)
        .ok_or(zx::Status::NOT_FOUND)
        .into_future()
        .and_then(|iface| iface.stats_sched.get_stats())
}

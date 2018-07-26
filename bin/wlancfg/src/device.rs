// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use config::{self, Config};
use client;
use known_ess_store::KnownEssStore;
use fidl::{self, endpoints2::create_endpoints};
use futures::prelude::*;
use futures::{future, stream};
use shim;
use std::sync::Arc;
use wlan;
use wlan_service::{self, DeviceWatcherEvent, DeviceServiceProxy};
use zx;

pub struct Listener {
    proxy: DeviceServiceProxy,
    config: Config,
    legacy_client: shim::ClientRef,
}

pub fn handle_event(listener: &Arc<Listener>, evt: DeviceWatcherEvent, ess_store: &Arc<KnownEssStore>)
    -> impl Future<Item = (), Error = fidl::Error>
{
    println!("wlancfg got event: {:?}", evt);
    match evt {
        DeviceWatcherEvent::OnPhyAdded { phy_id } => on_phy_added(
            listener, phy_id,
            ).map_err(|e| e.never_into())
            .left_future()
            .left_future(),
        DeviceWatcherEvent::OnPhyRemoved { phy_id } => on_phy_removed(
            listener, phy_id,
            ).map_err(|e| e.never_into())
            .right_future()
            .left_future(),
        DeviceWatcherEvent::OnIfaceAdded { iface_id } => on_iface_added(
            listener, iface_id, Arc::clone(ess_store),
            ).map_err(|e| e.never_into())
            .left_future()
            .right_future(),
        DeviceWatcherEvent::OnIfaceRemoved { iface_id } => on_iface_removed(
            listener, iface_id,
            ).map_err(|e| e.never_into())
            .right_future()
            .right_future(),
    }
}

fn on_phy_added(listener: &Arc<Listener>, id: u16)
    -> impl Future<Item = (), Error = Never>
{
    println!("wlancfg: phy {} added", id);

    let listener_ref = listener.clone();
    listener
        .proxy
        .query_phy(&mut wlan_service::QueryPhyRequest { phy_id: id })
        .and_then(move |resp| {
            let (status, query_resp) = resp;
            if let Err(e) = zx::Status::ok(status) {
                println!("wlancfg: failed to query phy {}: {:?}", id, e);
                return future::ok(()).left_future();
            }

            let info = match query_resp {
                Some(r) => r.info,
                None => {
                    println!("wlancfg: query_phy failed to return a PhyInfo in the QueryPhyResponse");
                    return future::ok(()).left_future();
                }
            };
            let path = info.dev_path.unwrap_or("*".into());
            println!("wlancfg: received a PhyInfo from phy #{}: path is {}", id, path);
            let roles_to_create = match listener_ref.config.roles_for_path(&path) {
                Some(roles) => roles,
                None => {
                    println!("wlancfg: no matches for wlan phy {}", id);
                    return future::ok(()).left_future();
                }
            };

            roles_to_create
                .iter()
                .map(|role: &config::Role| {
                    println!("wlancfg: Creating {:?} iface for phy {}", role, id);
                    let mut req = wlan_service::CreateIfaceRequest {
                        phy_id: id,
                        role: wlan::MacRole::from(*role),
                    };
                    listener_ref
                        .proxy
                        .create_iface(&mut req)
                        .map(|_| ())
                        .recover(|e| eprintln!("error creating iface: {:?}", e))
                })
                .collect::<stream::FuturesUnordered<_>>()
                .for_each(|()| Ok(()))
                .map(|_| ())
                .right_future()
        })
        .recover(|e| println!("failure in on_phy_added: {:?}", e))
}

fn on_phy_removed(_listener: &Arc<Listener>, id: u16)
    -> impl Future<Item = (), Error = Never>
{
    println!("wlancfg: phy removed: {}", id);
    future::ok(())
}

fn on_iface_added(listener: &Arc<Listener>, iface_id: u16, ess_store: Arc<KnownEssStore>)
    -> impl Future<Item = (), Error = Never>
{
    let service = listener.proxy.clone();
    let legacy_client = listener.legacy_client.clone();
    create_endpoints()
        .into_future()
        .and_then(move |(sme, remote)| {
            service.get_client_sme(iface_id, remote)
                .map(move |status| {
                    if status == zx::sys::ZX_OK {
                        let (c, fut) = client::new_client(iface_id, sme.clone(), ess_store);
                        async::spawn(fut);
                        let lc = shim::Client { service, client: c, sme, iface_id };
                        legacy_client.set_if_empty(lc);
                    } else {
                        eprintln!("GetClientSme returned {}", zx::Status::from_raw(status));
                    }
                })
        })
        .recover(|e| eprintln!("Failed to get client SME: {}", e))
        .inspect(move |()| println!("wlancfg: iface added: {}", iface_id))
}

fn on_iface_removed(listener: &Arc<Listener>, id: u16)
    -> impl Future<Item = (), Error = Never>
{
    listener.legacy_client.remove_if_matching(id);
    println!("wlancfg: iface removed: {}", id);
    future::ok(())
}

impl Listener {
    pub fn new(proxy: DeviceServiceProxy, config: Config, legacy_client: shim::ClientRef) -> Arc<Self> {
        Arc::new(Listener { proxy, config, legacy_client })
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use config::{self, Config};
use futures::prelude::*;
use futures::{future, stream};
use std::sync::Arc;
use wlan;
use wlan_service::{self, DeviceListener, DeviceListenerImpl, DeviceServiceProxyInterface};
use zx;

#[derive(Debug)]
pub struct Listener<Proxy>
where
    Proxy: DeviceServiceProxyInterface,
{
    proxy: Proxy,
    config: Config,
}

fn on_phy_added<Proxy>(
    listener: &Arc<Listener<Proxy>>, id: u16
) -> impl Future<Item = (), Error = Never>
where
    Proxy: DeviceServiceProxyInterface,
{
    println!("wlancfg: phy {} added", id);

    let listener_ref = listener.clone();
    listener
        .proxy
        .query_phy(&mut wlan_service::QueryPhyRequest { phy_id: id })
        .and_then(move |resp| {
            let (status, query_resp) = resp;
            if let Err(e) = zx::Status::ok(status) {
                println!("failed to query phy {}: {:?}", id, e);
                return future::ok(()).left();
            }

            let info = match query_resp {
                Some(r) => r.info,
                None => {
                    println!("query_phy failed to return a a PhyInfo in the QueryPhyResponse");
                    return future::ok(()).left();
                }
            };
            let path = info.dev_path.unwrap_or("*".into());
            let roles_to_create = match listener_ref.config.roles_for_path(&path) {
                Some(roles) => roles,
                None => {
                    println!("no matches for wlan phy {}", id);
                    return future::ok(()).left();
                }
            };

            roles_to_create
                .iter()
                .map(|role: &config::Role| {
                    println!("Creating {:?} iface for phy {}", role, id);
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
                .collect()
                .map(|_| ())
                .right()
        })
        .recover(|e| println!("failure in on_phy_added: {:?}", e))
}

fn on_phy_removed<Proxy>(
    _listener: &Arc<Listener<Proxy>>, id: u16
) -> impl Future<Item = (), Error = Never>
where
    Proxy: DeviceServiceProxyInterface,
{
    println!("wlancfg: phy removed: {}", id);
    future::ok(())
}

fn on_iface_added<Proxy>(
    _listener: &Arc<Listener<Proxy>>, phy_id: u16, iface_id: u16
) -> impl Future<Item = (), Error = Never>
where
    Proxy: DeviceServiceProxyInterface,
{
    println!("wlancfg: iface added: {} (phy={})", iface_id, phy_id);
    future::ok(())
}

fn on_iface_removed<Proxy>(
    _listener: &Arc<Listener<Proxy>>, phy_id: u16, iface_id: u16
) -> impl Future<Item = (), Error = Never>
where
    Proxy: DeviceServiceProxyInterface,
{
    println!("wlancfg: iface removed: {} (phy={}", iface_id, phy_id);
    future::ok(())
}

impl<Proxy> Listener<Proxy>
where
    Proxy: DeviceServiceProxyInterface,
{
    pub fn new(proxy: Proxy, config: Config) -> Self {
        Listener { proxy, config }
    }
}

pub fn device_listener<Proxy>(state: Listener<Proxy>) -> impl DeviceListener
where
    Proxy: DeviceServiceProxyInterface,
{
    DeviceListenerImpl {
        state: Arc::new(state),
        on_phy_added: |state, id, _| on_phy_added(state, id),

        on_phy_removed: |state, id, _| on_phy_removed(state, id),

        on_iface_added: |state, phy_id, iface_id, _| on_iface_added(state, phy_id, iface_id),

        on_iface_removed: |state, phy_id, iface_id, _| on_iface_removed(state, phy_id, iface_id),
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future;
use futures::prelude::*;
use wlan;
use wlan_service::{self, DeviceListener, DeviceListenerImpl, DeviceServiceProxy};

#[derive(Debug)]
pub struct Listener {
    proxy: DeviceServiceProxy,
}

impl Listener {
    pub fn new(proxy: DeviceServiceProxy) -> Self {
        Listener { proxy }
    }

    fn on_phy_added(&mut self, id: u16) -> impl Future<Item = (), Error = Never> {
        println!("wlancfg: phy {} added", id);
        // For now, just create a Client iface on the new phy.
        // TODO(tkilbourn): get info about this phy, then consult a configuration file to determine
        // what interfaces to create.
        let mut req = wlan_service::CreateIfaceRequest {
            phy_id: id,
            role: wlan::MacRole::Client,
        };
        self.proxy
            .create_iface(&mut req)
            .map(|_| ())
            .recover(|e| eprintln!("error creating iface: {:?}", e))
    }

    fn on_phy_removed(&mut self, id: u16) -> impl Future<Item = (), Error = Never> {
        println!("wlancfg: phy removed: {}", id);
        future::ok(())
    }

    fn on_iface_added(
        &mut self,
        phy_id: u16,
        iface_id: u16,
    ) -> impl Future<Item = (), Error = Never> {
        println!("wlancfg: iface added: {} (phy={})", iface_id, phy_id);
        future::ok(())
    }

    fn on_iface_removed(
        &mut self,
        phy_id: u16,
        iface_id: u16,
    ) -> impl Future<Item = (), Error = Never> {
        println!("wlancfg: iface removed: {} (phy={}", iface_id, phy_id);
        future::ok(())
    }
}

pub fn device_listener(state: Listener) -> impl DeviceListener {
    DeviceListenerImpl {
        state,
        on_phy_added: |state, id, _| state.on_phy_added(id),

        on_phy_removed: |state, id, _| state.on_phy_removed(id),

        on_iface_added: |state, phy_id, iface_id, _| state.on_iface_added(phy_id, iface_id),

        on_iface_removed: |state, phy_id, iface_id, _| state.on_iface_removed(phy_id, iface_id),
    }
}

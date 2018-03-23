// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use device::{self, DevMgrRef};
use failure::Error;
use fidl;
use futures::{Future, FutureExt, Never};
use futures::future::{self, FutureResult};
use wlan_service::{
    self,
    DeviceListenerProxy,
    DeviceService,
    DeviceServiceImpl,
};

fn catch_and_log_err<F>(ctx: &'static str, f: F) -> FutureResult<(), Never>
    where F: FnOnce() -> Result<(), fidl::Error>
{
    let res = f();
    if let Err(e) = res {
        eprintln!("Error running device_settings_server fidl handler {}: {:?}", ctx, e);
    }
    future::ok(())
}

pub fn device_service(devmgr: DevMgrRef, channel: async::Channel)
    -> impl Future<Item = (), Error = Never> {
    DeviceServiceImpl {
        state: devmgr,

        list_phys: |state, c| catch_and_log_err("list_phys", || {
            debug!("list_phys");
            let result = state.lock().list_phys();
            c.send(&mut wlan_service::ListPhysResponse { phys: result })
        }),

        list_ifaces: |_, c| catch_and_log_err("list_ifaces", || {
            debug!("list_ifaces (stub)");
            c.send(&mut wlan_service::ListIfacesResponse { ifaces: vec![] })
        }),

        create_iface: |state, req, c| catch_and_log_err("create_iface", || {
            debug!("create_iface req: {:?}", req);
            // TODO(tkilbourn): have DeviceManager return a future here
            let resp = state.lock().create_iface(req.phy_id, req.role);
            match resp {
                Ok(resp) => c.send(&mut wlan_service::CreateIfaceResponse { iface_id: resp }),
                Err(e) => {
                    error!("could not create iface: {:?}", e);
                    Ok(())
                }
            }
        }),

        destroy_iface: |state, req, _| catch_and_log_err("destroy_iface", || {
            debug!("destroy_iface req: {:?}", req);
            // TODO(tkilbourn): have DeviceManager return a future here
            let _ = state.lock().destroy_iface(req.phy_id, req.iface_id);
            Ok(())
        }),

        register_listener: |state, listener, _| catch_and_log_err("register_listener", || {
            debug!("register listener");
            if let Ok(proxy) = listener.into_proxy() {
                state.lock().add_listener(Box::new(proxy));
            }
            Ok(())
        }),
    }
    .serve(channel)
    .recover(|e| eprintln!("error running wlan device service: {:?}", e))
}

impl device::EventListener for DeviceListenerProxy {
    fn on_phy_added(&self, mut id: u16) -> Result<(), Error> {
        DeviceListenerProxy::on_phy_added(self, &mut id).map_err(|e| e.into())
    }

    fn on_phy_removed(&self, mut id: u16) -> Result<(), Error> {
        DeviceListenerProxy::on_phy_removed(self, &mut id).map_err(|e| e.into())
    }
}

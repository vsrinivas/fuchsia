// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use device::{self, DevMgrRef};
use failure::Error;
use fidl;
use futures::future;
use wlan_service::{self, DeviceListener, DeviceService};

struct State {
    devmgr: DevMgrRef,
}

pub fn device_service(
    devmgr: DevMgrRef,
) -> impl DeviceService::Server {
    DeviceService::Impl {
        state: State {
            devmgr: devmgr,
        },

        list_phys: |state| {
            debug!("list_phys");
            let result = state.devmgr.lock().list_phys();
            future::ok(wlan_service::ListPhysResponse { phys: result })
        },

        list_ifaces: |_| {
            debug!("list_ifaces (stub)");
            future::ok(wlan_service::ListIfacesResponse { ifaces: vec![] })
        },

        create_iface: |state, req| {
            debug!("create_iface req: {:?}", req);
            // TODO(tkilbourn): have DeviceManager return a future here
            let resp = state.devmgr.lock().create_iface(req.phy_id, req.role);
            match resp {
                Ok(resp) => future::ok(wlan_service::CreateIfaceResponse { iface_id: resp }),
                Err(e) => {
                    error!("could not create iface: {:?}", e);
                    future::err(fidl::CloseChannel)
                }
            }
        },

        destroy_iface: |state, req| {
            debug!("destroy_iface req: {:?}", req);
            // TODO(tkilbourn): have DeviceManager return a future here
            let resp = state
                .devmgr
                .lock()
                .destroy_iface(req.phy_id, req.iface_id);
            match resp {
                Ok(_) => future::ok(()),
                Err(_) => future::err(fidl::CloseChannel),
            }
        },

        register_listener: |state, listener| {
            debug!("register listener");
            if let Ok(proxy) = DeviceListener::new_proxy(listener.inner) {
                state.devmgr.lock().add_listener(Box::new(proxy));
                future::ok(())
            } else {
                future::err(fidl::CloseChannel)
            }
        },
    }
}

impl device::EventListener for DeviceListener::Proxy {
    fn on_phy_added(&self, id: u16) -> Result<(), Error> {
        DeviceListener::Client::on_phy_added(self, id).map_err(|e| e.into())
    }

    fn on_phy_removed(&self, id: u16) -> Result<(), Error> {
        DeviceListener::Client::on_phy_removed(self, id).map_err(|e| e.into())
    }
}

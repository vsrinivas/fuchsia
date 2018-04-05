// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use device::{self, DevMgrRef};
use failure::Error;
use fidl;
use futures::future::{self, FutureResult};
use futures::{Future, FutureExt, Never, StreamExt};
use wlan_service::{self, DeviceListenerProxy, DeviceService, DeviceServiceImpl};
use zx;

fn catch_and_log_err<F>(ctx: &'static str, f: F) -> FutureResult<(), Never>
where
    F: FnOnce() -> Result<(), fidl::Error>,
{
    let res = f();
    if let Err(e) = res {
        eprintln!("Error running wlanstack fidl handler {}: {:?}", ctx, e);
    }
    future::ok(())
}

pub fn device_service(
    devmgr: DevMgrRef,
    channel: async::Channel,
) -> impl Future<Item = (), Error = Never> {
    DeviceServiceImpl {
        state: devmgr,

        list_phys: |state, c| {
            debug!("list_phys");
            state
                .lock()
                .list_phys()
                .collect()
                .then(move |phys| match phys {
                    Ok(p) => catch_and_log_err("list_phys", || {
                        c.send(&mut wlan_service::ListPhysResponse { phys: p })
                    }),
                    Err(e) => {
                        error!("could not query phys: {:?}", e);
                        future::ok(())
                    }
                })
        },

        list_ifaces: |_, c| {
            catch_and_log_err("list_ifaces", || {
                debug!("list_ifaces (stub)");
                c.send(&mut wlan_service::ListIfacesResponse { ifaces: vec![] })
            })
        },

        create_iface: |state, req, c| {
            debug!("create_iface req: {:?}", req);
            state
                .lock()
                .create_iface(req.phy_id, req.role)
                .then(move |res| {
                    catch_and_log_err("create_iface", || match res {
                        Ok(id) => c.send(
                            &mut zx::Status::OK.into_raw(),
                            &mut Some(Box::new(wlan_service::CreateIfaceResponse { iface_id: id })),
                        ),
                        Err(e) => {
                            error!("could not create iface: {:?}", e);
                            c.send(&mut e.into_raw(), &mut None)
                        }
                    })
                })
        },

        destroy_iface: |state, req, c| {
            debug!("destroy_iface req: {:?}", req);
            state
                .lock()
                .destroy_iface(req.phy_id, req.iface_id)
                .then(move |res| {
                    catch_and_log_err("destroy_iface", || match res {
                        Ok(()) => c.send(&mut zx::Status::OK.into_raw()),
                        Err(e) => c.send(&mut e.into_raw()),
                    })
                })
        },

        register_listener: |state, listener, c| {
            catch_and_log_err("register_listener", || {
                debug!("register listener");
                if let Ok(proxy) = listener.into_proxy() {
                    state.lock().add_listener(Box::new(proxy));
                }
                c.send(&mut zx::Status::OK.into_raw())
            })
        },
    }.serve(channel)
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

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use device::{self, DevMgrRef};
use failure::Error;
use fidl;
use fidl::encoding2::OutOfLine;
use futures::future::{self, FutureResult};
use futures::{Future, FutureExt, Never, StreamExt};
use wlan_service::{self, DeviceService, DeviceServiceControlHandle, DeviceServiceImpl};
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
    devmgr: DevMgrRef, channel: async::Channel,
) -> impl Future<Item = (), Error = Never> {
    DeviceServiceImpl {
        state: devmgr,
        on_open: |state, control_handle| {
            debug!("on_open");
            state.lock().add_listener(Box::new(control_handle));
            future::ok(())
        },

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

        query_phy: |state, req, c| {
            debug!("query_phy req: {:?}", req);
            state.lock().query_phy(req.phy_id).then(move |res| {
                catch_and_log_err("query_phy", || match res {
                    Ok(info) => c.send(
                        zx::Status::OK.into_raw(),
                        Some(OutOfLine(&mut wlan_service::QueryPhyResponse { info })),
                    ),
                    Err(e) => c.send(e.into_raw(), None),
                })
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
                            zx::Status::OK.into_raw(),
                            Some(OutOfLine(&mut wlan_service::CreateIfaceResponse { iface_id: id })),
                        ),
                        Err(e) => {
                            error!("could not create iface: {:?}", e);
                            c.send(e.into_raw(), None)
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
                        Ok(()) => c.send(zx::Status::OK.into_raw()),
                        Err(e) => c.send(e.into_raw()),
                    })
                })
        },
    }.serve(channel)
        .recover(|e| eprintln!("error running wlan device service: {:?}", e))
}

impl device::EventListener for DeviceServiceControlHandle {
    fn on_phy_added(&self, id: u16) -> Result<(), Error> {
        self.send_on_phy_added(id).map_err(Into::into)
    }

    fn on_phy_removed(&self, id: u16) -> Result<(), Error> {
        self.send_on_phy_removed(id).map_err(Into::into)
    }

    fn on_iface_added(&self, id: u16) -> Result<(), Error> {
        self.send_on_iface_added(id).map_err(Into::into)
    }

    fn on_iface_removed(&self, id: u16) -> Result<(), Error> {
        self.send_on_iface_removed(id).map_err(Into::into)
    }
}

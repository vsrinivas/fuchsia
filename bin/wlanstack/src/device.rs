// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use device_watch;
use failure::Error;
use futures::prelude::*;
use futures::{future, stream};
use futures::channel::mpsc;
use parking_lot::Mutex;
use watchable_map::{MapWatcher, WatchableMap, WatcherResult};
use wlan;
use wlan_dev;
use wlan_service;
use zx;

use std::sync::Arc;

struct PhyDevice {
    proxy: wlan::PhyProxy,
    device: wlan_dev::Device,
}

pub type ClientSmeServer = mpsc::UnboundedSender<super::station::ClientSmeEndpoint>;

struct IfaceDevice {
    client_sme_server: Option<ClientSmeServer>,
    _device: wlan_dev::Device,
}

/// Called by the `DeviceManager` in response to device events.
pub trait EventListener: Send + Sync {
    /// Called when a phy device is added. On error, the listener is removed from the
    /// `DeviceManager`.
    fn on_phy_added(&self, id: u16) -> Result<(), Error>;

    /// Called when a phy device is removed. On error, the listener is removed from the
    /// `DeviceManager`.
    fn on_phy_removed(&self, id: u16) -> Result<(), Error>;

    /// Called when an iface device is added. On error, the listener is removed from the
    /// `DeviceManager`.
    fn on_iface_added(&self, id: u16) -> Result<(), Error>;

    /// Called when an iface device is removed. On error, the listener is removed from
    /// the `DeviceManager`.
    fn on_iface_removed(&self, id: u16) -> Result<(), Error>;
}

pub type DevMgrRef = Arc<Mutex<DeviceManager>>;

struct PhyWatcher(Arc<EventListener>);
impl MapWatcher<u16> for PhyWatcher {
    fn on_add_key(&self, key: &u16) -> WatcherResult {
        handle_notification_error("on_phy_added", self.0.on_phy_added(*key))
    }
    fn on_remove_key(&self, key: &u16) -> WatcherResult {
        handle_notification_error("on_phy_removed", self.0.on_phy_removed(*key))
    }
}

struct IfaceWatcher(Arc<EventListener>);
impl MapWatcher<u16> for IfaceWatcher {
    fn on_add_key(&self, key: &u16) -> WatcherResult {
        handle_notification_error("on_iface_added", self.0.on_iface_added(*key))
    }
    fn on_remove_key(&self, key: &u16) -> WatcherResult {
        handle_notification_error("on_iface_removed", self.0.on_iface_removed(*key))
    }
}

fn handle_notification_error(event_name: &str, r: Result<(), Error>) -> WatcherResult {
    match r {
        Ok(()) => WatcherResult::KeepWatching,
        Err(e) => {
            eprintln!("Failed to notify a watcher of {} event: {}", event_name, e);
            WatcherResult::StopWatching
        }
    }
}

/// Manages the wlan devices used by the wlanstack.
pub struct DeviceManager {
    phys: WatchableMap<u16, PhyDevice, PhyWatcher>,
    ifaces: WatchableMap<u16, IfaceDevice, IfaceWatcher>,
}

impl DeviceManager {
    /// Create a new `DeviceManager`.
    pub fn new() -> Self {
        DeviceManager {
            phys: WatchableMap::new(),
            ifaces: WatchableMap::new(),
        }
    }

    fn add_phy(&mut self, id: u16, phy: PhyDevice) {

        self.phys.insert(id, phy);
    }

    fn rm_phy(&mut self, id: u16) {
        self.phys.remove(&id);
    }

    fn add_iface(&mut self, new_iface: device_watch::NewIfaceDevice)
        -> impl Future<Item = (), Error = Never>
    {
        // TODO(gbonik): move this code outside of DeviceManager
        let (sender, receiver) = mpsc::unbounded();
        // TODO(gbonik): check the role of the interface instead of assuming it is a station
        self.ifaces.insert(new_iface.id, IfaceDevice {
            client_sme_server: Some(sender),
            _device: new_iface.device,
        });
        super::station::serve_client_sme(new_iface.proxy, receiver).recover::<Never, _>(|e| {
            eprintln!("Error serving client station: {:?}", e);
        })
    }

    fn rm_iface(&mut self, id: u16) {
        self.ifaces.remove(&id);
    }

    /// Retrieves information about all the phy devices managed by this `DeviceManager`.
    pub fn list_phys(&self) -> Vec<wlan_service::PhyListItem> {
        self.phys
            .iter()
            .map(|(phy_id, phy)| {
                wlan_service::PhyListItem {
                    phy_id: *phy_id,
                    path: phy.device.path().to_string_lossy().into_owned(),
                }
            })
            .collect()
    }

    pub fn query_phy(&self, id: u16) -> impl Future<Item = wlan::PhyInfo, Error = zx::Status> {
        let phy = match self.phys.get(&id) {
            Some(p) => p,
            None => return future::err(zx::Status::NOT_FOUND).left_future(),
        };
        let phy_path = phy.device.path().to_string_lossy().into_owned();
        phy.proxy
            .query()
            .map_err(|_| zx::Status::INTERNAL)
            .and_then(move |response| {
                zx::Status::ok(response.status)
                    .into_future()
                    .map(move |()| {
                        let mut info = response.info;
                        info.id = id;
                        info.dev_path = Some(phy_path);
                        info
                    })
            })
            .right_future()
    }

    /// Creates an interface on the phy with the given id.
    pub fn create_iface(
        &mut self, phy_id: u16, role: wlan::MacRole,
    ) -> impl Future<Item = u16, Error = zx::Status> + Send {
        let phy = match self.phys.get(&phy_id) {
            Some(p) => p,
            None => return future::err(zx::Status::INVALID_ARGS).left_future(),
        };
        let mut req = wlan::CreateIfaceRequest { role };
        phy.proxy
            .create_iface(&mut req)
            .map_err(|_| zx::Status::IO)
            .and_then(|resp| {
                if let Ok(()) = zx::Status::ok(resp.status) {
                    future::ok(resp.info.id)
                } else {
                    future::err(zx::Status::from_raw(resp.status))
                }
            })
            .right_future()
    }

    /// Destroys an interface with the given ids.
    pub fn destroy_iface(
        &mut self, phy_id: u16, iface_id: u16,
    ) -> impl Future<Item = (), Error = zx::Status> {
        let phy = match self.phys.get(&phy_id) {
            Some(p) => p,
            None => return future::err(zx::Status::INVALID_ARGS).left_future(),
        };
        let mut req = wlan::DestroyIfaceRequest { id: iface_id };
        phy.proxy
            .destroy_iface(&mut req)
            .map_err(|_| zx::Status::IO)
            .and_then(|resp| zx::Status::ok(resp.status).into_future())
            .right_future()
    }

    /// Adds an `EventListener`. The event methods will be called for each existing object tracked
    /// by this device manager.
    pub fn add_listener(&mut self, listener: Arc<EventListener>) {
        self.phys.add_watcher(PhyWatcher(listener.clone()));
        self.ifaces.add_watcher(IfaceWatcher(listener));
    }

    pub fn get_client_sme(&mut self, iface_id: u16) -> Option<ClientSmeServer> {
        self.ifaces.get(&iface_id).and_then(|dev| dev.client_sme_server.clone())
    }
}

pub fn serve_phys(devmgr: DevMgrRef)
    -> Result<impl Future<Item = (), Error = Error>, Error>
{
    Ok(device_watch::watch_phy_devices()?
        .err_into()
        .chain(stream::once(Err(format_err!("phy watcher stream unexpectedly finished"))))
        .for_each_concurrent(move |new_phy| {
            println!("new phy #{}: {}", new_phy.id, new_phy.device.path().to_string_lossy());
            let id = new_phy.id;
            let event_stream = new_phy.proxy.take_event_stream();
            devmgr.lock().add_phy(id, PhyDevice {
                proxy: new_phy.proxy,
                device: new_phy.device,
            });
            let devmgr = devmgr.clone();
            event_stream
                .for_each(|_| Ok(()))
                .then(move |r| {
                    println!("phy removed: {}", id);
                    devmgr.lock().rm_phy(id);
                    r.map(|_| ()).map_err(|e| e.into())
                })
        })
        .map(|_| ()))
}

pub fn serve_ifaces(devmgr: DevMgrRef)
    -> Result<impl Future<Item = (), Error = Error>, Error>
{
    Ok(device_watch::watch_iface_devices()?
        .err_into()
        .chain(stream::once(Err(format_err!("iface watcher stream unexpectedly finished"))))
        .for_each_concurrent(move |new_iface| {
            println!("new iface #{}: {}", new_iface.id, new_iface.device.path().to_string_lossy());
            let id = new_iface.id;
            let devmgr_ref = devmgr.clone();
            let mut devmgr = devmgr.lock();
            devmgr.add_iface(new_iface)
                .then(move |r| {
                    println!("iface removed: {}", id);
                    devmgr_ref.lock().rm_iface(id);
                    r
                })
                .map_err(|e| e.never_into())
        })
        .map(|_| ()))
}

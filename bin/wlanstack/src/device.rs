// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use failure::Error;
use futures::prelude::*;
use futures::{future, stream};
use parking_lot::Mutex;
use vfs_watcher;
use wlan;
use wlan_dev;
use zx;

use std::collections::HashMap;
use std::fs::File;
use std::path::Path;
use std::str::FromStr;
use std::sync::Arc;

struct PhyDevice {
    id: u16,
    proxy: wlan::PhyProxy,
    _dev: wlan_dev::WlanPhy,
}

impl PhyDevice {
    fn new<P: AsRef<Path>>(id: u16, path: P) -> Result<Self, Error> {
        let dev = wlan_dev::WlanPhy::new(path)?;
        Ok(PhyDevice {
            id,
            proxy: dev.connect()?,
            // TODO(tkilbourn): see about removing this field. It will close the fd to the device,
            // which means the refcount on the dev node goes down. Investigate how this works out
            // in practice.
            _dev: dev,
        })
    }
}

/// Called by the `DeviceManager` in response to device events.
pub trait EventListener: Send {
    /// Called when a phy device is added. On error, the listener is removed from the
    /// `DeviceManager`.
    fn on_phy_added(&self, id: u16) -> Result<(), Error>;

    /// Called when a phy device is removed. On error, the listener is removed from the
    /// `DeviceManager`.
    fn on_phy_removed(&self, id: u16) -> Result<(), Error>;
}

pub type DevMgrRef = Arc<Mutex<DeviceManager>>;

/// Manages the wlan devices used by the wlanstack.
pub struct DeviceManager {
    phys: HashMap<u16, PhyDevice>,
    listeners: Vec<Box<EventListener>>,
}

impl DeviceManager {
    /// Create a new `DeviceManager`.
    pub fn new() -> Self {
        DeviceManager {
            phys: HashMap::new(),
            listeners: Vec::new(),
        }
    }

    fn add_phy(&mut self, phy: PhyDevice) {
        let id = phy.id;
        self.phys.insert(phy.id, phy);
        self.listeners
            .retain(|listener| listener.on_phy_added(id).is_ok());
    }

    fn rm_phy(&mut self, id: u16) {
        self.phys.remove(&id);
        self.listeners
            .retain(|listener| listener.on_phy_removed(id).is_ok());
    }

    /// Retrieves information about all the phy devices managed by this `DeviceManager`.
    pub fn list_phys(&self) -> impl Stream<Item = wlan::PhyInfo, Error = ()> {
        self.phys
            .values()
            .map(|phy| {
                let phy_id = phy.id;
                // For now we query each device for every call to `list_phys`. We will need to
                // decide how to handle queries for static vs dynamic data, caching response, etc.
                phy.proxy.query().map_err(|_| ()).map(move |response| {
                    let mut info = response.info;
                    info.id = phy_id;
                    info
                })
            })
            .collect::<stream::FuturesUnordered<_>>()
    }

    /// Creates an interface on the phy with the given id.
    pub fn create_iface(
        &mut self,
        phy_id: u16,
        role: wlan::MacRole,
    ) -> impl Future<Item = u16, Error = zx::Status> + Send {
        let phy = match self.phys.get(&phy_id) {
            Some(p) => p,
            None => return future::err(zx::Status::INVALID_ARGS).left(),
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
            .right()
    }

    /// Destroys an interface with the given ids.
    pub fn destroy_iface(
        &mut self,
        phy_id: u16,
        iface_id: u16,
    ) -> impl Future<Item = (), Error = zx::Status> {
        let phy = match self.phys.get(&phy_id) {
            Some(p) => p,
            None => return future::err(zx::Status::INVALID_ARGS).left(),
        };
        let mut req = wlan::DestroyIfaceRequest { id: iface_id };
        phy.proxy
            .destroy_iface(&mut req)
            .map_err(|_| zx::Status::IO)
            .and_then(|resp| zx::Status::ok(resp.status).into_future())
            .right()
    }

    /// Adds an `EventListener`. The event methods will be called for each existing object tracked
    /// by this device manager.
    pub fn add_listener(&mut self, listener: Box<EventListener>) {
        if self.phys
            .values()
            .all(|phy| listener.on_phy_added(phy.id).is_ok())
        {
            self.listeners.push(listener);
        }
    }
}

fn new_watcher<P, OnAdd, OnRm>(
    path: P,
    devmgr: DevMgrRef,
    on_add: OnAdd,
    on_rm: OnRm,
) -> impl Future<Item = (), Error = Error>
where
    OnAdd: Fn(DevMgrRef, &Path),
    OnRm: Fn(DevMgrRef, &Path),
    P: AsRef<Path>,
{
    File::open(&path)
        .into_future()
        .err_into()
        .and_then(|dev| vfs_watcher::Watcher::new(&dev).map_err(Into::into))
        .and_then(|watcher| {
            watcher
                .for_each(move |msg| {
                    let full_path = path.as_ref().join(msg.filename);
                    match msg.event {
                        vfs_watcher::WatchEvent::EXISTING | vfs_watcher::WatchEvent::ADD_FILE => {
                            on_add(devmgr.clone(), &full_path);
                        }
                        vfs_watcher::WatchEvent::REMOVE_FILE => {
                            on_rm(devmgr.clone(), &full_path);
                        }
                        vfs_watcher::WatchEvent::IDLE => debug!("device watcher idle"),
                        e => warn!("unknown watch event: {:?}", e),
                    }
                    Ok(())
                })
                .map(|_s| ())
                .err_into()
        })
}

/// Creates a `futures::Stream` that adds phy devices to the `DeviceManager` as they appear at the
/// given path.
pub fn new_phy_watcher<P: AsRef<Path>>(
    path: P,
    devmgr: DevMgrRef,
) -> impl Future<Item = (), Error = Error> {
    new_watcher(
        path,
        devmgr,
        |devmgr, path| {
            info!("found phy at {}", path.to_string_lossy());
            // The path was constructed in the new_watcher closure, so filename should not be
            // empty. The file_name comes from devmgr and is an integer, so from_str should not
            // fail.
            let id = u16::from_str(&path.file_name().unwrap().to_string_lossy()).unwrap();
            // This could fail if the device were to go away in between our receiving the watcher
            // message and here. TODO(tkilbourn): handle this case more cleanly.
            let phy = PhyDevice::new(id, path).expect("Failed to open phy device");
            async::spawn(
                phy.proxy
                    .query()
                    .and_then(move |_resp| {
                        devmgr.lock().add_phy(phy);
                        Ok(())
                    })
                    .recover(|e| eprintln!("Could not query/add wlan phy device: {:?}", e)),
            );
        },
        |devmgr, path| {
            info!("removing phy at {}", path.to_string_lossy());
            // The path was constructed in the new_watcher closure, so filename should not be
            // empty. The file_name comes from devmgr and is an integer, so from_str should not
            // fail.
            let id = u16::from_str(&path.file_name().unwrap().to_string_lossy()).unwrap();
            devmgr.lock().rm_phy(id);
        },
    )
}

/// Creates a `futures::Stream` that adds iface devices to the `DeviceManager` as they appear at
/// the given path.
/// TODO(tkilbourn): add the iface to `DeviceManager`
pub fn new_iface_watcher<P: AsRef<Path>>(
    path: P,
    devmgr: DevMgrRef,
) -> impl Future<Item = (), Error = Error> {
    new_watcher(
        path,
        devmgr,
        |_, path| {
            info!("found iface at {}", path.to_string_lossy());
        },
        |_, path| {
            info!("removing iface at {}", path.to_string_lossy());
        },
    )
}

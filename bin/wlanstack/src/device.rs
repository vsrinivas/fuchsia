// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_vfs_watcher as watcher;
use fuchsia_zircon as zx;
use futures::{self, Async, Future, Stream};
use tokio_core::reactor;
use wlan;
use wlan_dev;

use std::cell::RefCell;
use std::collections::HashMap;
use std::fs::File;
use std::path::Path;
use std::rc::Rc;
use std::str::FromStr;

struct PhyDevice {
    id: u16,
    dev: wlan_dev::WlanPhy,
}

impl PhyDevice {
    fn new<P: AsRef<Path>>(id: u16, path: P) -> Result<Self, Error> {
        Ok(PhyDevice {
            id: id,
            dev: wlan_dev::WlanPhy::new(path)?,
        })
    }
}

/// Called by the `DeviceManager` in response to device events.
pub trait EventListener {
    /// Called when a phy device is added. On error, the listener is removed from the
    /// `DeviceManager`.
    fn on_phy_added(&self, id: u16) -> Result<(), Error>;

    /// Called when a phy device is removed. On error, the listener is removed from the
    /// `DeviceManager`.
    fn on_phy_removed(&self, id: u16) -> Result<(), Error>;
}

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
    pub fn list_phys(&self) -> Vec<wlan::WlanPhyInfo> {
        self.phys
            .values()
            .filter_map(|phy| {
                // TODO(tkilbourn): use the cached value of the query once it's available
                phy.dev.query().ok().map(|mut info| {
                    info.id = phy.id;
                    info
                })
            })
            .collect()
    }

    /// Creates an interface on the phy with the given id.
    pub fn create_iface(&mut self, phy_id: u16, role: wlan::MacRole) -> Result<u16, Error> {
        let phy = self.phys.get(&phy_id).ok_or(zx::Status::INVALID_ARGS)?;
        let iface_info = phy.dev.create_iface(role)?;
        Ok(iface_info.id)
    }

    /// Destroys an interface with the given ids.
    pub fn destroy_iface(&mut self, phy_id: u16, iface_id: u16) -> Result<(), Error> {
        let phy = self.phys.get(&phy_id).ok_or(zx::Status::INVALID_ARGS)?;
        phy.dev.destroy_iface(iface_id).map_err(|e| e.into())
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

struct PhyQuery {
    devmgr: Rc<RefCell<DeviceManager>>,
    phy: Option<PhyDevice>,
}

impl Future for PhyQuery {
    type Item = ();
    type Error = ();

    fn poll(&mut self) -> futures::Poll<Self::Item, Self::Error> {
        // TODO(tkilbourn): make this async once queries are async.
        // TODO(tkilbourn): store the result in the PhyDevice once we can clone FIDL structs.
        let phy = self.phy.take().expect("PhyQuery polled after completion");
        let info = phy.dev
            .query()
            .map(|mut info| {
                // We add the id to the device's info.
                info.id = phy.id;
                info
            })
            .map_err(|e| eprintln!("Could not query wlan phy device: {:?}", e))?;
        info!("queried phy: {:?}", info);
        self.devmgr.borrow_mut().add_phy(phy);
        Ok(Async::Ready(()))
    }
}

fn new_watcher<P, OnAdd, OnRm>(
    path: P,
    devmgr: Rc<RefCell<DeviceManager>>,
    handle: &reactor::Handle,
    on_add: OnAdd,
    on_rm: OnRm,
) -> Result<impl Future<Item = (), Error = Error>, Error>
where
    OnAdd: Fn(Rc<RefCell<DeviceManager>>, &Path, &reactor::Handle),
    OnRm: Fn(Rc<RefCell<DeviceManager>>, &Path, &reactor::Handle),
    P: AsRef<Path>,
{
    let dev = File::open(&path)?;
    let watcher = watcher::Watcher::new(&dev, handle)?;
    let handle = handle.clone();
    Ok(watcher
        .for_each(move |msg| {
            let full_path = path.as_ref().join(msg.filename);
            match msg.event {
                watcher::WatchEvent::EXISTING | watcher::WatchEvent::ADD_FILE => {
                    on_add(devmgr.clone(), &full_path, &handle);
                }
                watcher::WatchEvent::REMOVE_FILE => {
                    on_rm(devmgr.clone(), &full_path, &handle);
                }
                watcher::WatchEvent::IDLE => debug!("device watcher idle"),
                e => warn!("unknown watch event: {:?}", e),
            }
            Ok(())
        })
        .map_err(|e| e.into()))
}

/// Creates a `futures::Stream` that adds phy devices to the `DeviceManager` as they appear at the
/// given path.
pub fn new_phy_watcher<P: AsRef<Path>>(
    path: P,
    devmgr: Rc<RefCell<DeviceManager>>,
    handle: &reactor::Handle,
) -> Result<impl Future<Item = (), Error = Error>, Error> {
    new_watcher(
        path,
        devmgr,
        handle,
        |devmgr, path, handle| {
            info!("found phy at {}", path.to_string_lossy());
            // The path was constructed in the new_watcher closure, so filename should not be
            // empty. The file_name comes from devmgr and is an integer, so from_str should not
            // fail.
            let id = u16::from_str(&path.file_name().unwrap().to_string_lossy()).unwrap();
            // This could fail if the device were to go away in between our receiving the watcher
            // message and here. TODO(tkilbourn): handle this case more cleanly.
            let phy = PhyDevice::new(id, path).expect("Failed to open phy device");
            handle.spawn(PhyQuery {
                devmgr: devmgr.clone(),
                phy: Some(phy),
            });
        },
        |devmgr, path, _| {
            info!("removing phy at {}", path.to_string_lossy());
            // The path was constructed in the new_watcher closure, so filename should not be
            // empty. The file_name comes from devmgr and is an integer, so from_str should not
            // fail.
            let id = u16::from_str(&path.file_name().unwrap().to_string_lossy()).unwrap();
            devmgr.borrow_mut().rm_phy(id);
        },
    )
}

/// Creates a `futures::Stream` that adds iface devices to the `DeviceManager` as they appear at
/// the given path.
/// TODO(tkilbourn): add the iface to `DeviceManager`
pub fn new_iface_watcher<P: AsRef<Path>>(
    path: P,
    devmgr: Rc<RefCell<DeviceManager>>,
    handle: &reactor::Handle,
) -> Result<impl Future<Item = (), Error = Error>, Error> {
    new_watcher(
        path,
        devmgr,
        handle,
        |_, path, _| {
            info!("found iface at {}", path.to_string_lossy());
        },
        |_, path, _| {
            info!("removing iface at {}", path.to_string_lossy());
        },
    )
}

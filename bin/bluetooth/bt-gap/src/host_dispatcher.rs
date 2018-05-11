// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use adapter::{self, HostDevice};
use async;
use bt;
use bt::util::clone_adapter_info;
use failure::Error;
use fidl;
use fidl::endpoints2::{Proxy, ServerEnd};
use fidl_bluetooth;
use fidl_bluetooth_control::{ControlControlHandle, PairingDelegateProxy};
use fidl_bluetooth_control::AdapterInfo;
use fidl_bluetooth_host::{AdapterMarker, AdapterProxy, HostProxy};
use futures::{Poll, task, Async, Future, FutureExt};
use futures::IntoFuture;
use futures::StreamExt;
use futures::future::Either::{Left, Right};
use futures::future::ok as fok;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::path::PathBuf;
use std::sync::{Arc, Weak};
use util;
use vfs_watcher;
use zx;

static BT_HOST_DIR: &'static str = "/dev/class/bt-host";
static DEFAULT_NAME: &'static str = "fuchsia";

type HostAdapterPtr = ServerEnd<AdapterMarker>;

pub struct DiscoveryRequestToken {
    adap: Weak<RwLock<HostDevice>>,
}

impl Drop for DiscoveryRequestToken {
    fn drop(&mut self) {
        if let Some(host) = self.adap.upgrade() {
            let mut host = host.write();
            host.stop_discovery();
        }
    }
}

pub struct DiscoverableRequestToken {
    adap: Weak<RwLock<HostDevice>>,
}

impl Drop for DiscoverableRequestToken {
    fn drop(&mut self) {
        if let Some(host) = self.adap.upgrade() {
            let mut host = host.write();
            host.set_discoverable(false);
        }
    }
}

pub struct HostDispatcher {
    active_id: Option<String>,
    host_devices: HashMap<String, Arc<RwLock<HostDevice>>>,
    name: String,
    discovery: Option<Weak<DiscoveryRequestToken>>,
    discoverable: Option<Weak<DiscoverableRequestToken>>,

    pub pairing_delegate: Option<PairingDelegateProxy>,
    pub events: Option<ControlControlHandle>,
}

impl HostDispatcher {
    pub fn new() -> HostDispatcher {
        HostDispatcher {
            active_id: None,
            host_devices: HashMap::new(),
            name: DEFAULT_NAME.to_string(),
            discovery: None,
            discoverable: None,
            pairing_delegate: None,
            events: None,
        }
    }

    pub fn set_name(
        &mut self, name: Option<String>
    ) -> impl Future<Item = fidl_bluetooth::Status, Error = fidl::Error> {
        match name {
            Some(name) => self.name = name,
            None => self.name = DEFAULT_NAME.to_string(),
        };
        match self.get_active_id() {
            Some(ref id) => {
                let adap = self.host_devices.get(id).unwrap();
                let adap = adap.write();
                Left(adap.set_name(self.name.clone()))
            }
            None => Right(fok(bt_fidl_status!(
                BluetoothNotAvailable,
                "No Adapter found"
            ))),
        }
    }

    /// Return the active id. If the ID is current not set,
    /// this fn will make the first ID in it's host_devices active
    fn get_active_id(&mut self) -> Option<String> {
        if self.active_id.is_none() {
            for key in self.host_devices.keys() {
                self.active_id = Some(key.clone());
                break;
            }
        }
        self.active_id.clone()
    }

    pub fn start_discovery(
        hd: &Arc<RwLock<HostDispatcher>>
    ) -> impl Future<
        Item = (fidl_bluetooth::Status, Option<Arc<DiscoveryRequestToken>>),
        Error = fidl::Error,
    > {
        let strong_current_token = match hd.read().discovery {
            Some(ref token) => token.upgrade(),
            None => None,
        };
        if let Some(token) = strong_current_token {
            return Left(fok((bt_fidl_status!(), Some(Arc::clone(&token)))));
        }
        let id = hd.write().get_active_id();
        match id {
            Some(ref id) => {
                let hdr = hd.read();
                let adap = hdr.host_devices.get(id).unwrap();
                let weak_adap = Arc::downgrade(&adap);
                let hdref = Arc::clone(&hd);
                let mut adap = adap.write();
                Right(
                    adap.start_discovery()
                        .and_then(move |resp| match resp.error {
                            Some(_) => fok((resp, None)),
                            None => {
                                let token = Arc::new(DiscoveryRequestToken { adap: weak_adap });
                                hdref.write().discovery = Some(Arc::downgrade(&token));
                                fok((resp, Some(token)))
                            }
                        }),
                )
            }
            None => Left(fok((
                bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
                None,
            ))),
        }
    }

    pub fn set_discoverable(
        hd: &Arc<RwLock<HostDispatcher>>
    ) -> impl Future<
        Item = (
            fidl_bluetooth::Status,
            Option<Arc<DiscoverableRequestToken>>,
        ),
        Error = fidl::Error,
    > {
        let strong_current_token = match hd.read().discoverable {
            Some(ref token) => token.upgrade(),
            None => None,
        };
        if let Some(token) = strong_current_token {
            return Left(fok((bt_fidl_status!(), Some(Arc::clone(&token)))));
        }

        let id = hd.write().get_active_id();
        match id {
            Some(ref id) => {
                let hdr = hd.read();
                let adap = hdr.host_devices.get(id).unwrap();
                let weak_adap = Arc::downgrade(&adap);
                let hdref = Arc::clone(&hd);
                let mut adap = adap.write();
                Right(
                    adap.set_discoverable(true)
                        .and_then(move |resp| match resp.error {
                            Some(_) => fok((resp, None)),
                            None => {
                                let token = Arc::new(DiscoverableRequestToken { adap: weak_adap });
                                hdref.write().discoverable = Some(Arc::downgrade(&token));
                                fok((resp, Some(token)))
                            }
                        }),
                )
            }
            None => Left(fok((
                bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
                None,
            ))),
        }
    }

    pub fn set_active_adapter(&mut self, adapter_id: String) -> fidl_bluetooth::Status {
        if self.host_devices.contains_key(&adapter_id) {
            // Close the prior adapter
            if let Some(ref id) = self.active_id {
                let _ = self.host_devices[id].write().close();
            }
            self.active_id = Some(adapter_id);
            return bt_fidl_status!();
        }

        bt_fidl_status!(BadState, "Attempting to activate an unknown adapter")
    }

    pub fn get_active_adapter_info(&mut self) -> Option<AdapterInfo> {
        match self.get_active_id() {
            Some(ref id) => {
                // Id must always be valid
                let adap = self.host_devices.get(id).unwrap().read();
                Some(util::clone_adapter_info(adap.get_info()))
            }
            None => None,
        }
    }

    pub fn get_active_adapter(hd: Arc<RwLock<HostDispatcher>>) -> impl Future<Item = Option<Arc<RwLock<HostDevice>>>, Error = fidl::Error> {
        OnAdaptersFound(hd.clone()).and_then(move |_adapters| {
            let mut whd = hd.write();
            fok(match whd.get_active_id() {
                Some(id) => Some(hd.read().host_devices.get(&id).unwrap().clone()),
                None => None,
            })
        })
    }

    pub fn get_adapters(hd: &mut Arc<RwLock<HostDispatcher>>) -> impl Future<Item = Vec<AdapterInfo>, Error = fidl::Error> {
        OnAdaptersFound(hd.clone())
    }

}

/// A future that completes when at least one adapter is available.
#[must_use = "futures do nothing unless polled"]
pub struct OnAdaptersFound(Arc<RwLock<HostDispatcher>>);

impl Future for OnAdaptersFound {
    type Item = Vec<AdapterInfo>;
    type Error = fidl::Error;
    fn poll(&mut self, _: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        if self.0.read().host_devices.len() < 1 {
            return Ok(Async::Pending);
        }
        let mut host_devices = vec![];
        for adapter in self.0.read().host_devices.values() {
            let adapter = adapter.read();
            host_devices.push(util::clone_adapter_info(adapter.get_info()));
        }
        Ok(Async::Ready(host_devices))
    }
}


/// Adds an adapter to the host dispatcher. Called by the watch_hosts device
/// watcher
fn add_adapter(
    hd: Arc<RwLock<HostDispatcher>>, host_path: PathBuf
) -> impl Future<Item = (), Error = Error> {
    trace!("Adding Adapter: {:?}", host_path);
    File::open(host_path)
        .into_future()
        .err_into()
        .and_then(|host| {
            let handle = bt::host::open_host_channel(&host).unwrap();
            let host = HostProxy::new(async::Channel::from_channel(handle.into()).unwrap());
            host.get_info()
                .map_err(|e| e.into())
                .map(|info| (host, info))
        })
        .and_then(move |(host, adapter_info)| {
            // Setup the delegates for proxying calls through the bt-host
            let (host_local, host_remote) = zx::Channel::create().unwrap();
            let host_adapter =
                AdapterProxy::from_channel(async::Channel::from_channel(host_local).unwrap());
            let host_req = HostAdapterPtr::new(host_remote);
            let _ = host.request_adapter(host_req);

            // Set the adapter as connectable
            host_adapter
                .set_connectable(true)
                .map_err(|e| {
                    error!("Failed to set host adapter as connectable");
                    e.into()
                })
                .map(|_| (host_adapter, host, adapter_info))
        })
        .and_then(move |(host_adapter, host, adapter_info)| {
            // Add to the adapters
            let id = adapter_info.identifier.clone();
            let adapter = Arc::new(RwLock::new(HostDevice::new(
                host,
                host_adapter,
                adapter_info,
            )));
            hd.write().host_devices.insert(id, adapter.clone());
            fok((hd.clone(), adapter))
        })
        .and_then(|(hd, adapter)| {
            if let Some(ref events) = hd.read().events {
                let _res = events
                    .send_on_adapter_updated(&mut clone_adapter_info(adapter.read().get_info()));
            }
            info!("Host added: {:?}", adapter.read().get_info().identifier);
            adapter::run_host_device(hd.clone(), adapter.clone())
                .err_into()
                .map(|_| ())
        })
}

pub fn rm_adapter(
    _hd: Arc<RwLock<HostDispatcher>>, host_path: PathBuf
) -> impl Future<Item = (), Error = Error> {
    info!("Host removed: {:?}", host_path);
    // TODO:(NET-852)
    fok(())
}

pub fn watch_hosts(hd: Arc<RwLock<HostDispatcher>>) -> impl Future<Item = (), Error = Error> {
    File::open(&BT_HOST_DIR)
        .into_future()
        .err_into()
        .and_then(|dev| vfs_watcher::Watcher::new(&dev).map_err(Into::into))
        .and_then(|watcher| {
            watcher
                .for_each(move |msg| {
                    let path = PathBuf::from(format!(
                        "{}/{}",
                        BT_HOST_DIR,
                        msg.filename.to_string_lossy()
                    ));

                    match msg.event {
                        vfs_watcher::WatchEvent::EXISTING | vfs_watcher::WatchEvent::ADD_FILE => {
                            info!("Adding device from {:?}", path);
                            Left(Left(add_adapter(hd.clone(), path).map_err(|e| {
                                io::Error::new(io::ErrorKind::Other, e.to_string())
                            })))
                        }
                        vfs_watcher::WatchEvent::REMOVE_FILE => {
                            info!("Removing device from {:?}", path);
                            Left(Right(rm_adapter(hd.clone(), path).map_err(|e| {
                                io::Error::new(io::ErrorKind::Other, e.to_string())
                            })))
                        }
                        vfs_watcher::WatchEvent::IDLE => {
                            debug!("HostDispatcher is IDLE");
                            Right(fok(()))
                        }
                        e => {
                            warn!("Unrecognized host watch event: {:?}", e);
                            Right(fok(()))
                        }
                    }
                })
                .map(|_s| ())
                .err_into()
        })
}

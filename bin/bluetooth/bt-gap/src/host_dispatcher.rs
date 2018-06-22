// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use async::TimeoutExt;
use bt;
use bt::util::clone_host_info;
use failure::Error;
use fidl;
use fidl::encoding2::OutOfLine;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_control::{ControlControlHandle, PairingDelegateProxy};
use fidl_fuchsia_bluetooth_control::AdapterInfo;
use fidl_fuchsia_bluetooth_host::HostProxy;
use futures::{Poll, task, Async, Future, FutureExt};
use futures::IntoFuture;
use futures::StreamExt;
use futures::future::Either::{Left, Right};
use futures::future::ok as fok;
use host_device::{self, HostDevice};
use parking_lot::RwLock;
use slab::Slab;
use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::path::PathBuf;
use std::sync::{Arc, Weak};
use util;
use vfs_watcher;
use zx::Duration;

pub static HOST_INIT_TIMEOUT: u64 = 5; // Seconds

static BT_HOST_DIR: &'static str = "/dev/class/bt-host";
static DEFAULT_NAME: &'static str = "fuchsia";

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
    host_devices: HashMap<String, Arc<RwLock<HostDevice>>>,
    active_id: Option<String>,

    // GAP state
    name: String,
    discovery: Option<Weak<DiscoveryRequestToken>>,
    discoverable: Option<Weak<DiscoverableRequestToken>>,

    pub pairing_delegate: Option<PairingDelegateProxy>,
    pub event_listeners: Vec<ControlControlHandle>,

    // Pending requests to obtain a Host.
    host_requests: Slab<task::Waker>,
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
            event_listeners: vec![],
            host_requests: Slab::new(),
        }
    }

    pub fn set_name(hd: Arc<RwLock<HostDispatcher>>, name: Option<String>)
        -> impl Future<Item = fidl_fuchsia_bluetooth::Status, Error = fidl::Error> {
        hd.write().name = match name {
            Some(name) => name,
            None => DEFAULT_NAME.to_string(),
        };
        HostDispatcher::get_active_adapter(hd.clone()).and_then(move |adapter| match adapter {
            Some(adapter) => Left(adapter.write().set_name(hd.read().name.clone())),
            None => Right(fok(
                bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
            )),
        })
    }

    /// Return the active id. If the ID is current not set,
    /// this fn will make the first ID in it's host_devices active
    fn get_active_id(&mut self) -> Option<String> {
        match self.active_id {
            None => {
                let id = match self.host_devices.keys().next() {
                    None => {
                        return None;
                    }
                    Some(id) => id.clone(),
                };
                self.set_active_id(Some(id));
                self.active_id.clone()
            }
            ref id => id.clone(),
        }
    }

    pub fn start_discovery(hd: Arc<RwLock<HostDispatcher>>)
        -> impl Future<
            Item = (fidl_fuchsia_bluetooth::Status, Option<Arc<DiscoveryRequestToken>>),
            Error = fidl::Error,
        > {
        let strong_current_token = match hd.read().discovery {
            Some(ref token) => token.upgrade(),
            None => None,
        };
        if let Some(token) = strong_current_token {
            return Left(fok((bt_fidl_status!(), Some(Arc::clone(&token)))));
        }

        Right(HostDispatcher::get_active_adapter(hd.clone()).and_then(
            move |adapter| match adapter {
                Some(adapter) => {
                    let weak_adapter = Arc::downgrade(&adapter);
                    Right(adapter.write().start_discovery().and_then(move |resp| {
                        match resp.error {
                            Some(_) => fok((resp, None)),
                            None => {
                                let token = Arc::new(DiscoveryRequestToken { adap: weak_adapter });
                                hd.write().discovery = Some(Arc::downgrade(&token));
                                fok((resp, Some(token)))
                            }
                        }
                    }))
                }
                None => Left(fok((
                    bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
                    None,
                ))),
            },
        ))
    }

    pub fn set_discoverable(hd: Arc<RwLock<HostDispatcher>>)
        -> impl Future<
            Item = (fidl_fuchsia_bluetooth::Status, Option<Arc<DiscoverableRequestToken>>),
            Error = fidl::Error,
        > {
        let strong_current_token = match hd.read().discoverable {
            Some(ref token) => token.upgrade(),
            None => None,
        };
        if let Some(token) = strong_current_token {
            return Left(fok((bt_fidl_status!(), Some(Arc::clone(&token)))));
        }

        Right(HostDispatcher::get_active_adapter(hd.clone()).and_then(
            move |adapter| match adapter {
                Some(adapter) => {
                    let weak_adapter = Arc::downgrade(&adapter);
                    Right(adapter.write().set_discoverable(true).and_then(
                        move |resp| {
                            match resp.error {
                                Some(_) => fok((resp, None)),
                                None => {
                                    let token =
                                        Arc::new(DiscoverableRequestToken { adap: weak_adapter });
                                    hd.write().discoverable = Some(Arc::downgrade(&token));
                                    fok((resp, Some(token)))
                                }
                            }
                        },
                    ))
                }
                None => Left(fok((
                    bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
                    None,
                ))),
            },
        ))
    }

    pub fn set_active_adapter(&mut self, adapter_id: String) -> fidl_fuchsia_bluetooth::Status {
        if let Some(ref id) = self.active_id {
            if *id == adapter_id {
                return bt_fidl_status!(Already, "Adapter already active");
            }

            // Shut down the previously active host.
            let _ = self.host_devices[id].write().close();
        }

        if self.host_devices.contains_key(&adapter_id) {
            self.set_active_id(Some(adapter_id));
            bt_fidl_status!()
        } else {
            bt_fidl_status!(NotFound, "Attempting to activate an unknown adapter")
        }
    }

    pub fn get_active_adapter_info(&mut self) -> Option<AdapterInfo> {
        match self.get_active_id() {
            Some(ref id) => {
                // Id must always be valid
                let host = self.host_devices.get(id).unwrap().read();
                Some(util::clone_host_info(host.get_info()))
            }
            None => None,
        }
    }

    pub fn get_active_adapter(hd: Arc<RwLock<HostDispatcher>>)
        -> impl Future<Item = Option<Arc<RwLock<HostDevice>>>, Error = fidl::Error> {
        OnAdaptersFound::new(hd.clone()).and_then(|hd| {
            let mut hd = hd.write();
            fok(match hd.get_active_id() {
                Some(ref id) => Some(hd.host_devices.get(id).unwrap().clone()),
                None => None,
            })
        })
    }

    pub fn get_adapters(hd: &mut Arc<RwLock<HostDispatcher>>)
        -> impl Future<Item = Vec<AdapterInfo>, Error = fidl::Error> {
        OnAdaptersFound::new(hd.clone()).and_then(|hd| {
            let mut result = vec![];
            for host in hd.read().host_devices.values() {
                let host = host.read();
                result.push(util::clone_host_info(host.get_info()));
            }
            fok(result)
        })
    }

    // Resolves all pending OnAdapterFuture's. Called when we leave the init period (by seeing the
    // first host device or when the init timer expires).
    fn resolve_host_requests(&mut self) {
        for waker in &self.host_requests {
            waker.1.wake();
        }
    }

    fn add_host(&mut self, id: String, host: Arc<RwLock<HostDevice>>) {
        self.host_devices.insert(id, host);
    }

    // Updates the active adapter and sends a FIDL event.
    fn set_active_id(&mut self, id: Option<String>) {
        info!("New active adapter: {:?}", id);
        self.active_id = id;
        if let Some(ref mut adapter_info) = self.get_active_adapter_info() {
            for events in self.event_listeners.iter() {
                let _res = events.send_on_active_adapter_changed(Some(OutOfLine(adapter_info)));
            }
        }
    }
}

/// A future that completes when at least one adapter is available.
#[must_use = "futures do nothing unless polled"]
struct OnAdaptersFound {
    hd: Arc<RwLock<HostDispatcher>>,
    waker_key: Option<usize>,
}

impl OnAdaptersFound {
    // Constructs an OnAdaptersFound that completes at the latest after HOST_INIT_TIMEOUT seconds.
    fn new(hd: Arc<RwLock<HostDispatcher>>)
        -> impl Future<Item = Arc<RwLock<HostDispatcher>>, Error = fidl::Error> {
        OnAdaptersFound {
            hd: hd.clone(),
            waker_key: None,
        }.on_timeout(
            Duration::from_seconds(HOST_INIT_TIMEOUT).after_now(),
            move || {
                {
                    let mut hd = hd.write();
                    if hd.host_devices.len() == 0 {
                        info!("No bt-host devices found");
                        hd.resolve_host_requests();
                    }
                }
                Ok(hd)
            },
        )
            .unwrap()
            .err_into()
    }

    fn remove_waker(&mut self) {
        if let Some(key) = self.waker_key {
            self.hd.write().host_requests.remove(key);
        }
        self.waker_key = None;
    }
}

impl Drop for OnAdaptersFound {
    fn drop(&mut self) {
        self.remove_waker()
    }
}

impl Future for OnAdaptersFound {
    type Item = Arc<RwLock<HostDispatcher>>;
    type Error = fidl::Error;

    fn poll(&mut self, ctx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        if self.hd.read().host_devices.len() == 0 {
            if self.waker_key.is_none() {
                self.waker_key = Some(self.hd.write().host_requests.insert(ctx.waker().clone()));
            }
            Ok(Async::Pending)
        } else {
            self.remove_waker();
            Ok(Async::Ready(self.hd.clone()))
        }
    }
}


/// Adds an adapter to the host dispatcher. Called by the watch_hosts device
/// watcher
fn add_adapter(hd: Arc<RwLock<HostDispatcher>>, host_path: PathBuf)
    -> impl Future<Item = (), Error = Error> {
    info!("Adding Adapter: {:?}", host_path);
    File::open(host_path.clone())
        .into_future()
        .err_into()
        .and_then(move |host| {
            let handle = bt::host::open_host_channel(&host).unwrap();
            let host = HostProxy::new(async::Channel::from_channel(handle.into()).unwrap());
            host.get_info().map_err(|e| e.into()).map(|info| {
                (host, info, host_path)
            })
        })
        .and_then(move |(host, adapter_info, path)| {
            // Set the adapter as connectable
            host.set_connectable(true)
                .map_err(|e| {
                    error!("Failed to set host adapter as connectable");
                    e.into()
                })
                .map(|_| (host, adapter_info, path))
        })
        .and_then(move |(host, adapter_info, path)| {
            // Add to the adapters
            let id = adapter_info.identifier.clone();
            let host_device = Arc::new(RwLock::new(HostDevice::new(path, host, adapter_info)));
            hd.write().add_host(id, host_device.clone());
            fok((hd.clone(), host_device))
        })
        .and_then(|(hd, host_device)| {
            for listener in hd.read().event_listeners.iter() {
                let _res = listener.send_on_adapter_updated(
                    &mut clone_host_info(host_device.read().get_info()),
                );
            }
            info!("Host added: {:?}", host_device.read().get_info().identifier);
            hd.write().resolve_host_requests();
            host_device::run(hd.clone(), host_device.clone())
                .err_into()
                .map(|_| ())
        })
}

pub fn rm_adapter(hd: Arc<RwLock<HostDispatcher>>, host_path: PathBuf)
    -> impl Future<Item = (), Error = Error> {
    info!("Host removed: {:?}", host_path);

    let mut hd = hd.write();
    let active_id = hd.active_id.clone();

    // Get the host IDs that match |host_path|.
    let ids: Vec<String> = hd.host_devices
        .iter()
        .filter(|(_, ref host)| host.read().path == host_path)
        .map(|(k, _)| k.clone())
        .collect();
    for id in &ids {
        hd.host_devices.remove(id);
    }

    // Reset the active ID if it got removed.
    if let Some(active_id) = active_id {
        if ids.contains(&active_id) {
            hd.active_id = None;
        }
    }

    // Try to assign a new active adapter. This may send an "OnActiveAdapterChanged" event.
    if hd.active_id.is_none() {
        let _ = hd.get_active_id();
    }

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
                        vfs_watcher::WatchEvent::EXISTING |
                        vfs_watcher::WatchEvent::ADD_FILE => {
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

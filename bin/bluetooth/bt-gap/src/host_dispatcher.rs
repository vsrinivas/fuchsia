// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use adapter::{self, HostDevice};
use async;
use bt;
use failure::Error;
use fidl;
use fidl::endpoints2::{ClientEnd, ServerEnd};
use fidl::endpoints2::Proxy;
use fidl_bluetooth;
use fidl_bluetooth_control::{AdapterInfo, RemoteDeviceDelegateMarker};
use fidl_bluetooth_control::{ControlDelegateProxy, PairingDelegateProxy, RemoteDeviceDelegateProxy};
use fidl_bluetooth_host::{AdapterDelegate, AdapterDelegateMarker, AdapterMarker, AdapterProxy,
                          HostProxy};
use futures::{Future, FutureExt};
use futures::IntoFuture;
use futures::StreamExt;
use futures::future::Either::{Left, Right};
use futures::future::ok as fok;
use parking_lot::Mutex;
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
type AdapterDelegatePtr = ServerEnd<AdapterDelegateMarker>;
type RemoteDeviceDelegatePtr = Option<ClientEnd<RemoteDeviceDelegateMarker>>;

pub struct DiscoveryRequestToken {
    adap: Weak<Mutex<HostDevice>>,
}

impl Drop for DiscoveryRequestToken {
    fn drop(&mut self) {
        if let Some(host) = self.adap.upgrade() {
            let mut host = host.lock();
            host.stop_discovery();
        }
    }
}

pub struct DiscoverableRequestToken {
    adap: Weak<Mutex<HostDevice>>,
}

impl Drop for DiscoverableRequestToken {
    fn drop(&mut self) {
        if let Some(host) = self.adap.upgrade() {
            let mut host = host.lock();
            host.set_discoverable(false);
        }
    }
}

pub struct HostDispatcher {
    active_id: Option<String>,
    host_devices: HashMap<String, Arc<Mutex<HostDevice>>>,
    name: String,
    discovery: Option<Weak<DiscoveryRequestToken>>,
    discoverable: Option<Weak<DiscoverableRequestToken>>,
    pub remote_device_delegate: Option<RemoteDeviceDelegateProxy>,
    pub control_delegate: Option<ControlDelegateProxy>,
    pub pairing_delegate: Option<PairingDelegateProxy>,
}

impl HostDispatcher {
    pub fn new() -> HostDispatcher {
        HostDispatcher {
            active_id: None,
            host_devices: HashMap::new(),
            name: DEFAULT_NAME.to_string(),
            discovery: None,
            discoverable: None,
            remote_device_delegate: None,
            control_delegate: None,
            pairing_delegate: None,
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
                let mut adap = adap.lock();
                Left(adap.set_name(self.name.clone()))
            }
            None => Right(fok(
                bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
            )),
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
        println!("Starting discovery");
        let strong_current_token = match hd.read().discovery {
            Some(ref token) => token.upgrade(),
            None => None,
        };
        if let Some(token) = strong_current_token {
            return Left(fok((bt_fidl_status!(), Some(Arc::clone(&token)))));
        }

        match hd.write().get_active_id() {
            Some(ref id) => {
                let hdr = hd.read();
                let adap = hdr.host_devices.get(id).unwrap();
                let weak_adap = Arc::downgrade(&adap);
                let hdref = Arc::clone(&hd);
                let mut adap = adap.lock();
                Right(adap.start_discovery().and_then(
                    move |mut resp| match resp.error {
                        Some(_) => fok((resp, None)),
                        None => {
                            let token = Arc::new(DiscoveryRequestToken { adap: weak_adap });
                            hdref.write().discovery = Some(Arc::downgrade(&token));
                            fok((resp, Some(token)))
                        }
                    },
                ))
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

        match hd.write().get_active_id() {
            Some(ref id) => {
                let hdr = hd.read();
                let adap = hdr.host_devices.get(id).unwrap();
                let weak_adap = Arc::downgrade(&adap);
                let hdref = Arc::clone(&hd);
                let mut adap = adap.lock();
                Right(adap.set_discoverable(true).and_then(
                    move |mut resp| match resp.error {
                        Some(_) => fok((resp, None)),
                        None => {
                            let token = Arc::new(DiscoverableRequestToken { adap: weak_adap });
                            hdref.write().discoverable = Some(Arc::downgrade(&token));
                            fok((resp, Some(token)))
                        }
                    },
                ))
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
                self.host_devices[id].lock().close();
            }
            self.active_id = Some(adapter_id);
            return bt_fidl_status!();
        }

        bt_fidl_status!(BadState, "Attempting to activate an unknown adapter")
    }

    pub fn get_active_adapter(&mut self) -> Option<Arc<Mutex<HostDevice>>> {
        match self.get_active_id() {
            Some(id) => Some(self.host_devices.get(&id).unwrap().clone()),
            None => None,
        }
    }

    pub fn get_active_adapter_info(&self) -> Option<AdapterInfo> {
        match self.active_id {
            Some(ref id) => {
                // Id must always be valid
                let adap = self.host_devices.get(id).unwrap().lock();
                Some(util::clone_adapter_info(adap.get_info()))
            }
            None => None,
        }
    }

    pub fn get_adapters(&self) -> Vec<AdapterInfo> {
        // TODO:(bwb) future-ify this for initialization reasons
        // Not ready if zero in self.adapters
        let mut host_devices = vec![];
        for adapter in self.host_devices.values() {
            let adapter = adapter.lock();
            host_devices.push(util::clone_adapter_info(adapter.get_info()));
        }
        host_devices
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
            let mut host_req = HostAdapterPtr::new(host_remote);
            host.request_adapter(&mut host_req);

            let (del_local, del_remote) = zx::Channel::create().unwrap();
            let del_local = async::Channel::from_channel(del_local).unwrap();
            let mut adap_delegate = ClientEnd::<AdapterDelegateMarker>::new(del_remote);
            host_adapter.set_connectable(true).map(move |status| {
                status.error.and_then::<Box<Error>, _>(|e| {
                    info!("Can't set connectable: {:?}", e);
                    None
                })
            });
            host_adapter.set_delegate(&mut adap_delegate);

            // Add to the adapters
            let id = adapter_info.identifier.clone();
            let adapter = Arc::new(Mutex::new(
                HostDevice::new(host, host_adapter, adapter_info),
            ));
            hd.write().host_devices.insert(id, adapter.clone());

            fok((hd.clone(), adapter, del_local))
        })
        .and_then(|(hd, adapter, del_local)| {
            if let Some(ref control_delegate) = hd.read().control_delegate {
                control_delegate
                    .on_adapter_updated(&mut util::clone_adapter_info(adapter.lock().get_info()));
            }
            info!("Host added: {:?}", adapter.lock().get_info().identifier);
            adapter::run_host_device(hd.clone(), adapter.clone())
                .serve(del_local)
                .map_err(|e| e.into())
        })
}

fn rm_adapter(
    _: Arc<RwLock<HostDispatcher>>, host_path: PathBuf
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
                            Left(Left(
                                add_adapter(hd.clone(), path).map_err(
                                    |e| io::Error::new(io::ErrorKind::Other, e.to_string()),
                                ),
                            ))
                        }
                        vfs_watcher::WatchEvent::REMOVE_FILE => Left(Right(
                            rm_adapter(hd.clone(), path)
                                .map_err(|e| io::Error::new(io::ErrorKind::Other, e.to_string())),
                        )),
                        vfs_watcher::WatchEvent::IDLE => {
                            debug!("HostDispatcher is IDLE");
                            Right(fok(()))
                        }
                        e => {
                            debug!("Unrecognized event: {:?}", e);
                            Right(fok(()))
                        }
                    }
                })
                .map(|_s| ())
                .err_into()
        })
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::host_device::{self, HostDevice};
use crate::services;
use crate::store::stash::Stash;
use crate::util;
use failure::Error;
use fidl;
use fidl::encoding::OutOfLine;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_control::{
    AdapterInfo,
    ControlControlHandle,
    PairingDelegateMarker,
    PairingDelegateProxy,
    InputCapabilityType,
    OutputCapabilityType,
    RemoteDevice
};
use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
use fidl_fuchsia_bluetooth_gatt::Server_Marker;
use fidl_fuchsia_bluetooth_host::HostProxy;
use fidl_fuchsia_bluetooth_le::{CentralProxy, CentralMarker, PeripheralMarker};

use fuchsia_bluetooth::{
    self as bt,
    bt_fidl_status,
    error::Error as BTError,
    util::clone_host_info,
    util::clone_remote_device
};
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_syslog::{fx_log, fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_vfs_watcher as vfs_watcher;
use fuchsia_vfs_watcher::{WatchEvent, WatchMessage};
use fuchsia_zircon as zx;
use fuchsia_zircon::Duration;
use futures::TryStreamExt;
use futures::{task::{LocalWaker, Waker}, Future, Poll, TryFutureExt};
use parking_lot::RwLock;
use slab::Slab;
use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::marker::Unpin;
use std::path::PathBuf;
use std::sync::{Arc, Weak};

pub static HOST_INIT_TIMEOUT: i64 = 5; // Seconds

static BT_HOST_DIR: &'static str = "/dev/class/bt-host";
static DEFAULT_NAME: &'static str = "fuchsia";

/// Available FIDL services that can be provided by a particular Host
pub enum HostService {
    LeCentral,
    LePeripheral,
    LeGatt,
    Profile
}

// We use tokens to track the reference counting for discovery/discoverable states
// As long as at least one user maintains an Arc<> to the token, the state persists
// Once all references are dropped, the `Drop` trait on the token causes the state
// to be terminated.
pub struct DiscoveryRequestToken {
    adap: Weak<RwLock<HostDevice>>,
}

impl Drop for DiscoveryRequestToken {
    fn drop(&mut self) {
        if let Some(host) = self.adap.upgrade() {
            host.write().stop_discovery();
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

type DeviceId = String;

/// The HostDispatcher acts as a proxy aggregating multiple HostAdapters
/// It appears as a Host to higher level systems, and is responsible for
/// routing commands to the appropriate HostAdapter
struct HostDispatcherState {
    host_devices: HashMap<String, Arc<RwLock<HostDevice>>>,
    active_id: Option<String>,

    // Component storage.
    pub stash: Stash,

    // GAP state
    name: String,
    discovery: Option<Weak<DiscoveryRequestToken>>,
    discoverable: Option<Weak<DiscoverableRequestToken>>,
    pub input: InputCapabilityType,
    pub output: OutputCapabilityType,
    remote_devices: HashMap<DeviceId, RemoteDevice>,

    pub pairing_delegate: Option<PairingDelegateProxy>,
    pub event_listeners: Vec<ControlControlHandle>,

    // Pending requests to obtain a Host.
    host_requests: Slab<Waker>,
}

impl HostDispatcherState {
    /// Set the active adapter for this HostDispatcher
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

    /// Used to set the pairing delegate. If there is a prior pairing delegate connected to the
    /// host it will fail. It checks if the existing stored connection is closed, and will
    /// overwrite it if so.
    pub fn set_pairing_delegate(&mut self, delegate: Option<PairingDelegateProxy>) -> bool {
        match delegate {
            Some(delegate) => {
                let assign = match self.pairing_delegate {
                    None => true,
                    Some(ref pd) => pd.is_closed(),
                };
                if assign {
                    self.pairing_delegate = Some(delegate);
                }
                assign
            }
            None => {
                self.pairing_delegate = None;
                false
            }
        }
    }

    /// Returns the current pairing delegate proxy if it exists and has not been closed. Clears the
    /// if the handle is closed.
    pub fn pairing_delegate(&mut self) -> Option<PairingDelegateProxy> {
        if let Some(delegate) = &self.pairing_delegate {
            if delegate.is_closed() {
                self.pairing_delegate = None;
            }
        }
        self.pairing_delegate.clone()
    }

    /// Return the active id. If the ID is currently not set,
    /// it will make the first ID in it's host_devices active
    fn get_active_id(&mut self) -> Option<String> {
        match self.active_id {
            None => match self.host_devices.keys().next() {
                None => None,
                Some(id) => {
                    self.set_active_id(Some(id.clone()));
                    self.active_id.clone()
                }
            },
            ref id => id.clone(),
        }
    }

    /// Return the active host. If the Host is currently not set,
    /// it will make the first ID in it's host_devices active
    fn get_active_host(&mut self) -> Option<Arc<RwLock<HostDevice>>> {
        self.get_active_id()
            .as_ref()
            .and_then(|id| self.host_devices.get(id))
            .map(|host| host.clone())
    }

    /// Resolves all pending OnAdapterFuture's. Called when we leave the init period (by seeing the
    /// first host device or when the init timer expires).
    fn resolve_host_requests(&mut self) {
        for waker in &self.host_requests {
            waker.1.wake();
        }
    }

    fn add_host(&mut self, id: String, host: Arc<RwLock<HostDevice>>) {
        self.host_devices.insert(id, host);
    }

    /// Updates the active adapter and sends a FIDL event.
    fn set_active_id(&mut self, id: Option<String>) {
        fx_log_info!("New active adapter: {:?}", id);
        self.active_id = id;
        if let Some(ref mut adapter_info) = self.get_active_adapter_info() {
            for events in self.event_listeners.iter() {
                let _res = events.send_on_active_adapter_changed(Some(OutOfLine(adapter_info)));
            }
        }
    }

    pub fn get_active_adapter_info(&mut self) -> Option<AdapterInfo> {
        self.get_active_host().map(|host| util::clone_host_info(host.read().get_info()))
    }
}

#[derive(Clone)]
pub struct HostDispatcher {
    state: Arc<RwLock<HostDispatcherState>>,
}

impl HostDispatcher {
  pub fn new(stash: Stash) -> HostDispatcher {
        let hd = HostDispatcherState {
            active_id: None,
            host_devices: HashMap::new(),
            name: DEFAULT_NAME.to_string(),
            input: InputCapabilityType::None,
            output: OutputCapabilityType::None,
            remote_devices: HashMap::new(),
            stash: stash,
            discovery: None,
            discoverable: None,
            pairing_delegate: None,
            event_listeners: vec![],
            host_requests: Slab::new(),
        };
        HostDispatcher {
            state: Arc::new(RwLock::new(hd)),
        }
    }

    pub fn get_active_adapter_info(&mut self) -> Option<AdapterInfo> {
        self.state.write().get_active_adapter_info()
    }

    pub async fn on_adapters_found(&self) -> fidl::Result<HostDispatcher> {
        await!(OnAdaptersFound::new(self.clone()))
    }

    pub async fn set_name(
        &mut self, name: Option<String>,
    ) -> fidl::Result<fidl_fuchsia_bluetooth::Status> {
        self.state.write().name = name.unwrap_or(DEFAULT_NAME.to_string());

        match await!(self.get_active_adapter())? {
            Some(adapter) => await!(adapter.write().set_name(self.state.read().name.clone())),
            None => Ok(bt_fidl_status!(BluetoothNotAvailable, "No Adapter found")),
        }
    }

    /// Set the active adapter for this HostDispatcher
    pub fn set_active_adapter(&mut self, adapter_id: String) -> fidl_fuchsia_bluetooth::Status {
        self.state.write().set_active_adapter(adapter_id)
    }

    pub fn set_pairing_delegate(&mut self, delegate: Option<PairingDelegateProxy>) -> bool {
        self.state.write().set_pairing_delegate(delegate)
    }

    pub async fn start_discovery(
        &mut self,
    ) -> fidl::Result<(
        fidl_fuchsia_bluetooth::Status,
        Option<Arc<DiscoveryRequestToken>>,
    )> {
        let strong_current_token = self
            .state
            .read()
            .discovery
            .as_ref()
            .and_then(|token| token.upgrade());
        if let Some(token) = strong_current_token {
            return Ok((bt_fidl_status!(), Some(Arc::clone(&token))));
        }

        match await!(self.get_active_adapter())? {
            Some(adapter) => {
                let weak_adapter = Arc::downgrade(&adapter);
                let resp = await!(adapter.write().start_discovery())?;
                match resp.error {
                    Some(_) => Ok((resp, None)),
                    None => {
                        let token = Arc::new(DiscoveryRequestToken { adap: weak_adapter });
                        self.state.write().discovery = Some(Arc::downgrade(&token));
                        Ok((resp, Some(token)))
                    }
                }
            }
            None => Ok((
                bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
                None,
            )),
        }
    }

    pub async fn set_discoverable(
        &mut self,
    ) -> fidl::Result<(
        fidl_fuchsia_bluetooth::Status,
        Option<Arc<DiscoverableRequestToken>>,
    )> {
        let strong_current_token = self
            .state
            .read()
            .discoverable
            .as_ref()
            .and_then(|token| token.upgrade());
        if let Some(token) = strong_current_token {
            return Ok((bt_fidl_status!(), Some(Arc::clone(&token))));
        }

        match await!(self.get_active_adapter())? {
            Some(adapter) => {
                let weak_adapter = Arc::downgrade(&adapter);
                let resp = await!(adapter.write().set_discoverable(true))?;
                match resp.error {
                    Some(_) => Ok((resp, None)),
                    None => {
                        let token = Arc::new(DiscoverableRequestToken { adap: weak_adapter });
                        self.state.write().discoverable = Some(Arc::downgrade(&token));
                        Ok((resp, Some(token)))
                    }
                }
            }
            None => Ok((
                bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"),
                None,
            )),
        }
    }

    pub async fn connect_le_central(&mut self) -> fidl::Result<Option<CentralProxy>> {
        let adapter = await!(self.on_adapters_found())?;
        let mut adapter = adapter.state.write();
        match adapter.get_active_host() {
            Some(host) => host.write().connect_le_central().map(|central| Some(central)),
            None => Ok(None),
        }
    }

    pub async fn connect(
        &mut self, device_id: String,
    ) -> fidl::Result<fidl_fuchsia_bluetooth::Status> {
        let central = await!(self.connect_le_central())?;
        let central = match central {
            Some(c) => c,
            None => return Ok(bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"))
        };
        let (service_local, service_remote) = fidl::endpoints::create_proxy().unwrap();
        let connected = await!(central.connect_peripheral(device_id.as_str(), service_remote));
        // TODO(NET-1092): We want this as a host.fidl API
        match await!(self.get_active_adapter())? {
            Some(adapter) => {
                adapter
                    .write()
                    .store_gatt(device_id, central, service_local);
                connected
            }
            None => Ok(bt_fidl_status!(BluetoothNotAvailable, "Adapter went away")),
        }
    }

    pub async fn forget(
        &mut self, _device_id: String
    ) -> fidl::Result<fidl_fuchsia_bluetooth::Status> {
        // TODO(NET-1148): This function should perform the following:
        // 1. Remove the device from bt-gap's in-memory list of devices, once it exists.
        // 2. Remove bonding data from store::Stash.
        // 3. Call Host.Forget(), once it exists.
        Ok(bt_fidl_status!(NotSupported, "Operation not supported"))
    }

    pub async fn disconnect(
        &mut self, device_id: String,
    ) -> fidl::Result<fidl_fuchsia_bluetooth::Status> {
        let adapter = await!(self.get_active_adapter())?;
        match adapter {
            Some(adapter) => await!(adapter.write().rm_gatt(device_id)),
            None => Ok(bt_fidl_status!(BluetoothNotAvailable, "Adapter went away")),
        }
    }

    pub async fn get_active_adapter(&mut self) -> fidl::Result<Option<Arc<RwLock<HostDevice>>>> {
        let adapter = await!(self.on_adapters_found())?;
        let mut wstate = adapter.state.write();
        Ok(wstate.get_active_host())
    }

    pub async fn get_adapters(&mut self) -> fidl::Result<Vec<AdapterInfo>> {
        let _ = await!(self.on_adapters_found());
        let mut result = vec![];
        for host in self.state.read().host_devices.values() {
            let host = host.read();
            result.push(util::clone_host_info(host.get_info()));
        }
        Ok(result)
    }

    pub async fn request_host_service(mut self, chan: fasync::Channel, service: HostService) {
        let adapter = await!(self.get_active_adapter());
        match adapter {
            Ok(Some(adapter)) => {
                let adapter = adapter.read();
                let host = adapter.get_host();
                match service {
                    HostService::LeCentral => {
                        let remote = ServerEnd::<CentralMarker>::new(chan.into());
                        let _ = host.request_low_energy_central(remote);
                    }
                    HostService::LePeripheral => {
                        let remote = ServerEnd::<PeripheralMarker>::new(chan.into());
                        let _ = host.request_low_energy_peripheral(remote);
                    }
                    HostService::LeGatt => {
                        let remote = ServerEnd::<Server_Marker>::new(chan.into());
                        let _ = host.request_gatt_server_(remote);
                    }
                    HostService::Profile => {
                        let remote = ServerEnd::<ProfileMarker>::new(chan.into());
                        let _ = host.request_profile(remote);
                    }
                }
            },
            Ok(None) => eprintln!("Failed to spawn, no active adapter"),
            Err(e) => eprintln!("Failed to spawn, error resolving adapter {:?}", e),
        }
    }

    pub fn set_io_capability(&self, input: InputCapabilityType, output: OutputCapabilityType) {
        let mut state = self.state.write();
        state.input = input;
        state.output = output;
    }

    pub fn add_event_listener(&self, handle: ControlControlHandle) {
        self.state.write().event_listeners.push(handle);
    }

    pub fn event_listeners(&self) -> Vec<ControlControlHandle> {
        self.state.read().event_listeners.clone()
    }

    /// Returns the current pairing delegate proxy if it exists and has not been closed. Clears the
    /// if the handle is closed.
    pub fn pairing_delegate(&self) -> Option<PairingDelegateProxy> {
        self.state.write().pairing_delegate()
    }

    pub fn store_bond(&self, bond_data: fidl_fuchsia_bluetooth_host::BondingData) -> Result<(),Error> {
        self.state.write().stash.store_bond(bond_data)
    }

    pub fn on_device_updated(&self, mut device: RemoteDevice ) {
        // TODO(NET-1297): generic method for this pattern
        for listener in self.event_listeners().iter() {
            let _res = listener
                .send_on_device_updated(&mut device)
                .map_err(|e| fx_log_err!("Failed to send device updated event: {:?}", e));
        }
        let _drop_old_value = self.state.write().remote_devices.insert(device.identifier.clone(), device);
    }

    pub fn on_device_removed( &self, identifier: String ) {
        self.state.write().remote_devices.remove(&identifier);
        for listener in self.event_listeners().iter() {
            let _res = listener
                .send_on_device_removed(&identifier)
                .map_err(|e| fx_log_err!("Failed to send device removed event: {:?}", e));
        }
    }

    pub fn get_remote_devices(&self) -> Vec<RemoteDevice> {
        self.state.read().remote_devices.values().map(|d| clone_remote_device(d)).collect()
    }
}

/// A future that completes when at least one adapter is available.
#[must_use = "futures do nothing unless polled"]
struct OnAdaptersFound {
    hd: HostDispatcher,
    waker_key: Option<usize>,
}

impl OnAdaptersFound {
    // Constructs an OnAdaptersFound that completes at the latest after HOST_INIT_TIMEOUT seconds.
    fn new(hd: HostDispatcher) -> impl Future<Output = fidl::Result<HostDispatcher>> {
        OnAdaptersFound {
            hd: hd.clone(),
            waker_key: None,
        }.on_timeout(
            Duration::from_seconds(HOST_INIT_TIMEOUT).after_now(),
            move || {
                {
                    let mut inner = hd.state.write();
                    if inner.host_devices.len() == 0 {
                        fx_log_info!("No bt-host devices found");
                        inner.resolve_host_requests();
                    }
                }
                Ok(hd)
            },
        )
    }

    fn remove_waker(&mut self) {
        if let Some(key) = self.waker_key {
            self.hd.state.write().host_requests.remove(key);
        }
        self.waker_key = None;
    }
}

impl Drop for OnAdaptersFound {
    fn drop(&mut self) {
        self.remove_waker()
    }
}

impl Unpin for OnAdaptersFound {}

impl Future for OnAdaptersFound {
    type Output = fidl::Result<HostDispatcher>;

    fn poll(mut self: ::std::pin::Pin<&mut Self>, lw: &LocalWaker) -> Poll<Self::Output> {
        if self.hd.state.read().host_devices.len() == 0 {
            let hd = self.hd.clone();
            if self.waker_key.is_none() {
                self.waker_key = Some(hd.state.write().host_requests.insert(lw.clone().into_waker()));
            }
            Poll::Pending
        } else {
            self.remove_waker();
            Poll::Ready(Ok(self.hd.clone()))
        }
    }
}

/// Adds an adapter to the host dispatcher. Called by the watch_hosts device
/// watcher
async fn add_adapter(hd: HostDispatcher, host_path: PathBuf) -> Result<(), Error> {
    fx_log_info!("Adding Adapter: {:?}", host_path);

    // Connect to the host device.
    let host =
        File::open(host_path.clone()).map_err(|_| BTError::new("failed to open bt-host device"))?;
    let handle = bt::host::open_host_channel(&host)?;
    let handle = fasync::Channel::from_channel(handle.into())?;
    let host = HostProxy::new(handle);

    // Obtain basic information and create and entry in the disptacher's map.
    let adapter_info = await!(host.get_info())
        .map_err(|_| BTError::new("failed to obtain bt-host information"))?;
    let id = adapter_info.identifier.clone();
    let address = adapter_info.address.clone();
    let host_device = Arc::new(RwLock::new(HostDevice::new(host_path, host, adapter_info)));

    // Load bonding data that use this host's `address` as their "local identity address".
    if let Some(iter) = hd.state.read().stash.list_bonds(&address) {
        if let Err(e) = await!(
            host_device
                .read()
                .restore_bonds(iter.map(|bd| util::clone_bonding_data(&bd)).collect())
        ) {
            fx_log_err!("failed to restore bonding data for host: {}", e);
            return Err(e.into());
        }
    }

    // TODO(NET-1445): Only the active host should be made connectable and scanning in the
    // background.
    await!(host_device.read().set_connectable(true))
        .map_err(|_| BTError::new("failed to set connectable"))?;
    host_device
        .read()
        .enable_background_scan(true)
        .map_err(|_| BTError::new("failed to enable background scan"))?;

    // Initialize bt-gap as this host's pairing delegate.
    // TODO(NET-1445): Do this only for the active host. This will make sure that non-active hosts
    // always reject pairing.
    let (delegate_local, delegate_remote) = zx::Channel::create()?;
    let delegate_local = fasync::Channel::from_channel(delegate_local)?;
    let delegate_ptr = fidl::endpoints::ClientEnd::<PairingDelegateMarker>::new(delegate_remote);
    host_device
        .read()
        .set_host_pairing_delegate(hd.state.read().input, hd.state.read().output, delegate_ptr);
    fasync::spawn(
        services::start_pairing_delegate(hd.clone(), delegate_local)
            .unwrap_or_else(|e| eprintln!("Failed to spawn {:?}", e)),
    );
    fx_log_info!("Host added: {:?}", host_device.read().get_info().identifier);
    hd.state.write().add_host(id, host_device.clone());

    // Notify Control interface clients about the new device.
    // TODO(armansito): This layering isn't quite right. It's better to do this in
    // HostDispatcher::add_host instead.
    for listener in hd.state.read().event_listeners.iter() {
        let _res =
            listener.send_on_adapter_updated(&mut clone_host_info(host_device.read().get_info()));
    }

    // Resolve pending adapter futures.
    hd.state.write().resolve_host_requests();

    // Start listening to Host interface events.
    await!(host_device::run(hd.clone(), host_device.clone()))
        .map_err(|_| BTError::new("Host interface event stream error").into())
}

pub fn rm_adapter(hd: HostDispatcher, host_path: PathBuf) -> Result<(), Error> {
    fx_log_info!("Host removed: {:?}", host_path);

    let mut hd = hd.state.write();
    let active_id = hd.active_id.clone();

    // Get the host IDs that match `host_path`.
    let ids: Vec<String> = hd
        .host_devices
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

    Ok(())
}

fn bluetooth_device_path(msg: &WatchMessage) -> PathBuf {
    PathBuf::from(format!(
        "{}/{}",
        BT_HOST_DIR,
        msg.filename.to_string_lossy()
    ))
}

pub async fn watch_hosts(hd: HostDispatcher) -> Result<(), Error> {
    let dev = File::open(&BT_HOST_DIR);
    let watcher = vfs_watcher::Watcher::new(&dev.unwrap()).unwrap();
    await!(watcher.try_for_each(|msg| handle_device(hd.clone(), msg))).map_err(|e| e.into())
}

pub async fn handle_device(hd: HostDispatcher, msg: WatchMessage) -> Result<(), io::Error> {
    let path = bluetooth_device_path(&msg);
    match msg.event {
        WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
            fx_log_info!("Adding device from {:?}", path);
            await!(
                add_adapter(hd, path)
                    .map_err(|e| io::Error::new(io::ErrorKind::Other, e.to_string()))
            )
        }
        WatchEvent::REMOVE_FILE => {
            fx_log_info!("Removing device from {:?}", path);
            rm_adapter(hd, path).map_err(|e| io::Error::new(io::ErrorKind::Other, e.to_string()))
        }
        WatchEvent::IDLE => {
            fx_log_info!("HostDispatcher is IDLE");
            Ok(())
        }
        e => {
            fx_log_warn!("Unrecognized host watch event: {:?}", e);
            Ok(())
        }
    }
}

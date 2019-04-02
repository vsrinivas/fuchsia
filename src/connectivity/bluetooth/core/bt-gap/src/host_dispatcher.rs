// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl::{self, encoding::OutOfLine, endpoints::ServerEnd},
    fidl_fuchsia_bluetooth,
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_control::{
        AdapterInfo, ControlControlHandle, InputCapabilityType, OutputCapabilityType,
        PairingDelegateMarker, PairingDelegateProxy, RemoteDevice,
    },
    fidl_fuchsia_bluetooth_gatt::Server_Marker,
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_bluetooth::{
        self as bt, bt_fidl_status, error::Error as BTError, hci, util::clone_host_info,
        util::clone_remote_device,
    },
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_vlog},
    fuchsia_zircon::{self as zx, Duration},
    futures::{task::Waker, Future, FutureExt, Poll, TryFutureExt},
    parking_lot::RwLock,
    slab::Slab,
    std::collections::HashMap,
    std::fs::File,
    std::marker::Unpin,
    std::path::Path,
    std::sync::{Arc, Weak},
};

use crate::{
    host_device::{self, HostDevice},
    services,
    store::stash::Stash,
    util,
};

pub static HOST_INIT_TIMEOUT: i64 = 5; // Seconds

static DEFAULT_NAME: &'static str = "fuchsia";

/// Available FIDL services that can be provided by a particular Host
pub enum HostService {
    LeCentral,
    LePeripheral,
    LeGatt,
    Profile,
}

// We use tokens to track the reference counting for discovery/discoverable states
// As long as at least one user maintains an Arc<> to the token, the state persists
// Once all references are dropped, the `Drop` trait on the token causes the state
// to be terminated.
pub struct DiscoveryRequestToken {
    adap: Weak<RwLock<HostDevice>>,
}

impl Drop for DiscoveryRequestToken {
    #[allow(unused_must_use)] // FIXME(BT-643)
    fn drop(&mut self) {
        fx_vlog!(1, "DiscoveryRequestToken dropped");
        if let Some(host) = self.adap.upgrade() {
            // FIXME(nickpollard) this should be `await!`ed, but not while holding the lock
            host.write().stop_discovery();
        }
    }
}

pub struct DiscoverableRequestToken {
    adap: Weak<RwLock<HostDevice>>,
}

impl Drop for DiscoverableRequestToken {
    #[allow(unused_must_use)] // FIXME(nickpollard)
    fn drop(&mut self) {
        if let Some(host) = self.adap.upgrade() {
            // FIXME(BT-643) this should be `await!`ed, but not while holding the lock
            let host = host.write();
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
    pub event_listeners: Vec<Weak<ControlControlHandle>>,

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
        fx_log_info!("Host added: {:?}", host.read().get_info().identifier);
        let info = clone_host_info(host.read().get_info());
        self.host_devices.insert(id, host);

        // Notify Control interface clients about the new device.
        self.notify_event_listeners(|l| {
            let _res = l.send_on_adapter_updated(&mut clone_host_info(&info));
        });

        // Resolve pending adapter futures.
        self.resolve_host_requests();
    }

    /// Updates the active adapter and sends a FIDL event.
    fn set_active_id(&mut self, id: Option<String>) {
        fx_log_info!("New active adapter: {:?}", id);
        self.active_id = id;
        if let Some(ref mut adapter_info) = self.get_active_adapter_info() {
            self.notify_event_listeners(|listener| {
                let _res = listener.send_on_active_adapter_changed(Some(OutOfLine(adapter_info)));
            })
        }
    }

    pub fn get_active_adapter_info(&mut self) -> Option<AdapterInfo> {
        self.get_active_host().map(|host| clone_host_info(host.read().get_info()))
    }

    pub fn notify_event_listeners<F>(&mut self, mut f: F)
    where
        F: FnMut(&ControlControlHandle) -> (),
    {
        self.event_listeners.retain(|listener| match listener.upgrade() {
            Some(listener_) => {
                f(&listener_);
                true
            }
            None => false,
        })
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
        HostDispatcher { state: Arc::new(RwLock::new(hd)) }
    }

    pub fn get_active_adapter_info(&mut self) -> Option<AdapterInfo> {
        self.state.write().get_active_adapter_info()
    }

    pub async fn on_adapters_found(&self) -> fidl::Result<HostDispatcher> {
        await!(OnAdaptersFound::new(self.clone()))
    }

    pub async fn set_name(
        &mut self,
        name: Option<String>,
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
    ) -> fidl::Result<(fidl_fuchsia_bluetooth::Status, Option<Arc<DiscoveryRequestToken>>)> {
        let strong_current_token =
            self.state.read().discovery.as_ref().and_then(|token| token.upgrade());
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
            None => Ok((bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"), None)),
        }
    }

    pub async fn set_discoverable(
        &mut self,
    ) -> fidl::Result<(fidl_fuchsia_bluetooth::Status, Option<Arc<DiscoverableRequestToken>>)> {
        let strong_current_token =
            self.state.read().discoverable.as_ref().and_then(|token| token.upgrade());
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
            None => Ok((bt_fidl_status!(BluetoothNotAvailable, "No Adapter found"), None)),
        }
    }

    pub async fn forget(
        &mut self,
        _device_id: String,
    ) -> fidl::Result<fidl_fuchsia_bluetooth::Status> {
        // TODO(NET-1148): This function should perform the following:
        // 1. Remove the device from bt-gap's in-memory list of devices, once it exists.
        // 2. Remove bonding data from store::Stash.
        // 3. Call Host.Forget(), once it exists.
        Ok(bt_fidl_status!(NotSupported, "Operation not supported"))
    }

    pub async fn disconnect(
        &mut self,
        device_id: String,
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

    pub async fn get_adapters(&self) -> fidl::Result<Vec<AdapterInfo>> {
        let hosts = self.state.read();
        Ok(hosts
            .host_devices
            .values()
            .map(|host| clone_host_info(host.read().get_info()))
            .collect())
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
            }
            Ok(None) => eprintln!("Failed to spawn, no active adapter"),
            Err(e) => eprintln!("Failed to spawn, error resolving adapter {:?}", e),
        }
    }

    pub fn set_io_capability(&self, input: InputCapabilityType, output: OutputCapabilityType) {
        let mut state = self.state.write();
        state.input = input;
        state.output = output;
    }

    pub fn add_event_listener(&self, handle: Weak<ControlControlHandle>) {
        self.state.write().event_listeners.push(handle);
    }

    pub fn notify_event_listeners<F>(&self, f: F)
    where
        F: FnMut(&ControlControlHandle) -> (),
    {
        self.state.write().notify_event_listeners(f);
    }

    /// Returns the current pairing delegate proxy if it exists and has not been closed. Clears the
    /// if the handle is closed.
    pub fn pairing_delegate(&self) -> Option<PairingDelegateProxy> {
        self.state.write().pairing_delegate()
    }

    pub fn store_bond(
        &self,
        bond_data: fidl_fuchsia_bluetooth_host::BondingData,
    ) -> Result<(), Error> {
        self.state.write().stash.store_bond(bond_data)
    }

    pub fn on_device_updated(&self, mut device: RemoteDevice) {
        // TODO(NET-1297): generic method for this pattern
        self.notify_event_listeners(|listener| {
            let _res = listener
                .send_on_device_updated(&mut device)
                .map_err(|e| fx_log_err!("Failed to send device updated event: {:?}", e));
        });

        let _drop_old_value =
            self.state.write().remote_devices.insert(device.identifier.clone(), device);
    }

    pub fn on_device_removed(&self, identifier: String) {
        self.state.write().remote_devices.remove(&identifier);
        self.notify_event_listeners(|listener| {
            let _res = listener
                .send_on_device_removed(&identifier)
                .map_err(|e| fx_log_err!("Failed to send device removed event: {:?}", e));
        })
    }

    pub fn get_remote_devices(&self) -> Vec<RemoteDevice> {
        self.state.read().remote_devices.values().map(|d| clone_remote_device(d)).collect()
    }

    /// Adds an adapter to the host dispatcher. Called by the watch_hosts device
    /// watcher
    pub async fn add_adapter(self, host_path: &Path) -> Result<(), Error> {
        let host_dev = hci::open_rdwr(host_path)?;
        let device_topo = fdio::device_get_topo_path(&host_dev)?;
        fx_log_info!("Adding Adapter: {:?} (topology: {:?})", host_path, device_topo);
        let host_device = await!(init_host(host_path))?;

        let address = host_device.read().get_info().address.clone();
        await!(try_restore_bonds(host_device.clone(), self.clone(), &address))?;

        // TODO(NET-1445): Only the active host should be made connectable and scanning in the background.
        await!(host_device.read().set_connectable(true))
            .map_err(|_| BTError::new("failed to set connectable"))?;
        host_device
            .read()
            .enable_background_scan(true)
            .map_err(|_| BTError::new("failed to enable background scan"))?;

        // Initialize bt-gap as this host's pairing delegate.
        start_pairing_delegate(self.clone(), host_device.clone())?;

        let id = host_device.read().get_info().identifier.clone();
        self.state.write().add_host(id, host_device.clone());

        // Start listening to Host interface events.
        fasync::spawn(host_device::run(self.clone(), host_device.clone()).map(|_| ()));
        Ok(())
    }

    pub fn rm_adapter(self, host_path: &Path) {
        fx_log_info!("Host removed: {:?}", host_path);

        let mut hd = self.state.write();
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
            hd.notify_event_listeners(|listener| {
                let _ = listener.send_on_adapter_removed(id);
            })
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
    }

    pub async fn connect(
        &mut self,
        device_id: String,
    ) -> fidl::Result<fidl_fuchsia_bluetooth::Status> {
        let adapter = await!(self.get_active_adapter())?;
        match adapter {
            Some(adapter) => await!(adapter.write().connect(device_id)),
            None => Ok(bt_fidl_status!(BluetoothNotAvailable, "Adapter went away")),
        }
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
        OnAdaptersFound { hd: hd.clone(), waker_key: None }.on_timeout(
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

    fn poll(mut self: ::std::pin::Pin<&mut Self>, lw: &Waker) -> Poll<Self::Output> {
        if self.hd.state.read().host_devices.len() == 0 {
            let hd = self.hd.clone();
            if self.waker_key.is_none() {
                self.waker_key = Some(hd.state.write().host_requests.insert(lw.clone()));
            }
            Poll::Pending
        } else {
            self.remove_waker();
            Poll::Ready(Ok(self.hd.clone()))
        }
    }
}

/// Initialize a HostDevice
async fn init_host(path: &Path) -> Result<Arc<RwLock<HostDevice>>, Error> {
    // Connect to the host device.
    let host = File::open(path).map_err(|_| BTError::new("failed to open bt-host device"))?;
    let handle = bt::host::open_host_channel(&host)?;
    let handle = fasync::Channel::from_channel(handle.into())?;
    let host = HostProxy::new(handle);

    // Obtain basic information and create and entry in the disptacher's map.
    let adapter_info = await!(host.get_info())
        .map_err(|_| BTError::new("failed to obtain bt-host information"))?;
    Ok(Arc::new(RwLock::new(HostDevice::new(path.to_path_buf(), host, adapter_info))))
}

async fn try_restore_bonds(
    host_device: Arc<RwLock<HostDevice>>,
    hd: HostDispatcher,
    address: &str,
) -> Result<(), Error> {
    // Load bonding data that use this host's `address` as their "local identity address".
    if let Some(iter) = hd.state.read().stash.list_bonds(address) {
        let res = await!(host_device
            .read()
            .restore_bonds(iter.map(|bd| util::clone_bonding_data(&bd)).collect()));
        match res {
            Ok(_) => Ok(()),
            Err(e) => {
                fx_log_err!("failed to restore bonding data for host: {}", e);
                Err(e.into())
            }
        }
    } else {
        Ok(())
    }
}

fn start_pairing_delegate(
    hd: HostDispatcher,
    host_device: Arc<RwLock<HostDevice>>,
) -> Result<(), Error> {
    // Initialize bt-gap as this host's pairing delegate.
    // TODO(NET-1445): Do this only for the active host. This will make sure that non-active hosts
    // always reject pairing.
    let (delegate_local, delegate_remote) = zx::Channel::create()?;
    let delegate_local = fasync::Channel::from_channel(delegate_local)?;
    let delegate_ptr = fidl::endpoints::ClientEnd::<PairingDelegateMarker>::new(delegate_remote);
    host_device.read().set_host_pairing_delegate(
        hd.state.read().input,
        hd.state.read().output,
        delegate_ptr,
    );
    fasync::spawn(
        services::start_pairing_delegate(hd.clone(), delegate_local)
            .unwrap_or_else(|e| eprintln!("Failed to spawn {:?}", e)),
    );
    Ok(())
}

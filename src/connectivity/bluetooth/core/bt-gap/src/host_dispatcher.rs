// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error, ResultExt},
    fidl::endpoints::{self, ServerEnd},
    fidl_fuchsia_bluetooth::{Appearance, DeviceClass, Error as FidlError, ErrorCode},
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_control::{
        self as control, ControlControlHandle, HostData, InputCapabilityType, LocalKey,
        OutputCapabilityType, PairingDelegateProxy,
    },
    fidl_fuchsia_bluetooth_gatt::{LocalServiceDelegateRequest, Server_Marker, Server_Proxy},
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        self as bt,
        inspect::{DebugExt, Inspectable, ToProperty},
        types::{Address, BondingData, HostInfo, Identity, Peer, PeerId},
    },
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon::{self as zx, Duration},
    futures::{channel::mpsc, FutureExt, TryFutureExt},
    parking_lot::RwLock,
    slab::Slab,
    std::{
        collections::HashMap,
        convert::TryFrom,
        fs::File,
        future::Future,
        marker::Unpin,
        path::Path,
        sync::{Arc, Weak},
        task::{Context, Poll, Waker},
    },
};

use crate::{
    generic_access_service,
    host_device::{self, HostDevice, HostListener},
    services,
    store::stash::Stash,
    types,
};

pub static HOST_INIT_TIMEOUT: i64 = 5; // Seconds

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
            // FIXME(nickpollard) this should be `.await`ed, but not while holding the lock
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
            // FIXME(BT-643) this should be `.await`ed, but not while holding the lock
            let host = host.write();
            host.set_discoverable(false);
        }
    }
}

struct HostDispatcherInspect {
    _inspect: inspect::Node,
    peers: inspect::Node,
    hosts: inspect::Node,
    host_count: inspect::UintProperty,
    device_class: inspect::StringProperty,
    peer_count: inspect::UintProperty,
    input_capability: inspect::StringProperty,
    output_capability: inspect::StringProperty,
    has_pairing_delegate: inspect::UintProperty,
}

impl HostDispatcherInspect {
    pub fn new(inspect: inspect::Node) -> HostDispatcherInspect {
        HostDispatcherInspect {
            host_count: inspect.create_uint("host_count", 0),
            peer_count: inspect.create_uint("peer_count", 0),
            device_class: inspect.create_string("device_class", "default"),
            input_capability: inspect.create_string("input_capability", "unknown"),
            output_capability: inspect.create_string("output_capability", "unknown"),
            has_pairing_delegate: inspect.create_uint("has_pairing_delegate", 0),
            peers: inspect.create_child("peers"),
            hosts: inspect.create_child("hosts"),
            _inspect: inspect,
        }
    }

    pub fn peers(&self) -> &inspect::Node {
        &self.peers
    }

    pub fn hosts(&self) -> &inspect::Node {
        &self.hosts
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
    appearance: Appearance,
    discovery: Option<Weak<DiscoveryRequestToken>>,
    discoverable: Option<Weak<DiscoverableRequestToken>>,
    pub input: InputCapabilityType,
    pub output: OutputCapabilityType,
    peers: HashMap<DeviceId, Inspectable<Peer>>,

    // Sender end of a futures::mpsc channel to send LocalServiceDelegateRequests
    // to Generic Access Service. When a new host adapter is recognized, we create
    // a new GasProxy, which takes GAS requests from the new host and forwards
    // them along a clone of this channel to GAS
    gas_channel_sender: mpsc::Sender<LocalServiceDelegateRequest>,

    pub pairing_delegate: Option<PairingDelegateProxy>,
    pub event_listeners: Vec<Weak<ControlControlHandle>>,

    // Pending requests to obtain a Host.
    host_requests: Slab<Waker>,

    inspect: HostDispatcherInspect,
}

impl HostDispatcherState {
    /// Set the active adapter for this HostDispatcher
    pub fn set_active_adapter(&mut self, adapter_id: String) -> types::Result<()> {
        if let Some(ref id) = self.active_id {
            if *id == adapter_id {
                return Ok(());
            }

            // Shut down the previously active host.
            let _ = self.host_devices[id].write().close();
        }

        if self.host_devices.contains_key(&adapter_id) {
            self.set_active_id(Some(adapter_id));
            Ok(())
        } else {
            Err(types::Error::no_host())
        }
    }

    /// Used to set the pairing delegate. If there is a prior pairing delegate connected to the
    /// host it will fail. It checks if the existing stored connection is closed, and will
    /// overwrite it if so.
    pub fn set_pairing_delegate(&mut self, delegate: Option<PairingDelegateProxy>) -> bool {
        self.inspect.has_pairing_delegate.set(delegate.is_some().to_property());
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
                self.inspect.has_pairing_delegate.set(false.to_property());
                self.pairing_delegate = None;
            }
        }
        self.pairing_delegate.clone()
    }

    /// Set the IO capabilities of the system
    pub fn set_io_capability(&mut self, input: InputCapabilityType, output: OutputCapabilityType) {
        self.input = input;
        self.output = output;
        self.inspect.input_capability.set(&input.debug());
        self.inspect.output_capability.set(&output.debug());
    }

    /// Return the active id. If the ID is currently not set,
    /// it will make the first ID in it's host_devices active
    fn get_active_id(&mut self) -> Option<String> {
        match self.active_id {
            None => match self.host_devices.keys().next() {
                None => None,
                Some(id) => {
                    let id = Some(id.clone());
                    self.set_active_id(id);
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
            waker.1.wake_by_ref();
        }
    }

    fn add_host(&mut self, id: String, host: Arc<RwLock<HostDevice>>) {
        fx_log_info!("Host added: {:?}", host.read().get_info().id);
        self.host_devices.insert(id, host.clone());

        // Update inspect state
        self.inspect.host_count.set(self.host_devices.len() as u64);

        // Notify Control interface clients about the new device.
        self.notify_event_listeners(|l| {
            let _res = l.send_on_adapter_updated(&mut control::AdapterInfo::from(
                host.read().get_info().clone(),
            ));
        });

        // Resolve pending adapter futures.
        self.resolve_host_requests();
    }

    /// Updates the active adapter and notifies listeners
    fn set_active_id(&mut self, id: Option<String>) {
        fx_log_info!("New active adapter: {:?}", id);
        self.active_id = id;
        if let Some(host_info) = self.get_active_host_info() {
            let mut adapter_info = control::AdapterInfo::from(host_info);
            self.notify_event_listeners(|listener| {
                let _res = listener.send_on_active_adapter_changed(Some(&mut adapter_info));
            })
        }
    }

    fn get_active_host_info(&mut self) -> Option<HostInfo> {
        self.get_active_host().map(|host| host.read().get_info().clone())
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
    /// The HostDispatcher will forward all Generic Access Service requests to the mpsc::Receiver
    /// end of |gas_channel_sender|. It is the responsibility of this function's caller to ensure
    /// that these requests are handled. This can be done by passing the mpsc::Receiver into a
    /// GenericAccessService struct and ensuring its run method is scheduled.
    pub fn new(
        name: String,
        appearance: Appearance,
        stash: Stash,
        inspect: inspect::Node,
        gas_channel_sender: mpsc::Sender<LocalServiceDelegateRequest>,
    ) -> HostDispatcher {
        let hd = HostDispatcherState {
            active_id: None,
            host_devices: HashMap::new(),
            name,
            appearance,
            input: InputCapabilityType::None,
            output: OutputCapabilityType::None,
            peers: HashMap::new(),
            gas_channel_sender,
            stash,
            discovery: None,
            discoverable: None,
            pairing_delegate: None,
            event_listeners: vec![],
            host_requests: Slab::new(),
            inspect: HostDispatcherInspect::new(inspect),
        };
        HostDispatcher { state: Arc::new(RwLock::new(hd)) }
    }

    pub fn get_active_host_info(&mut self) -> Option<HostInfo> {
        self.state.write().get_active_host_info()
    }

    pub async fn when_hosts_found(&self) -> HostDispatcher {
        WhenHostsFound::new(self.clone()).await
    }

    pub fn get_name(&self) -> String {
        self.state.read().name.clone()
    }

    pub fn get_appearance(&self) -> Appearance {
        self.state.read().appearance
    }

    pub async fn set_name(&mut self, name: String) -> types::Result<()> {
        self.state.write().name = name;
        match self.get_active_adapter().await {
            Some(adapter) => {
                let fut = adapter.write().set_name(self.state.read().name.clone());
                fut.await
            }
            None => Err(types::Error::no_host()),
        }
    }

    pub async fn set_device_class(&mut self, class: DeviceClass) -> types::Result<()> {
        let class_repr = class.debug();
        let res = match self.get_active_adapter().await {
            Some(adapter) => {
                let fut = adapter.write().set_device_class(class);
                fut.await
            }
            None => Err(types::Error::no_host()),
        };

        // Update Inspect state
        if res.is_ok() {
            self.state.read().inspect.device_class.set(&class_repr);
        }
        res
    }

    /// Set the active adapter for this HostDispatcher
    pub fn set_active_adapter(&mut self, adapter_id: String) -> types::Result<()> {
        self.state.write().set_active_adapter(adapter_id)
    }

    pub fn set_pairing_delegate(&mut self, delegate: Option<PairingDelegateProxy>) -> bool {
        self.state.write().set_pairing_delegate(delegate)
    }

    pub async fn start_discovery(&mut self) -> types::Result<Arc<DiscoveryRequestToken>> {
        let strong_current_token =
            self.state.read().discovery.as_ref().and_then(|token| token.upgrade());
        if let Some(token) = strong_current_token {
            return Ok(Arc::clone(&token));
        }

        match self.get_active_adapter().await {
            Some(host) => {
                let weak_host = Arc::downgrade(&host);
                let fut = host.write().start_discovery();
                fut.await?;
                let token = Arc::new(DiscoveryRequestToken { adap: weak_host });
                self.state.write().discovery = Some(Arc::downgrade(&token));
                Ok(token)
            }
            None => Err(types::Error::no_host()),
        }
    }

    pub async fn set_discoverable(&mut self) -> types::Result<Arc<DiscoverableRequestToken>> {
        let strong_current_token =
            self.state.read().discoverable.as_ref().and_then(|token| token.upgrade());
        if let Some(token) = strong_current_token {
            return Ok(Arc::clone(&token));
        }

        match self.get_active_adapter().await {
            Some(host) => {
                let weak_host = Arc::downgrade(&host);
                let fut = host.write().set_discoverable(true);
                fut.await?;
                let token = Arc::new(DiscoverableRequestToken { adap: weak_host });
                self.state.write().discoverable = Some(Arc::downgrade(&token));
                Ok(token)
            }
            None => Err(types::Error::no_host()),
        }
    }

    fn stash(&self) -> Stash {
        self.state.read().stash.clone()
    }

    pub async fn forget(&mut self, peer_id: PeerId) -> types::Result<()> {
        // Try to delete from each adapter, even if it might not have the peer.
        // peers will be updated by the disconnection(s).
        let adapters = self.get_all_adapters().await;
        if adapters.is_empty() {
            return Err(types::Error::no_host());
        }
        let mut adapters_removed: u32 = 0;
        for adapter in adapters {
            let adapter_path = adapter.read().path.clone();

            let fut = adapter.write().forget(peer_id.to_string());
            match fut.await {
                Ok(()) => adapters_removed += 1,
                Err(types::Error::HostError(FidlError {
                    error_code: ErrorCode::NotFound, ..
                })) => fx_vlog!(1, "No peer {} on adapter {:?}; ignoring", peer_id, adapter_path),
                err => {
                    fx_log_err!("Could not forget peer {} on adapter {:?}", peer_id, adapter_path);
                    return err;
                }
            }
        }

        if let Err(_) = self.stash().rm_peer(peer_id).await {
            return Err(err_msg("Couldn't remove peer").into());
        }

        if adapters_removed == 0 {
            return Err(err_msg("No adapters had peer").into());
        }
        Ok(())
    }

    pub async fn connect(&mut self, peer_id: String) -> types::Result<()> {
        let host = self.get_active_adapter().await;
        match host {
            Some(host) => {
                let fut = host.write().connect(peer_id);
                fut.await
            }
            None => Err(types::Error::no_host()),
        }
    }

    // Attempt to disconnect peer with id `peer_id` from all transports
    pub async fn disconnect(&mut self, peer_id: String) -> types::Result<()> {
        let host = self.get_active_adapter().await;
        match host {
            Some(host) => {
                // Suppress the error from `rm_gatt`, as the peer not having a GATT entry
                // (i.e. not using LE) is not a failure condition
                let fut = host.write().rm_gatt(peer_id.clone());
                let _ = fut.await;
                let fut = host.write().disconnect(peer_id);
                fut.await
            }
            None => Err(types::Error::no_host()),
        }
    }

    pub async fn get_active_adapter(&mut self) -> Option<Arc<RwLock<HostDevice>>> {
        let adapter = self.when_hosts_found().await;
        let mut wstate = adapter.state.write();
        wstate.get_active_host()
    }

    pub async fn get_all_adapters(&self) -> Vec<Arc<RwLock<HostDevice>>> {
        let _ = self.when_hosts_found().await;
        self.state.read().host_devices.values().cloned().collect()
    }

    pub async fn get_adapters(&self) -> Vec<HostInfo> {
        let hosts = self.state.read();
        hosts.host_devices.values().map(|host| host.read().get_info().clone()).collect()
    }

    pub async fn request_host_service(mut self, chan: fasync::Channel, service: HostService) {
        match self.get_active_adapter().await {
            Some(host) => {
                let host = host.read();
                let host = host.get_host();
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
            None => eprintln!("Failed to spawn, no active host"),
        }
    }

    pub fn set_io_capability(&self, input: InputCapabilityType, output: OutputCapabilityType) {
        self.state.write().set_io_capability(input, output);
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

    // This is not an async method as we do not want to borrow `self` for the duration of the async
    // call, and we also want to trigger the send immediately even if the future is not yet awaited
    pub fn store_bond(&self, bond_data: BondingData) -> impl Future<Output = Result<(), Error>> {
        self.stash().store_bond(bond_data)
    }

    pub fn on_device_updated(&self, peer: Peer) {
        // TODO(825): generic method for this pattern
        let mut d = control::RemoteDevice::from(peer.clone());
        self.notify_event_listeners(|listener| {
            let _res = listener
                .send_on_device_updated(&mut d)
                .map_err(|e| fx_log_err!("Failed to send device updated event: {:?}", e));
        });

        let mut state = self.state.write();
        let node = state.inspect.peers().create_child(format!("peer {}", peer.identifier));
        let peer = Inspectable::new(peer, node);
        let _drop_old_value = state.peers.insert(peer.identifier.clone(), peer);
        state.inspect.peer_count.set(state.peers.len() as u64);
    }

    pub fn on_device_removed(&self, identifier: String) {
        {
            let mut state = self.state.write();
            state.peers.remove(&identifier);
            state.inspect.peer_count.set(state.peers.len() as u64)
        }
        self.notify_event_listeners(|listener| {
            let _res = listener
                .send_on_device_removed(&identifier)
                .map_err(|e| fx_log_err!("Failed to send device removed event: {:?}", e));
        })
    }

    pub fn get_peers(&self) -> Vec<Peer> {
        self.state.read().peers.values().map(|p| (*p).clone()).collect()
    }

    async fn spawn_gas_proxy(&self, gatt_server_proxy: Server_Proxy) -> Result<(), Error> {
        let gas_channel = self.state.read().gas_channel_sender.clone();
        let gas_proxy =
            generic_access_service::GasProxy::new(gatt_server_proxy, gas_channel).await?;
        fasync::spawn(gas_proxy.run().map(|r| {
            r.unwrap_or_else(|err| {
                fx_log_warn!("Error passing message through Generic Access proxy: {:?}", err);
            })
        }));
        Ok(())
    }

    /// Commit all bootstrapped bonding identities to the system. This will update both the Stash
    /// and our in memory store, and notify all hosts of new bonding identities. If we already have
    /// bonding data for any of the peers (as identified by address), the new bootstrapped data
    /// will override them.
    pub async fn commit_bootstrap(&self, identities: Vec<Identity>) -> types::Result<()> {
        let mut stash = self.state.read().stash.clone();
        for identity in identities {
            stash.store_bonds(identity.bonds).await?
        }
        // Notify all current hosts of any changes to their bonding data
        let host_devices: Vec<_> = self.state.read().host_devices.values().cloned().collect();

        for host in host_devices {
            let address = host.read().get_info().address;
            try_restore_bonds(host.clone(), self.clone(), &address).await?;
        }
        Ok(())
    }

    /// Adds an adapter to the host dispatcher. Called by the watch_hosts device
    /// watcher
    pub async fn add_adapter(self, host_path: &Path) -> Result<(), Error> {
        let node = self
            .state
            .read()
            .inspect
            .hosts()
            .create_child(format!("device {}", host_path.display()));
        let host_dev = bt::util::open_rdwr(host_path)?;
        let device_topo = fdio::device_get_topo_path(&host_dev)?;
        fx_log_info!("Adding Adapter: {:?} (topology: {:?})", host_path, device_topo);
        let host_device = init_host(host_path, node).await?;

        // TODO(armansito): Make sure that the bt-host device is left in a well-known state if any
        // of these operations fails.

        // TODO(PKG-47): The following code applies a number of configurations to the bt-host by
        // default. We should tie these to a package configuration (once it is possible), as some of these
        // are undesirable in certain situations, e.g when running PTS tests.
        //
        // Currently applied settings:
        //   - LE Privacy with IRK
        //   - LE background scan for auto-connection
        //   - BR/EDR connectable mode

        let address = host_device.read().get_info().address.clone();
        assign_host_data(host_device.clone(), self.clone(), &address).await?;
        try_restore_bonds(host_device.clone(), self.clone(), &address)
            .await
            .map_err(|e| e.as_failure())?;

        // Assign the name that is currently assigned to the HostDispatcher as the local name.
        host_device
            .read()
            .set_name(self.state.read().name.clone())
            .await
            .map_err(|e| e.as_failure())?;

        let (gatt_server_proxy, remote_gatt_server) = fidl::endpoints::create_proxy()?;
        host_device.read().get_host().request_gatt_server_(remote_gatt_server)?;
        self.spawn_gas_proxy(gatt_server_proxy).await?;

        // Enable privacy by default.
        host_device.read().enable_privacy(true).map_err(|e| e.as_failure())?;

        // TODO(845): Only the active host should be made connectable and scanning in the background.
        host_device
            .read()
            .set_connectable(true)
            .await
            .map_err(|_| err_msg("failed to set connectable"))?;
        host_device
            .read()
            .enable_background_scan(true)
            .map_err(|_| err_msg("failed to enable background scan"))?;

        // Initialize bt-gap as this host's pairing delegate.
        start_pairing_delegate(self.clone(), host_device.clone())?;

        // TODO(fxb/36378): Remove conversions to String when fuchsia.bluetooth.sys is supported.
        let id = host_device.read().get_info().id.value.to_string();
        self.state.write().add_host(id, host_device.clone());

        // Start listening to Host interface events.
        fasync::spawn(host_device::watch_events(self.clone(), host_device.clone()).map(|r| {
            r.unwrap_or_else(|err| {
                fx_log_warn!("Error handling host event: {:?}", err);
            })
        }));

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
}

impl HostListener for HostDispatcher {
    fn on_peer_updated(&mut self, peer: Peer) {
        self.on_device_updated(peer)
    }
    fn on_peer_removed(&mut self, identifier: String) {
        self.on_device_removed(identifier)
    }
    type HostBondFut = futures::future::BoxFuture<'static, Result<(), failure::Error>>;
    fn on_new_host_bond(&mut self, data: BondingData) -> Self::HostBondFut {
        self.store_bond(data).boxed()
    }
}

/// A future that completes when at least one adapter is available.
#[must_use = "futures do nothing unless polled"]
struct WhenHostsFound {
    hd: HostDispatcher,
    waker_key: Option<usize>,
}

impl WhenHostsFound {
    // Constructs an WhenHostsFound that completes at the latest after HOST_INIT_TIMEOUT seconds.
    fn new(hd: HostDispatcher) -> impl Future<Output = HostDispatcher> {
        WhenHostsFound { hd: hd.clone(), waker_key: None }.on_timeout(
            Duration::from_seconds(HOST_INIT_TIMEOUT).after_now(),
            move || {
                {
                    let mut inner = hd.state.write();
                    if inner.host_devices.len() == 0 {
                        fx_log_info!("No bt-host devices found");
                        inner.resolve_host_requests();
                    }
                }
                hd
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

impl Drop for WhenHostsFound {
    fn drop(&mut self) {
        self.remove_waker()
    }
}

impl Unpin for WhenHostsFound {}

impl Future for WhenHostsFound {
    type Output = HostDispatcher;

    fn poll(mut self: ::std::pin::Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        if self.hd.state.read().host_devices.len() == 0 {
            let hd = self.hd.clone();
            if self.waker_key.is_none() {
                self.waker_key = Some(hd.state.write().host_requests.insert(cx.waker().clone()));
            }
            Poll::Pending
        } else {
            self.remove_waker();
            Poll::Ready(self.hd.clone())
        }
    }
}

/// Initialize a HostDevice
async fn init_host(path: &Path, node: inspect::Node) -> Result<Arc<RwLock<HostDevice>>, Error> {
    // Connect to the host device.
    let host = File::open(path).map_err(|_| err_msg("failed to open bt-host device"))?;
    let handle = bt::host::open_host_channel(&host)?;
    let handle = fasync::Channel::from_channel(handle.into())?;
    let host = HostProxy::new(handle);

    // Obtain basic information and create and entry in the disptacher's map.
    let host_info = host.watch_state().await.context("failed to obtain bt-host information")?;
    let host_info = Inspectable::new(HostInfo::try_from(host_info)?, node);

    Ok(Arc::new(RwLock::new(HostDevice::new(path.to_path_buf(), host, host_info))))
}

async fn try_restore_bonds(
    host_device: Arc<RwLock<HostDevice>>,
    hd: HostDispatcher,
    address: &Address,
) -> types::Result<()> {
    // Load bonding data that use this host's `address` as their "local identity address".
    let opt_data = hd.stash().list_bonds(address.clone()).await?;
    let data = match opt_data {
        Some(data) => data,
        None => return Ok(()),
    };
    let fut = host_device.read().restore_bonds(data);
    let res = fut.await;
    res.map_err(|e| {
        fx_log_err!("failed to restore bonding data for host: {:?}", e);
        e
    })
}

fn generate_irk() -> Result<LocalKey, zx::Status> {
    let mut buf: [u8; 16] = [0; 16];
    zx::cprng_draw(&mut buf)?;
    Ok(LocalKey { value: buf })
}

async fn assign_host_data(
    host_device: Arc<RwLock<HostDevice>>,
    hd: HostDispatcher,
    address: &Address,
) -> Result<(), Error> {
    // Obtain an existing IRK or generate a new one if one doesn't already exists for |address|.
    let data = match hd.stash().get_host_data(address.clone()).await? {
        Some(host_data) => {
            fx_vlog!(1, "restored IRK");
            host_data.clone()
        }
        None => {
            // Generate a new IRK.
            fx_vlog!(1, "generating new IRK");
            let new_data = HostData { irk: Some(Box::new(generate_irk()?)) };

            if let Err(e) = hd.stash().store_host_data(address.clone(), new_data.clone()).await {
                fx_log_err!("failed to persist local IRK");
                return Err(e.into());
            }
            new_data
        }
    };
    let host = host_device.read();
    host.set_local_data(data).map_err(|e| e.into())
}

fn start_pairing_delegate(
    hd: HostDispatcher,
    host_device: Arc<RwLock<HostDevice>>,
) -> Result<(), Error> {
    // Initialize bt-gap as this host's pairing delegate.
    // TODO(845): Do this only for the active host. This will make sure that non-active hosts
    // always reject pairing.
    let (delegate_client_end, delegate_stream) = endpoints::create_request_stream()?;
    host_device.read().set_host_pairing_delegate(
        hd.state.read().input,
        hd.state.read().output,
        delegate_client_end,
    );
    fasync::spawn(
        services::start_pairing_delegate(hd.clone(), delegate_stream)
            .unwrap_or_else(|e| eprintln!("Failed to spawn {:?}", e)),
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::store::stash::Stash;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Peer;
    use fuchsia_inspect::{self as inspect, assert_inspect_tree};

    fn peer(id: &str) -> Peer {
        Peer {
            identifier: id.into(),
            address: "12:34:56:78:90:AB".into(),
            technology: control::TechnologyType::LowEnergy,
            name: Some("Peer Name".into()),
            appearance: control::Appearance::Unknown,
            rssi: None,
            tx_power: None,
            connected: false,
            bonded: false,
            service_uuids: vec![],
        }
    }

    #[test]
    fn on_device_changed_inspect_state() {
        // test setup
        let _executor = fasync::Executor::new();
        let stash = Stash::stub().expect("Create stash stub");
        let inspector = inspect::Inspector::new();
        let system_inspect = inspector.root().create_child("system");
        let (gas_channel_sender, _generic_access_req_stream) = mpsc::channel(0);
        let dispatcher = HostDispatcher::new(
            "test".to_string(),
            Appearance::Display,
            stash,
            system_inspect,
            gas_channel_sender,
        );
        let peer_id = "id".to_string();

        // assert inspect tree is in clean state
        assert_inspect_tree!(inspector, root: {
            system: contains {
                peer_count: 0u64,
                peers: {}
            }
        });

        // add new peer and assert inspect tree is updated
        dispatcher.on_device_updated(peer(&peer_id));
        assert_inspect_tree!(inspector, root: {
            system: contains {
                peer_count: 1u64,
                peers: {
                    "peer id": contains {
                        technology: "LowEnergy"
                    }
                }
            }
        });

        // remove peer and assert inspect tree is updated
        dispatcher.on_device_removed(peer_id);
        assert_inspect_tree!(inspector, root: {
            system: contains {
                peer_count: 0u64,
                peers: { }
            }
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_change_name_no_deadlock() {
        let stash = Stash::stub().expect("Create stash stub");
        let inspector = inspect::Inspector::new();
        let system_inspect = inspector.root().create_child("system");
        let (gas_channel_sender, _generic_access_req_stream) = mpsc::channel(0);
        let mut dispatcher = HostDispatcher::new(
            "test".to_string(),
            Appearance::Display,
            stash,
            system_inspect,
            gas_channel_sender,
        );
        // Call a function that used to use the self.state.write().gas_channel_sender.send().await
        // pattern, which caused a deadlock by yielding to the executor while holding onto a write
        // lock to the mutable gas_channel. We expect an error here because there's no active host
        // in the dispatcher - we don't need to go through the trouble of setting up an emulated
        // host to test whether or not we can send messages to the GAS task. We just want to make
        // sure that the function actually returns and doesn't deadlock.
        dispatcher.set_name("test-change".to_string()).await.unwrap_err();
    }
}

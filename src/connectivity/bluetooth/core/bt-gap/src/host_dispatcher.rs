// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, format_err, Context as _, Error},
    async_helpers::hanging_get::asynchronous as hanging_get,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_bluetooth::{Appearance, DeviceClass},
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_gatt::{LocalServiceDelegateRequest, Server_Marker, Server_Proxy},
    fidl_fuchsia_bluetooth_gatt2::Server_Marker as Server_Marker2,
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fidl_fuchsia_bluetooth_sys::{
        self as sys, InputCapability, OutputCapability, PairingDelegateProxy,
    },
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        self as bt,
        inspect::{DebugExt, Inspectable, ToProperty},
        types::{
            pairing_options::PairingOptions, Address, BondingData, HostData, HostId, HostInfo,
            Identity, Peer, PeerId,
        },
    },
    fuchsia_inspect::{self as inspect, unique_name, Property},
    fuchsia_zircon::{self as zx, Duration},
    futures::{
        channel::{mpsc, oneshot},
        future::BoxFuture,
        FutureExt,
    },
    log::{error, info, trace, warn},
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
    build_config, generic_access_service,
    host_device::{HostDevice, HostDiscoverableSession, HostDiscoverySession, HostListener},
    services::pairing::pairing_dispatcher::{PairingDispatcher, PairingDispatcherHandle},
    store::stash::Stash,
    types,
    watch_peers::PeerWatcher,
};

pub use fidl_fuchsia_device::DEFAULT_DEVICE_NAME;

/// Policies for HostDispatcher::set_name
#[derive(Copy, Clone, PartialEq, Debug)]
pub enum NameReplace {
    /// Keep the current name if it is already set, but set a new name if it hasn't been.
    Keep,
    /// Replace the current name unconditionally.
    Replace,
}

pub static HOST_INIT_TIMEOUT: i64 = 5; // Seconds

/// Available FIDL services that can be provided by a particular Host
#[derive(Copy, Clone)]
pub enum HostService {
    LeCentral,
    LePeripheral,
    LeGatt,
    LeGatt2,
    Profile,
}

/// When a client requests Discovery, we establish and store two distinct sessions; the dispatcher
/// DiscoverySession, an Arc<> of which is returned to clients and represents the dispatcher's
/// state of discovery that perists as long as one client maintains an Arc<> to the session, and
/// the HostDiscoverySession, which is returned by the active host device on which discovery is
/// physically ocurring and persists until the host disappears or the host session is dropped.
pub enum DiscoveryState {
    NotDiscovering,
    Pending(Vec<oneshot::Sender<Arc<DiscoverySession>>>),
    Discovering(Weak<DiscoverySession>, HostDiscoverySession),
}

impl DiscoveryState {
    // If a dispatcher discovery session exists, return an Arc<> pointer to it.
    fn get_discovery_session(&self) -> Option<Arc<DiscoverySession>> {
        match self {
            DiscoveryState::Discovering(weak_session, _) => weak_session.upgrade(),
            _ => None,
        }
    }

    fn end_discovery_session(&mut self) {
        // If we are Discovering, HostDiscoverySession is dropped here
        *self = DiscoveryState::NotDiscovering;
    }

    // If possible, replace the current host session with a given new one. This does not affect
    // the dispatcher session.
    fn attach_new_host_session(&mut self, new_host_session: HostDiscoverySession) {
        if let DiscoveryState::Discovering(_, host_session) = self {
            *host_session = new_host_session;
        }
    }
}

/// A dispatcher discovery session, which persists as long as at least one client holds an
/// Arc<> to it.
pub struct DiscoverySession {
    dispatcher_state: Arc<RwLock<HostDispatcherState>>,
}

impl Drop for DiscoverySession {
    fn drop(&mut self) {
        self.dispatcher_state.write().discovery.end_discovery_session()
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

/// The HostDispatcher acts as a proxy aggregating multiple HostAdapters
/// It appears as a Host to higher level systems, and is responsible for
/// routing commands to the appropriate HostAdapter
struct HostDispatcherState {
    host_devices: HashMap<HostId, HostDevice>,
    active_id: Option<HostId>,

    // Component storage.
    stash: Stash,

    // GAP state
    // Name, if set. If not set, hosts will not have a set name.
    name: Option<String>,
    appearance: Appearance,
    discovery: DiscoveryState,
    discoverable: Option<Weak<HostDiscoverableSession>>,
    config_settings: build_config::Config,
    peers: HashMap<PeerId, Inspectable<Peer>>,

    // Sender end of a futures::mpsc channel to send LocalServiceDelegateRequests
    // to Generic Access Service. When a new host adapter is recognized, we create
    // a new GasProxy, which takes GAS requests from the new host and forwards
    // them along a clone of this channel to GAS
    gas_channel_sender: mpsc::Sender<LocalServiceDelegateRequest>,

    pairing_dispatcher: Option<PairingDispatcherHandle>,

    watch_peers_publisher: hanging_get::Publisher<HashMap<PeerId, Peer>>,
    watch_peers_registrar: hanging_get::SubscriptionRegistrar<PeerWatcher>,

    watch_hosts_publisher: hanging_get::Publisher<Vec<HostInfo>>,
    watch_hosts_registrar: hanging_get::SubscriptionRegistrar<sys::HostWatcherWatchResponder>,

    // Pending requests to obtain a Host.
    host_requests: Slab<Waker>,

    inspect: HostDispatcherInspect,
}

impl HostDispatcherState {
    /// Set the active adapter for this HostDispatcher
    pub fn set_active_host(&mut self, adapter_id: HostId) -> types::Result<()> {
        if let Some(id) = self.active_id {
            if id == adapter_id {
                return Ok(());
            }

            // Shut down the previously active host.
            let _ = self.host_devices[&id].close();
        }

        if self.host_devices.contains_key(&adapter_id) {
            self.set_active_id(Some(adapter_id));
            Ok(())
        } else {
            Err(types::Error::no_host())
        }
    }

    /// Used to set the pairing delegate. If there is a prior pairing delegate connected to the
    /// host, check if the existing stored connection is closed:
    ///  * if it is closed, overwrite it and succeed
    ///  * if it is still active, fail
    /// If there is no prior delegate, this will always succeed
    /// Returns `true` if the delegate was set successfully, otherwise false
    fn set_pairing_delegate(
        &mut self,
        delegate: PairingDelegateProxy,
        input: InputCapability,
        output: OutputCapability,
    ) -> types::Result<()> {
        match self.pairing_dispatcher.as_ref() {
            Some(dispatcher) if !dispatcher.is_closed() => {
                Err(format_err!("Another Delegate is active"))?
            }
            _ => {
                self.inspect.input_capability.set(&input.debug());
                self.inspect.output_capability.set(&output.debug());
                self.inspect.has_pairing_delegate.set(true.to_property());
                let (dispatcher, handle) = PairingDispatcher::new(delegate, input, output);
                for host in self.host_devices.values() {
                    handle.add_host(host.id(), host.proxy().clone());
                }
                // Old pairing dispatcher dropped; this drops all host pairings
                self.pairing_dispatcher = Some(handle);
                // Spawn handling of the new pairing requests
                // TODO(fxbug.dev/72961) - We should avoid detach() here, and consider a more
                // explicit way to track this task
                fasync::Task::spawn(dispatcher.run()).detach();
                Ok(())
            }
        }
    }

    /// Return the active id. If the ID is currently not set,
    /// it will make the first ID in it's host_devices active
    fn get_active_id(&mut self) -> Option<HostId> {
        let active = self.active_id.clone();
        active.or_else(|| {
            self.host_devices.keys().next().cloned().map(|id| {
                self.set_active_id(Some(id));
                id
            })
        })
    }

    /// Return the active host. If the Host is currently not set,
    /// it will make the first ID in it's host_devices active
    fn get_active_host(&mut self) -> Option<HostDevice> {
        self.get_active_id().and_then(|id| self.host_devices.get(&id)).cloned()
    }

    /// Resolves all pending OnAdapterFuture's. Called when we leave the init period (by seeing the
    /// first host device or when the init timer expires).
    fn resolve_host_requests(&mut self) {
        for waker in &self.host_requests {
            waker.1.wake_by_ref();
        }
    }

    fn add_host(&mut self, id: HostId, host: HostDevice) {
        if self.host_devices.insert(id, host.clone()).is_some() {
            warn!("Host replaced: {}:", id.to_string())
        } else {
            info!("Host added: {}", id.to_string());
        }

        // If this is the only host, mark it as active.
        let _ = self.get_active_id();

        // Update inspect state
        self.inspect.host_count.set(self.host_devices.len() as u64);

        // Notify HostWatcher interface clients about the new device.
        self.notify_host_watchers();

        // Resolve pending adapter futures.
        self.resolve_host_requests();
    }

    /// Updates the active adapter and notifies listeners & host watchers.
    fn set_active_id(&mut self, id: Option<HostId>) {
        info!("New active adapter: {}", id.map_or("<none>".to_string(), |id| id.to_string()));
        self.active_id = id;
        self.notify_host_watchers();
    }

    pub fn notify_host_watchers(&self) {
        // The HostInfo::active field for the active host must be filled in later.
        let active_id = self.active_id;

        // Wait for the hanging get watcher to update so we can linearize updates
        let current_hosts: Vec<HostInfo> = self
            .host_devices
            .values()
            .map(|host| {
                let mut info = host.info();
                // Fill in HostInfo::active
                if let Some(active_id) = active_id {
                    info.active = active_id == host.id();
                }
                info
            })
            .collect();
        let mut publisher = self.watch_hosts_publisher.clone();
        fasync::Task::spawn(async move {
            publisher
                .set(current_hosts)
                .await
                .expect("Fatal error: Host Watcher HangingGet unreachable");
        })
        .detach();
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
        appearance: Appearance,
        stash: Stash,
        inspect: inspect::Node,
        gas_channel_sender: mpsc::Sender<LocalServiceDelegateRequest>,
        watch_peers_publisher: hanging_get::Publisher<HashMap<PeerId, Peer>>,
        watch_peers_registrar: hanging_get::SubscriptionRegistrar<PeerWatcher>,
        watch_hosts_publisher: hanging_get::Publisher<Vec<HostInfo>>,
        watch_hosts_registrar: hanging_get::SubscriptionRegistrar<sys::HostWatcherWatchResponder>,
    ) -> HostDispatcher {
        let hd = HostDispatcherState {
            active_id: None,
            host_devices: HashMap::new(),
            name: None,
            appearance,
            config_settings: build_config::load_default(),
            peers: HashMap::new(),
            gas_channel_sender,
            stash,
            discovery: DiscoveryState::NotDiscovering,
            discoverable: None,
            pairing_dispatcher: None,
            watch_peers_publisher,
            watch_peers_registrar,
            watch_hosts_publisher,
            watch_hosts_registrar,
            host_requests: Slab::new(),
            inspect: HostDispatcherInspect::new(inspect),
        };
        HostDispatcher { state: Arc::new(RwLock::new(hd)) }
    }

    pub fn when_hosts_found(&self) -> impl Future<Output = HostDispatcher> {
        WhenHostsFound::new(self.clone())
    }

    pub fn get_name(&self) -> String {
        self.state.read().name.clone().unwrap_or(DEFAULT_DEVICE_NAME.to_string())
    }

    pub fn get_appearance(&self) -> Appearance {
        self.state.read().appearance
    }

    pub async fn set_name(&self, name: String, replace: NameReplace) -> types::Result<()> {
        if NameReplace::Keep == replace && self.state.read().name.is_some() {
            return Ok(());
        }
        self.state.write().name = Some(name);
        match self.active_host().await {
            Some(host) => {
                let name = self.get_name();
                host.set_name(name).await
            }
            None => Err(types::Error::no_host()),
        }
    }

    pub async fn set_device_class(&self, class: DeviceClass) -> types::Result<()> {
        let class_repr = class.debug();
        let res = match self.active_host().await {
            Some(host) => host.set_device_class(class).await,
            None => Err(types::Error::no_host()),
        };

        // Update Inspect state
        if res.is_ok() {
            self.state.read().inspect.device_class.set(&class_repr);
        }
        res
    }

    /// Set the active adapter for this HostDispatcher
    pub fn set_active_host(&self, host: HostId) -> types::Result<()> {
        self.state.write().set_active_host(host)
    }

    /// Used to set the pairing delegate. If there is a prior pairing delegate connected to the
    /// host, check if the existing stored connection is closed:
    ///  * if it is closed, overwrite it and succeed
    ///  * if it is still active, fail
    /// If there is no prior delegate, this will always succeed
    /// Returns `true` if the delegate was set successfully, otherwise false
    pub fn set_pairing_delegate(
        &self,
        delegate: PairingDelegateProxy,
        input: InputCapability,
        output: OutputCapability,
    ) -> types::Result<()> {
        self.state.write().set_pairing_delegate(delegate, input, output)
    }

    pub async fn apply_sys_settings(&self, new_settings: sys::Settings) -> build_config::Config {
        let (host_devices, new_config) = {
            let mut state = self.state.write();
            state.config_settings = state.config_settings.update_with_sys_settings(&new_settings);
            (state.host_devices.clone(), state.config_settings.clone())
        };
        for (host_id, device) in host_devices {
            let fut = device.apply_sys_settings(&new_settings);
            if let Err(e) = fut.await {
                warn!("Unable to apply new settings to host {}: {:?}", host_id, e);
                let failed_host_path = device.path().to_path_buf();
                self.rm_device(&failed_host_path).await;
            }
        }
        new_config
    }

    async fn discover_on_active_host(&self) -> types::Result<HostDiscoverySession> {
        match self.active_host().await {
            Some(host) => HostDevice::establish_discovery_session(&host).await,
            None => Err(types::Error::no_host()),
        }
    }

    pub async fn start_discovery(&self) -> types::Result<Arc<DiscoverySession>> {
        // If a Discovery session already exists, return its session token
        if let Some(existing_session) = self.state.read().discovery.get_discovery_session() {
            return Ok(existing_session);
        }

        // If Discovery is pending, add ourself to queue of clients awaiting session token
        let mut session_receiver = None;
        if let DiscoveryState::Pending(client_queue) = &mut self.state.write().discovery {
            let (send, recv) = oneshot::channel();
            client_queue.push(send);
            session_receiver = Some(recv);
        }

        // We cannot also .await on the channel in the previous if statement, since we
        // acquire a lock on the dispatcher state there, i.e. self.state.write()
        if let Some(recv) = session_receiver {
            return recv
                .await
                .map_err(|_| format_err!("Pending discovery client channel closed").into());
        }

        // If we don't have a discovery session and we're not pending, we must be
        // NotDiscovering, so start a new discovery session

        // Immediately mark the state as pending to indicate to other requests to wait on
        // this discovery session initialization
        self.state.write().discovery = DiscoveryState::Pending(Vec::new());
        let host_session = self.discover_on_active_host().await?;
        let dispatcher_session =
            Arc::new(DiscoverySession { dispatcher_state: self.state.clone() });

        // Replace Pending state with new session and send session token to waiters
        if let DiscoveryState::Pending(client_queue) = std::mem::replace(
            &mut self.state.write().discovery,
            DiscoveryState::Discovering(Arc::downgrade(&dispatcher_session), host_session),
        ) {
            for client in client_queue {
                let _ = client.send(dispatcher_session.clone());
            }
        }

        Ok(dispatcher_session)
    }

    // TODO(fxbug.dev/61352) - This is susceptible to the same ToCtoToU race condition as
    // start_discovery. We can fix with the same tri-state pattern as for discovery
    pub async fn set_discoverable(&self) -> types::Result<Arc<HostDiscoverableSession>> {
        let strong_current_token =
            self.state.read().discoverable.as_ref().and_then(|token| token.upgrade());
        if let Some(token) = strong_current_token {
            return Ok(Arc::clone(&token));
        }

        match self.active_host().await {
            Some(host) => {
                let token = Arc::new(host.establish_discoverable_session().await?);
                self.state.write().discoverable = Some(Arc::downgrade(&token));
                Ok(token)
            }
            None => Err(types::Error::no_host()),
        }
    }

    fn stash(&self) -> Stash {
        self.state.read().stash.clone()
    }

    pub async fn forget(&self, peer_id: PeerId) -> types::Result<()> {
        // Try to delete from each host, even if it might not have the peer.
        // peers will be updated by the disconnection(s).
        let hosts = self.get_all_adapters().await;
        if hosts.is_empty() {
            return Err(sys::Error::Failed.into());
        }
        let mut hosts_removed: u32 = 0;
        for host in hosts {
            let host_path = host.path().to_path_buf();

            match host.forget(peer_id).await {
                Ok(()) => hosts_removed += 1,
                Err(types::Error::SysError(sys::Error::PeerNotFound)) => {
                    trace!("No peer {} on host {:?}; ignoring", peer_id, host_path);
                }
                err => {
                    error!("Could not forget peer {} on host {:?}", peer_id, host_path);
                    return err;
                }
            }
        }

        if let Err(_) = self.stash().rm_peer(peer_id).await {
            return Err(format_err!("Couldn't remove peer").into());
        }

        if hosts_removed == 0 {
            return Err(format_err!("No hosts had peer").into());
        }
        Ok(())
    }

    pub async fn connect(&self, peer_id: PeerId) -> types::Result<()> {
        let host = self.active_host().await;
        match host {
            Some(host) => host.connect(peer_id).await,
            None => Err(types::Error::SysError(sys::Error::Failed)),
        }
    }

    /// Instruct the active host to intitiate a pairing procedure with the target peer. If it
    /// fails, we return the error we receive from the host
    pub async fn pair(&self, id: PeerId, pairing_options: PairingOptions) -> types::Result<()> {
        let host = self.active_host().await;
        match host {
            Some(host) => host.pair(id, pairing_options.into()).await,
            None => Err(sys::Error::Failed.into()),
        }
    }

    // Attempt to disconnect peer with id `peer_id` from all transports
    pub async fn disconnect(&self, peer_id: PeerId) -> types::Result<()> {
        let host = self.active_host().await;
        match host {
            Some(host) => host.disconnect(peer_id).await,
            None => Err(types::Error::no_host()),
        }
    }

    pub fn active_host(&self) -> impl Future<Output = Option<HostDevice>> {
        self.when_hosts_found().map(|adapter| {
            let mut wstate = adapter.state.write();
            wstate.get_active_host()
        })
    }

    pub async fn get_all_adapters(&self) -> Vec<HostDevice> {
        let _ = self.when_hosts_found().await;
        self.state.read().host_devices.values().cloned().collect()
    }

    #[cfg(test)]
    pub fn get_adapters(&self) -> Vec<HostInfo> {
        let hosts = self.state.read();
        hosts.host_devices.values().map(|host| host.info()).collect()
    }

    pub async fn request_host_service(self, chan: zx::Channel, service: HostService) {
        match self.active_host().await {
            Some(host) => {
                let host = host.proxy();
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
                    HostService::LeGatt2 => {
                        let remote = ServerEnd::<Server_Marker2>::new(chan.into());
                        let _ = host.request_gatt2_server_(remote);
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

    // This is not an async method as we do not want to borrow `self` for the duration of the async
    // call, and we also want to trigger the send immediately even if the future is not yet awaited
    pub fn store_bond(&self, bond_data: BondingData) -> impl Future<Output = Result<(), Error>> {
        self.stash().store_bond(bond_data)
    }

    pub fn on_device_updated(&self, peer: Peer) -> impl Future<Output = ()> {
        let update_peer = peer.clone();

        let mut publisher = {
            let mut state = self.state.write();

            let node = state.inspect.peers().create_child(unique_name("peer_"));
            node.record_string("peer_id", peer.id.to_string());
            let peer = Inspectable::new(peer, node);
            let _drop_old_value = state.peers.insert(peer.id.clone(), peer);
            state.inspect.peer_count.set(state.peers.len() as u64);

            state.watch_peers_publisher.clone()
        };

        // Wait for the hanging get watcher to update so we can linearize updates
        async move {
            publisher
                .update(move |peers| {
                    let _ = peers.insert(update_peer.id, update_peer);
                    true
                })
                .await
                .expect("Fatal error: Peer Watcher HangingGet unreachable")
        }
    }

    pub fn on_device_removed(&self, id: PeerId) -> impl Future<Output = ()> {
        let mut publisher = {
            let mut state = self.state.write();
            drop(state.peers.remove(&id));
            state.inspect.peer_count.set(state.peers.len() as u64);
            state.watch_peers_publisher.clone()
        };

        // Wait for the hanging get watcher to update so we can linearize updates
        async move {
            publisher
                .update(move |peers| {
                    // Updated if we actually removed something.
                    peers.remove(&id).is_some()
                })
                .await
                .expect("Fatal error: Peer Watcher HangingGet unreachable")
        }
    }

    pub async fn watch_peers(&self) -> hanging_get::Subscriber<PeerWatcher> {
        let mut registrar = self.state.write().watch_peers_registrar.clone();
        registrar.new_subscriber().await.expect("Fatal error: Peer Watcher HangingGet unreachable")
    }

    pub async fn watch_hosts(&self) -> hanging_get::Subscriber<sys::HostWatcherWatchResponder> {
        let mut registrar = self.state.write().watch_hosts_registrar.clone();
        registrar.new_subscriber().await.expect("Fatal error: Host Watcher HangingGet unreachable")
    }

    async fn spawn_gas_proxy(&self, gatt_server_proxy: Server_Proxy) -> Result<(), Error> {
        let gas_channel = self.state.read().gas_channel_sender.clone();
        let gas_proxy =
            generic_access_service::GasProxy::new(gatt_server_proxy, gas_channel).await?;
        fasync::Task::spawn(gas_proxy.run().map(|r| {
            r.unwrap_or_else(|err| {
                warn!("Error passing message through Generic Access proxy: {:?}", err);
            })
        }))
        .detach();
        Ok(())
    }

    /// Commit all bootstrapped bonding identities to the system. This will update both the Stash
    /// and our in memory store, and notify all hosts of new bonding identities. If we already have
    /// bonding data for any of the peers (as identified by address), the new bootstrapped data
    /// will override them.
    pub async fn commit_bootstrap(&self, identities: Vec<Identity>) -> types::Result<()> {
        // Store all new bonds in our permanent Store. If we cannot successfully record the bonds
        // in the store, then Bootstrap.Commit() has failed.
        let mut stash = self.state.read().stash.clone();
        for identity in identities {
            stash.store_bonds(identity.bonds).await?
        }

        // Notify all current hosts of any changes to their bonding data
        let host_devices: Vec<_> = self.state.read().host_devices.values().cloned().collect();

        for host in host_devices {
            // If we fail to restore bonds to a given host, that is not a failure on a part of
            // Bootstrap.Commit(), but a failure on the host. So do not return error from this
            // function, but instead log and continue.
            // TODO(fxbug.dev/45325) - if a host fails we should close it and clean up after it
            if let Err(error) = try_restore_bonds(host.clone(), self.clone(), &host.address()).await
            {
                error!(
                    "Error restoring Bootstrapped bonds to host '{:?}': {}",
                    host.debug_identifiers(),
                    error
                )
            }
        }
        Ok(())
    }

    /// Finishes initializing a host device by setting host configs and services.
    async fn add_host_device(&self, host_device: &HostDevice) -> Result<(), Error> {
        let dbg_ids = host_device.debug_identifiers();

        // TODO(fxbug.dev/66615): Make sure that the bt-host device is left in a well-known state if
        // any of these operations fails.

        let address = host_device.address();
        assign_host_data(host_device.clone(), self.clone(), &address)
            .await
            .context(format!("{:?}: failed to assign identity to bt-host", dbg_ids))?;
        try_restore_bonds(host_device.clone(), self.clone(), &address)
            .await
            .map_err(|e| e.as_failure())?;

        let config = self.state.read().config_settings.clone();
        host_device
            .apply_config(config)
            .await
            .context(format!("{:?}: failed to configure bt-host device", dbg_ids))?;

        // Assign the name that is currently assigned to the HostDispatcher as the local name.
        let name = self.get_name();
        host_device
            .set_name(name)
            .await
            .map_err(|e| e.as_failure())
            .context(format!("{:?}: failed to set name of bt-host", dbg_ids))?;

        let (gatt_server_proxy, remote_gatt_server) = fidl::endpoints::create_proxy()?;
        host_device
            .proxy()
            .request_gatt_server_(remote_gatt_server)
            .context(format!("{:?}: failed to open gatt server for bt-host", dbg_ids))?;
        self.spawn_gas_proxy(gatt_server_proxy)
            .await
            .context(format!("{:?}: failed to spawn generic access service", dbg_ids))?;

        // Ensure the current active pairing delegate (if it exists) handles this host
        self.handle_pairing_requests(host_device.clone());

        self.state.write().add_host(host_device.id(), host_device.clone());

        Ok(())
    }

    /// Adds a bt-host device to the host dispatcher. Called by the watch_hosts device watcher
    pub async fn add_host_by_path(&self, host_path: &Path) -> Result<(), Error> {
        let node = self.state.read().inspect.hosts().create_child(unique_name("device_"));
        let host_dev = bt::util::open_rdwr(host_path)
            .context(format!("failed to open {:?} device file", host_path))?;
        let device_topo = fdio::device_get_topo_path(&host_dev)?;
        info!("Adding Adapter: {:?} (topology: {:?})", host_path, device_topo);
        let host_device = init_host(host_path, node).await?;
        self.add_host_device(&host_device).await?;

        // Start listening to Host interface events.
        fasync::Task::spawn(host_device.watch_events(self.clone()).map(|r| {
            r.unwrap_or_else(|err| {
                warn!("Error handling host event: {:?}", err);
                // TODO(fxbug.dev/44180): This should probably remove the bt-host since termination of the
                // `watch_events` task indicates that it no longer functions properly.
            })
        }))
        .detach();

        Ok(())
    }

    // Update our hanging_get server with the latest hosts. This will notify any pending
    // hanging_gets and any new requests will see the new results.
    fn notify_host_watchers(&self) {
        self.state.write().notify_host_watchers();
    }

    pub async fn rm_device(&self, host_path: &Path) {
        let mut new_adapter_activated = false;
        // Scope our HostDispatcherState lock
        {
            let mut hd = self.state.write();
            let active_id = hd.active_id.clone();

            // Get the host IDs that match `host_path`.
            let ids: Vec<HostId> = hd
                .host_devices
                .iter()
                .filter(|(_, ref host)| host.path() == host_path)
                .map(|(k, _)| k.clone())
                .collect();

            let id_strs: Vec<String> = ids.iter().map(|id| id.to_string()).collect();
            info!("Host removed: {} (path: {:?})", id_strs.join(","), host_path);

            for id in &ids {
                drop(hd.host_devices.remove(id));
            }

            // Reset the active ID if it got removed.
            if let Some(active_id) = active_id {
                if ids.contains(&active_id) {
                    hd.active_id = None;
                }
            }

            // Try to assign a new active adapter. This may send an "OnActiveAdapterChanged" event.
            if hd.active_id.is_none() && hd.get_active_id().is_some() {
                new_adapter_activated = true;
            }
        } // Now the lock is dropped, we can run the async notify

        if new_adapter_activated {
            if let Err(err) = self.configure_newly_active_adapter().await {
                warn!("Failed to persist state on adapter change: {:?}", err);
            }
        }
        self.notify_host_watchers();
    }

    /// Configure a newly active adapter with the correct behavior for an active adapter.
    async fn configure_newly_active_adapter(&self) -> types::Result<()> {
        // Migrate discovery state to new host
        if self.state.read().discovery.get_discovery_session().is_some() {
            let new_host_session = self.discover_on_active_host().await?;
            self.state.write().discovery.attach_new_host_session(new_host_session);
        }

        Ok(())
    }

    /// Route pairing requests from this host through our pairing dispatcher, if it exists
    fn handle_pairing_requests(&self, host: HostDevice) {
        let mut dispatcher = self.state.write();

        if let Some(handle) = &mut dispatcher.pairing_dispatcher {
            handle.add_host(host.id(), host.proxy().clone());
        }
    }
}

impl HostListener for HostDispatcher {
    type PeerUpdatedFut = BoxFuture<'static, ()>;
    fn on_peer_updated(&mut self, peer: Peer) -> Self::PeerUpdatedFut {
        self.on_device_updated(peer).boxed()
    }
    type PeerRemovedFut = BoxFuture<'static, ()>;
    fn on_peer_removed(&mut self, id: PeerId) -> Self::PeerRemovedFut {
        self.on_device_removed(id).boxed()
    }
    type HostBondFut = BoxFuture<'static, Result<(), anyhow::Error>>;
    fn on_new_host_bond(&mut self, data: BondingData) -> Self::HostBondFut {
        self.store_bond(data).boxed()
    }

    type HostInfoFut = BoxFuture<'static, Result<(), anyhow::Error>>;
    fn on_host_updated(&mut self, _info: HostInfo) -> Self::HostInfoFut {
        self.notify_host_watchers();
        async { Ok(()) }.boxed()
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
                        info!("No bt-host devices found");
                        inner.resolve_host_requests();
                    }
                }
                hd
            },
        )
    }

    fn remove_waker(&mut self) {
        if let Some(key) = self.waker_key {
            drop(self.hd.state.write().host_requests.remove(key));
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
async fn init_host(path: &Path, node: inspect::Node) -> Result<HostDevice, Error> {
    // Connect to the host device.
    let host = File::open(path).map_err(|_| format_err!("failed to open bt-host device"))?;
    let handle = bt::host::open_host_channel(&host)?;
    let handle = fasync::Channel::from_channel(handle.into())?;
    let host = HostProxy::new(handle);

    node.record_string("path", path.to_string_lossy());

    // Obtain basic information and create and entry in the dispatcher's map.
    let host_info = host.watch_state().await.context("failed to obtain bt-host information")?;
    let host_info = Inspectable::new(HostInfo::try_from(host_info)?, node);

    Ok(HostDevice::new(path.to_path_buf(), host, host_info))
}

async fn try_restore_bonds(
    host_device: HostDevice,
    hd: HostDispatcher,
    address: &Address,
) -> types::Result<()> {
    // Load bonding data that use this host's `address` as their "local identity address".
    let opt_data = hd.stash().list_bonds(address.clone()).await?;
    let data = match opt_data {
        Some(data) => data,
        None => return Ok(()),
    };
    match host_device.restore_bonds(data).await {
        Err(e) => {
            error!("failed to restore bonding data for host: {:?}", e);
            Err(e)
        }
        Ok(errors) => {
            if errors.is_empty() {
                Ok(())
            } else {
                let msg =
                    errors.into_iter().fold("".to_string(), |acc, b| format!("{}, {:?}", acc, b));
                let msg = format!("failed to restore bonding data: {}", msg);
                error!("{}", msg);
                Err(anyhow!(msg).into())
            }
        }
    }
}

fn generate_irk() -> Result<sys::Key, zx::Status> {
    let mut buf: [u8; 16] = [0; 16];
    // Generate a secure IRK.
    zx::cprng_draw(&mut buf);
    Ok(sys::Key { value: buf })
}

async fn assign_host_data(
    host: HostDevice,
    hd: HostDispatcher,
    address: &Address,
) -> Result<(), Error> {
    // Obtain an existing IRK or generate a new one if one doesn't already exists for |address|.
    let data = match hd.stash().get_host_data(address.clone()).await? {
        Some(host_data) => {
            trace!("restored IRK");
            host_data.clone()
        }
        None => {
            // Generate a new IRK.
            trace!("generating new IRK");
            let new_data = HostData { irk: Some(generate_irk()?) };

            if let Err(e) = hd.stash().store_host_data(address.clone(), new_data.clone()).await {
                error!("failed to persist local IRK");
                return Err(e.into());
            }
            new_data
        }
    };
    host.set_local_data(data).map_err(|e| e.into())
}

#[cfg(test)]
pub(crate) mod test {
    use super::*;

    use fidl_fuchsia_bluetooth_gatt::{
        LocalServiceDelegateProxy, LocalServiceRequestStream, Server_Request,
        Server_RequestStream as GattServerRequestStream,
    };
    use fidl_fuchsia_bluetooth_host::{HostRequest, HostRequestStream};
    use futures::{future::join, StreamExt};

    pub(crate) fn make_test_dispatcher(
        watch_peers_publisher: hanging_get::Publisher<HashMap<PeerId, Peer>>,
        watch_peers_registrar: hanging_get::SubscriptionRegistrar<PeerWatcher>,
        watch_hosts_publisher: hanging_get::Publisher<Vec<HostInfo>>,
        watch_hosts_registrar: hanging_get::SubscriptionRegistrar<sys::HostWatcherWatchResponder>,
    ) -> HostDispatcher {
        let (gas_channel_sender, _ignored_gas_task_req_stream) = mpsc::channel(0);
        HostDispatcher::new(
            Appearance::Display,
            Stash::in_memory_mock(),
            fuchsia_inspect::Node::default(),
            gas_channel_sender,
            watch_peers_publisher,
            watch_peers_registrar,
            watch_hosts_publisher,
            watch_hosts_registrar,
        )
    }

    pub(crate) fn make_simple_test_dispatcher() -> HostDispatcher {
        let watch_peers_broker = hanging_get::HangingGetBroker::new(
            HashMap::new(),
            |_, _| true,
            hanging_get::DEFAULT_CHANNEL_SIZE,
        );
        let watch_hosts_broker = hanging_get::HangingGetBroker::new(
            Vec::new(),
            |_, _| true,
            hanging_get::DEFAULT_CHANNEL_SIZE,
        );

        let dispatcher = make_test_dispatcher(
            watch_peers_broker.new_publisher(),
            watch_peers_broker.new_registrar(),
            watch_hosts_broker.new_publisher(),
            watch_hosts_broker.new_registrar(),
        );

        let watchers_fut = join(watch_peers_broker.run(), watch_hosts_broker.run()).map(|_| ());
        fasync::Task::spawn(watchers_fut).detach();
        dispatcher
    }

    #[derive(Default)]
    pub(crate) struct GasEndpoints {
        gatt_server: Option<GattServerRequestStream>,
        delegate: Option<LocalServiceDelegateProxy>,
        service: Option<LocalServiceRequestStream>,
    }

    async fn handle_standard_host_server_init(
        mut host_server: HostRequestStream,
    ) -> (HostRequestStream, GasEndpoints) {
        let mut gas_endpoints = GasEndpoints::default();
        while gas_endpoints.gatt_server.is_none() {
            match host_server.next().await {
                Some(Ok(HostRequest::SetLocalName { responder, .. })) => {
                    info!("Setting Local Name");
                    let _ = responder.send(&mut Ok(()));
                }
                Some(Ok(HostRequest::SetDeviceClass { responder, .. })) => {
                    info!("Setting Device Class");
                    let _ = responder.send(&mut Ok(()));
                }
                Some(Ok(HostRequest::RequestGattServer_ { server, .. })) => {
                    // don't respond at all on the server side.
                    info!("Storing Gatt Server");
                    let mut gatt_server = server.into_stream().unwrap();
                    info!("GAS Server was started, waiting for publish");
                    // The Generic Access Service now publishes itself.
                    match gatt_server.next().await {
                        Some(Ok(Server_Request::PublishService {
                            info,
                            delegate,
                            service,
                            responder,
                        })) => {
                            info!("Captured publish of GAS Service: {:?}", info);
                            gas_endpoints.delegate = Some(delegate.into_proxy().unwrap());
                            gas_endpoints.service = Some(service.into_stream().unwrap());
                            let _ =
                                responder.send(&mut fidl_fuchsia_bluetooth::Status { error: None });
                        }
                        x => error!("Got unexpected GAS Server request: {:?}", x),
                    }
                    gas_endpoints.gatt_server = Some(gatt_server);
                }
                Some(Ok(HostRequest::SetConnectable { responder, .. })) => {
                    info!("Setting connectable");
                    let _ = responder.send(&mut Ok(()));
                }
                Some(Ok(req)) => info!("Unhandled Host Request in add: {:?}", req),
                Some(Err(e)) => error!("Error in host server: {:?}", e),
                None => break,
            }
        }
        info!("Finishing host_device mocking for add host");
        (host_server, gas_endpoints)
    }

    pub(crate) async fn create_and_add_test_host_to_dispatcher(
        id: HostId,
        dispatcher: &HostDispatcher,
    ) -> types::Result<(HostRequestStream, HostDevice, GasEndpoints)> {
        let (host_server, host_device) = HostDevice::mock_from_id(id);
        let host_server_init_handler = handle_standard_host_server_init(host_server);
        let (res, (host_server, gas_endpoints)) =
            join(dispatcher.add_host_device(&host_device), host_server_init_handler).await;
        res?;
        Ok((host_server, host_device, gas_endpoints))
    }
}

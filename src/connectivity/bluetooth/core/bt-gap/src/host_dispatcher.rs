// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, format_err, Context as _, Error},
    async_helpers::hanging_get::asynchronous as hanging_get,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_bluetooth::{Appearance, DeviceClass},
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_control::{self as control, ControlControlHandle},
    fidl_fuchsia_bluetooth_gatt::{LocalServiceDelegateRequest, Server_Marker, Server_Proxy},
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fidl_fuchsia_bluetooth_sys::{self as sys, InputCapability, OutputCapability},
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
    host_device::{self, HostDevice, HostDiscoverySession, HostListener},
    services::pairing::{
        pairing_dispatcher::{PairingDispatcher, PairingDispatcherHandle},
        PairingDelegate,
    },
    store::stash::Stash,
    types,
    watch_peers::PeerWatcher,
};

pub static HOST_INIT_TIMEOUT: i64 = 5; // Seconds

/// Available FIDL services that can be provided by a particular Host
pub enum HostService {
    LeCentral,
    LePeripheral,
    LeGatt,
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

// We use tokens to track the reference counting for discovery/discoverable states
// As long as at least one user maintains an Arc<> to the token, the state persists
// Once all references are dropped, the `Drop` trait on the token causes the state
// to be terminated.
pub struct DiscoverableRequestToken {
    adap: Weak<RwLock<HostDevice>>,
}

impl Drop for DiscoverableRequestToken {
    fn drop(&mut self) {
        if let Some(host) = self.adap.upgrade() {
            let await_response = host.write().set_discoverable(false);
            fasync::Task::spawn(async move {
                if let Err(err) = await_response.await {
                    // TODO(45325) - we should close the host channel if an error is returned
                    warn!("Unexpected error response when disabling discoverable: {:?}", err);
                }
            })
            .detach();
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

/// The HostDispatcher acts as a proxy aggregating multiple HostAdapters
/// It appears as a Host to higher level systems, and is responsible for
/// routing commands to the appropriate HostAdapter
struct HostDispatcherState {
    host_devices: HashMap<HostId, Arc<RwLock<HostDevice>>>,
    active_id: Option<HostId>,

    // Component storage.
    pub stash: Stash,

    // GAP state
    name: String,
    appearance: Appearance,
    discovery: DiscoveryState,
    discoverable: Option<Weak<DiscoverableRequestToken>>,
    pub input: InputCapability,
    pub output: OutputCapability,
    pub config_settings: build_config::Config,
    peers: HashMap<PeerId, Inspectable<Peer>>,

    // Sender end of a futures::mpsc channel to send LocalServiceDelegateRequests
    // to Generic Access Service. When a new host adapter is recognized, we create
    // a new GasProxy, which takes GAS requests from the new host and forwards
    // them along a clone of this channel to GAS
    gas_channel_sender: mpsc::Sender<LocalServiceDelegateRequest>,

    pairing_delegate: Option<PairingDelegate>,
    pairing_dispatcher: Option<PairingDispatcherHandle>,

    pub event_listeners: Vec<Weak<ControlControlHandle>>,

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
            let _ = self.host_devices[&id].write().close();
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
    pub fn set_pairing_delegate(&mut self, delegate: sys::PairingDelegateProxy) {
        self.inspect.has_pairing_delegate.set(true.to_property());
        let assign = match &self.pairing_delegate {
            None => true,
            Some(delegate) => delegate.is_closed(),
        };
        if assign {
            self.set_pairing_delegate_internal(
                Some(PairingDelegate::Sys(delegate)),
                self.input,
                self.output,
            );
        } else {
            info!("Failed to set PairingDelegate; another Delegate is active");
        }
    }

    /// Used to set the pairing delegate. If there is a prior pairing delegate connected to the
    /// host it will fail. It checks if the existing stored connection is closed, and will
    /// overwrite it if so.
    pub fn set_control_pairing_delegate(
        &mut self,
        delegate: Option<control::PairingDelegateProxy>,
    ) -> bool {
        self.inspect.has_pairing_delegate.set(delegate.is_some().to_property());
        match delegate {
            Some(delegate) => {
                let assign = match &self.pairing_delegate {
                    None => true,
                    Some(delegate) => delegate.is_closed(),
                };
                if assign {
                    self.set_pairing_delegate_internal(
                        Some(PairingDelegate::Control(delegate)),
                        self.input,
                        self.output,
                    );
                }
                assign
            }
            None => {
                self.pairing_delegate = None;
                self.set_pairing_delegate_internal(None, self.input, self.output);
                false
            }
        }
    }

    fn set_pairing_delegate_internal(
        &mut self,
        delegate: Option<PairingDelegate>,
        input: InputCapability,
        output: OutputCapability,
    ) {
        match delegate {
            Some(delegate) => {
                let (dispatcher, handle) = PairingDispatcher::new(delegate, input, output);
                for host in self.host_devices.values() {
                    let (id, proxy) = {
                        let host = host.read();
                        let proxy = host.get_host().clone();
                        let info = host.get_info();
                        (HostId::from(info.id), proxy)
                    };
                    handle.add_host(id, proxy.clone());
                }
                // Old pairing dispatcher dropped; this drops all host pairings
                self.pairing_dispatcher = Some(handle);
                // Spawn handling of the new pairing requests
                fasync::Task::spawn(dispatcher.run()).detach();
            }
            // Old pairing dispatcher dropped; this drops all host pairings
            None => self.pairing_dispatcher = None,
        }
    }

    /// Set the IO capabilities of the system
    pub fn set_io_capability(&mut self, input: InputCapability, output: OutputCapability) {
        self.input = input;
        self.output = output;
        self.inspect.input_capability.set(&input.debug());
        self.inspect.output_capability.set(&output.debug());

        self.set_pairing_delegate_internal(self.pairing_delegate.clone(), self.input, self.output);
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
    fn get_active_host(&mut self) -> Option<Arc<RwLock<HostDevice>>> {
        self.get_active_id().and_then(|id| self.host_devices.get(&id)).cloned()
    }

    /// Resolves all pending OnAdapterFuture's. Called when we leave the init period (by seeing the
    /// first host device or when the init timer expires).
    fn resolve_host_requests(&mut self) {
        for waker in &self.host_requests {
            waker.1.wake_by_ref();
        }
    }

    fn add_host(&mut self, id: HostId, host: Arc<RwLock<HostDevice>>) {
        info!("Host added: {}", id.to_string());
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
    fn set_active_id(&mut self, id: Option<HostId>) {
        info!("New active adapter: {}", id.map_or("<none>".to_string(), |id| id.to_string()));
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
        watch_peers_publisher: hanging_get::Publisher<HashMap<PeerId, Peer>>,
        watch_peers_registrar: hanging_get::SubscriptionRegistrar<PeerWatcher>,
        watch_hosts_publisher: hanging_get::Publisher<Vec<HostInfo>>,
        watch_hosts_registrar: hanging_get::SubscriptionRegistrar<sys::HostWatcherWatchResponder>,
    ) -> HostDispatcher {
        let hd = HostDispatcherState {
            active_id: None,
            host_devices: HashMap::new(),
            name,
            appearance,
            input: InputCapability::None,
            output: OutputCapability::None,
            config_settings: build_config::load_default(),
            peers: HashMap::new(),
            gas_channel_sender,
            stash,
            discovery: DiscoveryState::NotDiscovering,
            discoverable: None,
            pairing_delegate: None,
            pairing_dispatcher: None,
            event_listeners: vec![],
            watch_peers_publisher,
            watch_peers_registrar,
            watch_hosts_publisher,
            watch_hosts_registrar,
            host_requests: Slab::new(),
            inspect: HostDispatcherInspect::new(inspect),
        };
        HostDispatcher { state: Arc::new(RwLock::new(hd)) }
    }

    pub fn get_active_host_info(&self) -> Option<HostInfo> {
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

    pub async fn set_name(&self, name: String) -> types::Result<()> {
        self.state.write().name = name;
        match self.get_active_adapter().await {
            Some(adapter) => {
                let fut = adapter.write().set_name(self.state.read().name.clone());
                fut.await
            }
            None => Err(types::Error::no_host()),
        }
    }

    pub async fn set_device_class(&self, class: DeviceClass) -> types::Result<()> {
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
    pub fn set_active_host(&self, host: HostId) -> types::Result<()> {
        self.state.write().set_active_host(host)
    }

    pub fn set_pairing_delegate(&self, delegate: sys::PairingDelegateProxy) {
        self.state.write().set_pairing_delegate(delegate)
    }

    pub fn set_control_pairing_delegate(
        &self,
        delegate: Option<control::PairingDelegateProxy>,
    ) -> bool {
        self.state.write().set_control_pairing_delegate(delegate)
    }

    pub async fn apply_sys_settings(&self, new_settings: sys::Settings) -> build_config::Config {
        let (host_devices, new_config) = {
            let mut state = self.state.write();
            state.config_settings = state.config_settings.update_with_sys_settings(&new_settings);
            (state.host_devices.clone(), state.config_settings.clone())
        };
        for (host_id, device) in host_devices {
            let fut = device.read().apply_sys_settings(&new_settings);
            if let Err(e) = fut.await {
                warn!("Unable to apply new settings to host {}: {:?}", host_id, e);
                let failed_host_path = device.read().path.clone();
                self.rm_adapter(&failed_host_path).await;
            }
        }
        new_config
    }

    async fn discover_on_active_host(&self) -> types::Result<HostDiscoverySession> {
        match self.get_active_adapter().await {
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

    pub async fn set_discoverable(&self) -> types::Result<Arc<DiscoverableRequestToken>> {
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

    pub async fn forget(&self, peer_id: PeerId) -> types::Result<()> {
        // Try to delete from each adapter, even if it might not have the peer.
        // peers will be updated by the disconnection(s).
        let adapters = self.get_all_adapters().await;
        if adapters.is_empty() {
            return Err(sys::Error::Failed.into());
        }
        let mut adapters_removed: u32 = 0;
        for adapter in adapters {
            let adapter_path = adapter.read().path.clone();

            let fut = adapter.write().forget(peer_id);
            match fut.await {
                Ok(()) => adapters_removed += 1,
                Err(types::Error::SysError(sys::Error::PeerNotFound)) => {
                    trace!("No peer {} on adapter {:?}; ignoring", peer_id, adapter_path);
                }
                err => {
                    error!("Could not forget peer {} on adapter {:?}", peer_id, adapter_path);
                    return err;
                }
            }
        }

        if let Err(_) = self.stash().rm_peer(peer_id).await {
            return Err(format_err!("Couldn't remove peer").into());
        }

        if adapters_removed == 0 {
            return Err(format_err!("No adapters had peer").into());
        }
        Ok(())
    }

    pub async fn connect(&self, peer_id: PeerId) -> types::Result<()> {
        let host = self.get_active_adapter().await;
        match host {
            Some(host) => {
                let fut = host.write().connect(peer_id);
                fut.await
            }
            None => Err(types::Error::SysError(sys::Error::Failed)),
        }
    }

    /// Instruct the active host to intitiate a pairing procedure with the target peer. If it
    /// fails, we return the error we receive from the host
    pub async fn pair(&self, id: PeerId, pairing_options: PairingOptions) -> types::Result<()> {
        let host = self.get_active_adapter().await;
        match host {
            Some(host) => {
                let fut = host.write().pair(id, pairing_options.into());
                fut.await
            }
            None => Err(sys::Error::Failed.into()),
        }
    }

    // Attempt to disconnect peer with id `peer_id` from all transports
    pub async fn disconnect(&self, peer_id: PeerId) -> types::Result<()> {
        let host = self.get_active_adapter().await;
        match host {
            Some(host) => {
                let fut = host.write().disconnect(peer_id);
                fut.await
            }
            None => Err(types::Error::no_host()),
        }
    }

    pub async fn get_active_adapter(&self) -> Option<Arc<RwLock<HostDevice>>> {
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

    pub async fn request_host_service(self, chan: zx::Channel, service: HostService) {
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

    pub fn set_io_capability(&self, input: InputCapability, output: OutputCapability) {
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

    // This is not an async method as we do not want to borrow `self` for the duration of the async
    // call, and we also want to trigger the send immediately even if the future is not yet awaited
    pub fn store_bond(&self, bond_data: BondingData) -> impl Future<Output = Result<(), Error>> {
        self.stash().store_bond(bond_data)
    }

    pub fn on_device_updated(&self, peer: Peer) -> impl Future<Output = ()> {
        // TODO(825): generic method for this pattern
        let mut d = control::RemoteDevice::from(peer.clone());
        self.notify_event_listeners(|listener| {
            let _res = listener
                .send_on_device_updated(&mut d)
                .map_err(|e| error!("Failed to send device updated event: {:?}", e));
        });

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
                    peers.insert(update_peer.id, update_peer);
                    true
                })
                .await
                .expect("Fatal error: Peer Watcher HangingGet unreachable")
        }
    }

    pub fn on_device_removed(&self, id: PeerId) -> impl Future<Output = ()> {
        let mut publisher = {
            let mut state = self.state.write();
            state.peers.remove(&id);
            state.inspect.peer_count.set(state.peers.len() as u64);
            state.notify_event_listeners(|listener| {
                let _res = listener
                    .send_on_device_removed(&id.to_string())
                    .map_err(|e| error!("Failed to send device removed event: {:?}", e));
            });
            state.watch_peers_publisher.clone()
        };

        // Wait for the hanging get watcher to update so we can linearize updates
        async move {
            publisher
                .update(move |peers| {
                    peers.remove(&id);
                    true
                })
                .await
                .expect("Fatal error: Peer Watcher HangingGet unreachable")
        }
    }

    pub fn get_peers(&self) -> Vec<Peer> {
        self.state.read().peers.values().map(|p| (*p).clone()).collect()
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
    pub async fn add_adapter(&self, host_path: &Path) -> Result<(), Error> {
        let node = self.state.read().inspect.hosts().create_child(unique_name("device_"));
        let host_dev = bt::util::open_rdwr(host_path)?;
        let device_topo = fdio::device_get_topo_path(&host_dev)?;
        info!("Adding Adapter: {:?} (topology: {:?})", host_path, device_topo);
        let host_device = init_host(host_path, node).await?;

        // TODO(armansito): Make sure that the bt-host device is left in a well-known state if any
        // of these operations fails.

        // TODO(fxbug.dev/22017): The following code applies a number of configurations to the bt-host by
        // default. We should tie these to a package configuration (once it is possible), as some of these
        // are undesirable in certain situations, e.g when running PTS tests.
        //
        // Currently applied settings:
        //   - LE Privacy with IRK
        //   - LE background scan for auto-connection
        //   - BR/EDR connectable mode

        self.state.read().config_settings.apply(&host_device.clone().read()).await?;
        let address = host_device.read().get_info().address.clone();
        assign_host_data(host_device.clone(), self.clone(), &address).await?;
        try_restore_bonds(host_device.clone(), self.clone(), &address)
            .await
            .map_err(|e| e.as_failure())?;

        // Assign the name that is currently assigned to the HostDispatcher as the local name.
        let fut = host_device.read().set_name(self.state.read().name.clone());
        fut.await.map_err(|e| e.as_failure())?;

        let (gatt_server_proxy, remote_gatt_server) = fidl::endpoints::create_proxy()?;
        host_device.read().get_host().request_gatt_server_(remote_gatt_server)?;
        self.spawn_gas_proxy(gatt_server_proxy).await?;

        // Ensure the current active pairing delegate (if it exists) handles this host
        self.handle_pairing_requests(host_device.clone());

        let id: HostId = host_device.read().get_info().id.into();
        self.state.write().add_host(id.clone(), host_device.clone());

        self.notify_host_watchers().await;

        // Start listening to Host interface events.
        fasync::Task::spawn(host_device::watch_events(self.clone(), host_device.clone()).map(
            |r| {
                r.unwrap_or_else(|err| {
                    warn!("Error handling host event: {:?}", err);
                    // TODO(fxbug.dev/44180): This should probably remove the bt-host since termination of the
                    // `watch_events` task indicates that it no longer functions properly.
                })
            },
        ))
        .detach();

        Ok(())
    }

    // Update our hanging_get server with the latest hosts. This will notify any pending
    // hanging_gets and any new requests will see the new results.
    fn notify_host_watchers(&self) -> impl Future<Output = ()> {
        let mut publisher = self.state.write().watch_hosts_publisher.clone();
        // Wait for the hanging get watcher to update so we can linearize updates
        let current_hosts: Vec<_> = self
            .state
            .write()
            .host_devices
            .values()
            .map(|host| host.read().get_info().clone())
            .collect();
        async move {
            publisher
                .set(current_hosts)
                .await
                .expect("Fatal error: Host Watcher HangingGet unreachable");
        }
    }

    pub async fn rm_adapter(&self, host_path: &Path) {
        let mut new_adapter_activated = false;
        // Scope our HostDispatcherState lock
        {
            let mut hd = self.state.write();
            let active_id = hd.active_id.clone();

            // Get the host IDs that match `host_path`.
            let ids: Vec<HostId> = hd
                .host_devices
                .iter()
                .filter(|(_, ref host)| host.read().path == host_path)
                .map(|(k, _)| k.clone())
                .collect();

            let id_strs: Vec<String> = ids.iter().map(|id| id.to_string()).collect();
            info!("Host removed: {} (path: {:?})", id_strs.join(","), host_path);

            for id in &ids {
                hd.host_devices.remove(id);
                hd.notify_event_listeners(|listener| {
                    let _ = listener.send_on_adapter_removed(&id.to_string());
                })
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
        self.notify_host_watchers().await;
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
    fn handle_pairing_requests(&self, host: Arc<RwLock<HostDevice>>) {
        let mut dispatcher = self.state.write();

        if let Some(handle) = &mut dispatcher.pairing_dispatcher {
            let (id, proxy) = {
                let host = host.read();
                let proxy = host.get_host().clone();
                let info = host.get_info();
                (HostId::from(info.id), proxy)
            };
            handle.add_host(id, proxy);
        }
    }

    #[cfg(test)]
    pub(crate) fn add_test_host(&self, id: HostId, host: Arc<RwLock<HostDevice>>) {
        let mut state = self.state.write();
        state.add_host(id, host)
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
        self.notify_host_watchers().map(|_| Ok(())).boxed()
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
    let host = File::open(path).map_err(|_| format_err!("failed to open bt-host device"))?;
    let handle = bt::host::open_host_channel(&host)?;
    let handle = fasync::Channel::from_channel(handle.into())?;
    let host = HostProxy::new(handle);

    node.record_string("path", path.to_string_lossy());

    // Obtain basic information and create and entry in the dispatcher's map.
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
    let result = fut.await;
    match result {
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
    zx::cprng_draw(&mut buf)?;
    Ok(sys::Key { value: buf })
}

async fn assign_host_data(
    host_device: Arc<RwLock<HostDevice>>,
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
    let host = host_device.read();
    host.set_local_data(data).map_err(|e| e.into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        build_config::{BrEdrConfig, Config},
        host_dispatcher::test as hd_test,
        launch_profile_forwarding_component, relay_profile_channel,
        store::stash::Stash,
    };
    use {
        fidl::encoding::Decodable,
        fidl_fuchsia_bluetooth::Appearance,
        fidl_fuchsia_bluetooth_host::HostRequest,
        fidl_fuchsia_bluetooth_sys::TechnologyType,
        fuchsia_async as fasync,
        fuchsia_bluetooth::types::{Peer, PeerId},
        fuchsia_component::fuchsia_single_component_package_url,
        fuchsia_inspect::{self as inspect, assert_inspect_tree},
        futures::stream::TryStreamExt,
        matches::assert_matches,
        std::collections::HashSet,
    };

    fn peer(id: PeerId) -> Peer {
        Peer {
            id: id.into(),
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            technology: TechnologyType::LowEnergy,
            name: Some("Peer Name".into()),
            appearance: Some(Appearance::Watch),
            device_class: None,
            rssi: None,
            tx_power: None,
            connected: false,
            bonded: false,
            le_services: vec![],
            bredr_services: vec![],
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn on_device_changed_inspect_state() {
        // test setup
        let stash = Stash::stub().expect("Create stash stub");
        let inspector = inspect::Inspector::new();
        let system_inspect = inspector.root().create_child("system");
        let (gas_channel_sender, _generic_access_req_stream) = mpsc::channel(0);
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
        let dispatcher = HostDispatcher::new(
            "test".to_string(),
            Appearance::Display,
            stash,
            system_inspect,
            gas_channel_sender,
            watch_peers_broker.new_publisher(),
            watch_peers_broker.new_registrar(),
            watch_hosts_broker.new_publisher(),
            watch_hosts_broker.new_registrar(),
        );
        let peer_id = PeerId(1);

        // assert inspect tree is in clean state
        assert_inspect_tree!(inspector, root: {
            system: contains {
                peer_count: 0u64,
                peers: {}
            }
        });

        // add new peer and assert inspect tree is updated
        dispatcher.on_device_updated(peer(peer_id)).await;
        assert_inspect_tree!(inspector, root: {
            system: contains {
                peer_count: 1u64,
                peers: {
                    "peer_0": contains {
                        peer_id: peer_id.to_string(),
                        technology: "LowEnergy"
                    }
                }
            }
        });

        // remove peer and assert inspect tree is updated
        dispatcher.on_device_removed(peer_id).await;
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

        let dispatcher = HostDispatcher::new(
            "test".to_string(),
            Appearance::Display,
            stash,
            system_inspect,
            gas_channel_sender,
            watch_peers_broker.new_publisher(),
            watch_peers_broker.new_registrar(),
            watch_hosts_broker.new_publisher(),
            watch_hosts_broker.new_registrar(),
        );
        // Call a function that used to use the self.state.write().gas_channel_sender.send().await
        // pattern, which caused a deadlock by yielding to the executor while holding onto a write
        // lock to the mutable gas_channel. We expect an error here because there's no active host
        // in the dispatcher - we don't need to go through the trouble of setting up an emulated
        // host to test whether or not we can send messages to the GAS task. We just want to make
        // sure that the function actually returns and doesn't deadlock.
        dispatcher.set_name("test-change".to_string()).await.unwrap_err();
    }

    async fn host_is_in_dispatcher(id: &HostId, dispatcher: &HostDispatcher) -> bool {
        dispatcher.get_adapters().await.iter().map(|i| i.id).collect::<HashSet<_>>().contains(id)
    }

    #[fasync::run_singlethreaded(test)]
    async fn apply_settings_fails_host_removed() {
        let dispatcher = hd_test::make_simple_test_dispatcher().unwrap();
        let host_id = HostId(42);
        let mut host_server =
            hd_test::create_and_add_test_host_to_dispatcher(host_id, &dispatcher).unwrap();
        assert!(host_is_in_dispatcher(&host_id, &dispatcher).await);
        let run_host = async move {
            if let Ok(Some(HostRequest::SetConnectable { responder, .. })) =
                host_server.try_next().await
            {
                responder.send(&mut Err(sys::Error::Failed)).unwrap();
            } else {
                panic!("Unexpected request");
            }
        };
        let disable_connectable_fut = async {
            let updated_config = dispatcher
                .apply_sys_settings(sys::Settings {
                    bredr_connectable_mode: Some(false),
                    ..sys::Settings::new_empty()
                })
                .await;
            assert_matches!(updated_config, Config { bredr: BrEdrConfig { connectable: false, ..}, ..});
        };
        futures::future::join(run_host, disable_connectable_fut).await;
        assert!(!host_is_in_dispatcher(&host_id, &dispatcher).await);
    }

    /// Tests that launching the profile forwarding component is successful. The component
    /// (in this case bt-rfcomm.cmx) should connect to the Profile service (indicated by the
    /// upstream host_server). We then expect any subsequent client connections over `Profile`
    /// to be relayed to the component and _not_ the upstream Host Server.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_launch_profile_forwarding_component_success() {
        let rfcomm_url = fuchsia_single_component_package_url!("bt-rfcomm").to_string();
        let host_dispatcher = hd_test::make_simple_test_dispatcher().unwrap();

        let host_id = HostId(43);
        let mut host_server =
            hd_test::create_and_add_test_host_to_dispatcher(host_id, &host_dispatcher).unwrap();
        assert!(host_is_in_dispatcher(&host_id, &host_dispatcher).await);
        let component =
            launch_profile_forwarding_component(rfcomm_url, host_dispatcher.clone()).await;
        assert!(component.is_ok());
        let component = component.ok();

        // We expect the launched RFCOMM component to connect to the Profile service - this should
        // be relayed to the `host_dispatcher` and then to the Test Host.
        match host_server.try_next().await {
            Ok(Some(HostRequest::RequestProfile { .. })) => {}
            x => panic!("Expected Profile Request but got: {:?}", x),
        }

        // Simulate a new client connection - this should be relayed to the launched RFCOMM
        // component but _not_ the upstream Host Server.
        let (chan, _local) = zx::Channel::create().unwrap();
        relay_profile_channel(chan, &component, host_dispatcher.clone());

        // We don't expect the `chan` to be relayed to the host server.
        let host_fut = host_server.try_next();
        assert!(futures::poll!(host_fut).is_pending());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_launch_profile_forwarding_component_invalid_component() {
        let rfcomm_url = fuchsia_single_component_package_url!("nonexistent-package").to_string();
        let host_dispatcher = hd_test::make_simple_test_dispatcher().unwrap();

        let host_id = HostId(44);
        let mut host_server =
            hd_test::create_and_add_test_host_to_dispatcher(host_id, &host_dispatcher).unwrap();
        assert!(host_is_in_dispatcher(&host_id, &host_dispatcher).await);
        let component =
            launch_profile_forwarding_component(rfcomm_url, host_dispatcher.clone()).await;
        assert!(component.is_err());

        // No component should be launched - therefore no clients of the `host_server`.
        let host_fut = host_server.try_next();
        assert!(futures::poll!(host_fut).is_pending());
    }

    /// Tests that an incoming Profile `channel` is relayed directly to the HostDispatcher in
    /// the event that we don't have a launched profile forwarding component.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_relay_channel_without_component_is_relayed_to_host() {
        let host_dispatcher = hd_test::make_simple_test_dispatcher().unwrap();

        let host_id = HostId(46);
        let mut host_server =
            hd_test::create_and_add_test_host_to_dispatcher(host_id, &host_dispatcher).unwrap();
        assert!(host_is_in_dispatcher(&host_id, &host_dispatcher).await);

        // Relay the channel - we have no component.
        let component = None;
        let (chan, _local) = zx::Channel::create().unwrap();
        relay_profile_channel(chan, &component, host_dispatcher.clone());

        // We expect the `chan` to be relayed to the `host_dispatcher` (which then forwards
        // to the `host_server`) since the `component` is not set.
        match host_server.try_next().await {
            Ok(Some(HostRequest::RequestProfile { .. })) => {}
            x => panic!("Expected Profile Request but got: {:?}", x),
        }
    }
}

#[cfg(test)]
pub(crate) mod test {
    use super::*;

    use {
        fidl::endpoints,
        fidl_fuchsia_bluetooth_host::{HostMarker, HostRequestStream},
        fuchsia_bluetooth::inspect::placeholder_node,
        futures::future::join,
    };

    pub(crate) fn make_test_dispatcher(
        watch_peers_publisher: hanging_get::Publisher<HashMap<PeerId, Peer>>,
        watch_peers_registrar: hanging_get::SubscriptionRegistrar<PeerWatcher>,
        watch_hosts_publisher: hanging_get::Publisher<Vec<HostInfo>>,
        watch_hosts_registrar: hanging_get::SubscriptionRegistrar<sys::HostWatcherWatchResponder>,
    ) -> Result<HostDispatcher, Error> {
        let (gas_channel_sender, _ignored_gas_task_req_stream) = mpsc::channel(0);
        Ok(HostDispatcher::new(
            "test".to_string(),
            Appearance::Display,
            Stash::stub()?,
            placeholder_node(),
            gas_channel_sender,
            watch_peers_publisher,
            watch_peers_registrar,
            watch_hosts_publisher,
            watch_hosts_registrar,
        ))
    }

    pub(crate) fn make_simple_test_dispatcher() -> Result<HostDispatcher, Error> {
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

    pub(crate) fn create_and_add_test_host_to_dispatcher(
        id: HostId,
        dispatcher: &HostDispatcher,
    ) -> types::Result<HostRequestStream> {
        let (host_proxy, host_server) = endpoints::create_proxy_and_stream::<HostMarker>()?;
        let id_val = id.0 as u8;
        let address = Address::Public([id_val; 6]);
        let path = format!("/dev/host{}", id_val);
        let host_device = host_device::test::new_mock(id, address, Path::new(&path), host_proxy);
        dispatcher.add_test_host(id, Arc::new(RwLock::new(host_device)));
        Ok(host_server)
    }
}

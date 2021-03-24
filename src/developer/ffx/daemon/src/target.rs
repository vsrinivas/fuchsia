// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{
        FASTBOOT_CHECK_INTERVAL_SECS, FASTBOOT_DROP_GRACE_PERIOD_SECS,
        MDNS_BROADCAST_INTERVAL_SECS, MDNS_TARGET_DROP_GRACE_PERIOD_SECS,
    },
    crate::events::{DaemonEvent, TargetInfo},
    crate::fastboot::open_interface_with_serial,
    crate::logger::{streamer::DiagnosticsStreamer, Logger},
    crate::onet::HostPipeConnection,
    anyhow::{anyhow, bail, Context, Error, Result},
    async_std::{
        future::{timeout, TimeoutError},
        sync::RwLock,
    },
    async_trait::async_trait,
    bridge::DaemonError,
    chrono::{DateTime, Utc},
    ffx_daemon_core::events::{self, EventSynthesizer},
    ffx_daemon_core::net::IsLocalAddr,
    ffx_daemon_core::task::{SingleFlight, TaskSnapshot},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_remotecontrol::{
        IdentifyHostError, IdentifyHostResponse, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet},
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::Timer,
    futures::future,
    futures::lock::Mutex,
    futures::prelude::*,
    hoist::OvernetInstance,
    rand::random,
    std::cmp::Ordering,
    std::collections::{BTreeSet, HashMap, HashSet},
    std::default::Default,
    std::fmt,
    std::fmt::{Debug, Display},
    std::hash::{Hash, Hasher},
    std::io::Write,
    std::net::{IpAddr, SocketAddr, SocketAddrV4, SocketAddrV6},
    std::net::{Ipv4Addr, Ipv6Addr},
    std::sync::{Arc, Weak},
    std::time::Duration,
    usb_bulk::AsyncInterface as Interface,
};

pub use crate::target_task::*;

const IDENTIFY_HOST_TIMEOUT_MILLIS: u64 = 1000;

pub trait SshFormatter {
    fn ssh_fmt<W: Write>(&self, f: &mut W) -> std::io::Result<()>;
}

impl SshFormatter for TargetAddr {
    fn ssh_fmt<W: Write>(&self, f: &mut W) -> std::io::Result<()> {
        if self.ip.is_ipv6() {
            write!(f, "[")?;
        }
        write!(f, "{}", self)?;
        if self.ip.is_ipv6() {
            write!(f, "]")?;
        }
        Ok(())
    }
}

/// A trait for returning a consistent SSH address.
///
/// Based on the structure from which the SSH address is coming, this will
/// return in order of priority:
/// -- The first local IPv6 address with a scope id.
/// -- The last local IPv4 address.
/// -- Any other address.
pub trait SshAddrFetcher {
    fn to_ssh_addr(self) -> Option<TargetAddr>;
}

impl<'a, T: Copy + IntoIterator<Item = &'a TargetAddr>> SshAddrFetcher for &'a T {
    fn to_ssh_addr(self) -> Option<TargetAddr> {
        let mut res: Option<TargetAddr> = None;
        for addr in self.into_iter() {
            let is_valid_local_addr = addr.ip().is_local_addr()
                && (addr.ip().is_ipv4() || !(addr.ip().is_link_local_addr() && addr.scope_id == 0));

            if res.is_none() || is_valid_local_addr {
                res.replace(addr.clone());
            }
            if addr.ip().is_ipv6() && is_valid_local_addr {
                res.replace(addr.clone());
                break;
            }
        }
        res
    }
}

#[async_trait]
pub trait ToFidlTarget {
    async fn to_fidl_target(self) -> bridge::Target;
}

#[derive(Debug, Clone)]
pub struct RcsConnection {
    pub proxy: RemoteControlProxy,
    pub overnet_id: NodeId,
}

impl Hash for RcsConnection {
    fn hash<H>(&self, state: &mut H)
    where
        H: Hasher,
    {
        self.overnet_id.id.hash(state)
    }
}

impl PartialEq for RcsConnection {
    fn eq(&self, other: &Self) -> bool {
        self.overnet_id == other.overnet_id
    }
}

impl Eq for RcsConnection {}

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum TargetEvent {
    RcsActivated,
    Rediscovered,

    /// LHS is previous state, RHS is current state.
    ConnectionStateChanged(ConnectionState, ConnectionState),
}

#[derive(Debug)]
pub enum RcsConnectionError {
    /// There is something wrong with the FIDL connection.
    FidlConnectionError(fidl::Error),
    /// There was a timeout trying to communicate with RCS.
    ConnectionTimeoutError(TimeoutError),
    /// There is an error from within Rcs itself.
    RemoteControlError(IdentifyHostError),

    /// There is an error with the output from Rcs.
    TargetError(Error),
}

impl Display for RcsConnectionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RcsConnectionError::FidlConnectionError(ferr) => {
                write!(f, "fidl connection error: {}", ferr)
            }
            RcsConnectionError::ConnectionTimeoutError(_) => write!(f, "timeout error"),
            RcsConnectionError::RemoteControlError(ierr) => write!(f, "internal error: {:?}", ierr),
            RcsConnectionError::TargetError(error) => write!(f, "general error: {}", error),
        }
    }
}

impl RcsConnection {
    pub async fn new(id: &mut NodeId) -> Result<Self> {
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        let _result = RcsConnection::connect_to_service(id, s)?;
        let proxy = RemoteControlProxy::new(
            fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?,
        );

        Ok(Self { proxy, overnet_id: id.clone() })
    }

    pub fn copy_to_channel(&mut self, channel: fidl::Channel) -> Result<()> {
        RcsConnection::connect_to_service(&mut self.overnet_id, channel)
    }

    fn connect_to_service(overnet_id: &mut NodeId, channel: fidl::Channel) -> Result<()> {
        let svc = hoist::hoist().connect_as_service_consumer()?;
        svc.connect_to_service(overnet_id, RemoteControlMarker::NAME, channel)
            .map_err(|e| anyhow!("Error connecting to Rcs: {}", e))
    }

    // For testing.
    #[cfg(test)]
    pub fn new_with_proxy(proxy: RemoteControlProxy, id: &NodeId) -> Self {
        Self { proxy, overnet_id: id.clone() }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum ConnectionState {
    /// Default state: no connection.
    Disconnected,
    /// Contains the last known ping from mDNS.
    Mdns(DateTime<Utc>),
    /// Contains an actual connection to RCS.
    Rcs(RcsConnection),
    /// Target was manually added. Same as `Disconnected` but indicates that the target's name is
    /// wrong as well.
    Manual,
    /// Contains the last known interface update with a Fastboot serial number.
    Fastboot(DateTime<Utc>),
}

impl Default for ConnectionState {
    fn default() -> Self {
        ConnectionState::Disconnected
    }
}

impl ConnectionState {
    fn is_connected(&self) -> bool {
        match self {
            Self::Disconnected => false,
            _ => true,
        }
    }

    fn take_rcs(&mut self) -> Option<RcsConnection> {
        if self.is_rcs() {
            match std::mem::replace(self, ConnectionState::Disconnected) {
                ConnectionState::Rcs(r) => Some(r),
                _ => None,
            }
        } else {
            None
        }
    }

    fn is_rcs(&self) -> bool {
        match self {
            ConnectionState::Rcs(_) => true,
            _ => false,
        }
    }
}

#[derive(Debug, Clone, Hash)]
pub(crate) struct TargetAddrEntry {
    addr: TargetAddr,
    timestamp: DateTime<Utc>,
}

impl PartialEq for TargetAddrEntry {
    fn eq(&self, other: &Self) -> bool {
        self.addr.eq(&other.addr)
    }
}

impl Eq for TargetAddrEntry {}

impl From<(TargetAddr, DateTime<Utc>)> for TargetAddrEntry {
    fn from(t: (TargetAddr, DateTime<Utc>)) -> Self {
        let (addr, timestamp) = t;
        Self { addr, timestamp }
    }
}

impl From<TargetAddr> for TargetAddrEntry {
    fn from(addr: TargetAddr) -> Self {
        Self { addr, timestamp: Utc::now() }
    }
}

impl Ord for TargetAddrEntry {
    fn cmp(&self, other: &Self) -> Ordering {
        self.addr.cmp(&other.addr)
    }
}

impl PartialOrd for TargetAddrEntry {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Debug, Default, Clone)]
#[cfg_attr(test, derive(PartialEq, Eq))]
pub struct TargetState {
    pub connection_state: ConnectionState,
}

struct TargetInner {
    // id is the locally created "primary identifier" for this target.
    id: u64,
    // ids keeps track of additional ids discovered over Overnet, these could
    // come from old Daemons, or other Daemons. The set should be used
    ids: Mutex<HashSet<u64>>,
    nodename: Mutex<Option<String>>,
    state: Mutex<TargetState>,
    last_response: RwLock<DateTime<Utc>>,
    addrs: RwLock<BTreeSet<TargetAddrEntry>>,
    // ssh_port if set overrides the global default configuration for ssh port,
    // for this target.
    ssh_port: Mutex<Option<u16>>,
    // used for Fastboot
    serial: RwLock<Option<String>>,
    boot_timestamp_nanos: RwLock<Option<u64>>,
    diagnostics_info: Arc<DiagnosticsStreamer<'static>>,
}

impl Debug for TargetInner {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        smol::block_on(async {
            f.debug_struct("TargetInner")
                .field("id", &self.id)
                .field("ids", &self.ids.lock().await.clone())
                .field("nodename", &self.nodename.lock().await.clone())
                .field("state", &self.state.lock().await.clone())
                .field("last_response", &self.last_response.read().await.clone())
                .field("addrs", &self.addrs.read().await.clone())
                .field("ssh_port", &self.ssh_port.lock().await.clone())
                .field("serial", &self.serial.read().await.clone())
                .field("boot_timestamp_nanos", &self.boot_timestamp_nanos.read().await.clone())
                .finish()
        })
    }
}

impl TargetInner {
    fn new(nodename: Option<String>) -> Self {
        let id = random::<u64>();
        let mut ids = HashSet::new();
        ids.insert(id.clone());
        Self {
            id: id.clone(),
            ids: Mutex::new(ids),
            nodename: Mutex::new(nodename),
            last_response: RwLock::new(Utc::now()),
            state: Mutex::new(TargetState::default()),
            addrs: RwLock::new(BTreeSet::new()),
            ssh_port: Mutex::new(None),
            serial: RwLock::new(None),
            boot_timestamp_nanos: RwLock::new(None),
            diagnostics_info: Arc::new(DiagnosticsStreamer::default()),
        }
    }

    pub fn new_with_boot_timestamp(nodename: String, boot_timestamp_nanos: u64) -> Self {
        Self {
            boot_timestamp_nanos: RwLock::new(Some(boot_timestamp_nanos)),
            ..Self::new(Some(nodename))
        }
    }

    pub fn new_with_addrs(nodename: Option<String>, addrs: BTreeSet<TargetAddr>) -> Self {
        Self {
            addrs: RwLock::new(addrs.iter().map(|e| (*e, Utc::now()).into()).collect()),
            ..Self::new(nodename)
        }
    }

    pub fn new_with_serial(nodename: Option<String>, serial: &str) -> Self {
        Self { serial: RwLock::new(Some(serial.to_string())), ..Self::new(nodename) }
    }

    pub fn id(&self) -> u64 {
        self.id.clone()
    }

    pub async fn ids(&self) -> HashSet<u64> {
        self.ids.lock().await.clone()
    }

    pub async fn has_id<'a, I>(&self, ids: I) -> bool
    where
        I: Iterator<Item = &'a u64>,
    {
        let my_ids = self.ids.lock().await;
        for id in ids {
            if my_ids.contains(id) {
                return true;
            }
        }
        false
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time(nodename: String, time: DateTime<Utc>) -> Self {
        Self { last_response: RwLock::new(time), ..Self::new(Some(nodename)) }
    }

    pub async fn nodename_str(&self) -> String {
        self.nodename.lock().await.clone().unwrap_or("<unknown>".to_owned())
    }
}

#[async_trait]
impl EventSynthesizer<TargetEvent> for TargetInner {
    async fn synthesize_events(&self) -> Vec<TargetEvent> {
        match self.state.lock().await.connection_state {
            ConnectionState::Rcs(_) => vec![TargetEvent::RcsActivated],
            _ => vec![],
        }
    }
}

#[derive(Clone)]
pub struct WeakTarget {
    pub events: events::Queue<TargetEvent>,
    inner: Weak<TargetInner>,
}

impl WeakTarget {
    /// attempts to upgrade to a target with a null task manager.
    pub fn upgrade(&self) -> Option<Target> {
        let inner = self.inner.upgrade()?;
        let events = self.events.clone();
        Some(Target {
            inner,
            events,
            task_manager: Arc::new(SingleFlight::new(|_| futures::future::ready(Ok(())).boxed())),
        })
    }
}

#[derive(Clone)]
pub struct Target {
    pub events: events::Queue<TargetEvent>,

    // TODO(awdavies): This shouldn't need to be behind an Arc<>, but for some
    // reason (probably something to do with the merge_insert function in the
    // TargetCollection struct?) this will drop all tasks immediately if this
    // isn't an Arc<>.
    pub task_manager: Arc<SingleFlight<TargetTaskType, Result<(), String>>>,

    inner: Arc<TargetInner>,
}

impl Target {
    async fn mdns_monitor_loop(weak_target: WeakTarget, limit: Duration) -> Result<(), String> {
        let limit = chrono::Duration::from_std(limit).map_err(|e| format!("{:?}", e))?;
        loop {
            if let Some(t) = weak_target.upgrade() {
                let nodename = t.nodename_str().await;
                t.update_connection_state(|s| match s {
                    ConnectionState::Mdns(ref time) => {
                        let now = Utc::now();
                        if now.signed_duration_since(*time) > limit {
                            log::debug!(
                                "dropping target '{}'. MDNS response older than {}",
                                nodename,
                                limit,
                            );
                            ConnectionState::Disconnected
                        } else {
                            s
                        }
                    }
                    _ => s,
                })
                .await;
                Timer::new(Duration::from_secs(1)).await;
            } else {
                log::debug!("parent target dropped in mdns monitor loop. exiting");
                break;
            }
        }
        Ok(())
    }

    async fn fastboot_monitor_loop(weak_target: WeakTarget, limit: Duration) -> Result<(), String> {
        let limit = chrono::Duration::from_std(limit).map_err(|e| format!("{:?}", e))?;
        loop {
            if let Some(t) = weak_target.upgrade() {
                let nodename = t.nodename_str().await;
                t.update_connection_state(|s| match s {
                    ConnectionState::Fastboot(ref time) => {
                        let now = Utc::now();
                        if now.signed_duration_since(*time) > limit {
                            log::debug!(
                                "dropping target '{}'. fastboot state older than {}",
                                nodename,
                                limit
                            );
                            ConnectionState::Disconnected
                        } else {
                            s
                        }
                    }
                    _ => s,
                })
                .await;
                Timer::new(Duration::from_secs(1)).await;
            } else {
                log::debug!("parent target dropped in serial monitor loop. exiting");
                break;
            }
        }
        Ok(())
    }

    pub async fn is_connected(&self) -> bool {
        self.inner.state.lock().await.connection_state.is_connected()
    }

    pub fn downgrade(&self) -> WeakTarget {
        WeakTarget { events: self.events.clone(), inner: Arc::downgrade(&self.inner) }
    }

    fn from_inner(inner: Arc<TargetInner>) -> Self {
        let events = events::Queue::new(&inner);
        let weak_inner = Arc::downgrade(&inner);
        let weak_target = WeakTarget { inner: weak_inner, events: events.clone() };
        let task_manager = Arc::new(SingleFlight::new(move |t| match t {
            TargetTaskType::HostPipe => HostPipeConnection::new(weak_target.clone()).boxed(),
            TargetTaskType::MdnsMonitor => Target::mdns_monitor_loop(
                weak_target.clone(),
                Duration::from_secs(
                    MDNS_BROADCAST_INTERVAL_SECS + MDNS_TARGET_DROP_GRACE_PERIOD_SECS,
                ),
            )
            .boxed(),
            TargetTaskType::ProactiveLog => Logger::new(weak_target.clone()).start().boxed(),
            TargetTaskType::FastbootMonitor => Target::fastboot_monitor_loop(
                weak_target.clone(),
                Duration::from_secs(FASTBOOT_CHECK_INTERVAL_SECS + FASTBOOT_DROP_GRACE_PERIOD_SECS),
            )
            .boxed(),
        }));
        Self { inner, events, task_manager }
    }

    pub fn new<S>(nodename: S) -> Self
    where
        S: Into<String>,
    {
        let inner = Arc::new(TargetInner::new(Some(nodename.into())));
        Self::from_inner(inner)
    }

    pub fn new_with_boot_timestamp<S>(nodename: S, boot_timestamp_nanos: u64) -> Self
    where
        S: Into<String>,
    {
        let inner =
            Arc::new(TargetInner::new_with_boot_timestamp(nodename.into(), boot_timestamp_nanos));
        Self::from_inner(inner)
    }

    pub fn new_with_addrs<S>(nodename: Option<S>, addrs: BTreeSet<TargetAddr>) -> Self
    where
        S: Into<String>,
    {
        let inner = Arc::new(TargetInner::new_with_addrs(nodename.map(Into::into), addrs));
        Self::from_inner(inner)
    }

    pub fn new_with_serial<S>(nodename: Option<S>, serial: &str) -> Self
    where
        S: Into<String>,
    {
        let inner = Arc::new(TargetInner::new_with_serial(nodename.map(Into::into), serial));
        Self::from_inner(inner)
    }

    pub fn from_target_info(mut t: TargetInfo) -> Self {
        if let Some(s) = t.serial {
            Self::new_with_serial(t.nodename, &s)
        } else {
            Self::new_with_addrs(t.nodename, t.addresses.drain(..).collect())
        }
    }

    pub async fn target_info(&self) -> TargetInfo {
        TargetInfo {
            nodename: self.nodename().await,
            addresses: self.addrs().await,
            serial: self.serial().await,
            ssh_port: self.ssh_port().await,
        }
    }

    // Get the locally minted identifier for the target
    pub fn id(&self) -> u64 {
        self.inner.id()
    }

    // Get all known ids for the target
    pub async fn ids(&self) -> HashSet<u64> {
        self.inner.ids().await
    }

    pub async fn has_id<'a, I>(&self, ids: I) -> bool
    where
        I: Iterator<Item = &'a u64>,
    {
        self.inner.has_id(ids).await
    }

    pub async fn merge_ids<'a, I>(&self, new_ids: I)
    where
        I: Iterator<Item = &'a u64>,
    {
        let mut my_ids = self.inner.ids.lock().await;
        for id in new_ids {
            my_ids.insert(*id);
        }
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time<S: Into<String>>(nodename: S, time: DateTime<Utc>) -> Self {
        let inner = Arc::new(TargetInner::new_with_time(nodename.into(), time));
        Self::from_inner(inner)
    }

    async fn rcs_state(&self) -> bridge::RemoteControlState {
        let loop_running = self.task_manager.task_snapshot(TargetTaskType::HostPipe).await
            == TaskSnapshot::Running;
        let state = self.inner.state.lock().await;
        match (loop_running, &state.connection_state) {
            (true, ConnectionState::Rcs(_)) => bridge::RemoteControlState::Up,
            (true, _) => bridge::RemoteControlState::Down,
            (_, _) => bridge::RemoteControlState::Unknown,
        }
    }

    pub async fn nodename(&self) -> Option<String> {
        self.inner.nodename.lock().await.clone()
    }

    pub async fn nodename_str(&self) -> String {
        self.inner.nodename_str().await
    }

    pub async fn boot_timestamp_nanos(&self) -> Option<u64> {
        self.inner.boot_timestamp_nanos.read().await.clone()
    }

    pub async fn update_boot_timestamp(&self, ts: Option<u64>) {
        let mut inner_ts = self.inner.boot_timestamp_nanos.write().await;
        *inner_ts = ts;
    }

    pub fn stream_info(&self) -> Arc<DiagnosticsStreamer<'static>> {
        self.inner.diagnostics_info.clone()
    }

    pub async fn serial(&self) -> Option<String> {
        self.inner.serial.read().await.clone()
    }

    pub async fn get_connection_state(&self) -> ConnectionState {
        let state = self.inner.state.lock().await;
        state.connection_state.clone()
    }

    /// Allows a client to atomically update the connection state based on what
    /// is returned from the predicate.
    ///
    /// For example, if the client wants to update the connection state to RCS
    /// connected, but only if the target is already disconnected, it would
    /// look like this:
    ///
    /// ```rust
    ///   let rcs_connection = ...;
    ///   target.update_connection_state(move |s| {
    ///     match s {
    ///       ConnectionState::Disconnected => ConnectionState::Rcs(rcs_connection),
    ///       _ => s
    ///     }
    ///   }).await;
    /// ```
    ///
    /// The client must always return the state, as this is swapped with the
    /// current target state in-place.
    ///
    /// If the state changes, this will push a `ConnectionStateChanged` event
    /// to the event queue.
    pub async fn update_connection_state<F>(&self, func: F)
    where
        F: FnOnce(ConnectionState) -> ConnectionState + Sized + Send,
    {
        let mut state = self.inner.state.lock().await;
        let former_state = state.connection_state.clone();
        let update =
            (func)(std::mem::replace(&mut state.connection_state, ConnectionState::Disconnected));
        state.connection_state = update;
        if former_state != state.connection_state {
            let _ = self
                .events
                .push(TargetEvent::ConnectionStateChanged(
                    former_state,
                    state.connection_state.clone(),
                ))
                .await;
        }
    }

    pub async fn rcs(&self) -> Option<RcsConnection> {
        match &self.inner.state.lock().await.connection_state {
            ConnectionState::Rcs(conn) => Some(conn.clone()),
            _ => None,
        }
    }

    pub async fn usb(&self) -> (String, Option<Interface>) {
        match self.inner.serial.read().await.as_ref() {
            Some(s) => (s.to_string(), open_interface_with_serial(s).ok()),
            None => ("".to_string(), None),
        }
    }

    pub async fn last_response(&self) -> DateTime<Utc> {
        self.inner.last_response.read().await.clone()
    }

    pub async fn addrs(&self) -> Vec<TargetAddr> {
        let mut addrs = self.inner.addrs.read().await.iter().cloned().collect::<Vec<_>>();
        addrs.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));
        addrs.drain(..).map(|e| e.addr).collect()
    }

    pub async fn drop_unscoped_link_local_addrs(&self) {
        let mut addrs = self.inner.addrs.write().await;

        *addrs = addrs
            .clone()
            .into_iter()
            .filter(|entry| {
                if let IpAddr::V6(v) = &entry.addr.ip {
                    entry.addr.scope_id != 0 || !v.is_link_local_addr()
                } else {
                    true
                }
            })
            .collect();
    }

    pub async fn ssh_port(&self) -> Option<u16> {
        self.inner.ssh_port.lock().await.clone()
    }

    pub(crate) async fn set_ssh_port(&self, port: Option<u16>) {
        if let Some(port) = port {
            self.inner.ssh_port.lock().await.replace(port);
        } else {
            self.inner.ssh_port.lock().await.take();
        }
    }

    #[cfg(test)]
    pub(crate) async fn addrs_insert(&self, t: TargetAddr) {
        self.inner.addrs.write().await.replace(t.into());
    }

    #[cfg(test)]
    pub async fn new_autoconnected(n: &str) -> Self {
        let s = Self::new(n);
        s.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(Utc::now())
        })
        .await;
        s
    }

    #[cfg(test)]
    pub(crate) async fn addrs_insert_entry(&self, t: TargetAddrEntry) {
        self.inner.addrs.write().await.replace(t);
    }

    async fn addrs_extend<T>(&self, new_addrs: T)
    where
        T: IntoIterator<Item = TargetAddr>,
    {
        let now = Utc::now();
        let mut addrs = self.inner.addrs.write().await;

        for mut addr in new_addrs.into_iter() {
            // Do not add localhost to the collection during extend.
            // Note: localhost addresses are added sometimes by direct
            // insertion, in the manual add case.
            let localhost_v4 = IpAddr::V4(Ipv4Addr::LOCALHOST);
            let localhost_v6 = IpAddr::V6(Ipv6Addr::LOCALHOST);
            if addr.ip() == localhost_v4 || addr.ip() == localhost_v6 {
                continue;
            }

            // Subtle:
            // Some sources of addresses can not be scoped, such as those which come from queries
            // over Overnet.
            // Link-local IPv6 addresses require scopes in order to be routable, and mdns events will
            // provide us with valid scopes. As such, if an incoming address is not scoped, try to
            // find an existing address entry with a scope, and carry the scope forward.
            // If the incoming address has a scope, it is likely to be more recent than one that was
            // originally present, for example if a directly connected USB target has restarted,
            // wherein the scopeid could be incremented due to the device being given a new
            // interface id allocation.
            if addr.ip().is_ipv6() && addr.scope_id == 0 {
                if let Some(entry) = addrs.get(&(addr, now.clone()).into()) {
                    addr.scope_id = entry.addr.scope_id;
                }

                // Note: not adding ipv6 link-local addresses without scopes here is deliberate!
                if addr.ip().is_link_local_addr() && addr.scope_id == 0 {
                    continue;
                }
            }
            addrs.replace((addr, now.clone()).into());
        }
    }

    async fn update_last_response(&self, other: DateTime<Utc>) {
        let mut last_response = self.inner.last_response.write().await;
        if *last_response < other {
            *last_response = other;
        }
    }

    async fn overwrite_state(&self, mut other: TargetState) {
        let mut state = self.inner.state.lock().await;
        if let Some(rcs) = other.connection_state.take_rcs() {
            // Nots this so that we know to push an event.
            let rcs_activated = !state.connection_state.is_rcs();
            state.connection_state = ConnectionState::Rcs(rcs);
            if rcs_activated {
                self.events.push(TargetEvent::RcsActivated).await.unwrap_or_else(|err| {
                    log::warn!("unable to enqueue RCS activation event: {:#}", err)
                });
            }
        }
    }

    pub async fn from_identify(identify: IdentifyHostResponse) -> Result<Self, Error> {
        // TODO(raggi): allow targets to truly be created without a nodename.
        let nodename = match identify.nodename {
            Some(n) => n,
            None => bail!("Target identification missing a nodename: {:?}", identify),
        };

        let target = Target::new(nodename);
        target.update_last_response(Utc::now().into()).await;
        if let Some(ids) = identify.ids {
            target.merge_ids(ids.iter()).await;
        }
        if let Some(t) = identify.boot_timestamp_nanos {
            target.inner.boot_timestamp_nanos.write().await.replace(t);
        }
        if let Some(addrs) = identify.addresses {
            let mut taddrs = target.inner.addrs.write().await;
            for addr in addrs.iter().map(|addr| TargetAddr::from(addr.clone())) {
                taddrs.insert(addr.into());
            }
        }
        if let Some(serial) = identify.serial_number {
            target.inner.serial.write().await.replace(serial);
        }
        Ok(target)
    }

    pub async fn from_rcs_connection(rcs: RcsConnection) -> Result<Self, RcsConnectionError> {
        let identify_result =
            timeout(Duration::from_millis(IDENTIFY_HOST_TIMEOUT_MILLIS), rcs.proxy.identify_host())
                .await
                .map_err(|e| RcsConnectionError::ConnectionTimeoutError(e))?;

        let identify = match identify_result {
            Ok(res) => match res {
                Ok(target) => target,
                Err(e) => return Err(RcsConnectionError::RemoteControlError(e)),
            },
            Err(e) => return Err(RcsConnectionError::FidlConnectionError(e)),
        };

        let target = Target::from_identify(identify)
            .await
            .map_err(|e| RcsConnectionError::TargetError(e))?;
        target.update_connection_state(move |_| ConnectionState::Rcs(rcs)).await;
        Ok(target)
    }

    pub async fn run_host_pipe(&self) {
        self.task_manager.spawn_detached(TargetTaskType::HostPipe).await
    }

    pub async fn run_mdns_monitor(&self) {
        self.task_manager.spawn_detached(TargetTaskType::MdnsMonitor).await;
    }

    pub async fn run_fastboot_monitor(&self) {
        self.task_manager.spawn_detached(TargetTaskType::FastbootMonitor).await;
    }

    pub async fn run_logger(&self) {
        self.task_manager.spawn_detached(TargetTaskType::ProactiveLog).await;
    }
}

#[async_trait]
impl EventSynthesizer<DaemonEvent> for Target {
    async fn synthesize_events(&self) -> Vec<DaemonEvent> {
        if self.inner.state.lock().await.connection_state.is_connected() {
            vec![DaemonEvent::NewTarget(self.target_info().await)]
        } else {
            vec![]
        }
    }
}

#[async_trait]
impl ToFidlTarget for Target {
    async fn to_fidl_target(self) -> bridge::Target {
        let (addrs, last_response, rcs_state) =
            futures::join!(self.addrs(), self.last_response(), self.rcs_state());

        bridge::Target {
            nodename: self.nodename().await,
            addresses: Some(addrs.iter().map(|a| a.into()).collect()),
            age_ms: Some(
                match Utc::now().signed_duration_since(last_response).num_milliseconds() {
                    dur if dur < 0 => {
                        log::trace!(
                            "negative duration encountered on target '{}': {}",
                            self.inner.nodename_str().await,
                            dur
                        );
                        0
                    }
                    dur => dur,
                } as u64,
            ),
            rcs_state: Some(rcs_state),

            // TODO(awdavies): Gather more information here when possible.
            target_type: Some(bridge::TargetType::Unknown),
            target_state: Some(bridge::TargetState::Unknown),
            ..bridge::Target::EMPTY
        }
    }
}

impl Debug for Target {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Target {{ {:?} }}", self.inner)
    }
}

// TODO(fxbug.dev/52733): Have `TargetAddr` support serial numbers.
#[derive(Hash, Clone, Debug, Copy, Eq, PartialEq)]
pub struct TargetAddr {
    ip: IpAddr,
    scope_id: u32,
}

impl Ord for TargetAddr {
    fn cmp(&self, other: &Self) -> Ordering {
        let this_socket = SocketAddr::from(self);
        let other_socket = SocketAddr::from(other);
        this_socket.cmp(&other_socket)
    }
}

impl PartialOrd for TargetAddr {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Into<bridge::TargetAddrInfo> for &TargetAddr {
    fn into(self) -> bridge::TargetAddrInfo {
        bridge::TargetAddrInfo::Ip(bridge::TargetIp {
            ip: match self.ip {
                IpAddr::V6(i) => IpAddress::Ipv6(Ipv6Address { addr: i.octets().into() }),
                IpAddr::V4(i) => IpAddress::Ipv4(Ipv4Address { addr: i.octets().into() }),
            },
            scope_id: self.scope_id,
        })
    }
}

impl Into<bridge::TargetAddrInfo> for TargetAddr {
    fn into(self) -> bridge::TargetAddrInfo {
        (&self).into()
    }
}

impl From<bridge::TargetAddrInfo> for TargetAddr {
    fn from(t: bridge::TargetAddrInfo) -> Self {
        (&t).into()
    }
}

impl From<&bridge::TargetAddrInfo> for TargetAddr {
    fn from(t: &bridge::TargetAddrInfo) -> Self {
        let (addr, scope): (IpAddr, u32) = match t {
            bridge::TargetAddrInfo::Ip(ip) => match ip.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => (addr.into(), ip.scope_id),
                IpAddress::Ipv4(Ipv4Address { addr }) => (addr.into(), ip.scope_id),
            },
            bridge::TargetAddrInfo::IpPort(ip) => match ip.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => (addr.into(), ip.scope_id),
                IpAddress::Ipv4(Ipv4Address { addr }) => (addr.into(), ip.scope_id),
            },
            // TODO(fxbug.dev/52733): Add serial numbers.,
        };

        (addr, scope).into()
    }
}

impl From<Subnet> for TargetAddr {
    fn from(i: Subnet) -> Self {
        // TODO(awdavies): Figure out if it's possible to get the scope_id from
        // this address.
        match i.addr {
            IpAddress::Ipv4(ip4) => SocketAddr::from((ip4.addr, 0)).into(),
            IpAddress::Ipv6(ip6) => SocketAddr::from((ip6.addr, 0)).into(),
        }
    }
}

impl From<TargetAddr> for SocketAddr {
    fn from(t: TargetAddr) -> Self {
        Self::from(&t)
    }
}

impl From<&TargetAddr> for SocketAddr {
    fn from(t: &TargetAddr) -> Self {
        match t.ip {
            IpAddr::V6(addr) => SocketAddr::V6(SocketAddrV6::new(addr, 0, 0, t.scope_id)),
            IpAddr::V4(addr) => SocketAddr::V4(SocketAddrV4::new(addr, 0)),
        }
    }
}

impl From<(IpAddr, u32)> for TargetAddr {
    fn from(f: (IpAddr, u32)) -> Self {
        Self { ip: f.0, scope_id: f.1 }
    }
}

impl From<SocketAddr> for TargetAddr {
    fn from(s: SocketAddr) -> Self {
        Self {
            ip: s.ip(),
            scope_id: match s {
                SocketAddr::V6(addr) => addr.scope_id(),
                _ => 0,
            },
        }
    }
}

impl TargetAddr {
    pub fn scope_id(&self) -> u32 {
        self.scope_id
    }

    pub fn ip(&self) -> IpAddr {
        self.ip.clone()
    }
}

impl Display for TargetAddr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.ip())?;

        if self.ip.is_link_local_addr() && self.scope_id() > 0 {
            let mut buf = vec![0; libc::IF_NAMESIZE];
            let res = unsafe {
                libc::if_indextoname(self.scope_id(), buf.as_mut_ptr() as *mut libc::c_char)
            };
            if res.is_null() {
                // TODO(awdavies): This will likely happen if the interface
                // is unplugged before being removed from the target cache.
                // There should be a clear error for the user indicating why
                // the interface name wasn't shown as a string.
                log::warn!(
                    "error getting interface name: {}",
                    nix::Error::from_errno(nix::errno::Errno::from_i32(nix::errno::errno())),
                );
                write!(f, "%{}", self.scope_id())?;
            } else {
                let string =
                    String::from_utf8_lossy(&buf.split(|&c| c == 0u8).next().unwrap_or(&[0u8]));
                if !string.is_empty() {
                    write!(f, "%{}", string)?;
                } else {
                    log::warn!("empty string for iface idx: {}", self.scope_id());
                }
            }
        }

        Ok(())
    }
}

#[async_trait]
pub trait MatchTarget {
    async fn match_target<TQ>(self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery> + Send;
}

// It's unclear why this definition has to exist, but the compiler complains
// if invoking this either directly on `iter()` or if invoking on
// `iter().into_iter()` about there being unmet trait constraints. With this
// definition there are no compilation complaints.
#[async_trait]
impl<'a> MatchTarget for std::slice::Iter<'_, &'a Target> {
    async fn match_target<TQ>(self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery> + Send,
    {
        let t: TargetQuery = t.into();
        for target in self {
            if t.matches(&target).await {
                return Some((*target).clone());
            }
        }
        None
    }
}

#[async_trait]
impl<'a, T: Iterator<Item = &'a Target> + Send> MatchTarget for &'a mut T {
    async fn match_target<TQ>(self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery> + Send,
    {
        let t: TargetQuery = t.into();
        for target in self {
            if t.matches(&target).await {
                return Some(target.clone());
            }
        }
        None
    }
}

#[derive(Debug)]
pub enum TargetQuery {
    /// Attempts to match the nodename, falling back to serial (in that order).
    NodenameOrSerial(String),
    AddrPort((TargetAddr, u16)),
    Addr(TargetAddr),
    First,
}

impl TargetQuery {
    pub fn match_info(&self, t: &TargetInfo) -> bool {
        match self {
            Self::NodenameOrSerial(arg) => {
                if let Some(ref nodename) = t.nodename {
                    if nodename.contains(arg) {
                        return true;
                    }
                }
                if let Some(ref serial) = t.serial {
                    if serial.contains(arg) {
                        return true;
                    }
                }
                false
            }
            Self::AddrPort((addr, port)) => {
                t.ssh_port == Some(*port) && Self::Addr(*addr).match_info(t)
            }
            Self::Addr(addr) => t.addresses.iter().any(|a| {
                // If the query does not contain a scope, allow a match against
                // only the IP.
                a == addr || addr.scope_id == 0 && a.ip == addr.ip
            }),
            Self::First => true,
        }
    }

    pub async fn matches(&self, t: &Target) -> bool {
        self.match_info(&t.target_info().await)
    }
}

impl<T> From<Option<T>> for TargetQuery
where
    T: Into<TargetQuery>,
{
    fn from(o: Option<T>) -> Self {
        o.map(Into::into).unwrap_or(Self::First)
    }
}

impl From<&str> for TargetQuery {
    fn from(s: &str) -> Self {
        String::from(s).into()
    }
}

impl From<String> for TargetQuery {
    /// If the string can be parsed as some kind of IP address, will attempt to
    /// match based on that, else fall back to the nodename or serial matches.
    fn from(s: String) -> Self {
        // TODO(raggi): add support for named scopes in the strings

        if let Ok(saddr) = s.parse::<SocketAddr>() {
            if saddr.port() == 0 {
                return Self::Addr(saddr.into());
            } else {
                let port = saddr.port();
                return Self::AddrPort((saddr.into(), port));
            }
        }

        match s.parse::<IpAddr>() {
            Ok(a) => Self::Addr(TargetAddr::from((a, 0))),
            Err(_) => Self::NodenameOrSerial(s),
        }
    }
}

impl From<TargetAddr> for TargetQuery {
    fn from(t: TargetAddr) -> Self {
        Self::Addr(t)
    }
}

struct TargetCollectionInner {
    named: HashMap<String, Target>,
    unnamed: Vec<Target>,
}

pub struct TargetCollection {
    inner: RwLock<TargetCollectionInner>,
    events: RwLock<Option<events::Queue<DaemonEvent>>>,
}

#[async_trait]
impl EventSynthesizer<DaemonEvent> for TargetCollection {
    async fn synthesize_events(&self) -> Vec<DaemonEvent> {
        let collection = self.inner.read().await;
        // TODO(awdavies): This won't be accurate once a target is able to create
        // more than one event at a time.
        let mut res = Vec::with_capacity(collection.named.len());
        for target in collection.named.values() {
            res.extend(target.synthesize_events().await);
        }
        for target in collection.unnamed.iter() {
            res.extend(target.synthesize_events().await);
        }
        res
    }
}

impl TargetCollection {
    pub fn new() -> Self {
        Self {
            inner: RwLock::new(TargetCollectionInner {
                named: HashMap::new(),
                unnamed: Vec::new(),
            }),
            events: RwLock::new(None),
        }
    }

    pub async fn set_event_queue(&self, q: events::Queue<DaemonEvent>) {
        // This should be the only place a write lock is ever held.
        self.events
            .write()
            .then(move |mut e| {
                *e = Some(q);
                future::ready(())
            })
            .await;
    }

    pub async fn targets(&self) -> Vec<Target> {
        let inner = self.inner.read().await;
        inner.named.values().chain(inner.unnamed.iter()).cloned().collect()
    }

    pub async fn remove_target(&self, target_id: String) -> bool {
        let mut inner = self.inner.write().await;

        if inner.named.remove(&target_id).is_none() {
            let mut found = None;
            'unnamed: for (index, target) in inner.unnamed.iter().enumerate() {
                for addr in target.addrs().await.into_iter() {
                    if format!("{}", addr.ip) == target_id {
                        found = Some(index);
                        break 'unnamed;
                    }
                }
            }

            if let Some(found) = found {
                inner.unnamed.remove(found);
                true
            } else {
                let mut found = None;
                'named: for (index, target) in inner.named.iter() {
                    for addr in target.addrs().await.into_iter() {
                        if format!("{}", addr.ip) == target_id {
                            found = Some(index.clone());
                            break 'named;
                        }
                    }
                }

                if let Some(found) = found {
                    inner.named.remove(&found);
                    true
                } else {
                    false
                }
            }
        } else {
            true
        }
    }

    pub async fn merge_insert(&self, new_target: Target) -> Target {
        let new_ids = new_target.ids().await;
        let new_nodename = new_target.nodename().await;
        let new_ips =
            new_target.addrs().await.iter().map(|addr| addr.ip.clone()).collect::<Vec<IpAddr>>();
        let new_port = new_target.ssh_port().await;

        let mut inner = self.inner.write().await;

        let mut unnamed_match = None;

        // Look for an unnamed match by index, so we can move it if found.
        for (i, unnamed_target) in inner.unnamed.iter().enumerate() {
            // Look for an unnamed target with a matching ID
            if unnamed_target.has_id(new_target.ids().await.iter()).await {
                unnamed_match = Some(i);
                break;
            }

            // Or a matching address
            if unnamed_target.addrs().await.iter().any(|addr| new_ips.contains(&addr.ip)) {
                unnamed_match = Some(i);
                break;
            }
        }

        // If an unnamed entry was found, attempt to name it, moving it to the named set.
        let mut to_update = if let Some(unnamed_match) = unnamed_match {
            if let Some(name) = new_target.inner.nodename.lock().await.clone() {
                let mergeable = inner.unnamed.remove(unnamed_match);
                *mergeable.inner.nodename.lock().await = Some(name.clone());
                inner.named.insert(name.clone(), mergeable);
                inner.named.get(&name)
            } else {
                Some(&inner.unnamed[unnamed_match])
            }
        } else {
            None
        };

        // If we haven't yet found a target, try to find one from the named set
        // by ID, nodename, or address.
        if to_update.is_none() {
            for (_, named_target) in inner.named.iter() {
                if named_target.has_id(new_ids.iter()).await
                    || new_nodename == named_target.nodename().await
                    || (
                        // Only match IP addresses IF the ssh port is the same (including un-set).
                        named_target.ssh_port().await == new_port
                            && named_target
                                .addrs()
                                .await
                                .iter()
                                .any(|addr| new_ips.contains(&addr.ip))
                    )
                {
                    to_update.replace(named_target);
                    break;
                }
            }
        }

        if let Some(to_update) = to_update {
            futures::join!(
                to_update.update_last_response(new_target.last_response().await),
                to_update.addrs_extend(new_target.addrs().await),
                to_update.update_boot_timestamp(new_target.boot_timestamp_nanos().await),
                to_update.overwrite_state(TargetState {
                    connection_state: std::mem::replace(
                        &mut new_target.inner.state.lock().await.connection_state,
                        ConnectionState::Disconnected
                    ),
                }),
            );

            to_update.events.push(TargetEvent::Rediscovered).await.unwrap_or_else(|err| {
                log::warn!("unable to enqueue rediscovered event: {:#}", err)
            });
            to_update.clone()
        } else {
            new_target.drop_unscoped_link_local_addrs().await;
            let result = new_target.clone();

            if let Some(name) = new_target.nodename().await {
                inner.named.insert(name, new_target);

                let info = result.target_info().await;
                if let Some(e) = self.events.read().await.as_ref() {
                    e.push(DaemonEvent::NewTarget(info))
                        .await
                        .unwrap_or_else(|e| log::warn!("unable to push new target event: {}", e));
                }
            } else {
                inner.unnamed.push(new_target);
            }

            log::info!(
                "New target ({}): {}",
                result.id(),
                result.nodename().await.unwrap_or("<unnamed>".to_string())
            );
            result
        }
    }

    /// wait_for_match attempts to find a target matching "matcher". If no
    /// matcher is provided, either the default target is matched, or, if there
    /// is no default a single target is returned iff it is the only target in
    /// the collection. If there is neither a matcher or a defualt, and there are
    /// several targets in the collection when the query starts, a
    /// DaemonError::TargetAmbiguous error is returned. The matcher is converted to a
    /// TargetQuery for matching, and follows the TargetQuery semantics.
    pub async fn wait_for_match(&self, matcher: Option<String>) -> Result<Target, DaemonError> {
        // If there is no matcher, and there are already multiple targets in the
        // target collection, we know that the target is ambiguous and thus
        // produce an actionable error to the user.
        if matcher.is_none() {
            // PERFORMANCE: it's possible to avoid the discarded clones here, with more work.
            if self.targets().await.len() > 1 {
                return Err(DaemonError::TargetAmbiguous);
            }
        }

        // If there's nothing to match against, unblock on the first target.
        let target_query = TargetQuery::from(matcher.clone());

        // Infinite timeout here is fine, as the client dropping connection
        // will lead to this being cleaned up eventually. It is the client's
        // responsibility to determine their respective timeout(s).
        self.events
            .read()
            .await
            .as_ref()
            .expect("target event queue must be initialized by now")
            .wait_for(None, move |e| {
                if let DaemonEvent::NewTarget(ref target_info) = e {
                    target_query.match_info(target_info)
                } else {
                    false
                }
            })
            .await
            .map_err(|e| {
                log::warn!("{}", e);
                DaemonError::TargetCacheError
            })?;

        // TODO(awdavies): It's possible something might happen between the new
        // target event and now, so it would make sense to give the
        // user some information on what happened: likely something
        // to do with the target suddenly being forced out of the cache
        // (this isn't a problem yet, but will be once more advanced
        // lifetime tracking is implemented). If a name isn't specified it's
        // possible a secondary/tertiary target showed up, and those cases are
        // handled here.
        self.get(matcher).await.ok_or(DaemonError::TargetNotFound)
    }

    /// Attempts to get a target based off of the default. Returns an error if
    /// there is no target available (this doesn't await a new target).
    ///
    /// Ignores any targets that do not have the state of "connected" at the
    /// time the command is invoked, so can cause some raciness.
    pub async fn get_default(&self, n: Option<String>) -> Result<Target, DaemonError> {
        // The "get the mapped targets for filtering connected ones" step has
        // to be separate from the actual `filter_map()` statement on account of
        // the compiler claiming a temporary value is borrowed whilst still in
        // use. It is unclear why this needs to be done in two statements, but
        // without it the compiler will complain.
        let targets = &self.inner.read().await.named;
        let targets = futures::future::join_all(
            targets
                .iter()
                .map(|(nodename, t)| async move {
                    (
                        nodename,
                        t.clone(),
                        t.inner.state.lock().await.connection_state.is_connected(),
                    )
                })
                .collect::<Vec<_>>(),
        )
        .await;
        let targets = targets
            .iter()
            .filter_map(|(_, t, connected)| if *connected { Some(t) } else { None })
            .collect::<Vec<_>>();
        match (targets.len(), n) {
            (0, None) => Err(DaemonError::TargetCacheEmpty),
            (1, None) => {
                let res = targets
                    .iter()
                    .next()
                    .ok_or(DaemonError::TargetCacheEmpty)
                    .map(|t| (*t).clone())?;
                log::debug!(
                    "No default target selected, returning only target - {:?}",
                    res.nodename().await,
                );
                Ok(res)
            }
            (_, None) => {
                // n > 1 case (0 and 1 are covered, and this is an unsigned integer).
                Err(DaemonError::TargetAmbiguous)
            }
            (_, Some(nodename)) => {
                targets.iter().match_target(nodename).await.ok_or(DaemonError::TargetNotFound)
            }
        }
    }

    pub async fn get_connected<TQ>(&self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery>,
    {
        let t: TargetQuery = t.into();
        let inner = self.inner.read().await;
        for target in inner.named.values().chain(inner.unnamed.iter()) {
            if target.inner.state.lock().await.connection_state.is_connected() {
                if t.matches(target).await {
                    return Some(target.clone());
                }
            }
        }

        None
    }

    pub async fn get<TQ>(&self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery>,
    {
        let t: TargetQuery = t.into();
        let inner = self.inner.read().await;
        inner.named.values().chain(inner.unnamed.iter()).match_target(t).await
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        chrono::offset::TimeZone,
        fidl, fidl_fuchsia_developer_remotecontrol as rcs,
        futures::{channel::mpsc, executor::block_on},
        std::net::{Ipv4Addr, Ipv6Addr},
    };

    async fn clone_target(t: &Target) -> Target {
        let inner = Arc::new(TargetInner::clone(&t.inner));
        Target::from_inner(inner)
    }

    impl Clone for TargetInner {
        fn clone(&self) -> Self {
            Self {
                id: self.id.clone(),
                ids: Mutex::new(block_on(self.ids.lock()).clone()),
                nodename: Mutex::new(block_on(self.nodename.lock()).clone()),
                last_response: RwLock::new(block_on(self.last_response.read()).clone()),
                state: Mutex::new(block_on(self.state.lock()).clone()),
                addrs: RwLock::new(block_on(self.addrs.read()).clone()),
                ssh_port: Mutex::new(block_on(self.ssh_port.lock()).clone()),
                serial: RwLock::new(block_on(self.serial.read()).clone()),
                boot_timestamp_nanos: RwLock::new(
                    block_on(self.boot_timestamp_nanos.read()).clone(),
                ),
                diagnostics_info: self.diagnostics_info.clone(),
            }
        }
    }

    fn fake_now() -> DateTime<Utc> {
        Utc.ymd(2014, 10, 31).and_hms(9, 10, 12)
    }

    fn fake_elapsed() -> DateTime<Utc> {
        Utc.ymd(2014, 11, 2).and_hms(13, 2, 1)
    }

    impl PartialEq for Target {
        fn eq(&self, o: &Target) -> bool {
            block_on(self.nodename()) == block_on(o.nodename())
                && *block_on(self.inner.last_response.read())
                    == *block_on(o.inner.last_response.read())
                && block_on(self.addrs()) == block_on(o.addrs())
                && *block_on(self.inner.state.lock()) == *block_on(o.inner.state.lock())
        }
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new_not_connected() {
        let tc = TargetCollection::new();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, fake_now());
        tc.merge_insert(clone_target(&t).await).await;
        let other_target = &tc.get(nodename.clone()).await.unwrap();
        assert_eq!(other_target, &t);
        match tc.get_connected(nodename.clone()).await {
            Some(_) => panic!("string lookup should return None"),
            _ => (),
        }
        let now = Utc::now();
        other_target
            .update_connection_state(|s| {
                assert_eq!(s, ConnectionState::Disconnected);
                ConnectionState::Mdns(now)
            })
            .await;
        t.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(now)
        })
        .await;
        assert_eq!(&tc.get_connected(nodename.clone()).await.unwrap(), &t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new() {
        let tc = TargetCollection::new();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, fake_now());
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(&tc.get(nodename.clone()).await.unwrap(), &t);
        match tc.get("oihaoih").await {
            Some(_) => panic!("string lookup should return None"),
            _ => (),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_merge() {
        let tc = TargetCollection::new();
        let nodename = String::from("bananas");
        let t1 = Target::new_with_time(&nodename, fake_now());
        let t2 = Target::new_with_time(&nodename, fake_elapsed());
        let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
        let a2 = IpAddr::V6(Ipv6Addr::new(
            0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
        ));
        t1.addrs_insert((a1.clone(), 1).into()).await;
        t2.addrs_insert((a2.clone(), 1).into()).await;
        tc.merge_insert(clone_target(&t2).await).await;
        tc.merge_insert(clone_target(&t1).await).await;
        let merged_target = tc.get(nodename.clone()).await.unwrap();
        assert_ne!(&merged_target, &t1);
        assert_ne!(&merged_target, &t2);
        assert_eq!(merged_target.addrs().await.len(), 2);
        assert_eq!(*merged_target.inner.last_response.read().await, fake_elapsed());
        assert!(merged_target.addrs().await.contains(&(a1, 1).into()));
        assert!(merged_target.addrs().await.contains(&(a2, 1).into()));

        // Insert another instance of the a2 address, but with a missing
        // scope_id, and ensure that the new address does not affect the address
        // collection.
        let t3 = Target::new_with_time(&nodename, fake_now());
        t3.addrs_insert((a2.clone(), 0).into()).await;
        tc.merge_insert(clone_target(&t3).await).await;
        let merged_target = tc.get(nodename.clone()).await.unwrap();
        assert_eq!(merged_target.addrs().await.len(), 2);

        // Insert another instance of the a2 address, but with a new scope_id, and ensure that the new scope is used.
        let t3 = Target::new_with_time(&nodename, fake_now());
        t3.addrs_insert((a2.clone(), 3).into()).await;
        tc.merge_insert(clone_target(&t3).await).await;
        let merged_target = tc.get(nodename.clone()).await.unwrap();
        assert_eq!(merged_target.addrs().await.iter().filter(|addr| addr.scope_id == 3).count(), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_no_scopeless_ipv6() {
        let tc = TargetCollection::new();
        let nodename = String::from("bananas");
        let t1 = Target::new_with_time(&nodename, fake_now());
        let t2 = Target::new_with_time(&nodename, fake_elapsed());
        let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
        let a2 = IpAddr::V6(Ipv6Addr::new(
            0xfe80, 0x0000, 0x0000, 0x0000, 0xb412, 0xb455, 0x1337, 0xfeed,
        ));
        t1.addrs_insert((a1.clone(), 0).into()).await;
        t2.addrs_insert((a2.clone(), 0).into()).await;
        tc.merge_insert(clone_target(&t2).await).await;
        tc.merge_insert(clone_target(&t1).await).await;
        let merged_target = tc.get(nodename.clone()).await.unwrap();
        assert_ne!(&merged_target, &t1);
        assert_ne!(&merged_target, &t2);
        assert_eq!(merged_target.addrs().await.len(), 1);
        assert_eq!(*merged_target.inner.last_response.read().await, fake_elapsed());
        assert!(merged_target.addrs().await.contains(&(a1, 0).into()));
        assert!(!merged_target.addrs().await.contains(&(a2, 0).into()));
    }

    fn setup_fake_remote_control_service(
        send_internal_error: bool,
        nodename_response: String,
    ) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    rcs::RemoteControlRequest::IdentifyHost { responder } => {
                        if send_internal_error {
                            let _ = responder
                                .send(&mut Err(rcs::IdentifyHostError::ListInterfacesFailed))
                                .context("sending testing error response")
                                .unwrap();
                        } else {
                            let result: Vec<Subnet> = vec![Subnet {
                                addr: IpAddress::Ipv4(Ipv4Address { addr: [192, 168, 0, 1] }),
                                prefix_len: 24,
                            }];
                            let nodename = if nodename_response.len() == 0 {
                                None
                            } else {
                                Some(nodename_response.clone())
                            };
                            responder
                                .send(&mut Ok(rcs::IdentifyHostResponse {
                                    nodename,
                                    addresses: Some(result),
                                    ..rcs::IdentifyHostResponse::EMPTY
                                }))
                                .context("sending testing response")
                                .unwrap();
                        }
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_from_rcs_connection_internal_err() {
        // TODO(awdavies): Do some form of PartialEq implementation for
        // the RcsConnectionError enum to avoid the nested matches.
        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(true, "foo".to_owned()),
            &NodeId { id: 123 },
        );
        match Target::from_rcs_connection(conn).await {
            Ok(_) => assert!(false),
            Err(e) => match e {
                RcsConnectionError::RemoteControlError(rce) => match rce {
                    rcs::IdentifyHostError::ListInterfacesFailed => (),
                    _ => assert!(false),
                },
                _ => assert!(false),
            },
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_from_rcs_connection_nodename_none() {
        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(false, "".to_owned()),
            &NodeId { id: 123456 },
        );
        match Target::from_rcs_connection(conn).await {
            Ok(_) => assert!(false),
            Err(e) => match e {
                RcsConnectionError::TargetError(_) => (),
                _ => assert!(false),
            },
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_from_rcs_connection_no_err() {
        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(false, "foo".to_owned()),
            &NodeId { id: 1234 },
        );
        match Target::from_rcs_connection(conn).await {
            Ok(t) => {
                assert_eq!(t.nodename().await.unwrap(), "foo".to_string());
                assert_eq!(t.rcs().await.unwrap().overnet_id.id, 1234u64);
                assert_eq!(t.addrs().await.len(), 1);
            }
            Err(_) => assert!(false),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_query_matches_nodename() {
        let query = TargetQuery::from("foo");
        let target = Arc::new(Target::new("foo"));
        assert!(query.matches(&target).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_by_addr() {
        let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
        let t = Target::new("foo");
        t.addrs_insert(addr.clone()).await;
        let tc = TargetCollection::new();
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get(addr).await.unwrap(), t);
        assert_eq!(tc.get("192.168.0.1").await.unwrap(), t);
        assert!(tc.get("fe80::dead:beef:beef:beef").await.is_none());

        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        let t = Target::new("fooberdoober");
        t.addrs_insert(addr.clone()).await;
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get("fe80::dead:beef:beef:beef").await.unwrap(), t);
        assert_eq!(tc.get(addr.clone()).await.unwrap(), t);
        assert_eq!(tc.get("fooberdoober").await.unwrap(), t);
    }

    // Most of this is now handled in `task.rs`
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_disconnect_multiple_invocations() {
        let t = Arc::new(Target::new("flabbadoobiedoo"));
        {
            let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
            t.addrs_insert(addr).await;
        }
        // Assures multiple "simultaneous" invocations to start the target
        // doesn't put it into a bad state that would hang.
        let _: ((), (), ()) =
            futures::join!(t.run_host_pipe(), t.run_host_pipe(), t.run_host_pipe());
    }

    struct RcsStateTest {
        loop_started: bool,
        rcs_is_some: bool,
        expected: bridge::RemoteControlState,
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_rcs_states() {
        for test in vec![
            RcsStateTest {
                loop_started: true,
                rcs_is_some: false,
                expected: bridge::RemoteControlState::Down,
            },
            RcsStateTest {
                loop_started: true,
                rcs_is_some: true,
                expected: bridge::RemoteControlState::Up,
            },
            RcsStateTest {
                loop_started: false,
                rcs_is_some: true,
                expected: bridge::RemoteControlState::Unknown,
            },
            RcsStateTest {
                loop_started: false,
                rcs_is_some: false,
                expected: bridge::RemoteControlState::Unknown,
            },
        ] {
            let t = Target::new("schlabbadoo");
            let a2 = IpAddr::V6(Ipv6Addr::new(
                0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
            ));
            t.addrs_insert((a2, 2).into()).await;
            if test.loop_started {
                t.run_host_pipe().await;
            }
            {
                *t.inner.state.lock().await = TargetState {
                    connection_state: if test.rcs_is_some {
                        ConnectionState::Rcs(RcsConnection::new_with_proxy(
                            setup_fake_remote_control_service(true, "foobiedoo".to_owned()),
                            &NodeId { id: 123 },
                        ))
                    } else {
                        ConnectionState::Disconnected
                    },
                };
            }
            assert_eq!(t.rcs_state().await, test.expected);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_to_fidl_target() {
        let t = Target::new("cragdune-the-impaler");
        let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
        let a2 = IpAddr::V6(Ipv6Addr::new(
            0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
        ));
        t.addrs_insert((a1, 1).into()).await;
        t.addrs_insert((a2, 1).into()).await;

        let t_conv = t.clone().to_fidl_target().await;
        assert_eq!(t.nodename().await.unwrap(), t_conv.nodename.unwrap().to_string());
        let addrs = t.addrs().await;
        let conv_addrs = t_conv.addresses.unwrap();
        assert_eq!(addrs.len(), conv_addrs.len());

        // Will crash if any addresses are missing.
        for address in conv_addrs {
            let address = TargetAddr::from(address);
            assert!(addrs.iter().any(|&a| a == address));
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_event_synthesis() {
        let t = Target::new("clopperdoop");
        let vec = t.synthesize_events().await;
        assert_eq!(vec.len(), 0);
        t.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(Utc::now())
        })
        .await;
        let vec = t.synthesize_events().await;
        assert_eq!(vec.len(), 1);
        assert_eq!(
            vec.iter().next().expect("events empty"),
            &DaemonEvent::NewTarget(TargetInfo {
                nodename: Some("clopperdoop".to_string()),
                ..Default::default()
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_event_synthesis_all_connected() {
        let t = Target::new_autoconnected("clam-chowder-is-tasty").await;
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel").await;
        let t3 = Target::new_autoconnected("i-should-probably-eat-lunch").await;
        let t4 = Target::new_autoconnected("i-should-probably-eat-lunch").await;
        let tc = TargetCollection::new();
        tc.merge_insert(t).await;
        tc.merge_insert(t2).await;
        tc.merge_insert(t3).await;
        tc.merge_insert(t4).await;

        let events = tc.synthesize_events().await;
        assert_eq!(events.len(), 3);
        assert!(events.iter().any(|e| e
            == &DaemonEvent::NewTarget(TargetInfo {
                nodename: Some("clam-chowder-is-tasty".to_string()),
                ..Default::default()
            })));
        assert!(events.iter().any(|e| e
            == &DaemonEvent::NewTarget(TargetInfo {
                nodename: Some("this-is-a-crunchy-falafel".to_string()),
                ..Default::default()
            })));
        assert!(events.iter().any(|e| e
            == &DaemonEvent::NewTarget(TargetInfo {
                nodename: Some("i-should-probably-eat-lunch".to_string()),
                ..Default::default()
            })));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_event_synthesis_none_connected() {
        let t = Target::new("clam-chowder-is-tasty");
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let t3 = Target::new("i-should-probably-eat-lunch");
        let t4 = Target::new("i-should-probably-eat-lunch");

        let tc = TargetCollection::new();
        tc.merge_insert(t).await;
        tc.merge_insert(t2).await;
        tc.merge_insert(t3).await;
        tc.merge_insert(t4).await;

        let events = tc.synthesize_events().await;
        assert_eq!(events.len(), 0);
    }

    struct EventPusher {
        got: mpsc::UnboundedSender<String>,
    }

    impl EventPusher {
        fn new() -> (Self, mpsc::UnboundedReceiver<String>) {
            let (got, rx) = mpsc::unbounded::<String>();
            (Self { got }, rx)
        }
    }

    #[async_trait]
    impl events::EventHandler<DaemonEvent> for EventPusher {
        async fn on_event(&self, event: DaemonEvent) -> Result<bool> {
            if let DaemonEvent::NewTarget(TargetInfo { nodename: Some(s), .. }) = event {
                self.got.unbounded_send(s).unwrap();
                Ok(false)
            } else {
                panic!("this should never receive any other kind of event");
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_events() {
        let t = Target::new("clam-chowder-is-tasty");
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let t3 = Target::new("i-should-probably-eat-lunch");

        let tc = Arc::new(TargetCollection::new());
        let queue = events::Queue::new(&tc);
        let (handler, rx) = EventPusher::new();
        queue.add_handler(handler).await;
        tc.set_event_queue(queue).await;
        tc.merge_insert(t).await;
        tc.merge_insert(t2).await;
        tc.merge_insert(t3).await;
        let results = rx.take(3).collect::<Vec<_>>().await;
        assert!(results.iter().any(|e| e == &"clam-chowder-is-tasty".to_string()));
        assert!(results.iter().any(|e| e == &"this-is-a-crunchy-falafel".to_string()));
        assert!(results.iter().any(|e| e == &"i-should-probably-eat-lunch".to_string()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_event_synthesis_wait() {
        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(false, "foo".to_owned()),
            &NodeId { id: 1234 },
        );
        let t = match Target::from_rcs_connection(conn).await {
            Ok(t) => {
                assert_eq!(t.nodename().await.unwrap(), "foo".to_string());
                assert_eq!(t.rcs().await.unwrap().overnet_id.id, 1234u64);
                assert_eq!(t.addrs().await.len(), 1);
                t
            }
            Err(_) => unimplemented!("this branch should never happen"),
        };
        // This will hang forever if no synthesis happens.
        t.events.wait_for(None, |e| e == TargetEvent::RcsActivated).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_event_fire() {
        let t = Target::new("balaowihf");
        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(false, "balaowihf".to_owned()),
            &NodeId { id: 1234 },
        );

        let fut = t.events.wait_for(None, |e| e == TargetEvent::RcsActivated);
        let mut new_state = TargetState::default();
        new_state.connection_state = ConnectionState::Rcs(conn);
        // This is a bit of a race, so it's possible that state will be
        // updated before the wait_for invocation is registered with the
        // event queue, but either way this should succeed.
        let (res, ()) = futures::join!(fut, t.overwrite_state(new_state));
        res.unwrap();
    }

    #[test]
    fn test_to_ssh_addr() {
        let sockets = vec![
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 0)),
            SocketAddr::V6(SocketAddrV6::new("f111::3".parse().unwrap(), 0, 0, 0)),
            SocketAddr::V6(SocketAddrV6::new("fe80::1".parse().unwrap(), 0, 0, 0)),
            SocketAddr::V6(SocketAddrV6::new("fe80::2".parse().unwrap(), 0, 0, 1)),
            SocketAddr::V6(SocketAddrV6::new("fe80::3".parse().unwrap(), 0, 0, 0)),
        ];
        let addrs = sockets.iter().map(|s| TargetAddr::from(*s)).collect::<Vec<_>>();
        assert_eq!((&addrs).to_ssh_addr(), Some(addrs[3]));

        let sockets = vec![
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 0)),
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(129, 0, 0, 1), 0)),
        ];
        let addrs = sockets.iter().map(|s| TargetAddr::from(*s)).collect::<Vec<_>>();
        assert_eq!((&addrs).to_ssh_addr(), Some(addrs[0]));

        let addrs = Vec::<TargetAddr>::new();
        assert_eq!((&addrs).to_ssh_addr(), None);
    }

    #[test]
    fn test_ssh_formatting() {
        struct SshFormatTest {
            addr: TargetAddr,
            expect: &'static str,
        }
        let tests_pre = vec![
            (SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 0)), "127.0.0.1"),
            (SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(129, 0, 0, 1), 0)), "129.0.0.1"),
            (SocketAddr::V6(SocketAddrV6::new("f111::3".parse().unwrap(), 0, 0, 0)), "[f111::3]"),
            (SocketAddr::V6(SocketAddrV6::new("fe80::1".parse().unwrap(), 0, 0, 0)), "[fe80::1]"),
            (
                SocketAddr::V6(SocketAddrV6::new("fe80::2".parse().unwrap(), 0, 0, 198)),
                "[fe80::2%198]",
            ),
        ];
        let tests = tests_pre
            .iter()
            .map(|t| SshFormatTest { addr: TargetAddr::from(t.0), expect: t.1 })
            .collect::<Vec<_>>();
        for test in tests.iter() {
            let mut res = Vec::<u8>::new();
            test.addr.ssh_fmt(&mut res).unwrap();
            assert_eq!(std::str::from_utf8(&res[..]).unwrap(), test.expect);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_get_default() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default).await;
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel").await;
        let tc = TargetCollection::new();
        assert!(tc.get_default(None).await.is_err());
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get_default(Some(default.to_string())).await.unwrap(), t);
        assert_eq!(tc.get_default(None).await.unwrap(), t);
        tc.merge_insert(t2).await;
        assert_eq!(tc.get_default(Some(default.to_string())).await.unwrap(), t);
        assert!(tc.get_default(None).await.is_err());
        assert!(tc.get_default(Some("not_in_here".to_owned())).await.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_get_default_matches_contains() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default).await;
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel").await;
        let tc = TargetCollection::new();
        assert!(tc.get_default(None).await.is_err());
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get_default(Some(default.to_string())).await.unwrap(), t);
        assert_eq!(tc.get_default(None).await.unwrap(), t);
        tc.merge_insert(t2).await;
        assert_eq!(tc.get_default(Some(default.to_string())).await.unwrap(), t);
        assert!(tc.get_default(None).await.is_err());
        assert!(tc.get_default(Some("not_in_here".to_owned())).await.is_err());
        assert_eq!(tc.get_default(Some("clam".to_string())).await.unwrap(), t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_update_connection_state() {
        let t = Target::new("have-you-seen-my-cat");
        let fake_time = Utc.yo(2017, 12).and_hms(1, 2, 3);
        let fake_time_clone = fake_time.clone();
        t.update_connection_state(move |s| {
            assert_eq!(s, ConnectionState::Disconnected);

            ConnectionState::Mdns(fake_time_clone)
        })
        .await;
        assert_eq!(ConnectionState::Mdns(fake_time), t.inner.state.lock().await.connection_state);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_mdns_set_disconnected() {
        let t = Target::new("yo-yo-ma-plays-that-cello-ya-hear");
        let now = Utc::now();
        t.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(now)
        })
        .await;
        let events = t.events.clone();
        let _task = fuchsia_async::Task::local(async move {
            Target::mdns_monitor_loop(t.downgrade(), Duration::from_secs(2))
                .await
                .expect("mdns monitor loop failed")
        });
        events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    ConnectionState::Mdns(now),
                    ConnectionState::Disconnected,
                )
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_fastboot_set_disconnected() {
        let t = Target::new("platypodes-are-venomous");
        let now = Utc::now();
        t.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Fastboot(now)
        })
        .await;
        let events = t.events.clone();
        let _task = fuchsia_async::Task::local(async move {
            Target::fastboot_monitor_loop(t.downgrade(), Duration::from_secs(2))
                .await
                .expect("mdns monitor loop failed")
        });
        events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    ConnectionState::Fastboot(now),
                    ConnectionState::Disconnected,
                )
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_addresses_order_preserved() {
        let t = Target::new("this-is-a-target-i-guess");
        let addrs_pre = vec![
            SocketAddr::V6(SocketAddrV6::new("fe80::1".parse().unwrap(), 0, 0, 0)),
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 0)),
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(129, 0, 0, 1), 0)),
            SocketAddr::V6(SocketAddrV6::new("f111::3".parse().unwrap(), 0, 0, 0)),
            SocketAddr::V6(SocketAddrV6::new("fe80::1".parse().unwrap(), 0, 0, 0)),
            SocketAddr::V6(SocketAddrV6::new("fe80::2".parse().unwrap(), 0, 0, 2)),
        ];
        let mut addrs_post = addrs_pre
            .iter()
            .cloned()
            .enumerate()
            .map(|(i, e)| {
                (TargetAddr::from(e), Utc.ymd(2014 + (i as i32), 10, 31).and_hms(9, 10, 12)).into()
            })
            .collect::<Vec<TargetAddrEntry>>();
        for a in addrs_post.iter().cloned() {
            t.addrs_insert_entry(a).await;
        }

        // Removes expected duplicate address. Should be marked as a duplicate
        // and also removed from the very beginning as a more-recent version
        // is added later.
        addrs_post.remove(0);
        // The order should be: last one inserted should show up first.
        addrs_post.reverse();
        assert_eq!(addrs_post.drain(..).map(|e| e.addr).collect::<Vec<_>>(), t.addrs().await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_addresses_order() {
        let t = Target::new("hi-hi-hi");
        let expected = SocketAddr::V6(SocketAddrV6::new(
            "fe80::4559:49b2:462d:f46b".parse().unwrap(),
            0,
            0,
            8,
        ));
        let addrs_pre = vec![
            expected.clone(),
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(192, 168, 70, 68), 0)),
        ];
        let addrs_post = addrs_pre
            .iter()
            .cloned()
            .enumerate()
            .map(|(i, e)| {
                (TargetAddr::from(e), Utc.ymd(2014 + (i as i32), 10, 31).and_hms(9, 10, 12)).into()
            })
            .collect::<Vec<TargetAddrEntry>>();
        for a in addrs_post.iter().cloned() {
            t.addrs_insert_entry(a).await;
        }
        assert_eq!((&t.addrs().await).to_ssh_addr().unwrap(), TargetAddr::from(expected));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_addresses_prefer_local_vs_v6() {
        let t = Target::new("hi-hi-hi");
        let expected = SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(192, 168, 70, 68), 0));
        let addrs_pre = vec![
            expected.clone(),
            SocketAddr::V6(SocketAddrV6::new(
                "9999::4559:49b2:462d:f46b".parse().unwrap(),
                0,
                0,
                0,
            )),
        ];
        let addrs_post = addrs_pre
            .iter()
            .cloned()
            .enumerate()
            .map(|(i, e)| {
                (TargetAddr::from(e), Utc.ymd(2014 + (i as i32), 10, 31).and_hms(9, 10, 12)).into()
            })
            .collect::<Vec<TargetAddrEntry>>();
        for a in addrs_post.iter().cloned() {
            t.addrs_insert_entry(a).await;
        }
        assert_eq!((&t.addrs().await).to_ssh_addr().unwrap(), TargetAddr::from(expected));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_merge_no_name() {
        let ip = "f111::3".parse().unwrap();

        // t1 is a target as we would naturally discover it via mdns, or from a
        // user adding it explicitly. That is, the target has a correctly scoped
        // link-local address.
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip, scope_id: 0xbadf00d });
        let t1 = Target::new_with_addrs(Option::<String>::None, addr_set);

        // t2 is an incoming target that has the same address, but, it is
        // missing scope information, this is essentially what occurs when we
        // ask the target for its addresses.
        let t2 = Target::new("this-is-a-crunchy-falafel");
        t2.inner.addrs.write().await.replace(TargetAddr { ip, scope_id: 0 }.into());

        let tc = TargetCollection::new();
        tc.merge_insert(t1).await;
        tc.merge_insert(t2).await;
        let mut targets = tc.targets().await.into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename_str().await, "this-is-a-crunchy-falafel");
        let mut addrs = target.addrs().await.into_iter();
        let addr = addrs.next().expect("Merged target has no address.");
        assert!(addrs.next().is_none());
        assert_eq!(addr, TargetAddr { ip, scope_id: 0xbadf00d });
        assert_eq!(addr.ip, ip);
        assert_eq!(addr.scope_id, 0xbadf00d);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_does_not_merge_different_ports() {
        let ip = "::1".parse().unwrap();

        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip, scope_id: 0 });
        let t1 = Target::new_with_addrs(Some("t1"), addr_set.clone());
        t1.set_ssh_port(Some(8022)).await;
        let t2 = Target::new_with_addrs(Some("t2"), addr_set.clone());
        t2.set_ssh_port(Some(8023)).await;

        let tc = TargetCollection::new();
        tc.merge_insert(t1).await;
        tc.merge_insert(t2).await;

        let mut targets = tc.targets().await.into_iter().collect::<Vec<Target>>();

        assert_eq!(targets.len(), 2);

        targets.sort_by(|a, b| block_on(a.ssh_port()).cmp(&block_on(b.ssh_port())));
        let mut iter = targets.into_iter();
        let found1 = iter.next().expect("must have target one");
        let found2 = iter.next().expect("must have target two");

        assert_eq!(found1.addrs().await.into_iter().next().unwrap().ip, ip);
        assert_eq!(found1.ssh_port().await, Some(8022));
        assert_eq!(found1.nodename().await, Some("t1".to_string()));

        assert_eq!(found2.addrs().await.into_iter().next().unwrap().ip, ip);
        assert_eq!(found2.ssh_port().await, Some(8023));
        assert_eq!(found2.nodename().await, Some("t2".to_string()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_get_default_empty() {
        let tc = TargetCollection::new();
        assert_eq!(Err(DaemonError::TargetCacheEmpty), tc.get_default(None).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_get_default_successful() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default).await;
        let tc = TargetCollection::new();
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get_default(Some(default.to_string())).await.unwrap(), t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_get_default_ambiguous() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default).await;
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel").await;
        let tc = TargetCollection::new();
        tc.merge_insert(clone_target(&t).await).await;
        tc.merge_insert(t2).await;
        assert_eq!(Err(DaemonError::TargetAmbiguous), tc.get_default(None).await);

        assert_eq!(
            Err(DaemonError::TargetNotFound),
            tc.get_default(Some("not_in_here".to_owned())).await
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_get_default_not_found() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default).await;
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel").await;
        let tc = TargetCollection::new();
        tc.merge_insert(clone_target(&t).await).await;
        tc.merge_insert(t2).await;
        assert_eq!(
            Err(DaemonError::TargetNotFound),
            tc.get_default(Some("not_in_here".to_owned())).await
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_remove_unnamed_by_addr() {
        let ip1 = "f111::3".parse().unwrap();
        let ip2 = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip: ip1, scope_id: 0xbadf00d });
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new();
        t2.inner.addrs.write().await.replace(TargetAddr { ip: ip2, scope_id: 0 }.into());
        tc.merge_insert(t1).await;
        tc.merge_insert(t2).await;
        let mut targets = tc.targets().await.into_iter();
        let target1 = targets.next().expect("Merging resulted in no targets.");
        let target2 = targets.next().expect("Merging resulted in only one target.");
        assert!(targets.next().is_none());
        assert_eq!(target1.nodename_str().await, "this-is-a-crunchy-falafel");
        assert_eq!(target2.nodename().await, None);
        assert!(tc.remove_target("f111::3".to_owned()).await);
        let mut targets = tc.targets().await.into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename_str().await, "this-is-a-crunchy-falafel");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_remove_named_by_addr() {
        let ip1 = "f111::3".parse().unwrap();
        let ip2 = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip: ip1, scope_id: 0xbadf00d });
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new();
        t2.inner.addrs.write().await.replace(TargetAddr { ip: ip2, scope_id: 0 }.into());
        tc.merge_insert(t1).await;
        tc.merge_insert(t2).await;
        let mut targets = tc.targets().await.into_iter();
        let target1 = targets.next().expect("Merging resulted in no targets.");
        let target2 = targets.next().expect("Merging resulted in only one target.");
        assert!(targets.next().is_none());
        assert_eq!(target1.nodename_str().await, "this-is-a-crunchy-falafel");
        assert_eq!(target2.nodename().await, None);
        assert!(tc.remove_target("f111::4".to_owned()).await);
        let mut targets = tc.targets().await.into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename().await, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_remove_by_name() {
        let ip1 = "f111::3".parse().unwrap();
        let ip2 = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip: ip1, scope_id: 0xbadf00d });
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new();
        t2.inner.addrs.write().await.replace(TargetAddr { ip: ip2, scope_id: 0 }.into());
        tc.merge_insert(t1).await;
        tc.merge_insert(t2).await;
        let mut targets = tc.targets().await.into_iter();
        let target1 = targets.next().expect("Merging resulted in no targets.");
        let target2 = targets.next().expect("Merging resulted in only one target.");
        assert!(targets.next().is_none());
        assert_eq!(target1.nodename_str().await, "this-is-a-crunchy-falafel");
        assert_eq!(target2.nodename().await, None);
        assert!(tc.remove_target("this-is-a-crunchy-falafel".to_owned()).await);
        let mut targets = tc.targets().await.into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename().await, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_match_serial() {
        let t =
            Target::new_with_serial(Some("turritopsis-dohrnii-is-an-immortal-jellyfish"), "florp");
        let tc = TargetCollection::new();
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(
            t.nodename().await.unwrap(),
            tc.get("flor").await.unwrap().nodename().await.unwrap()
        );
    }

    #[test]
    fn test_target_query_from_sockaddr() {
        let tq = TargetQuery::from("127.0.0.1:8022");
        let ti = TargetInfo {
            addresses: vec![("127.0.0.1".parse::<IpAddr>().unwrap(), 0).into()],
            ssh_port: Some(8022),
            ..Default::default()
        };
        assert!(
            matches!(tq, TargetQuery::AddrPort((addr, port)) if addr == ti.addresses[0] && Some(port) == ti.ssh_port)
        );
        assert!(tq.match_info(&ti));

        let tq = TargetQuery::from("[::1]:8022");
        let ti = TargetInfo {
            addresses: vec![("::1".parse::<IpAddr>().unwrap(), 0).into()],
            ssh_port: Some(8022),
            ..Default::default()
        };
        assert!(
            matches!(tq, TargetQuery::AddrPort((addr, port)) if addr == ti.addresses[0] && Some(port) == ti.ssh_port)
        );
        assert!(tq.match_info(&ti));

        let tq = TargetQuery::from("[fe80::1]:22");
        let ti = TargetInfo {
            addresses: vec![("fe80::1".parse::<IpAddr>().unwrap(), 0).into()],
            ssh_port: Some(22),
            ..Default::default()
        };
        assert!(
            matches!(tq, TargetQuery::AddrPort((addr, port)) if addr == ti.addresses[0] && Some(port) == ti.ssh_port)
        );
        assert!(tq.match_info(&ti));

        let tq = TargetQuery::from("192.168.0.1:22");
        let ti = TargetInfo {
            addresses: vec![("192.168.0.1".parse::<IpAddr>().unwrap(), 0).into()],
            ssh_port: Some(22),
            ..Default::default()
        };
        assert!(
            matches!(tq, TargetQuery::AddrPort((addr, port)) if addr == ti.addresses[0] && Some(port) == ti.ssh_port)
        );
        assert!(tq.match_info(&ti));

        // Note: socketaddr only supports numeric scopes
        let tq = TargetQuery::from("[fe80::1%1]:22");
        let ti = TargetInfo {
            addresses: vec![("fe80::1".parse::<IpAddr>().unwrap(), 1).into()],
            ssh_port: Some(22),
            ..Default::default()
        };
        assert!(
            matches!(tq, TargetQuery::AddrPort((addr, port)) if addr == ti.addresses[0] && Some(port) == ti.ssh_port)
        );
        assert!(tq.match_info(&ti));
    }

    #[test]
    fn test_target_query_with_no_scope_matches_scoped_target_info() {
        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        let tq = TargetQuery::from("fe80::dead:beef:beef:beef");
        assert!(tq.match_info(&TargetInfo { addresses: vec![addr], ..Default::default() }))
    }
}

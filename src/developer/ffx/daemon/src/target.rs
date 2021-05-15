// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{FASTBOOT_MAX_AGE, MDNS_MAX_AGE, ZEDBOOT_MAX_AGE},
    crate::events::{DaemonEvent, TargetInfo},
    crate::fastboot::open_interface_with_serial,
    crate::logger::{streamer::DiagnosticsStreamer, Logger},
    crate::onet::HostPipeConnection,
    anyhow::{anyhow, bail, Context, Error, Result},
    async_trait::async_trait,
    bridge::{DaemonError, TargetAddrInfo, TargetIpPort},
    chrono::{DateTime, Utc},
    ffx_daemon_core::events::{self, EventSynthesizer},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_bridge::TargetState as FidlTargetState,
    fidl_fuchsia_developer_remotecontrol::{
        IdentifyHostError, IdentifyHostResponse, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet},
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::Task,
    hoist::OvernetInstance,
    netext::{scope_id_to_name, IsLocalAddr},
    rand::random,
    std::cell::RefCell,
    std::cmp::Ordering,
    std::collections::{BTreeSet, HashMap, HashSet},
    std::default::Default,
    std::fmt,
    std::fmt::{Debug, Display},
    std::hash::{Hash, Hasher},
    std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6},
    std::rc::{Rc, Weak},
    std::sync::Arc,
    std::time::{Duration, Instant},
    timeout::{timeout, TimeoutError},
    usb_bulk::AsyncInterface as Interface,
};

const IDENTIFY_HOST_TIMEOUT_MILLIS: u64 = 1000;
const DEFAULT_SSH_PORT: u16 = 22;
pub trait ToFidlTarget {
    fn to_fidl_target(self) -> bridge::Target;
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
    pub fn new(id: &mut NodeId) -> Result<Self> {
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
    Mdns(Instant),
    /// Contains an actual connection to RCS.
    Rcs(RcsConnection),
    /// Target was manually added. Same as `Disconnected` but indicates that the target's name is
    /// wrong as well.
    Manual,
    /// Contains the last known interface update with a Fastboot serial number.
    Fastboot(Instant),
    /// Contains the last known interface update with a Fastboot serial number.
    Zedboot(Instant),
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
    // If the target entry was added manually
    manual: bool,
    // If the target comes from netsvc,
    netsvc: bool,
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
        Self { addr, timestamp, manual: false, netsvc: false }
    }
}

impl From<(TargetAddr, DateTime<Utc>, bool)> for TargetAddrEntry {
    fn from(t: (TargetAddr, DateTime<Utc>, bool)) -> Self {
        let (addr, timestamp, manual) = t;
        Self { addr, timestamp, manual, netsvc: false }
    }
}

impl From<(TargetAddr, DateTime<Utc>, bool, bool)> for TargetAddrEntry {
    fn from(t: (TargetAddr, DateTime<Utc>, bool, bool)) -> Self {
        let (addr, timestamp, manual, netsvc) = t;
        Self { addr, timestamp, manual, netsvc }
    }
}

impl From<TargetAddr> for TargetAddrEntry {
    fn from(addr: TargetAddr) -> Self {
        Self { addr, timestamp: Utc::now(), manual: false, netsvc: false }
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

/// Given an iterator of TargetAddrEntry, return the TargetAddr from the entry
/// that should be attempted for SSH connections next.
///
/// The sort algorithm for SSH address priority is in order of:
/// - Manual addresses first
///   - By recency of observation
/// - Other addresses
///   - By link-local first
///   - By most recently observed
///
/// The host-pipe connection mechanism will requests addresses from this function
/// on each connection attempt.
fn ssh_address_from<'a, I>(iter: I) -> Option<TargetAddr>
where
    I: Iterator<Item = &'a TargetAddrEntry>,
{
    use itertools::Itertools;

    // Order e1 & e2 by most recent timestamp
    let recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| e2.timestamp.cmp(&e1.timestamp);

    // Order by link-local first, then by recency
    let link_local_recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| match (
        e1.addr.ip.is_link_local_addr(),
        e2.addr.ip.is_link_local_addr(),
    ) {
        (true, true) | (false, false) => recency(e1, e2),
        (true, false) => Ordering::Less,
        (false, true) => Ordering::Greater,
    };

    let manual_link_local_recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| {
        match (e1.manual, e2.manual) {
            // Note: for manually added addresses, they are ordered strictly
            // by recency, not link-local first.
            (true, true) => recency(e1, e2),
            (true, false) => Ordering::Less,
            (false, true) => Ordering::Greater,
            (false, false) => link_local_recency(e1, e2),
        }
    };

    iter.filter(|t| !t.netsvc)
        .sorted_by(|e1, e2| manual_link_local_recency(e1, e2))
        .next()
        .map(|e| e.addr.clone())
}

#[derive(Debug, Default, Clone)]
#[cfg_attr(test, derive(PartialEq, Eq))]
pub struct TargetState {
    pub connection_state: ConnectionState,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BuildConfig {
    pub product_config: String,
    pub board_config: String,
}

struct TargetInner {
    // id is the locally created "primary identifier" for this target.
    id: u64,
    // ids keeps track of additional ids discovered over Overnet, these could
    // come from old Daemons, or other Daemons. The set should be used
    ids: RefCell<HashSet<u64>>,
    nodename: RefCell<Option<String>>,
    state: RefCell<TargetState>,
    last_response: RefCell<DateTime<Utc>>,
    addrs: RefCell<BTreeSet<TargetAddrEntry>>,
    // ssh_port if set overrides the global default configuration for ssh port,
    // for this target.
    ssh_port: RefCell<Option<u16>>,
    // used for Fastboot
    serial: RefCell<Option<String>>,
    build_config: RefCell<Option<BuildConfig>>,
    boot_timestamp_nanos: RefCell<Option<u64>>,
    diagnostics_info: Arc<DiagnosticsStreamer<'static>>,
}

impl Debug for TargetInner {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("TargetInner")
            .field("id", &self.id)
            .field("ids", &self.ids.borrow().clone())
            .field("nodename", &self.nodename.borrow().clone())
            .field("state", &self.state.borrow().clone())
            .field("last_response", &self.last_response.borrow().clone())
            .field("addrs", &self.addrs.borrow().clone())
            .field("ssh_port", &self.ssh_port.borrow().clone())
            .field("serial", &self.serial.borrow().clone())
            .field("boot_timestamp_nanos", &self.boot_timestamp_nanos.borrow().clone())
            .finish()
    }
}

impl TargetInner {
    fn new(nodename: Option<String>) -> Self {
        let id = random::<u64>();
        let mut ids = HashSet::new();
        ids.insert(id.clone());
        Self {
            id: id.clone(),
            ids: RefCell::new(ids),
            nodename: RefCell::new(nodename),
            last_response: RefCell::new(Utc::now()),
            state: RefCell::new(TargetState::default()),
            addrs: RefCell::new(BTreeSet::new()),
            ssh_port: RefCell::new(None),
            serial: RefCell::new(None),
            boot_timestamp_nanos: RefCell::new(None),
            build_config: Default::default(),
            diagnostics_info: Arc::new(DiagnosticsStreamer::default()),
        }
    }

    pub fn new_with_boot_timestamp(nodename: String, boot_timestamp_nanos: u64) -> Self {
        Self {
            boot_timestamp_nanos: RefCell::new(Some(boot_timestamp_nanos)),
            ..Self::new(Some(nodename))
        }
    }

    pub fn new_with_addrs(nodename: Option<String>, addrs: BTreeSet<TargetAddr>) -> Self {
        Self {
            addrs: RefCell::new(addrs.iter().map(|e| (*e, Utc::now()).into()).collect()),
            ..Self::new(nodename)
        }
    }

    pub fn new_with_netsvc_addrs(nodename: Option<String>, addrs: BTreeSet<TargetAddr>) -> Self {
        Self {
            addrs: RefCell::new(
                addrs.iter().map(|e| (*e, Utc::now(), false, true).into()).collect(),
            ),
            ..Self::new(nodename)
        }
    }

    pub fn new_with_addr_entries(
        nodename: Option<String>,
        entries: BTreeSet<TargetAddrEntry>,
    ) -> Self {
        Self { addrs: RefCell::new(entries), ..Self::new(nodename) }
    }

    pub fn new_with_serial(serial: &str) -> Self {
        Self { serial: RefCell::new(Some(serial.to_string())), ..Self::new(None) }
    }

    pub fn id(&self) -> u64 {
        self.id.clone()
    }

    pub fn ids(&self) -> HashSet<u64> {
        self.ids.borrow().clone()
    }

    pub fn has_id<'a, I>(&self, ids: I) -> bool
    where
        I: Iterator<Item = &'a u64>,
    {
        let my_ids = self.ids.borrow();
        for id in ids {
            if my_ids.contains(id) {
                return true;
            }
        }
        false
    }

    /// ssh_address returns the TargetAddr of the next SSH address to connect to for this target.
    pub fn ssh_address(&self) -> Option<TargetAddr> {
        ssh_address_from(self.addrs.borrow().iter())
    }

    pub fn netsvc_address(&self) -> Option<TargetAddr> {
        use itertools::Itertools;
        // Order e1 & e2 by most recent timestamp
        let recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| e2.timestamp.cmp(&e1.timestamp);
        // TODO(fxb/76325) `.find(|t| t.netsvc)` doesn't work for some reason, using next()
        self.addrs
            .borrow()
            .iter()
            .sorted_by(|e1, e2| recency(e1, e2))
            .next()
            .map(|t| t.addr.clone())
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time(nodename: String, time: DateTime<Utc>) -> Self {
        Self { last_response: RefCell::new(time), ..Self::new(Some(nodename)) }
    }

    pub fn nodename_str(&self) -> String {
        self.nodename.borrow().clone().unwrap_or("<unknown>".to_owned())
    }
}

#[async_trait(?Send)]
impl EventSynthesizer<TargetEvent> for TargetInner {
    async fn synthesize_events(&self) -> Vec<TargetEvent> {
        match self.state.borrow().connection_state {
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
        Some(Target { inner, events, host_pipe: Default::default(), logger: Default::default() })
    }
}

#[derive(Clone)]
pub struct Target {
    pub events: events::Queue<TargetEvent>,

    host_pipe: Rc<RefCell<Option<Task<()>>>>,
    logger: Rc<RefCell<Option<Task<()>>>>,

    inner: Rc<TargetInner>,
}

impl Target {
    pub fn is_connected(&self) -> bool {
        self.inner.state.borrow().connection_state.is_connected()
    }

    pub fn downgrade(&self) -> WeakTarget {
        WeakTarget { events: self.events.clone(), inner: Rc::downgrade(&self.inner) }
    }

    fn from_inner(inner: Rc<TargetInner>) -> Self {
        let events = events::Queue::new(&inner);
        Self { inner, events, host_pipe: Default::default(), logger: Default::default() }
    }

    pub fn new<S>(nodename: S) -> Self
    where
        S: Into<String>,
    {
        let inner = Rc::new(TargetInner::new(Some(nodename.into())));
        Self::from_inner(inner)
    }

    pub fn new_with_boot_timestamp<S>(nodename: S, boot_timestamp_nanos: u64) -> Self
    where
        S: Into<String>,
    {
        let inner =
            Rc::new(TargetInner::new_with_boot_timestamp(nodename.into(), boot_timestamp_nanos));
        Self::from_inner(inner)
    }

    pub fn new_with_addrs<S>(nodename: Option<S>, addrs: BTreeSet<TargetAddr>) -> Self
    where
        S: Into<String>,
    {
        let inner = Rc::new(TargetInner::new_with_addrs(nodename.map(Into::into), addrs));
        Self::from_inner(inner)
    }

    pub(crate) fn new_with_addr_entries<S, I>(nodename: Option<S>, entries: I) -> Self
    where
        S: Into<String>,
        I: Iterator<Item = TargetAddrEntry>,
    {
        use std::iter::FromIterator;
        let inner = Rc::new(TargetInner::new_with_addr_entries(
            nodename.map(Into::into),
            BTreeSet::from_iter(entries),
        ));
        Self::from_inner(inner)
    }

    pub fn new_with_netsvc_addrs<S>(nodename: Option<S>, addrs: BTreeSet<TargetAddr>) -> Self
    where
        S: Into<String>,
    {
        let inner = Rc::new(TargetInner::new_with_netsvc_addrs(nodename.map(Into::into), addrs));
        let target = Self::from_inner(inner);
        target.update_connection_state(|_| ConnectionState::Zedboot(Instant::now()));
        target
    }

    pub fn new_with_serial(serial: &str) -> Self {
        let inner = Rc::new(TargetInner::new_with_serial(serial));
        let target = Self::from_inner(inner);
        target.update_connection_state(|_| ConnectionState::Fastboot(Instant::now()));
        target
    }

    pub fn from_target_info(mut t: TargetInfo) -> Self {
        if let Some(s) = t.serial {
            Self::new_with_serial(&s)
        } else {
            Self::new_with_addrs(t.nodename.take(), t.addresses.drain(..).collect())
        }
    }

    pub fn from_netsvc_target_info(mut t: TargetInfo) -> Self {
        Self::new_with_netsvc_addrs(t.nodename.take(), t.addresses.drain(..).collect())
    }

    pub fn target_info(&self) -> TargetInfo {
        TargetInfo {
            nodename: self.nodename(),
            addresses: self.addrs(),
            serial: self.serial(),
            ssh_port: self.ssh_port(),
        }
    }

    // Get the locally minted identifier for the target
    pub fn id(&self) -> u64 {
        self.inner.id()
    }

    // Get all known ids for the target
    pub fn ids(&self) -> HashSet<u64> {
        self.inner.ids()
    }

    pub fn has_id<'a, I>(&self, ids: I) -> bool
    where
        I: Iterator<Item = &'a u64>,
    {
        self.inner.has_id(ids)
    }

    pub fn merge_ids<'a, I>(&self, new_ids: I)
    where
        I: Iterator<Item = &'a u64>,
    {
        let mut my_ids = self.inner.ids.borrow_mut();
        for id in new_ids {
            my_ids.insert(*id);
        }
    }

    pub fn ssh_address(&self) -> Option<TargetAddr> {
        self.inner.ssh_address()
    }

    pub fn netsvc_address(&self) -> Option<TargetAddr> {
        self.inner.netsvc_address()
    }

    pub fn ssh_address_info(&self) -> Option<bridge::TargetAddrInfo> {
        if let Some(addr) = self.ssh_address() {
            let ip = match addr.ip() {
                IpAddr::V6(i) => IpAddress::Ipv6(Ipv6Address { addr: i.octets().into() }),
                IpAddr::V4(i) => IpAddress::Ipv4(Ipv4Address { addr: i.octets().into() }),
            };
            let scope_id = addr.scope_id();

            let port = self.ssh_port().unwrap_or(DEFAULT_SSH_PORT);

            Some(TargetAddrInfo::IpPort(TargetIpPort { ip, port, scope_id }))
        } else {
            None
        }
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time<S: Into<String>>(nodename: S, time: DateTime<Utc>) -> Self {
        let inner = Rc::new(TargetInner::new_with_time(nodename.into(), time));
        Self::from_inner(inner)
    }

    fn rcs_state(&self) -> bridge::RemoteControlState {
        let state = self.inner.state.borrow();
        match (self.is_host_pipe_running(), &state.connection_state) {
            (true, ConnectionState::Rcs(_)) => bridge::RemoteControlState::Up,
            (true, _) => bridge::RemoteControlState::Down,
            (_, _) => bridge::RemoteControlState::Unknown,
        }
    }

    pub fn nodename(&self) -> Option<String> {
        self.inner.nodename.borrow().clone()
    }

    pub fn nodename_str(&self) -> String {
        self.inner.nodename_str()
    }

    pub fn set_nodename(&self, nodename: String) {
        self.inner.nodename.borrow_mut().replace(nodename);
    }

    pub fn boot_timestamp_nanos(&self) -> Option<u64> {
        self.inner.boot_timestamp_nanos.borrow().clone()
    }

    pub fn update_boot_timestamp(&self, ts: Option<u64>) {
        self.inner.boot_timestamp_nanos.replace(ts);
    }

    pub fn stream_info(&self) -> Arc<DiagnosticsStreamer<'static>> {
        self.inner.diagnostics_info.clone()
    }

    pub fn serial(&self) -> Option<String> {
        self.inner.serial.borrow().clone()
    }

    pub fn state(&self) -> TargetState {
        self.inner.state.borrow().clone()
    }

    #[cfg(test)]
    pub fn set_state(&self, state: ConnectionState) {
        self.inner.state.replace(TargetState { connection_state: state });
    }

    pub fn get_connection_state(&self) -> ConnectionState {
        self.inner.state.borrow().connection_state.clone()
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
    pub fn update_connection_state<F>(&self, func: F)
    where
        F: FnOnce(ConnectionState) -> ConnectionState + Sized,
    {
        let former_state = self.inner.state.borrow().connection_state.clone();
        let update = (func)(std::mem::replace(
            &mut self.inner.state.borrow_mut().connection_state,
            ConnectionState::Disconnected,
        ));
        self.inner.state.borrow_mut().connection_state = update;
        if former_state != self.inner.state.borrow().connection_state {
            self.events
                .push(TargetEvent::ConnectionStateChanged(
                    former_state,
                    self.inner.state.borrow().connection_state.clone(),
                ))
                .unwrap_or_else(|e| {
                    log::error!("Failed to push state change for {:?}: {:?}", self, e)
                });
        }
    }

    pub fn rcs(&self) -> Option<RcsConnection> {
        match &self.inner.state.borrow().connection_state {
            ConnectionState::Rcs(conn) => Some(conn.clone()),
            _ => None,
        }
    }

    pub fn usb(&self) -> (String, Option<Interface>) {
        match self.inner.serial.borrow().as_ref() {
            Some(s) => (s.to_string(), open_interface_with_serial(s).ok()),
            None => ("".to_string(), None),
        }
    }

    pub fn last_response(&self) -> DateTime<Utc> {
        self.inner.last_response.borrow().clone()
    }

    pub fn build_config(&self) -> Option<BuildConfig> {
        self.inner.build_config.borrow().clone()
    }

    pub fn addrs(&self) -> Vec<TargetAddr> {
        let mut addrs = self.inner.addrs.borrow().iter().cloned().collect::<Vec<_>>();
        addrs.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));
        addrs.drain(..).map(|e| e.addr).collect()
    }

    pub fn drop_unscoped_link_local_addrs(&self) {
        let mut addrs = self.inner.addrs.borrow_mut();

        *addrs = addrs
            .clone()
            .into_iter()
            .filter(|entry| {
                entry.manual
                    || if let IpAddr::V6(v) = &entry.addr.ip {
                        entry.addr.scope_id != 0 || !v.is_link_local_addr()
                    } else {
                        true
                    }
            })
            .collect();
    }

    pub fn drop_loopback_addrs(&self) {
        let mut addrs = self.inner.addrs.borrow_mut();

        *addrs = addrs
            .clone()
            .into_iter()
            .filter(|entry| {
                entry.manual
                    || if let IpAddr::V4(v) = &entry.addr.ip { !v.is_loopback() } else { true }
            })
            .collect();
    }

    pub fn ssh_port(&self) -> Option<u16> {
        self.inner.ssh_port.borrow().clone()
    }

    pub(crate) fn set_ssh_port(&self, port: Option<u16>) {
        self.inner.ssh_port.replace(port);
    }

    pub(crate) fn manual_addrs(&self) -> Vec<TargetAddr> {
        self.inner
            .addrs
            .borrow()
            .iter()
            .filter_map(|entry| if entry.manual { Some(entry.addr.clone()) } else { None })
            .collect()
    }

    #[cfg(test)]
    pub(crate) fn addrs_insert(&self, t: TargetAddr) {
        self.inner.addrs.borrow_mut().replace(t.into());
    }

    #[cfg(test)]
    pub fn new_autoconnected(n: &str) -> Self {
        let s = Self::new(n);
        s.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(Instant::now())
        });
        s
    }

    #[cfg(test)]
    pub(crate) fn addrs_insert_entry(&self, t: TargetAddrEntry) {
        self.inner.addrs.borrow_mut().replace(t);
    }

    fn addrs_extend<T>(&self, new_addrs: T)
    where
        T: IntoIterator<Item = TargetAddr>,
    {
        let now = Utc::now();
        let mut addrs = self.inner.addrs.borrow_mut();

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

    fn update_last_response(&self, other: DateTime<Utc>) {
        let mut last_response = self.inner.last_response.borrow_mut();
        if *last_response < other {
            *last_response = other;
        }
    }

    fn overwrite_state(&self, mut other: TargetState) {
        if let Some(rcs) = other.connection_state.take_rcs() {
            // Nots this so that we know to push an event.
            let rcs_activated = !self.inner.state.borrow().connection_state.is_rcs();
            self.inner.state.borrow_mut().connection_state = ConnectionState::Rcs(rcs);
            if rcs_activated {
                self.events.push(TargetEvent::RcsActivated).unwrap_or_else(|err| {
                    log::warn!("unable to enqueue RCS activation event: {:#}", err)
                });
            }
        }
    }

    pub fn from_identify(identify: IdentifyHostResponse) -> Result<Self, Error> {
        // TODO(raggi): allow targets to truly be created without a nodename.
        let nodename = match identify.nodename {
            Some(n) => n,
            None => bail!("Target identification missing a nodename: {:?}", identify),
        };

        let target = Target::new(nodename);
        target.update_last_response(Utc::now().into());
        if let Some(ids) = identify.ids {
            target.merge_ids(ids.iter());
        }
        *target.inner.build_config.borrow_mut() =
            if identify.board_config.is_some() || identify.product_config.is_some() {
                let p = identify.product_config.unwrap_or("<unknown>".to_string());
                let b = identify.board_config.unwrap_or("<unknown>".to_string());
                Some(BuildConfig { product_config: p, board_config: b })
            } else {
                None
            };

        if let Some(serial) = identify.serial_number {
            target.inner.serial.borrow_mut().replace(serial);
        }
        if let Some(t) = identify.boot_timestamp_nanos {
            target.inner.boot_timestamp_nanos.borrow_mut().replace(t);
        }
        if let Some(addrs) = identify.addresses {
            let mut taddrs = target.inner.addrs.borrow_mut();
            for addr in addrs.iter().map(|addr| TargetAddr::from(addr.clone())) {
                taddrs.insert(addr.into());
            }
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
        let target =
            Target::from_identify(identify).map_err(|e| RcsConnectionError::TargetError(e))?;
        target.update_connection_state(move |_| ConnectionState::Rcs(rcs));
        Ok(target)
    }

    pub fn run_host_pipe(&self) {
        if self.host_pipe.borrow().is_none() {
            let host_pipe = Rc::downgrade(&self.host_pipe);
            let weak_target = self.downgrade();
            self.host_pipe.replace(Some(Task::local(async move {
                let r = HostPipeConnection::new(weak_target).await;
                // XXX(raggi): decide what to do with this log data:
                log::info!("HostPipeConnection returned: {:?}", r);
                host_pipe.upgrade().and_then(|host_pipe| host_pipe.replace(None));
            })));
        }
    }

    pub fn is_host_pipe_running(&self) -> bool {
        self.host_pipe.borrow().is_some()
    }

    pub fn run_logger(&self) {
        if self.logger.borrow().is_none() {
            let logger = Rc::downgrade(&self.logger);
            let weak_target = self.downgrade();
            self.logger.replace(Some(Task::local(async move {
                let r = Logger::new(weak_target).start().await;
                // XXX(raggi): decide what to do with this log data:
                log::info!("Logger returned: {:?}", r);
                logger.upgrade().and_then(|logger| logger.replace(None));
            })));
        }
    }

    pub fn is_logger_running(&self) -> bool {
        self.logger.borrow().is_some()
    }

    pub async fn init_remote_proxy(&self) -> Result<RemoteControlProxy> {
        // Ensure auto-connect has at least started.
        self.run_host_pipe();
        match self.events.wait_for(None, |e| e == TargetEvent::RcsActivated).await {
            Ok(()) => (),
            Err(e) => {
                log::warn!("{}", e);
                bail!("RCS connection issue")
            }
        }
        self.rcs().ok_or(anyhow!("rcs dropped after event fired")).map(|r| r.proxy)
    }

    /// Check the current target state, and if it is a state that expires (such
    /// as mdns) perform the appropriate state transition. The daemon target
    /// collection expiry loop calls this function regularly.
    pub fn expire_state(&self) {
        self.update_connection_state(|current_state| {
            let expire_duration = match current_state {
                ConnectionState::Mdns(_) => MDNS_MAX_AGE,
                ConnectionState::Fastboot(_) => FASTBOOT_MAX_AGE,
                ConnectionState::Zedboot(_) => ZEDBOOT_MAX_AGE,
                _ => Duration::default(),
            };

            let new_state = match &current_state {
                ConnectionState::Mdns(ref last_seen)
                | ConnectionState::Fastboot(ref last_seen)
                | ConnectionState::Zedboot(ref last_seen) => {
                    if last_seen.elapsed() > expire_duration {
                        Some(ConnectionState::Disconnected)
                    } else {
                        None
                    }
                }
                _ => None,
            };

            if let Some(ref new_state) = new_state {
                log::debug!(
                    "Target {:?} state {:?} => {:?} due to expired state after {:?}.",
                    self,
                    &current_state,
                    new_state,
                    expire_duration
                );
            }

            new_state.unwrap_or(current_state)
        });
    }
}

#[async_trait(?Send)]
impl EventSynthesizer<DaemonEvent> for Target {
    async fn synthesize_events(&self) -> Vec<DaemonEvent> {
        if self.inner.state.borrow().connection_state.is_connected() {
            vec![DaemonEvent::NewTarget(self.target_info())]
        } else {
            vec![]
        }
    }
}

impl From<Target> for bridge::Target {
    fn from(target: Target) -> Self {
        let (product_config, board_config) = target
            .build_config()
            .map(|b| (Some(b.product_config), Some(b.board_config)))
            .unwrap_or((None, None));

        Self {
            nodename: target.nodename(),
            serial_number: target.serial(),
            addresses: Some(target.addrs().into_iter().map(|a| a.into()).collect()),
            age_ms: Some(match Utc::now()
                .signed_duration_since(target.last_response())
                .num_milliseconds()
            {
                dur if dur < 0 => {
                    log::trace!(
                        "negative duration encountered on target '{}': {}",
                        target.nodename_str(),
                        dur
                    );
                    0
                }
                dur => dur,
            } as u64),
            product_config,
            board_config,
            rcs_state: Some(target.rcs_state()),
            target_state: Some(match target.state().connection_state {
                ConnectionState::Disconnected => FidlTargetState::Disconnected,
                ConnectionState::Manual | ConnectionState::Mdns(_) | ConnectionState::Rcs(_) => {
                    FidlTargetState::Product
                }
                ConnectionState::Fastboot(_) => FidlTargetState::Fastboot,
                ConnectionState::Zedboot(_) => FidlTargetState::Zedboot,
            }),
            ssh_address: target.ssh_address_info(),
            // TODO(awdavies): Gather more information here when possible.
            target_type: Some(bridge::TargetType::Unknown),
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
    /// Construct a new TargetAddr from a string representation of the form
    /// accepted by std::net::SocketAddr, e.g. 127.0.0.1:22, or [fe80::1%1]:0.
    pub fn new<S>(s: S) -> Result<Self>
    where
        S: AsRef<str>,
    {
        let sa = s.as_ref().parse::<SocketAddr>()?;
        Ok(Self::from(sa))
    }

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
            write!(f, "%{}", scope_id_to_name(self.scope_id()))?;
        }

        Ok(())
    }
}

/// Convert a TargetAddrInfo to a SocketAddr preserving the port number if
/// provided, otherwise the returned SocketAddr will have port number 0.
pub fn target_addr_info_to_socketaddr(tai: TargetAddrInfo) -> SocketAddr {
    let mut sa = SocketAddr::from(TargetAddr::from(&tai));
    // TODO(raggi): the port special case needed here indicates a general problem in our
    // addressing strategy that is worth reviewing.
    if let TargetAddrInfo::IpPort(ref ipp) = tai {
        sa.set_port(ipp.port)
    }
    sa
}

pub trait MatchTarget {
    fn match_target<TQ>(self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery>;
}

// It's unclear why this definition has to exist, but the compiler complains
// if invoking this either directly on `iter()` or if invoking on
// `iter().into_iter()` about there being unmet trait constraints. With this
// definition there are no compilation complaints.
impl<'a> MatchTarget for std::slice::Iter<'_, &'a Target> {
    fn match_target<TQ>(self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery>,
    {
        let t: TargetQuery = t.into();
        for target in self {
            if t.matches(&target) {
                return Some((*target).clone());
            }
        }
        None
    }
}

impl<'a, T: Iterator<Item = &'a Target>> MatchTarget for &'a mut T {
    fn match_target<TQ>(self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery>,
    {
        let t: TargetQuery = t.into();
        for target in self {
            if t.matches(&target) {
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

    pub fn matches(&self, t: &Target) -> bool {
        self.match_info(&t.target_info())
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
        if s == "" {
            return Self::First;
        }

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

pub struct TargetCollection {
    targets: RefCell<HashMap<u64, Target>>,
    events: RefCell<Option<events::Queue<DaemonEvent>>>,
}

#[async_trait(?Send)]
impl EventSynthesizer<DaemonEvent> for TargetCollection {
    async fn synthesize_events(&self) -> Vec<DaemonEvent> {
        // TODO(awdavies): This won't be accurate once a target is able to create
        // more than one event at a time.
        let mut res = Vec::with_capacity(self.targets.borrow().len());
        let targets = self.targets.borrow().values().cloned().collect::<Vec<_>>();
        for target in targets.into_iter() {
            res.extend(target.synthesize_events().await);
        }
        res
    }
}

impl TargetCollection {
    pub fn new() -> Self {
        Self { targets: RefCell::new(HashMap::new()), events: RefCell::new(None) }
    }

    #[cfg(test)]
    fn new_with_queue() -> Rc<Self> {
        let target_collection = Rc::new(Self::new());
        let queue = events::Queue::new(&target_collection);
        target_collection.set_event_queue(queue);
        target_collection
    }

    pub fn set_event_queue(&self, q: events::Queue<DaemonEvent>) {
        // This should be the only place a write lock is ever held.
        self.events.replace(Some(q));
    }

    pub fn targets(&self) -> Vec<Target> {
        self.targets.borrow().values().cloned().collect()
    }

    pub fn remove_target(&self, target_id: String) -> bool {
        if let Some(t) = self.get(target_id) {
            self.targets.borrow_mut().remove(&t.id());
            true
        } else {
            false
        }
    }

    fn find_matching_target(&self, new_target: &Target) -> Option<Target> {
        // Look for a target by primary ID first
        let new_ids = new_target.ids();
        let mut to_update =
            new_ids.iter().find_map(|id| self.targets.borrow().get(id).map(|t| t.clone()));

        // If we haven't yet found a target, try to find one by all IDs, nodename, serial, or address.
        if to_update.is_none() {
            let new_nodename = new_target.nodename();
            let new_ips =
                new_target.addrs().iter().map(|addr| addr.ip.clone()).collect::<Vec<IpAddr>>();
            let new_port = new_target.ssh_port();
            let new_serial = new_target.serial();

            for target in self.targets.borrow().values() {
                let serials_match = || match (target.serial().as_ref(), new_serial.as_ref()) {
                    (Some(s), Some(other_s)) => s == other_s,
                    _ => false,
                };

                // Only match the new nodename if it is Some and the same.
                let nodenames_match = || match (&new_nodename, target.nodename()) {
                    (Some(ref left), Some(ref right)) => left == right,
                    _ => false,
                };

                if target.has_id(new_ids.iter())
                    || serials_match()
                    || nodenames_match()
                    // Only match against addresses if the ports are the same
                    || (target.ssh_port() == new_port
                        && target.addrs().iter().any(|addr| new_ips.contains(&addr.ip)))
                {
                    to_update.replace(target.clone());
                    break;
                }
            }
        }

        to_update
    }

    pub fn merge_insert(&self, new_target: Target) -> Target {
        // Drop non-manual loopback address entries, as matching against
        // them could otherwise match every target in the collection.
        new_target.drop_loopback_addrs();

        let to_update = self.find_matching_target(&new_target);

        log::info!("Merging target {:?} into {:?}", new_target, to_update);

        // Do not merge unscoped link-local addresses into the target
        // collection, as they are not routable and therefore not safe
        // for connecting to the remote, and may collide with other
        // scopes.
        new_target.drop_unscoped_link_local_addrs();

        if let Some(to_update) = to_update {
            if let Some(config) = new_target.build_config() {
                to_update.inner.build_config.borrow_mut().replace(config);
            }
            if let Some(serial) = new_target.serial() {
                to_update.inner.serial.borrow_mut().replace(serial);
            }
            if let Some(new_name) = new_target.nodename() {
                to_update.set_nodename(new_name);
            }

            to_update.update_last_response(new_target.last_response());
            to_update.addrs_extend(new_target.addrs());
            to_update.update_boot_timestamp(new_target.boot_timestamp_nanos());

            // TODO(76632): The following overwrite could be dropping RcsConnections.
            to_update.overwrite_state(TargetState {
                connection_state: std::mem::replace(
                    &mut new_target.inner.state.borrow_mut().connection_state,
                    ConnectionState::Disconnected,
                ),
            });

            to_update.events.push(TargetEvent::Rediscovered).unwrap_or_else(|err| {
                log::warn!("unable to enqueue rediscovered event: {:#}", err)
            });
            to_update.clone()
        } else {
            self.targets.borrow_mut().insert(new_target.id(), new_target.clone());

            if let Some(event_queue) = self.events.borrow().as_ref() {
                event_queue
                    .push(DaemonEvent::NewTarget(new_target.target_info()))
                    .unwrap_or_else(|e| log::warn!("unable to push new target event: {}", e));
            }

            new_target
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
        // If there's nothing to match against, unblock on the first target.
        let target_query = TargetQuery::from(matcher.clone());

        // If there is no matcher, and there are already multiple targets in the
        // target collection, we know that the target is ambiguous and thus
        // produce an actionable error to the user.
        if let TargetQuery::First = target_query {
            // PERFORMANCE: it's possible to avoid the discarded clones here, with more work.

            // This is a stop-gap UX check. The other option is to
            // just display disconnected targets in `ffx target list` to make it
            // clear that an ambiguous target error is about having more than
            // one target in the cache rather than giving an ambiguous target
            // error around targets that cannot be displayed in the frontend.
            if self.targets.borrow().values().filter(|t| t.is_connected()).count() > 1 {
                return Err(DaemonError::TargetAmbiguous);
            }
        }

        // Infinite timeout here is fine, as the client dropping connection
        // will lead to this being cleaned up eventually. It is the client's
        // responsibility to determine their respective timeout(s).
        self.events
            .borrow()
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
        self.get_connected(matcher).ok_or(DaemonError::TargetNotFound)
    }

    pub fn get_connected<TQ>(&self, tq: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery>,
    {
        let target_query: TargetQuery = tq.into();

        self.targets
            .borrow()
            .values()
            .filter(|t| t.is_connected())
            .filter(|target| target_query.matches(target))
            .cloned()
            .next()
    }

    pub fn get<TQ>(&self, t: TQ) -> Option<Target>
    where
        TQ: Into<TargetQuery>,
    {
        let t: TargetQuery = t.into();
        self.targets.borrow().values().match_target(t)
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        bridge::TargetIp,
        chrono::offset::TimeZone,
        fidl, fidl_fuchsia_developer_remotecontrol as rcs,
        futures::prelude::*,
        std::net::{Ipv4Addr, Ipv6Addr},
    };

    const DEFAULT_PRODUCT_CONFIG: &str = "core";
    const DEFAULT_BOARD_CONFIG: &str = "x64";
    const TEST_SERIAL: &'static str = "test-serial";

    fn clone_target(t: &Target) -> Target {
        let inner = Rc::new(TargetInner::clone(&t.inner));
        Target::from_inner(inner)
    }

    impl Clone for TargetInner {
        fn clone(&self) -> Self {
            Self {
                id: self.id.clone(),
                ids: RefCell::new(self.ids.borrow().clone()),
                nodename: RefCell::new(self.nodename.borrow().clone()),
                last_response: RefCell::new(self.last_response.borrow().clone()),
                state: RefCell::new(self.state.borrow().clone()),
                addrs: RefCell::new(self.addrs.borrow().clone()),
                ssh_port: RefCell::new(self.ssh_port.borrow().clone()),
                serial: RefCell::new(self.serial.borrow().clone()),
                boot_timestamp_nanos: RefCell::new(self.boot_timestamp_nanos.borrow().clone()),
                diagnostics_info: self.diagnostics_info.clone(),
                build_config: RefCell::new(self.build_config.borrow().clone()),
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
            self.nodename() == o.nodename()
                && *self.inner.last_response.borrow() == *o.inner.last_response.borrow()
                && self.addrs() == o.addrs()
                && *self.inner.state.borrow() == *o.inner.state.borrow()
                && self.build_config() == o.build_config()
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new_not_connected() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, fake_now());
        tc.merge_insert(clone_target(&t));
        let other_target = &tc.get(nodename.clone()).unwrap();
        assert_eq!(other_target, &t);
        match tc.get_connected(nodename.clone()) {
            Some(_) => panic!("string lookup should return None"),
            _ => (),
        }
        let now = Instant::now();
        other_target.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(now)
        });
        t.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(now)
        });
        assert_eq!(&tc.get_connected(nodename.clone()).unwrap(), &t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, fake_now());
        tc.merge_insert(clone_target(&t));
        assert_eq!(&tc.get(nodename.clone()).unwrap(), &t);
        match tc.get("oihaoih") {
            Some(_) => panic!("string lookup should return None"),
            _ => (),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_merge() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("bananas");
        let t1 = Target::new_with_time(&nodename, fake_now());
        let t2 = Target::new_with_time(&nodename, fake_elapsed());
        let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
        let a2 = IpAddr::V6(Ipv6Addr::new(
            0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
        ));
        t1.addrs_insert((a1.clone(), 1).into());
        t2.addrs_insert((a2.clone(), 1).into());
        tc.merge_insert(clone_target(&t2));
        tc.merge_insert(clone_target(&t1));
        let merged_target = tc.get(nodename.clone()).unwrap();
        assert_ne!(&merged_target, &t1);
        assert_ne!(&merged_target, &t2);
        assert_eq!(merged_target.addrs().len(), 2);
        assert_eq!(*merged_target.inner.last_response.borrow(), fake_elapsed());
        assert!(merged_target.addrs().contains(&(a1, 1).into()));
        assert!(merged_target.addrs().contains(&(a2, 1).into()));

        // Insert another instance of the a2 address, but with a missing
        // scope_id, and ensure that the new address does not affect the address
        // collection.
        let t3 = Target::new_with_time(&nodename, fake_now());
        t3.addrs_insert((a2.clone(), 0).into());
        tc.merge_insert(clone_target(&t3));
        let merged_target = tc.get(nodename.clone()).unwrap();
        assert_eq!(merged_target.addrs().len(), 2);

        // Insert another instance of the a2 address, but with a new scope_id, and ensure that the new scope is used.
        let t3 = Target::new_with_time(&nodename, fake_now());
        t3.addrs_insert((a2.clone(), 3).into());
        tc.merge_insert(clone_target(&t3));
        let merged_target = tc.get(nodename.clone()).unwrap();
        assert_eq!(merged_target.addrs().iter().filter(|addr| addr.scope_id == 3).count(), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_no_scopeless_ipv6() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("bananas");
        let t1 = Target::new_with_time(&nodename, fake_now());
        let t2 = Target::new_with_time(&nodename, fake_elapsed());
        let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
        let a2 = IpAddr::V6(Ipv6Addr::new(
            0xfe80, 0x0000, 0x0000, 0x0000, 0xb412, 0xb455, 0x1337, 0xfeed,
        ));
        t1.addrs_insert((a1.clone(), 0).into());
        t2.addrs_insert((a2.clone(), 0).into());
        tc.merge_insert(clone_target(&t2));
        tc.merge_insert(clone_target(&t1));
        let merged_target = tc.get(nodename.clone()).unwrap();
        assert_ne!(&merged_target, &t1);
        assert_ne!(&merged_target, &t2);
        assert_eq!(merged_target.addrs().len(), 1);
        assert_eq!(*merged_target.inner.last_response.borrow(), fake_elapsed());
        assert!(merged_target.addrs().contains(&(a1, 0).into()));
        assert!(!merged_target.addrs().contains(&(a2, 0).into()));
    }

    fn setup_fake_remote_control_service(
        send_internal_error: bool,
        nodename_response: String,
    ) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        fuchsia_async::Task::local(async move {
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
                            let serial = String::from(TEST_SERIAL);
                            let nodename = if nodename_response.len() == 0 {
                                None
                            } else {
                                Some(nodename_response.clone())
                            };
                            responder
                                .send(&mut Ok(rcs::IdentifyHostResponse {
                                    nodename,
                                    serial_number: Some(serial),
                                    addresses: Some(result),
                                    product_config: Some(DEFAULT_PRODUCT_CONFIG.to_owned()),
                                    board_config: Some(DEFAULT_BOARD_CONFIG.to_owned()),
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
                assert_eq!(t.nodename().unwrap(), "foo".to_string());
                assert_eq!(t.rcs().unwrap().overnet_id.id, 1234u64);
                assert_eq!(t.addrs().len(), 1);
                assert_eq!(
                    t.build_config().unwrap(),
                    BuildConfig {
                        product_config: DEFAULT_PRODUCT_CONFIG.to_string(),
                        board_config: DEFAULT_BOARD_CONFIG.to_string()
                    }
                );
                assert_eq!(t.serial().unwrap(), String::from(TEST_SERIAL));
            }
            Err(_) => assert!(false),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_query_matches_nodename() {
        let query = TargetQuery::from("foo");
        let target = Rc::new(Target::new("foo"));
        assert!(query.matches(&target));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_by_addr() {
        let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
        let t = Target::new("foo");
        t.addrs_insert(addr.clone());
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t));
        assert_eq!(tc.get(addr).unwrap(), t);
        assert_eq!(tc.get("192.168.0.1").unwrap(), t);
        assert!(tc.get("fe80::dead:beef:beef:beef").is_none());

        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        let t = Target::new("fooberdoober");
        t.addrs_insert(addr.clone());
        tc.merge_insert(clone_target(&t));
        assert_eq!(tc.get("fe80::dead:beef:beef:beef").unwrap(), t);
        assert_eq!(tc.get(addr.clone()).unwrap(), t);
        assert_eq!(tc.get("fooberdoober").unwrap(), t);
    }

    // Most of this is now handled in `task.rs`
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_disconnect_multiple_invocations() {
        let t = Rc::new(Target::new("flabbadoobiedoo"));
        {
            let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
            t.addrs_insert(addr);
        }
        // Assures multiple "simultaneous" invocations to start the target
        // doesn't put it into a bad state that would hang.
        t.run_host_pipe();
        t.run_host_pipe();
        t.run_host_pipe();
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
            t.addrs_insert((a2, 2).into());
            if test.loop_started {
                t.run_host_pipe();
            }
            {
                *t.inner.state.borrow_mut() = TargetState {
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
            assert_eq!(t.rcs_state(), test.expected);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_into_bridge_target() {
        let t = Target::new("cragdune-the-impaler");
        let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
        let a2 = IpAddr::V6(Ipv6Addr::new(
            0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
        ));
        *t.inner.build_config.borrow_mut() = Some(BuildConfig {
            board_config: DEFAULT_BOARD_CONFIG.to_owned(),
            product_config: DEFAULT_PRODUCT_CONFIG.to_owned(),
        });
        t.addrs_insert((a1, 1).into());
        t.addrs_insert((a2, 1).into());

        let t_conv: bridge::Target = t.clone().into();
        assert_eq!(t.nodename().unwrap(), t_conv.nodename.unwrap().to_string());
        let addrs = t.addrs();
        let conv_addrs = t_conv.addresses.unwrap();
        assert_eq!(addrs.len(), conv_addrs.len());

        // Will crash if any addresses are missing.
        for address in conv_addrs {
            let address = TargetAddr::from(address);
            assert!(addrs.iter().any(|&a| a == address));
        }
        assert_eq!(t_conv.board_config.unwrap(), DEFAULT_BOARD_CONFIG.to_owned(),);
        assert_eq!(t_conv.product_config.unwrap(), DEFAULT_PRODUCT_CONFIG.to_owned(),);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_event_synthesis() {
        let t = Target::new("clopperdoop");
        let vec = t.synthesize_events().await;
        assert_eq!(vec.len(), 0);
        t.update_connection_state(|s| {
            assert_eq!(s, ConnectionState::Disconnected);
            ConnectionState::Mdns(Instant::now())
        });
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
        let t = Target::new_autoconnected("clam-chowder-is-tasty");
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel");
        let t3 = Target::new_autoconnected("i-should-probably-eat-lunch");
        let t4 = Target::new_autoconnected("i-should-probably-eat-lunch");
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t);
        tc.merge_insert(t2);
        tc.merge_insert(t3);
        tc.merge_insert(t4);

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

        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t);
        tc.merge_insert(t2);
        tc.merge_insert(t3);
        tc.merge_insert(t4);

        let events = tc.synthesize_events().await;
        assert_eq!(events.len(), 0);
    }

    struct EventPusher {
        got: async_channel::Sender<String>,
    }

    impl EventPusher {
        fn new() -> (Self, async_channel::Receiver<String>) {
            let (got, rx) = async_channel::unbounded::<String>();
            (Self { got }, rx)
        }
    }

    #[async_trait(?Send)]
    impl events::EventHandler<DaemonEvent> for EventPusher {
        async fn on_event(&self, event: DaemonEvent) -> Result<bool> {
            if let DaemonEvent::NewTarget(TargetInfo { nodename: Some(s), .. }) = event {
                self.got.send(s).await.unwrap();
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

        let tc = Rc::new(TargetCollection::new());
        let queue = events::Queue::new(&tc);
        let (handler, rx) = EventPusher::new();
        queue.add_handler(handler).await;
        tc.set_event_queue(queue);
        tc.merge_insert(t);
        tc.merge_insert(t2);
        tc.merge_insert(t3);
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
                assert_eq!(t.nodename().unwrap(), "foo".to_string());
                assert_eq!(t.rcs().unwrap().overnet_id.id, 1234u64);
                assert_eq!(t.addrs().len(), 1);
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
        t.overwrite_state(new_state);
        fut.await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_wait_for_match() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default);
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t));
        assert_eq!(tc.wait_for_match(Some(default.to_string())).await.unwrap(), t);
        assert_eq!(tc.wait_for_match(None).await.unwrap(), t);
        tc.merge_insert(t2);
        assert_eq!(tc.wait_for_match(Some(default.to_string())).await.unwrap(), t);
        assert!(tc.wait_for_match(None).await.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_wait_for_match_matches_contains() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default);
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t));
        assert_eq!(tc.wait_for_match(Some(default.to_string())).await.unwrap(), t);
        assert_eq!(tc.wait_for_match(None).await.unwrap(), t);
        tc.merge_insert(t2);
        assert_eq!(tc.wait_for_match(Some(default.to_string())).await.unwrap(), t);
        assert!(tc.wait_for_match(None).await.is_err());
        assert_eq!(tc.wait_for_match(Some("clam".to_string())).await.unwrap(), t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_update_connection_state() {
        let t = Target::new("have-you-seen-my-cat");
        let instant = Instant::now();
        let instant_clone = instant.clone();
        t.update_connection_state(move |s| {
            assert_eq!(s, ConnectionState::Disconnected);

            ConnectionState::Mdns(instant_clone)
        });
        assert_eq!(ConnectionState::Mdns(instant), t.inner.state.borrow_mut().connection_state);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expire_state_mdns() {
        let t = Target::new("yo-yo-ma-plays-that-cello-ya-hear");
        let then = Instant::now() - (MDNS_MAX_AGE + Duration::from_secs(1));
        t.update_connection_state(|_| ConnectionState::Mdns(then));

        t.expire_state();

        t.events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    ConnectionState::Mdns(then),
                    ConnectionState::Disconnected,
                )
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expire_state_fastboot() {
        let t = Target::new("platypodes-are-venomous");
        let then = Instant::now() - (FASTBOOT_MAX_AGE + Duration::from_secs(1));
        t.update_connection_state(|_| ConnectionState::Fastboot(then));

        t.expire_state();

        t.events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    ConnectionState::Fastboot(then),
                    ConnectionState::Disconnected,
                )
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expire_state_zedboot() {
        let t = Target::new("platypodes-are-venomous");
        let then = Instant::now() - (ZEDBOOT_MAX_AGE + Duration::from_secs(1));
        t.update_connection_state(|_| ConnectionState::Zedboot(then));

        t.expire_state();

        t.events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    ConnectionState::Zedboot(then),
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
            t.addrs_insert_entry(a);
        }

        // Removes expected duplicate address. Should be marked as a duplicate
        // and also removed from the very beginning as a more-recent version
        // is added later.
        addrs_post.remove(0);
        // The order should be: last one inserted should show up first.
        addrs_post.reverse();
        assert_eq!(addrs_post.drain(..).map(|e| e.addr).collect::<Vec<_>>(), t.addrs());
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
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(192, 168, 70, 68), 0)),
            expected.clone(),
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
            t.addrs_insert_entry(a);
        }
        assert_eq!(t.addrs().into_iter().next().unwrap(), TargetAddr::from(expected));
    }

    #[test]
    fn test_target_addr_entry_from_with_manual() {
        let ta = TargetAddrEntry::from((
            TargetAddr::from(("127.0.0.1".parse::<IpAddr>().unwrap(), 0)),
            Utc::now(),
            true,
        ));
        assert_eq!(ta.manual, true);
        let ta = TargetAddrEntry::from((
            TargetAddr::from(("127.0.0.1".parse::<IpAddr>().unwrap(), 0)),
            Utc::now(),
            false,
        ));
        assert_eq!(ta.manual, false);
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
        t2.inner.addrs.borrow_mut().replace(TargetAddr { ip, scope_id: 0 }.into());

        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t1);
        tc.merge_insert(t2);
        let mut targets = tc.targets().into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename_str(), "this-is-a-crunchy-falafel");
        let mut addrs = target.addrs().into_iter();
        let addr = addrs.next().expect("Merged target has no address.");
        assert!(addrs.next().is_none());
        assert_eq!(addr, TargetAddr { ip, scope_id: 0xbadf00d });
        assert_eq!(addr.ip, ip);
        assert_eq!(addr.scope_id, 0xbadf00d);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_does_not_merge_different_ports_with_no_name() {
        let ip = "::1".parse().unwrap();

        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip, scope_id: 0 });
        let t1 = Target::new_with_addrs(Option::<String>::None, addr_set.clone());
        t1.set_ssh_port(Some(8022));
        let t2 = Target::new_with_addrs(Option::<String>::None, addr_set.clone());
        t2.set_ssh_port(Some(8023));

        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t1);
        tc.merge_insert(t2);

        let mut targets = tc.targets().into_iter().collect::<Vec<Target>>();

        assert_eq!(targets.len(), 2);

        targets.sort_by(|a, b| a.ssh_port().cmp(&b.ssh_port()));
        let mut iter = targets.into_iter();
        let mut found1 = iter.next().expect("must have target one");
        let mut found2 = iter.next().expect("must have target two");

        // Avoid iterator order dependency
        if found1.ssh_port() == Some(8023) {
            std::mem::swap(&mut found1, &mut found2)
        }

        assert_eq!(found1.addrs().into_iter().next().unwrap().ip, ip);
        assert_eq!(found1.ssh_port(), Some(8022));

        assert_eq!(found2.addrs().into_iter().next().unwrap().ip, ip);
        assert_eq!(found2.ssh_port(), Some(8023));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_does_not_merge_different_ports() {
        let ip = "::1".parse().unwrap();

        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip, scope_id: 0 });
        let t1 = Target::new_with_addrs(Some("t1"), addr_set.clone());
        t1.set_ssh_port(Some(8022));
        let t2 = Target::new_with_addrs(Some("t2"), addr_set.clone());
        t2.set_ssh_port(Some(8023));

        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t1);
        tc.merge_insert(t2);

        let mut targets = tc.targets().into_iter().collect::<Vec<Target>>();

        assert_eq!(targets.len(), 2);

        targets.sort_by(|a, b| a.ssh_port().cmp(&b.ssh_port()));
        let mut iter = targets.into_iter();
        let found1 = iter.next().expect("must have target one");
        let found2 = iter.next().expect("must have target two");

        assert_eq!(found1.addrs().into_iter().next().unwrap().ip, ip);
        assert_eq!(found1.ssh_port(), Some(8022));
        assert_eq!(found1.nodename(), Some("t1".to_string()));

        assert_eq!(found2.addrs().into_iter().next().unwrap().ip, ip);
        assert_eq!(found2.ssh_port(), Some(8023));
        assert_eq!(found2.nodename(), Some("t2".to_string()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_wait_for_match_successful() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default);
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t));
        assert_eq!(tc.wait_for_match(Some(default.to_string())).await.unwrap(), t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_wait_for_match_ambiguous() {
        let default = "clam-chowder-is-tasty";
        let t = Target::new_autoconnected(default);
        let t2 = Target::new_autoconnected("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t));
        tc.merge_insert(t2);
        assert_eq!(Err(DaemonError::TargetAmbiguous), tc.wait_for_match(None).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_remove_unnamed_by_addr() {
        let ip1 = "f111::3".parse().unwrap();
        let ip2 = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip: ip1, scope_id: 0xbadf00d });
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        t2.inner.addrs.borrow_mut().replace(TargetAddr { ip: ip2, scope_id: 0 }.into());
        tc.merge_insert(t1);
        tc.merge_insert(t2);
        let mut targets = tc.targets().into_iter();
        let mut target1 = targets.next().expect("Merging resulted in no targets.");
        let mut target2 = targets.next().expect("Merging resulted in only one target.");

        if target1.nodename().is_none() {
            std::mem::swap(&mut target1, &mut target2)
        }

        assert!(targets.next().is_none());
        assert_eq!(target1.nodename_str(), "this-is-a-crunchy-falafel");
        assert_eq!(target2.nodename(), None);
        assert!(tc.remove_target("f111::3".to_owned()));
        let mut targets = tc.targets().into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename_str(), "this-is-a-crunchy-falafel");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_remove_named_by_addr() {
        let ip1 = "f111::3".parse().unwrap();
        let ip2 = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip: ip1, scope_id: 0xbadf00d });
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        t2.inner.addrs.borrow_mut().replace(TargetAddr { ip: ip2, scope_id: 0 }.into());
        tc.merge_insert(t1);
        tc.merge_insert(t2);
        let mut targets = tc.targets().into_iter();
        let mut target1 = targets.next().expect("Merging resulted in no targets.");
        let mut target2 = targets.next().expect("Merging resulted in only one target.");

        if target1.nodename().is_none() {
            std::mem::swap(&mut target1, &mut target2);
        }
        assert!(targets.next().is_none());
        assert_eq!(target1.nodename_str(), "this-is-a-crunchy-falafel");
        assert_eq!(target2.nodename(), None);
        assert!(tc.remove_target("f111::4".to_owned()));
        let mut targets = tc.targets().into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename(), None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_remove_by_name() {
        let ip1 = "f111::3".parse().unwrap();
        let ip2 = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip: ip1, scope_id: 0xbadf00d });
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        t2.inner.addrs.borrow_mut().replace(TargetAddr { ip: ip2, scope_id: 0 }.into());
        tc.merge_insert(t1);
        tc.merge_insert(t2);
        let mut targets = tc.targets().into_iter();
        let mut target1 = targets.next().expect("Merging resulted in no targets.");
        let mut target2 = targets.next().expect("Merging resulted in only one target.");

        if target1.nodename().is_none() {
            std::mem::swap(&mut target1, &mut target2);
        }

        assert!(targets.next().is_none());
        assert_eq!(target1.nodename_str(), "this-is-a-crunchy-falafel");
        assert_eq!(target2.nodename(), None);
        assert!(tc.remove_target("this-is-a-crunchy-falafel".to_owned()));
        let mut targets = tc.targets().into_iter();
        let target = targets.next().expect("Merging resulted in no targets.");
        assert!(targets.next().is_none());
        assert_eq!(target.nodename(), None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_match_serial() {
        let string = "turritopsis-dohrnii-is-an-immortal-jellyfish";
        let t = Target::new_with_serial(string);
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t));
        let found_target = tc.get(string).expect("target serial should match");
        assert_eq!(string, found_target.serial().expect("target should have serial number"));
        assert!(found_target.nodename().is_none());
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
    fn test_target_query_from_empty_string() {
        let query = TargetQuery::from(Some(""));
        assert!(matches!(query, TargetQuery::First));
    }

    #[test]
    fn test_target_query_with_no_scope_matches_scoped_target_info() {
        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        let tq = TargetQuery::from("fe80::dead:beef:beef:beef");
        assert!(tq.match_info(&TargetInfo { addresses: vec![addr], ..Default::default() }))
    }

    #[test]
    fn test_target_ssh_address_priority() {
        let start = std::time::SystemTime::now();
        use std::iter::FromIterator;

        // An empty set returns nothing.
        let addrs = BTreeSet::<TargetAddrEntry>::new();
        assert_eq!(ssh_address_from(addrs.iter()), None);

        // Given two addresses, from the exact same time, neither manual, prefer any link-local address.
        let addrs = BTreeSet::from_iter(vec![
            (("2000::1".parse().unwrap(), 0).into(), start.into(), false).into(),
            (("fe80::1".parse().unwrap(), 2).into(), start.into(), false).into(),
        ]);
        assert_eq!(ssh_address_from(addrs.iter()), Some(("fe80::1".parse().unwrap(), 2).into()));

        // Given two addresses, one link local the other not, prefer the link local even if older.
        let addrs = BTreeSet::from_iter(vec![
            (("2000::1".parse().unwrap(), 0).into(), start.into(), false).into(),
            (
                ("fe80::1".parse().unwrap(), 2).into(),
                (start - Duration::from_secs(1)).into(),
                false,
            )
                .into(),
        ]);
        assert_eq!(ssh_address_from(addrs.iter()), Some(("fe80::1".parse().unwrap(), 2).into()));

        // Given two addresses, both link-local, pick the one most recent.
        let addrs = BTreeSet::from_iter(vec![
            (("fe80::2".parse().unwrap(), 1).into(), start.into(), false).into(),
            (
                ("fe80::1".parse().unwrap(), 2).into(),
                (start - Duration::from_secs(1)).into(),
                false,
            )
                .into(),
        ]);
        assert_eq!(ssh_address_from(addrs.iter()), Some(("fe80::2".parse().unwrap(), 1).into()));

        // Given two addresses, one manual, old and non-local, prefer the manual entry.
        let addrs = BTreeSet::from_iter(vec![
            (("fe80::2".parse().unwrap(), 1).into(), start.into(), false).into(),
            (("2000::1".parse().unwrap(), 0).into(), (start - Duration::from_secs(1)).into(), true)
                .into(),
        ]);
        assert_eq!(ssh_address_from(addrs.iter()), Some(("2000::1".parse().unwrap(), 0).into()));

        // Given two addresses, neither local, neither manual, prefer the most recent.
        let addrs = BTreeSet::from_iter(vec![
            (("2000::1".parse().unwrap(), 0).into(), start.into(), false).into(),
            (
                ("2000::2".parse().unwrap(), 0).into(),
                (start + Duration::from_secs(1)).into(),
                false,
            )
                .into(),
        ]);
        assert_eq!(ssh_address_from(addrs.iter()), Some(("2000::2".parse().unwrap(), 0).into()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ssh_address_info_no_port_provides_default_port() {
        let target = Target::new_with_addr_entries(
            Some("foo"),
            vec![TargetAddrEntry::from((
                TargetAddr::from(("::1".parse::<IpAddr>().unwrap().into(), 0)),
                Utc::now(),
                true,
            ))]
            .into_iter(),
        );

        let (ip, port) = match target.ssh_address_info().unwrap() {
            TargetAddrInfo::IpPort(TargetIpPort { ip, port, .. }) => match ip {
                IpAddress::Ipv4(i) => (IpAddr::from(i.addr), port),
                IpAddress::Ipv6(i) => (IpAddr::from(i.addr), port),
            },
            _ => panic!("unexpected type"),
        };

        assert_eq!(ip, "::1".parse::<IpAddr>().unwrap());
        assert_eq!(port, 22);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ssh_address_info_with_port() {
        let target = Target::new_with_addr_entries(
            Some("foo"),
            vec![TargetAddrEntry::from((
                TargetAddr::from(("::1".parse::<IpAddr>().unwrap().into(), 0)),
                Utc::now(),
                true,
            ))]
            .into_iter(),
        );
        target.set_ssh_port(Some(8022));

        let (ip, port) = match target.ssh_address_info().unwrap() {
            TargetAddrInfo::IpPort(TargetIpPort { ip, port, .. }) => match ip {
                IpAddress::Ipv4(i) => (IpAddr::from(i.addr), port),
                IpAddress::Ipv6(i) => (IpAddr::from(i.addr), port),
            },
            _ => panic!("unexpected type"),
        };

        assert_eq!(ip, "::1".parse::<IpAddr>().unwrap());
        assert_eq!(port, 8022);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_ambiguous_target_when_disconnected() {
        // While this is an implementation detail, the naming of these targets
        // are important. The match order of the targets, if not filtering by
        // whether they are connected, is such that the disconnected target
        // would come first. With filtering, though, the "this-is-connected"
        // target would be found.
        let t = Target::new_autoconnected("this-is-connected");
        let t2 = Target::new("this-is-not-connected");
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t2));
        tc.merge_insert(clone_target(&t));
        let found_target = tc.wait_for_match(None).await.expect("should match");
        assert_eq!(
            "this-is-connected",
            found_target.nodename().expect("target should have nodename")
        );
        let found_target =
            tc.wait_for_match(Some("connected".to_owned())).await.expect("should match");
        assert_eq!(
            "this-is-connected",
            found_target.nodename().expect("target should have nodename")
        );
    }

    #[test]
    fn test_target_addr_info_to_socketaddr() {
        let tai = TargetAddrInfo::IpPort(TargetIpPort {
            ip: IpAddress::Ipv4(Ipv4Address { addr: [127, 0, 0, 1] }),
            port: 8022,
            scope_id: 0,
        });

        let sa = "127.0.0.1:8022".parse::<SocketAddr>().unwrap();

        assert_eq!(target_addr_info_to_socketaddr(tai), sa);

        let tai = TargetAddrInfo::Ip(TargetIp {
            ip: IpAddress::Ipv4(Ipv4Address { addr: [127, 0, 0, 1] }),
            scope_id: 0,
        });

        let sa = "127.0.0.1:0".parse::<SocketAddr>().unwrap();

        assert_eq!(target_addr_info_to_socketaddr(tai), sa);

        let tai = TargetAddrInfo::IpPort(TargetIpPort {
            ip: IpAddress::Ipv6(Ipv6Address {
                addr: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
            }),
            port: 8022,
            scope_id: 0,
        });

        let sa = "[::1]:8022".parse::<SocketAddr>().unwrap();

        assert_eq!(target_addr_info_to_socketaddr(tai), sa);

        let tai = TargetAddrInfo::Ip(TargetIp {
            ip: IpAddress::Ipv6(Ipv6Address {
                addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
            }),
            scope_id: 1,
        });

        let sa = "[fe80::1%1]:0".parse::<SocketAddr>().unwrap();

        assert_eq!(target_addr_info_to_socketaddr(tai), sa);

        let tai = TargetAddrInfo::IpPort(TargetIpPort {
            ip: IpAddress::Ipv6(Ipv6Address {
                addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
            }),
            port: 8022,
            scope_id: 1,
        });

        let sa = "[fe80::1%1]:8022".parse::<SocketAddr>().unwrap();

        assert_eq!(target_addr_info_to_socketaddr(tai), sa);
    }

    #[test]
    fn test_target_addr_entry_from_with_netsvc() {
        let ta = TargetAddrEntry::from((
            TargetAddr::from(("127.0.0.1".parse::<IpAddr>().unwrap(), 0)),
            Utc::now(),
            false,
            true,
        ));
        assert_eq!(ta.netsvc, true);
        let ta = TargetAddrEntry::from((
            TargetAddr::from(("127.0.0.1".parse::<IpAddr>().unwrap(), 0)),
            Utc::now(),
            false,
            false,
        ));
        assert_eq!(ta.netsvc, false);
    }

    #[test]
    fn test_netsvc_target_has_no_ssh() {
        let start = std::time::SystemTime::now();
        use std::iter::FromIterator;

        // An empty set returns nothing.
        let addrs = BTreeSet::<TargetAddrEntry>::new();
        assert_eq!(ssh_address_from(addrs.iter()), None);

        let addrs = BTreeSet::from_iter(vec![(
            ("2000::1".parse().unwrap(), 0).into(),
            start.into(),
            false,
            true,
        )
            .into()]);
        assert_eq!(ssh_address_from(addrs.iter()), None);

        let addrs = BTreeSet::from_iter(vec![
            (("2000::1".parse().unwrap(), 0).into(), start.into(), false, true).into(),
            (("fe80::1".parse().unwrap(), 0).into(), start.into(), false).into(),
        ]);

        assert_eq!(ssh_address_from(addrs.iter()), Some(("fe80::1".parse().unwrap(), 0).into()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_netsvc_ssh_address_info_should_be_none() {
        let ip = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr { ip, scope_id: 0xbadf00d });
        let target = Target::new_with_netsvc_addrs(Some("foo"), addr_set);

        assert!(target.ssh_address_info().is_none());
    }
}

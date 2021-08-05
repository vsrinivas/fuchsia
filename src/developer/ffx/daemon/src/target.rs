// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{FASTBOOT_MAX_AGE, MDNS_MAX_AGE, ZEDBOOT_MAX_AGE},
    crate::fastboot::open_interface_with_serial,
    crate::logger::{streamer::DiagnosticsStreamer, Logger},
    crate::onet::HostPipeConnection,
    addr::TargetAddr,
    anyhow::{anyhow, bail, Error, Result},
    async_trait::async_trait,
    bridge::{DaemonError, TargetAddrInfo, TargetIpPort},
    chrono::{DateTime, Utc},
    ffx_daemon_core::events::{self, EventSynthesizer},
    ffx_daemon_events::{DaemonEvent, TargetConnectionState, TargetEvent, TargetInfo},
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_bridge::TargetState,
    fidl_fuchsia_developer_remotecontrol::{IdentifyHostResponse, RemoteControlProxy},
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    fuchsia_async::Task,
    netext::IsLocalAddr,
    rand::random,
    rcs::{RcsConnection, RcsConnectionError},
    std::cell::RefCell,
    std::cmp::Ordering,
    std::collections::{BTreeSet, HashMap, HashSet},
    std::default::Default,
    std::fmt,
    std::fmt::Debug,
    std::hash::Hash,
    std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    std::rc::{Rc, Weak},
    std::sync::Arc,
    std::time::{Duration, Instant},
    timeout::timeout,
    usb_bulk::AsyncInterface as Interface,
};

const IDENTIFY_HOST_TIMEOUT_MILLIS: u64 = 1000;
const DEFAULT_SSH_PORT: u16 = 22;

#[derive(Debug, Clone, Hash)]
pub(crate) enum TargetAddrType {
    Ssh,
    Manual,
    Netsvc,
    Fastboot,
}

#[derive(Debug, Clone, Hash)]
pub(crate) struct TargetAddrEntry {
    addr: TargetAddr,
    timestamp: DateTime<Utc>,
    addr_type: TargetAddrType,
}

impl PartialEq for TargetAddrEntry {
    fn eq(&self, other: &Self) -> bool {
        self.addr.eq(&other.addr)
    }
}

impl Eq for TargetAddrEntry {}

impl TargetAddrEntry {
    pub(crate) fn new(
        addr: TargetAddr,
        timestamp: DateTime<Utc>,
        addr_type: TargetAddrType,
    ) -> Self {
        Self { addr, timestamp, addr_type }
    }
}

#[cfg(test)]
impl From<TargetAddr> for TargetAddrEntry {
    fn from(addr: TargetAddr) -> Self {
        Self { addr, timestamp: Utc::now(), addr_type: TargetAddrType::Ssh }
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BuildConfig {
    pub product_config: String,
    pub board_config: String,
}

// TargetEventSynthesizer resolves by weak reference the embedded event
// queue's need for a self reference.
#[derive(Default)]
struct TargetEventSynthesizer {
    target: RefCell<Weak<Target>>,
}

#[async_trait(?Send)]
impl EventSynthesizer<TargetEvent> for TargetEventSynthesizer {
    async fn synthesize_events(&self) -> Vec<TargetEvent> {
        match self.target.borrow().upgrade() {
            Some(target) => match target.get_connection_state() {
                TargetConnectionState::Rcs(_) => vec![TargetEvent::RcsActivated],
                _ => vec![],
            },
            None => vec![],
        }
    }
}

pub struct Target {
    pub events: events::Queue<TargetEvent>,

    host_pipe: RefCell<Option<Task<()>>>,
    logger: Rc<RefCell<Option<Task<()>>>>,

    // id is the locally created "primary identifier" for this target.
    id: u64,
    // ids keeps track of additional ids discovered over Overnet, these could
    // come from old Daemons, or other Daemons. The set should be used
    ids: RefCell<HashSet<u64>>,
    nodename: RefCell<Option<String>>,
    state: RefCell<TargetConnectionState>,
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

    // The event synthesizer is retained on the target as a strong
    // reference, as the queue only retains a weak reference.
    target_event_synthesizer: Rc<TargetEventSynthesizer>,
}

impl Target {
    pub fn new() -> Rc<Self> {
        let target_event_synthesizer = Rc::new(TargetEventSynthesizer::default());
        let events = events::Queue::new(&target_event_synthesizer);

        let id = random::<u64>();
        let mut ids = HashSet::new();
        ids.insert(id.clone());

        let target = Rc::new(Self {
            id: id.clone(),
            ids: RefCell::new(ids),
            nodename: RefCell::new(None),
            last_response: RefCell::new(Utc::now()),
            state: RefCell::new(Default::default()),
            addrs: RefCell::new(BTreeSet::new()),
            ssh_port: RefCell::new(None),
            serial: RefCell::new(None),
            boot_timestamp_nanos: RefCell::new(None),
            build_config: Default::default(),
            diagnostics_info: Arc::new(DiagnosticsStreamer::default()),
            events,
            host_pipe: Default::default(),
            logger: Default::default(),
            target_event_synthesizer,
        });
        target.target_event_synthesizer.target.replace(Rc::downgrade(&target));
        target
    }

    pub fn new_named<S>(nodename: S) -> Rc<Self>
    where
        S: Into<String>,
    {
        let target = Self::new();
        target.nodename.replace(Some(nodename.into()));
        target
    }

    pub fn new_with_boot_timestamp<S>(nodename: S, boot_timestamp_nanos: u64) -> Rc<Self>
    where
        S: Into<String>,
    {
        let target = Self::new_named(nodename);
        target.boot_timestamp_nanos.replace(Some(boot_timestamp_nanos));
        target
    }

    pub fn new_with_addrs<S>(nodename: Option<S>, addrs: BTreeSet<TargetAddr>) -> Rc<Self>
    where
        S: Into<String>,
    {
        let target = Self::new();
        target.nodename.replace(nodename.map(Into::into));
        let now = Utc::now();
        target.addrs_extend(
            addrs.iter().map(|addr| TargetAddrEntry::new(*addr, now.clone(), TargetAddrType::Ssh)),
        );
        target
    }

    pub(crate) fn new_with_addr_entries<S, I>(nodename: Option<S>, entries: I) -> Rc<Self>
    where
        S: Into<String>,
        I: Iterator<Item = TargetAddrEntry>,
    {
        use std::iter::FromIterator;
        let target = Self::new();
        target.nodename.replace(nodename.map(Into::into));
        target.addrs.replace(BTreeSet::from_iter(entries));
        target
    }

    pub fn new_with_fastboot_addrs<S>(nodename: Option<S>, addrs: BTreeSet<TargetAddr>) -> Rc<Self>
    where
        S: Into<String>,
    {
        let target = Self::new();
        target.nodename.replace(nodename.map(Into::into));
        target.addrs.replace(
            addrs
                .iter()
                .map(|e| TargetAddrEntry::new(*e, Utc::now(), TargetAddrType::Fastboot))
                .collect(),
        );
        target.update_connection_state(|_| TargetConnectionState::Fastboot(Instant::now()));
        target
    }

    pub fn new_with_netsvc_addrs<S>(nodename: Option<S>, addrs: BTreeSet<TargetAddr>) -> Rc<Self>
    where
        S: Into<String>,
    {
        let target = Self::new();
        target.nodename.replace(nodename.map(Into::into));
        target.addrs.replace(
            addrs
                .iter()
                .map(|e| TargetAddrEntry::new(*e, Utc::now(), TargetAddrType::Netsvc))
                .collect(),
        );
        target.update_connection_state(|_| TargetConnectionState::Zedboot(Instant::now()));
        target
    }

    pub fn new_with_serial(serial: &str) -> Rc<Self> {
        let target = Self::new();
        target.serial.replace(Some(serial.to_string()));
        target.update_connection_state(|_| TargetConnectionState::Fastboot(Instant::now()));
        target
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time<S: Into<String>>(nodename: S, time: DateTime<Utc>) -> Rc<Self> {
        let target = Self::new_named(nodename);
        target.last_response.replace(time);
        target
    }

    pub fn from_target_info(mut t: TargetInfo) -> Rc<Self> {
        if let Some(s) = t.serial {
            Self::new_with_serial(&s)
        } else {
            Self::new_with_addrs(t.nodename.take(), t.addresses.drain(..).collect())
        }
    }

    pub fn from_netsvc_target_info(mut t: TargetInfo) -> Rc<Self> {
        Self::new_with_netsvc_addrs(t.nodename.take(), t.addresses.drain(..).collect())
    }

    pub fn from_fastboot_target_info(mut t: TargetInfo) -> Rc<Self> {
        Self::new_with_fastboot_addrs(t.nodename.take(), t.addresses.drain(..).collect())
    }

    pub fn target_info(&self) -> TargetInfo {
        TargetInfo {
            nodename: self.nodename(),
            addresses: self.addrs(),
            serial: self.serial(),
            ssh_port: self.ssh_port(),
            is_fastboot: matches!(self.get_connection_state(), TargetConnectionState::Fastboot(_)),
        }
    }

    // Get the locally minted identifier for the target
    pub fn id(&self) -> u64 {
        self.id
    }

    // Get all known ids for the target
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

    pub fn merge_ids<'a, I>(&self, new_ids: I)
    where
        I: Iterator<Item = &'a u64>,
    {
        let mut my_ids = self.ids.borrow_mut();
        for id in new_ids {
            my_ids.insert(*id);
        }
    }

    /// ssh_address returns the SocketAddr of the next SSH address to connect to for this target.
    ///
    /// The sort algorithm for SSH address priority is in order of:
    /// - Manual addresses first
    ///   - By recency of observation
    /// - Other addresses
    ///   - By link-local first
    ///   - By most recently observed
    ///
    /// The host-pipe connection mechanism will requests addresses from this function on each
    /// connection attempt.
    pub fn ssh_address(&self) -> Option<SocketAddr> {
        use itertools::Itertools;

        // Order e1 & e2 by most recent timestamp
        let recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| e2.timestamp.cmp(&e1.timestamp);

        // Order by link-local first, then by recency
        let link_local_recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| match (
            e1.addr.ip().is_link_local_addr(),
            e2.addr.ip().is_link_local_addr(),
        ) {
            (true, true) | (false, false) => recency(e1, e2),
            (true, false) => Ordering::Less,
            (false, true) => Ordering::Greater,
        };

        let manual_link_local_recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| {
            match (&e1.addr_type, &e2.addr_type) {
                // Note: for manually added addresses, they are ordered strictly
                // by recency, not link-local first.
                (TargetAddrType::Manual, TargetAddrType::Manual) => recency(e1, e2),
                (TargetAddrType::Manual, TargetAddrType::Ssh) => Ordering::Less,
                (TargetAddrType::Ssh, TargetAddrType::Manual) => Ordering::Greater,
                (TargetAddrType::Ssh, TargetAddrType::Ssh) => link_local_recency(e1, e2),
                _ => Ordering::Less, // Should not get here due to filtering in next line.
            }
        };

        let target_addr = self
            .addrs
            .borrow()
            .iter()
            .filter(|t| match t.addr_type {
                TargetAddrType::Manual | TargetAddrType::Ssh => true,
                _ => false,
            })
            .sorted_by(|e1, e2| manual_link_local_recency(e1, e2))
            .next()
            .map(|e| e.addr);

        target_addr.map(|target_addr| {
            let mut socket_addr: SocketAddr = target_addr.into();
            socket_addr.set_port(self.ssh_port().unwrap_or(DEFAULT_SSH_PORT));
            socket_addr
        })
    }

    pub fn netsvc_address(&self) -> Option<TargetAddr> {
        use itertools::Itertools;
        // Order e1 & e2 by most recent timestamp
        let recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| e2.timestamp.cmp(&e1.timestamp);
        self.addrs
            .borrow()
            .iter()
            .sorted_by(|e1, e2| recency(e1, e2))
            .find(|t| match t.addr_type {
                TargetAddrType::Netsvc => true,
                _ => false,
            })
            .map(|addr_entry| addr_entry.addr.clone())
    }

    pub fn fastboot_address(&self) -> Option<TargetAddr> {
        use itertools::Itertools;
        // Order e1 & e2 by most recent timestamp
        let recency = |e1: &TargetAddrEntry, e2: &TargetAddrEntry| e2.timestamp.cmp(&e1.timestamp);
        self.addrs
            .borrow()
            .iter()
            .sorted_by(|e1, e2| recency(e1, e2))
            .find(|t| match t.addr_type {
                TargetAddrType::Fastboot => true,
                _ => false,
            })
            .map(|addr_entry| addr_entry.addr.clone())
    }

    pub fn ssh_address_info(&self) -> Option<bridge::TargetAddrInfo> {
        if let Some(addr) = self.ssh_address() {
            let ip = match addr.ip() {
                IpAddr::V6(i) => IpAddress::Ipv6(Ipv6Address { addr: i.octets().into() }),
                IpAddr::V4(i) => IpAddress::Ipv4(Ipv4Address { addr: i.octets().into() }),
            };

            let scope_id = match addr {
                SocketAddr::V6(ref v6) => v6.scope_id(),
                _ => 0,
            };

            let port = self.ssh_port().unwrap_or(DEFAULT_SSH_PORT);

            Some(TargetAddrInfo::IpPort(TargetIpPort { ip, port, scope_id }))
        } else {
            None
        }
    }

    fn rcs_state(&self) -> bridge::RemoteControlState {
        match (self.is_host_pipe_running(), self.get_connection_state()) {
            (true, TargetConnectionState::Rcs(_)) => bridge::RemoteControlState::Up,
            (true, _) => bridge::RemoteControlState::Down,
            (_, _) => bridge::RemoteControlState::Unknown,
        }
    }

    pub fn nodename(&self) -> Option<String> {
        self.nodename.borrow().clone()
    }

    pub fn nodename_str(&self) -> String {
        self.nodename.borrow().clone().unwrap_or("<unknown>".to_owned())
    }

    pub fn set_nodename(&self, nodename: String) {
        self.nodename.borrow_mut().replace(nodename);
    }

    pub fn boot_timestamp_nanos(&self) -> Option<u64> {
        self.boot_timestamp_nanos.borrow().clone()
    }

    pub fn update_boot_timestamp(&self, ts: Option<u64>) {
        self.boot_timestamp_nanos.replace(ts);
    }

    pub fn stream_info(&self) -> Arc<DiagnosticsStreamer<'static>> {
        self.diagnostics_info.clone()
    }

    pub fn serial(&self) -> Option<String> {
        self.serial.borrow().clone()
    }

    pub fn state(&self) -> TargetConnectionState {
        self.state.borrow().clone()
    }

    #[cfg(test)]
    pub fn set_state(&self, state: TargetConnectionState) {
        // Note: Do not mark this function non-test, as it does not
        // enforce state transition control, such as ensuring that
        // manual targets do not enter the disconnected state. It must
        // only be used in tests.
        self.state.replace(state);
    }

    pub fn get_connection_state(&self) -> TargetConnectionState {
        self.state.borrow().clone()
    }

    /// Propose a target connection state transition from the state passed to the provided FnOnce to
    /// the state returned by the FnOnce. Some proposals are adjusted before application, as below.
    /// If the target state reaches RCS, an RcsActivated event is produced. If the proposal results
    /// in a state change, a ConnectionStateChanged event is produced.
    ///
    ///   RCS  ->   MDNS          =>  RCS (does not drop RCS state)
    ///   *    ->   Disconnected  =>  Manual if the device is manual
    pub fn update_connection_state<F>(&self, func: F)
    where
        F: FnOnce(TargetConnectionState) -> TargetConnectionState + Sized,
    {
        let former_state = self.get_connection_state();
        let mut new_state = (func)(former_state.clone());

        match &new_state {
            TargetConnectionState::Disconnected => {
                if self.is_manual() {
                    new_state = TargetConnectionState::Manual;
                }
            }
            TargetConnectionState::Mdns(_) => {
                // Do not transition connection state for RCS -> MDNS.
                if former_state.is_rcs() {
                    self.update_last_response(Utc::now());
                    return;
                }
            }
            _ => {}
        }

        if former_state == new_state {
            return;
        }

        self.state.replace(new_state);

        if self.get_connection_state().is_rcs() {
            self.events.push(TargetEvent::RcsActivated).unwrap_or_else(|err| {
                log::warn!("unable to enqueue RCS activation event: {:#}", err)
            });
        }

        self.events
            .push(TargetEvent::ConnectionStateChanged(former_state, self.state.borrow().clone()))
            .unwrap_or_else(|e| log::error!("Failed to push state change for {:?}: {:?}", self, e));
    }

    pub fn rcs(&self) -> Option<RcsConnection> {
        match self.get_connection_state() {
            TargetConnectionState::Rcs(conn) => Some(conn),
            _ => None,
        }
    }

    pub fn usb(&self) -> (String, Option<Interface>) {
        match self.serial.borrow().as_ref() {
            Some(s) => (s.to_string(), open_interface_with_serial(s).ok()),
            None => ("".to_string(), None),
        }
    }

    pub fn last_response(&self) -> DateTime<Utc> {
        self.last_response.borrow().clone()
    }

    pub fn build_config(&self) -> Option<BuildConfig> {
        self.build_config.borrow().clone()
    }

    pub fn addrs(&self) -> Vec<TargetAddr> {
        let mut addrs = self.addrs.borrow().iter().cloned().collect::<Vec<_>>();
        addrs.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));
        addrs.drain(..).map(|e| e.addr).collect()
    }

    pub fn drop_unscoped_link_local_addrs(&self) {
        let mut addrs = self.addrs.borrow_mut();

        *addrs = addrs
            .clone()
            .into_iter()
            .filter(|entry| match (&entry.addr_type, &entry.addr.ip()) {
                (TargetAddrType::Manual, _) => true,
                (_, IpAddr::V6(v)) => entry.addr.scope_id() != 0 || !v.is_link_local_addr(),
                _ => true,
            })
            .collect();
    }

    pub fn drop_loopback_addrs(&self) {
        let mut addrs = self.addrs.borrow_mut();

        *addrs = addrs
            .clone()
            .into_iter()
            .filter(|entry| match (&entry.addr_type, &entry.addr.ip()) {
                (TargetAddrType::Manual, _) => true,
                (_, IpAddr::V4(v)) => !v.is_loopback(),
                _ => true,
            })
            .collect();
    }

    pub fn ssh_port(&self) -> Option<u16> {
        self.ssh_port.borrow().clone()
    }

    pub(crate) fn set_ssh_port(&self, port: Option<u16>) {
        self.ssh_port.replace(port);
    }

    pub(crate) fn manual_addrs(&self) -> Vec<TargetAddr> {
        self.addrs
            .borrow()
            .iter()
            .filter_map(|entry| match entry.addr_type {
                TargetAddrType::Manual => Some(entry.addr.clone()),
                _ => None,
            })
            .collect()
    }

    #[cfg(test)]
    pub(crate) fn addrs_insert(&self, t: TargetAddr) {
        self.addrs.borrow_mut().replace(t.into());
    }

    #[cfg(test)]
    pub fn new_autoconnected(n: &str) -> Rc<Self> {
        let s = Self::new_named(n);
        s.update_connection_state(|s| {
            assert_eq!(s, TargetConnectionState::Disconnected);
            TargetConnectionState::Mdns(Instant::now())
        });
        s
    }

    #[cfg(test)]
    pub(crate) fn addrs_insert_entry(&self, t: TargetAddrEntry) {
        self.addrs.borrow_mut().replace(t);
    }

    fn addrs_extend<T>(&self, new_addrs: T)
    where
        T: IntoIterator<Item = TargetAddrEntry>,
    {
        let mut addrs = self.addrs.borrow_mut();

        for mut addr in new_addrs.into_iter() {
            // Do not add localhost to the collection during extend.
            // Note: localhost addresses are added sometimes by direct
            // insertion, in the manual add case.
            let localhost_v4 = IpAddr::V4(Ipv4Addr::LOCALHOST);
            let localhost_v6 = IpAddr::V6(Ipv6Addr::LOCALHOST);
            if addr.addr.ip() == localhost_v4 || addr.addr.ip() == localhost_v6 {
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
            if addr.addr.ip().is_ipv6() && addr.addr.scope_id() == 0 {
                if let Some(entry) = addrs.get(&addr) {
                    addr.addr.set_scope_id(entry.addr.scope_id());
                }

                // Note: not adding ipv6 link-local addresses without scopes here is deliberate!
                if addr.addr.ip().is_link_local_addr() && addr.addr.scope_id() == 0 {
                    continue;
                }
            }
            addrs.replace(addr);
        }
    }

    fn update_last_response(&self, other: DateTime<Utc>) {
        let mut last_response = self.last_response.borrow_mut();
        if *last_response < other {
            *last_response = other;
        }
    }

    pub fn from_identify(identify: IdentifyHostResponse) -> Result<Rc<Self>, Error> {
        // TODO(raggi): allow targets to truly be created without a nodename.
        let nodename = match identify.nodename {
            Some(n) => n,
            None => bail!("Target identification missing a nodename: {:?}", identify),
        };

        let target = Target::new_named(nodename);
        target.update_last_response(Utc::now().into());
        if let Some(ids) = identify.ids {
            target.merge_ids(ids.iter());
        }
        *target.build_config.borrow_mut() =
            if identify.board_config.is_some() || identify.product_config.is_some() {
                let p = identify.product_config.unwrap_or("<unknown>".to_string());
                let b = identify.board_config.unwrap_or("<unknown>".to_string());
                Some(BuildConfig { product_config: p, board_config: b })
            } else {
                None
            };

        if let Some(serial) = identify.serial_number {
            target.serial.borrow_mut().replace(serial);
        }
        if let Some(t) = identify.boot_timestamp_nanos {
            target.boot_timestamp_nanos.borrow_mut().replace(t);
        }
        if let Some(addrs) = identify.addresses {
            let mut taddrs = target.addrs.borrow_mut();
            let now = Utc::now();
            for addr in addrs.iter().map(|addr| {
                TargetAddrEntry::new(
                    TargetAddr::from(addr.clone()),
                    now.clone(),
                    TargetAddrType::Ssh,
                )
            }) {
                taddrs.insert(addr);
            }
        }
        Ok(target)
    }

    pub async fn from_rcs_connection(rcs: RcsConnection) -> Result<Rc<Self>, RcsConnectionError> {
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
        target.update_connection_state(move |_| TargetConnectionState::Rcs(rcs));
        Ok(target)
    }

    pub fn run_host_pipe(self: &Rc<Self>) {
        if self.host_pipe.borrow().is_some() {
            return;
        }

        let weak_target = Rc::downgrade(self);
        self.host_pipe.borrow_mut().replace(Task::local(async move {
            let r = HostPipeConnection::new(weak_target.clone()).await;
            // XXX(raggi): decide what to do with this log data:
            log::info!("HostPipeConnection returned: {:?}", r);
            weak_target.upgrade().and_then(|target| target.host_pipe.borrow_mut().take());
        }));
    }

    pub fn is_host_pipe_running(&self) -> bool {
        self.host_pipe.borrow().is_some()
    }

    pub fn run_logger(self: &Rc<Self>) {
        if self.logger.borrow().is_none() {
            let logger = Rc::downgrade(&self.logger);
            let weak_target = Rc::downgrade(self);
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

    pub async fn init_remote_proxy(self: &Rc<Self>) -> Result<RemoteControlProxy> {
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
                TargetConnectionState::Mdns(_) => MDNS_MAX_AGE,
                TargetConnectionState::Fastboot(_) => FASTBOOT_MAX_AGE,
                TargetConnectionState::Zedboot(_) => ZEDBOOT_MAX_AGE,
                _ => Duration::default(),
            };

            let new_state = match &current_state {
                TargetConnectionState::Mdns(ref last_seen)
                | TargetConnectionState::Fastboot(ref last_seen)
                | TargetConnectionState::Zedboot(ref last_seen) => {
                    if last_seen.elapsed() > expire_duration {
                        Some(TargetConnectionState::Disconnected)
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

    pub fn is_connected(&self) -> bool {
        self.state.borrow().is_connected()
    }

    pub fn is_manual(&self) -> bool {
        self.addrs
            .borrow()
            .iter()
            .any(|addr_entry| matches!(addr_entry.addr_type, TargetAddrType::Manual))
    }

    pub fn disconnect(&self) {
        drop(self.host_pipe.take());
        self.update_connection_state(|_| TargetConnectionState::Disconnected);
    }
}

impl From<&Target> for bridge::Target {
    fn from(target: &Target) -> Self {
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
            target_state: Some(match target.state() {
                TargetConnectionState::Disconnected => TargetState::Disconnected,
                TargetConnectionState::Manual
                | TargetConnectionState::Mdns(_)
                | TargetConnectionState::Rcs(_) => TargetState::Product,
                TargetConnectionState::Fastboot(_) => TargetState::Fastboot,
                TargetConnectionState::Zedboot(_) => TargetState::Zedboot,
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
        f.debug_struct("Target")
            .field("id", &self.id)
            .field("ids", &self.ids.borrow().clone())
            .field("nodename", &self.nodename.borrow().clone())
            .field("state", &self.state.borrow().clone())
            .field("last_response", &self.last_response.borrow().clone())
            .field("addrs", &self.addrs.borrow().clone())
            .field("ssh_port", &self.ssh_port.borrow().clone())
            .field("serial", &self.serial.borrow().clone())
            .field("boot_timestamp_nanos", &self.boot_timestamp_nanos.borrow().clone())
            // TODO(raggi): add task fields
            .finish()
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
                a == addr || addr.scope_id() == 0 && a.ip() == addr.ip()
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
    targets: RefCell<HashMap<u64, Rc<Target>>>,
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
            if target.is_connected() {
                res.push(DaemonEvent::NewTarget(target.target_info()));
            }
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

    pub fn targets(&self) -> Vec<Rc<Target>> {
        self.targets.borrow().values().cloned().collect()
    }

    pub fn is_empty(&self) -> bool {
        self.targets.borrow().len() == 0
    }

    pub fn remove_target(&self, target_id: String) -> bool {
        if let Some(t) = self.get(target_id) {
            let target = self.targets.borrow_mut().remove(&t.id());
            if let Some(target) = target {
                target.disconnect()
            }
            true
        } else {
            false
        }
    }

    fn find_matching_target(&self, new_target: &Target) -> Option<Rc<Target>> {
        // Look for a target by primary ID first
        let new_ids = new_target.ids();
        let mut to_update =
            new_ids.iter().find_map(|id| self.targets.borrow().get(id).map(|t| t.clone()));

        // If we haven't yet found a target, try to find one by all IDs, nodename, serial, or address.
        if to_update.is_none() {
            let new_nodename = new_target.nodename();
            let new_ips =
                new_target.addrs().iter().map(|addr| addr.ip().clone()).collect::<Vec<IpAddr>>();
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
                        && target.addrs().iter().any(|addr| new_ips.contains(&addr.ip())))
                {
                    to_update.replace(target.clone());
                    break;
                }
            }
        }

        to_update
    }

    pub fn merge_insert(&self, new_target: Rc<Target>) -> Rc<Target> {
        // Drop non-manual loopback address entries, as matching against
        // them could otherwise match every target in the collection.
        new_target.drop_loopback_addrs();

        let to_update = self.find_matching_target(&new_target);

        log::trace!("Merging target {:?} into {:?}", new_target, to_update);

        // Do not merge unscoped link-local addresses into the target
        // collection, as they are not routable and therefore not safe
        // for connecting to the remote, and may collide with other
        // scopes.
        new_target.drop_unscoped_link_local_addrs();

        if let Some(to_update) = to_update {
            if let Some(config) = new_target.build_config() {
                to_update.build_config.borrow_mut().replace(config);
            }
            if let Some(serial) = new_target.serial() {
                to_update.serial.borrow_mut().replace(serial);
            }
            if let Some(new_name) = new_target.nodename() {
                to_update.set_nodename(new_name);
            }

            to_update.update_last_response(new_target.last_response());
            let mut addrs = new_target.addrs.borrow().iter().cloned().collect::<Vec<_>>();
            addrs.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));
            to_update.addrs_extend(addrs.drain(..).collect::<Vec<TargetAddrEntry>>());
            to_update.update_boot_timestamp(new_target.boot_timestamp_nanos());

            to_update.update_connection_state(|current_state| {
                let new_state = new_target.get_connection_state();
                match (&current_state, &new_state) {
                    // Don't downgrade a targets connection state / drop RCS
                    (TargetConnectionState::Rcs(_), TargetConnectionState::Mdns(_)) => {
                        current_state
                    }
                    _ => new_state,
                }
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
    pub async fn wait_for_match(&self, matcher: Option<String>) -> Result<Rc<Target>, DaemonError> {
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

    pub fn get_connected<TQ>(&self, tq: TQ) -> Option<Rc<Target>>
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

    pub fn get<TQ>(&self, t: TQ) -> Option<Rc<Target>>
    where
        TQ: Into<TargetQuery>,
    {
        let query: TargetQuery = t.into();
        self.targets
            .borrow()
            .values()
            // TODO(raggi): cleanup query matching so that targets can match themselves against a query
            .find(|target| query.match_info(&target.target_info()))
            .map(Clone::clone)
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        anyhow::Context as _,
        bridge::TargetIp,
        chrono::offset::TimeZone,
        fidl, fidl_fuchsia_developer_remotecontrol as rcs,
        fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
        fidl_fuchsia_net::Subnet,
        fidl_fuchsia_overnet_protocol::NodeId,
        futures::prelude::*,
        matches::assert_matches,
        std::net::{Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6},
    };

    const DEFAULT_PRODUCT_CONFIG: &str = "core";
    const DEFAULT_BOARD_CONFIG: &str = "x64";
    const TEST_SERIAL: &'static str = "test-serial";

    fn clone_target(target: &Target) -> Rc<Target> {
        let new = Target::new();
        new.nodename.replace(target.nodename());
        // Note: ID is omitted deliberately, as ID merging is unconditional on
        // match, which breaks some uses of this helper function.
        new.ids.replace(target.ids.borrow().clone());
        new.state.replace(target.state.borrow().clone());
        new.addrs.replace(target.addrs.borrow().clone());
        new.ssh_port.replace(target.ssh_port.borrow().clone());
        new.serial.replace(target.serial.borrow().clone());
        new.boot_timestamp_nanos.replace(target.boot_timestamp_nanos.borrow().clone());
        new.build_config.replace(target.build_config.borrow().clone());
        new.last_response.replace(target.last_response.borrow().clone());
        // TODO(raggi): there are missing fields here, as there were before the
        // refactor in which I introduce this comment. It should be a goal to
        // remove this helper function over time.
        new
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
                && *self.last_response.borrow() == *o.last_response.borrow()
                && self.addrs() == o.addrs()
                && *self.state.borrow() == *o.state.borrow()
                && self.build_config() == o.build_config()
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new_not_connected() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, fake_now());
        tc.merge_insert(clone_target(&t));
        let other_target = tc.get(nodename.clone()).unwrap();
        assert_eq!(other_target, t);
        match tc.get_connected(nodename.clone()) {
            Some(_) => panic!("string lookup should return None"),
            _ => (),
        }
        let now = Instant::now();
        other_target.update_connection_state(|s| {
            assert_eq!(s, TargetConnectionState::Disconnected);
            TargetConnectionState::Mdns(now)
        });
        t.update_connection_state(|s| {
            assert_eq!(s, TargetConnectionState::Disconnected);
            TargetConnectionState::Mdns(now)
        });
        assert_eq!(&tc.get_connected(nodename.clone()).unwrap(), &t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, fake_now());
        tc.merge_insert(t.clone());
        assert_eq!(tc.get(nodename.clone()).unwrap(), t);
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
        assert_ne!(merged_target, t1);
        assert_ne!(merged_target, t2);
        assert_eq!(merged_target.addrs().len(), 2);
        assert_eq!(*merged_target.last_response.borrow(), fake_elapsed());
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
        assert_eq!(merged_target.addrs().iter().filter(|addr| addr.scope_id() == 3).count(), 1);
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
        assert_eq!(*merged_target.last_response.borrow(), fake_elapsed());
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
        let target = Rc::new(Target::new_named("foo"));
        assert!(query.matches(&target));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_by_addr() {
        let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
        let t = Target::new_named("foo");
        t.addrs_insert(addr.clone());
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(clone_target(&t));
        assert_eq!(tc.get(addr).unwrap(), t);
        assert_eq!(tc.get("192.168.0.1").unwrap(), t);
        assert!(tc.get("fe80::dead:beef:beef:beef").is_none());

        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        let t = Target::new_named("fooberdoober");
        t.addrs_insert(addr.clone());
        tc.merge_insert(clone_target(&t));
        assert_eq!(tc.get("fe80::dead:beef:beef:beef").unwrap(), t);
        assert_eq!(tc.get(addr.clone()).unwrap(), t);
        assert_eq!(tc.get("fooberdoober").unwrap(), t);
    }

    // Most of this is now handled in `task.rs`
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_disconnect_multiple_invocations() {
        let t = Rc::new(Target::new_named("flabbadoobiedoo"));
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
            let t = Target::new_named("schlabbadoo");
            let a2 = IpAddr::V6(Ipv6Addr::new(
                0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
            ));
            t.addrs_insert((a2, 2).into());
            if test.loop_started {
                t.run_host_pipe();
            }
            {
                *t.state.borrow_mut() = if test.rcs_is_some {
                    TargetConnectionState::Rcs(RcsConnection::new_with_proxy(
                        setup_fake_remote_control_service(true, "foobiedoo".to_owned()),
                        &NodeId { id: 123 },
                    ))
                } else {
                    TargetConnectionState::Disconnected
                };
            }
            assert_eq!(t.rcs_state(), test.expected);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_into_bridge_target() {
        let t = Target::new_named("cragdune-the-impaler");
        let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
        let a2 = IpAddr::V6(Ipv6Addr::new(
            0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
        ));
        *t.build_config.borrow_mut() = Some(BuildConfig {
            board_config: DEFAULT_BOARD_CONFIG.to_owned(),
            product_config: DEFAULT_PRODUCT_CONFIG.to_owned(),
        });
        t.addrs_insert((a1, 1).into());
        t.addrs_insert((a2, 1).into());

        let t_conv: bridge::Target = t.as_ref().into();
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
    async fn test_new_target_event_synthesis() {
        let t = Target::new_named("clopperdoop");
        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t.clone());
        let vec = tc.synthesize_events().await;
        assert_eq!(vec.len(), 0);
        t.update_connection_state(|s| {
            assert_eq!(s, TargetConnectionState::Disconnected);
            TargetConnectionState::Mdns(Instant::now())
        });
        let vec = tc.synthesize_events().await;
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
        let t = Target::new_named("clam-chowder-is-tasty");
        let t2 = Target::new_named("this-is-a-crunchy-falafel");
        let t3 = Target::new_named("i-should-probably-eat-lunch");
        let t4 = Target::new_named("i-should-probably-eat-lunch");

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
        async fn on_event(&self, event: DaemonEvent) -> Result<events::Status> {
            if let DaemonEvent::NewTarget(TargetInfo { nodename: Some(s), .. }) = event {
                self.got.send(s).await.unwrap();
                Ok(events::Status::Waiting)
            } else {
                panic!("this should never receive any other kind of event");
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_events() {
        let t = Target::new_named("clam-chowder-is-tasty");
        let t2 = Target::new_named("this-is-a-crunchy-falafel");
        let t3 = Target::new_named("i-should-probably-eat-lunch");

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
        let t = Target::new_named("balaowihf");
        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(false, "balaowihf".to_owned()),
            &NodeId { id: 1234 },
        );

        let fut = t.events.wait_for(None, |e| e == TargetEvent::RcsActivated);
        t.update_connection_state(|_| TargetConnectionState::Rcs(conn));
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
        let t = Target::new_named("have-you-seen-my-cat");
        let instant = Instant::now();
        let instant_clone = instant.clone();
        t.update_connection_state(move |s| {
            assert_eq!(s, TargetConnectionState::Disconnected);

            TargetConnectionState::Mdns(instant_clone)
        });
        assert_eq!(TargetConnectionState::Mdns(instant), t.get_connection_state());
    }

    #[test]
    fn test_target_connection_state_will_not_drop_rcs_on_mdns_events() {
        let t = Target::new_named("hello-kitty");
        let rcs_state =
            TargetConnectionState::Rcs(RcsConnection::new(&mut NodeId { id: 1234 }).unwrap());
        t.set_state(rcs_state.clone());

        // Attempt to set the state to TargetConnectionState::Mdns, this transition should fail, as in
        // this transition RCS should be retained.
        t.update_connection_state(|_| TargetConnectionState::Mdns(Instant::now()));

        assert_eq!(t.get_connection_state(), rcs_state);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expire_state_mdns() {
        let t = Target::new_named("yo-yo-ma-plays-that-cello-ya-hear");
        let then = Instant::now() - (MDNS_MAX_AGE + Duration::from_secs(1));
        t.update_connection_state(|_| TargetConnectionState::Mdns(then));

        t.expire_state();

        t.events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    TargetConnectionState::Mdns(then),
                    TargetConnectionState::Disconnected,
                )
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expire_state_fastboot() {
        let t = Target::new_named("platypodes-are-venomous");
        let then = Instant::now() - (FASTBOOT_MAX_AGE + Duration::from_secs(1));
        t.update_connection_state(|_| TargetConnectionState::Fastboot(then));

        t.expire_state();

        t.events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    TargetConnectionState::Fastboot(then),
                    TargetConnectionState::Disconnected,
                )
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expire_state_zedboot() {
        let t = Target::new_named("platypodes-are-venomous");
        let then = Instant::now() - (ZEDBOOT_MAX_AGE + Duration::from_secs(1));
        t.update_connection_state(|_| TargetConnectionState::Zedboot(then));

        t.expire_state();

        t.events
            .wait_for(None, move |e| {
                e == TargetEvent::ConnectionStateChanged(
                    TargetConnectionState::Zedboot(then),
                    TargetConnectionState::Disconnected,
                )
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_addresses_order_preserved() {
        let t = Target::new_named("this-is-a-target-i-guess");
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
                TargetAddrEntry::new(
                    TargetAddr::from(e),
                    Utc.ymd(2014 + (i as i32), 10, 31).and_hms(9, 10, 12),
                    TargetAddrType::Ssh,
                )
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
        let t = Target::new_named("hi-hi-hi");
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
                TargetAddrEntry::new(
                    TargetAddr::from(e),
                    Utc.ymd(2014 + (i as i32), 10, 31).and_hms(9, 10, 12),
                    TargetAddrType::Ssh,
                )
            })
            .collect::<Vec<TargetAddrEntry>>();
        for a in addrs_post.iter().cloned() {
            t.addrs_insert_entry(a);
        }
        assert_eq!(t.addrs().into_iter().next().unwrap(), TargetAddr::from(expected));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_merge_no_name() {
        let ip = "f111::3".parse().unwrap();

        // t1 is a target as we would naturally discover it via mdns, or from a
        // user adding it explicitly. That is, the target has a correctly scoped
        // link-local address.
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr::from((ip, 0xbadf00d)));
        let t1 = Target::new_with_addrs(Option::<String>::None, addr_set);

        // t2 is an incoming target that has the same address, but, it is
        // missing scope information, this is essentially what occurs when we
        // ask the target for its addresses.
        let t2 = Target::new_named("this-is-a-crunchy-falafel");
        t2.addrs.borrow_mut().replace(TargetAddrEntry::new(
            TargetAddr::from((ip, 0)),
            Utc::now(),
            TargetAddrType::Ssh,
        ));

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
        assert_eq!(addr, TargetAddr::from((ip, 0xbadf00d)));
        assert_eq!(addr.ip(), ip);
        assert_eq!(addr.scope_id(), 0xbadf00d);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_does_not_merge_different_ports_with_no_name() {
        let ip = "fe80::1".parse().unwrap();

        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr::from((ip, 1)));
        let t1 = Target::new_with_addrs(Option::<String>::None, addr_set.clone());
        t1.set_ssh_port(Some(8022));
        let t2 = Target::new_with_addrs(Option::<String>::None, addr_set.clone());
        t2.set_ssh_port(Some(8023));

        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t1);
        tc.merge_insert(t2);

        let mut targets = tc.targets().into_iter().collect::<Vec<Rc<Target>>>();

        assert_eq!(targets.len(), 2);

        targets.sort_by(|a, b| a.ssh_port().cmp(&b.ssh_port()));
        let mut iter = targets.into_iter();
        let mut found1 = iter.next().expect("must have target one");
        let mut found2 = iter.next().expect("must have target two");

        // Avoid iterator order dependency
        if found1.ssh_port() == Some(8023) {
            std::mem::swap(&mut found1, &mut found2)
        }

        assert_eq!(found1.addrs().into_iter().next().unwrap().ip(), ip);
        assert_eq!(found1.ssh_port(), Some(8022));

        assert_eq!(found2.addrs().into_iter().next().unwrap().ip(), ip);
        assert_eq!(found2.ssh_port(), Some(8023));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_does_not_merge_different_ports() {
        let ip = "fe80::1".parse().unwrap();

        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr::from((ip, 1)));
        let t1 = Target::new_with_addrs(Some("t1"), addr_set.clone());
        t1.set_ssh_port(Some(8022));
        let t2 = Target::new_with_addrs(Some("t2"), addr_set.clone());
        t2.set_ssh_port(Some(8023));

        let tc = TargetCollection::new_with_queue();
        tc.merge_insert(t1);
        tc.merge_insert(t2);

        let mut targets = tc.targets().into_iter().collect::<Vec<Rc<Target>>>();

        assert_eq!(targets.len(), 2);

        targets.sort_by(|a, b| a.ssh_port().cmp(&b.ssh_port()));
        let mut iter = targets.into_iter();
        let found1 = iter.next().expect("must have target one");
        let found2 = iter.next().expect("must have target two");

        assert_eq!(found1.addrs().into_iter().next().unwrap().ip(), ip);
        assert_eq!(found1.ssh_port(), Some(8022));
        assert_eq!(found1.nodename(), Some("t1".to_string()));

        assert_eq!(found2.addrs().into_iter().next().unwrap().ip(), ip);
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
        addr_set.replace(TargetAddr::from((ip1, 0xbadf00d)));
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new_named("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        t2.addrs.borrow_mut().replace(TargetAddr::from((ip2, 0)).into());
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
        addr_set.replace(TargetAddr::from((ip1, 0xbadf00d)));
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new_named("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        t2.addrs.borrow_mut().replace(TargetAddr::from((ip2, 0)).into());
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
        addr_set.replace(TargetAddr::from((ip1, 0xbadf00d)));
        let t1 = Target::new_with_addrs::<String>(None, addr_set);
        let t2 = Target::new_named("this-is-a-crunchy-falafel");
        let tc = TargetCollection::new_with_queue();
        t2.addrs.borrow_mut().replace(TargetAddr::from((ip2, 0)).into());
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

    #[test]
    fn test_collection_removal_disconnects_target() {
        let target = Target::new_named("soggy-falafel");
        target.set_state(TargetConnectionState::Mdns(Instant::now()));
        target.host_pipe.borrow_mut().replace(Task::local(future::pending()));

        let collection = TargetCollection::new();
        collection.merge_insert(target.clone());
        collection.remove_target("soggy-falafel".to_owned());

        assert_eq!(target.get_connection_state(), TargetConnectionState::Disconnected);
        assert!(target.host_pipe.borrow().is_none());
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
        let name = Some("bubba");
        let start = std::time::SystemTime::now();
        use std::iter::FromIterator;

        // An empty set returns nothing.
        let addrs = BTreeSet::<TargetAddrEntry>::new();
        assert_eq!(Target::new_with_addr_entries(name, addrs.into_iter()).ssh_address(), None);

        // Given two addresses, from the exact same time, neither manual, prefer any link-local address.
        let addrs = BTreeSet::from_iter(vec![
            TargetAddrEntry::new(
                ("2000::1".parse().unwrap(), 0).into(),
                start.into(),
                TargetAddrType::Ssh,
            ),
            TargetAddrEntry::new(
                ("fe80::1".parse().unwrap(), 2).into(),
                start.into(),
                TargetAddrType::Ssh,
            ),
        ]);
        assert_eq!(
            Target::new_with_addr_entries(name, addrs.into_iter()).ssh_address(),
            Some("[fe80::1%2]:22".parse().unwrap())
        );

        // Given two addresses, one link local the other not, prefer the link local even if older.
        let addrs = BTreeSet::from_iter(vec![
            TargetAddrEntry::new(
                ("2000::1".parse().unwrap(), 0).into(),
                start.into(),
                TargetAddrType::Ssh,
            ),
            TargetAddrEntry::new(
                ("fe80::1".parse().unwrap(), 2).into(),
                (start - Duration::from_secs(1)).into(),
                TargetAddrType::Ssh,
            ),
        ]);
        assert_eq!(
            Target::new_with_addr_entries(name, addrs.into_iter()).ssh_address(),
            Some("[fe80::1%2]:22".parse().unwrap())
        );

        // Given two addresses, both link-local, pick the one most recent.
        let addrs = BTreeSet::from_iter(vec![
            TargetAddrEntry::new(
                ("fe80::2".parse().unwrap(), 1).into(),
                start.into(),
                TargetAddrType::Ssh,
            ),
            TargetAddrEntry::new(
                ("fe80::1".parse().unwrap(), 2).into(),
                (start - Duration::from_secs(1)).into(),
                TargetAddrType::Ssh,
            ),
        ]);
        assert_eq!(
            Target::new_with_addr_entries(name, addrs.into_iter()).ssh_address(),
            Some("[fe80::2%1]:22".parse().unwrap())
        );

        // Given two addresses, one manual, old and non-local, prefer the manual entry.
        let addrs = BTreeSet::from_iter(vec![
            TargetAddrEntry::new(
                ("fe80::2".parse().unwrap(), 1).into(),
                start.into(),
                TargetAddrType::Ssh,
            ),
            TargetAddrEntry::new(
                ("2000::1".parse().unwrap(), 0).into(),
                (start - Duration::from_secs(1)).into(),
                TargetAddrType::Manual,
            ),
        ]);
        assert_eq!(
            Target::new_with_addr_entries(name, addrs.into_iter()).ssh_address(),
            Some("[2000::1]:22".parse().unwrap())
        );

        // Given two addresses, neither local, neither manual, prefer the most recent.
        let addrs = BTreeSet::from_iter(vec![
            TargetAddrEntry::new(
                ("2000::1".parse().unwrap(), 0).into(),
                start.into(),
                TargetAddrType::Ssh,
            ),
            TargetAddrEntry::new(
                ("2000::2".parse().unwrap(), 0).into(),
                (start + Duration::from_secs(1)).into(),
                TargetAddrType::Ssh,
            ),
        ]);
        assert_eq!(
            Target::new_with_addr_entries(name, addrs.into_iter()).ssh_address(),
            Some("[2000::2]:22".parse().unwrap())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ssh_address_info_no_port_provides_default_port() {
        let target = Target::new_with_addr_entries(
            Some("foo"),
            vec![TargetAddrEntry::new(
                TargetAddr::from(("::1".parse::<IpAddr>().unwrap().into(), 0)),
                Utc::now(),
                TargetAddrType::Ssh,
            )]
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
            vec![TargetAddrEntry::new(
                TargetAddr::from(("::1".parse::<IpAddr>().unwrap().into(), 0)),
                Utc::now(),
                TargetAddrType::Ssh,
            )]
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
        let t2 = Target::new_named("this-is-not-connected");
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
    fn test_netsvc_target_has_no_ssh() {
        use std::iter::FromIterator;
        let target = Target::new_with_netsvc_addrs(
            Some("foo"),
            BTreeSet::from_iter(
                vec!["[fe80::1%1]:0".parse::<SocketAddr>().unwrap().into()].into_iter(),
            ),
        );
        assert_eq!(target.ssh_address(), None);

        let target = Target::new();
        target.addrs_insert_entry(
            TargetAddrEntry::new(
                ("2000::1".parse().unwrap(), 0).into(),
                Utc::now().into(),
                TargetAddrType::Netsvc,
            )
            .into(),
        );
        target.addrs_insert_entry(
            TargetAddrEntry::new(
                ("fe80::1".parse().unwrap(), 0).into(),
                Utc::now().into(),
                TargetAddrType::Ssh,
            )
            .into(),
        );
        assert_eq!(target.ssh_address(), Some("[fe80::1%0]:22".parse::<SocketAddr>().unwrap()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_netsvc_ssh_address_info_should_be_none() {
        let ip = "f111::4".parse().unwrap();
        let mut addr_set = BTreeSet::new();
        addr_set.replace(TargetAddr::from((ip, 0xbadf00d)));
        let target = Target::new_with_netsvc_addrs(Some("foo"), addr_set);

        assert!(target.ssh_address_info().is_none());
    }

    #[test]
    fn test_target_is_manual() {
        let target = Target::new();
        target.addrs_insert_entry(TargetAddrEntry::new(
            ("::1".parse().unwrap(), 0).into(),
            Utc::now(),
            TargetAddrType::Manual,
        ));
        assert!(target.is_manual());

        let target = Target::new();
        assert!(!target.is_manual());
    }

    #[test]
    fn test_update_connection_state_manual_disconnect() {
        let target = Target::new();
        target.addrs_insert_entry(TargetAddrEntry::new(
            ("::1".parse().unwrap(), 0).into(),
            Utc::now(),
            TargetAddrType::Manual,
        ));
        target.set_state(TargetConnectionState::Manual);

        // Attempting to transition a manual target into the disconnected state remains in manual.
        target.update_connection_state(|_| TargetConnectionState::Disconnected);
        assert_eq!(target.get_connection_state(), TargetConnectionState::Manual);

        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(false, "abc".to_owned()),
            &NodeId { id: 1234 },
        );
        // A manual target can enter the RCS state.
        target.update_connection_state(|_| TargetConnectionState::Rcs(conn));
        assert_matches!(target.get_connection_state(), TargetConnectionState::Rcs(_));

        // A manual target exiting the RCS state to disconnected returns to manual instead.
        target.update_connection_state(|_| TargetConnectionState::Disconnected);
        assert_eq!(target.get_connection_state(), TargetConnectionState::Manual);
    }

    #[test]
    fn test_target_disconnect() {
        let target = Target::new();
        target.set_state(TargetConnectionState::Mdns(Instant::now()));
        target.host_pipe.borrow_mut().replace(Task::local(future::pending()));

        target.disconnect();

        assert_eq!(TargetConnectionState::Disconnected, target.get_connection_state());
        assert!(target.host_pipe.borrow().is_none());
    }
}

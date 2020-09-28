// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::{self, DaemonEvent, EventSynthesizer},
    crate::net::IsLocalAddr,
    crate::onet::HostPipeConnection,
    crate::target_task::*,
    crate::task::{SingleFlight, TaskSnapshot},
    anyhow::{anyhow, Context, Error, Result},
    async_std::sync::RwLock,
    async_trait::async_trait,
    chrono::{DateTime, Utc},
    ffx_fastboot::open_interface_with_serial,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_remotecontrol::{
        IdentifyHostError, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet},
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::future,
    futures::lock::Mutex,
    futures::prelude::*,
    std::collections::{HashMap, HashSet},
    std::default::Default,
    std::fmt,
    std::fmt::{Debug, Display},
    std::hash::Hash,
    std::net::{IpAddr, SocketAddr},
    std::sync::Arc,
    usb_bulk::Interface,
};

#[async_trait]
pub trait TargetAddrFetcher: Send + Sync {
    async fn target_addrs(&self) -> HashSet<TargetAddr>;
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

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum TargetEvent {
    RcsActivated,
}

#[derive(Debug)]
pub enum RcsConnectionError {
    /// There is something wrong with the FIDL connection.
    FidlConnectionError(fidl::Error),
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
        let svc = hoist::connect_as_service_consumer()?;
        svc.connect_to_service(overnet_id, RemoteControlMarker::NAME, channel)
            .map_err(|e| anyhow!("Error connecting to Rcs: {}", e))
    }

    // For testing.
    #[cfg(test)]
    pub fn new_with_proxy(proxy: RemoteControlProxy, id: &NodeId) -> Self {
        Self { proxy, overnet_id: id.clone() }
    }
}

#[derive(Debug)]
pub struct TargetState {
    pub rcs: Option<RcsConnection>,
}

impl TargetState {
    pub fn new() -> Self {
        Self { rcs: None }
    }
}

struct TargetInner {
    nodename: String,
    state: Mutex<TargetState>,
    last_response: RwLock<DateTime<Utc>>,
    addrs: RwLock<HashSet<TargetAddr>>,
    // used for Fastboot
    serial: RwLock<Option<String>>,
}

impl Default for TargetInner {
    fn default() -> Self {
        Self {
            nodename: Default::default(),
            last_response: RwLock::new(Utc::now()),
            state: Mutex::new(TargetState::new()),
            addrs: RwLock::new(HashSet::new()),
            serial: RwLock::new(None),
        }
    }
}

impl TargetInner {
    fn new(nodename: &str) -> Self {
        Self { nodename: nodename.to_string(), ..Default::default() }
    }

    pub fn new_with_addrs(nodename: &str, addrs: HashSet<TargetAddr>) -> Self {
        Self { nodename: nodename.to_string(), addrs: RwLock::new(addrs), ..Default::default() }
    }

    pub fn new_with_serial(nodename: &str, serial: &str) -> Self {
        Self {
            nodename: nodename.to_string(),
            serial: RwLock::new(Some(serial.to_string())),
            ..Default::default()
        }
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time(nodename: &str, time: DateTime<Utc>) -> Self {
        Self {
            nodename: nodename.to_string(),
            last_response: RwLock::new(time),
            ..Default::default()
        }
    }
}

#[async_trait]
impl TargetAddrFetcher for TargetInner {
    async fn target_addrs(&self) -> HashSet<TargetAddr> {
        self.addrs.read().await.clone()
    }
}

#[async_trait]
impl EventSynthesizer<TargetEvent> for TargetInner {
    async fn synthesize_events(&self) -> Vec<TargetEvent> {
        match self.state.lock().await.rcs {
            Some(_) => vec![TargetEvent::RcsActivated],
            None => vec![],
        }
    }
}

#[derive(Clone)]
pub struct Target {
    pub events: events::Queue<TargetEvent>,

    // TODO(awdavies): This shouldn't need to be behind an Arc<>, but for some
    // reason (probably something to do with the merge_insert function in the
    // TargetCollection struct?) this will drop all tasks immediately if this
    // isn't an Arc<>.
    task_manager: Arc<SingleFlight<TargetTaskType, Result<(), String>>>,
    inner: Arc<TargetInner>,
}

impl Target {
    fn from_inner(inner: Arc<TargetInner>) -> Self {
        let events = events::Queue::new(&inner);
        let weak_inner = Arc::downgrade(&inner);
        let task_manager = Arc::new(SingleFlight::new(move |t| match t {
            TargetTaskType::HostPipe => HostPipeConnection::new(weak_inner.clone()).boxed(),
        }));
        Self { inner, events, task_manager }
    }

    pub fn new(nodename: &str) -> Self {
        let inner = Arc::new(TargetInner::new(nodename));
        Self::from_inner(inner)
    }

    pub fn new_with_addrs(nodename: &str, addrs: HashSet<TargetAddr>) -> Self {
        let inner = Arc::new(TargetInner::new_with_addrs(nodename, addrs));
        Self::from_inner(inner)
    }

    pub fn new_with_serial(nodename: &str, serial: &str) -> Self {
        let inner = Arc::new(TargetInner::new_with_serial(nodename, serial));
        Self::from_inner(inner)
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time(nodename: &str, time: DateTime<Utc>) -> Self {
        let inner = Arc::new(TargetInner::new_with_time(nodename, time));
        Self::from_inner(inner)
    }

    async fn rcs_state(&self) -> bridge::RemoteControlState {
        let loop_running = self.task_manager.task_snapshot(TargetTaskType::HostPipe).await
            == TaskSnapshot::Running;
        let state = self.inner.state.lock().await;
        match (loop_running, state.rcs.as_ref()) {
            (true, Some(_)) => bridge::RemoteControlState::Up,
            (true, None) => bridge::RemoteControlState::Down,
            (_, _) => bridge::RemoteControlState::Unknown,
        }
    }

    pub fn nodename(&self) -> String {
        self.inner.nodename.clone()
    }

    pub async fn rcs(&self) -> Option<RcsConnection> {
        match self.inner.state.lock().await.rcs.as_ref() {
            Some(r) => Some(r.clone()),
            None => None,
        }
    }

    pub async fn usb(&self) -> Option<Interface> {
        match self.inner.serial.read().await.as_ref() {
            Some(s) => open_interface_with_serial(s).ok(),
            None => None,
        }
    }

    pub async fn last_response(&self) -> DateTime<Utc> {
        self.inner.last_response.read().await.clone()
    }

    pub async fn addrs(&self) -> HashSet<TargetAddr> {
        self.inner.addrs.read().await.clone()
    }

    #[cfg(test)]
    async fn addrs_insert(&self, t: TargetAddr) {
        self.inner.addrs.write().await.insert(t);
    }

    async fn addrs_extend<T>(&self, iter: T)
    where
        T: IntoIterator<Item = TargetAddr>,
    {
        self.inner.addrs.write().await.extend(iter);
    }

    async fn update_last_response(&self, other: DateTime<Utc>) {
        let mut last_response = self.inner.last_response.write().await;
        if *last_response < other {
            *last_response = other;
        }
    }

    async fn update_state(&self, mut other: TargetState) {
        let mut state = self.inner.state.lock().await;
        if let Some(rcs) = other.rcs.take() {
            let rcs_activated = state.rcs.is_none();
            state.rcs = Some(rcs);
            if rcs_activated {
                self.events.push(TargetEvent::RcsActivated).await.unwrap_or_else(|err| {
                    log::warn!("unable to enqueue RCS activation event: {:#}", err)
                });
            }
        }
    }

    pub async fn from_rcs_connection(r: RcsConnection) -> Result<Self, RcsConnectionError> {
        let fidl_target = match r.proxy.identify_host().await {
            Ok(res) => match res {
                Ok(target) => target,
                Err(e) => return Err(RcsConnectionError::RemoteControlError(e)),
            },
            Err(e) => return Err(RcsConnectionError::FidlConnectionError(e)),
        };
        let nodename = fidl_target
            .nodename
            .ok_or(RcsConnectionError::TargetError(anyhow!("nodename required")))?;

        // TODO(awdavies): Merge target addresses once the scope_id is picked
        // up properly, else there will be duplicate link-local addresses that
        // aren't usable.
        let target = Target::new(nodename.as_ref());
        // Forces drop of target state mutex so that target can be returned.
        {
            let mut target_state = target.inner.state.lock().await;
            target_state.rcs = Some(r);
        }

        Ok(target)
    }

    pub async fn run_host_pipe(&self) {
        // This is a) intended to run forever, and b) implemented using
        // a task (which polls itself), so just drop the future here.
        let _ = self.task_manager.spawn(TargetTaskType::HostPipe).await;
    }
}

#[async_trait]
impl EventSynthesizer<DaemonEvent> for Target {
    async fn synthesize_events(&self) -> Vec<DaemonEvent> {
        vec![DaemonEvent::NewTarget(self.nodename())]
    }
}

#[async_trait]
impl ToFidlTarget for Target {
    async fn to_fidl_target(self) -> bridge::Target {
        let (mut addrs, last_response, rcs_state) =
            futures::join!(self.addrs(), self.last_response(), self.rcs_state());

        bridge::Target {
            nodename: Some(self.nodename()),
            addresses: Some(addrs.drain().map(|a| a.into()).collect()),
            age_ms: Some(
                match Utc::now().signed_duration_since(last_response).num_milliseconds() {
                    dur if dur < 0 => {
                        log::trace!(
                            "negative duration encountered on target '{}': {}",
                            self.inner.nodename,
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
        }
    }
}

impl From<events::TargetInfo> for Target {
    fn from(mut t: events::TargetInfo) -> Self {
        if let Some(s) = t.serial {
            Self::new_with_serial(t.nodename.as_ref(), &s)
        } else {
            Self::new_with_addrs(t.nodename.as_ref(), t.addresses.drain(..).collect())
        }
    }
}

impl Debug for Target {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Target {{ {:?} }}", self.inner.nodename)
    }
}

// TODO(fxbug.dev/52733): Have `TargetAddr` support serial numbers.
#[derive(Hash, Clone, Debug, Copy, Eq, PartialEq)]
pub struct TargetAddr {
    ip: IpAddr,
    scope_id: u32,
}

impl Into<bridge::TargetAddrInfo> for TargetAddr {
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

impl From<bridge::TargetAddrInfo> for TargetAddr {
    fn from(t: bridge::TargetAddrInfo) -> Self {
        let (addr, scope): (IpAddr, u32) = match t {
            bridge::TargetAddrInfo::Ip(ip) => match ip.ip {
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
            write!(f, "%{}", self.scope_id())?;
        }

        Ok(())
    }
}

pub enum TargetQuery {
    Nodename(String),
    Addr(TargetAddr),
    OvernetId(u64),
}

impl TargetQuery {
    pub async fn matches(&self, t: &Target) -> bool {
        match self {
            Self::Nodename(nodename) => *nodename == t.inner.nodename,
            Self::Addr(addr) => {
                let addrs = t.addrs().await;
                // Try to do the lookup if either the scope_id is non-zero (and
                // the IP address is IPv6 OR if the address is just IPv4 (as the
                // scope_id is always zero in this case).
                if addr.scope_id != 0 || addr.ip.is_ipv4() {
                    return addrs.contains(addr);
                }

                // Currently there's no way to parse an IP string w/ a scope_id,
                // so if we're at this stage the address is IPv6 and has been
                // probably parsed with a string (or it's a global IPv6 addr).
                for target_addr in addrs.iter() {
                    if target_addr.ip == addr.ip {
                        return true;
                    }
                }

                false
            }
            Self::OvernetId(id) => match &t.inner.state.lock().await.rcs {
                Some(rcs) => rcs.overnet_id.id == *id,
                None => false,
            },
        }
    }
}

impl From<&str> for TargetQuery {
    fn from(s: &str) -> Self {
        String::from(s).into()
    }
}

impl From<String> for TargetQuery {
    fn from(s: String) -> Self {
        match s.parse::<IpAddr>() {
            Ok(a) => Self::Addr((a, 0).into()),
            Err(_) => Self::Nodename(s),
        }
    }
}

impl From<TargetAddr> for TargetQuery {
    fn from(t: TargetAddr) -> Self {
        Self::Addr(t)
    }
}

impl From<u64> for TargetQuery {
    fn from(id: u64) -> Self {
        Self::OvernetId(id)
    }
}

pub struct TargetCollection {
    inner: RwLock<HashMap<String, Target>>,
    events: RwLock<Option<events::Queue<DaemonEvent>>>,
}

#[async_trait]
impl EventSynthesizer<DaemonEvent> for TargetCollection {
    async fn synthesize_events(&self) -> Vec<DaemonEvent> {
        let collection = self.inner.read().await;
        // TODO(awdavies): This won't be accurate once a target is able to create
        // more than one event at a time.
        let mut res = Vec::with_capacity(collection.len());
        for target in collection.values() {
            res.extend(target.synthesize_events().await);
        }
        res
    }
}

impl TargetCollection {
    pub fn new() -> Self {
        Self { inner: RwLock::new(HashMap::new()), events: RwLock::new(None) }
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

    pub async fn inner_lock(
        &self,
    ) -> async_std::sync::RwLockWriteGuard<'_, HashMap<String, Target>> {
        self.inner.write().await
    }

    pub async fn targets(&self) -> Vec<Target> {
        self.inner.read().await.values().map(|t| t.clone()).collect()
    }

    pub async fn merge_insert(&self, t: Target) -> Target {
        let mut inner = self.inner.write().await;
        // TODO(awdavies): better merging (using more indices for matching).
        match inner.get(&t.inner.nodename) {
            Some(to_update) => {
                futures::join!(
                    to_update.update_last_response(t.last_response().await),
                    to_update.addrs_extend(t.addrs().await),
                    to_update.update_state(TargetState { rcs: t.rcs().await }),
                );
                to_update.clone()
            }
            None => {
                let t: Target = t.into();
                inner.insert(t.nodename(), t.clone());
                if let Some(e) = self.events.read().await.as_ref() {
                    e.push(DaemonEvent::NewTarget(t.nodename()))
                        .await
                        .unwrap_or_else(|e| log::warn!("unable to push new target event: {}", e));
                }

                t
            }
        }
    }

    pub async fn get(&self, t: TargetQuery) -> Option<Target> {
        for target in self.inner.read().await.values() {
            if t.matches(target).await {
                return Some(target.clone());
            }
        }

        None
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

    impl PartialEq for TargetState {
        /// This is a very loose eq function for now, might need to be updated
        /// later, but this shouldn't be used outside of tests. Compares that
        /// another option is not None.
        fn eq(&self, other: &Self) -> bool {
            match self.rcs {
                Some(_) => match other.rcs {
                    Some(_) => true,
                    _ => false,
                },
                None => match other.rcs {
                    None => true,
                    _ => false,
                },
            }
        }
    }

    impl Clone for TargetInner {
        fn clone(&self) -> Self {
            Self {
                nodename: self.nodename.clone(),
                last_response: RwLock::new(block_on(self.last_response.read()).clone()),
                state: Mutex::new(block_on(self.state.lock()).clone()),
                addrs: RwLock::new(block_on(self.addrs.read()).clone()),
                serial: RwLock::new(block_on(self.serial.read()).clone()),
            }
        }
    }

    impl Clone for TargetState {
        fn clone(&self) -> Self {
            Self { rcs: self.rcs.clone() }
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
                && *block_on(self.inner.last_response.read())
                    == *block_on(o.inner.last_response.read())
                && block_on(self.addrs()) == block_on(o.addrs())
                && *block_on(self.inner.state.lock()) == *block_on(o.inner.state.lock())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new() {
        let tc = TargetCollection::new();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, fake_now());
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(&tc.get(nodename.clone().into()).await.unwrap(), &t);
        match tc.get("oihaoih".into()).await {
            Some(_) => panic!("string lookup should return Nobne"),
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
        let merged_target = tc.get(nodename.clone().into()).await.unwrap();
        assert_ne!(&merged_target, &t1);
        assert_ne!(&merged_target, &t2);
        assert_eq!(merged_target.addrs().await.len(), 2);
        assert_eq!(*merged_target.inner.last_response.read().await, fake_elapsed());
        assert!(merged_target.addrs().await.contains(&(a1, 1).into()));
        assert!(merged_target.addrs().await.contains(&(a2, 1).into()));
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
                assert_eq!(t.nodename(), "foo".to_string());
                assert_eq!(t.rcs().await.unwrap().overnet_id.id, 1234u64);
                // For now there shouldn't be any addresses put in here, as
                // there's not a consistent way to convert them yet.
                assert_eq!(t.addrs().await.len(), 0);
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
    async fn test_target_by_overnet_id() {
        const ID: u64 = 12345;
        let conn = RcsConnection::new_with_proxy(
            setup_fake_remote_control_service(false, "foo".to_owned()),
            &NodeId { id: ID },
        );
        let t = Target::from_rcs_connection(conn).await.unwrap();
        let tc = TargetCollection::new();
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get(ID.into()).await.unwrap(), t);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_by_addr() {
        let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
        let t = Target::new("foo");
        t.addrs_insert(addr.clone()).await;
        let tc = TargetCollection::new();
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get(addr.into()).await.unwrap(), t);
        assert_eq!(tc.get("192.168.0.1".into()).await.unwrap(), t);
        assert!(tc.get("fe80::dead:beef:beef:beef".into()).await.is_none());

        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        let t = Target::new("fooberdoober");
        t.addrs_insert(addr.clone()).await;
        tc.merge_insert(clone_target(&t).await).await;
        assert_eq!(tc.get("fe80::dead:beef:beef:beef".into()).await.unwrap(), t);
        assert_eq!(tc.get(addr.clone().into()).await.unwrap(), t);
        assert_eq!(tc.get("fooberdoober".into()).await.unwrap(), t);
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
            futures::join!(t.run_host_pipe(), t.run_host_pipe(), t.run_host_pipe(),);
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
                    rcs: if test.rcs_is_some {
                        Some(RcsConnection::new_with_proxy(
                            setup_fake_remote_control_service(true, "foobiedoo".to_owned()),
                            &NodeId { id: 123 },
                        ))
                    } else {
                        None
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
        assert_eq!(t.nodename(), t_conv.nodename.unwrap().to_string());
        let addrs = t.addrs().await;
        let conv_addrs = t_conv.addresses.unwrap();
        assert_eq!(addrs.len(), conv_addrs.len());

        // Will crash if any addresses are missing.
        for address in conv_addrs {
            let _ = addrs.get(&TargetAddr::from(address)).unwrap();
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_event_synthesis() {
        let t = Target::new("clopperdoop");
        let vec = t.synthesize_events().await;
        assert_eq!(vec.len(), 1);
        assert_eq!(&vec[0], &DaemonEvent::NewTarget("clopperdoop".to_string()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_event_synthesis() {
        let t = Target::new("clam-chowder-is-tasty");
        let t2 = Target::new("this-is-a-crunchy-falafel");
        let t3 = Target::new("i-should-probably-eat-lunch");

        let tc = TargetCollection::new();
        tc.merge_insert(t).await;
        tc.merge_insert(t2).await;
        tc.merge_insert(t3).await;

        let events = tc.synthesize_events().await;
        assert_eq!(events.len(), 3);
        assert!(events
            .iter()
            .any(|e| e == &events::DaemonEvent::NewTarget("clam-chowder-is-tasty".to_string())));
        assert!(
            events
                .iter()
                .any(|e| e
                    == &events::DaemonEvent::NewTarget("this-is-a-crunchy-falafel".to_string()))
        );
        assert!(events.iter().any(
            |e| e == &events::DaemonEvent::NewTarget("i-should-probably-eat-lunch".to_string())
        ));
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
            if let DaemonEvent::NewTarget(s) = event {
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
                assert_eq!(t.nodename(), "foo".to_string());
                assert_eq!(t.rcs().await.unwrap().overnet_id.id, 1234u64);
                // For now there shouldn't be any addresses put in here, as
                // there's not a consistent way to convert them yet.
                assert_eq!(t.addrs().await.len(), 0);
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
        let mut new_state = TargetState::new();
        new_state.rcs = Some(conn);
        // This is a bit of a race, so it's possible that state will be
        // updated before the wait_for invocation is registered with the
        // event queue, but either way this should succeed.
        let (res, ()) = futures::join!(fut, t.update_state(new_state));
        res.unwrap();
    }
}

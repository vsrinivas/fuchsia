// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::net::IsLinkLocal,
    crate::onet,
    anyhow::{anyhow, Context, Error},
    async_std::sync::RwLock,
    async_trait::async_trait,
    chrono::{DateTime, Utc},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_remotecontrol::{
        IdentifyHostError, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet},
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::lock::{Mutex, MutexGuard},
    std::collections::{HashMap, HashSet},
    std::fmt,
    std::fmt::{Debug, Display},
    std::net::{IpAddr, SocketAddr},
    std::sync::atomic::{AtomicBool, Ordering},
    std::sync::Arc,
    std::time::{Duration, Instant},
};

#[async_trait]
pub trait ToFidlTarget {
    async fn to_fidl_target(self) -> bridge::Target;
}

#[derive(Debug, Clone)]
pub struct RCSConnection {
    pub proxy: RemoteControlProxy,
    pub overnet_id: NodeId,
}

#[derive(Debug)]
pub enum RCSConnectionError {
    /// There is something wrong with the FIDL connection.
    FidlConnectionError(fidl::Error),
    /// There is an error from within RCS itself.
    RemoteControlError(IdentifyHostError),

    /// There is an error with the output from RCS.
    TargetError(Error),
}

impl Display for RCSConnectionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RCSConnectionError::FidlConnectionError(ferr) => {
                write!(f, "fidl connection error: {}", ferr)
            }
            RCSConnectionError::RemoteControlError(ierr) => write!(f, "internal error: {:?}", ierr),
            RCSConnectionError::TargetError(error) => write!(f, "general error: {}", error),
        }
    }
}

impl RCSConnection {
    pub async fn new(id: &mut NodeId) -> Result<Self, Error> {
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        let _result = RCSConnection::connect_to_service(id, s)?;
        let proxy = RemoteControlProxy::new(
            fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?,
        );

        Ok(Self { proxy, overnet_id: id.clone() })
    }

    pub fn copy_to_channel(&mut self, channel: fidl::Channel) -> Result<(), Error> {
        RCSConnection::connect_to_service(&mut self.overnet_id, channel)
    }

    fn connect_to_service(overnet_id: &mut NodeId, channel: fidl::Channel) -> Result<(), Error> {
        let svc = hoist::connect_as_service_consumer()?;
        svc.connect_to_service(overnet_id, RemoteControlMarker::NAME, channel)
            .map_err(|e| anyhow!("Error connecting to RCS: {}", e))
    }

    // For testing.
    #[cfg(test)]
    pub fn new_with_proxy(proxy: RemoteControlProxy, id: &NodeId) -> Self {
        Self { proxy, overnet_id: id.clone() }
    }
}

#[derive(Debug)]
pub struct TargetState {
    pub rcs: Option<RCSConnection>,
}

impl TargetState {
    pub fn new() -> Self {
        Self { rcs: None }
    }
}

pub struct Target {
    // Nodename of the target (immutable).
    pub nodename: String,
    pub state: Mutex<TargetState>,
    last_response: RwLock<DateTime<Utc>>,
    addrs: RwLock<HashSet<TargetAddr>>,
    host_pipe: Mutex<Option<onet::HostPipeConnection>>,
    host_pipe_loop_started: AtomicBool,
}

impl Target {
    pub fn new(nodename: &str) -> Self {
        Self {
            nodename: nodename.to_string(),
            last_response: RwLock::new(Utc::now()),
            state: Mutex::new(TargetState::new()),
            addrs: RwLock::new(HashSet::new()),
            host_pipe: Mutex::new(None),
            host_pipe_loop_started: false.into(),
        }
    }

    pub fn new_with_addrs(nodename: &str, addrs: HashSet<TargetAddr>) -> Self {
        Self {
            nodename: nodename.to_string(),
            last_response: RwLock::new(Utc::now()),
            state: Mutex::new(TargetState::new()),
            addrs: RwLock::new(addrs),
            host_pipe: Mutex::new(None),
            host_pipe_loop_started: false.into(),
        }
    }

    /// Dependency injection constructor so we can insert a fake time for
    /// testing.
    #[cfg(test)]
    pub fn new_with_time(nodename: &str, time: DateTime<Utc>) -> Self {
        Self {
            nodename: nodename.to_string(),
            last_response: RwLock::new(time),
            state: Mutex::new(TargetState::new()),
            addrs: RwLock::new(HashSet::new()),
            host_pipe: Mutex::new(None),
            host_pipe_loop_started: false.into(),
        }
    }

    async fn rcs_state(&self) -> bridge::RemoteControlState {
        let loop_started = self.host_pipe_loop_started.load(Ordering::Relaxed);
        let state = self.state.lock().await;
        match (loop_started, state.rcs.as_ref()) {
            (true, Some(_)) => bridge::RemoteControlState::Up,
            (true, None) => bridge::RemoteControlState::Down,
            (_, _) => bridge::RemoteControlState::Unknown,
        }
    }

    /// Attempts to disconnect any active connections to the target.
    pub async fn disconnect(&self) -> Result<(), Error> {
        if let Some(child) = self.host_pipe.lock().await.as_mut() {
            child.stop()?;
        }
        Ok(())
    }

    pub async fn last_response(&self) -> DateTime<Utc> {
        self.last_response.read().await.clone()
    }

    pub async fn addrs(&self) -> HashSet<TargetAddr> {
        self.addrs.read().await.clone()
    }

    #[cfg(test)]
    async fn addrs_insert(&self, t: TargetAddr) {
        self.addrs.write().await.insert(t);
    }

    async fn addrs_extend<T>(&self, iter: T)
    where
        T: IntoIterator<Item = TargetAddr>,
    {
        self.addrs.write().await.extend(iter);
    }

    async fn update_last_response(&self, other: DateTime<Utc>) {
        let mut last_response = self.last_response.write().await;
        if *last_response < other {
            *last_response = other;
        }
    }

    async fn update_state(&self, mut other: TargetState) {
        let mut state = self.state.lock().await;
        if let Some(rcs) = other.rcs.take() {
            state.rcs = Some(rcs);
        }
    }

    pub async fn from_rcs_connection(r: RCSConnection) -> Result<Self, RCSConnectionError> {
        let fidl_target = match r.proxy.identify_host().await {
            Ok(res) => match res {
                Ok(target) => target,
                Err(e) => return Err(RCSConnectionError::RemoteControlError(e)),
            },
            Err(e) => return Err(RCSConnectionError::FidlConnectionError(e)),
        };
        let nodename = fidl_target
            .nodename
            .ok_or(RCSConnectionError::TargetError(anyhow!("nodename required")))?;
        // TODO(awdavies): Merge target addresses once the scope_id is picked
        // up properly, else there will be duplicate link-local addresses that
        // aren't usable.
        let target = Target::new(nodename.as_ref());
        // Forces drop of target state mutex so that target can be returned.
        {
            let mut target_state = target.state.lock().await;
            target_state.rcs = Some(r);
        }
        Ok(target)
    }

    pub async fn wait_for_state_with_rcs(
        &self,
        retries: u32,
        retry_delay: Duration,
    ) -> Result<MutexGuard<'_, TargetState>, Error> {
        // TODO(awdavies): It would make more sense to have something
        // like std::sync::CondVar here, but there is no implementation
        // in futures or async_std yet.
        for _ in 0..retries {
            let state_guard = self.state.lock().await;
            match &state_guard.rcs {
                Some(_) => return Ok(state_guard),
                None => {
                    std::mem::drop(state_guard);
                    overnet_core::wait_until(Instant::now() + retry_delay).await;
                }
            }
        }

        Err(anyhow!("Waiting for RCS timed out"))
    }

    pub async fn run_host_pipe(self: Arc<Target>) {
        Target::run_host_pipe_with_allocator(self, onet::HostPipeConnection::new).await
    }

    async fn run_host_pipe_with_allocator(
        target: Arc<Target>,
        host_pipe_allocator: impl Fn(Arc<Target>) -> Result<onet::HostPipeConnection, Error> + 'static,
    ) {
        match target.host_pipe_loop_started.compare_exchange(
            false,
            true,
            Ordering::Acquire,
            Ordering::Relaxed,
        ) {
            Ok(false) => (),
            _ => return,
        };
        let mut pipe = target.host_pipe.lock().await;
        log::info!("Initiating overnet host-pipe");
        // Hack alert: since there's no error returned in this function, this unwraps
        // the error here and sets that host_pipe_loop_started is false. In order to
        // accomplish this, since the `?` operator isn't supported in a function that
        // returns `()` the "error" is returned as a Future that is awaited (as it
        // returns `()` anyway).
        //
        // At the time of implementation this can only happen if there is
        // an error spawning a thread, so this is pretty unlikely (but it is tested).
        *pipe = match host_pipe_allocator(target.clone()).map_err(|e| {
                log::warn!(
                    "Error starting host pipe connection, setting host_pipe_loop_started to false: {:?}", e
                );
                target.host_pipe_loop_started.store(false, Ordering::Relaxed);

                ()
            }) {
                Ok(p) => Some(p),
                Err(()) => return,
            };
    }
}

#[async_trait]
impl ToFidlTarget for Arc<Target> {
    async fn to_fidl_target(self) -> bridge::Target {
        let (mut addrs, last_response, rcs_state) =
            futures::join!(self.addrs(), self.last_response(), self.rcs_state());

        bridge::Target {
            nodename: Some(self.nodename.clone()),
            addresses: Some(addrs.drain().map(|a| a.into()).collect()),
            age_ms: Some(
                match Utc::now().signed_duration_since(last_response).num_milliseconds() {
                    dur if dur < 0 => {
                        log::trace!(
                            "negative duration encountered on target '{}': {}",
                            self.nodename,
                            dur
                        );
                        0
                    }
                    dur => dur,
                } as u64,
            ),
            rcs_state: Some(rcs_state),

            // TODO(awdavies): Gather more information here when possilbe.
            target_type: Some(bridge::TargetType::Unknown),
            target_state: Some(bridge::TargetState::Unknown),
        }
    }
}

impl From<crate::events::TargetInfo> for Target {
    fn from(mut t: crate::events::TargetInfo) -> Self {
        Self::new_with_addrs(t.nodename.as_ref(), t.addresses.drain(..).collect())
    }
}

impl Debug for Target {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Target {{ {:?} }}", self.nodename)
    }
}

// TODO(fxb/52733): Have `TargetAddr` support serial numbers.
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
            // TODO(fxb/52733): Add serial numbers.,
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
        match self.ip {
            IpAddr::V6(addr) => {
                if addr.is_ll() && self.scope_id() > 0 {
                    write!(f, "%{}", self.scope_id())?;
                }
            }
            _ => (),
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
    pub async fn matches(&self, t: &Arc<Target>) -> bool {
        match self {
            Self::Nodename(nodename) => *nodename == t.nodename,
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
            Self::OvernetId(id) => match &t.state.lock().await.rcs {
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

#[derive(Debug)]
pub struct TargetCollection {
    inner: RwLock<HashMap<String, Arc<Target>>>,
}

impl TargetCollection {
    pub fn new() -> Self {
        Self { inner: RwLock::new(HashMap::new()) }
    }

    pub async fn inner_lock(
        &self,
    ) -> async_std::sync::RwLockWriteGuard<'_, HashMap<String, Arc<Target>>> {
        self.inner.write().await
    }

    pub async fn targets(&self) -> Vec<Arc<Target>> {
        self.inner.read().await.values().map(|t| t.clone()).collect()
    }

    pub async fn merge_insert(&self, t: Target) -> Arc<Target> {
        let mut inner = self.inner.write().await;
        // TODO(awdavies): better merging (using more indices for matching).
        match inner.get(&t.nodename) {
            Some(to_update) => {
                futures::join!(
                    to_update.update_last_response(t.last_response().await),
                    to_update.addrs_extend(t.addrs().await),
                    to_update.update_state(t.state.into_inner()),
                );

                to_update.clone()
            }
            None => {
                let t = Arc::new(t);
                inner.insert(t.nodename.clone(), t.clone());
                t
            }
        }
    }

    pub async fn get(&self, t: TargetQuery) -> Option<Arc<Target>> {
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
    use super::*;
    use std::net::{Ipv4Addr, Ipv6Addr};

    use chrono::offset::TimeZone;
    use fidl;
    use fidl_fuchsia_developer_remotecontrol as rcs;
    use futures::executor::block_on;
    use futures::prelude::*;

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

    impl Clone for Target {
        fn clone(&self) -> Self {
            Self {
                nodename: self.nodename.clone(),
                last_response: RwLock::new(block_on(self.last_response.read()).clone()),
                state: Mutex::new(block_on(self.state.lock()).clone()),
                addrs: RwLock::new(block_on(self.addrs()).clone()),
                host_pipe: Mutex::new(None),
                host_pipe_loop_started: self.host_pipe_loop_started.load(Ordering::SeqCst).into(),
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
            self.nodename == o.nodename
                && *block_on(self.last_response.read()) == *block_on(o.last_response.read())
                && block_on(self.addrs()) == block_on(o.addrs())
                && *block_on(self.state.lock()) == *block_on(o.state.lock())
        }
    }

    #[test]
    fn test_target_collection_insert_new() {
        hoist::run(async move {
            let tc = TargetCollection::new();
            let nodename = String::from("what");
            let t = Target::new_with_time(&nodename, fake_now());
            tc.merge_insert(t.clone()).await;
            assert_eq!(&*tc.get(nodename.clone().into()).await.unwrap(), &t.clone());
            match tc.get("oihaoih".into()).await {
                Some(_) => panic!("string lookup should return Nobne"),
                _ => (),
            }
        });
    }

    #[test]
    fn test_target_collection_merge() {
        hoist::run(async move {
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
            tc.merge_insert(t2.clone()).await;
            tc.merge_insert(t1.clone()).await;
            let merged_target = tc.get(nodename.clone().into()).await.unwrap();
            assert_ne!(&*merged_target, &t1);
            assert_ne!(&*merged_target, &t2);
            assert_eq!(merged_target.addrs().await.len(), 2);
            assert_eq!(*merged_target.last_response.read().await, fake_elapsed());
            assert!(merged_target.addrs().await.contains(&(a1, 1).into()));
            assert!(merged_target.addrs().await.contains(&(a2, 1).into()));
        });
    }

    fn setup_fake_remote_control_service(
        send_internal_error: bool,
        nodename_response: String,
    ) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        hoist::spawn(async move {
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
        });

        proxy
    }

    #[test]
    fn test_target_from_rcs_connection_internal_err() {
        // TODO(awdavies): Do some form of PartialEq implementation for
        // the RCSConnectionError enum to avoid the nested matches.
        hoist::run(async move {
            let conn = RCSConnection::new_with_proxy(
                setup_fake_remote_control_service(true, "foo".to_owned()),
                &NodeId { id: 123 },
            );
            match Target::from_rcs_connection(conn).await {
                Ok(_) => assert!(false),
                Err(e) => match e {
                    RCSConnectionError::RemoteControlError(rce) => match rce {
                        rcs::IdentifyHostError::ListInterfacesFailed => (),
                        _ => assert!(false),
                    },
                    _ => assert!(false),
                },
            }
        });
    }

    #[test]
    fn test_target_from_rcs_connection_nodename_none() {
        hoist::run(async move {
            let conn = RCSConnection::new_with_proxy(
                setup_fake_remote_control_service(false, "".to_owned()),
                &NodeId { id: 123456 },
            );
            match Target::from_rcs_connection(conn).await {
                Ok(_) => assert!(false),
                Err(e) => match e {
                    RCSConnectionError::TargetError(_) => (),
                    _ => assert!(false),
                },
            }
        });
    }

    #[test]
    fn test_target_from_rcs_connection_no_err() {
        hoist::run(async move {
            let conn = RCSConnection::new_with_proxy(
                setup_fake_remote_control_service(false, "foo".to_owned()),
                &NodeId { id: 1234 },
            );
            match Target::from_rcs_connection(conn).await {
                Ok(t) => {
                    assert_eq!(t.nodename, "foo");
                    assert_eq!(t.state.lock().await.rcs.as_ref().unwrap().overnet_id.id, 1234u64);
                    // For now there shouldn't be any addresses put in here, as
                    // there's not a consistent way to convert them yet.
                    assert_eq!(t.addrs().await.len(), 0);
                }
                Err(_) => assert!(false),
            }
        });
    }

    #[test]
    fn test_target_query_matches_nodename() {
        hoist::run(async move {
            let query = TargetQuery::from("foo");
            let target = Arc::new(Target::new("foo"));
            assert!(query.matches(&target).await);
        });
    }

    #[test]
    fn test_target_by_overnet_id() {
        hoist::run(async move {
            const ID: u64 = 12345;
            let conn = RCSConnection::new_with_proxy(
                setup_fake_remote_control_service(false, "foo".to_owned()),
                &NodeId { id: ID },
            );
            let t = Target::from_rcs_connection(conn).await.unwrap();
            let tc = TargetCollection::new();
            tc.merge_insert(t.clone()).await;
            assert_eq!(*tc.get(ID.into()).await.unwrap(), t);
        });
    }

    #[test]
    fn test_target_by_addr() {
        hoist::run(async move {
            let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
            let t = Target::new("foo");
            t.addrs_insert(addr.clone()).await;
            let tc = TargetCollection::new();
            tc.merge_insert(t.clone()).await;
            assert_eq!(*tc.get(addr.into()).await.unwrap(), t);
            assert_eq!(*tc.get("192.168.0.1".into()).await.unwrap(), t);
            assert!(tc.get("fe80::dead:beef:beef:beef".into()).await.is_none());

            let addr: TargetAddr =
                (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
            let t = Target::new("fooberdoober");
            t.addrs_insert(addr.clone()).await;
            tc.merge_insert(t.clone()).await;
            assert_eq!(*tc.get("fe80::dead:beef:beef:beef".into()).await.unwrap(), t);
            assert_eq!(*tc.get(addr.clone().into()).await.unwrap(), t);
            assert_eq!(*tc.get("fooberdoober".into()).await.unwrap(), t);
        });
    }

    #[test]
    fn test_wait_for_rcs() {
        hoist::run(async move {
            let t = Arc::new(Target::new("foo"));
            assert!(t.wait_for_state_with_rcs(1, Duration::from_millis(1)).await.is_err());
            assert!(t.wait_for_state_with_rcs(10, Duration::from_millis(1)).await.is_err());
            let t_clone = t.clone();
            hoist::spawn(async move {
                let mut state = t_clone.state.lock().await;
                let conn = RCSConnection::new_with_proxy(
                    setup_fake_remote_control_service(false, "foo".to_owned()),
                    &NodeId { id: 5u64 },
                );
                t_clone.host_pipe_loop_started.store(true, Ordering::Relaxed);
                state.rcs = Some(conn);
            });
            // Adds a few hundred thousands as this is a race test.
            t.wait_for_state_with_rcs(500000, Duration::from_millis(1)).await.unwrap();
        });
    }

    #[test]
    fn test_target_disconnect_no_process() {
        hoist::run(async move {
            let t = Target::new("fooberdoober");
            t.disconnect().await.unwrap();
        });
    }

    #[test]
    fn test_target_disconnect_multiple_invocations() {
        hoist::run(async move {
            let t = Arc::new(Target::new("flabbadoobiedoo"));
            {
                let addr: TargetAddr = (IpAddr::from([192, 168, 0, 1]), 0).into();
                t.addrs_insert(addr).await;
            }
            // Assures multiple "simultaneous" invocations to start the target
            // doesn't put it into a bad state that would hang.
            let _: ((), (), ()) = futures::join!(
                Target::run_host_pipe_with_allocator(t.clone(), onet::HostPipeConnection::new),
                Target::run_host_pipe_with_allocator(t.clone(), onet::HostPipeConnection::new),
                Target::run_host_pipe_with_allocator(t.clone(), onet::HostPipeConnection::new),
            );
            t.disconnect().await.unwrap();
        });
    }

    #[test]
    fn test_target_run_host_pipe_failure_sets_loop_false() {
        hoist::run(async move {
            let t = Arc::new(Target::new("bonanza"));
            Target::run_host_pipe_with_allocator(t.clone(), |_| {
                Result::<onet::HostPipeConnection, Error>::Err(anyhow!(
                    "testing error to check state"
                ))
            })
            .await;
            assert!(!t.host_pipe_loop_started.load(Ordering::SeqCst));
        });
    }

    #[test]
    fn test_target_run_host_pipe_skips_if_started() {
        hoist::run(async move {
            let t = Arc::new(Target::new("bloop"));
            t.host_pipe_loop_started.store(true, Ordering::Relaxed);
            // The error returned in this function should be skipped leaving
            // the loop state set to true.
            Target::run_host_pipe_with_allocator(t.clone(), |_| {
                Result::<onet::HostPipeConnection, Error>::Err(anyhow!(
                    "testing error to check state"
                ))
            })
            .await;
            assert!(t.host_pipe_loop_started.load(Ordering::SeqCst));
        });
    }

    struct RcsStateTest {
        loop_started: bool,
        rcs_is_some: bool,
        expected: bridge::RemoteControlState,
    }

    #[test]
    fn test_target_rcs_states() {
        hoist::run(async move {
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
                t.host_pipe_loop_started.store(test.loop_started, Ordering::Relaxed);
                *t.state.lock().await = TargetState {
                    rcs: if test.rcs_is_some {
                        Some(RCSConnection::new_with_proxy(
                            setup_fake_remote_control_service(true, "foobiedoo".to_owned()),
                            &NodeId { id: 123 },
                        ))
                    } else {
                        None
                    },
                };
                assert_eq!(t.rcs_state().await, test.expected);
            }
        });
    }

    #[test]
    fn test_target_to_fidl_target() {
        hoist::run(async move {
            let t = Arc::new(Target::new("cragdune-the-impaler"));
            let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
            let a2 = IpAddr::V6(Ipv6Addr::new(
                0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
            ));
            t.addrs_insert((a1, 1).into()).await;
            t.addrs_insert((a2, 1).into()).await;

            let t_conv = t.clone().to_fidl_target().await;
            assert_eq!(t.nodename, t_conv.nodename.unwrap().to_string());
            let addrs = t.addrs().await;
            let conv_addrs = t_conv.addresses.unwrap();
            assert_eq!(addrs.len(), conv_addrs.len());

            // Will crash if any addresses are missing.
            for address in conv_addrs {
                let _ = addrs.get(&TargetAddr::from(address)).unwrap();
            }
        });
    }
}

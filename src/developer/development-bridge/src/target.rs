// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::net::IsLinkLocal,
    anyhow::{anyhow, Context, Error},
    async_std::sync::RwLock,
    async_std::task,
    chrono::{DateTime, Utc},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_remotecontrol::{
        IdentifyHostError, InterfaceAddress, IpAddress, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::lock::{Mutex, MutexGuard},
    std::collections::{HashMap, HashSet},
    std::fmt,
    std::fmt::{Debug, Display},
    std::net::{IpAddr, SocketAddr},
    std::process::Child,
    std::sync::Arc,
    std::time::Duration,
};

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
    pub overnet_started: bool,
    // Note that Child is not Send, so it needs its own Mutex. We expose
    // the mutex here so that operations on the Option can be atomic (e.g. 'replace if None').
    pub host_pipe: Mutex<Option<Child>>,
}

impl TargetState {
    pub fn new() -> Self {
        Self { rcs: None, overnet_started: false, host_pipe: Mutex::new(None) }
    }
}

pub struct Target {
    // Nodename of the target (immutable).
    pub nodename: String,
    pub last_response: Mutex<DateTime<Utc>>,
    pub state: Mutex<TargetState>,
    pub addrs: Mutex<HashSet<TargetAddr>>,
}

impl Target {
    pub fn new(nodename: &str, t: DateTime<Utc>) -> Self {
        Self {
            nodename: nodename.to_string(),
            last_response: Mutex::new(t),
            state: Mutex::new(TargetState::new()),
            addrs: Mutex::new(HashSet::new()),
        }
    }

    // TODO(fxb/50708) remove this once we've resolved the possible deadlocks that result
    // without it.
    pub async fn clone_addrs(&self) -> HashSet<TargetAddr> {
        let addrs = self.addrs.lock().await;
        return addrs.clone();
    }

    pub async fn to_string_async(&self) -> String {
        // Need to hold onto the state for the duration of the format
        // function to ensure that it doesn't change abruptly.
        let state = self.state.lock().await;
        format!(
            "{} [{}] [overnet_started: {}] [overnet_peer_id: {}] {}",
            self.nodename,
            self.addrs
                .lock()
                .await
                .iter()
                .map(|addr| addr.to_string())
                .collect::<Vec<_>>()
                .join(", "),
            state.overnet_started,
            match state.rcs.as_ref() {
                Some(s) => s.overnet_id.id.to_string(),
                None => String::from("not connected"),
            },
            self.last_response.lock().await.to_rfc2822(),
        )
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
        let target = Target::new(nodename.as_ref(), Utc::now());
        // Forces drop of target state mutex so that target can be returned.
        {
            let mut target_state = target.state.lock().await;
            // If we're here, then overnet must have been started.
            target_state.overnet_started = true;
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
                    task::sleep(retry_delay).await;
                }
            }
        }

        Err(anyhow!("Waiting for RCS timed out"))
    }
}

impl Debug for Target {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Target {{ {:?} }}", self.nodename)
    }
}

#[derive(Hash, Clone, Debug, Copy, Eq, PartialEq)]
pub struct TargetAddr {
    ip: IpAddr,
    scope_id: u32,
}

impl From<InterfaceAddress> for TargetAddr {
    fn from(i: InterfaceAddress) -> Self {
        // TODO(awdavies): Figure out if it's possible to get the scope_id from
        // this address.
        match i.ip_address {
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
                let addrs = t.addrs.lock().await;
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
                let mut addrs = to_update.addrs.lock().await;
                let mut last_response = to_update.last_response.lock().await;
                addrs.extend(&*t.addrs.lock().await);
                // Ignore out-of-order packets.
                if *last_response < *t.last_response.lock().await {
                    *last_response = *t.last_response.lock().await;
                }

                // TODO(awdavies): Create a merge function just for state.
                let mut state = to_update.state.lock().await;
                if state.rcs.is_none() {
                    state.rcs = t.state.lock().await.rcs.clone();
                }

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

pub trait TryIntoTarget: Sized {
    type Error;

    /// Attempts, given a source socket address, to determine whether the
    /// received message was from a Fuchsia target, and if so, what kind. Attempts
    /// to fill in as much information as possible given the message, consuming
    /// the underlying object in the process.
    fn try_into_target(self, src: SocketAddr) -> Result<Target, Self::Error>;
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
                last_response: Mutex::new(block_on(self.last_response.lock()).clone()),
                state: Mutex::new(block_on(self.state.lock()).clone()),
                addrs: Mutex::new(block_on(self.addrs.lock()).clone()),
            }
        }
    }

    impl Clone for TargetState {
        fn clone(&self) -> Self {
            Self {
                rcs: self.rcs.clone(),
                overnet_started: self.overnet_started,
                // host_pipe is not used in tests.
                host_pipe: Mutex::new(None),
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
            self.nodename == o.nodename
                && *block_on(self.last_response.lock()) == *block_on(o.last_response.lock())
                && *block_on(self.addrs.lock()) == *block_on(o.addrs.lock())
                && *block_on(self.state.lock()) == *block_on(o.state.lock())
        }
    }

    #[test]
    fn test_target_collection_insert_new() {
        hoist::run(async move {
            let tc = TargetCollection::new();
            let nodename = String::from("what");
            let t = Target::new(&nodename, fake_now());
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
            let t1 = Target::new(&nodename, fake_now());
            let t2 = Target::new(&nodename, fake_elapsed());
            let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
            let a2 = IpAddr::V6(Ipv6Addr::new(
                0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
            ));
            t1.addrs.lock().await.insert((a1.clone(), 1).into());
            t2.addrs.lock().await.insert((a2.clone(), 1).into());
            tc.merge_insert(t2.clone()).await;
            tc.merge_insert(t1.clone()).await;
            let merged_target = tc.get(nodename.clone().into()).await.unwrap();
            assert_ne!(&*merged_target, &t1);
            assert_ne!(&*merged_target, &t2);
            assert_eq!(merged_target.addrs.lock().await.len(), 2);
            assert_eq!(*merged_target.last_response.lock().await, fake_elapsed());
            assert!(merged_target.addrs.lock().await.contains(&(a1, 1).into()));
            assert!(merged_target.addrs.lock().await.contains(&(a2, 1).into()));
        });
    }

    fn setup_fake_remote_control_service(
        send_internal_error: bool,
        nodename_response: String,
    ) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(rcs::RemoteControlRequest::IdentifyHost { responder }) => {
                        if send_internal_error {
                            let _ = responder
                                .send(&mut Err(rcs::IdentifyHostError::ListInterfacesFailed))
                                .context("sending testing error response")
                                .unwrap();
                        } else {
                            let result: Vec<rcs::InterfaceAddress> = vec![rcs::InterfaceAddress {
                                ip_address: rcs::IpAddress::Ipv4(rcs::Ipv4Address {
                                    addr: [192, 168, 0, 1],
                                }),
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
                    assert_eq!(t.addrs.lock().await.len(), 0);
                }
                Err(_) => assert!(false),
            }
        });
    }

    #[test]
    fn test_target_query_matches_nodename() {
        hoist::run(async move {
            let query = TargetQuery::from("foo");
            let target = Arc::new(Target::new("foo", Utc::now()));
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
            let t = Target::new("foo", Utc::now());
            t.addrs.lock().await.insert(addr.clone());
            let tc = TargetCollection::new();
            tc.merge_insert(t.clone()).await;
            assert_eq!(*tc.get(addr.into()).await.unwrap(), t);
            assert_eq!(*tc.get("192.168.0.1".into()).await.unwrap(), t);
            assert!(tc.get("fe80::dead:beef:beef:beef".into()).await.is_none());

            let addr: TargetAddr =
                (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
            let t = Target::new("fooberdoober", Utc::now());
            t.addrs.lock().await.insert(addr.clone());
            tc.merge_insert(t.clone()).await;
            assert_eq!(*tc.get("fe80::dead:beef:beef:beef".into()).await.unwrap(), t);
            assert_eq!(*tc.get(addr.clone().into()).await.unwrap(), t);
            assert_eq!(*tc.get("fooberdoober".into()).await.unwrap(), t);
        });
    }

    #[test]
    fn test_target_debug_string() {
        hoist::run(async move {
            let t = Target::new("foo", fake_now());
            assert_eq!(t.to_string_async().await, "foo [] [overnet_started: false] [overnet_peer_id: not connected] Fri, 31 Oct 2014 09:10:12 +0000");
            t.addrs.lock().await.insert((IpAddr::from([192, 168, 1, 1]), 0).into());
            t.state.lock().await.rcs = Some(RCSConnection::new_with_proxy(
                setup_fake_remote_control_service(false, "foo".to_owned()),
                &NodeId { id: 2u64 },
            ));
            assert_eq!(t.to_string_async().await, "foo [192.168.1.1] [overnet_started: false] [overnet_peer_id: 2] Fri, 31 Oct 2014 09:10:12 +0000");
        });
    }

    #[test]
    fn test_wait_for_rcs() {
        hoist::run(async move {
            let t = Arc::new(Target::new("foo", Utc::now()));
            assert!(t.wait_for_state_with_rcs(0, Duration::from_millis(1)).await.is_err());
            assert!(t.wait_for_state_with_rcs(1, Duration::from_millis(1)).await.is_err());
            assert!(t.wait_for_state_with_rcs(10, Duration::from_millis(1)).await.is_err());
            let t_clone = t.clone();
            hoist::spawn(async move {
                let mut state = t_clone.state.lock().await;
                let conn = RCSConnection::new_with_proxy(
                    setup_fake_remote_control_service(false, "foo".to_owned()),
                    &NodeId { id: 5u64 },
                );
                state.overnet_started = true;
                state.rcs = Some(conn);
            });
            // Adds a few hundred thousands as this is a race test.
            assert!(t.wait_for_state_with_rcs(500000, Duration::from_millis(1)).await.is_ok());
        });
    }
}

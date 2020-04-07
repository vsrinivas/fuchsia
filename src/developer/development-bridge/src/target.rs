// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::collections::hash_map::Values;
use std::collections::{HashMap, HashSet};
use std::fmt;
use std::fmt::{Debug, Display};
use std::net::{IpAddr, SocketAddr};
use std::sync::Arc;

use anyhow::{anyhow, Context, Error};
use chrono::{DateTime, Utc};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_developer_remotecontrol::{
    IdentifyHostError, InterfaceAddress, IpAddress, RemoteControlMarker, RemoteControlProxy,
};
use fidl_fuchsia_overnet::ServiceConsumerProxyInterface;
use fidl_fuchsia_overnet_protocol::NodeId;
use futures::lock::Mutex;

use crate::net::IsLinkLocal;

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
        let svc = hoist::connect_as_service_consumer()?;
        svc.connect_to_service(id, RemoteControlMarker::NAME, s)?;
        let proxy = RemoteControlProxy::new(
            fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?,
        );

        Ok(Self { proxy, overnet_id: id.clone() })
    }

    pub fn new_with_proxy(proxy: RemoteControlProxy, id: &NodeId) -> Self {
        Self { proxy, overnet_id: id.clone() }
    }
}

#[derive(Debug, Clone)]
pub struct TargetState {
    pub rcs: Option<RCSConnection>,
    pub overnet_started: bool,
}

impl TargetState {
    pub fn new() -> Self {
        Self { rcs: None, overnet_started: false }
    }
}

pub struct Target {
    // Nodename of the target (immutable).
    pub nodename: String,
    pub last_response: Arc<Mutex<DateTime<Utc>>>,
    pub state: Arc<Mutex<TargetState>>,
    pub addrs: Arc<Mutex<HashSet<TargetAddr>>>,
}

impl Target {
    pub fn new(nodename: &str, t: DateTime<Utc>) -> Self {
        Self {
            nodename: nodename.to_string(),
            last_response: Arc::new(Mutex::new(t)),
            state: Arc::new(Mutex::new(TargetState::new())),
            addrs: Arc::new(Mutex::new(HashSet::new())),
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

#[derive(Debug)]
pub struct TargetCollection {
    map: HashMap<String, Arc<Target>>,
}

impl TargetCollection {
    pub fn new() -> Self {
        Self { map: HashMap::new() }
    }

    pub fn len(&self) -> usize {
        self.map.len()
    }

    // This is implemented as this instead of `impl Iterator` as most
    // of the borrow-checker heavy lifting is already taken care of by
    // using the `Values` struct.
    pub fn iter(&self) -> Values<'_, String, Arc<Target>> {
        self.map.values()
    }

    pub async fn merge_insert(&mut self, t: Target) {
        // TODO(awdavies): better merging (using more indices for matching).
        match self.map.get(&t.nodename) {
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
            }
            None => {
                self.map.insert(t.nodename.clone(), Arc::new(t));
            }
        }
    }

    pub fn target_by_nodename(&self, n: &String) -> Option<&Arc<Target>> {
        self.map.get(n)
    }

    pub async fn target_by_overnet_id(&self, node_id: &NodeId) -> Option<&Arc<Target>> {
        for (_, target) in self.map.iter() {
            match &target.state.lock().await.rcs {
                Some(rcs) => {
                    if rcs.overnet_id == *node_id {
                        return Some(target);
                    }
                }
                _ => (),
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
                last_response: Arc::new(Mutex::new(block_on(self.last_response.lock()).clone())),
                state: Arc::new(Mutex::new(block_on(self.state.lock()).clone())),
                addrs: Arc::new(Mutex::new(block_on(self.addrs.lock()).clone())),
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
            let mut tc = TargetCollection::new();
            let nodename = String::from("what");
            let t = Target::new(&nodename, fake_now());
            tc.merge_insert(t.clone()).await;
            assert_eq!(&**tc.target_by_nodename(&nodename).unwrap(), &t.clone());
            match tc.target_by_nodename(&"oihaoih".to_string()) {
                Some(_) => panic!("string lookup should return Nobne"),
                _ => (),
            }
        });
    }

    #[test]
    fn test_target_collection_merge() {
        hoist::run(async move {
            let mut tc = TargetCollection::new();
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
            let merged_target: &Arc<Target> = tc.target_by_nodename(&nodename).unwrap();
            assert_ne!(&**merged_target, &t1);
            assert_ne!(&**merged_target, &t2);
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
    fn test_target_by_overnet_id() {
        hoist::run(async move {
            const ID: u64 = 1234;
            let conn = RCSConnection::new_with_proxy(
                setup_fake_remote_control_service(false, "foo".to_owned()),
                &NodeId { id: ID },
            );
            let t = Target::from_rcs_connection(conn).await.unwrap();
            let mut tc = TargetCollection::new();
            tc.merge_insert(t.clone()).await;
            assert_eq!(**tc.target_by_overnet_id(&NodeId { id: ID }).await.unwrap(), t);
        });
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::collections::{HashMap, HashSet};
use std::fmt::Display;
use std::net::{IpAddr, SocketAddr};
use std::sync::Arc;

use chrono::{DateTime, Utc};
use futures::lock::Mutex;

use crate::net::IsLinkLocal;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TargetState {
    Unknown,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Target {
    pub nodename: String,
    pub last_response: DateTime<Utc>,
    pub state: TargetState,
    pub addrs: HashSet<TargetAddr>,
}

#[derive(Hash, Clone, Debug, Copy, Eq, PartialEq)]
pub struct TargetAddr {
    ip: IpAddr,
    scope_id: u32,
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
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
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
    map: HashMap<String, Arc<Mutex<Target>>>,
}

impl TargetCollection {
    pub fn new() -> Self {
        Self { map: HashMap::new() }
    }

    pub async fn merge_insert(&mut self, t: Target) {
        // TODO(awdavies): better merging (using more indices for matching).
        match self.map.get(&t.nodename) {
            Some(to_update) => {
                let mut to_update = to_update.lock().await;
                to_update.addrs.extend(&t.addrs);
                // Ignore out-of-order packets.
                if to_update.last_response < t.last_response {
                    to_update.last_response = t.last_response;
                }
            }
            None => {
                self.map.insert(t.nodename.clone(), Arc::new(Mutex::new(t)));
            }
        }
    }

    pub fn target_by_nodename(&self, n: &String) -> Option<&Arc<Mutex<Target>>> {
        self.map.get(n)
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

    impl Target {
        fn new(nodename: &str, t: DateTime<Utc>) -> Self {
            Self {
                nodename: nodename.to_string(),
                last_response: t,
                state: TargetState::Unknown,
                addrs: HashSet::new(),
            }
        }
    }

    fn fake_now() -> DateTime<Utc> {
        Utc.ymd(2014, 10, 31).and_hms(9, 10, 12)
    }

    fn fake_elapsed() -> DateTime<Utc> {
        Utc.ymd(2014, 11, 2).and_hms(13, 2, 1)
    }

    #[test]
    fn test_target_collection_insert_new() {
        hoist::run(async move {
            let mut tc = TargetCollection::new();
            let nodename = String::from("what");
            let t = Target::new(&nodename, fake_now());
            tc.merge_insert(t.clone()).await;
            assert_eq!(*tc.target_by_nodename(&nodename).unwrap().lock().await, t);
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
            let mut t1 = Target::new(&nodename, fake_now());
            let mut t2 = Target::new(&nodename, fake_elapsed());
            let a1 = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
            let a2 = IpAddr::V6(Ipv6Addr::new(
                0xfe80, 0xcafe, 0xf00d, 0xf000, 0xb412, 0xb455, 0x1337, 0xfeed,
            ));
            t1.addrs.insert((a1.clone(), 1).into());
            t2.addrs.insert((a2.clone(), 1).into());
            tc.merge_insert(t2.clone()).await;
            tc.merge_insert(t1.clone()).await;
            let merged_target = tc.target_by_nodename(&nodename).unwrap().lock().await;
            assert_ne!(*merged_target, t1);
            assert_ne!(*merged_target, t2);
            assert_eq!(merged_target.addrs.len(), 2);
            assert_eq!(merged_target.last_response, fake_elapsed());
            assert!(merged_target.addrs.contains(&(a1, 1).into()));
            assert!(merged_target.addrs.contains(&(a2, 1).into()));
        });
    }
}

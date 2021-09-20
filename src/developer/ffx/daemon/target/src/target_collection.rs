// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::target::{Target, TargetAddrEntry},
    addr::TargetAddr,
    anyhow::Result,
    async_trait::async_trait,
    bridge::DaemonError,
    ffx_daemon_core::events::{self, EventSynthesizer},
    ffx_daemon_events::{DaemonEvent, TargetEvent, TargetInfo},
    fidl_fuchsia_developer_bridge as bridge,
    std::cell::RefCell,
    std::collections::HashMap,
    std::fmt::Debug,
    std::net::{IpAddr, SocketAddr},
    std::rc::Rc,
};

#[derive(Default)]
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

            to_update.update_connection_state(|_| new_target.get_connection_state());

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

        let s_ip = if s.starts_with('[') && s.ends_with(']') { &s[..s.len() - 1][1..] } else { &s };

        match s_ip.parse::<IpAddr>() {
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::target::clone_target,
        crate::target::TargetAddrType,
        chrono::{TimeZone, Utc},
        ffx_daemon_events::TargetConnectionState,
        fuchsia_async::Task,
        futures::prelude::*,
        std::collections::BTreeSet,
        std::net::{Ipv4Addr, Ipv6Addr},
        std::time::Instant,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_insert_new_not_connected() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("what");
        let t = Target::new_with_time(&nodename, Utc.ymd(2014, 10, 31).and_hms(9, 10, 12));
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
        let t = Target::new_with_time(&nodename, Utc.ymd(2014, 10, 31).and_hms(9, 10, 12));
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
        let t1 = Target::new_with_time(&nodename, Utc.ymd(2014, 10, 31).and_hms(9, 10, 12));
        let t2 = Target::new_with_time(&nodename, Utc.ymd(2014, 11, 2).and_hms(13, 2, 1));
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
        assert_eq!(*merged_target.last_response.borrow(), Utc.ymd(2014, 11, 2).and_hms(13, 2, 1));
        assert!(merged_target.addrs().contains(&(a1, 1).into()));
        assert!(merged_target.addrs().contains(&(a2, 1).into()));

        // Insert another instance of the a2 address, but with a missing
        // scope_id, and ensure that the new address does not affect the address
        // collection.
        let t3 = Target::new_with_time(&nodename, Utc.ymd(2014, 10, 31).and_hms(9, 10, 12));
        t3.addrs_insert((a2.clone(), 0).into());
        tc.merge_insert(clone_target(&t3));
        let merged_target = tc.get(nodename.clone()).unwrap();
        assert_eq!(merged_target.addrs().len(), 2);

        // Insert another instance of the a2 address, but with a new scope_id, and ensure that the new scope is used.
        let t3 = Target::new_with_time(&nodename, Utc.ymd(2014, 10, 31).and_hms(9, 10, 12));
        t3.addrs_insert((a2.clone(), 3).into());
        tc.merge_insert(clone_target(&t3));
        let merged_target = tc.get(nodename.clone()).unwrap();
        assert_eq!(merged_target.addrs().iter().filter(|addr| addr.scope_id() == 3).count(), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_collection_no_scopeless_ipv6() {
        let tc = TargetCollection::new_with_queue();
        let nodename = String::from("bananas");
        let t1 = Target::new_with_time(&nodename, Utc.ymd(2014, 10, 31).and_hms(9, 10, 12));
        let t2 = Target::new_with_time(&nodename, Utc.ymd(2014, 11, 2).and_hms(13, 2, 1));
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
        assert_eq!(*merged_target.last_response.borrow(), Utc.ymd(2014, 11, 2).and_hms(13, 2, 1));
        assert!(merged_target.addrs().contains(&(a1, 0).into()));
        assert!(!merged_target.addrs().contains(&(a2, 0).into()));
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_query_matches_nodename() {
        let query = TargetQuery::from("foo");
        let target = Rc::new(Target::new_named("foo"));
        assert!(query.matches(&target));
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

        let tq = TargetQuery::from("[::1]");
        let ti = TargetInfo {
            addresses: vec![("::1".parse::<IpAddr>().unwrap(), 0).into()],
            ssh_port: None,
            ..Default::default()
        };
        assert!(matches!(tq, TargetQuery::Addr(addr) if addr == ti.addresses[0]));
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
}

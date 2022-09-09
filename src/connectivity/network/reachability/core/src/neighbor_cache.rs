// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_neighbor as fnet_neighbor;
use fidl_fuchsia_net_neighbor_ext as fnet_neighbor_ext;
use fuchsia_zircon as zx;
use std::collections::HashMap;
use tracing::error;

use super::Id;

#[derive(Debug, Clone)]
#[cfg_attr(test, derive(PartialEq, Eq))]
pub(crate) enum NeighborHealth {
    Unknown,
    Stale { last_observed: zx::Time, last_healthy: Option<zx::Time> },
    Healthy { last_observed: zx::Time },
    Unhealthy { last_healthy: Option<zx::Time> },
}

impl NeighborHealth {
    fn last_healthy(&self) -> Option<zx::Time> {
        match self {
            NeighborHealth::Unknown => None,
            NeighborHealth::Healthy { last_observed } => Some(*last_observed),
            NeighborHealth::Unhealthy { last_healthy }
            | NeighborHealth::Stale { last_observed: _, last_healthy } => *last_healthy,
        }
    }

    /// Transitions to a new [`NeighborHealth`] state given a new
    /// [`fnet_neighbor::EntryState`].
    ///
    /// Entry states that do not explicitly encode healthy or unhealthy status
    /// (`Delay`, `Probe`, `Static`) will maintain the state machine in the
    /// current state.
    ///
    /// A `Reachable` entry will always move to a `Healthy` state.
    ///
    /// An `Incomplete` or `Unreachable` entry will always move to an
    /// `Unhealthy` state.
    ///
    /// A `Stale` entry will always move to the `Stale` health state.
    fn transition(&self, now: zx::Time, state: fnet_neighbor::EntryState) -> Self {
        match state {
            fnet_neighbor::EntryState::Incomplete | fnet_neighbor::EntryState::Unreachable => {
                NeighborHealth::Unhealthy { last_healthy: self.last_healthy() }
            }
            fnet_neighbor::EntryState::Reachable => NeighborHealth::Healthy { last_observed: now },
            fnet_neighbor::EntryState::Stale => {
                NeighborHealth::Stale { last_observed: now, last_healthy: self.last_healthy() }
            }
            fnet_neighbor::EntryState::Delay
            | fnet_neighbor::EntryState::Probe
            | fnet_neighbor::EntryState::Static => self.clone(),
        }
    }
}

#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
pub(crate) struct NeighborState {
    health: NeighborHealth,
}

impl NeighborState {
    #[cfg(test)]
    pub(crate) const fn new(health: NeighborHealth) -> Self {
        Self { health }
    }
}

#[derive(Debug, Default)]
#[cfg_attr(test, derive(Eq, PartialEq))]
pub struct InterfaceNeighborCache {
    neighbors: HashMap<fnet::IpAddress, NeighborState>,
}

impl InterfaceNeighborCache {
    pub(crate) fn iter_health(
        &self,
    ) -> impl Iterator<Item = (&'_ fnet::IpAddress, &'_ NeighborHealth)> {
        let Self { neighbors } = self;
        neighbors.iter().map(|(n, NeighborState { health })| (n, health))
    }
}

#[cfg(test)]
impl FromIterator<(fnet::IpAddress, NeighborState)> for InterfaceNeighborCache {
    fn from_iter<T: IntoIterator<Item = (fnet::IpAddress, NeighborState)>>(iter: T) -> Self {
        Self { neighbors: FromIterator::from_iter(iter) }
    }
}

/// Provides a cache of known neighbors and keeps track of their health.
#[derive(Debug, Default)]
pub struct NeighborCache {
    interfaces: HashMap<Id, InterfaceNeighborCache>,
}

impl NeighborCache {
    pub fn process_neighbor_event(&mut self, e: fnet_neighbor::EntryIteratorItem) {
        let Self { interfaces } = self;
        enum Event {
            Added,
            Changed,
            Removed,
        }
        let (event, entry) = match e {
            fnet_neighbor::EntryIteratorItem::Existing(entry)
            | fnet_neighbor::EntryIteratorItem::Added(entry) => (Event::Added, entry),
            fnet_neighbor::EntryIteratorItem::Idle(fnet_neighbor::IdleEvent {}) => {
                return;
            }
            fnet_neighbor::EntryIteratorItem::Changed(entry) => (Event::Changed, entry),
            fnet_neighbor::EntryIteratorItem::Removed(entry) => (Event::Removed, entry),
        };
        let fnet_neighbor_ext::Entry { interface, neighbor, state, mac: _, updated_at } =
            match fnet_neighbor_ext::Entry::try_from(entry) {
                Ok(entry) => entry,
                Err(e) => {
                    error!(e = ?e, "invalid neighbor entry");
                    return;
                }
            };
        let updated_at = zx::Time::from_nanos(updated_at);

        let InterfaceNeighborCache { neighbors } =
            interfaces.entry(interface).or_insert_with(Default::default);

        match event {
            Event::Added => match neighbors.entry(neighbor) {
                std::collections::hash_map::Entry::Occupied(occupied) => {
                    error!(entry = ?occupied, "received entry for already existing neighbor");
                    return;
                }
                std::collections::hash_map::Entry::Vacant(vacant) => {
                    let _: &mut _ = vacant.insert(NeighborState {
                        health: NeighborHealth::Unknown.transition(updated_at, state),
                    });
                }
            },
            Event::Changed => {
                let NeighborState { health } = match neighbors.get_mut(&neighbor) {
                    Some(s) => s,
                    None => {
                        error!(neigh = ?neighbor, "got changed event for unseen neighbor");
                        return;
                    }
                };
                *health = health.transition(updated_at, state);
            }
            Event::Removed => match neighbors.remove(&neighbor) {
                Some(NeighborState { .. }) => {
                    if neighbors.is_empty() {
                        // Clean up interface state when we see all neighbors
                        // removed. Unwrap is valid because `neighbors` is
                        // itself a borrow into the map's entry.
                        InterfaceNeighborCache { .. } = interfaces.remove(&interface).unwrap();
                    }
                }
                None => {
                    error!(neigh = ?neighbor, "got removed event for unseen neighbor");
                }
            },
        }
    }

    pub fn get_interface_neighbors(&self, interface: Id) -> Option<&InterfaceNeighborCache> {
        let Self { interfaces } = self;
        interfaces.get(&interface)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use net_declare::fidl_ip;

    const IFACE1: Id = 1;
    const IFACE2: Id = 2;
    const NEIGH1: fnet::IpAddress = fidl_ip!("192.0.2.1");
    const NEIGH2: fnet::IpAddress = fidl_ip!("2001:db8::1");

    struct EventSource {
        now: zx::Time,
    }

    impl EventSource {
        fn new() -> Self {
            Self { now: zx::Time::from_nanos(0) }
        }

        fn advance_secs(&mut self, secs: u64) {
            self.now += zx::Duration::from_seconds(secs.try_into().unwrap());
        }

        fn entry(
            &self,
            interface: Id,
            neighbor: fnet::IpAddress,
            state: fnet_neighbor::EntryState,
        ) -> NeighborEntry {
            NeighborEntry { interface, neighbor, state, updated_at: self.now }
        }

        fn reachable(&self, interface: Id, neighbor: fnet::IpAddress) -> NeighborEntry {
            self.entry(interface, neighbor, fnet_neighbor::EntryState::Reachable)
        }

        fn probe(&self, interface: Id, neighbor: fnet::IpAddress) -> NeighborEntry {
            self.entry(interface, neighbor, fnet_neighbor::EntryState::Probe)
        }

        fn stale(&self, interface: Id, neighbor: fnet::IpAddress) -> NeighborEntry {
            self.entry(interface, neighbor, fnet_neighbor::EntryState::Stale)
        }

        fn delay(&self, interface: Id, neighbor: fnet::IpAddress) -> NeighborEntry {
            self.entry(interface, neighbor, fnet_neighbor::EntryState::Delay)
        }

        fn incomplete(&self, interface: Id, neighbor: fnet::IpAddress) -> NeighborEntry {
            self.entry(interface, neighbor, fnet_neighbor::EntryState::Incomplete)
        }

        fn unreachable(&self, interface: Id, neighbor: fnet::IpAddress) -> NeighborEntry {
            self.entry(interface, neighbor, fnet_neighbor::EntryState::Unreachable)
        }
    }

    struct NeighborEntry {
        interface: Id,
        neighbor: fnet::IpAddress,
        state: fnet_neighbor::EntryState,
        updated_at: zx::Time,
    }

    impl NeighborEntry {
        fn into_entry(self) -> fnet_neighbor::Entry {
            let Self { interface, neighbor, state, updated_at } = self;
            fnet_neighbor::Entry {
                interface: Some(interface),
                neighbor: Some(neighbor),
                state: Some(state),
                updated_at: Some(updated_at.into_nanos()),
                ..fnet_neighbor::Entry::EMPTY
            }
        }

        fn into_added(self) -> fnet_neighbor::EntryIteratorItem {
            fnet_neighbor::EntryIteratorItem::Added(self.into_entry())
        }

        fn into_changed(self) -> fnet_neighbor::EntryIteratorItem {
            fnet_neighbor::EntryIteratorItem::Changed(self.into_entry())
        }

        fn into_removed(self) -> fnet_neighbor::EntryIteratorItem {
            fnet_neighbor::EntryIteratorItem::Removed(self.into_entry())
        }
    }

    impl NeighborCache {
        fn assert_neighbors(
            &self,
            interface: Id,
            it: impl IntoIterator<Item = (fnet::IpAddress, NeighborHealth)>,
        ) {
            let InterfaceNeighborCache { neighbors } =
                self.get_interface_neighbors(interface).unwrap();
            let it = it
                .into_iter()
                .map(|(n, health)| (n, NeighborState { health }))
                .collect::<HashMap<_, _>>();
            assert_eq!(neighbors, &it);
        }
    }

    #[fuchsia::test]
    fn caches_healthy_neighbors_per_interface() {
        let mut cache = NeighborCache::default();
        let mut events = EventSource::new();
        cache.process_neighbor_event(events.reachable(IFACE1, NEIGH1).into_added());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Healthy { last_observed: events.now })],
        );

        events.advance_secs(1);
        cache.process_neighbor_event(events.reachable(IFACE2, NEIGH2).into_added());
        cache.assert_neighbors(
            IFACE2,
            [(NEIGH2, NeighborHealth::Healthy { last_observed: events.now })],
        );
    }

    #[fuchsia::test]
    fn updates_healthy_state() {
        let mut cache = NeighborCache::default();
        let mut events = EventSource::new();
        cache.process_neighbor_event(events.reachable(IFACE1, NEIGH1).into_added());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Healthy { last_observed: events.now })],
        );

        events.advance_secs(3);
        cache.process_neighbor_event(events.reachable(IFACE1, NEIGH1).into_changed());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Healthy { last_observed: events.now })],
        );
    }

    #[fuchsia::test]
    fn probe_reachable_stale() {
        let mut cache = NeighborCache::default();
        let mut events = EventSource::new();

        cache.process_neighbor_event(events.probe(IFACE1, NEIGH1).into_added());
        cache.assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unknown)]);
        events.advance_secs(1);

        cache.process_neighbor_event(events.reachable(IFACE1, NEIGH1).into_changed());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Healthy { last_observed: events.now })],
        );

        let last_healthy = Some(events.now);
        events.advance_secs(1);
        cache.process_neighbor_event(events.stale(IFACE1, NEIGH1).into_changed());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Stale { last_observed: events.now, last_healthy })],
        );
    }

    #[fuchsia::test]
    fn stale_delay_reachable() {
        let mut cache = NeighborCache::default();
        let mut events = EventSource::new();

        cache.process_neighbor_event(events.stale(IFACE1, NEIGH1).into_added());
        let last_observed = events.now;
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Stale { last_observed, last_healthy: None })],
        );

        events.advance_secs(1);
        cache.process_neighbor_event(events.delay(IFACE1, NEIGH1).into_changed());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Stale { last_observed, last_healthy: None })],
        );

        events.advance_secs(1);
        cache.process_neighbor_event(events.reachable(IFACE1, NEIGH1).into_changed());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Healthy { last_observed: events.now })],
        );
    }

    #[fuchsia::test]
    fn reachable_unreachable() {
        let mut cache = NeighborCache::default();
        let mut events = EventSource::new();

        cache.process_neighbor_event(events.reachable(IFACE1, NEIGH1).into_added());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Healthy { last_observed: events.now })],
        );

        let last_healthy = Some(events.now);
        events.advance_secs(1);
        cache.process_neighbor_event(events.unreachable(IFACE1, NEIGH1).into_changed());
        cache.assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unhealthy { last_healthy })]);
    }

    #[fuchsia::test]
    fn probe_incomplete() {
        let mut cache = NeighborCache::default();
        let mut events = EventSource::new();

        cache.process_neighbor_event(events.probe(IFACE1, NEIGH1).into_added());
        cache.assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unknown)]);

        events.advance_secs(1);
        cache.process_neighbor_event(events.incomplete(IFACE1, NEIGH1).into_changed());
        cache
            .assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unhealthy { last_healthy: None })]);
    }

    #[fuchsia::test]
    fn stale_unreachable_probe_incomplete() {
        let mut cache = NeighborCache::default();
        let mut events = EventSource::new();

        cache.process_neighbor_event(events.stale(IFACE1, NEIGH1).into_added());
        cache.assert_neighbors(
            IFACE1,
            [(NEIGH1, NeighborHealth::Stale { last_observed: events.now, last_healthy: None })],
        );

        events.advance_secs(1);
        cache.process_neighbor_event(events.unreachable(IFACE1, NEIGH1).into_changed());
        cache
            .assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unhealthy { last_healthy: None })]);

        events.advance_secs(1);
        cache.process_neighbor_event(events.probe(IFACE1, NEIGH1).into_changed());
        cache
            .assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unhealthy { last_healthy: None })]);

        events.advance_secs(1);
        cache.process_neighbor_event(events.incomplete(IFACE1, NEIGH1).into_changed());
        cache
            .assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unhealthy { last_healthy: None })]);
    }

    #[fuchsia::test]
    fn removing_last_neighbor_clears_interface_state() {
        let mut cache = NeighborCache::default();
        let events = EventSource::new();

        cache.process_neighbor_event(events.probe(IFACE1, NEIGH1).into_added());
        cache.assert_neighbors(IFACE1, [(NEIGH1, NeighborHealth::Unknown)]);

        cache.process_neighbor_event(events.probe(IFACE1, NEIGH1).into_removed());
        assert_eq!(cache.get_interface_neighbors(IFACE1), None);
    }
}

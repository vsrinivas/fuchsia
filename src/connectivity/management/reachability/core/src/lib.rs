// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod inspect;
mod ping;

#[macro_use]
extern crate log;
pub use ping::{IcmpPinger, Pinger};
use {
    anyhow::Error,
    fidl_fuchsia_net_stack as stack, fidl_fuchsia_netstack as netstack,
    fuchsia_inspect::Inspector,
    fuchsia_zircon::{self as zx},
    inspect::InspectInfo,
    network_manager_core::{address::LifIpAddr, error, hal},
    std::collections::{HashMap, HashSet},
};

const INTERNET_CONNECTIVITY_CHECK_ADDRESS: &str = "8.8.8.8";
const PROBE_PERIOD_IN_SEC: i64 = 60;

/// `Stats` keeps the monitoring service statistic counters.
#[derive(Debug, Default, Clone)]
pub struct Stats {
    /// `events` is the number of events received.
    pub events: u64,
    /// `state_updates` is the number of times reachability state has changed.
    pub state_updates: HashMap<Id, u64>,
}

// TODO(dpradilla): consider splitting the state in l2 state and l3 state, as there can be multiple
// L3 networks on the same physical medium.
/// `State` represents the reachability state.
#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Clone, Copy)]
pub enum State {
    /// State not yet determined.
    None,
    /// Interface no longer present.
    Removed,
    /// Interface is down.
    Down,
    /// Interface is up, no packets seen yet.
    Up,
    /// Interface is up, packets seen.
    LinkLayerUp,
    /// Interface is up, and configured as an L3 interface.
    NetworkLayerUp,
    /// L3 Interface is up, local neighbors seen.
    Local,
    /// L3 Interface is up, local gateway configured and reachable.
    Gateway,
    /// Expected response not seen from reachability test URL.
    WalledGarden,
    /// Expected response seen from reachability test URL.
    Internet,
}

impl Default for State {
    fn default() -> Self {
        State::None
    }
}

#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy)]
enum Proto {
    IPv4,
    IPv6,
}
impl std::fmt::Display for Proto {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Proto::IPv4 => write!(f, "IPv4"),
            Proto::IPv6 => write!(f, "IPv6"),
        }
    }
}

/// `PortType` is the type of port backing the L3 interface.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum PortType {
    /// EthernetII or 802.3.
    Ethernet,
    /// Wireless LAN based on 802.11.
    WiFi,
    /// Switch virtual interface.
    SVI,
    /// Loopback.
    Loopback,
}

impl Default for PortType {
    fn default() -> Self {
        PortType::Ethernet
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
struct Time(zx::Time);
impl Default for Time {
    fn default() -> Self {
        Time(zx::Time::get(zx::ClockId::Monotonic))
    }
}

/// `StateEvent` records a state and the time it was reached.
#[derive(Default, Debug, Clone, Copy)]
struct StateEvent {
    /// `state` is the current reachability state.
    state: State,
    /// `time` of the last `state` update.
    time: Time,
}

impl From<State> for StateEvent {
    fn from(state: State) -> Self {
        StateEvent { state, ..Default::default() }
    }
}

impl PartialEq for StateEvent {
    fn eq(&self, other: &Self) -> bool {
        // Only compare the state, ignoring the time.
        self.state == other.state
    }
}

impl PartialEq<State> for StateEvent {
    fn eq(&self, other: &State) -> bool {
        self.state == *other
    }
}
impl PartialEq<StateEvent> for State {
    fn eq(&self, other: &StateEvent) -> bool {
        *self == other.state
    }
}

/// `NetworkInfo` keeps information about a network.
#[derive(Default, Debug, PartialEq, Clone, Copy)]
struct NetworkInfo {
    /// `is_default` indicates the default route is via this network.
    is_default: bool,
    /// `is_l3` indicates L3 is configured.
    is_l3: bool,
    /// `state` is the current reachability state.
    state: StateEvent,
    /// `previous_state` is the previous reachability state.
    previous_state: StateEvent,
}

/// `ReachabilityInfo` is the information related to an iinterface.
#[derive(Debug, PartialEq, Clone)]
pub struct ReachabilityInfo {
    /// `port_type` is the type of port.
    port_type: PortType,
    /// `name` is the interface name.
    name: String,
    /// per IP version reachability information.
    info: HashMap<Proto, NetworkInfo>,
}

impl ReachabilityInfo {
    fn new(port_type: PortType, name: &str, v4: NetworkInfo, v6: NetworkInfo) -> Self {
        let mut info = HashMap::new();
        info.insert(Proto::IPv4, v4);
        info.insert(Proto::IPv6, v6);
        ReachabilityInfo { port_type, name: name.to_string(), info }
    }
    fn get(&self, proto: Proto) -> &NetworkInfo {
        // it exists by construction.
        self.info.get(&proto).unwrap()
    }
    fn get_mut(&mut self, proto: Proto) -> &mut NetworkInfo {
        // it exists by construction.
        self.info.get_mut(&proto).unwrap()
    }
}

impl Default for ReachabilityInfo {
    fn default() -> Self {
        ReachabilityInfo::new(
            PortType::default(),
            "",
            NetworkInfo::default(),
            NetworkInfo::default(),
        )
    }
}

type Id = hal::PortId;
/// `StateInfo` keeps the reachability state.
#[derive(Debug, Default)]
struct StateInfo(HashMap<Id, ReachabilityInfo>);

impl StateInfo {
    /// gets the reachability info associated with an interface.
    fn get(&self, id: Id) -> Option<&ReachabilityInfo> {
        self.0.get(&id)
    }

    /// Updates the reachability info associated with an interface if it has changed.
    /// Returns a set with the IP version that had reachability state change.
    fn update_on_change(&mut self, id: Id, new: &mut ReachabilityInfo) -> HashSet<Proto> {
        let mut updated = HashSet::new();
        match self.0.get(&id) {
            Some(old) => {
                if fix_state_on_change(&old.get(Proto::IPv4), new.get_mut(Proto::IPv4)) {
                    updated.insert(Proto::IPv4);
                }
                if fix_state_on_change(&old.get(Proto::IPv6), new.get_mut(Proto::IPv6)) {
                    updated.insert(Proto::IPv6);
                }
                if !updated.is_empty() {
                    // Something changed, update.
                    self.0.insert(id, new.clone());
                }
            }
            None => {
                self.0.insert(id, new.clone());
                updated.insert(Proto::IPv4);
                updated.insert(Proto::IPv6);
            }
        };
        updated
    }
}

// If there has been a state change, saves the previous state and returns true.
// If there was no change, restores state time and return false.
fn fix_state_on_change(old: &NetworkInfo, new: &mut NetworkInfo) -> bool {
    if old.state == new.state {
        // Keep the old time as state did not change.
        new.state.time = old.state.time;
        new.previous_state = old.previous_state;
        return false;
    }
    new.previous_state = old.state;
    true
}
/// Trait to set a periodic timer that calls `Monitor::timer_event`
/// every `duration`.
pub trait Timer {
    fn periodic(&self, duration: zx::Duration, id: Option<u64>);
}

/// `Monitor` monitors the reachability state.
pub struct Monitor {
    hal: hal::NetCfg,
    interface_state: StateInfo,
    system_state: HashMap<Proto, (Id, NetworkInfo)>,
    stats: Stats,
    pinger: Box<dyn Pinger>,
    timer: Option<Box<dyn Timer>>,
    inspector: Option<&'static Inspector>,
    system_node: Option<InspectInfo>,
    nodes: HashMap<Id, InspectInfo>,
}

impl Monitor {
    /// Create the monitoring service.
    pub fn new(pinger: Box<dyn Pinger>) -> Result<Self, Error> {
        let hal = hal::NetCfg::new()?;
        Ok(Monitor {
            hal,
            interface_state: Default::default(),
            stats: Default::default(),
            system_state: [
                (Proto::IPv4, (Id::from(0), NetworkInfo::default())),
                (Proto::IPv6, (Id::from(0), NetworkInfo::default())),
            ]
            .iter()
            .cloned()
            .collect(),
            pinger,
            timer: None,
            inspector: None,
            system_node: None,
            nodes: HashMap::new(),
        })
    }

    /// `stats` returns monitoring service statistic counters.
    pub fn stats(&self) -> &Stats {
        &self.stats
    }

    /// Reports all information.
    pub fn report_state(&mut self) {
        debug!("reachability state IPv4 {:?}", self.system_state[&Proto::IPv4]);
        debug!("reachability state IPv6 {:?}", self.system_state[&Proto::IPv4]);
        debug!("reachability stats {:?}", self.stats());
        self.report_state_duration_for_all();
    }

    // Reports current state duration for all interfaces.
    fn report_state_duration_for_all(&mut self) {
        debug!("report_state_duration_for_all");
    }

    fn report_system_updated(&mut self, updated: HashSet<Proto>) {
        for v in updated {
            let state = &self.system_state.get(&v).unwrap().1;
            info!("report_system_updated {:?} {:?}", v, state);
            log_state(self.system_node.as_mut(), v, state.state.state);
        }
    }

    fn report_updated(&mut self, id: Id, updated: HashSet<Proto>) {
        // Get node, if not present create it with proper name.
        let info = match self.interface_state.get(id) {
            None => return,
            Some(info) => info.clone(),
        };
        let name = info.name.clone();
        for v in updated {
            let state = &info.get(v);
            info!("report_updated {:?}: {:?} {:?}", id, v, state);
            log_state(self.interface_node(id, &name), v, state.state.state);
        }
    }

    /// Returns the underlying event streams associated with the open channels to fuchsia.net.stack
    /// and fuchsia.netstack.
    pub fn take_event_streams(
        &mut self,
    ) -> (stack::StackEventStream, netstack::NetstackEventStream) {
        self.hal.take_event_streams()
    }

    /// Sets the timer to use for periodic events.
    pub fn set_timer(&mut self, timer: Box<dyn Timer>) {
        self.timer = Some(timer);

        if let Some(timer) = &self.timer {
            debug!("setting periodic report timer");
            timer.periodic(zx::Duration::from_seconds(PROBE_PERIOD_IN_SEC), None);
        }
    }

    /// Sets the inspector.
    pub fn set_inspector(&mut self, inspector: &'static Inspector) {
        self.inspector = Some(inspector);

        let system_node = InspectInfo::new(inspector.root(), "system", "");
        self.system_node = Some(system_node);
    }

    fn interface_node(&mut self, id: Id, name: &str) -> Option<&mut InspectInfo> {
        match self.inspector {
            Some(inspector) => {
                if !self.nodes.contains_key(&id) {
                    let n = InspectInfo::new(inspector.root(), &format!("{:?}", id), name);
                    self.nodes.insert(id, n);
                }
                self.nodes.get_mut(&id)
            }
            None => None,
        }
    }

    /// `update_state` evaluates reachability on an interface and updates
    /// its state accordingly.
    async fn update_state(&mut self, interface_info: &hal::Interface) -> HashSet<Proto> {
        let port_type = port_type(interface_info);
        if port_type == PortType::Loopback {
            return HashSet::new();
        }

        debug!("update_state {:?}  ->  interface_info: {:?}", interface_info.id, interface_info);
        let routes = self.hal.routes().await;
        let mut new_info = match compute_state(interface_info, routes, &*self.pinger).await {
            Some(new_info) => new_info,
            None => return HashSet::new(),
        };

        if self.interface_state.get(interface_info.id).is_none() {
            // New interface, set timer for periodic updates if it wasn't already set.
            debug!("update_state {:?} ->  new interface {:?}", interface_info.id, new_info);
            if let Some(timer) = &self.timer {
                debug!("update_state {:?}  ->  setting timer", interface_info.id);
                timer.periodic(
                    zx::Duration::from_seconds(PROBE_PERIOD_IN_SEC),
                    Some(interface_info.id.to_u64()),
                );
            }
        }

        let updated = self.interface_state.update_on_change(interface_info.id, &mut new_info);
        if !updated.is_empty() {
            // Count number of state updates.
            *self.stats.state_updates.entry(interface_info.id).or_insert(0) += 1;
        }

        updated
    }

    /// `evaluate_state` recomputes the reachability state for interface `id` and the system
    /// reachability state. If they have changed, it saves the new state and reports the changes.
    async fn evaluate_state(&mut self, id: Id, info: &hal::Interface) {
        let updated = self.update_state(&info).await;
        let system_updated =
            update_system_state(id, &updated, &mut self.system_state, &self.interface_state.0);
        self.report_system_updated(system_updated);
        self.report_updated(info.id, updated);
    }

    pub async fn timer_event(&mut self, id: u64) -> error::Result<()> {
        if let Some(info) = self.hal.get_interface(id).await {
            debug!("timer_event {} info {:?}", id, info);
            self.evaluate_state(id.into(), &info).await;
        }
        Ok(())
    }

    /// Processes an event coming from fuchsia.net.stack containing updates to
    /// properties associated with an interface. `OnInterfaceStatusChange` event is raised when an
    /// interface is enabled/disabled, connected/disconnected, or added/removed.
    pub async fn stack_event(&mut self, event: stack::StackEvent) -> error::Result<()> {
        self.stats.events += 1;
        match event {
            stack::StackEvent::OnInterfaceStatusChange { info } => {
                // This event is not really hooked up (stack does not generate them), code here
                // just for completeness and to be ready when it gets hooked up.
                if let Some(current_info) = self.hal.get_interface(info.id).await {
                    self.evaluate_state(info.id.into(), &current_info).await;
                }
                Ok(())
            }
        }
    }

    /// Processes an event coming from fuchsia.netstack containing updates to
    /// properties associated with an interface.
    pub async fn netstack_event(&mut self, event: netstack::NetstackEvent) -> error::Result<()> {
        self.stats.events += 1;
        match event {
            netstack::NetstackEvent::OnInterfacesChanged { interfaces } => {
                // This type of event is useful to know that there has been some change related
                // to the network state, but doesn't give an indication about what that event
                // was. We need to check all interfaces to find out if there has been a state
                // change.
                for i in interfaces {
                    if let Some(current_info) = self.hal.get_interface(u64::from(i.id)).await {
                        self.evaluate_state(Id::from(i.id as u64), &current_info).await;
                    }
                }
            }
        }
        Ok(())
    }

    /// `populate_state` queries the networks stack to determine current state.
    pub async fn populate_state(&mut self) -> error::Result<()> {
        for info in self.hal.interfaces().await?.iter() {
            self.evaluate_state(info.id, &info).await;
        }
        Ok(())
    }
}

fn log_state(info: Option<&mut InspectInfo>, proto: Proto, state: State) {
    info.map(|info| {
        info.log_state(proto, state);
    });
}

fn update_system_state(
    id: Id,
    updated: &HashSet<Proto>,
    system_state: &mut HashMap<Proto, (Id, NetworkInfo)>,
    interfaces: &HashMap<Id, ReachabilityInfo>,
) -> HashSet<Proto> {
    let mut changed = HashSet::new();
    for proto in updated {
        let (s_id, s_info) = system_state.get(&proto).unwrap();
        let info = interfaces.get(&id).unwrap().get(*proto);

        if s_info.state.state < info.state.state {
            let mut new_info = *info;
            new_info.previous_state = s_info.state;
            system_state.insert(*proto, (id, new_info));
            changed.insert(*proto);
        } else if *s_id == id && s_info.state.state > info.state.state {
            let (id, info) = interfaces
                .iter()
                .max_by(|x, y| x.1.get(*proto).state.state.cmp(&y.1.get(*proto).state.state))
                .unwrap();
            let mut new_info = *info.get(*proto);
            new_info.previous_state = s_info.state;
            system_state.insert(*proto, (*id, new_info));
            changed.insert(*proto);
        }
    }
    changed
}

/// `compute_state` processes an event and computes the reachability based on the event and
/// system observations.
async fn compute_state(
    interface_info: &hal::Interface,
    routes: Option<Vec<hal::Route>>,
    pinger: &dyn Pinger,
) -> Option<ReachabilityInfo> {
    let port_type = port_type(interface_info);
    if port_type == PortType::Loopback {
        return None;
    }

    let ipv4_address = interface_info.get_address_v4();

    let mut info = ReachabilityInfo::new(
        port_type,
        &interface_info.name,
        NetworkInfo {
            is_l3: interface_info.ipv4_addr.is_some(),
            state: State::Down.into(),
            ..Default::default()
        },
        NetworkInfo {
            is_l3: !interface_info.ipv6_addr.is_empty(),
            state: State::Down.into(),
            ..Default::default()
        },
    );

    if interface_info.state != hal::InterfaceState::Up {
        return Some(info);
    }

    info.get_mut(Proto::IPv4).state = State::Up.into();
    info.get_mut(Proto::IPv6).state = State::Up.into();

    // packet reception is network layer independent.
    if !packet_count_increases(interface_info.id) {
        // TODO(dpradilla): add active probing here.
        // No packets seen, but interface is up.
        return Some(info);
    }

    info.get_mut(Proto::IPv4).state = State::LinkLayerUp.into();
    info.get_mut(Proto::IPv6).state = State::LinkLayerUp.into();

    info.get_mut(Proto::IPv4).state =
        network_layer_state(ipv4_address.into_iter(), &routes, &info.get(Proto::IPv4), &*pinger)
            .await;

    // TODO(dpradilla): Add support for IPV6

    Some(info)
}

// `local_routes` traverses `route_table` to find routes that use a gateway local to `address`
// network.
fn local_routes<'a>(address: &LifIpAddr, route_table: &'a [hal::Route]) -> Vec<&'a hal::Route> {
    let local_routes: Vec<&hal::Route> = route_table
        .iter()
        .filter(|r| match r.gateway {
            Some(gateway) => address.is_in_same_subnet(&gateway),
            None => false,
        })
        .collect();
    local_routes
}

// TODO(dpradilla): implement.
// `has_local_neighbors` checks for local neighbors.
fn has_local_neighbors() -> bool {
    true
}

// TODO(dpradilla): implement.
// `packet_count_increases` verifies packet counts are going up.
fn packet_count_increases(_: hal::PortId) -> bool {
    true
}

fn port_type(interface_info: &hal::Interface) -> PortType {
    if interface_info.topo_path.contains("wlan") {
        PortType::WiFi
    } else if interface_info.topo_path.contains("ethernet") {
        PortType::Ethernet
    } else if interface_info.topo_path.contains("loopback") {
        PortType::Loopback
    } else {
        PortType::SVI
    }
}

// `network_layer_state` determines the L3 reachability state.
async fn network_layer_state(
    mut addresses: impl Iterator<Item = LifIpAddr>,
    routes: &Option<Vec<hal::Route>>,
    info: &NetworkInfo,
    p: &dyn Pinger,
) -> StateEvent {
    // This interface is not configured for L3, Nothing to check.
    if !info.is_l3 {
        return info.state;
    }

    if info.state.state != State::LinkLayerUp || !has_local_neighbors() {
        return info.state;
    }

    // TODO(dpradilla): add support for multiple addresses.
    let address = addresses.next();
    if address.is_none() {
        return info.state;
    }

    let route_table = match routes {
        Some(r) => r,
        _ => return State::Local.into(),
    };

    // Has local gateway.
    let rs = local_routes(&address.unwrap(), &route_table);
    if rs.is_empty() {
        return State::Local.into();
    }

    let mut gateway_active = false;
    for r in rs.iter() {
        if let Some(gw_ip) = r.gateway {
            if p.ping(&gw_ip.to_string()) {
                gateway_active = true;
                break;
            }
        }
    }
    if !gateway_active {
        return State::Local.into();
    }

    if !p.ping(INTERNET_CONNECTIVITY_CHECK_ADDRESS) {
        return State::Gateway.into();
    }
    State::Internet.into()
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, ping::IcmpPinger};

    #[test]
    fn test_has_local_neighbors() {
        assert_eq!(has_local_neighbors(), true);
    }

    #[test]
    fn test_packet_count_increases() {
        assert_eq!(packet_count_increases(hal::PortId::from(1)), true);
    }

    #[test]
    fn state_info_get() {
        let s = StateInfo::default();
        let id = Id::from(5);
        assert_eq!(s.get(id), None, "no interfaces");

        let s = StateInfo([(Id::from(4), ReachabilityInfo::default())].iter().cloned().collect());
        let id = Id::from(5);
        assert_eq!(s.get(id), None, "desired interface not present");

        let s = StateInfo(
            [
                (Id::from(6), ReachabilityInfo::default()),
                (Id::from(5), ReachabilityInfo::default()),
            ]
            .iter()
            .cloned()
            .collect(),
        );
        let id = Id::from(5);
        assert_eq!(s.get(id), Some(&ReachabilityInfo::default()), "desired interface present");
    }

    #[test]
    fn state_info_update_on_change() {
        // verify all state times are in agreement.
        fn verify_time(info: ReachabilityInfo, want: ReachabilityInfo) -> (bool, String) {
            if info.info[&Proto::IPv4].state.time != want.info[&Proto::IPv4].state.time {
                return (
                    false,
                    format!(
                        "IPv4 time is incorrect {:?} != {:?}",
                        info.info[&Proto::IPv4].state,
                        want.info[&Proto::IPv4].state,
                    ),
                );
            };
            if info.info[&Proto::IPv4].previous_state.time
                != want.info[&Proto::IPv4].previous_state.time
            {
                return (
                    false,
                    format!(
                        "IPv4 previous time is incorrect {:?} != {:?}",
                        info.info[&Proto::IPv4].previous_state,
                        want.info[&Proto::IPv4].previous_state
                    ),
                );
            };
            if info.info[&Proto::IPv6].state.time != want.info[&Proto::IPv6].state.time {
                return (
                    false,
                    format!(
                        "IPv6 time is incorrect {:?} != {:?}",
                        info.info[&Proto::IPv6].state,
                        want.info[&Proto::IPv6].state
                    ),
                );
            };
            if info.info[&Proto::IPv6].previous_state.time
                != want.info[&Proto::IPv6].previous_state.time
            {
                return (
                    false,
                    format!(
                        "IPv6 previous time is incorrect {:?} != {:?}",
                        info.info[&Proto::IPv6].previous_state,
                        want.info[&Proto::IPv6].previous_state
                    ),
                );
            };
            (true, "ok".to_string())
        }

        let mut s = StateInfo::default();
        let id = Id::from(5);
        let mut info = ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: StateEvent { state: State::Local, time: Time(zx::Time::from_nanos(10000)) },
                previous_state: StateEvent {
                    state: State::None,
                    time: Time(zx::Time::from_nanos(10000)),
                },
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: StateEvent {
                    state: State::LinkLayerUp,
                    time: Time(zx::Time::from_nanos(10000)),
                },
                previous_state: StateEvent {
                    state: State::None,
                    time: Time(zx::Time::from_nanos(10000)),
                },
            },
        );
        let want = info.clone();

        let updated = s.update_on_change(id, &mut info);
        assert_eq!(
            updated,
            [Proto::IPv4, Proto::IPv6].iter().cloned().collect(),
            "as nothing was there previously, both have changed."
        );
        assert_eq!(info, want, "no previous state, just add it");
        let (result, message) = verify_time(info, want);
        assert!(result, message);

        let mut info = ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: StateEvent {
                    state: State::Internet,
                    time: Time(zx::Time::from_nanos(20000)),
                },
                ..Default::default()
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: State::LinkLayerUp.into(),
                ..Default::default()
            },
        );

        let want = ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: StateEvent {
                    state: State::Internet,
                    time: Time(zx::Time::from_nanos(20000)),
                },
                previous_state: StateEvent {
                    state: State::Local,
                    time: Time(zx::Time::from_nanos(10000)),
                },
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: StateEvent {
                    state: State::LinkLayerUp,
                    time: Time(zx::Time::from_nanos(10000)),
                },
                previous_state: StateEvent {
                    state: State::None,
                    time: Time(zx::Time::from_nanos(10000)),
                },
            },
        );

        let updated = s.update_on_change(id, &mut info);
        assert_eq!(updated, [Proto::IPv4].iter().cloned().collect(), "An IPv4 change.");
        assert_eq!(info, want, "IPv4 has now a previouse state, IPv6 has not changed.");
        let (result, message) = verify_time(info, want);
        assert!(result, message);
    }

    #[test]
    fn test_fix_state_on_change_no_change() {
        let old = NetworkInfo {
            is_l3: true,
            state: StateEvent {
                state: State::LinkLayerUp,
                time: Time(zx::Time::from_nanos(10000)),
            },
            ..Default::default()
        };
        let mut new = NetworkInfo {
            is_l3: true,
            state: StateEvent {
                state: State::LinkLayerUp,
                time: Time(zx::Time::from_nanos(20000)),
            },
            ..Default::default()
        };
        let has_changed = fix_state_on_change(&old, &mut new);
        assert_eq!(has_changed, false, "no change is seen");
        assert_eq!(old, new, "all fields must be same as before (except time)");
        assert_eq!(
            Time(zx::Time::from_nanos(10000)),
            new.state.time,
            "time should be same as original"
        );
    }

    #[test]
    fn test_fix_state_on_change_change() {
        let old = NetworkInfo {
            is_l3: true,
            state: StateEvent {
                state: State::LinkLayerUp,
                time: Time(zx::Time::from_nanos(10000)),
            },
            ..Default::default()
        };
        let mut new = NetworkInfo {
            is_l3: true,
            state: StateEvent { state: State::Local, time: Time(zx::Time::from_nanos(20000)) },
            ..Default::default()
        };
        let has_changed = fix_state_on_change(&old, &mut new);
        let want = NetworkInfo {
            is_l3: true,
            state: new.state.clone(),
            previous_state: old.state.clone(),
            ..Default::default()
        };
        assert_eq!(has_changed, true, "change is seen");
        assert_eq!(new, want, "all fields ");
        assert_eq!(
            Time(zx::Time::from_nanos(20000)),
            new.state.time,
            "time should be time of change"
        );
        assert_eq!(
            Time(zx::Time::from_nanos(10000)),
            new.previous_state.time,
            "previous time should be time of previous event."
        );
    }

    #[test]
    fn test_port_type() {
        assert_eq!(
            port_type(&hal::Interface {
                ipv4_addr: None,
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Unknown,
                enabled: true,
                topo_path: "loopback".to_string(),
                name: "lo".to_string(),
                id: hal::PortId::from(1),
                dhcp_client_enabled: false,
            }),
            PortType::Loopback
        );
        assert_eq!(
            port_type(&hal::Interface {
                ipv4_addr: None,
                ipv6_addr: Vec::new(),
                enabled: true,
                state: hal::InterfaceState::Up,
                topo_path: "ethernet/eth0".to_string(),
                name: "eth0".to_string(),
                id: hal::PortId::from(1),
                dhcp_client_enabled: false,
            }),
            PortType::Ethernet
        );
        assert_eq!(
            port_type(&hal::Interface {
                ipv4_addr: None,
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Up,
                enabled: true,
                topo_path: "ethernet/wlan".to_string(),
                name: "eth0".to_string(),
                id: hal::PortId::from(1),
                dhcp_client_enabled: false,
            }),
            PortType::WiFi
        );
        assert_eq!(
            port_type(&hal::Interface {
                ipv4_addr: None,
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Up,
                enabled: true,
                topo_path: "br0".to_string(),
                name: "eth0".to_string(),
                id: hal::PortId::from(1),
                dhcp_client_enabled: false,
            }),
            PortType::SVI
        );
    }

    #[test]
    fn test_local_routes() {
        let address = &LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 };
        let route_table = &vec![
            hal::Route {
                gateway: Some("1.2.3.1".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 },
            },
        ];

        let want_route = &hal::Route {
            gateway: Some("1.2.3.1".parse().unwrap()),
            metric: None,
            port_id: Some(hal::PortId::from(1)),
            target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
        };

        let want = vec![want_route];
        let got = local_routes(address, route_table);
        assert_eq!(got, want, "route via local network found.");

        let address = &LifIpAddr { address: "2.2.3.4".parse().unwrap(), prefix: 24 };
        let route_table = &vec![
            hal::Route {
                gateway: Some("1.2.3.1".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "2.2.3.0".parse().unwrap(), prefix: 24 },
            },
        ];

        let want = Vec::<&hal::Route>::new();
        let got = local_routes(address, route_table);
        assert_eq!(got, want, "route via local network not present.");
    }

    struct FakePing<'a> {
        gateway_url: &'a str,
        gateway_response: bool,
        internet_url: &'a str,
        internet_response: bool,
        default_response: bool,
    }

    impl Pinger for FakePing<'_> {
        fn ping(&self, url: &str) -> bool {
            if self.gateway_url == url {
                return self.gateway_response;
            }
            if self.internet_url == url {
                return self.internet_response;
            }
            self.default_response
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_network_layer_state() {
        let address = Some(LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 });
        let route_table = &vec![
            hal::Route {
                gateway: Some("1.2.3.1".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 },
            },
        ];
        let route_table_2 = vec![
            hal::Route {
                gateway: Some("2.2.3.1".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "2.2.3.0".parse().unwrap(), prefix: 24 },
            },
        ];

        assert_eq!(
            network_layer_state(
                address.into_iter(),
                &Some(route_table.to_vec()),
                &NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: State::LinkLayerUp.into(),
                    ..Default::default()
                },
                &FakePing {
                    gateway_url: "1.2.3.1",
                    gateway_response: true,
                    internet_url: "8.8.8.8",
                    internet_response: true,
                    default_response: true
                },
            )
            .await,
            State::Internet,
            "All is good. Can reach internet"
        );

        assert_eq!(
            network_layer_state(
                address.into_iter(),
                &Some(route_table.to_vec()),
                &NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: State::LinkLayerUp.into(),
                    ..Default::default()
                },
                &FakePing {
                    gateway_url: "1.2.3.1",
                    gateway_response: true,
                    internet_url: "8.8.8.8",
                    internet_response: false,
                    default_response: true
                },
            )
            .await,
            State::Gateway,
            "Can reach gateway"
        );

        assert_eq!(
            network_layer_state(
                address.into_iter(),
                &Some(route_table.to_vec()),
                &NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: State::LinkLayerUp.into(),
                    ..Default::default()
                },
                &FakePing {
                    gateway_url: "1.2.3.1",
                    gateway_response: false,
                    internet_url: "8.8.8.8",
                    internet_response: false,
                    default_response: true
                },
            )
            .await,
            State::Local,
            "Local only, Cannot reach gateway"
        );

        assert_eq!(
            network_layer_state(
                address.into_iter(),
                &None,
                &NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: State::LinkLayerUp.into(),
                    ..Default::default()
                },
                &IcmpPinger {},
            )
            .await,
            State::Local,
            "No routes"
        );

        assert_eq!(
            network_layer_state(
                None.into_iter(),
                &Some(route_table_2),
                &NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: State::NetworkLayerUp.into(),
                    ..Default::default()
                },
                &IcmpPinger {},
            )
            .await,
            State::NetworkLayerUp,
            "default route is not local"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn test_compute_state() {
        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                topo_path: "ifname".to_string(),
                name: "eth1".to_string(),
                ipv4_addr: None,
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Down,
                enabled: false,
                dhcp_client_enabled: false,
            },
            None,
            &FakePing {
                gateway_url: "1.2.3.1",
                gateway_response: true,
                internet_url: "8.8.8.8",
                internet_response: false,
                default_response: false,
            },
        )
        .await;
        assert_eq!(
            got,
            Some(ReachabilityInfo::new(
                PortType::SVI,
                "eth1",
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: State::Down.into(),
                    ..Default::default()
                },
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: State::Down.into(),
                    ..Default::default()
                },
            )),
            "not an ethernet interface"
        );

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                topo_path: "ethernet/eth0".to_string(),
                name: "eth1".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Down,
                enabled: false,
                dhcp_client_enabled: false,
            },
            None,
            &FakePing {
                gateway_url: "1.2.3.1",
                gateway_response: true,
                internet_url: "8.8.8.8",
                internet_response: false,
                default_response: false,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: State::Down.into(),
                ..Default::default()
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: State::Down.into(),
                ..Default::default()
            },
        ));
        assert_eq!(got, want, "ethernet interface, ipv4 configured, interface down");

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                topo_path: "ethernet/eth0".to_string(),
                name: "eth1".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Up,
                enabled: true,
                dhcp_client_enabled: false,
            },
            None,
            &FakePing {
                gateway_url: "1.2.3.1",
                gateway_response: true,
                internet_url: "8.8.8.8",
                internet_response: false,
                default_response: false,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: State::Local.into(),
                ..Default::default()
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: State::LinkLayerUp.into(),
                ..Default::default()
            },
        ));
        assert_eq!(got, want, "ethernet interface, ipv4 configured, interface up");

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                topo_path: "ethernet/eth0".to_string(),
                name: "eth1".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Up,
                enabled: true,
                dhcp_client_enabled: false,
            },
            Some(vec![hal::Route {
                gateway: Some("2.2.3.1".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            }]),
            &FakePing {
                gateway_url: "1.2.3.1",
                gateway_response: true,
                internet_url: "8.8.8.8",
                internet_response: false,
                default_response: false,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: State::Local.into(),
                ..Default::default()
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: State::LinkLayerUp.into(),
                ..Default::default()
            },
        ));
        assert_eq!(
            got, want,
            "ethernet interface, ipv4 configured, interface up, no local gateway"
        );

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                topo_path: "ethernet/eth0".to_string(),
                name: "eth1".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Up,
                enabled: true,
                dhcp_client_enabled: false,
            },
            Some(vec![hal::Route {
                gateway: Some("1.2.3.1".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            }]),
            &FakePing {
                gateway_url: "1.2.3.1",
                gateway_response: true,
                internet_url: "8.8.8.8",
                internet_response: false,
                default_response: false,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: State::Gateway.into(),
                ..Default::default()
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: State::LinkLayerUp.into(),
                ..Default::default()
            },
        ));
        assert_eq!(
            got, want,
            "ethernet interface, ipv4 configured, interface up, with local gateway"
        );

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                topo_path: "ethernet/eth0".to_string(),
                name: "eth1".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Up,
                enabled: true,
                dhcp_client_enabled: false,
            },
            Some(vec![hal::Route {
                gateway: Some("1.2.3.1".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            }]),
            &FakePing {
                gateway_url: "1.2.3.1",
                gateway_response: true,
                internet_url: "8.8.8.8",
                internet_response: true,
                default_response: false,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: State::Internet.into(),
                ..Default::default()
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: State::LinkLayerUp.into(),
                ..Default::default()
            },
        ));
        assert_eq!(got, want, "ethernet interface, ipv4 configured, interface up, with internet");

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                topo_path: "ethernet/eth0".to_string(),
                name: "eth1".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Up,
                enabled: true,
                dhcp_client_enabled: false,
            },
            Some(vec![hal::Route {
                gateway: Some("fe80::2aad:3fe0:7436:5677".parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: "::".parse().unwrap(), prefix: 0 },
            }]),
            &FakePing {
                gateway_url: "1.2.3.1",
                gateway_response: true,
                internet_url: "8.8.8.8",
                internet_response: false,
                default_response: false,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            "eth1",
            NetworkInfo {
                is_default: false,
                is_l3: true,
                state: State::Local.into(),
                ..Default::default()
            },
            NetworkInfo {
                is_default: false,
                is_l3: false,
                state: State::LinkLayerUp.into(),
                ..Default::default()
            },
        ));
        assert_eq!(
            got, want,
            "ethernet interface, ipv4 configured, interface up, no local gateway"
        );
    }

    #[test]
    fn test_update_system_state() {
        let updated: HashSet<Proto> = [Proto::IPv4].iter().cloned().collect();
        let mut system_state: HashMap<Proto, (Id, NetworkInfo)> = [
            (
                Proto::IPv4,
                (
                    Id::from(0),
                    NetworkInfo {
                        is_default: false,
                        is_l3: true,
                        state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                ),
            ),
            (
                Proto::IPv6,
                (
                    Id::from(0),
                    NetworkInfo {
                        is_default: false,
                        is_l3: true,
                        state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                ),
            ),
        ]
        .iter()
        .cloned()
        .collect();
        let mut interfaces: HashMap<Id, ReachabilityInfo> = [
            (
                Id::from(1),
                ReachabilityInfo::new(
                    PortType::Ethernet,
                    "eth1",
                    NetworkInfo {
                        is_default: false,
                        is_l3: true,
                        state: StateEvent {
                            state: State::Down,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                    NetworkInfo {
                        is_default: false,
                        is_l3: false,
                        state: StateEvent {
                            state: State::Down,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                ),
            ),
            (
                Id::from(3),
                ReachabilityInfo::new(
                    PortType::Ethernet,
                    "eth1",
                    NetworkInfo {
                        is_default: false,
                        is_l3: true,
                        state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                    NetworkInfo {
                        is_default: false,
                        is_l3: false,
                        state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                ),
            ),
            (
                Id::from(3),
                ReachabilityInfo::new(
                    PortType::Ethernet,
                    "eth1",
                    NetworkInfo {
                        is_default: false,
                        is_l3: true,
                        state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                    NetworkInfo {
                        is_default: false,
                        is_l3: false,
                        state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                        previous_state: StateEvent {
                            state: State::None,
                            time: Time(zx::Time::from_nanos(0)),
                        },
                    },
                ),
            ),
        ]
        .iter()
        .cloned()
        .collect();
        update_system_state(Id::from(1), &updated, &mut system_state, &interfaces);
        assert_eq!(
            system_state,
            [
                (
                    Proto::IPv4,
                    (
                        Id::from(1),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::Down,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
                (
                    Proto::IPv6,
                    (
                        Id::from(0),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
            "port 1 is down"
        );

        interfaces.insert(
            Id::from(2),
            ReachabilityInfo::new(
                PortType::Ethernet,
                "eth1",
                NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: StateEvent { state: State::Up, time: Time(zx::Time::from_nanos(20000)) },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: StateEvent {
                        state: State::Local,
                        time: Time(zx::Time::from_nanos(200000)),
                    },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
            ),
        );
        update_system_state(Id::from(2), &updated, &mut system_state, &interfaces);
        assert_eq!(
            system_state,
            [
                (
                    Proto::IPv4,
                    (
                        Id::from(2),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::Up,
                                time: Time(zx::Time::from_nanos(10000)),
                            },
                            previous_state: StateEvent {
                                state: State::Down,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
                (
                    Proto::IPv6,
                    (
                        Id::from(0),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
            "port 2 is up"
        );

        interfaces.insert(
            Id::from(3),
            ReachabilityInfo::new(
                PortType::Ethernet,
                "eth1",
                NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: StateEvent {
                        state: State::Internet,
                        time: Time(zx::Time::from_nanos(30000)),
                    },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: StateEvent { state: State::None, time: Time(zx::Time::from_nanos(0)) },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
            ),
        );
        update_system_state(Id::from(3), &updated, &mut system_state, &interfaces);
        assert_eq!(
            system_state,
            [
                (
                    Proto::IPv4,
                    (
                        Id::from(3),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::Internet,
                                time: Time(zx::Time::from_nanos(30000)),
                            },
                            previous_state: StateEvent {
                                state: State::Up,
                                time: Time(zx::Time::from_nanos(20000)),
                            },
                        },
                    ),
                ),
                (
                    Proto::IPv6,
                    (
                        Id::from(0),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
            "port 3 internet"
        );

        interfaces.insert(
            Id::from(1),
            ReachabilityInfo::new(
                PortType::Ethernet,
                "eth1",
                NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: StateEvent {
                        state: State::Local,
                        time: Time(zx::Time::from_nanos(40000)),
                    },
                    previous_state: StateEvent {
                        state: State::Up,
                        time: Time(zx::Time::from_nanos(10000)),
                    },
                },
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: StateEvent {
                        state: State::Internet,
                        time: Time(zx::Time::from_nanos(200000)),
                    },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
            ),
        );
        update_system_state(Id::from(1), &updated, &mut system_state, &interfaces);
        assert_eq!(
            system_state,
            [
                (
                    Proto::IPv4,
                    (
                        Id::from(3),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::Internet,
                                time: Time(zx::Time::from_nanos(30000)),
                            },
                            previous_state: StateEvent {
                                state: State::Up,
                                time: Time(zx::Time::from_nanos(20000)),
                            },
                        },
                    ),
                ),
                (
                    Proto::IPv6,
                    (
                        Id::from(0),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
            "port 1 local"
        );

        interfaces.insert(
            Id::from(3),
            ReachabilityInfo::new(
                PortType::Ethernet,
                "eth1",
                NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: StateEvent {
                        state: State::Down,
                        time: Time(zx::Time::from_nanos(50000)),
                    },
                    previous_state: StateEvent {
                        state: State::Up,
                        time: Time(zx::Time::from_nanos(10000)),
                    },
                },
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: StateEvent { state: State::None, time: Time(zx::Time::from_nanos(0)) },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
            ),
        );
        update_system_state(Id::from(3), &updated, &mut system_state, &interfaces);
        assert_eq!(
            system_state,
            [
                (
                    Proto::IPv4,
                    (
                        Id::from(1),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::Local,
                                time: Time(zx::Time::from_nanos(40000)),
                            },
                            previous_state: StateEvent {
                                state: State::Internet,
                                time: Time(zx::Time::from_nanos(30000)),
                            },
                        },
                    ),
                ),
                (
                    Proto::IPv6,
                    (
                        Id::from(0),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
            "port 3 down"
        );

        interfaces.insert(
            Id::from(3),
            ReachabilityInfo::new(
                PortType::Ethernet,
                "eth1",
                NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: StateEvent {
                        state: State::Gateway,
                        time: Time(zx::Time::from_nanos(60000)),
                    },
                    previous_state: StateEvent {
                        state: State::Up,
                        time: Time(zx::Time::from_nanos(10000)),
                    },
                },
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: StateEvent { state: State::None, time: Time(zx::Time::from_nanos(0)) },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
            ),
        );
        update_system_state(Id::from(3), &updated, &mut system_state, &interfaces);
        assert_eq!(
            system_state,
            [
                (
                    Proto::IPv4,
                    (
                        Id::from(3),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::Gateway,
                                time: Time(zx::Time::from_nanos(60000)),
                            },
                            previous_state: StateEvent {
                                state: State::Local,
                                time: Time(zx::Time::from_nanos(40000)),
                            },
                        },
                    ),
                ),
                (
                    Proto::IPv6,
                    (
                        Id::from(0),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
            "port 3 gateway"
        );

        interfaces.insert(
            Id::from(3),
            ReachabilityInfo::new(
                PortType::Ethernet,
                "eth1",
                NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: StateEvent {
                        state: State::Internet,
                        time: Time(zx::Time::from_nanos(70000)),
                    },
                    previous_state: StateEvent {
                        state: State::Gateway,
                        time: Time(zx::Time::from_nanos(60000)),
                    },
                },
                NetworkInfo {
                    is_default: false,
                    is_l3: false,
                    state: StateEvent { state: State::None, time: Time(zx::Time::from_nanos(0)) },
                    previous_state: StateEvent {
                        state: State::None,
                        time: Time(zx::Time::from_nanos(0)),
                    },
                },
            ),
        );
        update_system_state(Id::from(3), &updated, &mut system_state, &interfaces);
        assert_eq!(
            system_state,
            [
                (
                    Proto::IPv4,
                    (
                        Id::from(3),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::Internet,
                                time: Time(zx::Time::from_nanos(70000)),
                            },
                            previous_state: StateEvent {
                                state: State::Gateway,
                                time: Time(zx::Time::from_nanos(60000)),
                            },
                        },
                    ),
                ),
                (
                    Proto::IPv6,
                    (
                        Id::from(0),
                        NetworkInfo {
                            is_default: false,
                            is_l3: true,
                            state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                            previous_state: StateEvent {
                                state: State::None,
                                time: Time(zx::Time::from_nanos(0)),
                            },
                        },
                    ),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
            "port 3 gateway"
        );
    }
}

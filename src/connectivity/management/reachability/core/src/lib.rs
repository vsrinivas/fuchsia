// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod inspect;
mod ping;

#[macro_use]
extern crate log;
pub use ping::{ping_fut, IcmpPinger, Pinger};
use {
    fidl_fuchsia_hardware_network, fidl_fuchsia_net_interfaces as fnet_interfaces,
    fidl_fuchsia_net_interfaces_ext::{self as fnet_interfaces_ext},
    fuchsia_inspect::Inspector,
    fuchsia_zircon::{self as zx},
    inspect::InspectInfo,
    net_types::ScopeableAddress as _,
    network_manager_core::{address::LifIpAddr, error, hal},
    std::collections::{HashMap, HashSet},
};

const IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS: &str = "8.8.8.8";
const IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS: &str = "2001:4860:4860::8888";

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
    /// Unknown.
    Unknown,
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

impl From<fnet_interfaces::DeviceClass> for PortType {
    fn from(device_class: fnet_interfaces::DeviceClass) -> Self {
        match device_class {
            fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {}) => PortType::Loopback,
            fnet_interfaces::DeviceClass::Device(device_class) => match device_class {
                fidl_fuchsia_hardware_network::DeviceClass::Ethernet => PortType::Ethernet,
                fidl_fuchsia_hardware_network::DeviceClass::Wlan => PortType::WiFi,
                fidl_fuchsia_hardware_network::DeviceClass::Ppp
                | fidl_fuchsia_hardware_network::DeviceClass::Bridge
                | fidl_fuchsia_hardware_network::DeviceClass::Unknown => PortType::Unknown,
            },
        }
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
struct Time(zx::Time);
impl Default for Time {
    fn default() -> Self {
        Time(zx::Time::get_monotonic())
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

/// `Monitor` monitors the reachability state.
pub struct Monitor {
    hal: hal::NetCfg,
    interface_state: StateInfo,
    system_state: HashMap<Proto, (Id, NetworkInfo)>,
    stats: Stats,
    pinger: Box<dyn Pinger>,
    inspector: Option<&'static Inspector>,
    system_node: Option<InspectInfo>,
    nodes: HashMap<Id, InspectInfo>,
}

impl Monitor {
    /// Create the monitoring service.
    pub fn new(pinger: Box<dyn Pinger>) -> anyhow::Result<Self> {
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

    /// Returns an interface watcher client proxy.
    pub fn create_interface_watcher(&self) -> error::Result<fnet_interfaces::WatcherProxy> {
        self.hal.create_interface_watcher()
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

    /// Update state based on the new reachability info.
    pub fn update_state(&mut self, id: Id, mut new_info: ReachabilityInfo) {
        let updated = self.interface_state.update_on_change(id, &mut new_info);
        if !updated.is_empty() {
            // Count number of state updates.
            *self.stats.state_updates.entry(id).or_insert(0) += 1;

            let system_updated =
                update_system_state(id, &updated, &mut self.system_state, &self.interface_state.0);
            let () = self.report_system_updated(system_updated);
            let () = self.report_updated(id, updated);
        }
    }

    /// Compute the reachability state of an interface.
    ///
    /// The interface may have been recently-discovered, or the properties of a known interface may
    /// have changed.
    pub async fn compute_state(&mut self, properties: &fnet_interfaces_ext::Properties) {
        if let Some(info) =
            compute_state(properties, self.hal.routes().await, &mut *self.pinger).await
        {
            let id = Id::from(properties.id);
            let () = self.update_state(id, info);
        }
    }

    /// Handle an interface removed event.
    pub fn handle_interface_removed(
        &mut self,
        fnet_interfaces_ext::Properties { id, .. }: fnet_interfaces_ext::Properties,
    ) {
        if let Some(mut info) = self.interface_state.get(id.into()).cloned() {
            for info in info.info.values_mut() {
                info.previous_state = StateEvent::default();
                info.state = State::Removed.into();
            }
            let () = self.update_state(id.into(), info);
        }
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
    &fnet_interfaces_ext::Properties {
        id,
        ref name,
        device_class,
        online,
        ref addresses,
        has_default_ipv4_route: _,
        has_default_ipv6_route: _,
    }: &fnet_interfaces_ext::Properties,
    routes: Option<Vec<hal::Route>>,
    pinger: &mut dyn Pinger,
) -> Option<ReachabilityInfo> {
    let id = Id::from(id);
    let port_type = PortType::from(device_class);
    let (v4_addrs, v6_addrs): (Vec<_>, _) = addresses
        .iter()
        .map(|fnet_interfaces_ext::Address { addr }| LifIpAddr::from(addr))
        .partition(|addr| addr.is_ipv4());

    if port_type == PortType::Loopback {
        return None;
    }

    let mut info = ReachabilityInfo::new(
        port_type,
        name,
        NetworkInfo {
            is_l3: !v4_addrs.is_empty(),
            state: if online { State::Up } else { State::Down }.into(),
            ..Default::default()
        },
        NetworkInfo {
            is_l3: !v6_addrs.is_empty(),
            state: if online { State::Up } else { State::Down }.into(),
            ..Default::default()
        },
    );

    if !online {
        return Some(info);
    }

    // packet reception is network layer independent.
    if !packet_count_increases(id) {
        // TODO(dpradilla): add active probing here.
        // No packets seen, but interface is up.
        return Some(info);
    }

    info.get_mut(Proto::IPv4).state = State::LinkLayerUp.into();
    info.get_mut(Proto::IPv6).state = State::LinkLayerUp.into();

    // TODO(fxbug.dev/65581) Parallelize the following two calls for IPv4 and IPv6 respectively
    // when the ping logic is implemented in Rust and async.
    info.get_mut(Proto::IPv4).state = network_layer_state(
        v4_addrs,
        &routes,
        &info.get(Proto::IPv4),
        pinger,
        IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS,
    )
    .await;
    info.get_mut(Proto::IPv6).state = network_layer_state(
        v6_addrs,
        &routes,
        &info.get(Proto::IPv6),
        pinger,
        IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS,
    )
    .await;

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

// `network_layer_state` determines the L3 reachability state.
async fn network_layer_state(
    addresses: impl IntoIterator<Item = LifIpAddr>,
    routes: &Option<Vec<hal::Route>>,
    info: &NetworkInfo,
    p: &mut dyn Pinger,
    ping_address: &str,
) -> StateEvent {
    // This interface is not configured for L3, Nothing to check.
    if !info.is_l3 {
        return info.state;
    }

    if info.state.state != State::LinkLayerUp || !has_local_neighbors() {
        return info.state;
    }

    let route_table = match routes {
        Some(r) => r,
        None => {
            return if addresses.into_iter().count() > 0 { State::Local.into() } else { info.state }
        }
    };

    let mut gateway_reachable = false;
    'outer: for a in addresses {
        for r in local_routes(&a, &route_table) {
            if let Some(gw_ip) = r.gateway {
                let gw_url = match gw_ip {
                    std::net::IpAddr::V4(_) => gw_ip.to_string(),
                    std::net::IpAddr::V6(v6) => {
                        if let Some(id) = r.port_id {
                            if net_types::ip::Ipv6Addr::new(v6.octets()).scope()
                                != net_types::ip::Ipv6Scope::Global
                            {
                                format!("{}%{}", gw_ip, id.to_u64())
                            } else {
                                gw_ip.to_string()
                            }
                        } else {
                            gw_ip.to_string()
                        }
                    }
                };
                if p.ping(&gw_url).await {
                    gateway_reachable = true;
                    break 'outer;
                }
            }
        }
    }
    if !gateway_reachable {
        return State::Local.into();
    }

    if !p.ping(ping_address).await {
        return State::Gateway.into();
    }
    State::Internet.into()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_trait::async_trait,
        fuchsia_async as fasync,
        net_declare::{fidl_subnet, std_ip},
        std::net::{IpAddr, Ipv4Addr, Ipv6Addr},
    };

    const ETHERNET_INTERFACE_NAME: &str = "eth1";

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
            ETHERNET_INTERFACE_NAME,
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
            ETHERNET_INTERFACE_NAME,
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
            ETHERNET_INTERFACE_NAME,
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
            PortType::from(fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {})),
            PortType::Loopback
        );
        assert_eq!(
            PortType::from(fnet_interfaces::DeviceClass::Device(
                fidl_fuchsia_hardware_network::DeviceClass::Ethernet
            )),
            PortType::Ethernet
        );
        assert_eq!(
            PortType::from(fnet_interfaces::DeviceClass::Device(
                fidl_fuchsia_hardware_network::DeviceClass::Wlan
            )),
            PortType::WiFi
        );
        assert_eq!(
            PortType::from(fnet_interfaces::DeviceClass::Device(
                fidl_fuchsia_hardware_network::DeviceClass::Unknown
            )),
            PortType::Unknown
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

    #[derive(Default)]
    struct FakePing<'a> {
        gateway_urls: Vec<&'a str>,
        gateway_response: bool,
        internet_response: bool,
    }

    #[async_trait]
    impl Pinger for FakePing<'_> {
        async fn ping(&mut self, url: &str) -> bool {
            if self.gateway_urls.contains(&url) {
                return self.gateway_response;
            }
            if IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS == url
                || IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS == url
            {
                return self.internet_response;
            }
            panic!("ping destination URL {} is not in the set of gateway URLs ({:?}) or equal to the IPv4/IPv6 internet connectivity check address", url, self.gateway_urls);
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_network_layer_state_ipv4() {
        test_network_layer_state(
            "1.2.3.0",
            "1.2.3.4",
            "1.2.3.1",
            "2.2.3.0",
            "2.2.3.1",
            LifIpAddr { address: IpAddr::V4(Ipv4Addr::UNSPECIFIED), prefix: 0 },
            IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS,
            24,
        )
        .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_network_layer_state_ipv6() {
        test_network_layer_state(
            "123::",
            "123::4",
            "123::1",
            "223::",
            "223::1",
            LifIpAddr { address: IpAddr::V6(Ipv6Addr::UNSPECIFIED), prefix: 0 },
            IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS,
            64,
        )
        .await
    }

    async fn test_network_layer_state(
        net1: &str,
        net1_addr: &str,
        net1_gateway: &str,
        net2: &str,
        net2_gateway: &str,
        unspecified_addr: LifIpAddr,
        ping_url: &str,
        prefix: u8,
    ) {
        let address = LifIpAddr { address: net1_addr.parse().unwrap(), prefix };
        let route_table = vec![
            hal::Route {
                gateway: Some(net1_gateway.parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: unspecified_addr,
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: net1.parse().unwrap(), prefix },
            },
        ];
        let route_table_2 = vec![
            hal::Route {
                gateway: Some(net2_gateway.parse().unwrap()),
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: unspecified_addr,
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: net1.parse().unwrap(), prefix },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(hal::PortId::from(1)),
                target: LifIpAddr { address: net2.parse().unwrap(), prefix },
            },
        ];
        let network_info = NetworkInfo {
            is_default: false,
            is_l3: true,
            state: State::LinkLayerUp.into(),
            ..Default::default()
        };

        assert_eq!(
            network_layer_state(
                std::iter::once(address),
                &Some(route_table.clone()),
                &network_info,
                &mut FakePing {
                    gateway_urls: vec![net1_gateway],
                    gateway_response: true,
                    internet_response: true,
                },
                ping_url,
            )
            .await,
            State::Internet,
            "All is good. Can reach internet"
        );

        assert_eq!(
            network_layer_state(
                std::iter::once(address),
                &Some(route_table.clone()),
                &network_info,
                &mut FakePing {
                    gateway_urls: vec![net1_gateway],
                    gateway_response: true,
                    internet_response: false,
                },
                ping_url,
            )
            .await,
            State::Gateway,
            "Can reach gateway"
        );

        assert_eq!(
            network_layer_state(
                std::iter::once(address),
                &Some(route_table.clone()),
                &network_info,
                &mut FakePing {
                    gateway_urls: vec![net1_gateway],
                    gateway_response: false,
                    internet_response: false,
                },
                ping_url,
            )
            .await,
            State::Local,
            "Local only, Cannot reach gateway"
        );

        assert_eq!(
            network_layer_state(
                std::iter::once(address),
                &None,
                &network_info,
                &mut FakePing::default(),
                ping_url,
            )
            .await,
            State::Local,
            "No routes"
        );

        assert_eq!(
            network_layer_state(
                std::iter::empty(),
                &Some(route_table_2),
                &network_info,
                &mut FakePing::default(),
                ping_url,
            )
            .await,
            State::Local,
            "No default route"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn test_compute_state() {
        let id = hal::PortId::from(1);
        let properties = fnet_interfaces_ext::Properties {
            id: id.to_u64(),
            name: ETHERNET_INTERFACE_NAME.to_string(),
            device_class: fnet_interfaces::DeviceClass::Device(
                fidl_fuchsia_hardware_network::DeviceClass::Ethernet,
            ),
            has_default_ipv4_route: true,
            has_default_ipv6_route: true,
            online: true,
            addresses: vec![
                fnet_interfaces_ext::Address { addr: fidl_subnet!("1.2.3.4/24") },
                fnet_interfaces_ext::Address { addr: fidl_subnet!("123::4/64") },
            ],
        };
        let route_table = vec![
            hal::Route {
                gateway: Some(std_ip!("1.2.3.1")),
                metric: None,
                port_id: Some(id),
                target: LifIpAddr { address: IpAddr::V4(Ipv4Addr::UNSPECIFIED), prefix: 0 },
            },
            hal::Route {
                gateway: Some(std_ip!("123::1")),
                metric: None,
                port_id: Some(id),
                target: LifIpAddr { address: IpAddr::V6(Ipv6Addr::UNSPECIFIED), prefix: 0 },
            },
        ];
        let route_table2 = vec![
            hal::Route {
                gateway: Some(std_ip!("2.2.3.1")),
                metric: None,
                port_id: Some(id),
                target: LifIpAddr { address: IpAddr::V4(Ipv4Addr::UNSPECIFIED), prefix: 0 },
            },
            hal::Route {
                gateway: Some(std_ip!("223::1")),
                metric: None,
                port_id: Some(id),
                target: LifIpAddr { address: IpAddr::V6(Ipv6Addr::UNSPECIFIED), prefix: 0 },
            },
        ];

        const NON_ETHERNET_INTERFACE_NAME: &str = "test01";
        let got = compute_state(
            &fnet_interfaces_ext::Properties {
                id: 1,
                name: NON_ETHERNET_INTERFACE_NAME.to_string(),
                device_class: fnet_interfaces::DeviceClass::Device(
                    fidl_fuchsia_hardware_network::DeviceClass::Unknown,
                ),
                online: false,
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
                addresses: vec![],
            },
            None,
            &mut FakePing::default(),
        )
        .await;
        assert_eq!(
            got,
            Some(ReachabilityInfo::new(
                PortType::Unknown,
                NON_ETHERNET_INTERFACE_NAME,
                NetworkInfo { state: State::Down.into(), ..Default::default() },
                NetworkInfo { state: State::Down.into(), ..Default::default() },
            )),
            "not an ethernet interface, no addresses, interface down"
        );

        let got = compute_state(
            &fnet_interfaces_ext::Properties { online: false, ..properties.clone() },
            None,
            &mut FakePing::default(),
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            ETHERNET_INTERFACE_NAME,
            NetworkInfo { is_l3: true, state: State::Down.into(), ..Default::default() },
            NetworkInfo { is_l3: true, state: State::Down.into(), ..Default::default() },
        ));
        assert_eq!(got, want, "ethernet interface, address configured, interface down");

        let got = compute_state(
            &fnet_interfaces_ext::Properties {
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
                ..properties.clone()
            },
            None,
            &mut FakePing::default(),
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            ETHERNET_INTERFACE_NAME,
            NetworkInfo { is_l3: true, state: State::Local.into(), ..Default::default() },
            NetworkInfo { is_l3: true, state: State::Local.into(), ..Default::default() },
        ));
        assert_eq!(got, want, "ethernet interface, address configured, interface up");

        let got = compute_state(&properties, Some(route_table2), &mut FakePing::default()).await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            ETHERNET_INTERFACE_NAME,
            NetworkInfo { is_l3: true, state: State::Local.into(), ..Default::default() },
            NetworkInfo { is_l3: true, state: State::Local.into(), ..Default::default() },
        ));
        assert_eq!(
            got, want,
            "ethernet interface, address configured, interface up, no local gateway"
        );

        let got = compute_state(
            &properties,
            Some(route_table.clone()),
            &mut FakePing {
                gateway_urls: vec!["1.2.3.1", "123::1"],
                gateway_response: true,
                internet_response: false,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            ETHERNET_INTERFACE_NAME,
            NetworkInfo { is_l3: true, state: State::Gateway.into(), ..Default::default() },
            NetworkInfo { is_l3: true, state: State::Gateway.into(), ..Default::default() },
        ));
        assert_eq!(
            got, want,
            "ethernet interface, address configured, interface up, with local gateway"
        );

        let got = compute_state(
            &properties,
            Some(route_table.clone()),
            &mut FakePing {
                gateway_urls: vec!["1.2.3.1", "123::1"],
                gateway_response: true,
                internet_response: true,
            },
        )
        .await;
        let want = Some(ReachabilityInfo::new(
            PortType::Ethernet,
            ETHERNET_INTERFACE_NAME,
            NetworkInfo { is_l3: true, state: State::Internet.into(), ..Default::default() },
            NetworkInfo { is_l3: true, state: State::Internet.into(), ..Default::default() },
        ));
        assert_eq!(
            got, want,
            "ethernet interface, address configured, interface up, with internet"
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
                    ETHERNET_INTERFACE_NAME,
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
                    ETHERNET_INTERFACE_NAME,
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
                ETHERNET_INTERFACE_NAME,
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
                ETHERNET_INTERFACE_NAME,
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
                ETHERNET_INTERFACE_NAME,
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
                ETHERNET_INTERFACE_NAME,
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
                ETHERNET_INTERFACE_NAME,
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
                ETHERNET_INTERFACE_NAME,
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
            "port 3 internet"
        );

        interfaces.insert(
            Id::from(3),
            ReachabilityInfo::new(
                PortType::Ethernet,
                ETHERNET_INTERFACE_NAME,
                NetworkInfo {
                    is_default: false,
                    is_l3: true,
                    state: StateEvent {
                        state: State::Removed,
                        time: Time(zx::Time::from_nanos(80000)),
                    },
                    previous_state: StateEvent {
                        state: State::Internet,
                        time: Time(zx::Time::from_nanos(70000)),
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
            "port 3 removed"
        );
    }
}

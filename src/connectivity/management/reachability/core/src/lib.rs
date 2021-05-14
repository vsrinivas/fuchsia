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
    fuchsia_async as fasync,
    fuchsia_inspect::Inspector,
    inspect::InspectInfo,
    net_types::ScopeableAddress as _,
    network_manager_core::{address::LifIpAddr, error, hal},
    std::collections::HashMap,
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

/// A trait for types containing reachability state that should be compared without the timestamp.
trait StateEq {
    /// Returns true iff `self` and `other` have equivalent reachability state.
    fn compare_state(&self, other: &Self) -> bool;
}

/// `StateEvent` records a state and the time it was reached.
// NB PartialEq is derived only for tests to avoid unintentionally making a comparison that
// includes the timestamp.
#[derive(Debug, Clone, Copy)]
#[cfg_attr(test, derive(PartialEq))]
struct StateEvent {
    /// `state` is the current reachability state.
    state: State,
    /// The time of this event.
    time: fasync::Time,
}

impl StateEvent {
    /// Overwrite `self` with `other` if the state is different, returning the previous and current
    /// values (which may be equal).
    fn update(&mut self, other: Self) -> Delta<Self> {
        let previous = Some(*self);
        if self.state != other.state {
            *self = other;
        }
        Delta { previous, current: *self }
    }
}

impl StateEq for StateEvent {
    fn compare_state(&self, &Self { state, time: _ }: &Self) -> bool {
        self.state == state
    }
}

#[derive(Clone, Debug, PartialEq)]
struct Delta<T> {
    current: T,
    previous: Option<T>,
}

impl<T: StateEq> Delta<T> {
    fn change_observed(&self) -> bool {
        match &self.previous {
            Some(previous) => !previous.compare_state(&self.current),
            None => true,
        }
    }
}

// NB PartialEq is derived only for tests to avoid unintentionally making a comparison that
// includes the timestamp in `StateEvent`.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq))]
struct StateDelta {
    port: IpVersions<Delta<StateEvent>>,
    system: IpVersions<Delta<SystemState>>,
}

#[derive(Clone, Default, Debug, PartialEq)]
struct IpVersions<T> {
    ipv4: T,
    ipv6: T,
}

impl<T> IpVersions<T> {
    fn with_version<F: FnMut(Proto, &T)>(&self, mut f: F) {
        let () = f(Proto::IPv4, &self.ipv4);
        let () = f(Proto::IPv6, &self.ipv6);
    }
}

type Id = hal::PortId;

// NB PartialEq is derived only for tests to avoid unintentionally making a comparison that
// includes the timestamp in `StateEvent`.
#[derive(Copy, Clone, Debug)]
#[cfg_attr(test, derive(PartialEq))]
struct SystemState {
    id: Id,
    state: StateEvent,
}

impl SystemState {
    fn max(self, other: Self) -> Self {
        if other.state.state > self.state.state {
            other
        } else {
            self
        }
    }
}

impl StateEq for SystemState {
    fn compare_state(&self, &Self { id, state: StateEvent { state, time: _ } }: &Self) -> bool {
        self.id == id && self.state.state == state
    }
}

/// `StateInfo` keeps the reachability state.
// NB PartialEq is derived only for tests to avoid unintentionally making a comparison that
// includes the timestamp in `StateEvent`.
#[derive(Debug, Default, Clone)]
#[cfg_attr(test, derive(PartialEq))]
struct StateInfo {
    /// Mapping from interface ID to reachability information.
    per_interface: HashMap<Id, IpVersions<StateEvent>>,
    /// Interface IDs with the best reachability state per IP version.
    system: IpVersions<Option<Id>>,
}

impl StateInfo {
    /// Get the reachability info associated with an interface.
    fn get(&self, id: Id) -> Option<&IpVersions<StateEvent>> {
        self.per_interface.get(&id)
    }

    /// Get the system-wide IPv4 reachability info.
    fn get_system_ipv4(&self) -> Option<SystemState> {
        self.system.ipv4.map(|id| SystemState {
            id,
            state: self
                .get(id)
                .expect(&format!("inconsistent system IPv4 state: no interface with ID {:?}", id))
                .ipv4,
        })
    }

    /// Get the system-wide IPv6 reachability info.
    fn get_system_ipv6(&self) -> Option<SystemState> {
        self.system.ipv6.map(|id| SystemState {
            id,
            state: self
                .get(id)
                .expect(&format!("inconsistent system IPv6 state: no interface with ID {:?}", id))
                .ipv6,
        })
    }

    /// Report the duration of the current state for each interface and each protocol.
    fn report(&self) {
        let time = fasync::Time::now();
        debug!("system reachability state IPv4 {:?}", self.get_system_ipv4());
        debug!("system reachability state IPv6 {:?}", self.get_system_ipv6());
        for (id, IpVersions { ipv4, ipv6 }) in self.per_interface.iter() {
            debug!(
                "reachability state {:?} IPv4 {:?} with duration {:?}",
                id,
                ipv4,
                time - ipv4.time
            );
            debug!(
                "reachability state {:?} IPv6 {:?} with duration {:?}",
                id,
                ipv6,
                time - ipv6.time
            );
        }
    }

    /// Update interface `id` with its new reachability info.
    ///
    /// Returns the protocols and their new reachability states iff a change was observed.
    fn update(&mut self, id: Id, new_reachability: IpVersions<StateEvent>) -> StateDelta {
        let IpVersions { ipv4: new_ipv4_state, ipv6: new_ipv6_state } = new_reachability;

        let previous_system_ipv4 = self.get_system_ipv4();
        let previous_system_ipv6 = self.get_system_ipv6();
        let port = match self.per_interface.entry(id) {
            std::collections::hash_map::Entry::Occupied(mut occupied) => {
                let IpVersions { ipv4: ipv4_state, ipv6: ipv6_state } = occupied.get_mut();
                IpVersions {
                    ipv4: ipv4_state.update(new_ipv4_state),
                    ipv6: ipv6_state.update(new_ipv6_state),
                }
            }
            std::collections::hash_map::Entry::Vacant(vacant) => {
                vacant.insert(new_reachability);
                IpVersions {
                    ipv4: Delta { previous: None, current: new_ipv4_state },
                    ipv6: Delta { previous: None, current: new_ipv6_state },
                }
            }
        };

        let IpVersions { ipv4: system_ipv4, ipv6: system_ipv6 } = self.per_interface.iter().fold(
            IpVersions {
                ipv4: SystemState { id, state: new_ipv4_state },
                ipv6: SystemState { id, state: new_ipv6_state },
            },
            |IpVersions { ipv4: system_ipv4, ipv6: system_ipv6 },
             (&id, &IpVersions { ipv4, ipv6 })| {
                IpVersions {
                    ipv4: system_ipv4.max(SystemState { id, state: ipv4 }),
                    ipv6: system_ipv6.max(SystemState { id, state: ipv6 }),
                }
            },
        );

        self.system = IpVersions { ipv4: Some(system_ipv4.id), ipv6: Some(system_ipv6.id) };

        StateDelta {
            port,
            system: IpVersions {
                ipv4: Delta { previous: previous_system_ipv4, current: system_ipv4 },
                ipv6: Delta { previous: previous_system_ipv6, current: system_ipv6 },
            },
        }
    }
}

/// `Monitor` monitors the reachability state.
pub struct Monitor {
    hal: hal::NetCfg,
    state: StateInfo,
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
            state: Default::default(),
            stats: Default::default(),
            pinger,
            inspector: None,
            system_node: None,
            nodes: HashMap::new(),
        })
    }

    /// Reports all information.
    pub fn report_state(&self) {
        self.state.report();
        debug!("reachability stats {:?}", self.stats);
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
    fn update_state(&mut self, id: Id, name: &str, reachability: IpVersions<StateEvent>) {
        let StateDelta { port, system } = self.state.update(id, reachability);

        let () = port.with_version(|proto, delta| {
            if delta.change_observed() {
                let &Delta { previous, current } = delta;
                if let Some(previous) = previous {
                    info!(
                        "interface updated {:?} {:?} current: {:?} previous: {:?}",
                        id, proto, current, previous
                    );
                } else {
                    info!("new interface {:?} {:?}: {:?}", id, proto, current);
                }
                let () = log_state(self.interface_node(id, name), proto, current.state);
                *self.stats.state_updates.entry(id).or_insert(0) += 1;
            }
        });

        let () = system.with_version(|proto, delta| {
            if delta.change_observed() {
                let &Delta { previous, current } = delta;
                if let Some(previous) = previous {
                    info!(
                        "system updated {:?} current: {:?}, previous: {:?}",
                        proto, current, previous,
                    );
                } else {
                    info!("initial system state {:?}: {:?}", proto, current);
                }
                let () = log_state(self.system_node.as_mut(), proto, current.state.state);
            }
        });
    }

    /// Compute the reachability state of an interface.
    ///
    /// The interface may have been recently-discovered, or the properties of a known interface may
    /// have changed.
    pub async fn compute_state(&mut self, properties: &fnet_interfaces_ext::Properties) {
        // TODO(https://fxbug.dev/75079) Get the route table ourselves.
        let routes = self.hal.routes().await.unwrap_or_else(|| {
            error!("failed to get route table");
            Vec::new()
        });
        if let Some(info) = compute_state(properties, &routes, &mut *self.pinger).await {
            let id = Id::from(properties.id);
            let () = self.update_state(id, &properties.name, info);
        }
    }

    /// Handle an interface removed event.
    pub fn handle_interface_removed(
        &mut self,
        fnet_interfaces_ext::Properties { id, name, .. }: fnet_interfaces_ext::Properties,
    ) {
        let time = fasync::Time::now();
        if let Some(mut reachability) = self.state.get(id.into()).cloned() {
            reachability.ipv4 = StateEvent { state: State::Removed, time };
            reachability.ipv6 = StateEvent { state: State::Removed, time };
            let () = self.update_state(id.into(), &name, reachability);
        }
    }
}

fn log_state(info: Option<&mut InspectInfo>, proto: Proto, state: State) {
    info.map(|info| {
        info.log_state(proto, state);
    });
}

/// `compute_state` processes an event and computes the reachability based on the event and
/// system observations.
async fn compute_state(
    &fnet_interfaces_ext::Properties {
        id: _,
        name: _,
        device_class,
        online,
        ref addresses,
        has_default_ipv4_route: _,
        has_default_ipv6_route: _,
    }: &fnet_interfaces_ext::Properties,
    routes: &[hal::Route],
    pinger: &mut dyn Pinger,
) -> Option<IpVersions<StateEvent>> {
    if PortType::from(device_class) == PortType::Loopback {
        return None;
    }

    let (v4_addrs, v6_addrs): (Vec<_>, _) = addresses
        .iter()
        .map(|fnet_interfaces_ext::Address { addr, valid_until: _ }| LifIpAddr::from(addr))
        .partition(|addr| addr.is_ipv4());

    if !online {
        return Some(IpVersions {
            ipv4: StateEvent { state: State::Down, time: fasync::Time::now() },
            ipv6: StateEvent { state: State::Down, time: fasync::Time::now() },
        });
    }

    // TODO(https://fxbug.dev/74517) Check if packet count has increased, and if so upgrade the state to
    // LinkLayerUp.

    // TODO(https://fxbug.dev/65581) Parallelize the following two calls for IPv4 and IPv6 respectively
    // when the ping logic is implemented in Rust and async.
    let ipv4 = StateEvent {
        state: if v4_addrs.is_empty() {
            State::Up
        } else {
            network_layer_state(v4_addrs, routes, pinger, IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS)
                .await
        },
        time: fasync::Time::now(),
    };
    let ipv6 = StateEvent {
        state: if v6_addrs.is_empty() {
            State::Up
        } else {
            network_layer_state(v6_addrs, routes, pinger, IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS)
                .await
        },
        time: fasync::Time::now(),
    };
    Some(IpVersions { ipv4, ipv6 })
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

// `network_layer_state` determines the L3 reachability state.
async fn network_layer_state(
    addresses: impl IntoIterator<Item = LifIpAddr>,
    routes: &[hal::Route],
    p: &mut dyn Pinger,
    ping_address: &str,
) -> State {
    // TODO(https://fxbug.dev/36242) Check neighbor reachability and upgrade state to Local.
    let mut gateway_reachable = false;
    'outer: for a in addresses {
        for r in local_routes(&a, routes) {
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
        return State::Local;
    }

    if !p.ping(ping_address).await {
        return State::Gateway;
    }
    return State::Internet;
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_trait::async_trait,
        fuchsia_async as fasync,
        net_declare::{fidl_subnet, std_ip},
        std::{
            net::{IpAddr, Ipv4Addr, Ipv6Addr},
            task::Poll,
        },
    };

    const ETHERNET_INTERFACE_NAME: &str = "eth1";
    const ID1: Id = crate::hal::PortId::new(1);
    const ID2: Id = crate::hal::PortId::new(2);

    // A trait for writing helper constructors.
    //
    // Note that this trait differs from `std::convert::From` only in name, but will almost always
    // contain shortcuts that would be too surprising for an actual `From` implementation.
    trait Construct<T> {
        fn construct(_: T) -> Self;
    }

    impl Construct<State> for StateEvent {
        fn construct(state: State) -> Self {
            Self { state, time: fasync::Time::INFINITE }
        }
    }

    impl Construct<StateEvent> for IpVersions<StateEvent> {
        fn construct(state: StateEvent) -> Self {
            Self { ipv4: state, ipv6: state }
        }
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
                port_id: Some(ID1),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 },
            },
        ];

        let want_route = &hal::Route {
            gateway: Some("1.2.3.1".parse().unwrap()),
            metric: None,
            port_id: Some(ID1),
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
                port_id: Some(ID1),
                target: LifIpAddr { address: "0.0.0.0".parse().unwrap(), prefix: 0 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(ID1),
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
                port_id: Some(ID1),
                target: unspecified_addr,
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: net1.parse().unwrap(), prefix },
            },
        ];
        let route_table_2 = vec![
            hal::Route {
                gateway: Some(net2_gateway.parse().unwrap()),
                metric: None,
                port_id: Some(ID1),
                target: unspecified_addr,
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: net1.parse().unwrap(), prefix },
            },
            hal::Route {
                gateway: None,
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: net2.parse().unwrap(), prefix },
            },
        ];

        assert_eq!(
            network_layer_state(
                std::iter::once(address),
                &route_table,
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
                &route_table,
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
                &route_table,
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
                std::iter::empty(),
                &route_table_2,
                &mut FakePing::default(),
                ping_url,
            )
            .await,
            State::Local,
            "No default route"
        );
    }

    #[test]
    fn test_compute_state() {
        let properties = fnet_interfaces_ext::Properties {
            id: ID1.to_u64(),
            name: ETHERNET_INTERFACE_NAME.to_string(),
            device_class: fnet_interfaces::DeviceClass::Device(
                fidl_fuchsia_hardware_network::DeviceClass::Ethernet,
            ),
            has_default_ipv4_route: true,
            has_default_ipv6_route: true,
            online: true,
            addresses: vec![
                fnet_interfaces_ext::Address {
                    addr: fidl_subnet!("1.2.3.4/24"),
                    valid_until: fuchsia_zircon::Time::INFINITE.into_nanos(),
                },
                fnet_interfaces_ext::Address {
                    addr: fidl_subnet!("123::4/64"),
                    valid_until: fuchsia_zircon::Time::INFINITE.into_nanos(),
                },
            ],
        };
        let route_table = &[
            hal::Route {
                gateway: Some(std_ip!("1.2.3.1")),
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: IpAddr::V4(Ipv4Addr::UNSPECIFIED), prefix: 0 },
            },
            hal::Route {
                gateway: Some(std_ip!("123::1")),
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: IpAddr::V6(Ipv6Addr::UNSPECIFIED), prefix: 0 },
            },
        ];
        let route_table2 = &[
            hal::Route {
                gateway: Some(std_ip!("2.2.3.1")),
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: IpAddr::V4(Ipv4Addr::UNSPECIFIED), prefix: 0 },
            },
            hal::Route {
                gateway: Some(std_ip!("223::1")),
                metric: None,
                port_id: Some(ID1),
                target: LifIpAddr { address: IpAddr::V6(Ipv6Addr::UNSPECIFIED), prefix: 0 },
            },
        ];

        const NON_ETHERNET_INTERFACE_NAME: &str = "test01";

        let mut exec = fasync::TestExecutor::new_with_fake_time()
            .expect("failed to create fake-time executor");
        let time = fasync::Time::from_nanos(1_000_000_000);
        let () = exec.set_fake_time(time.into());

        fn run_compute_state(
            exec: &mut fasync::TestExecutor,
            properties: &fnet_interfaces_ext::Properties,
            routes: &[hal::Route],
            pinger: &mut dyn Pinger,
        ) -> Result<Option<IpVersions<StateEvent>>, anyhow::Error> {
            let fut = compute_state(&properties, routes, pinger);
            futures::pin_mut!(fut);
            match exec.run_until_stalled(&mut fut) {
                Poll::Ready(got) => Ok(got),
                Poll::Pending => Err(anyhow::anyhow!("compute_state blocked unexpectedly")),
            }
        }

        let got = run_compute_state(
            &mut exec,
            &fnet_interfaces_ext::Properties {
                id: ID1.to_u64(),
                name: NON_ETHERNET_INTERFACE_NAME.to_string(),
                device_class: fnet_interfaces::DeviceClass::Device(
                    fidl_fuchsia_hardware_network::DeviceClass::Unknown,
                ),
                online: false,
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
                addresses: vec![],
            },
            &[],
            &mut FakePing::default(),
        )
        .expect(
            "error calling compute_state with non-ethernet interface, no addresses, interface down",
        );
        assert_eq!(got, Some(IpVersions::construct(StateEvent { state: State::Down, time })));

        let got = run_compute_state(
            &mut exec,
            &fnet_interfaces_ext::Properties { online: false, ..properties.clone() },
            &[],
            &mut FakePing::default(),
        )
        .expect("error calling compute_state, want Down state");
        let want =
            Some(IpVersions::<StateEvent>::construct(StateEvent { state: State::Down, time }));
        assert_eq!(got, want);

        let got = run_compute_state(
            &mut exec,
            &fnet_interfaces_ext::Properties {
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
                ..properties.clone()
            },
            &[],
            &mut FakePing::default(),
        )
        .expect("error calling compute_state, want Local state due to no default routes");
        let want =
            Some(IpVersions::<StateEvent>::construct(StateEvent { state: State::Local, time }));
        assert_eq!(got, want);

        let got = run_compute_state(&mut exec, &properties, route_table2, &mut FakePing::default())
            .expect(
                "error calling compute_state, want Local state due to no matching default route",
            );
        let want =
            Some(IpVersions::<StateEvent>::construct(StateEvent { state: State::Local, time }));
        assert_eq!(got, want);

        let got = run_compute_state(
            &mut exec,
            &properties,
            route_table,
            &mut FakePing {
                gateway_urls: vec!["1.2.3.1", "123::1"],
                gateway_response: true,
                internet_response: false,
            },
        )
        .expect("error calling compute_state, want Gateway state");
        let want =
            Some(IpVersions::<StateEvent>::construct(StateEvent { state: State::Gateway, time }));
        assert_eq!(got, want);

        let got = run_compute_state(
            &mut exec,
            &properties,
            route_table,
            &mut FakePing {
                gateway_urls: vec!["1.2.3.1", "123::1"],
                gateway_response: true,
                internet_response: true,
            },
        )
        .expect("error calling compute_state, want Internet state");
        let want =
            Some(IpVersions::<StateEvent>::construct(StateEvent { state: State::Internet, time }));
        assert_eq!(got, want);
    }

    #[test]
    fn test_state_info_update() {
        fn update_delta(port: Delta<StateEvent>, system: Delta<SystemState>) -> StateDelta {
            StateDelta {
                port: IpVersions { ipv4: port.clone(), ipv6: port },
                system: IpVersions { ipv4: system.clone(), ipv6: system },
            }
        }

        let if1_local_event = StateEvent::construct(State::Local);
        let if1_local = IpVersions::<StateEvent>::construct(if1_local_event);
        // Post-update the system state should be Local due to interface 1.
        let mut state = StateInfo::default();
        let want = update_delta(
            Delta { previous: None, current: if1_local_event },
            Delta { previous: None, current: SystemState { id: ID1, state: if1_local_event } },
        );
        assert_eq!(state.update(ID1, if1_local.clone()), want);
        let want_state = StateInfo {
            per_interface: std::iter::once((ID1, if1_local.clone())).collect::<HashMap<_, _>>(),
            system: IpVersions { ipv4: Some(ID1), ipv6: Some(ID1) },
        };
        assert_eq!(state, want_state);

        let if2_gateway_event = StateEvent::construct(State::Gateway);
        let if2_gateway = IpVersions::<StateEvent>::construct(if2_gateway_event);
        // Pre-update, the system state is Local due to interface 1; post-update the system state
        // will be Gateway due to interface 2.
        let want = update_delta(
            Delta { previous: None, current: if2_gateway_event },
            Delta {
                previous: Some(SystemState { id: ID1, state: if1_local_event }),
                current: SystemState { id: ID2, state: if2_gateway_event },
            },
        );
        assert_eq!(state.update(ID2, if2_gateway.clone()), want);
        let want_state = StateInfo {
            per_interface: [(ID1, if1_local.clone()), (ID2, if2_gateway.clone())]
                .iter()
                .cloned()
                .collect::<HashMap<_, _>>(),
            system: IpVersions { ipv4: Some(ID2), ipv6: Some(ID2) },
        };
        assert_eq!(state, want_state);

        let if2_removed_event = StateEvent::construct(State::Removed);
        let if2_removed = IpVersions::<StateEvent>::construct(if2_removed_event);
        // Pre-update, the system state is Gateway due to interface 2; post-update the system state
        // will be Local due to interface 1.
        let want = update_delta(
            Delta { previous: Some(if2_gateway_event), current: if2_removed_event },
            Delta {
                previous: Some(SystemState { id: ID2, state: if2_gateway_event }),
                current: SystemState { id: ID1, state: if1_local_event },
            },
        );
        assert_eq!(state.update(ID2, if2_removed.clone()), want);
        let want_state = StateInfo {
            per_interface: [(ID1, if1_local.clone()), (ID2, if2_removed.clone())]
                .iter()
                .cloned()
                .collect::<HashMap<_, _>>(),
            system: IpVersions { ipv4: Some(ID1), ipv6: Some(ID1) },
        };
        assert_eq!(state, want_state);
    }
}

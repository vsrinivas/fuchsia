// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod inspect;
mod ping;

use {
    crate::ping::Ping,
    anyhow::Context as _,
    fidl_fuchsia_hardware_network, fidl_fuchsia_net_ext as fnet_ext,
    fidl_fuchsia_net_interfaces as fnet_interfaces,
    fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext, fidl_fuchsia_net_stack as fnet_stack,
    fuchsia_async as fasync,
    fuchsia_inspect::Inspector,
    futures::{FutureExt as _, StreamExt as _},
    inspect::InspectInfo,
    net_declare::std_ip,
    net_types::ScopeableAddress as _,
    std::collections::hash_map::{Entry, HashMap},
};

const IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS: std::net::IpAddr = std_ip!("8.8.8.8");
const IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS: std::net::IpAddr = std_ip!("2001:4860:4860::8888");

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

impl std::str::FromStr for State {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "None" => Ok(Self::None),
            "Removed" => Ok(Self::Removed),
            "Down" => Ok(Self::Down),
            "Up" => Ok(Self::Up),
            "LinkLayerUp" => Ok(Self::LinkLayerUp),
            "NetworkLayerUp" => Ok(Self::NetworkLayerUp),
            "Local" => Ok(Self::Local),
            "Gateway" => Ok(Self::Gateway),
            "WalledGarden" => Ok(Self::WalledGarden),
            "Internet" => Ok(Self::Internet),
            _ => Err(()),
        }
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
                fidl_fuchsia_hardware_network::DeviceClass::Wlan
                | fidl_fuchsia_hardware_network::DeviceClass::WlanAp => PortType::WiFi,
                fidl_fuchsia_hardware_network::DeviceClass::Ppp
                | fidl_fuchsia_hardware_network::DeviceClass::Bridge
                | fidl_fuchsia_hardware_network::DeviceClass::Virtual => PortType::Unknown,
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

type Id = u64;

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
        log::debug!("system reachability state IPv4 {:?}", self.get_system_ipv4());
        log::debug!("system reachability state IPv6 {:?}", self.get_system_ipv6());
        for (id, IpVersions { ipv4, ipv6 }) in self.per_interface.iter() {
            log::debug!(
                "reachability state {:?} IPv4 {:?} with duration {:?}",
                id,
                ipv4,
                time - ipv4.time
            );
            log::debug!(
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
        let previous_system_ipv4 = self.get_system_ipv4();
        let previous_system_ipv6 = self.get_system_ipv6();
        let port = match self.per_interface.entry(id) {
            Entry::Occupied(mut occupied) => {
                let IpVersions { ipv4, ipv6 } = occupied.get_mut();
                let IpVersions { ipv4: new_ipv4, ipv6: new_ipv6 } = new_reachability;

                IpVersions { ipv4: ipv4.update(new_ipv4), ipv6: ipv6.update(new_ipv6) }
            }
            Entry::Vacant(vacant) => {
                let IpVersions { ipv4, ipv6 } = vacant.insert(new_reachability);
                IpVersions {
                    ipv4: Delta { previous: None, current: *ipv4 },
                    ipv6: Delta { previous: None, current: *ipv6 },
                }
            }
        };

        let IpVersions { ipv4: system_ipv4, ipv6: system_ipv6 } = self.per_interface.iter().fold(
            {
                let IpVersions {
                    ipv4: Delta { previous: _, current: ipv4 },
                    ipv6: Delta { previous: _, current: ipv6 },
                } = port;
                IpVersions {
                    ipv4: SystemState { id, state: ipv4 },
                    ipv6: SystemState { id, state: ipv6 },
                }
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
    stack: fnet_stack::StackProxy,
    interface_state: fnet_interfaces::StateProxy,
    state: StateInfo,
    stats: Stats,
    inspector: Option<&'static Inspector>,
    system_node: Option<InspectInfo>,
    nodes: HashMap<Id, InspectInfo>,
}

impl Monitor {
    /// Create the monitoring service.
    pub fn new() -> anyhow::Result<Self> {
        use fuchsia_component::client::connect_to_protocol;

        let stack = connect_to_protocol::<fnet_stack::StackMarker>()
            .context("network_manager failed to connect to netstack")?;
        let interface_state = connect_to_protocol::<fnet_interfaces::StateMarker>()
            .context("network_manager failed to connect to interface state")?;
        Ok(Monitor {
            stack,
            interface_state,
            state: Default::default(),
            stats: Default::default(),
            inspector: None,
            system_node: None,
            nodes: HashMap::new(),
        })
    }

    /// Reports all information.
    pub fn report_state(&self) {
        self.state.report();
        log::debug!("reachability stats {:?}", self.stats);
    }

    /// Returns an interface watcher client proxy.
    pub fn create_interface_watcher(&self) -> anyhow::Result<fnet_interfaces::WatcherProxy> {
        let (watcher, watcher_server) =
            fidl::endpoints::create_proxy::<fnet_interfaces::WatcherMarker>()
                .context("failed to create fuchsia.net.interfaces/Watcher proxy")?;
        let () = self
            .interface_state
            .get_watcher(
                fnet_interfaces::WatcherOptions { ..fnet_interfaces::WatcherOptions::EMPTY },
                watcher_server,
            )
            .context("failed to call fuchsia.net.interfaces/State.get_watcher")?;
        Ok(watcher)
    }

    /// Sets the inspector.
    pub fn set_inspector(&mut self, inspector: &'static Inspector) {
        self.inspector = Some(inspector);

        let system_node = InspectInfo::new(inspector.root(), "system", "");
        self.system_node = Some(system_node);
    }

    fn interface_node(&mut self, id: Id, name: &str) -> Option<&mut InspectInfo> {
        self.inspector.map(move |inspector| {
            self.nodes.entry(id).or_insert_with_key(|id| {
                InspectInfo::new(inspector.root(), &format!("{:?}", id), name)
            })
        })
    }

    /// Update state based on the new reachability info.
    fn update_state(&mut self, id: Id, name: &str, reachability: IpVersions<StateEvent>) {
        let StateDelta { port, system } = self.state.update(id, reachability);

        let () = port.with_version(|proto, delta| {
            if delta.change_observed() {
                let &Delta { previous, current } = delta;
                if let Some(previous) = previous {
                    log::info!(
                        "interface updated {:?} {:?} current: {:?} previous: {:?}",
                        id,
                        proto,
                        current,
                        previous
                    );
                } else {
                    log::info!("new interface {:?} {:?}: {:?}", id, proto, current);
                }
                let () = log_state(self.interface_node(id, name), proto, current.state);
                *self.stats.state_updates.entry(id).or_insert(0) += 1;
            }
        });

        let () = system.with_version(|proto, delta| {
            if delta.change_observed() {
                let &Delta { previous, current } = delta;
                if let Some(previous) = previous {
                    log::info!(
                        "system updated {:?} current: {:?}, previous: {:?}",
                        proto,
                        current,
                        previous,
                    );
                } else {
                    log::info!("initial system state {:?}: {:?}", proto, current);
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
        let routes = self.stack.get_forwarding_table().await.unwrap_or_else(|e| {
            log::error!("failed to get route table: {}", e);
            Vec::new()
        });
        if let Some(info) = compute_state(properties, &routes, &ping::Pinger).await {
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
    info.into_iter().for_each(|info| info.log_state(proto, state))
}

/// `compute_state` processes an event and computes the reachability based on the event and
/// system observations.
async fn compute_state(
    &fnet_interfaces_ext::Properties {
        id,
        ref name,
        device_class,
        online,
        addresses: _,
        has_default_ipv4_route: _,
        has_default_ipv6_route: _,
    }: &fnet_interfaces_ext::Properties,
    routes: &[fnet_stack::ForwardingEntry],
    pinger: &dyn Ping,
) -> Option<IpVersions<StateEvent>> {
    if PortType::from(device_class) == PortType::Loopback {
        return None;
    }

    if !online {
        return Some(IpVersions {
            ipv4: StateEvent { state: State::Down, time: fasync::Time::now() },
            ipv6: StateEvent { state: State::Down, time: fasync::Time::now() },
        });
    }

    // TODO(https://fxbug.dev/74517) Check if packet count has increased, and if so upgrade the
    // state to LinkLayerUp.

    let (ipv4, ipv6) = futures::join!(
        network_layer_state(&name, id, routes, pinger, IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS)
            .map(|state| StateEvent { state, time: fasync::Time::now() }),
        network_layer_state(&name, id, routes, pinger, IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS)
            .map(|state| StateEvent { state, time: fasync::Time::now() })
    );
    Some(IpVersions { ipv4, ipv6 })
}

// `network_layer_state` determines the L3 reachability state.
async fn network_layer_state(
    name: &str,
    interface_id: u64,
    routes: &[fnet_stack::ForwardingEntry],
    p: &dyn Ping,
    internet_ping_address: std::net::IpAddr,
) -> State {
    use std::convert::TryInto as _;

    let mut relevant_routes = routes
        .into_iter()
        .filter(|fnet_stack::ForwardingEntry { subnet: _, device_id, next_hop: _, metric: _ }| {
            *device_id == interface_id
        })
        .peekable();

    if relevant_routes.peek() == None {
        return State::Up;
    }

    // TODO(https://fxbug.dev/36242) Check neighbor reachability and upgrade state to Local.

    let gateway_reachable = relevant_routes
        .filter_map(
            move |fnet_stack::ForwardingEntry { subnet: _, device_id, next_hop, metric: _ }| {
                // TODO(https://fxbug.dev/91821): only consider default routes.
                next_hop.as_ref().and_then(|next_hop| {
                    let fnet_ext::IpAddress(next_hop) = (**next_hop).into();
                    match next_hop {
                        std::net::IpAddr::V4(v4) => {
                            Some(std::net::SocketAddr::V4(std::net::SocketAddrV4::new(v4, 0)))
                        }
                        std::net::IpAddr::V6(v6) => match (*device_id).try_into() {
                            Err(std::num::TryFromIntError { .. }) => {
                                log::error!("device id {} doesn't fit in u32", device_id);
                                None
                            }
                            Ok(device_id) => {
                                if device_id == 0
                                    && net_types::ip::Ipv6Addr::from_bytes(v6.octets()).scope()
                                        != net_types::ip::Ipv6Scope::Global
                                {
                                    None
                                } else {
                                    Some(std::net::SocketAddr::V6(std::net::SocketAddrV6::new(
                                        v6, 0, 0, device_id,
                                    )))
                                }
                            }
                        },
                    }
                })
            },
        )
        .map(|next_hop| p.ping(name, next_hop))
        .collect::<futures::stream::FuturesUnordered<_>>()
        .filter(|ok| futures::future::ready(*ok))
        .next()
        .await
        .is_some();
    if !gateway_reachable {
        return State::Local;
    };

    if !p.ping(name, std::net::SocketAddr::new(internet_ping_address, 0)).await {
        return State::Gateway;
    }
    return State::Internet;
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_trait::async_trait,
        fidl_fuchsia_net as fnet, fuchsia_async as fasync,
        net_declare::{fidl_ip, fidl_subnet, std_ip},
        std::task::Poll,
    };

    const ETHERNET_INTERFACE_NAME: &str = "eth1";
    const ID1: u64 = 1;
    const ID2: u64 = 2;

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
                fidl_fuchsia_hardware_network::DeviceClass::WlanAp
            )),
            PortType::WiFi
        );
        assert_eq!(
            PortType::from(fnet_interfaces::DeviceClass::Device(
                fidl_fuchsia_hardware_network::DeviceClass::Virtual
            )),
            PortType::Unknown
        );
    }

    #[derive(Default)]
    struct FakePing {
        gateway_addrs: std::collections::HashSet<std::net::IpAddr>,
        gateway_response: bool,
        internet_response: bool,
    }

    #[async_trait]
    impl Ping for FakePing {
        async fn ping(&self, _interface_name: &str, addr: std::net::SocketAddr) -> bool {
            let Self { gateway_addrs, gateway_response, internet_response } = self;
            let ip = addr.ip();
            if [IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS, IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS]
                .contains(&ip)
            {
                *internet_response
            } else if gateway_addrs.contains(&ip) {
                *gateway_response
            } else {
                false
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_network_layer_state_ipv4() {
        test_network_layer_state(
            fidl_ip!("1.2.3.0"),
            fidl_ip!("1.2.3.4"),
            fidl_ip!("1.2.3.1"),
            fidl_ip!("2.2.3.0"),
            fidl_ip!("2.2.3.1"),
            fidl_subnet!("0.0.0.0/0"),
            IPV4_INTERNET_CONNECTIVITY_CHECK_ADDRESS,
            24,
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_network_layer_state_ipv6() {
        test_network_layer_state(
            fidl_ip!("123::"),
            fidl_ip!("123::4"),
            fidl_ip!("123::1"),
            fidl_ip!("223::"),
            fidl_ip!("223::1"),
            fidl_subnet!("::/0"),
            IPV6_INTERNET_CONNECTIVITY_CHECK_ADDRESS,
            64,
        )
        .await
    }

    async fn test_network_layer_state(
        net1: fnet::IpAddress,
        _net1_addr: fnet::IpAddress,
        net1_gateway: fnet::IpAddress,
        net2: fnet::IpAddress,
        net2_gateway: fnet::IpAddress,
        unspecified_addr: fnet::Subnet,
        ping_internet_addr: std::net::IpAddr,
        prefix_len: u8,
    ) {
        let route_table = [
            fnet_stack::ForwardingEntry {
                subnet: unspecified_addr,
                device_id: ID1,
                next_hop: Some(Box::new(net1_gateway)),
                metric: 0,
            },
            fnet_stack::ForwardingEntry {
                subnet: fnet::Subnet { addr: net1, prefix_len },
                device_id: ID1,
                next_hop: None,
                metric: 0,
            },
        ];
        let route_table_2 = [
            fnet_stack::ForwardingEntry {
                subnet: unspecified_addr,
                device_id: ID1,
                next_hop: Some(Box::new(net2_gateway)),
                metric: 0,
            },
            fnet_stack::ForwardingEntry {
                subnet: fnet::Subnet { addr: net1, prefix_len },
                device_id: ID1,
                next_hop: None,
                metric: 0,
            },
            fnet_stack::ForwardingEntry {
                subnet: fnet::Subnet { addr: net2, prefix_len },
                device_id: ID1,
                next_hop: None,
                metric: 0,
            },
        ];
        let route_table_3 = [
            fnet_stack::ForwardingEntry {
                subnet: unspecified_addr,
                device_id: ID2,
                next_hop: Some(Box::new(net1_gateway)),
                metric: 0,
            },
            fnet_stack::ForwardingEntry {
                subnet: fnet::Subnet { addr: net1, prefix_len },
                device_id: ID2,
                next_hop: None,
                metric: 0,
            },
        ];

        let fnet_ext::IpAddress(net1_gateway) = net1_gateway.into();

        assert_eq!(
            network_layer_state(
                ETHERNET_INTERFACE_NAME,
                ID1,
                &route_table,
                &FakePing {
                    gateway_addrs: std::iter::once(net1_gateway).collect(),
                    gateway_response: true,
                    internet_response: true,
                },
                ping_internet_addr,
            )
            .await,
            State::Internet,
            "All is good. Can reach internet"
        );

        assert_eq!(
            network_layer_state(
                ETHERNET_INTERFACE_NAME,
                ID1,
                &route_table,
                &FakePing {
                    gateway_addrs: std::iter::once(net1_gateway).collect(),
                    gateway_response: true,
                    internet_response: false,
                },
                ping_internet_addr,
            )
            .await,
            State::Gateway,
            "Can reach gateway"
        );

        assert_eq!(
            network_layer_state(
                ETHERNET_INTERFACE_NAME,
                ID1,
                &route_table,
                &FakePing {
                    gateway_addrs: std::iter::once(net1_gateway).collect(),
                    gateway_response: false,
                    internet_response: false,
                },
                ping_internet_addr,
            )
            .await,
            State::Local,
            "Local only, Cannot reach gateway"
        );

        assert_eq!(
            network_layer_state(
                ETHERNET_INTERFACE_NAME,
                ID1,
                &route_table_2,
                &FakePing::default(),
                ping_internet_addr,
            )
            .await,
            State::Local,
            "No default route"
        );

        assert_eq!(
            network_layer_state(
                ETHERNET_INTERFACE_NAME,
                ID1,
                &route_table_3,
                &FakePing::default(),
                ping_internet_addr
            )
            .await,
            State::Up,
            "No routes"
        );
    }

    #[test]
    fn test_compute_state() {
        let properties = fnet_interfaces_ext::Properties {
            id: ID1,
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
        let local_routes = [fnet_stack::ForwardingEntry {
            subnet: fidl_subnet!("1.2.3.4/24"),
            device_id: ID1,
            next_hop: None,
            metric: 0,
        }];
        let route_table = [
            fnet_stack::ForwardingEntry {
                subnet: fidl_subnet!("0.0.0.0/0"),
                device_id: ID1,
                next_hop: Some(Box::new(fidl_ip!("1.2.3.1"))),
                metric: 0,
            },
            fnet_stack::ForwardingEntry {
                subnet: fidl_subnet!("::/0"),
                device_id: ID1,
                next_hop: Some(Box::new(fidl_ip!("123::1"))),
                metric: 0,
            },
        ];
        let route_table2 = [
            fnet_stack::ForwardingEntry {
                subnet: fidl_subnet!("0.0.0.0/0"),
                device_id: ID1,
                next_hop: Some(Box::new(fidl_ip!("2.2.3.1"))),
                metric: 0,
            },
            fnet_stack::ForwardingEntry {
                subnet: fidl_subnet!("::/0"),
                device_id: ID1,
                next_hop: Some(Box::new(fidl_ip!("223::1"))),
                metric: 0,
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
            routes: &[fnet_stack::ForwardingEntry],
            pinger: &dyn Ping,
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
                id: ID1,
                name: NON_ETHERNET_INTERFACE_NAME.to_string(),
                device_class: fnet_interfaces::DeviceClass::Device(
                    fidl_fuchsia_hardware_network::DeviceClass::Virtual,
                ),
                online: false,
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
                addresses: vec![],
            },
            &[],
            &FakePing::default(),
        )
        .expect(
            "error calling compute_state with non-ethernet interface, no addresses, interface down",
        );
        assert_eq!(got, Some(IpVersions::construct(StateEvent { state: State::Down, time })));

        let got = run_compute_state(
            &mut exec,
            &fnet_interfaces_ext::Properties { online: false, ..properties.clone() },
            &[],
            &FakePing::default(),
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
            &local_routes,
            &FakePing::default(),
        )
        .expect("error calling compute_state, want Local state due to no default routes");
        let want =
            Some(IpVersions::<StateEvent>::construct(StateEvent { state: State::Local, time }));
        assert_eq!(got, want);

        let got = run_compute_state(&mut exec, &properties, &route_table2, &FakePing::default())
            .expect(
                "error calling compute_state, want Local state due to no matching default route",
            );
        let want =
            Some(IpVersions::<StateEvent>::construct(StateEvent { state: State::Local, time }));
        assert_eq!(got, want);

        let got = run_compute_state(
            &mut exec,
            &properties,
            &route_table,
            &mut FakePing {
                gateway_addrs: [std_ip!("1.2.3.1"), std_ip!("123::1")].iter().cloned().collect(),
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
            &route_table,
            &mut FakePing {
                gateway_addrs: [std_ip!("1.2.3.1"), std_ip!("123::1")].iter().cloned().collect(),
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

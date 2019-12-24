// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ping;

#[macro_use]
extern crate log;
use anyhow::Error;
use fidl_fuchsia_net_stack as stack;
use fidl_fuchsia_netstack as netstack;
use fuchsia_zircon as zx;
use network_manager_core::error;
use network_manager_core::hal;
use network_manager_core::lifmgr::LifIpAddr;
pub use ping::{IcmpPinger, Pinger};
use std::collections::HashMap;

const INTERNET_CONNECTIVITY_CHECK_ADDRESS: &str = "8.8.8.8";
const PROBE_PERIOD_IN_SEC: i64 = 60;

/// `Stats` keeps the monitoring service statistic counters.
#[derive(Debug, Default, Clone, Copy)]
pub struct Stats {
    /// `events` is the number of events received.
    pub events: u64,
    // TODO(dpradilla): consider keeping this stat per interface or even per network.
    /// `state_updates` is the number of times reachability state has changed.
    pub state_updates: u64,
}

// TODO(dpradilla): consider splitting the state in l2 state and l3 state, as there can be multiple
// L3 networks on the same physical medium.
/// `State` represents the reachability state.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum State {
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

/// `PortType` is the type of port backing the L3 interface.
#[derive(Debug, PartialEq)]
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

/// `NetworkInfo` keeps information about an network.
#[derive(Debug, PartialEq)]
struct NetworkInfo {
    /// `is_default` indicates the default route is via this network.
    is_default: bool,
    /// `is_l3` indicates L3 is configured.
    is_l3: bool,
    /// `state` is the current reachability state.
    state: State,
}

/// `ReachabilityInfo` is the information about an interface.
#[derive(Debug, PartialEq)]
pub struct ReachabilityInfo {
    /// `port_type` is the type of port.
    port_type: PortType,
    /// IPv4 reachability information.
    v4: NetworkInfo,
    /// IPv6 reachability information.
    v6: NetworkInfo,
}

type Id = hal::PortId;
type StateInfo = HashMap<Id, ReachabilityInfo>;

/// Trait to set a periodic timer that calls `Monitor::timer_event`
/// every `duration`.
pub trait Timer {
    fn periodic(&self, duration: zx::Duration, id: u64) -> i64;
}

/// `Monitor` monitors the reachability state.
pub struct Monitor {
    hal: hal::NetCfg,
    state_info: StateInfo,
    stats: Stats,
    pinger: Box<dyn Pinger>,
    timer: Option<Box<dyn Timer>>,
}

impl Monitor {
    /// Create the monitoring service.
    pub fn new(pinger: Box<dyn Pinger>) -> Result<Self, Error> {
        let hal = hal::NetCfg::new()?;
        Ok(Monitor {
            hal,
            state_info: HashMap::new(),
            stats: Default::default(),
            pinger,
            timer: None,
        })
    }

    /// `stats` returns monitoring service statistic counters.
    pub fn stats(&self) -> &Stats {
        &self.stats
    }
    /// `state` returns reachability state for all interfaces known to the monitoring service.
    pub fn state(&self) -> &StateInfo {
        &self.state_info
    }

    fn dump_state(&self) {
        for (key, value) in &self.state_info {
            debug!("{:?}: {:?}", key, value);
        }
    }

    fn report(&self, id: Id, info: &ReachabilityInfo) {
        warn!("State Change {:?}: {:?}", id, info);
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
    }

    /// `update_state` processes an event and updates the reachability state accordingly.
    async fn update_state(&mut self, interface_info: &hal::Interface) {
        let port_type = port_type(interface_info);
        if port_type == PortType::Loopback {
            return;
        }

        debug!("update_state ->  interface_info: {:?}", interface_info);
        let routes = self.hal.routes().await;
        if let Some(new_info) = compute_state(interface_info, routes, &*self.pinger).await {
            if let Some(info) = self.state_info.get(&interface_info.id) {
                if info == &new_info {
                    // State has not changed, nothing to do.
                    debug!("update_state ->  no change");
                    return;
                }
            } else {
                debug!("update_state ->  new interface");
                if let Some(timer) = &self.timer {
                    debug!("update_state ->  setting timer");
                    timer.periodic(
                        zx::Duration::from_seconds(PROBE_PERIOD_IN_SEC),
                        interface_info.id.to_u64(),
                    );
                }
            }

            self.report(interface_info.id, &new_info);
            self.stats.state_updates += 1;
            debug!("update_state ->  new state {:?}", new_info);
            self.state_info.insert(interface_info.id, new_info);
        };
    }

    pub async fn timer_event(&mut self, id: u64) -> error::Result<()> {
        if let Some(info) = self.hal.get_interface(id).await {
            debug!("timer_event {} info {:?}", id, info);
            self.update_state(&info).await;
        }
        self.dump_state();
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
                // just for completeness and to be ready then it gets hooked up.
                if let Some(current_info) = self.hal.get_interface(info.id).await {
                    self.update_state(&current_info).await;
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
                        self.update_state(&current_info).await;
                    }
                }
            }
        }
        self.dump_state();
        Ok(())
    }

    /// `populate_state` queries the networks stack to determine current state.
    pub async fn populate_state(&mut self) -> error::Result<()> {
        for info in self.hal.interfaces().await?.iter() {
            self.update_state(info).await;
        }
        self.dump_state();
        Ok(())
    }
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

    let ipv4_address = interface_info.get_address();

    let mut new_info = ReachabilityInfo {
        port_type,
        v4: NetworkInfo {
            is_default: false,
            is_l3: interface_info.dhcp_client_enabled || ipv4_address.is_some(),
            state: State::Down,
        },
        v6: NetworkInfo {
            is_default: false,
            is_l3: !interface_info.ipv6_addr.is_empty(),
            state: State::Down,
        },
    };

    let is_up = interface_info.state != hal::InterfaceState::Down;
    if !is_up {
        return Some(new_info);
    }

    new_info.v4.state = State::Up;
    new_info.v6.state = State::Up;

    // packet reception is network layer independent.
    if !packet_count_increases(interface_info.id) {
        // TODO(dpradilla): add active probing here.
        // No packets seen, but interface is up.
        return Some(new_info);
    }

    new_info.v4.state = State::LinkLayerUp;
    new_info.v6.state = State::LinkLayerUp;

    new_info.v4.state =
        network_layer_state(ipv4_address.into_iter(), &routes, &new_info.v4, &*pinger).await;

    // TODO(dpradilla): Add support for IPV6

    Some(new_info)
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
    if interface_info.name.contains("wlan") {
        PortType::WiFi
    } else if interface_info.name.contains("ethernet") {
        PortType::Ethernet
    } else if interface_info.name.contains("loopback") {
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
) -> State {
    // This interface is not configured for L3, Nothing to check.
    if !info.is_l3 {
        return info.state;
    }

    if info.state != State::LinkLayerUp || !has_local_neighbors() {
        return info.state;
    }

    // TODO(dpradilla): add support for multiple addresses.
    let address = addresses.next();
    if address.is_none() {
        return info.state;
    }

    let route_table = match routes {
        Some(r) => r,
        _ => return State::Local,
    };

    // Has local gateway.
    let rs = local_routes(&address.unwrap(), &route_table);
    if rs.is_empty() {
        return State::Local;
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
        return State::Local;
    }

    if !p.ping(INTERNET_CONNECTIVITY_CHECK_ADDRESS) {
        return State::Gateway;
    }
    State::Internet
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use ping::IcmpPinger;

    #[test]
    fn test_has_local_neighbors() {
        assert_eq!(has_local_neighbors(), true);
    }

    #[test]
    fn test_packet_count_increases() {
        assert_eq!(packet_count_increases(hal::PortId::from(1)), true);
    }

    #[test]
    fn test_port_type() {
        assert_eq!(
            port_type(&hal::Interface {
                ipv4_addr: None,
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Unknown,
                enabled: true,
                name: "loopback".to_string(),
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
                name: "ethernet/eth0".to_string(),
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
                name: "ethernet/wlan".to_string(),
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
                name: "br0".to_string(),
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
                &NetworkInfo { is_default: false, is_l3: true, state: State::LinkLayerUp },
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
                &NetworkInfo { is_default: false, is_l3: true, state: State::LinkLayerUp },
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
                &NetworkInfo { is_default: false, is_l3: true, state: State::LinkLayerUp },
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
                &NetworkInfo { is_default: false, is_l3: true, state: State::LinkLayerUp },
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
                &NetworkInfo { is_default: false, is_l3: true, state: State::NetworkLayerUp },
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
                name: "ifname".to_string(),
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
            Some(ReachabilityInfo {
                port_type: PortType::SVI,
                v4: NetworkInfo { is_default: false, is_l3: false, state: State::Down },
                v6: NetworkInfo { is_default: false, is_l3: false, state: State::Down }
            }),
            "not an ethernet interface"
        );

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                name: "ethernet/eth0".to_string(),
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
        let want = Some(ReachabilityInfo {
            port_type: PortType::Ethernet,
            v4: NetworkInfo { is_default: false, is_l3: true, state: State::Down },
            v6: NetworkInfo { is_default: false, is_l3: false, state: State::Down },
        });
        assert_eq!(got, want, "ethernet interface, ipv4 configured, interface down");

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                name: "ethernet/eth0".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Unknown,
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
        let want = Some(ReachabilityInfo {
            port_type: PortType::Ethernet,
            v4: NetworkInfo { is_default: false, is_l3: true, state: State::Local },
            v6: NetworkInfo { is_default: false, is_l3: false, state: State::LinkLayerUp },
        });
        assert_eq!(got, want, "ethernet interface, ipv4 configured, interface up");

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                name: "ethernet/eth0".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Unknown,
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
        let want = Some(ReachabilityInfo {
            port_type: PortType::Ethernet,
            v4: NetworkInfo { is_default: false, is_l3: true, state: State::Local },
            v6: NetworkInfo { is_default: false, is_l3: false, state: State::LinkLayerUp },
        });
        assert_eq!(
            got, want,
            "ethernet interface, ipv4 configured, interface up, no local gateway"
        );

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                name: "ethernet/eth0".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Unknown,
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
        let want = Some(ReachabilityInfo {
            port_type: PortType::Ethernet,
            v4: NetworkInfo { is_default: false, is_l3: true, state: State::Gateway },
            v6: NetworkInfo { is_default: false, is_l3: false, state: State::LinkLayerUp },
        });
        assert_eq!(
            got, want,
            "ethernet interface, ipv4 configured, interface up, with local gateway"
        );

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                name: "ethernet/eth0".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Unknown,
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
        let want = Some(ReachabilityInfo {
            port_type: PortType::Ethernet,
            v4: NetworkInfo { is_default: false, is_l3: true, state: State::Internet },
            v6: NetworkInfo { is_default: false, is_l3: false, state: State::LinkLayerUp },
        });
        assert_eq!(got, want, "ethernet interface, ipv4 configured, interface up, with internet");

        let got = compute_state(
            &hal::Interface {
                id: hal::PortId::from(1),
                name: "ethernet/eth0".to_string(),
                ipv4_addr: Some(hal::InterfaceAddress::Unknown(LifIpAddr {
                    address: "1.2.3.4".parse().unwrap(),
                    prefix: 24,
                })),
                ipv6_addr: Vec::new(),
                state: hal::InterfaceState::Unknown,
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
        let want = Some(ReachabilityInfo {
            port_type: PortType::Ethernet,
            v4: NetworkInfo { is_default: false, is_l3: true, state: State::Local },
            v6: NetworkInfo { is_default: false, is_l3: false, state: State::LinkLayerUp },
        });
        assert_eq!(
            got, want,
            "ethernet interface, ipv4 configured, interface up, no local gateway"
        );
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple port manager.

use crate::error;
use crate::lifmgr::{self, subnet_mask_to_prefix_length, to_ip_addr, LIFProperties, LifIpAddr};
use crate::oir;
use crate::DnsPolicy;
use anyhow::{Context as _, Error};
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_stack::{
    self as stack, ForwardingDestination, ForwardingEntry, InterfaceInfo, StackMarker, StackProxy,
};
use fidl_fuchsia_net_stack_ext::FidlReturn;
use fidl_fuchsia_netstack::{
    self as netstack, NetstackMarker, NetstackProxy, ResolverAdminMarker, ResolverAdminProxy,
};
use fuchsia_component::client::connect_to_service;
use std::convert::TryFrom;
use std::net::IpAddr;

/// The port ID's used by the netstack.
///
/// This is what is passed in the stack FIDL to denote the port or nic id.
#[derive(Eq, PartialEq, Hash, Debug, Copy, Clone)]
pub struct StackPortId(u64);
impl From<u64> for StackPortId {
    fn from(p: u64) -> StackPortId {
        StackPortId(p)
    }
}
impl From<PortId> for StackPortId {
    fn from(p: PortId) -> StackPortId {
        // TODO(dpradilla): This should be based on the mapping between physical location and
        // logical (from management plane point of view) port id.
        StackPortId::from(p.to_u64())
    }
}
impl StackPortId {
    /// Performs the conversion to `u64`, some FIDL interfaces need the ID as a `u64`.
    pub fn to_u64(self) -> u64 {
        self.0
    }
    /// Performs the conversion to `u32`, some FIDL interfaces need the ID as a `u32`.
    pub fn to_u32(self) -> u32 {
        match u32::try_from(self.0) {
            Ok(v) => v,
            e => {
                warn!("overflow converting StackPortId {:?}: {:?}", self.0, e);
                self.0 as u32
            }
        }
    }
}

#[derive(Eq, PartialEq, Hash, Debug, Copy, Clone)]
pub struct PortId(u64);
impl From<u64> for PortId {
    fn from(p: u64) -> PortId {
        PortId(p)
    }
}
impl From<StackPortId> for PortId {
    // TODO(dpradilla): This should be based on the mapping between physical location and
    // logical (from management plane point of view) port id.
    fn from(p: StackPortId) -> PortId {
        PortId(p.to_u64())
    }
}
impl PortId {
    /// Performs the conversion to `u64`, some FIDL interfaces need the ID as a `u64`.
    pub fn to_u64(self) -> u64 {
        self.0
    }

    /// Performs the conversion to `u32`, some FIDL interfaces need the ID as a `u32`.
    pub fn to_u32(self) -> u32 {
        match u32::try_from(self.0) {
            Ok(v) => v,
            e => {
                warn!("overflow converting StackPortId {:?}: {:?}", self.0, e);
                self.0 as u32
            }
        }
    }
}

/// Route table entry.
#[derive(PartialEq, Debug, Clone)]
pub struct Route {
    /// Target network address.
    pub target: LifIpAddr,
    /// Next hop to reach `target`.
    pub gateway: Option<IpAddr>,
    /// Port to reach `gateway`.
    pub port_id: Option<PortId>,
    /// Represents the route priority.
    pub metric: Option<u32>,
}

impl From<&ForwardingEntry> for Route {
    fn from(r: &ForwardingEntry) -> Self {
        let (gateway, port_id) = match r.destination {
            ForwardingDestination::DeviceId(id) => (None, Some(PortId::from(id))),
            ForwardingDestination::NextHop(gateway) => (Some(to_ip_addr(gateway)), None),
        };
        Route {
            target: LifIpAddr { address: to_ip_addr(r.subnet.addr), prefix: r.subnet.prefix_len },
            gateway,
            port_id,
            metric: None,
        }
    }
}

pub struct NetCfg {
    stack: StackProxy,
    netstack: NetstackProxy,
    resolver_admin: ResolverAdminProxy,
}

#[derive(Debug)]
pub struct Port {
    pub id: PortId,
    pub path: String,
}

#[derive(Debug, Eq, PartialEq)]
pub enum InterfaceAddress {
    Unknown(LifIpAddr),
    Static(LifIpAddr),
    Dhcp(LifIpAddr),
}

#[derive(Debug, Eq, PartialEq)]
pub enum InterfaceState {
    Unknown,
    Up,
    Down,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Interface {
    pub id: PortId,
    pub name: String,
    pub ipv4_addr: Option<InterfaceAddress>,
    pub ipv6_addr: Vec<LifIpAddr>,
    pub enabled: bool,
    pub state: InterfaceState,
    pub dhcp_client_enabled: bool,
}

impl Interface {
    pub fn get_address(&self) -> Option<LifIpAddr> {
        self.ipv4_addr.as_ref().map(|a| match a {
            InterfaceAddress::Unknown(a) => a.clone(),
            InterfaceAddress::Static(a) => a.clone(),
            InterfaceAddress::Dhcp(a) => a.clone(),
        })
    }
}

fn address_is_valid_unicast(addr: &IpAddr) -> bool {
    // TODO(guzt): This should also check for special-purpose addresses as defined by rfc6890.
    !addr.is_loopback() && !addr.is_unspecified() && !addr.is_multicast()
}

impl From<&InterfaceInfo> for Interface {
    fn from(iface: &InterfaceInfo) -> Self {
        Interface {
            id: iface.id.into(),
            name: iface.properties.topopath.clone(),
            ipv4_addr: iface
                .properties
                .addresses
                .iter()
                .find(|&addr| match addr {
                    // Only return interfaces with an IPv4 address
                    // TODO(dpradilla) support interfaces with multiple IPs? (is there
                    // a use case given this context?)
                    stack::InterfaceAddress {
                        ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr }),
                        ..
                    } => address_is_valid_unicast(&IpAddr::from(*addr)),
                    _ => false,
                })
                .map(|addr| InterfaceAddress::Unknown(addr.into())),
            ipv6_addr: iface
                .properties
                .addresses
                .iter()
                .filter_map(|addr| match addr {
                    // Only return Ipv6 addresses
                    stack::InterfaceAddress {
                        ip_address: net::IpAddress::Ipv6(net::Ipv6Address { addr }),
                        ..
                    } => Some(LifIpAddr { address: addr.clone().into(), prefix: 128 }),
                    _ => None,
                })
                .collect(),

            enabled: match iface.properties.administrative_status {
                stack::AdministrativeStatus::Enabled => true,
                stack::AdministrativeStatus::Disabled => false,
            },
            state: match iface.properties.physical_status {
                stack::PhysicalStatus::Up => InterfaceState::Up,
                stack::PhysicalStatus::Down => InterfaceState::Down,
            },
            dhcp_client_enabled: false,
        }
    }
}

fn valid_unicast_address_or_none(addr: LifIpAddr) -> Option<LifIpAddr> {
    if address_is_valid_unicast(&addr.address) {
        Some(addr)
    } else {
        None
    }
}

impl From<netstack::NetInterface> for Interface {
    fn from(iface: netstack::NetInterface) -> Self {
        let dhcp = iface.flags & netstack::NET_INTERFACE_FLAG_DHCP != 0;
        let addr = valid_unicast_address_or_none(LifIpAddr {
            address: to_ip_addr(iface.addr),
            prefix: subnet_mask_to_prefix_length(iface.netmask),
        });
        Interface {
            id: PortId(iface.id.into()),
            name: iface.name,
            ipv4_addr: addr.map(|a| {
                if dhcp {
                    InterfaceAddress::Dhcp(a)
                } else {
                    InterfaceAddress::Static(a)
                }
            }),
            ipv6_addr: Vec::new(),
            enabled: (iface.flags & netstack::NET_INTERFACE_FLAG_UP) != 0,
            state: InterfaceState::Unknown,
            dhcp_client_enabled: dhcp,
        }
    }
}

impl Into<LIFProperties> for Interface {
    fn into(self) -> LIFProperties {
        LIFProperties {
            dhcp: self.dhcp_client_enabled,
            address: self.get_address(),
            enabled: self.enabled,
        }
    }
}

impl NetCfg {
    pub fn new() -> Result<Self, Error> {
        let stack = connect_to_service::<StackMarker>()
            .context("network_manager failed to connect to netstack")?;
        let netstack = connect_to_service::<NetstackMarker>()
            .context("network_manager failed to connect to netstack")?;
        let resolver_admin = connect_to_service::<ResolverAdminMarker>()
            .context("network_manager failed to connect to resolver admin")?;
        Ok(NetCfg { stack, netstack, resolver_admin })
    }

    /// Returns event streams for fuchsia.net.stack and fuchsia.netstack.
    pub fn take_event_streams(
        &mut self,
    ) -> (stack::StackEventStream, netstack::NetstackEventStream) {
        (self.stack.take_event_stream(), self.netstack.take_event_stream())
    }

    /// Returns the interface associated with the specified port.
    pub async fn get_interface(&mut self, port: u64) -> Option<Interface> {
        match self.stack.get_interface_info(port).await {
            Ok(Ok(info)) => Some((&info).into()),
            _ => None,
        }
    }

    /// Returns all physical ports in the system.
    pub async fn ports(&self) -> error::Result<Vec<Port>> {
        let ports = self.stack.list_interfaces().await.map_err(|_| error::Hal::OperationFailed)?;
        let p = ports
            .into_iter()
            .filter(|x| x.properties.topopath != "")
            .map(|x| Port { id: StackPortId::from(x.id).into(), path: x.properties.topopath })
            .collect::<Vec<Port>>();
        Ok(p)
    }

    /// Returns all L3 interfaces with valid, non-local IPs in the system.
    pub async fn interfaces(&mut self) -> error::Result<Vec<Interface>> {
        let ifs = self
            .stack
            .list_interfaces()
            .await
            .map_err(|_| error::Hal::OperationFailed)?
            .iter()
            .map(|i| i.into())
            .filter(|i: &Interface| i.ipv4_addr.is_some())
            .collect();
        Ok(ifs)
    }

    /// Creates a new interface, bridging the given ports.
    pub async fn create_bridge(&mut self, ports: Vec<PortId>) -> error::Result<Interface> {
        let br = self
            .netstack
            .bridge_interfaces(&mut ports.into_iter().map(|id| StackPortId::from(id).to_u32()))
            .await
            .map_err(|e| {
                error!("Failed creating bridge {:?}", e);
                error::NetworkManager::HAL(error::Hal::OperationFailed)
            })?;
        if br.0.status != fidl_fuchsia_netstack::Status::Ok {
            error!("Failed creating bridge {:?}", br.0);
            return Err(error::NetworkManager::HAL(error::Hal::OperationFailed));
        }
        info!("bridge created {:?}", br.1);
        if let Some(i) = self.get_interface(br.1.into()).await {
            Ok(i)
        } else {
            Err(error::NetworkManager::HAL(error::Hal::BridgeNotFound))
        }
    }

    /// Deletes the given bridge.
    pub async fn delete_bridge(&mut self, id: PortId) -> error::Result<()> {
        // TODO(dpradilla): what is the API for deleting a bridge? Call it
        info!("delete_bridge {:?} - Noop for now", id);
        Ok(())
    }

    /// Configures an IP address.
    pub async fn set_ip_address<'a>(
        &'a mut self,
        pid: PortId,
        addr: &'a LifIpAddr,
    ) -> error::Result<()> {
        let r = self
            .stack
            .add_interface_address(
                StackPortId::from(pid).to_u64(),
                &mut addr.to_fidl_interface_address(),
            )
            .await;
        r.squash_result().map_err(|_| error::NetworkManager::HAL(error::Hal::OperationFailed))
    }

    /// Removes an IP address.
    pub async fn unset_ip_address<'a>(
        &'a mut self,
        pid: PortId,
        addr: &'a LifIpAddr,
    ) -> error::Result<()> {
        let a = addr.to_fidl_address_and_prefix();
        // TODO(dpradilla): this needs to be changed to use the stack fidl once
        // this functionality is moved there. the u32 conversion won't be needed.
        let r = self
            .netstack
            .remove_interface_address(
                pid.to_u32(),
                &mut a.address.unwrap(),
                a.prefix_length.unwrap(),
            )
            .await;
        r.map_err(|_| error::NetworkManager::HAL(error::Hal::OperationFailed))?;

        Ok(())
    }

    /// Sets the state of an interface.
    ///
    /// `state` controls whether the given `PortId` is enabled or disabled.
    pub async fn set_interface_state(&mut self, pid: PortId, state: bool) -> error::Result<()> {
        let r = if state {
            self.stack.enable_interface(StackPortId::from(pid).to_u64())
        } else {
            self.stack.disable_interface(StackPortId::from(pid).to_u64())
        }
        .await;

        r.squash_result()
            .with_context(|| format!("failed setting interface state"))
            .map_err(|_| error::NetworkManager::HAL(error::Hal::OperationFailed))
    }

    /// Sets the state of the DHCP client on the specified interface.
    ///
    /// `enable` controls whether the given `PortId` has a DHCP client started or stopped.
    pub async fn set_dhcp_client_state(&mut self, pid: PortId, enable: bool) -> error::Result<()> {
        let (dhcp_client, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_dhcp::ClientMarker>()
                .context("dhcp client: failed to create fidl endpoints")?;
        if let Err(e) = self.netstack.get_dhcp_client(pid.to_u32(), server_end).await {
            warn!("failed to create fidl endpoint for dhch client: {:?}", e);
            return Err(error::NetworkManager::HAL(error::Hal::OperationFailed));
        }
        let r = if enable {
            dhcp_client.start().await.context("failed to start dhcp client")?
        } else {
            dhcp_client.stop().await.context("failed to stop dhcp client")?
        };
        if let Err(e) = r {
            warn!("failed to start dhcp client: {:?}", e);
            return Err(error::NetworkManager::HAL(error::Hal::OperationFailed));
        }
        info!("DHCP client on nicid: {}, enabled: {}", pid.to_u32(), enable);
        Ok(())
    }

    /// Sets the state of IP forwarding.
    ///
    /// `enabled` controls whether IP forwarding is enabled or disabled.
    pub async fn set_ip_forwarding(&self, enabled: bool) -> error::Result<()> {
        let r = if enabled {
            self.stack.enable_ip_forwarding()
        } else {
            self.stack.disable_ip_forwarding()
        }
        .await;
        r.map_err(|_| error::NetworkManager::HAL(error::Hal::OperationFailed))
    }

    /// Updates a configured IP address.
    async fn apply_manual_ip<'a>(
        &'a mut self,
        pid: PortId,
        current: &'a Option<LifIpAddr>,
        desired: &'a Option<LifIpAddr>,
    ) -> error::Result<()> {
        match (current, desired) {
            (Some(current_ip), Some(desired_ip)) => {
                if current_ip != desired_ip {
                    // There has been a change.
                    // Remove the old one and add the new one.
                    self.unset_ip_address(pid, &current_ip).await?;
                    self.set_ip_address(pid, &desired_ip).await?;
                }
            }
            (None, Some(desired_ip)) => {
                self.set_ip_address(pid, &desired_ip).await?;
            }
            (Some(current_ip), None) => {
                self.unset_ip_address(pid, &current_ip).await?;
            }
            // Nothing to do.
            (None, None) => {}
        };
        Ok(())
    }

    /// Applies the given LIF properties.
    pub async fn apply_properties<'a>(
        &'a mut self,
        pid: PortId,
        old: &'a lifmgr::LIFProperties,
        properties: &'a lifmgr::LIFProperties,
    ) -> error::Result<()> {
        match (old.dhcp, properties.dhcp) {
            // dhcp configuration transitions from enabled to disabled.
            (true, false) => {
                // Disable dhcp and apply manual address configuration.
                self.set_dhcp_client_state(pid, properties.dhcp).await?;
                self.apply_manual_ip(pid, &old.address, &properties.address).await?;
            }
            // dhcp configuration transitions from disabled to enabled.
            (false, true) => {
                // Remove any manual IP address and enable dhcp client.
                self.apply_manual_ip(pid, &old.address, &None).await?;
                self.set_dhcp_client_state(pid, properties.dhcp).await?;
            }
            // dhcp is still disabled, check for manual IP address changes.
            (false, false) => {
                self.apply_manual_ip(pid, &old.address, &properties.address).await?;
            }
            // No changes to dhcp configuration, it is still enabled, nothing to do.
            (true, true) => {}
        }
        if old.enabled != properties.enabled {
            info!("id {:?} updating state {:?}", pid, properties.enabled);
            self.set_interface_state(pid, properties.enabled).await?;
        }
        Ok(())
    }

    /// Returns the running routing table (as seen by the network stack).
    pub async fn routes(&mut self) -> Option<Vec<Route>> {
        let table = self.stack.get_forwarding_table().await;
        match table {
            Ok(entries) => Some(
                entries
                    .iter()
                    .map(Route::from)
                    .filter(|r| !r.target.address.is_loopback())
                    .collect(),
            ),
            _ => {
                info!("no entries present in forwarding table.");
                None
            }
        }
    }

    /// Sets the DNS resolver.
    pub async fn set_dns_resolver(
        &mut self,
        servers: &mut [fidl_fuchsia_net::IpAddress],
        _domains: Option<String>,
        _policy: DnsPolicy,
    ) -> error::Result<()> {
        self.resolver_admin
            .set_name_servers(&mut servers.iter_mut())
            .with_context(|| format!("failed setting interface state"))
            .map_err(|e| {
                error!("set_dns_resolver error {:?}", e);
                error::NetworkManager::HAL(error::Hal::OperationFailed)
            })
    }

    /// Adds an ethernet device to netstack.
    pub async fn add_ethernet_device(
        &self,
        channel: fuchsia_zircon::Channel,
        port: oir::PortDevice,
    ) -> error::Result<PortId> {
        info!("Adding port: {:#?}", port);
        let nic_id = self
            .netstack
            .add_ethernet_device(
                &port.topological_path,
                &mut fidl_fuchsia_netstack::InterfaceConfig {
                    name: port.name,
                    metric: port.metric,
                    filepath: port.file_path,
                    ip_address_config: fidl_fuchsia_netstack::IpAddressConfig::Dhcp(false),
                },
                fidl::endpoints::ClientEnd::<fidl_fuchsia_hardware_ethernet::DeviceMarker>::new(
                    channel,
                ),
            )
            .await
            .map_err(|_| error::NetworkManager::HAL(error::Hal::OperationFailed))?;
        info!("added port with id {:?}", nic_id);

        Ok(StackPortId::from(u64::from(nic_id)).into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn interface_info_with_addrs(addrs: Vec<stack::InterfaceAddress>) -> InterfaceInfo {
        InterfaceInfo {
            id: 42,
            properties: stack::InterfaceProperties {
                topopath: "test/interface/info".to_string(),
                addresses: addrs,
                administrative_status: stack::AdministrativeStatus::Enabled,
                name: "ethtest".to_string(),
                filepath: "/some/file".to_string(),
                mac: None,
                mtu: 0,
                features: 0,
                physical_status: stack::PhysicalStatus::Up,
            },
        }
    }

    fn interface_with_addr(addr: Option<LifIpAddr>) -> Interface {
        Interface {
            id: 42.into(),
            name: "test/interface/info".to_string(),
            ipv4_addr: addr.map(|a| InterfaceAddress::Unknown(a)),
            ipv6_addr: Vec::new(),
            enabled: true,
            state: InterfaceState::Up,
            dhcp_client_enabled: false,
        }
    }

    fn sample_addresses() -> Vec<stack::InterfaceAddress> {
        vec![
            // Unspecified addresses are skipped.
            stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr: [0, 0, 0, 0] }),
                prefix_len: 24,
            },
            // Multicast addresses are skipped.
            stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr: [224, 0, 0, 5] }),
                prefix_len: 24,
            },
            // Loopback addresses are skipped.
            stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr: [127, 0, 0, 1] }),
                prefix_len: 24,
            },
            // IPv6 addresses are skipped.
            stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                }),
                prefix_len: 8,
            },
            // First valid address, should be picked.
            stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr: [4, 3, 2, 1] }),
                prefix_len: 24,
            },
            // A valid address is already available, so this address should be skipped.
            stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                prefix_len: 24,
            },
        ]
    }

    #[test]
    fn test_net_interface_info_into_hal_interface() {
        let info = interface_info_with_addrs(sample_addresses());
        let iface: Interface = (&info).into();
        assert_eq!(iface.name, "test/interface/info");
        assert_eq!(iface.enabled, true);
        assert_eq!(
            iface.get_address(),
            Some(LifIpAddr { address: IpAddr::from([4, 3, 2, 1]), prefix: 24 })
        );
        assert_eq!(iface.id.to_u64(), 42);
    }

    async fn handle_list_interfaces(request: stack::StackRequest) {
        match request {
            stack::StackRequest::ListInterfaces { responder } => {
                responder
                    .send(
                        &mut sample_addresses()
                            .into_iter()
                            .map(|addr| interface_info_with_addrs(vec![addr]))
                            .collect::<Vec<InterfaceInfo>>()
                            .iter_mut(),
                    )
                    .unwrap();
            }
            _ => {
                panic!("unexpected stack request: {:?}", request);
            }
        }
    }

    async fn handle_with_panic<Request: std::fmt::Debug>(request: Request) {
        panic!("unexpected request: {:?}", request);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_ignore_interface_without_ip() {
        let stack: StackProxy =
            fidl::endpoints::spawn_stream_handler(handle_list_interfaces).unwrap();
        let netstack: NetstackProxy =
            fidl::endpoints::spawn_stream_handler(handle_with_panic).unwrap();
        let resolver_admin: ResolverAdminProxy =
            fidl::endpoints::spawn_stream_handler(handle_with_panic).unwrap();
        let mut netcfg = NetCfg { stack, netstack, resolver_admin };
        assert_eq!(
            netcfg.interfaces().await.unwrap(),
            // Should return only interfaces with a valid address.
            vec![
                interface_with_addr(Some(LifIpAddr {
                    address: IpAddr::from([4, 3, 2, 1]),
                    prefix: 24
                })),
                interface_with_addr(Some(LifIpAddr {
                    address: IpAddr::from([1, 2, 3, 4]),
                    prefix: 24
                })),
            ]
        );
    }

    #[test]
    fn test_valid_address() {
        let f = |addr| {
            valid_unicast_address_or_none(LifIpAddr { address: IpAddr::from(addr), prefix: 24 })
        };
        assert!(f([0, 0, 0, 0]).is_none());
        assert!(f([127, 0, 0, 1]).is_none());
        assert!(f([224, 0, 0, 5]).is_none());
        assert_eq!(
            f([1, 2, 3, 4]),
            Some(LifIpAddr { address: IpAddr::from([1, 2, 3, 4]), prefix: 24 })
        );
    }

    #[test]
    fn test_netstack_net_interface_into_hal_interface() {
        let net_interface = netstack::NetInterface {
            id: 5,
            flags: netstack::NET_INTERFACE_FLAG_UP | netstack::NET_INTERFACE_FLAG_DHCP,
            features: 0,
            configuration: 0,
            name: "test_if".to_string(),
            addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
            netmask: net::IpAddress::Ipv4(net::Ipv4Address { addr: [255, 255, 255, 0] }),
            broadaddr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 255] }),
            ipv6addrs: vec![],
            hwaddr: vec![1, 2, 3, 4, 5, 6],
        };
        assert_eq!(
            Interface::from(net_interface),
            Interface {
                id: PortId(5),
                name: "test_if".to_string(),
                ipv4_addr: Some(InterfaceAddress::Dhcp(LifIpAddr {
                    address: IpAddr::from([1, 2, 3, 4]),
                    prefix: 24
                }),),
                ipv6_addr: Vec::new(),
                enabled: true,
                state: InterfaceState::Unknown,
                dhcp_client_enabled: true,
            }
        )
    }

    #[test]
    fn test_route_from_forwarding_entry() {
        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fidl_fuchsia_net::Subnet {
                    addr: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: [1, 2, 3, 0],
                    }),
                    prefix_len: 23,
                },
                destination: stack::ForwardingDestination::NextHop(
                    fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: [1, 2, 3, 4]
                    },)
                ),
            }),
            Route {
                target: lifmgr::LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 23 },
                gateway: Some("1.2.3.4".parse().unwrap()),
                metric: None,
                port_id: None,
            },
            "valid IPv4 entry, with gateway"
        );

        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fidl_fuchsia_net::Subnet {
                    addr: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: [1, 2, 3, 0],
                    }),
                    prefix_len: 23,
                },
                destination: stack::ForwardingDestination::DeviceId(3)
            }),
            Route {
                target: lifmgr::LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 23 },
                gateway: None,
                metric: None,
                port_id: Some(PortId(3))
            },
            "valid IPv4 entry, no gateway"
        );

        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fidl_fuchsia_net::Subnet {
                    addr: fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                        addr: [0x26, 0x20, 0, 0, 0x10, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                    }),
                    prefix_len: 64,
                },
                destination: stack::ForwardingDestination::NextHop(
                    fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                        addr: [
                            0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x0, 0x5e, 0xff, 0xfe, 0x0, 0x02,
                            0x65
                        ],
                    })
                ),
            }),
            Route {
                target: lifmgr::LifIpAddr {
                    address: "2620:0:1000:5000::".parse().unwrap(),
                    prefix: 64
                },
                gateway: Some("fe80::200:5eff:fe00:265".parse().unwrap()),
                metric: None,
                port_id: None,
            },
            "valid IPv6 entry, with gateway"
        );

        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fidl_fuchsia_net::Subnet {
                    addr: fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                        addr: [0x26, 0x20, 0, 0, 0x10, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                    }),
                    prefix_len: 58,
                },
                destination: stack::ForwardingDestination::DeviceId(3)
            }),
            Route {
                target: lifmgr::LifIpAddr {
                    address: "2620:0:1000:5000::".parse().unwrap(),
                    prefix: 58
                },
                gateway: None,
                metric: None,
                port_id: Some(PortId(3))
            },
            "valid IPv6 entry, no gateway"
        );
    }
}

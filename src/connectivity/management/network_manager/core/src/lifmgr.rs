// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A basic Logical Interface (LIF) Manager.

// TODO(dpradilla): remove when done.
#![allow(dead_code)]

use crate::portmgr::PortId;
use crate::{error, ElementId, Version, UUID};
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_stack::{self as stack, InterfaceAddress};
use fidl_fuchsia_router_config;
use std::collections::{HashMap, HashSet};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

/// `LIFType` denotes the supported types of Logical Interfaces.
#[derive(Eq, PartialEq, Debug, Copy, Clone)]
pub enum LIFType {
    INVALID,
    WAN,
    LAN,
    ACCESS,
    TRUNK,
    GRE,
}

/// `LIF` implements a logical interface object.
#[derive(Eq, PartialEq, Debug, Clone)]
pub struct LIF {
    id: ElementId,
    l_type: LIFType,
    name: String,
    // `pid` is the id of the port associated with the LIF.
    // In case of a LIF associated to a bridge, it is the id of the bridge port.
    // In the case of a LIF associated to a single port, it's the id of that port.
    pid: PortId,
    // `ports` is the list of ports that are associated to the LIF.
    // In the case of a bridge these are all the ports that belong ot the bridge.
    ports: HashSet<PortId>,
    // VLAN ID of the bridge.
    vlan: u16,
    properties: LIFProperties,
}

impl LIF {
    pub fn new(
        v: Version,
        l_type: LIFType,
        name: &str,
        pid: PortId,
        port_list: Vec<PortId>,
        vlan: u16,
        properties: Option<LIFProperties>,
    ) -> error::Result<Self> {
        match l_type {
            LIFType::WAN => {
                if port_list.len() != 1 {
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidNumberOfPorts));
                }
            }
            LIFType::LAN => {
                if port_list.is_empty() {
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidNumberOfPorts));
                }
            }
            _ => return Err(error::NetworkManager::LIF(error::Lif::TypeNotSupported)),
        };
        let ports: HashSet<PortId> = port_list.iter().cloned().collect();
        let id = ElementId::new(v);
        Ok(LIF {
            id,
            l_type,
            name: name.to_string(),
            pid,
            ports,
            vlan,
            properties: match properties {
                None => LIFProperties { enabled: true, ..Default::default() },
                Some(p) => p,
            },
        })
    }

    fn add_port(&mut self, v: Version, p: PortId) -> error::Result<()> {
        self.id.version = v;
        self.ports.insert(p);
        Ok(())
    }

    /// Returns a list of ports associated with this interface. May be more than one if this
    /// interface is a bridge.
    pub fn ports(&self) -> impl ExactSizeIterator<Item = PortId> + '_ {
        self.ports.iter().copied()
    }

    fn remove_port(&mut self, v: Version, p: PortId) -> error::Result<()> {
        match self.l_type {
            LIFType::LAN => {
                if self.ports.len() <= 1 {
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidNumberOfPorts));
                }
            }
            LIFType::WAN => {
                return Err(error::NetworkManager::LIF(error::Lif::InvalidNumberOfPorts))
            }
            _ => return Err(error::NetworkManager::LIF(error::Lif::TypeNotSupported)),
        }
        if !self.ports.contains(&p) {
            return Ok(());
        }
        self.id.version = v;
        self.ports.remove(&p);
        Ok(())
    }

    fn set_vlan(&mut self, _v: Version, _vlan: u16) -> error::Result<()> {
        Err(error::NetworkManager::LIF(error::Lif::NotSupported))
    }

    /// Updates the version and properties associated with this interface.
    pub fn set_properties(&mut self, v: Version, p: LIFProperties) -> error::Result<()> {
        self.id.version = v;
        self.properties = p;
        Ok(())
    }

    /// Renames a LIF.
    fn rename(&mut self, v: Version, name: &'static str) -> error::Result<()> {
        self.id.version = v;
        self.name = name.to_string();
        Ok(())
    }

    /// Returns the LIF ElementID.
    pub fn id(&self) -> ElementId {
        self.id
    }

    /// Sets the Lif pid.
    pub fn set_pid(&mut self, pid: PortId) {
        self.pid = pid;
    }

    /// Returns the Lif pid.
    pub fn pid(&self) -> PortId {
        self.pid
    }

    /// Returns the properties associated with the LIF.
    pub fn properties(&self) -> &LIFProperties {
        &self.properties
    }

    /// Creates a fuchsia.router.config.Lif using the current state.
    pub fn to_fidl_lif(&self) -> fidl_fuchsia_router_config::Lif {
        let ps: Vec<_> = self.ports.iter().map(|p| p.to_u32()).collect();
        let lt;
        let p;
        match self.l_type {
            LIFType::WAN => {
                lt = fidl_fuchsia_router_config::LifType::Wan;
                p = Some(self.properties.to_fidl_wan());
            }
            LIFType::LAN => {
                lt = fidl_fuchsia_router_config::LifType::Lan;
                p = Some(self.properties.to_fidl_lan());
            }
            _ => {
                lt = fidl_fuchsia_router_config::LifType::Invalid;
                p = None;
            }
        };
        fidl_fuchsia_router_config::Lif {
            element: Some(self.id.into()),
            type_: Some(lt),
            name: Some(self.name.clone()),
            port_ids: Some(ps),
            vlan: Some(self.vlan),
            properties: p,
        }
    }
}

// TODO(dpradilla): Move struct declarations to the top.

/// LifIpAddr is an IP address and its prefix.
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct LifIpAddr {
    pub address: IpAddr,
    pub prefix: u8,
}

/// Creates an `std::net::IpAddr` from fuchsia.net.IpAddress.
pub fn to_ip_addr(addr: net::IpAddress) -> IpAddr {
    match addr {
        net::IpAddress::Ipv4(net::Ipv4Address { addr }) => IpAddr::from(addr),
        net::IpAddress::Ipv6(net::Ipv6Address { addr }) => IpAddr::from(addr),
    }
}

impl From<&InterfaceAddress> for LifIpAddr {
    fn from(addr: &InterfaceAddress) -> Self {
        LifIpAddr { address: to_ip_addr(addr.ip_address), prefix: addr.prefix_len }
    }
}

impl From<&net::Subnet> for LifIpAddr {
    fn from(s: &net::Subnet) -> Self {
        match *s {
            net::Subnet {
                addr: net::IpAddress::Ipv4(net::Ipv4Address { addr }),
                prefix_len: prefix,
            } => LifIpAddr { address: addr.into(), prefix },
            net::Subnet {
                addr: net::IpAddress::Ipv6(net::Ipv6Address { addr }),
                prefix_len: prefix,
            } => LifIpAddr { address: addr.into(), prefix },
        }
    }
}

/// Converts a subnet mask given as a set of octets to a scalar prefix length.
pub fn subnet_mask_to_prefix_length(addr: net::IpAddress) -> u8 {
    match addr {
        net::IpAddress::Ipv4(net::Ipv4Address { addr }) => {
            (!u32::from_be_bytes(addr)).leading_zeros() as u8
        }
        net::IpAddress::Ipv6(net::Ipv6Address { addr }) => {
            (!u128::from_be_bytes(addr)).leading_zeros() as u8
        }
    }
}

impl From<&fidl_fuchsia_router_config::CidrAddress> for LifIpAddr {
    fn from(a: &fidl_fuchsia_router_config::CidrAddress) -> Self {
        match a.address {
            Some(addr) => {
                LifIpAddr { address: to_ip_addr(addr), prefix: a.prefix_length.unwrap_or(0) }
            }
            None => LifIpAddr { address: IpAddr::from([0, 0, 0, 0]), prefix: 0 },
        }
    }
}

/// Strips the host part from a given `address` and `prefix`.
fn strip_host(address: &IpAddr, prefix: u8) -> IpAddr {
    match address {
        IpAddr::V4(a) => {
            if prefix == 0 {
                IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0))
            } else if prefix > 32 {
                *address
            } else {
                IpAddr::V4(Ipv4Addr::from(
                    (u32::from_be_bytes(a.octets()) >> (32 - prefix) << (32 - prefix))
                        .to_be_bytes(),
                ))
            }
        }
        IpAddr::V6(a) => {
            if prefix == 0 {
                IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0))
            } else if prefix > 128 {
                *address
            } else {
                IpAddr::V6(Ipv6Addr::from(
                    (u128::from_be_bytes(a.octets()) >> (128 - prefix) << (128 - prefix))
                        .to_be_bytes(),
                ))
            }
        }
    }
}

impl LifIpAddr {
    /// Convert to fuchsia.router.config.CidrAddress.
    pub fn to_fidl_address_and_prefix(&self) -> fidl_fuchsia_router_config::CidrAddress {
        match self.address {
            IpAddr::V4(a) => fidl_fuchsia_router_config::CidrAddress {
                address: Some(net::IpAddress::Ipv4(net::Ipv4Address { addr: a.octets() })),
                prefix_length: Some(self.prefix),
            },
            IpAddr::V6(a) => fidl_fuchsia_router_config::CidrAddress {
                address: Some(net::IpAddress::Ipv6(net::Ipv6Address { addr: a.octets() })),
                prefix_length: Some(self.prefix),
            },
        }
    }

    /// Convert to fuchsia.net.Subnet, which contains a subnet mask and prefix length.
    pub fn to_fidl_subnet(&self) -> net::Subnet {
        match self.address {
            IpAddr::V4(a) => net::Subnet {
                addr: net::IpAddress::Ipv4(net::Ipv4Address {
                    addr: (u32::from_be_bytes(a.octets()) >> (32 - self.prefix)
                        << (32 - self.prefix))
                        .to_be_bytes(),
                }),
                prefix_len: self.prefix,
            },
            IpAddr::V6(a) => net::Subnet {
                addr: net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: (u128::from_be_bytes(a.octets()) >> (128 - self.prefix)
                        << (128 - self.prefix))
                        .to_be_bytes(),
                }),
                prefix_len: self.prefix,
            },
        }
    }

    /// Convert to fuchsia.net.stack::InterfaceAddress.
    pub fn to_fidl_interface_address(&self) -> stack::InterfaceAddress {
        match self.address {
            IpAddr::V4(a) => stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr: a.octets() }),
                prefix_len: self.prefix,
            },
            IpAddr::V6(a) => stack::InterfaceAddress {
                ip_address: net::IpAddress::Ipv6(net::Ipv6Address { addr: a.octets() }),
                prefix_len: self.prefix,
            },
        }
    }

    /// Returns true if `address` is in the same subnet as `LifIpAddr`.
    pub fn is_in_same_subnet(&self, address: &IpAddr) -> bool {
        let local_subnet = strip_host(&self.address, self.prefix);
        let address_subnet = strip_host(address, self.prefix);
        local_subnet == address_subnet
    }
}

#[derive(Eq, PartialEq, Debug, Clone, Default)]
/// Dynamic properties associated with the LIF.
pub struct LIFProperties {
    /// Whether this interface's current address was acquired via DHCP. Corresponds to
    /// fuchsia.netstack.NetInterfaceFlagUp.
    pub dhcp: bool,
    /// Current address of this interface, may be `None`.
    pub address: Option<LifIpAddr>,
    /// Corresponds to fuchsia.netstack.NetInterfaceFlagUp.
    pub enabled: bool,
}

impl LIFProperties {
    /// Convert to fuchsia.router.config.LifProperties, WAN variant.
    pub fn to_fidl_wan(&self) -> fidl_fuchsia_router_config::LifProperties {
        fidl_fuchsia_router_config::LifProperties::Wan(fidl_fuchsia_router_config::WanProperties {
            address_method: Some(if self.dhcp {
                fidl_fuchsia_router_config::WanAddressMethod::Automatic
            } else {
                fidl_fuchsia_router_config::WanAddressMethod::Manual
            }),
            address_v4: self.address.as_ref().map(|x| x.to_fidl_address_and_prefix()),
            gateway_v4: None,
            address_v6: None,
            gateway_v6: None,
            enable: Some(self.enabled),
            metric: None,
            mtu: None,
            hostname: None,
            clone_mac: None,
            connection_parameters: None,
            connection_type: Some(fidl_fuchsia_router_config::WanConnection::Direct),
            connection_v6_mode: Some(
                fidl_fuchsia_router_config::WanIpV6ConnectionMode::Passthrough,
            ),
        })
    }

    /// Convert to fuchsia.router.config.LifProperties, LAN variant.
    pub fn to_fidl_lan(&self) -> fidl_fuchsia_router_config::LifProperties {
        fidl_fuchsia_router_config::LifProperties::Lan(fidl_fuchsia_router_config::LanProperties {
            address_v4: self.address.as_ref().map(|x| x.to_fidl_address_and_prefix()),
            address_v6: None,
            enable: Some(self.enabled),
            dhcp_config: None,
            enable_dhcp_server: Some(false),
            enable_dns_forwarder: Some(false),
        })
    }
}

/// `LIFManager` keeps track of Logical interfaces.
#[derive(Default)]
pub struct LIFManager {
    lifs: HashMap<UUID, LIF>,
    lif_names: HashSet<String>,
    lif_vlans: HashSet<u16>,
}

impl LIFManager {
    //! Create a new LIF database.
    pub fn new() -> Self {
        LIFManager { lifs: HashMap::new(), lif_names: HashSet::new(), lif_vlans: HashSet::new() }
    }

    /// Adds a lif to be managed by network manager.
    ///
    /// It verifies LIF is valid and does not colide with an exisiting one.
    pub fn add_lif(&mut self, l: &LIF) -> error::Result<()> {
        if self.lifs.contains_key(&l.id.uuid) {
            return Err(error::NetworkManager::LIF(error::Lif::DuplicateLIF));
        }
        if self.lif_names.contains(&l.name) {
            return Err(error::NetworkManager::LIF(error::Lif::InvalidName));
        }
        if l.vlan != 0 && self.lif_vlans.contains(&l.vlan) {
            return Err(error::NetworkManager::LIF(error::Lif::InvalidVlan));
        }
        // TODO(dpradilla): Verify ports not in use by other lif and ports actually exist.
        // This will change if switch trunk ports are supported, in that case, a trunk port can be
        // part of multiple LANs as long as its different vlans.

        self.lif_names.insert(l.name.clone());
        self.lif_vlans.insert(l.vlan);
        self.lifs.insert(l.id.uuid, l.clone());
        Ok(())
    }

    /// Removes a lif from lif manager.
    pub fn remove_lif(&mut self, id: UUID) -> Option<LIF> {
        let l = self.lifs.remove(&id)?;
        self.lif_vlans.remove(&l.vlan);
        self.lif_names.remove(&l.name);
        Some(l)
    }

    /// Returns a reference to a lif in lif manager.
    pub fn lif(&self, id: &UUID) -> Option<&LIF> {
        self.lifs.get(id)
    }

    /// Returns a mutable reference to a lif in lif manager.
    pub fn lif_mut(&mut self, id: &UUID) -> Option<&mut LIF> {
        self.lifs.get_mut(id)
    }

    /// Returns all LIFs of a given type.
    pub fn lifs(&self, lt: LIFType) -> impl Iterator<Item = &LIF> {
        self.lifs.iter().filter_map(move |(_, l)| if l.l_type == lt { Some(l) } else { None })
    }

    /// Returns all LIFs.
    pub fn all_lifs(&self) -> impl Iterator<Item = &LIF> {
        self.lifs.iter().map(|(_, l)| l)
    }

    /// Returns the lif at the given `PortId`.
    pub fn lif_at_port(&mut self, port: PortId) -> Option<&mut LIF> {
        self.lifs.iter_mut().find_map(|(_, lif)| if lif.pid == port { Some(lif) } else { None })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::portmgr::{Port, PortManager};
    use net::Ipv4Address;

    fn create_ports() -> PortManager {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), "port1", 1));
        pm.add_port(Port::new(PortId::from(2), "port2", 1));
        pm.add_port(Port::new(PortId::from(3), "port3", 1));
        pm.add_port(Port::new(PortId::from(4), "port4", 1));
        pm
    }

    #[test]
    fn test_new_lif() {
        let d = LIF::new(3, LIFType::WAN, "name", PortId::from(0), vec![PortId::from(3)], 0, None);
        d.unwrap();
        let d = LIF::new(3, LIFType::LAN, "name", PortId::from(0), vec![PortId::from(1)], 0, None);
        d.unwrap();
        let d = LIF::new(
            3,
            LIFType::LAN,
            "name",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        );
        d.unwrap();
    }
    #[test]
    fn test_new_lif_wrong_number_ports() {
        let d = LIF::new(3, LIFType::GRE, "name", PortId::from(0), Vec::new(), 0, None);
        assert!(d.is_err());
        let d = LIF::new(3, LIFType::WAN, "name", PortId::from(0), Vec::new(), 0, None);
        assert!(d.is_err());
        let d = LIF::new(
            3,
            LIFType::WAN,
            "name",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        );
        assert!(d.is_err());
        let d = LIF::new(3, LIFType::WAN, "name", PortId::from(0), vec![], 0, None);
        assert!(d.is_err());
        let d = LIF::new(3, LIFType::LAN, "name", PortId::from(0), Vec::new(), 0, None);
        assert!(d.is_err());
        let d = LIF::new(3, LIFType::LAN, "name", PortId::from(0), vec![], 0, None);
        assert!(d.is_err());
    }
    #[test]
    fn test_new_lif_inexisting_port() {
        let d = LIF::new(3, LIFType::WAN, "name", PortId::from(0), vec![PortId::from(5)], 0, None);
        assert!(d.is_ok());
        let d = LIF::new(
            3,
            LIFType::LAN,
            "name",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(6)],
            0,
            None,
        );
        assert!(d.is_ok());
    }
    #[test]
    fn test_new_lif_reusing_port() {
        let d = LIF::new(3, LIFType::WAN, "name", PortId::from(0), vec![PortId::from(1)], 0, None);
        assert!(d.is_ok());
        let d = LIF::new(
            3,
            LIFType::LAN,
            "name",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        );
        assert!(d.is_ok());
    }

    #[test]
    fn test_lif_manager_new() {
        let lm = LIFManager::new();
        assert_eq!(lm.lifs.len(), 0);
    }

    #[test]
    fn test_lif_manager_add() {
        let mut lm = LIFManager::new();
        lm.add_lif(
            &LIF::new(
                3,
                LIFType::LAN,
                "lan1",
                PortId::from(0),
                vec![PortId::from(1), PortId::from(2)],
                0,
                None,
            )
            .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::LAN, "lan2", PortId::from(0), vec![PortId::from(3)], 0, None)
                .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::WAN, "wan", PortId::from(0), vec![PortId::from(4)], 0, None)
                .unwrap(),
        )
        .unwrap();
        assert_eq!(lm.lifs.len(), 3);
    }
    // TODO(dpradilla): verify a port cant be part of multiple LIFs except for trunk switchport ports

    #[test]
    fn test_lif_manager_add_existing() {
        let mut lm = LIFManager::new();
        let l = LIF::new(
            3,
            LIFType::LAN,
            "lan1",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        )
        .unwrap();
        lm.add_lif(&l).unwrap();
        assert!(lm.add_lif(&l).is_err());
        assert_eq!(lm.lifs.len(), 1);
    }

    #[test]
    fn test_lif_manager_add_same_name() {
        let mut lm = LIFManager::new();
        let l = LIF::new(3, LIFType::LAN, "lan1", PortId::from(0), vec![PortId::from(1)], 0, None)
            .unwrap();
        lm.add_lif(&l).unwrap();
        let l = LIF::new(4, LIFType::LAN, "lan1", PortId::from(0), vec![PortId::from(2)], 0, None)
            .unwrap();
        assert!(lm.add_lif(&l).is_err());
        assert_eq!(lm.lifs.len(), 1);
    }

    #[test]
    #[ignore] // TODO(dpradilla): enable test once LIF actually checks for this.
    fn test_lif_manager_add_same_port() {
        let mut lm = LIFManager::new();
        let l = LIF::new(
            3,
            LIFType::LAN,
            "lan1",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        )
        .unwrap();
        lm.add_lif(&l).unwrap();
        let l = LIF::new(
            3,
            LIFType::LAN,
            "lan2",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        )
        .unwrap();
        lm.add_lif(&l).unwrap();
        assert_eq!(lm.lifs.len(), 1);
    }

    #[test]
    fn test_lif_manager_get_existing() {
        let mut lm = LIFManager::new();
        let l = LIF::new(
            3,
            LIFType::LAN,
            "lan1",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        )
        .unwrap();
        lm.add_lif(&l).unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::LAN, "lan2", PortId::from(0), vec![PortId::from(3)], 0, None)
                .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::WAN, "wan", PortId::from(0), vec![PortId::from(4)], 0, None)
                .unwrap(),
        )
        .unwrap();
        let got = lm.lif(&l.id.uuid);
        assert_eq!(got, Some(&l))
    }

    #[test]
    fn test_lif_manager_get_inexisting() {
        let mut lm = LIFManager::new();
        let l = LIF::new(
            3,
            LIFType::LAN,
            "lan1",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        )
        .unwrap();
        lm.add_lif(&l).unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::LAN, "lan2", PortId::from(0), vec![PortId::from(3)], 0, None)
                .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::WAN, "wan", PortId::from(0), vec![PortId::from(4)], 0, None)
                .unwrap(),
        )
        .unwrap();
        // Look for an entry that we know doesn't exist.
        let got = lm.lif(&std::u128::MAX);
        assert_eq!(got, None)
    }

    #[test]
    fn test_lif_manager_remove_existing() {
        let mut lm = LIFManager::new();
        let l = LIF::new(
            3,
            LIFType::LAN,
            "lan1",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        )
        .unwrap();
        lm.add_lif(&l).unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::LAN, "lan2", PortId::from(0), vec![PortId::from(3)], 0, None)
                .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::WAN, "wan", PortId::from(0), vec![PortId::from(4)], 0, None)
                .unwrap(),
        )
        .unwrap();
        assert_eq!(lm.lifs.len(), 3);
        let got = lm.remove_lif(l.id.uuid);
        assert_eq!(lm.lifs.len(), 2);
        assert_eq!(got, Some(l))
    }

    #[test]
    fn test_lif_manager_reuse_name_and_port_after_remove() {
        let mut lm = LIFManager::new();
        let l = LIF::new(
            3,
            LIFType::LAN,
            "lan1",
            PortId::from(0),
            vec![PortId::from(1), PortId::from(2)],
            0,
            None,
        )
        .unwrap();
        lm.add_lif(&l).unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::LAN, "lan2", PortId::from(0), vec![PortId::from(3)], 0, None)
                .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::WAN, "wan", PortId::from(0), vec![PortId::from(4)], 0, None)
                .unwrap(),
        )
        .unwrap();
        assert_eq!(lm.lifs.len(), 3);
        let got = lm.remove_lif(l.id.uuid);
        assert_eq!(lm.lifs.len(), 2);
        assert_eq!(got, Some(l));
        lm.add_lif(
            &LIF::new(
                3,
                LIFType::LAN,
                "lan1",
                PortId::from(0),
                vec![PortId::from(1), PortId::from(2)],
                0,
                None,
            )
            .unwrap(),
        )
        .unwrap();
        assert_eq!(lm.lifs.len(), 3)
    }

    #[test]
    fn test_lif_manager_remove_inexisting() {
        let mut lm = LIFManager::new();
        lm.add_lif(
            &LIF::new(
                3,
                LIFType::LAN,
                "lan1",
                PortId::from(0),
                vec![PortId::from(1), PortId::from(2)],
                0,
                None,
            )
            .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::LAN, "lan2", PortId::from(0), vec![PortId::from(3)], 0, None)
                .unwrap(),
        )
        .unwrap();
        lm.add_lif(
            &LIF::new(3, LIFType::WAN, "wan", PortId::from(0), vec![PortId::from(4)], 0, None)
                .unwrap(),
        )
        .unwrap();
        assert_eq!(lm.lifs.len(), 3);
        let got = lm.remove_lif(5 as UUID);
        assert_eq!(lm.lifs.len(), 3);
        assert_eq!(got, None);
    }

    fn v4(addr: [u8; 4]) -> net::IpAddress {
        net::IpAddress::Ipv4(net::Ipv4Address { addr })
    }

    fn v6(addr: [u8; 16]) -> net::IpAddress {
        net::IpAddress::Ipv6(net::Ipv6Address { addr })
    }

    #[test]
    fn test_to_prefix() {
        assert_eq!(subnet_mask_to_prefix_length(v4([255, 255, 255, 255])), 32);
        assert_eq!(subnet_mask_to_prefix_length(v4([255, 255, 255, 0])), 24);
        assert_eq!(subnet_mask_to_prefix_length(v4([255, 128, 0, 0])), 9);
        assert_eq!(subnet_mask_to_prefix_length(v4([0, 0, 0, 0])), 0);
        assert_eq!(
            subnet_mask_to_prefix_length(v6([
                255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
            ])),
            128
        );
        assert_eq!(
            subnet_mask_to_prefix_length(v6([
                255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0,
            ])),
            64
        );
    }

    #[test]
    fn test_from_subnet_tolifipaddr() {
        assert_eq!(
            LifIpAddr::from(&net::Subnet {
                addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                prefix_len: 32
            }),
            LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 32 }
        );
        assert_eq!(
            LifIpAddr::from(&net::Subnet {
                addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 4] }),
                prefix_len: 24
            }),
            LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 }
        );
        assert_eq!(
            LifIpAddr::from(&net::Subnet {
                addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 0] }),
                prefix_len: 24
            }),
            LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 }
        );
        assert_eq!(
            LifIpAddr::from(&net::Subnet {
                addr: net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: [
                        0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0xfc, 0xb6, 0x5b, 0x27, 0xfd, 0x2c, 0xf, 0x12
                    ]
                }),
                prefix_len: 64
            }),
            LifIpAddr { address: "fe80::fcb6:5b27:fd2c:f12".parse().unwrap(), prefix: 64 }
        );
    }

    fn build_lif_subnet(
        lifip_addr: &str,
        expected_addr: &str,
        prefix_len: u8,
    ) -> (LifIpAddr, net::Subnet) {
        let lifip = LifIpAddr { address: lifip_addr.parse().unwrap(), prefix: prefix_len };

        let ip: IpAddr = expected_addr.parse().unwrap();
        let expected_subnet = net::Subnet {
            addr: net::IpAddress::Ipv4(Ipv4Address {
                addr: match ip {
                    std::net::IpAddr::V4(v4addr) => v4addr.octets(),
                    std::net::IpAddr::V6(_) => panic!("unexpected ipv6 address"),
                },
            }),
            prefix_len,
        };
        (lifip, expected_subnet)
    }

    #[test]
    fn test_fidl_subnet_math() {
        let (lifip, expected_subnet) = build_lif_subnet("169.254.10.10", "169.254.10.10", 32);
        assert_eq!(lifip.to_fidl_subnet(), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.10.10", "169.254.10.0", 24);
        assert_eq!(lifip.to_fidl_subnet(), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.10.10", "169.254.0.0", 16);
        assert_eq!(lifip.to_fidl_subnet(), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.10.10", "169.0.0.0", 8);
        assert_eq!(lifip.to_fidl_subnet(), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.127.254", "169.254.124.0", 22);
        assert_eq!(lifip.to_fidl_subnet(), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("16.25.12.25", "16.16.0.0", 12);
        assert_eq!(lifip.to_fidl_subnet(), expected_subnet);
    }

    #[test]
    fn test_strip_host() {
        let got = strip_host(&"85.170.255.170".parse().unwrap(), 23);
        let want: IpAddr = "85.170.254.0".parse().unwrap();
        assert_eq!(want, got, "valid ipv4 prefix");

        let got = strip_host(&"1200:5555:aaaa:aaaa:aaaa:aaaa:5555:aaaa".parse().unwrap(), 57);
        let want: IpAddr = "1200:5555:aaaa:aa80:0:0:0:0".parse().unwrap();
        assert_eq!(want, got, "valid ipv6 prefix");

        let got = strip_host(&"85.170.170.85".parse().unwrap(), 58);
        let want: IpAddr = "85.170.170.85".parse().unwrap();
        assert_eq!(want, got, "invalid ipv4 prefix");

        let got = strip_host(&"1200:0:0:0:aaaa:5555:aaaa:5555".parse().unwrap(), 129);
        let want: IpAddr = "1200:0:0:0:aaaa:5555:aaaa:5555".parse().unwrap();
        assert_eq!(want, got, "invalid ipv6 prefix");

        let got = strip_host(&"85.170.170.85".parse().unwrap(), 0);
        let want: IpAddr = "0.0.0.0".parse().unwrap();
        assert_eq!(want, got, "ipv4 prefix 0");

        let got = strip_host(&"1200:0:0:0:aaaa:5555:aaaa:5555".parse().unwrap(), 0);
        let want: IpAddr = "::".parse().unwrap();
        assert_eq!(want, got, "ipv6 prefix 0");
    }

    #[test]
    fn test_is_in_same_subnet() {
        let address = LifIpAddr { address: "1.2.3.26".parse().unwrap(), prefix: 27 };
        assert!(address.is_in_same_subnet(&"1.2.3.26".parse().unwrap()));
        assert!(address.is_in_same_subnet(&"1.2.3.30".parse().unwrap()));
        assert!(!address.is_in_same_subnet(&"1.2.3.32".parse().unwrap()));
        let address = LifIpAddr {
            address: "2401:fa00:480:16:1295:6946:837:373a".parse().unwrap(),
            prefix: 58,
        };
        assert!(address.is_in_same_subnet(&"2401:fa00:480:16:1295:6946:837:373a".parse().unwrap()));
        assert!(address.is_in_same_subnet(&"2401:fa00:480:16:2345:6946:837:373a".parse().unwrap()));
        assert!(address.is_in_same_subnet(&"2401:fa00:480:26:1295:6946:837:373a".parse().unwrap()));
        assert!(!address.is_in_same_subnet(&"2401:fa00:480:46:2345:6946:837:373a".parse().unwrap()));
    }
}

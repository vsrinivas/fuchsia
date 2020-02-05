// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A basic Logical Interface (LIF) Manager.

// TODO(dpradilla): remove when done.
#![allow(dead_code)]

use {
    crate::{address::LifIpAddr, error, portmgr::PortId, ElementId, Version, UUID},
    fidl_fuchsia_router_config,
    std::collections::{HashMap, HashSet},
};

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
}

impl From<&LIF> for fidl_fuchsia_router_config::Lif {
    /// Creates a fuchsia.router.config.Lif using the current state.
    fn from(lif: &LIF) -> fidl_fuchsia_router_config::Lif {
        let ps: Vec<_> = lif.ports.iter().map(|p| p.to_u32()).collect();
        let lt;
        let p;
        match lif.l_type {
            LIFType::WAN => {
                lt = fidl_fuchsia_router_config::LifType::Wan;
                p = Some(lif.properties.to_fidl_wan());
            }
            LIFType::LAN => {
                lt = fidl_fuchsia_router_config::LifType::Lan;
                p = Some(lif.properties.to_fidl_lan());
            }
            _ => {
                lt = fidl_fuchsia_router_config::LifType::Invalid;
                p = None;
            }
        };
        fidl_fuchsia_router_config::Lif {
            element: Some(lif.id.into()),
            type_: Some(lt),
            name: Some(lif.name.clone()),
            port_ids: Some(ps),
            vlan: Some(lif.vlan),
            properties: p,
        }
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct DnsSearch {
    /// List of DNS servers to consult.
    servers: Vec<LifIpAddr>,
    /// Domain to add to non fully qualified domain names.
    domain_name: Option<String>,
}
impl From<fidl_fuchsia_router_config::DnsSearch> for DnsSearch {
    fn from(d: fidl_fuchsia_router_config::DnsSearch) -> Self {
        DnsSearch {
            servers: d.servers.into_iter().map(|ip| LifIpAddr::from(&ip)).collect(),
            domain_name: d.domain_name,
        }
    }
}

#[derive(Debug, Eq, PartialEq, Clone, Default)]
pub struct DhcpServerOptions {
    pub(crate) id: ElementId,
    pub(crate) lease_time_sec: u32,
    pub(crate) default_gateway: Option<fidl_fuchsia_net::Ipv4Address>,
    pub(crate) dns_server: Option<DnsSearch>,
    pub(crate) enable: bool,
}

#[derive(Eq, PartialEq, Clone, Debug)]
pub enum Dhcp {
    Server,
    Client,
    None,
}

impl Default for Dhcp {
    fn default() -> Self {
        Dhcp::None
    }
}

#[derive(Eq, PartialEq, Debug, Clone, Default)]
/// Properties associated with the LIF.
pub struct LIFProperties {
    /// Whether this interface's current address was acquired via DHCP. Corresponds to
    /// fuchsia.netstack.NetInterfaceFlagUp.
    pub dhcp: bool,
    /// Current address of this interface, may be `None`.
    pub address_v4: Option<LifIpAddr>,
    pub address_v6: Vec<LifIpAddr>,
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
            address_v4: self.address_v4.as_ref().map(|x| x.into()),
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
            address_v4: self.address_v4.as_ref().map(|x| x.into()),
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
    use fidl_fuchsia_net as fnet;
    use fnet::Ipv4Address;

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

    #[test]
    fn test_from_subnet_tolifipaddr() {
        assert_eq!(
            LifIpAddr::from(&fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                prefix_len: 32
            }),
            LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 32 }
        );
        assert_eq!(
            LifIpAddr::from(&fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                prefix_len: 24
            }),
            LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 }
        );
        assert_eq!(
            LifIpAddr::from(&fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 0] }),
                prefix_len: 24
            }),
            LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 24 }
        );
        assert_eq!(
            LifIpAddr::from(&fnet::Subnet {
                addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
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
    ) -> (LifIpAddr, fnet::Subnet) {
        let lifip = LifIpAddr { address: lifip_addr.parse().unwrap(), prefix: prefix_len };

        let ip = expected_addr.parse().unwrap();
        let expected_subnet = fnet::Subnet {
            addr: fnet::IpAddress::Ipv4(Ipv4Address {
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
        assert_eq!(fidl_fuchsia_net::Subnet::from(&lifip), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.10.10", "169.254.10.0", 24);
        assert_eq!(fidl_fuchsia_net::Subnet::from(&lifip), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.10.10", "169.254.0.0", 16);
        assert_eq!(fidl_fuchsia_net::Subnet::from(&lifip), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.10.10", "169.0.0.0", 8);
        assert_eq!(fidl_fuchsia_net::Subnet::from(&lifip), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("169.254.127.254", "169.254.124.0", 22);
        assert_eq!(fidl_fuchsia_net::Subnet::from(&lifip), expected_subnet);

        let (lifip, expected_subnet) = build_lif_subnet("16.25.12.25", "16.16.0.0", 12);
        assert_eq!(fidl_fuchsia_net::Subnet::from(&lifip), expected_subnet);
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A basic Logical Interface (LIF) Manager.

// TODO(dpradilla): remove when done.
#![allow(unused)]

use {
    crate::{address::LifIpAddr, error, portmgr::PortId, ElementId, Version, UUID},
    eui48::MacAddress,
    fidl_fuchsia_router_config as netconfig,
    std::collections::{HashMap, HashSet},
    std::net,
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
    pub l_type: LIFType,
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

    /// Returns the Lif type.
    pub fn ltype(&self) -> LIFType {
        self.l_type
    }

    /// Returns the properties associated with the LIF.
    pub fn properties(&self) -> &LIFProperties {
        &self.properties
    }
}

impl From<&LIF> for netconfig::Lif {
    /// Creates a fuchsia.router.config.Lif using the current state.
    fn from(lif: &LIF) -> netconfig::Lif {
        let ps: Vec<_> = lif.ports.iter().map(|p| p.to_u32()).collect();
        let lt;
        let p;
        match lif.l_type {
            LIFType::WAN => {
                lt = netconfig::LifType::Wan;
                p = Some(lif.properties.to_fidl_wan());
            }
            LIFType::LAN => {
                lt = netconfig::LifType::Lan;
                p = Some(lif.properties.to_fidl_lan());
            }
            _ => {
                lt = netconfig::LifType::Invalid;
                p = None;
            }
        };
        netconfig::Lif {
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
impl From<netconfig::DnsSearch> for DnsSearch {
    fn from(d: netconfig::DnsSearch) -> Self {
        DnsSearch {
            servers: d.servers.into_iter().map(|ip| LifIpAddr::from(&ip)).collect(),
            domain_name: d.domain_name,
        }
    }
}

/// Keeps track of DHCP server options.
#[derive(Debug, Eq, PartialEq, Clone, Default)]
pub(crate) struct DhcpServerOptions {
    pub(crate) id: ElementId,
    pub(crate) lease_time_sec: u32,
    pub(crate) default_gateway: Option<fidl_fuchsia_net::Ipv4Address>,
    pub(crate) dns_server: Option<DnsSearch>,
    pub(crate) enable: bool,
}

/// Defines the DHCP address pool.
#[derive(Debug, Eq, PartialEq, Clone)]
pub(crate) struct DhcpAddressPool {
    pub(crate) id: ElementId,
    pub(crate) start: std::net::Ipv4Addr,
    pub(crate) end: std::net::Ipv4Addr,
}

impl From<&netconfig::AddressPool> for DhcpAddressPool {
    fn from(p: &netconfig::AddressPool) -> Self {
        DhcpAddressPool {
            id: ElementId::default(),
            start: net::Ipv4Addr::from(p.from.addr),
            end: net::Ipv4Addr::from(p.to.addr),
        }
    }
}

/// Defines the DHCP address reservation.
#[derive(Debug, Eq, PartialEq, Clone)]
pub(crate) struct DhcpReservation {
    pub(crate) id: ElementId,
    pub(crate) name: Option<String>,
    pub(crate) address: std::net::Ipv4Addr,
    pub(crate) mac: eui48::MacAddress,
}

impl From<&netconfig::DhcpReservation> for DhcpReservation {
    fn from(p: &netconfig::DhcpReservation) -> Self {
        DhcpReservation {
            id: ElementId::default(),
            address: net::Ipv4Addr::from(p.address.addr),
            mac: eui48::MacAddress::new(p.mac.octets),
            name: Some(p.name.clone()),
        }
    }
}

/// Keeps track of DHCP server configuration.
#[derive(Debug, Eq, PartialEq, Clone, Default)]
pub struct DhcpServerConfig {
    pub(crate) options: DhcpServerOptions,
    pub(crate) pool: Option<DhcpAddressPool>,
    pub(crate) reservations: Vec<DhcpReservation>,
}

impl From<&netconfig::DhcpServerConfig> for DhcpServerConfig {
    fn from(p: &netconfig::DhcpServerConfig) -> Self {
        DhcpServerConfig {
            options: DhcpServerOptions::default(),
            pool: Some(DhcpAddressPool::from(&p.pool)),
            reservations: p.reservations.iter().map(|x| x.into()).collect(),
        }
    }
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
    /// DHCP configuration
    pub(crate) dhcp: Dhcp,
    pub(crate) dhcp_config: Option<DhcpServerConfig>,
    /// Current address of this interface, may be `None`.
    pub(crate) address_v4: Option<LifIpAddr>,
    pub(crate) address_v6: Vec<LifIpAddr>,
    /// Corresponds to fuchsia.netstack.NetInterfaceFlagUp.
    pub(crate) enabled: bool,
}

impl LIFProperties {
    /// Convert to fuchsia.router.config.LifProperties, WAN variant.
    pub fn to_fidl_wan(&self) -> netconfig::LifProperties {
        netconfig::LifProperties::Wan(netconfig::WanProperties {
            address_method: Some(if self.dhcp == Dhcp::Client {
                netconfig::WanAddressMethod::Automatic
            } else {
                netconfig::WanAddressMethod::Manual
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
            connection_type: Some(netconfig::WanConnection::Direct),
            connection_v6_mode: Some(netconfig::WanIpV6ConnectionMode::Passthrough),
        })
    }

    /// Convert to fuchsia.router.config.LifProperties, LAN variant.
    pub fn to_fidl_lan(&self) -> netconfig::LifProperties {
        let enable_dhcp_server = if Dhcp::Server == self.dhcp {
            if self.address_v4.is_none() {
                warn!("Ignoring DHCP server configuration as interface does not have static IP configured");
            }
            Some(self.dhcp == Dhcp::Server)
        //TODO(dpradilla): p.dhcp_config = self.dhcp_config.map(|x| x.into());
        } else {
            Some(false)
        };
        netconfig::LifProperties::Lan(netconfig::LanProperties {
            address_v4: self.address_v4.as_ref().map(|x| x.into()),
            address_v6: None,
            enable: Some(self.enabled),
            dhcp_config: None,
            enable_dhcp_server,
            enable_dns_forwarder: Some(false),
        })
    }

    fn update_wan_properties(&mut self, p: &netconfig::WanProperties) -> error::Result<()> {
        match &p.connection_type {
            None => {}
            Some(netconfig::WanConnection::Direct) => {
                info!("connection_type DIRECT ");
            }
            Some(cfg) => {
                info!("connection_type {:?}", cfg);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.connection_parameters {
            None => {}
            Some(cfg) => {
                info!("connection parameters {:?}", cfg);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.hostname {
            None => {}
            Some(cfg) => {
                info!("hostname {:?}", cfg);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.clone_mac {
            None => {}
            Some(cfg) => {
                info!("clone mac {:?}", cfg);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.mtu {
            None => {}
            Some(cfg) => {
                info!("mtu  {:?}", cfg);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.metric {
            None => {}
            Some(cfg) => {
                info!("metric {:?}", cfg);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.address_method {
            Some(netconfig::WanAddressMethod::Automatic) => {
                self.dhcp = Dhcp::Client;
                self.address_v4 = None;
            }
            Some(netconfig::WanAddressMethod::Manual) => {
                self.dhcp = Dhcp::None;
            }
            None => {}
        };
        match &p.address_v4 {
            None => {}
            Some(netconfig::CidrAddress {
                address: Some(address),
                prefix_length: Some(prefix_length),
            }) => {
                if self.dhcp == Dhcp::Client {
                    warn!(
                        "Setting a static ip is not allowed when \
                                 a dhcp client is configured"
                    );
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
                }
                let v4addr = LifIpAddr::from(p.address_v4.as_ref().unwrap());
                if !v4addr.is_ipv4() {
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
                }
                info!("Setting WAN IPv4 address to {:?}/{:?}", address, prefix_length);
                self.address_v4 = Some(v4addr);
            }
            _ => {
                warn!("invalid address {:?}", p.address_v4);
                return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
            }
        };
        match &p.gateway_v4 {
            None => {}
            Some(gw) => {
                if self.dhcp == Dhcp::Client {
                    warn!(
                        "Setting an ipv4 gateway is not allowed when \
                                 a dhcp client is configured"
                    );
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
                }
                warn!("setting gateway not supportted {:?}", gw);
                // TODO(dpradilla): verify gateway is local
                // and install route.
            }
        }
        match &p.connection_v6_mode {
            None => {}
            Some(netconfig::WanIpV6ConnectionMode::Passthrough) => {
                info!("v6 mode Passthrough");
            }
            Some(cfg) => {
                info!("v6 mode {:?}", cfg);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.address_v6 {
            None => {}
            Some(netconfig::CidrAddress {
                address: Some(address),
                prefix_length: Some(prefix_length),
            }) => {
                info!("Setting WAN IPv6 to {:?}/{:?}", address, prefix_length);
                let a = LifIpAddr::from(p.address_v6.as_ref().unwrap());
                if !a.is_ipv6() {
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
                }
                self.address_v6 = vec![a];
            }
            _ => {
                warn!("invalid address {:?}", p.address_v6);
                return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
            }
        };
        match &p.gateway_v6 {
            None => {}
            Some(gw) => {
                info!("setting gateway {:?}", gw);
                //  TODO(dpradilla): implement. - verify gw is in local network
                warn!("setting gateway not supportted {:?}", gw);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        if let Some(enable) = &p.enable {
            info!("enable {:?}", enable);
            self.enabled = *enable
        };
        Ok(())
    }

    fn update_lan_properties(&mut self, p: &netconfig::LanProperties) -> error::Result<()> {
        match p.enable_dhcp_server {
            None => {}
            Some(true) => {
                info!("enable DHCP server");
                self.dhcp = Dhcp::Server;
            }
            Some(false) => {
                info!("disable DHCP server");
                self.dhcp = Dhcp::None;
            }
        };
        match &p.dhcp_config {
            None => {}
            Some(cfg) => {
                info!("DHCP server configuration {:?}", cfg);
                self.dhcp_config = Some(DhcpServerConfig::from(cfg));
            }
        };
        match &p.address_v4 {
            None => self.dhcp = Dhcp::None,
            Some(netconfig::CidrAddress {
                address: Some(address),
                prefix_length: Some(prefix_length),
            }) => {
                info!("Setting LAN IPv4 address to {:?}/{:?}", address, prefix_length);
                let v4addr = LifIpAddr::from(p.address_v4.as_ref().unwrap());
                if !v4addr.is_ipv4() {
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
                }
                self.address_v4 = Some(v4addr);
            }
            _ => {
                warn!("invalid address {:?}", p.address_v4);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };
        match &p.address_v6 {
            None => {}
            Some(netconfig::CidrAddress {
                address: Some(address),
                prefix_length: Some(prefix_length),
            }) => {
                info!("Setting LAN IPv6 address to {:?}/{:?}", address, prefix_length);
                let a = LifIpAddr::from(p.address_v6.as_ref().unwrap());
                if !a.is_ipv6() {
                    return Err(error::NetworkManager::LIF(error::Lif::InvalidParameter));
                }
                self.address_v6 = vec![a];
            }
            _ => {
                warn!("invalid address {:?}", p.address_v6);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
        };

        if let Some(enable) = p.enable {
            info!("enable {:?}", enable);
            self.enabled = enable
        }
        Ok(())
    }

    /// `get_updated` returns a `LIFProperties` updated to reflect the changes indicated in
    /// `properties`.
    pub fn get_updated(&self, properties: &netconfig::LifProperties) -> error::Result<Self> {
        let mut lp = self.clone();
        match properties {
            netconfig::LifProperties::Wan(p) => lp.update_wan_properties(p)?,

            netconfig::LifProperties::Lan(p) => lp.update_lan_properties(p)?,
        }
        Ok(lp)
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
    fn test_from_ipaddress_to_lifipaddr() {
        assert_eq!(
            LifIpAddr::from(&fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] })),
            LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 32 }
        );
        assert_eq!(
            LifIpAddr::from(&fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0xfc, 0xb6, 0x5b, 0x27, 0xfd, 0x2c, 0xf, 0x12]
            })),
            LifIpAddr { address: "fe80::fcb6:5b27:fd2c:f12".parse().unwrap(), prefix: 128 }
        );
    }

    #[test]
    fn test_get_updated() {
        for (base, properties, result, name) in [
            (
                LIFProperties::default(),
                netconfig::LifProperties::Lan(netconfig::LanProperties {
                    address_v4: None,
                    address_v6: None,
                    dhcp_config: None,
                    enable: None,
                    enable_dhcp_server: None,
                    enable_dns_forwarder: None,
                }),
                Ok(LIFProperties::default()),
                "lan all default",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Lan(netconfig::LanProperties {
                    address_v4: None,
                    address_v6: None,
                    dhcp_config: None,
                    enable: Some(true),
                    enable_dhcp_server: Some(true),
                    enable_dns_forwarder: Some(true),
                }),
                Ok(LIFProperties { enabled: true, ..Default::default() }),
                "enable dhcp server, but no ip v4 address",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Lan(netconfig::LanProperties {
                    address_v4: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                            addr: [1, 2, 3, 4],
                        })),
                        prefix_length: Some(24),
                    }),
                    address_v6: None,
                    dhcp_config: None,
                    enable: Some(true),
                    enable_dhcp_server: Some(true),
                    enable_dns_forwarder: Some(true),
                }),
                Ok(LIFProperties {
                    address_v4: Some(LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 }),
                    dhcp: Dhcp::Server,
                    enabled: true,
                    ..Default::default()
                }),
                "enable dhcp server, with ip v4 address",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Lan(netconfig::LanProperties {
                    address_v4: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                            addr: [1, 2, 3, 4],
                        })),
                        prefix_length: Some(24),
                    }),
                    address_v6: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        })),
                        prefix_length: Some(64),
                    }),
                    dhcp_config: None,
                    enable: None,
                    enable_dhcp_server: Some(true),
                    enable_dns_forwarder: None,
                }),
                Ok(LIFProperties {
                    address_v4: Some(LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 }),
                    address_v6: vec![LifIpAddr {
                        address: "102:304:506:708:90a:b0c:d0e:f10".parse().unwrap(),
                        prefix: 64,
                    }],
                    dhcp: Dhcp::Server,
                    ..Default::default()
                }),
                "dhcp server, ipv4 and ipv6",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Lan(netconfig::LanProperties {
                    address_v4: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        })),
                        prefix_length: Some(64),
                    }),
                    address_v6: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                            addr: [1, 2, 3, 4],
                        })),
                        prefix_length: Some(24),
                    }),
                    dhcp_config: None,
                    enable: None,
                    enable_dhcp_server: Some(true),
                    enable_dns_forwarder: None,
                }),
                Err(error::NetworkManager::LIF(error::Lif::InvalidParameter)),
                "dhcp server, ipv4 and ipv6 reversed, should not pass.",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Wan(netconfig::WanProperties {
                    address_v4: None,
                    address_v6: None,
                    address_method: None,
                    clone_mac: None,
                    connection_parameters: None,
                    connection_type: None,
                    connection_v6_mode: None,
                    gateway_v4: None,
                    gateway_v6: None,
                    hostname: None,
                    metric: None,
                    mtu: None,
                    enable: None,
                }),
                Ok(LIFProperties::default()),
                "wan all default",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Wan(netconfig::WanProperties {
                    address_v4: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                            addr: [1, 2, 3, 4],
                        })),
                        prefix_length: Some(24),
                    }),
                    address_v6: None,
                    address_method: None,
                    clone_mac: None,
                    connection_parameters: None,
                    connection_type: None,
                    connection_v6_mode: None,
                    gateway_v4: None,
                    gateway_v6: None,
                    hostname: None,
                    metric: None,
                    mtu: None,
                    enable: None,
                }),
                Ok(LIFProperties {
                    address_v4: Some(LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 }),
                    ..Default::default()
                }),
                "wan ip v4",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Wan(netconfig::WanProperties {
                    address_v4: None,
                    address_v6: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                            addr: [1, 2, 3, 4],
                        })),
                        prefix_length: Some(24),
                    }),
                    address_method: None,
                    clone_mac: None,
                    connection_parameters: None,
                    connection_type: None,
                    connection_v6_mode: None,
                    gateway_v4: None,
                    gateway_v6: None,
                    hostname: None,
                    metric: None,
                    mtu: None,
                    enable: None,
                }),
                Err(error::NetworkManager::LIF(error::Lif::InvalidParameter)),
                "wan ip v4 in wrong place",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Wan(netconfig::WanProperties {
                    address_v4: None,
                    address_v6: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        })),
                        prefix_length: Some(64),
                    }),
                    address_method: None,
                    clone_mac: None,
                    connection_parameters: None,
                    connection_type: None,
                    connection_v6_mode: None,
                    gateway_v4: None,
                    gateway_v6: None,
                    hostname: None,
                    metric: None,
                    mtu: None,
                    enable: None,
                }),
                Ok(LIFProperties {
                    address_v6: vec![LifIpAddr {
                        address: "102:304:506:708:90a:b0c:d0e:f10".parse().unwrap(),
                        prefix: 64,
                    }],
                    ..Default::default()
                }),
                "wan ip v6",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Wan(netconfig::WanProperties {
                    address_v4: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                            addr: [1, 2, 3, 4],
                        })),
                        prefix_length: Some(24),
                    }),
                    address_v6: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        })),
                        prefix_length: Some(64),
                    }),
                    address_method: Some(netconfig::WanAddressMethod::Manual),
                    clone_mac: None,
                    connection_parameters: None,
                    connection_type: None,
                    connection_v6_mode: None,
                    gateway_v4: None,
                    gateway_v6: None,
                    hostname: None,
                    metric: None,
                    mtu: None,
                    enable: None,
                }),
                Ok(LIFProperties {
                    address_v4: Some(LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 24 }),
                    address_v6: vec![LifIpAddr {
                        address: "102:304:506:708:90a:b0c:d0e:f10".parse().unwrap(),
                        prefix: 64,
                    }],
                    ..Default::default()
                }),
                "wan ip v4 and ipv6",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Wan(netconfig::WanProperties {
                    address_v4: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                            addr: [1, 2, 3, 4],
                        })),
                        prefix_length: Some(24),
                    }),
                    address_v6: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        })),
                        prefix_length: Some(64),
                    }),
                    address_method: Some(netconfig::WanAddressMethod::Automatic),
                    clone_mac: None,
                    connection_parameters: None,
                    connection_type: None,
                    connection_v6_mode: None,
                    gateway_v4: None,
                    gateway_v6: None,
                    hostname: None,
                    metric: None,
                    mtu: None,
                    enable: None,
                }),
                Err(error::NetworkManager::LIF(error::Lif::InvalidParameter)),
                "wan invalid address method",
            ),
            (
                LIFProperties::default(),
                netconfig::LifProperties::Wan(netconfig::WanProperties {
                    address_v4: None,
                    address_v6: Some(netconfig::CidrAddress {
                        address: Some(fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        })),
                        prefix_length: Some(64),
                    }),
                    address_method: Some(netconfig::WanAddressMethod::Automatic),
                    clone_mac: None,
                    connection_parameters: None,
                    connection_type: None,
                    connection_v6_mode: None,
                    gateway_v4: None,
                    gateway_v6: None,
                    hostname: None,
                    metric: None,
                    mtu: None,
                    enable: None,
                }),
                Ok(LIFProperties {
                    dhcp: Dhcp::Client,
                    address_v6: vec![LifIpAddr {
                        address: "102:304:506:708:90a:b0c:d0e:f10".parse().unwrap(),
                        prefix: 64,
                    }],
                    ..Default::default()
                }),
                "wan DHCPv4 address.",
            ),
            // TODO(dpradilla) Not testing unsupported features. Add test cases as features are
            // implemented.
        ]
        .iter()
        {
            let got = LIFProperties::get_updated(&base, &properties);
            assert_eq!(&got, result, "{}: Got {:?}, Want {:?}", name, got, result);
        }
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

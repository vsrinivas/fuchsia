// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{address::LifIpAddr, error, lifmgr, ElementId},
    eui48::MacAddress,
    serde::{de, Deserializer},
    serde::{Deserialize, Serialize},
    serde_json::Value,
    std::collections::HashSet,
    std::convert::{TryFrom, TryInto},
    std::fs::File,
    std::io::Read,
    std::net::{self, IpAddr},
    std::path::{Path, PathBuf},
    std::str::FromStr,
    valico::json_schema::{self, schema},
};

/// Interface types defined by the OpenConfig interfaces model.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum InterfaceType {
    IfEthernet,
    IfAggregate,
    IfLoopback,
    IfRoutedVlan,
    IfSonet,
    IfTunnelGre4,
    IfTunnelGre6,
    IfUplink,
}

/// The possible interface operational states from RFC2863 "Standard Interfaces MIB".
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum OperState {
    Up,
    Down,
    Unknown,
    Testing,
    NotPresent,
    LowerLayerDown,
}

/// The possible interface admin states from RFC2863 "Standard Interfaces MIB".
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum AdminState {
    Up,
    Down,
    Testing,
}

/// When `auto-negotiate` is true, this optionally sets the duplex mode that will be advertised to
/// the peer. If unspecified, the interface should negotiate the duplex mode directly (typically
/// full-duplex). When auto-negotiate is false, this sets the duplex mode on the interface directly.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DuplexMode {
    Full,
    Half,
}

/// Defines VLAN interface types.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum InterfaceMode {
    Access,
    Trunk,
}

/// When `auto-negotiate` is true, this optionally sets the port-speed mode that will be advertised
/// to the peer for negotiation. If unspecified, it is expected that the interface will select the
/// highest speed available based on negotiation. When auto-negotiate is set to false, sets the
/// link speed to a fixed value.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields)]
pub enum PortSpeed {
    #[serde(alias = "SPEED_10MB")]
    Speed10mb,
    #[serde(alias = "SPEED_100MB")]
    Speed100mb,
    #[serde(alias = "SPEED_1G")]
    Speed1g,
    #[serde(alias = "SPEED_2500MB")]
    Speed2500mb,
    #[serde(alias = "SPEED_5G")]
    Speed5g,
    #[serde(alias = "SPEED_10G")]
    Speed10g,
    #[serde(alias = "SPEED_UNKNOWN")]
    SpeedUnknown,
}

/// The forwarding actions for ACLs.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ForwardingAction {
    Accept,
    Drop,
}

/// The direction of the connection.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum Direction {
    In,
    Out,
    Both,
}

/// The protocol to match.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum Protocol {
    Tcp,
    Udp,
    Any,
}

// TODO(cgibson): VLANs.
// TODO(cgibson): WLAN.
// TODO(cgibson): Need to figure out versioning. Having "unknown" fields and "flatten"'ing them
// into an `extras` field might be an interesting experiment.
#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DeviceConfig {
    pub device: Device,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Device {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub default_interface: Option<Interface>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interfaces: Option<Vec<Interfaces>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub acls: Option<Acls>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub services: Option<Services>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Interfaces {
    pub interface: Interface,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Interface {
    // Certain types of interface (e.g. RoutedVlan) do not have a device_id.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub device_id: Option<String>,
    // Every Interface must have a config definition.
    pub config: InterfaceConfig,
    // If oper_state is omitted, then the default is `OperState::Up`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oper_state: Option<OperState>,
    // An interface must contain exactly one: 'subinterfaces', 'switched_vlan', or 'routed_vlan'.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subinterfaces: Option<Vec<Subinterface>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub switched_vlan: Option<SwitchedVlan>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub routed_vlan: Option<RoutedVlan>,
    // ethernet can be omitted and defaults will be applied.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ethernet: Option<Ethernet>,
    // tcp_offload can be omitted and the default will be applied.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tcp_offload: Option<bool>,
}

impl Interface {
    fn get_dhcp_server_config(&self) -> Option<&DhcpServer> {
        self.subinterfaces.as_ref().and_then(|subifs| {
            if subifs.len() != 1 {
                warn!("LIFProperties does not support multiple addresses yet.")
            }
            subifs
                .first()
                .and_then(|subif| subif.ipv4.as_ref().and_then(|cfg| cfg.dhcp_server.as_ref()))
        })
    }
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct InterfaceConfig {
    pub name: String,
    #[serde(rename = "type")]
    pub interface_type: InterfaceType,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Subinterface {
    // If admin_state is omitted, then the default is AdminState::Up.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub admin_state: Option<AdminState>,
    // A subinterface must have at least one IP address configuration.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpAddressConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpAddressConfig>,
}

#[derive(Serialize, Deserialize, Clone, Debug, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct IpAddressConfig {
    pub addresses: Vec<IpAddress>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dhcp_server: Option<DhcpServer>,
}

#[derive(Serialize, Deserialize, Debug, Eq, PartialEq, Copy, Clone)]
pub struct NetIpAddr(pub IpAddr);

impl From<fidl_fuchsia_net::IpAddress> for NetIpAddr {
    fn from(addr: fidl_fuchsia_net::IpAddress) -> Self {
        NetIpAddr(match addr {
            fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr }) => {
                addr.into()
            }
            fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address { addr }) => {
                addr.into()
            }
        })
    }
}

impl From<NetIpAddr> for fidl_fuchsia_net::IpAddress {
    fn from(netipaddr: NetIpAddr) -> Self {
        let addr = netipaddr.0;
        match addr {
            IpAddr::V4(v4addr) => {
                fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                    addr: v4addr.octets(),
                })
            }
            IpAddr::V6(v6addr) => {
                fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                    addr: v6addr.octets(),
                })
            }
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct IpAddress {
    // If omitted, the default is to enable a DHCP client on this interface.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dhcp_client: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cidr_address: Option<CidrAddress>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct SwitchedVlan {
    pub interface_mode: InterfaceMode,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub access_vlan: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub trunk_vlans: Option<Vec<u16>>,
}

#[derive(Serialize, Deserialize, Clone, Debug, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct RoutedVlan {
    pub vlan_id: u16,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpAddressConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpAddressConfig>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Ethernet {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<EthernetConfig>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct EthernetConfig {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub auto_negotiate: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub duplex_mode: Option<DuplexMode>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enable_flow_control: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mac_address: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mtu: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub port_speed: Option<PortSpeed>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Acls {
    pub acl_entries: Vec<AclEntry>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct AclEntry {
    pub config: FilterConfig,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpFilter>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpFilter>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct FilterConfig {
    pub forwarding_action: ForwardingAction,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub device_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub direction: Option<Direction>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub comment: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct IpFilter {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub src_address: Option<CidrAddress>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dst_address: Option<CidrAddress>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub src_ports: Option<PortRange>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dst_ports: Option<PortRange>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub protocol: Option<Protocol>,
}

#[derive(Serialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PortRange {
    pub from: u16,
    pub to: u16,
}

/// Converts a port number from a string into a `u16`.
fn make_port(port: &str) -> error::Result<u16> {
    port.parse::<u16>().map_err(|e| {
        error::NetworkManager::Config(error::Config::Malformed {
            msg: format!("Failed to make new port range from: '{}': {}", port, e),
        })
    })
}

impl FromStr for PortRange {
    type Err = error::NetworkManager;
    fn from_str(ports: &str) -> error::Result<Self> {
        let mut iter = ports.trim().split("-").fuse();
        let first = iter.next().ok_or_else(|| {
            error::NetworkManager::Config(error::Config::Malformed {
                msg: format!("invalid port range: {}", ports),
            })
        })?;
        let second = iter.next().unwrap_or(first);
        let range = std::ops::RangeInclusive::new(make_port(first)?, make_port(second)?);
        Ok(PortRange { from: *range.start(), to: *range.end() })
    }
}

impl<'de> Deserialize<'de> for PortRange {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let p = String::deserialize(deserializer)?;
        FromStr::from_str(&p).map_err(de::Error::custom)
    }
}

impl From<&PortRange> for fidl_fuchsia_router_config::PortRange {
    fn from(range: &PortRange) -> Self {
        fidl_fuchsia_router_config::PortRange { from: range.from, to: range.to }
    }
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Services {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ip_forwarding: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub nat: Option<bool>,
}

#[derive(Serialize, Deserialize, Clone, Debug, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DhcpServer {
    pub enabled: bool,
    pub dhcp_pool: DhcpPool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub static_ip_allocations: Option<Vec<StaticIpAllocations>>,
}

impl DhcpServer {
    fn validate(&self, config_path: &str) -> error::Result<()> {
        self.dhcp_pool.validate(config_path)?;
        match &self.static_ip_allocations {
            None => Ok(()),
            Some(allocations) => {
                for allocation in allocations.iter() {
                    allocation.validate(config_path)?
                }
                Ok(())
            }
        }
    }

    fn validate_addresses(&self, addr: &LifIpAddr) -> bool {
        let valid_pool = addr.is_in_same_subnet(&net::IpAddr::V4(self.dhcp_pool.start))
            && addr.is_in_same_subnet(&net::IpAddr::V4(self.dhcp_pool.end));
        self.static_ip_allocations.as_ref().map_or(valid_pool, |x| {
            x.iter()
                .fold(valid_pool, |r, a| r & addr.is_in_same_subnet(&net::IpAddr::V4(a.ip_address)))
        })
    }
}

impl TryFrom<DhcpServer> for lifmgr::DhcpServerConfig {
    type Error = error::NetworkManager;
    fn try_from(s: DhcpServer) -> Result<Self, Self::Error> {
        let a = s.static_ip_allocations.unwrap_or_else(|| vec![]);
        let reservations = a.iter().filter_map(|x| x.try_into().ok());

        Ok(lifmgr::DhcpServerConfig {
            options: lifmgr::DhcpServerOptions { enable: Some(s.enabled), ..Default::default() },
            pool: Some(lifmgr::DhcpAddressPool {
                id: ElementId::default(),
                start: s.dhcp_pool.start,
                end: s.dhcp_pool.end,
            }),
            reservations: reservations.collect(),
        })
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DhcpPool {
    pub start: net::Ipv4Addr,
    pub end: net::Ipv4Addr,
    pub lease_time: String, // TODO(dpradilla): add support and validation for lease_time.
}

impl DhcpPool {
    /// Validate a [`config::dhcp_pool`] configuration.
    fn validate(&self, config_path: &str) -> error::Result<()> {
        if !self.start.is_private() || !self.end.is_private() {
            return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: config_path.to_string(),
                error: "DhcpPool start and end must be private addresses.".to_string(),
            }));
        }
        if u32::from(self.start) > u32::from(self.end) {
            return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: config_path.to_string(),
                error: "DhcpPool start and end is not a valid range.".to_string(),
            }));
        }
        Ok(())
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct StaticIpAllocations {
    pub device_name: String,
    pub mac_address: MacAddress,
    pub ip_address: net::Ipv4Addr,
}

impl TryFrom<&StaticIpAllocations> for lifmgr::DhcpReservation {
    type Error = error::NetworkManager;
    fn try_from(allocations: &StaticIpAllocations) -> Result<Self, Self::Error> {
        let name = if allocations.device_name.is_empty() {
            None
        } else {
            Some(allocations.device_name.to_string())
        };
        if !allocations.mac_address.is_unicast() || allocations.mac_address.is_nil() {
            return Err(error::NetworkManager::Config(error::Config::NotSupported {
                msg: "Invalid mac address".to_string(),
            }));
        }
        Ok(lifmgr::DhcpReservation {
            id: ElementId::default(),
            name,
            address: allocations.ip_address,
            mac: allocations.mac_address,
        })
    }
}

impl StaticIpAllocations {
    /// Validate a [`config::StaticIpAllocations`] configuration.
    fn validate(&self, config_path: &str) -> error::Result<()> {
        if !self.ip_address.is_private() {
            return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: config_path.to_string(),
                error: "must be private addresses.".to_string(),
            }));
        }
        if !self.mac_address.is_unicast() || self.mac_address.is_nil() {
            return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: config_path.to_string(),
                error: "not a valid MAC address".to_string(),
            }));
        }
        Ok(())
    }
}

#[derive(Debug, PartialEq)]
struct DeviceConfigPaths {
    user_config_path: PathBuf,
    factory_config_path: PathBuf,
    device_schema_path: PathBuf,
}

#[derive(Debug, PartialEq)]
pub struct Config {
    device_config: Option<DeviceConfig>,
    startup_path: Option<PathBuf>,
    paths: DeviceConfigPaths,
}

#[derive(Serialize, Clone, Debug, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct CidrAddress {
    pub ip: NetIpAddr,
    pub prefix_length: u8,
}

impl FromStr for CidrAddress {
    type Err = String;
    fn from_str(cidr_address: &str) -> Result<Self, Self::Err> {
        let mut iter = cidr_address.trim().split('/');
        let addr: IpAddr = iter
            .next()
            .ok_or(format!("invalid CIDR formatted IP address string: {}", cidr_address))?
            .parse()
            .map_err(|e| format!("failed while parsing CIDR address: {}", e))?;
        let prefix_length = iter
            .next()
            .ok_or(format!("invalid CIDR formatted IP address string: {}", cidr_address))?
            .parse::<u8>()
            .map_err(|e| format!("failed while parsing CIDR address prefix: {}", e))?;
        if iter.next().is_some() {
            return Err(format!("invalid CIDR formatted IP address string: {}", cidr_address));
        }

        Ok(CidrAddress { ip: NetIpAddr(addr), prefix_length })
    }
}

impl<'de> Deserialize<'de> for CidrAddress {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        FromStr::from_str(&s).map_err(de::Error::custom)
    }
}

impl From<&CidrAddress> for fidl_fuchsia_router_config::CidrAddress {
    fn from(cidr_addr: &CidrAddress) -> Self {
        fidl_fuchsia_router_config::CidrAddress {
            address: Some(cidr_addr.ip.into()),
            prefix_length: Some(cidr_addr.prefix_length),
        }
    }
}

impl TryFrom<fidl_fuchsia_router_config::CidrAddress> for CidrAddress {
    type Error = error::NetworkManager;
    fn try_from(cidr_addr: fidl_fuchsia_router_config::CidrAddress) -> error::Result<Self> {
        let ip = cidr_addr.address.ok_or_else(|| {
            error::NetworkManager::Config(error::Config::Malformed {
                msg: format!("Failed to convert invalid FIDL CidrAddress: {:?}", cidr_addr),
            })
        })?;
        let prefix_length = cidr_addr.prefix_length.ok_or_else(|| {
            error::NetworkManager::Config(error::Config::Malformed {
                msg: format!("Failed to convert invalid FIDL CidrAddress: {:?}", cidr_addr),
            })
        })?;
        Ok(CidrAddress { ip: ip.into(), prefix_length })
    }
}

const UNNAMED_BRIDGE: &str = "unnamed_bridge";

/// Converts a Valico JSON SchemaError to a `String`.
fn schema_error(error: schema::SchemaError) -> String {
    match error {
        schema::SchemaError::WrongId => String::from("Wrong Id"),
        schema::SchemaError::IdConflicts => String::from("Id conflicts"),
        schema::SchemaError::NotAnObject => String::from("Not an object"),
        schema::SchemaError::UrlParseError(p) => format!("Url parse error: {}", p),
        schema::SchemaError::UnknownKey(key) => format!("Unknown key: {}", key),
        schema::SchemaError::Malformed { path, detail } => {
            format!("Malformed: {}, {}", path, detail)
        }
    }
}

impl Config {
    /// Creates a new Config object with the given user, factory, and device schema paths.
    pub fn new<P: Into<PathBuf>>(user_path: P, factory_path: P, device_schema: P) -> Config {
        Config {
            device_config: None,
            startup_path: None,
            paths: DeviceConfigPaths {
                user_config_path: user_path.into(),
                factory_config_path: factory_path.into(),
                device_schema_path: device_schema.into(),
            },
        }
    }

    /// Returns the user config path.
    pub fn user_config_path(&self) -> &Path {
        self.paths.user_config_path.as_path()
    }

    /// Returns the factory config path.
    pub fn factory_config_path(&self) -> &Path {
        self.paths.factory_config_path.as_path()
    }

    /// Returns the device schema path.
    pub fn device_schema_path(&self) -> &Path {
        self.paths.device_schema_path.as_path()
    }

    /// Returns the path of the config that was read at startup
    ///
    /// If no configuration file has been read yet, then returns an empty path.
    pub fn startup_path(&self) -> &Path {
        if let Some(p) = &self.startup_path {
            p.as_path()
        } else {
            Path::new("")
        }
    }

    /// Loads the relevant configuration file.
    ///
    /// This method tries to load the user configuration file. If the user configuration file does
    /// not exist (e.g. first boot after OOBE, FDR, etc), then fallback to loading the factory
    /// configuration file.
    ///
    /// If this method returns successfully, there will be a newly loaded and properly validated
    /// deserialized configuration available.
    pub async fn load_config(&mut self) -> error::Result<()> {
        let (loaded_config, loaded_path) =
            match self.try_load_config(&self.user_config_path()).await {
                Ok(c) => (c, self.paths.user_config_path.to_path_buf()),
                Err(e) => {
                    warn!("Failed to load user config: {}", e);
                    (
                        self.try_load_config(&self.factory_config_path()).await?,
                        self.paths.factory_config_path.to_path_buf(),
                    )
                }
            };
        match self.validate_with_schema(&loaded_config).await {
            Ok(_) => {
                self.device_config = Some(serde_json::from_value(loaded_config).map_err(|e| {
                    error::NetworkManager::Config(error::Config::FailedToDeserializeConfig {
                        path: String::from(loaded_path.to_string_lossy()),
                        error: e.to_string(),
                    })
                })?);
                self.startup_path = Some(loaded_path);
                self.final_validation().await
            }
            Err(e) => Err(e),
        }
    }

    /// Attempts to load a given configuration file.
    ///
    /// Returns the [`serde_json::Value`] of the loaded configuration file. It is important to note
    /// that if the config fails further validation then it will not be used.
    ///
    /// # Errors
    ///
    /// If the config file does not exist, cannot be read, or is not a file, then an error result
    /// of `ConfigNotLoaded` will be returned.
    ///
    /// If the loaded configuration fails to deserialize, then an error result of
    /// `FailedToDeserializeConfig` will be returned.
    async fn try_load_config(&self, config_path: &Path) -> error::Result<Value> {
        info!("Trying to load from: {}", String::from(config_path.to_string_lossy()));
        if config_path.is_file() {
            let mut contents = String::new();
            let mut f = File::open(config_path).map_err(|e| {
                error::NetworkManager::Config(error::Config::ConfigNotLoaded {
                    path: String::from(config_path.to_string_lossy()),
                    error: e.to_string(),
                })
            })?;
            f.read_to_string(&mut contents).map_err(|e| {
                error::NetworkManager::Config(error::Config::ConfigNotLoaded {
                    path: String::from(config_path.to_string_lossy()),
                    error: e.to_string(),
                })
            })?;
            // Note that counter to intuition it is faster to read the full configuration file
            // into memory and deserialize it using serde_json::from_str(), than using
            // serde_json::from_reader(), see: https://github.com/serde-rs/json/issues/160.
            let json: Value = serde_json::from_str(&contents).map_err(|e| {
                error::NetworkManager::Config(error::Config::FailedToDeserializeConfig {
                    path: String::from(config_path.to_string_lossy()),
                    error: e.to_string(),
                })
            })?;
            return Ok(json);
        }
        Err(error::NetworkManager::Config(error::Config::ConfigNotFound {
            path: String::from(config_path.to_string_lossy()),
        }))
    }

    /// Ensure that the configuration validates against the device schema.
    ///
    /// # Errors
    ///
    /// If the actual device schema fails to load, then Valico will emit an appropriate error
    /// message that can be used to fix device schema errors.
    ///
    /// If the config does not validate against the device schema, then an error result
    /// `FailedToValidateConfig` will be returned providing information with what went wrong.
    async fn validate_with_schema(&self, config: &Value) -> error::Result<()> {
        info!("Validating config against the device schema");
        let device_schema = self.try_load_config(&self.device_schema_path()).await?;
        let mut scope = json_schema::Scope::new();
        let schema = scope.compile_and_return(device_schema, false).map_err(|e| {
            error!("Failed to validate schema: {:?}", e);
            error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: String::from(self.device_schema_path().to_string_lossy()),
                error: schema_error(e),
            })
        })?;
        // Use the JSON Schema to give the JSON config some formal structure, but we need to
        // do most of the validation ourselves.
        let result = schema.validate(config);
        if !result.is_strictly_valid() {
            let mut err_msgs = Vec::new();
            for e in &result.errors {
                err_msgs.push(format!("{} at {}", e.get_title(), e.get_path()).into_boxed_str());
            }
            for u in &result.missing {
                err_msgs.push(format!("Device Config missing: {}", u).into_boxed_str());
            }
            // The ordering in which valico emits these errors is unstable. Sort error messages so
            // that the resulting message is predictable.
            err_msgs.sort_unstable();
            return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: String::from(self.startup_path().to_string_lossy()),
                error: err_msgs.join(", "),
            }));
        }
        Ok(())
    }

    /// Validates an [`config::Interface`]'s configuration.
    ///
    /// An Interface definition is valid if it has exactly one configuration type (e.g.
    /// subinterface, switched_vlan, or routed_vlan).
    fn validate_interface_config(&self, intf: &Interface) -> error::Result<()> {
        // TODO(cgibson): Try using serde's 'externally tagged' enum representation to remove
        // this validation step: https://serde.rs/enum-representations.html
        match (intf.subinterfaces.as_ref(), intf.switched_vlan.as_ref(), intf.routed_vlan.as_ref())
        {
            (Some(_), None, None) | (None, Some(_), None) | (None, None, Some(_)) => Ok(()),
            _ => Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: String::from(self.startup_path().to_string_lossy()),
                error: concat!(
                    "Interface must be exactly one of either: ",
                    "'subinterfaces', 'routed_vlan', or 'switched_vlan'",
                )
                .to_string(),
            })),
        }
    }

    /// Validates a [`config::InterfaceType`].
    ///
    /// If an Interface's type is [`InterfaceType::IfUplink`], then the Interface must have a
    /// [`Interface::Subinterfaces`] definition.
    fn validate_interface_types(&self, intf: &Interface) -> error::Result<()> {
        match &intf.config.interface_type {
            InterfaceType::IfUplink => {
                if intf.subinterfaces.is_none() {
                    return Err(error::NetworkManager::Config(
                        error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error: concat!(
                                "Interface type is 'IF_UPLINK' ",
                                "but does not define a 'subinterface'"
                            )
                            .to_string(),
                        },
                    ));
                }
                if let Some(subifs) = intf.subinterfaces.as_ref() {
                    self.validate_subinterfaces(subifs)?;
                }
            }
            InterfaceType::IfEthernet => {
                if intf.subinterfaces.is_none() && intf.switched_vlan.is_none() {
                    return Err(error::NetworkManager::Config(
                        error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error: concat!(
                                "Interface type is 'IF_ETHERNET' ",
                                "but does not define a 'subinterface' nor a 'switched_vlan'"
                            )
                            .to_string(),
                        },
                    ));
                }
                if let Some(subifs) = intf.subinterfaces.as_ref() {
                    self.validate_subinterfaces(subifs)?;
                }
            }
            InterfaceType::IfRoutedVlan => {
                if intf.routed_vlan.is_none() {
                    return Err(error::NetworkManager::Config(
                        error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error: concat!(
                                "Interface type is 'IF_ROUTED_VLAN' but does ",
                                "not define a 'routed_vlan'"
                            )
                            .to_string(),
                        },
                    ));
                }
                // TODO(dpradilla): Fix Router vlan validationi. It is incomplete.
                // Validation should be similar to validate_subinterfaces.
            }
            InterfaceType::IfLoopback => {
                if intf.subinterfaces.is_none() {
                    return Err(error::NetworkManager::Config(
                        error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error: concat!(
                                "Interface type is 'IF_LOOPBACK' ",
                                "but does not define a 'subinterface'"
                            )
                            .to_string(),
                        },
                    ));
                }
            }
            // Add additional type validation here.
            t => {
                return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                    path: String::from(self.startup_path().to_string_lossy()),
                    error: format!("Interface type {:?} not supported", t),
                }))
            }
        }
        Ok(())
    }

    /// Validates an [`config::IpAddress`].
    fn validate_ip_address(&self, addr: &IpAddress) -> error::Result<()> {
        let has_static = addr.cidr_address.is_some();
        let has_dhcp_client = addr.dhcp_client.unwrap_or(false);
        if has_static ^ has_dhcp_client {
            Ok(())
        } else {
            Err(error::NetworkManager::Config(error::Config::Malformed {
                msg: format!("Invalid IpAddress configuration: {:?}", addr),
            }))
        }
    }

    /// Validates each [`config::Subinterface`].
    fn validate_subinterfaces(&self, subinterfaces: &[Subinterface]) -> error::Result<()> {
        for subif in subinterfaces.iter() {
            if let Some(v4addr) = &subif.ipv4 {
                v4addr
                    .dhcp_server
                    .as_ref()
                    .map_or(Ok(()), |x| x.validate(&self.startup_path().to_string_lossy()))?;
                let mut pool_in_range = v4addr.dhcp_server.as_ref().map_or(true, |d| !d.enabled);
                for a in v4addr.addresses.iter() {
                    self.validate_ip_address(&a)?;
                    if let Some(dhcp_server) = &v4addr.dhcp_server {
                        if a.dhcp_client.unwrap_or(false) {
                            return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error: "configuring dhcp client and server on same interface is invalid".to_string(),
                            }));
                        }
                        if let Some(cidr_addr) = a.cidr_address.as_ref() {
                            let addr = LifIpAddr {
                                address: cidr_addr.ip.0,
                                prefix: cidr_addr.prefix_length,
                            };
                            pool_in_range |= dhcp_server.validate_addresses(&addr);
                        }
                    }
                }
                if !pool_in_range {
                    return Err(error::NetworkManager::Config(
                        error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error: "DhcpPool is not related to any IP address".to_string(),
                        },
                    ));
                }
            }
            if let Some(v6addr) = &subif.ipv6 {
                for a in v6addr.addresses.iter() {
                    self.validate_ip_address(&a)?;
                }
            }
        }
        Ok(())
    }

    /// Validates an [`config::Interface`] configuration.
    fn validate_interface(&self, intf: &Interface) -> error::Result<()> {
        self.validate_interface_config(&intf)?;
        self.validate_interface_types(&intf)?;
        Ok(())
    }

    /// Performs an additional layer of validation checks that cannot be expressed in JSON Schema.
    async fn final_validation(&self) -> error::Result<()> {
        let mut intf_names = HashSet::new();
        let intfs = match self.interfaces() {
            Some(intfs) => intfs,
            None => {
                return Err(error::NetworkManager::Config(error::Config::NotFound {
                    msg: "Config contains no interfaces".to_string(),
                }));
            }
        };
        if let Some(default_intf) = self.default_interface() {
            if default_intf.device_id.is_some() {
                return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                    path: String::from(self.startup_path().to_string_lossy()),
                    error: "The default_interface cannot contain a device_id".to_string(),
                }));
            }
            match default_intf.config.interface_type {
                InterfaceType::IfUplink | InterfaceType::IfEthernet => (),
                InterfaceType::IfAggregate
                | InterfaceType::IfLoopback
                | InterfaceType::IfRoutedVlan
                | InterfaceType::IfSonet
                | InterfaceType::IfTunnelGre4
                | InterfaceType::IfTunnelGre6 => {
                    return Err(error::NetworkManager::Config(error::Config::NotSupported {
                        msg: "default_interface type must be 'IF_UPLINK' or 'IF_ETHERNET'"
                            .to_string(),
                    }))
                }
            }
        }
        for intfs in intfs.iter() {
            self.validate_interface(&intfs.interface)?;

            // Interface names must be unique.
            if intf_names.contains(&intfs.interface.config.name) {
                return Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                    path: String::from(self.startup_path().to_string_lossy()),
                    error: format!(
                        "Duplicate interface names detected: '{}'",
                        intfs.interface.config.name
                    ),
                }));
            }
            intf_names.insert(intfs.interface.config.name.clone());
        }
        Ok(())
    }

    /// Returns [`config::Device`] from the config.
    pub fn device(&self) -> error::Result<&Device> {
        self.device_config.as_ref().map(|c| &c.device).ok_or_else(|| {
            error::NetworkManager::Config(error::Config::NotFound {
                msg: "Device was not found yet. Is the config loaded?".to_string(),
            })
        })
    }

    /// Returns all the configured [`config::Interfaces`].
    pub fn interfaces(&self) -> Option<&Vec<Interfaces>> {
        self.device().ok().and_then(|x| x.interfaces.as_ref())
    }

    /// Returns the default [`config::Interface`], if provided.
    pub fn default_interface(&self) -> Option<&Interface> {
        self.device().ok().and_then(|x| x.default_interface.as_ref())
    }

    /// Returns the configured [`config::Acls`].
    pub fn acls(&self) -> Option<&Acls> {
        self.device().ok().and_then(|x| x.acls.as_ref())
    }

    /// Returns all configured [`config::AclEntry`]'s for the given topological path.
    pub fn get_acl_entries<'a>(
        &'a self,
        topo_path: &'a str,
    ) -> Option<impl Iterator<Item = &'a AclEntry> + 'a> {
        self.acls().map(|acls| {
            acls.acl_entries.iter().filter_map(move |entry| {
                return if let Some(device_id) = &entry.config.device_id {
                    if !topo_path.contains(device_id) {
                        info!(
                            "No matching filter rule found for: device_id: {} in topo_path: {}",
                            device_id, topo_path
                        );
                        return None;
                    }
                    info!(
                        "Matched new filter rule: device_id: {} in topo_path: {}",
                        device_id, topo_path
                    );
                    Some(entry)
                } else {
                    info!("Matched new global filter rule for topo_path: {}", topo_path);
                    Some(entry)
                };
            })
        })
    }

    /// Returns the [`config::Interface`] that matches the device_id contained in `topo_path`.
    pub fn get_interface_by_device_id(&self, topo_path: &str) -> Option<&Interface> {
        if let Some(ifs) = self.interfaces() {
            for intfs in ifs.iter() {
                if let Some(d) = &intfs.interface.device_id {
                    if topo_path.contains(d.as_str()) {
                        return Some(&intfs.interface);
                    }
                }
            }
        }
        // If no default interface is configured, this will result in `None` being returned.
        self.default_interface()
    }

    /// Returns `true` if the device id from the `topo_path` is an uplink.
    ///
    /// An "uplink" is defined by having an [`InterfaceType::IfUplink`] and having
    /// a "subinterface" definition.
    pub fn device_id_is_an_uplink(&self, topo_path: &str) -> bool {
        self.get_interface_by_device_id(topo_path)
            .map(|intf| match intf.config.interface_type {
                InterfaceType::IfUplink => intf.subinterfaces.is_some(),
                _ => false,
            })
            .unwrap_or(false)
    }

    /// Returns `true` if the device id from the `topo_path` is a downlink.
    ///
    /// A "downlink" is an L3 interface that is not configured as uplink. That is,
    /// it's of type  `InterfaceType::IfEthernet`] and has at least one "subinterface".
    pub fn device_id_is_a_downlink(&self, topo_path: &str) -> bool {
        self.get_interface_by_device_id(topo_path)
            .map(|intf| match intf.config.interface_type {
                InterfaceType::IfEthernet => intf.subinterfaces.is_some(),
                _ => false,
            })
            .unwrap_or(false)
    }

    /// Returns name of the interface at topo_path.
    ///
    /// If there is a `default_interface` configured, then use the topological path name instead.
    pub fn get_interface_name(&self, topo_path: &str) -> error::Result<String> {
        if self.is_unknown_device_id(topo_path) && self.default_interface().is_some() {
            // TODO(fxbug.dev/51107): This special case is needed because LIF manager seems to enforce that
            // names be unique across the system. If more than one unconfigured interface were to be
            // connected then LIF manager would refuse to register the device. We should revisit
            // this decision to see if it is still valid.
            return Ok(topo_path.to_owned());
        }
        if let Some(intf) = self.get_interface_by_device_id(topo_path) {
            return Ok(intf.config.name.clone());
        }
        Err(error::NetworkManager::Config(error::Config::NotFound {
            msg: format!("Getting interface name for {} failed.", topo_path),
        }))
    }

    /// Returns a tuple of IPv4 and IPv6 [`config::IpAddress`]'s for this interface.
    ///
    /// The 'switched_vlan' configuration does not support IP addressing, so any `Interface` that
    /// has a 'switched_vlan' configuration will return `None`.
    pub fn get_ip_address<'a>(
        &self,
        intf: &'a Interface,
    ) -> error::Result<(Option<&'a IpAddress>, Option<&'a IpAddress>)> {
        if let Some(subifs) = &intf.subinterfaces {
            if subifs.is_empty() {
                return Err(error::NetworkManager::Config(error::Config::Malformed {
                    msg: "Interface must have at least one \'subinterface\' definition".to_string(),
                }));
            }
            // TODO(cgibson): LIFProperties doesn't support vectors of IP addresses yet. fxbug.dev/42315.
            if subifs.len() != 1 {
                warn!("LIFProperties does not support multiple addresses yet.")
            }
            let subif = &subifs[0];
            let v4addr = subif.ipv4.as_ref().and_then(|c| c.addresses.iter().next());
            let v6addr = subif.ipv6.as_ref().and_then(|c| c.addresses.iter().next());
            return Ok((v4addr, v6addr));
        }
        Err(error::NetworkManager::Config(error::Config::NotFound {
            msg: format!(
                "Could not find an IP config defined on the provided interface: {:?}",
                intf
            ),
        }))
    }

    /// Updates [`lifmgr::LIFProperties`] with the given IP address configuration.
    ///
    /// In an IPv4-only network, with DHCP client disabled, and no static IP address configuration,
    /// then there will be no address configured for this interface.
    ///
    /// In an IPv6-only network, we currently have no plans to support DHCPv6 and there is no
    /// static IPv6 address configuration, we will rely on SLAAC to get a V6 address.
    ///
    /// Blended V4/V6 networks with IPv4 DHCP client disabled and no static IP address will rely on
    /// SLAAC to get an IPv6 address.
    ///
    /// If `ipconfig` is `None` then it is the same as an IPv6-only network, we rely on SLAAC to get
    /// an address.
    fn set_ip_address_config(
        &self,
        properties: &mut lifmgr::LIFProperties,
        ipconfig: Option<&IpAddress>,
    ) {
        if let Some(c) = ipconfig {
            if let Some(dhcp_client) = c.dhcp_client {
                // TODO(dpradilla): do not allow this if dhcp_server configuration is present.
                properties.dhcp =
                    if dhcp_client { lifmgr::Dhcp::Client } else { lifmgr::Dhcp::None };
            }

            // TODO(fxbug.dev/42315): LIFProperties doesn't support IPv6 addresses yet.
            if let Some(addr) = c.cidr_address.as_ref() {
                properties.address_v4 =
                    Some(LifIpAddr { address: addr.ip.0, prefix: addr.prefix_length });
            }
        }

        // TODO(fxbug.dev/42315): LIF manager throws an error if both a DHCP client and a static IP address
        // configuration are set. We don't want to be generating invalid LIFProperties, so favor
        // the static IP configuration and turn off DHCP.
        if properties.dhcp == lifmgr::Dhcp::Client && properties.address_v4.is_some() {
            warn!(
                "DHCP client and static IP cannot be configured at the same time: Disabling DHCP."
            );
            properties.dhcp = lifmgr::Dhcp::None;
        }
    }

    /// Returns a LAN-specific [`lifmgr::LIFProperties`] based on the running configuration.
    pub fn create_lan_properties(&self, topo_path: &str) -> error::Result<lifmgr::LIFProperties> {
        let properties = crate::lifmgr::LIFProperties::default();
        self.create_properties(topo_path, properties)
    }

    /// Returns a WAN-specific [`lifmgr::LIFProperties`] based on the running configuration.
    pub fn create_wan_properties(&self, topo_path: &str) -> error::Result<lifmgr::LIFProperties> {
        let properties =
            crate::lifmgr::LIFProperties { dhcp: lifmgr::Dhcp::Client, ..Default::default() };
        self.create_properties(topo_path, properties)
    }

    /// Discovers the interface that matches the device ID from the topological path and sets
    /// LIFProperties for the admin state of the interface as well as configures the IP address.
    /// Properties that are not explicitly specified in the configuration take the values indicated
    /// in `properties`
    fn create_properties(
        &self,
        topo_path: &str,
        mut properties: lifmgr::LIFProperties,
    ) -> error::Result<lifmgr::LIFProperties> {
        info!("create_properties: {:?}", topo_path);
        let intf = match self.get_interface_by_device_id(topo_path) {
            Some(x) => x,
            None => {
                return Err(error::NetworkManager::Config(error::Config::Malformed {
                    msg: format!(
                        "Cannot find an Interface matching `device_id` from topo path: {}",
                        topo_path
                    ),
                }));
            }
        };
        let subifs = match &intf.subinterfaces {
            Some(subif) => subif,
            None => {
                return Err(error::NetworkManager::Config(error::Config::Malformed {
                    msg: "An uplink must have a \'subinterface\' configuration".to_string(),
                }));
            }
        };

        // TODO(fxbug.dev/42315): LIFProperties does not support multiple addresses yet.
        if subifs.len() != 1 {
            return Err(error::NetworkManager::Config(error::Config::NotSupported {
                msg: "Multiple subinterfaces on a single interface are not supported".to_string(),
            }));
        }

        if let Some(subif) = subifs.get(0) {
            match subif.admin_state {
                Some(AdminState::Up) => {
                    properties.enabled = true;
                }
                Some(AdminState::Down) => {
                    warn!("WAN subinterface is admin down by config");
                    properties.enabled = false;
                }
                Some(AdminState::Testing) => {
                    warn!("Admin state 'TESTING' is not supported; defaulting to 'Up'");
                    properties.enabled = true;
                }
                _ => {} // State not indicated, use the default passed in.
            }
        }

        let (v4addr, v6addr) = self.get_ip_address(intf)?;
        // TODO(fxbug.dev/42316): LIFProperties doesn't support IPv6 addresses yet.
        if v6addr.is_some() {
            warn!("Setting IPv6 addresses is not supported yet");
        }
        self.set_ip_address_config(&mut properties, v4addr);
        if let Some(dhcp) = intf.get_dhcp_server_config() {
            if properties.dhcp != lifmgr::Dhcp::Client {
                properties.dhcp = lifmgr::Dhcp::Server;
                properties.dhcp_config = dhcp.clone().try_into().ok();
            } else {
                warn!("Ignoring DHCP server configuration as DHCP client is configured.");
            }
        }
        Ok(properties)
    }

    /// Returns a vector of [`config::Interface`]'s that contain a [`config::RoutedVlan`].
    ///
    /// If no `Interface`'s are defined as being a `RoutedVlan`, then this method will return an
    /// empty Iterator.
    ///
    /// If there are no interfaces configured, then this method returns `None`.
    pub fn get_routed_vlan_interfaces(&self) -> Option<impl Iterator<Item = &Interface>> {
        self.interfaces().map(|ifs| {
            ifs.iter().filter_map(|intfs| {
                if intfs.interface.routed_vlan.is_some() {
                    Some(&intfs.interface)
                } else {
                    None
                }
            })
        })
    }

    /// Returns the [`SwitchedVlan`] that matches the device id contained in `topo_path`.
    pub fn get_switched_vlan_by_device_id(&self, topo_path: &str) -> error::Result<&SwitchedVlan> {
        self.get_interface_by_device_id(topo_path)
            .and_then(|intf| intf.switched_vlan.as_ref())
            .ok_or_else(|| {
                error::NetworkManager::Config(error::Config::NotFound {
                    msg: format!("Failed to find interface matching: {}", topo_path),
                })
            })
    }

    /// Returns `true` if `topo_path` resolves to a [`config::SwitchedVlan`].
    pub fn device_id_is_a_switched_vlan(&self, topo_path: &str) -> bool {
        self.get_switched_vlan_by_device_id(topo_path).is_ok()
    }

    /// Returns true if `topo_path` does not appear in the config.
    pub fn is_unknown_device_id(&self, topo_path: &str) -> bool {
        if let Some(ifs) = self.interfaces() {
            for intfs in ifs.iter() {
                if let Some(device_id) = &intfs.interface.device_id {
                    if device_id.as_str() == topo_path {
                        return false;
                    }
                }
            }
        }
        true
    }

    /// Resolves a [`config::SwitchedVlan`] to it's [`config::RoutedVlan`] configuration.
    ///
    /// # Errors
    ///
    /// If the switched VLAN does not resolve to a routed VLAN, then a `NotFound` error will be
    /// returned.
    ///
    /// If the switched VLAN port does not have the correct configuration based on its interface
    /// mode, then a `Malformed` error will be returned.
    pub fn resolve_to_routed_vlans(
        &self,
        switched_vlan: &SwitchedVlan,
    ) -> error::Result<&RoutedVlan> {
        let mut switched_vlan_ids = Vec::new();
        match switched_vlan.interface_mode {
            InterfaceMode::Access => match switched_vlan.access_vlan {
                Some(vid) => switched_vlan_ids.push(vid),
                None => {
                    return Err(error::NetworkManager::Config(error::Config::Malformed {
                        msg: format!(
                            "Expecting access port to have 'access_vlan': {:?}",
                            switched_vlan
                        ),
                    }));
                }
            },
            // TODO(cgibson): Implement trunk VLANs when netstack supports it.
            InterfaceMode::Trunk => {
                return Err(error::NetworkManager::Config(error::Config::NotSupported {
                    msg: "Trunk VLANs are not supported yet.".to_string(),
                }));
            }
        }
        if let Some(it) = self.get_routed_vlan_interfaces() {
            for intf in it {
                // Safe to unwrap() here because `get_routed_vlan_interfaces()` checked for us
                // already.
                let routed_vlan = intf.routed_vlan.as_ref().unwrap();
                if switched_vlan_ids.contains(&routed_vlan.vlan_id) {
                    return Ok(&routed_vlan);
                }
            }
        }
        Err(error::NetworkManager::Config(error::Config::NotFound {
            msg: format!(
                "Switched VLAN port does not resolve to a routed VLAN: {:?}",
                switched_vlan
            ),
        }))
    }

    /// Checks that all `ports` resolve to a single [`config::RoutedVlan`].
    pub fn all_ports_have_same_bridge<'a>(
        &'a self,
        ports: impl Iterator<Item = error::Result<&'a SwitchedVlan>>,
    ) -> error::Result<&'a RoutedVlan> {
        let mut ports = ports.peekable();
        if ports.peek().is_none() {
            warn!("Provided list of ports was empty?");
            return Err(error::NetworkManager::Config(error::Config::Malformed {
                msg: "Provided list of ports was empty?".to_string(),
            }));
        }
        let mut routed_vlan = None;
        for p in ports {
            let r = self.resolve_to_routed_vlans(p?)?;
            routed_vlan = match routed_vlan {
                None => Some(r),
                Some(v) => {
                    if v == r {
                        Some(r)
                    } else {
                        return Err(error::NetworkManager::Config(error::Config::Malformed {
                            msg: "switched_vlan ports do not resolve to the same RoutedVlan"
                                .to_string(),
                        }));
                    }
                }
            }
        }
        routed_vlan.ok_or_else(|| {
            error::NetworkManager::Config(error::Config::Malformed {
                msg: "switched_vlan ports do not resolve to the same RoutedVlan".to_string(),
            })
        })
    }

    /// Returns the name of the bridge.
    pub fn get_bridge_name(&self, target: &RoutedVlan) -> &str {
        self.get_routed_vlan_interfaces()
            .and_then(|mut vlan_ifs| {
                vlan_ifs.find_map(|intf| {
                    intf.routed_vlan.as_ref().and_then(|vlan| {
                        if vlan.vlan_id == target.vlan_id {
                            Some(intf.config.name.as_str())
                        } else {
                            None
                        }
                    })
                })
            })
            .unwrap_or(UNNAMED_BRIDGE)
    }

    /// Creates a new [`lifmgr::LIFProperties`] for the provided `RoutedVlan`.
    pub fn create_routed_vlan_properties(
        &self,
        bridge: &RoutedVlan,
    ) -> error::Result<lifmgr::LIFProperties> {
        let mut properties = crate::lifmgr::LIFProperties { enabled: true, ..Default::default() };

        // TODO(fxbug.dev/42316): LIFProperties doesn't support IPv6 addresses yet.
        if bridge.ipv6.is_some() {
            warn!("Setting IPv6 addresses is not supported yet.");
        }
        bridge.ipv4.as_ref().and_then(|c| {
            if c.addresses.len() > 1 {
                warn!("Configuring multiple IPv4 addresses is not supported yet");
            }
            Some(())
        });
        let v4addr = bridge.ipv4.as_ref().and_then(|c| c.addresses.iter().next());
        self.set_ip_address_config(&mut properties, v4addr);

        Ok(properties)
    }

    /// Returns configured VLAN IDs for the given device that matches `topo_path`.
    ///
    /// If the interface is a `SwitchedVlan` then the VLAN IDs will be depend on if the interface
    /// mode is either a `trunk` or `access` port.
    ///
    /// If the interface is a `RoutedVlan` then the VLAN ID will be a vector of a single element
    /// containing the `routed_vlan`'s VLAN ID.
    ///
    /// If the interface is a `subinterface` then the vector will be empty.
    pub fn get_vlans(&self, topo_path: &str) -> Vec<u16> {
        let mut vids = Vec::new();
        if let Some(intf) = self.get_interface_by_device_id(topo_path) {
            match &intf.routed_vlan {
                Some(r) => {
                    // routed_vlan's have a single VLAN ID only.
                    return vec![r.vlan_id];
                }
                None => {}
            }

            match &intf.switched_vlan {
                Some(switched_vlan) => match switched_vlan.interface_mode {
                    InterfaceMode::Access => {
                        if let Some(access_vlan) =
                            intf.switched_vlan.as_ref().and_then(|x| x.access_vlan)
                        {
                            vids.push(access_vlan);
                        }
                    }
                    InterfaceMode::Trunk => {
                        if let Some(trunk_vlans) =
                            intf.switched_vlan.as_ref().and_then(|x| x.trunk_vlans.as_ref())
                        {
                            vids.extend(trunk_vlans.iter().cloned());
                        }
                    }
                },
                None => (),
            }
        }
        vids
    }

    /// Returns the [`config::Services`] definition.
    ///
    /// # Errors
    ///
    /// If there is no "services" definition in the configuration, returns a
    /// [`error::Config::NotFound`] error.
    fn get_services(&self) -> error::Result<&Services> {
        self.device()?.services.as_ref().ok_or_else(|| {
            error::NetworkManager::Config(error::Config::NotFound {
                msg: "\'services\' definition was not found".to_string(),
            })
        })
    }

    /// Returns the IP forwarding configuration.
    ///
    /// If IP forwarding is enabled in the configuration, then this method will return true.
    pub fn get_ip_forwarding_state(&self) -> bool {
        self.get_services().ok().and_then(|s| s.ip_forwarding).unwrap_or(false)
    }

    /// Returns the current NAT configuration.
    ///
    /// If NAT is enabled in the configuration, then this method will return true. NAT is disabled
    /// by default.
    pub fn get_nat_state(&self) -> bool {
        self.get_services().ok().and_then(|s| s.nat).unwrap_or(false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use std::fs;

    fn create_test_config_no_paths() -> Config {
        Config::new("/user", "/factory", "/device_schema")
    }

    fn create_test_config(user_cfg: &str, factory_cfg: &str, device_schema: &str) -> Config {
        Config::new(user_cfg, factory_cfg, device_schema)
    }

    fn create_test_interface() -> Interface {
        Interface {
            config: InterfaceConfig {
                name: "test_eth".to_string(),
                interface_type: InterfaceType::IfEthernet,
            },
            ethernet: None,
            tcp_offload: None,
            device_id: Some("device_id".to_string()),
            oper_state: Some(OperState::Up),
            subinterfaces: Some(vec![Subinterface {
                admin_state: Some(AdminState::Up),
                ipv4: Some(IpAddressConfig {
                    addresses: vec![IpAddress { dhcp_client: Some(true), cidr_address: None }],
                    dhcp_server: None,
                }),
                ipv6: None,
            }]),
            switched_vlan: None,
            routed_vlan: None,
        }
    }

    fn build_full_config() -> DeviceConfig {
        DeviceConfig {
            device: Device {
                default_interface: Some(Interface {
                    config: InterfaceConfig {
                        name: "default_dhcp_policy".to_string(),
                        interface_type: InterfaceType::IfUplink,
                    },
                    oper_state: None,
                    device_id: None,
                    ethernet: None,
                    tcp_offload: None,
                    routed_vlan: None,
                    switched_vlan: None,
                    subinterfaces: Some(vec![Subinterface {
                        admin_state: Some(AdminState::Up),
                        ipv4: Some(IpAddressConfig {
                            addresses: vec![IpAddress {
                                dhcp_client: Some(true),
                                cidr_address: None,
                            }],
                            dhcp_server: None,
                        }),
                        ipv6: None,
                    }]),
                }),
                interfaces: Some(vec![
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_wan_no_admin_state".to_string(),
                                interface_type: InterfaceType::IfUplink,
                            },
                            oper_state: None,
                            device_id: Some("test_wan_no_admin_state_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: None,
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: None,
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                            prefix_length: 24,
                                        }),
                                    }],
                                    dhcp_server: None,
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_wan_up".to_string(),
                                interface_type: InterfaceType::IfUplink,
                            },
                            oper_state: None,
                            device_id: Some("test_wan_up_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: Some(AdminState::Up),
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: None,
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                            prefix_length: 24,
                                        }),
                                    }],
                                    dhcp_server: None,
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "bridge".to_string(),
                                interface_type: InterfaceType::IfRoutedVlan,
                            },
                            oper_state: None,
                            device_id: Some("routed_vlan".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            subinterfaces: None,
                            switched_vlan: None,
                            routed_vlan: Some(RoutedVlan {
                                vlan_id: 2,
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: Some(false),
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.168.0.1".parse().unwrap()),
                                            prefix_length: 32,
                                        }),
                                    }],
                                    dhcp_server: Some(DhcpServer {
                                        enabled: true,
                                        dhcp_pool: DhcpPool {
                                            lease_time: "1d".to_string(),
                                            start: "192.168.0.100".parse().unwrap(),
                                            end: "192.168.0.254".parse().unwrap(),
                                        },
                                        static_ip_allocations: None,
                                    }),
                                }),
                                ipv6: None,
                            }),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_eth".to_string(),
                                interface_type: InterfaceType::IfEthernet,
                            },
                            oper_state: None,
                            device_id: Some("switched_vlan".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            subinterfaces: None,
                            switched_vlan: Some(SwitchedVlan {
                                interface_mode: InterfaceMode::Access,
                                access_vlan: Some(2),
                                trunk_vlans: None,
                            }),
                            routed_vlan: None,
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_wan_down".to_string(),
                                interface_type: InterfaceType::IfUplink,
                            },
                            oper_state: None,
                            device_id: Some("test_wan_down_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: Some(AdminState::Down),
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: None,
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                            prefix_length: 24,
                                        }),
                                    }],
                                    dhcp_server: None,
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_wan_dhcp".to_string(),
                                interface_type: InterfaceType::IfUplink,
                            },
                            oper_state: None,
                            device_id: Some("test_wan_dhcp_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: Some(AdminState::Up),
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: Some(true),
                                        cidr_address: None,
                                    }],
                                    dhcp_server: None,
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_lan_no_admin_state".to_string(),
                                interface_type: InterfaceType::IfEthernet,
                            },
                            oper_state: None,
                            device_id: Some("test_lan_no_admin_state_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: None,
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: None,
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                            prefix_length: 24,
                                        }),
                                    }],
                                    dhcp_server: None,
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_lan_up".to_string(),
                                interface_type: InterfaceType::IfEthernet,
                            },
                            oper_state: None,
                            device_id: Some("test_lan_up_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: Some(AdminState::Up),
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: None,
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                            prefix_length: 24,
                                        }),
                                    }],
                                    dhcp_server: None,
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_lan_down".to_string(),
                                interface_type: InterfaceType::IfEthernet,
                            },
                            oper_state: None,
                            device_id: Some("test_lan_down_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: Some(AdminState::Down),
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: None,
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                            prefix_length: 24,
                                        }),
                                    }],
                                    dhcp_server: None,
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_lan_no_subint".to_string(),
                                interface_type: InterfaceType::IfEthernet,
                            },
                            oper_state: None,
                            device_id: Some("test_lan_no_subint_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: None,
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_lan_has_dhcp_server".to_string(),
                                interface_type: InterfaceType::IfEthernet,
                            },
                            oper_state: None,
                            device_id: Some("test_lan_has_dhcp_server_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: Some(AdminState::Up),
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: None,
                                        cidr_address: Some(CidrAddress {
                                            ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                            prefix_length: 24,
                                        }),
                                    }],
                                    dhcp_server: Some(DhcpServer {
                                        enabled: true,
                                        dhcp_pool: DhcpPool {
                                            start: "192.0.2.100".parse().unwrap(),
                                            end: "192.0.2.254".parse().unwrap(),
                                            lease_time: "1d".to_string(),
                                        },
                                        static_ip_allocations: None,
                                    }),
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                ]),
                services: Some(Services { ip_forwarding: Some(true), nat: Some(true) }),
                acls: Some(Acls {
                    acl_entries: vec![
                        AclEntry {
                            config: FilterConfig {
                                forwarding_action: ForwardingAction::Drop,
                                device_id: Some("test_wan_up_id".to_string()),
                                direction: Some(Direction::In),
                                comment: None,
                            },
                            ipv4: Some(IpFilter {
                                src_address: None,
                                dst_address: Some("192.168.0.0/24".to_string().parse().unwrap()),
                                src_ports: None,
                                dst_ports: Some(PortRange { from: 8080, to: 8081 }),
                                protocol: Some(Protocol::Any),
                            }),
                            ipv6: None,
                        },
                        AclEntry {
                            config: FilterConfig {
                                forwarding_action: ForwardingAction::Drop,
                                device_id: Some("test_lan_up_id".to_string()),
                                direction: None,
                                comment: Some("Block traffic to sshd from the wlan".to_string()),
                            },
                            ipv4: Some(IpFilter {
                                src_address: None,
                                src_ports: None,
                                dst_address: Some("0.0.0.0/0".to_string().parse().unwrap()),
                                dst_ports: Some(PortRange { from: 2222, to: 2222 }),
                                protocol: Some(Protocol::Any),
                            }),
                            ipv6: None,
                        },
                    ],
                }),
            },
        }
    }

    #[test]
    fn test_new() {
        let user_cfg = "/data/my/user.cfg";
        let factory_cfg = "/data/my/factory.cfg";
        let device_schema = "/data/my/device_schema.cfg";
        let test_config = create_test_config(user_cfg, factory_cfg, device_schema);

        assert_eq!(test_config.device_config.is_none(), true);
        assert_eq!(test_config.paths.user_config_path, Path::new(user_cfg).to_path_buf());
        assert_eq!(test_config.paths.factory_config_path, Path::new(factory_cfg).to_path_buf());
        assert_eq!(test_config.paths.device_schema_path, Path::new(device_schema).to_path_buf());
    }

    #[test]
    fn test_get_paths() {
        let user_cfg = "/data/my/user.cfg";
        let factory_cfg = "/data/my/factory.cfg";
        let device_schema = "/data/my/device_schema.cfg";
        let test_config = create_test_config(user_cfg, factory_cfg, device_schema);

        assert_eq!(test_config.device_config.is_none(), true);
        assert_eq!(test_config.user_config_path(), Path::new(user_cfg));
        assert_eq!(test_config.factory_config_path(), Path::new(factory_cfg));
        assert_eq!(test_config.device_schema_path(), Path::new(device_schema));
        assert_eq!(test_config.startup_path(), Path::new(""));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_config_file() {
        let test_config = create_test_config_no_paths();

        // Missing config should raise an `error::Config::ConfigNotFound`.
        assert_eq!(
            test_config.try_load_config(Path::new("/doesntexist")).await,
            Err(error::NetworkManager::Config(error::Config::ConfigNotFound {
                path: "/doesntexist".to_string()
            }))
        );

        // An invalid config should fail to deserialize.
        let invalid_empty = String::from("/pkg/data/invalid_factory_configs/invalid_empty.json");
        assert_eq!(
            test_config.try_load_config(Path::new(&invalid_empty)).await,
            Err(error::NetworkManager::Config(error::Config::FailedToDeserializeConfig {
                path: "/pkg/data/invalid_factory_configs/invalid_empty.json".to_string(),
                error: "EOF while parsing an object at line 2 column 0".to_string(),
            }))
        );

        // A valid config should deserialize successfully.
        let valid_empty = String::from("/pkg/data/valid_factory_configs/valid_empty.json");
        let contents = fs::read_to_string(&valid_empty)
            .expect(format!("Failed to open testdata file: {}", valid_empty).as_str());

        // The expected configuration should deserialize successfully.
        let expected_config: Value = serde_json::from_str(&contents).unwrap();
        assert_eq!(
            test_config.try_load_config(Path::new(&valid_empty)).await.unwrap(),
            expected_config
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_validate_schema() {
        let test_config =
            create_test_config("/doesntmatter", "/doesntmatter", "/pkg/data/device_schema.json");

        let valid_config = r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "subinterfaces": [
                      {
                        "ipv4": {
                          "addresses": [
                            {
                              "dhcp_client": false,
                              "ip": "1.1.1.1",
                              "prefix_length": 24
                            }
                          ],
                          "dhcp_server": {
                            "properties": {
                              "enabled": true,
                              "lease_time": "1d",
                              "dhcp_pool": {
                                "start": "1.1.1.100",
                                "end": "1.1.1.254"
                              },
                              "static_ip_allocations": {
                                "name": "device1",
                                "ip_address": "1.1.1.200",
                                "mac_address": "00:01:02:03:04:05"
                              }
                            }
                          }
                        }
                      }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#;
        let expected_config: Value;
        match serde_json::from_str(&valid_config) {
            Ok(j) => expected_config = j,
            Err(e) => panic!("Want: {:?} Got unexpected error result: {}", valid_config, e),
        }

        // Make sure that the configuration actually validates.
        assert!(test_config.validate_with_schema(&expected_config).await.is_ok());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verify_toulouse_factory_config() {
        let config_path = "/pkg/data/valid_factory_configs/toulouse.json";
        let mut contents = String::new();
        let mut f = File::open(config_path).unwrap();
        f.read_to_string(&mut contents).unwrap();
        let deserialized_config = serde_json::from_str(&contents)
            .expect(format!("Failed to deserialized {}", config_path).as_ref());
        let mut test_config =
            create_test_config("/doesntmatter", config_path, "/pkg/data/device_schema.json");
        // Make sure that the configuration actually validates.
        match test_config.validate_with_schema(&deserialized_config).await {
            Ok(()) => {
                test_config.device_config =
                    Some(serde_json::from_value(deserialized_config).unwrap())
            }
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
        assert!(test_config.final_validation().await.is_ok());
    }

    #[test]
    fn verify_factory_configs() {
        // Verify that /pkg/data/valid_factory_configs/* are all valid configs.
        let entries = std::fs::read_dir("/pkg/data/valid_factory_configs/")
            .expect("could not list /pkg/data/valid_factory_configs/");
        for entry in entries {
            let entry = entry.expect("could not list /pkg/data/valid_factory_configs/");
            let mut contents = String::new();
            let mut f = File::open(entry.path()).unwrap();
            f.read_to_string(&mut contents).unwrap();
            let _deserialized_config: DeviceConfig = serde_json::from_str(&contents)
                .expect(&format!("Failed to deserialized {:?}", entry.path()));
        }
    }

    #[test]
    fn test_validate_interface_types() {
        let test_config = create_test_config_no_paths();
        let mut intf = create_test_interface();

        // If the Interface type is `InterfaceType::IfUplink`, then there must be a 'subinterface'
        // defined.
        intf.config =
            InterfaceConfig { name: "test".to_string(), interface_type: InterfaceType::IfUplink };
        assert!(test_config.validate_interface_types(&intf).is_ok());

        // If the Interface type is `InterfaceType::IfRoutedVlan`, then there must be a
        // 'routed_vlan' defined. Anything else should fail.
        intf.config = InterfaceConfig {
            name: "test".to_string(),
            interface_type: InterfaceType::IfRoutedVlan,
        };
        assert!(test_config.validate_interface_types(&intf).is_err());
    }

    #[test]
    fn test_validate_interface_config() {
        let fake_factory_path = "/fake_factory_path";
        let test_config = create_test_config("/doesntmatter", fake_factory_path, "/doesntmatter");
        let mut intf = create_test_interface();

        // Should pass because an interface must have exactly one configuration.
        assert!(test_config.validate_interface_config(&intf).is_ok());

        // The following should all fail for the same reason as above, exactly one configuration
        // per interface.
        let routed_vlan = Some(RoutedVlan { vlan_id: 1, ipv4: None, ipv6: None });
        let switched_vlan = Some(SwitchedVlan {
            interface_mode: InterfaceMode::Access,
            access_vlan: None,
            trunk_vlans: None,
        });
        let subinterfaces = Subinterface { admin_state: None, ipv4: None, ipv6: None };

        intf.routed_vlan = routed_vlan.clone();
        intf.subinterfaces = Some(vec![subinterfaces.clone()]);
        intf.switched_vlan = switched_vlan.clone();
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = None;
        intf.subinterfaces = Some(vec![subinterfaces.clone()]);
        intf.switched_vlan = switched_vlan.clone();
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = routed_vlan.clone();
        intf.subinterfaces = None;
        intf.switched_vlan = switched_vlan.clone();
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = routed_vlan.clone();
        intf.subinterfaces = Some(vec![subinterfaces.clone()]);
        intf.switched_vlan = None;
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = None;
        intf.subinterfaces = None;
        intf.switched_vlan = None;
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());
    }

    #[test]
    fn test_validate_subinterfaces() {
        let fake_factory_path = "/fake_factory_path";
        let test_config = create_test_config("/doesntmatter", fake_factory_path, "/doesntmatter");

        // Should not fail because `dhcp_client` is set to true.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress { dhcp_client: Some(true), cidr_address: None }],
                dhcp_server: None,
            }),
            ipv6: None,
        }];
        assert!(test_config.validate_subinterfaces(&test_subif).is_ok());

        // Should fail because `dhcp_client` is set to true and has dhcp server configured.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress { dhcp_client: Some(true), cidr_address: None }],
                dhcp_server: Some(DhcpServer {
                    dhcp_pool: DhcpPool {
                        start: "192.168.2.100".parse().unwrap(),
                        end: "192.168.2.254".parse().unwrap(),
                        lease_time: "1d".to_string(),
                    },
                    enabled: true,
                    static_ip_allocations: None,
                }),
            }),
            ipv6: None,
        }];
        assert!(test_config.validate_subinterfaces(&test_subif).is_err());

        // Should not fail with dhcp_client set to false, ip and prefix set to valid values.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: Some(false),
                    cidr_address: Some(CidrAddress {
                        ip: NetIpAddr("127.0.0.1".parse().unwrap()),
                        prefix_length: 32,
                    }),
                }],
                dhcp_server: None,
            }),
            ipv6: None,
        }];
        assert!(test_config.validate_subinterfaces(&test_subif).is_ok());

        // Should fail with dhcp_client set to false, ip and prefix set to None.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress { dhcp_client: Some(false), cidr_address: None }],
                dhcp_server: None,
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Err(error::NetworkManager::Config(error::Config::Malformed { msg: _ })) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
            Ok(_) => panic!("Got unexpected 'Ok' result!"),
        }

        // Should not fail with dhcp client set to None, ip, and prefx set to valid values.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: None,
                    cidr_address: Some(CidrAddress {
                        ip: NetIpAddr("127.0.0.1".parse().unwrap()),
                        prefix_length: 32,
                    }),
                }],
                dhcp_server: None,
            }),
            ipv6: None,
        }];
        assert!(test_config.validate_subinterfaces(&test_subif).is_ok());

        // Should not fail with dhcp client set to None, ip, and prefx set to valid values,
        // and dhcp server.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: None,
                    cidr_address: Some("192.168.2.1/24".parse().unwrap()),
                }],
                dhcp_server: Some(DhcpServer {
                    dhcp_pool: DhcpPool {
                        start: "192.168.2.100".parse().unwrap(),
                        end: "192.168.2.254".parse().unwrap(),
                        lease_time: "1d".to_string(),
                    },
                    enabled: true,
                    static_ip_allocations: None,
                }),
            }),
            ipv6: None,
        }];
        assert!(test_config.validate_subinterfaces(&test_subif).is_ok());

        // Should fail with dhcp client set to None, ip, and prefx set to valid values,
        // but dhcp server pool misconfiguredr.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: None,
                    cidr_address: Some(CidrAddress {
                        ip: NetIpAddr("192.168.2.1".parse().unwrap()),
                        prefix_length: 30,
                    }),
                }],
                dhcp_server: Some(DhcpServer {
                    dhcp_pool: DhcpPool {
                        start: "192.168.2.100".parse().unwrap(),
                        end: "192.168.2.254".parse().unwrap(),
                        lease_time: "1d".to_string(),
                    },
                    enabled: true,
                    static_ip_allocations: None,
                }),
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                path: _,
                error: _,
            })) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
            Ok(_) => panic!("Got unexpected 'Ok' result!"),
        }
        // Should fail because both dhcp_client and ip/prefix_len are None.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress { dhcp_client: None, cidr_address: None }],
                dhcp_server: None,
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Err(error::NetworkManager::Config(error::Config::Malformed { msg: _ })) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
            Ok(_) => panic!("Got unexpected 'Ok' result!"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_validate_dhcp_pool() {
        for (test_case, pool, want_ok) in &[
            (
                "valid pool",
                DhcpPool {
                    start: "192.168.2.10".parse().unwrap(),
                    end: "192.168.2.254".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                true,
            ),
            (
                "only one",
                DhcpPool {
                    start: "192.168.2.10".parse().unwrap(),
                    end: "192.168.2.10".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                true,
            ),
            (
                "two",
                DhcpPool {
                    start: "192.168.2.10".parse().unwrap(),
                    end: "192.168.2.11".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                true,
            ),
            (
                "invalid pool",
                DhcpPool {
                    start: "192.168.2.254".parse().unwrap(),
                    end: "192.168.2.10".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                false,
            ),
            (
                "another invalid pool",
                DhcpPool {
                    start: "192.168.51.10".parse().unwrap(),
                    end: "192.168.0.254".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                false,
            ),
            (
                "valid large pool",
                DhcpPool {
                    start: "192.168.2.10".parse().unwrap(),
                    end: "192.168.4.254".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                true,
            ),
            (
                "invalid large pool",
                DhcpPool {
                    start: "192.168.4.254".parse().unwrap(),
                    end: "192.168.2.10".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                false,
            ),
            (
                "non private range should invalid",
                DhcpPool {
                    start: "12.168.4.100".parse().unwrap(),
                    end: "12.168.4.200".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                false,
            ),
            (
                "localhost range",
                DhcpPool {
                    start: "127.0.0.1".parse().unwrap(),
                    end: "127.0.0.2".parse().unwrap(),
                    lease_time: "1".to_string(),
                },
                false,
            ),
        ] {
            let got = pool.validate("config_file.path");
            assert_eq!(&got.is_ok(), want_ok, "test case {} failed", test_case);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_final_validation() {
        let mut test_config =
            create_test_config("/user", "/factory", "/pkg/data/device_schema.json");

        for (test_name, config, want) in &[
            (
                "valid_config",
                r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "cidr_address": "1.1.1.1/32"
                          }
                        ]
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              },
              {
                "interface": {
                  "config": {
                    "name": "lan",
                    "type": "IF_ETHERNET"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "cidr_address": "192.168.1.1/24"
                          }
                        ],
                        "dhcp_server": {
                           "enabled": true,
                           "dhcp_pool": {
                             "start": "192.168.1.100",
                             "end": "192.168.1.254",
                             "lease_time": "1d"
                           },
                           "static_ip_allocations": [
                           {
                             "device_name": "device1",
                             "ip_address": "192.168.1.200",
                             "mac_address": "00:01:02:03:04:05"
                           },
                           {
                             "device_name": "device2",
                             "ip_address": "192.168.1.201",
                             "mac_address": "00:01:02:03:04:06"
                           }
                           ]
                        }
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#,
                Ok(()),
            ),
            (
                "invalid config",
                r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "cidr_address": "1.1.1.1/32"
                          }
                        ]
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              },
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "routed_vlan": {
                    "vlan_id": 1
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "cidr_address": "1.1.1.1/32"
                          }
                        ]
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#,
                Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                    path: "".to_string(),
                    error: "Interface must be exactly one of either: \'subinterfaces\', \'routed_vlan\', or \'switched_vlan\'".to_string(),
                })),
            ),
            (
                "bad dhcp pool",
                r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "lan",
                    "type": "IF_ETHERNET"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "cidr_address": "192.168.1.1/24"
                          }
                        ],
                        "dhcp_server": {
                           "enabled": true,
                           "dhcp_pool": {
                             "start": "192.168.1.100",
                             "end": "192.168.2.254",
                             "lease_time": "1d"
                           },
                           "static_ip_allocations": [{
                             "device_name": "device1",
                             "ip_address": "192.168.1.200",
                             "mac_address": "00:01:02:03:04:05"
                           }]
                        }
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#,
                Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                    path: "".to_string(),
                    error: "DhcpPool is not related to any IP address".to_string()
                })),
            ),
            (
                "bad dhcp allocation ip address",
                r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "lan",
                    "type": "IF_ETHERNET"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "cidr_address": "192.168.1.1/24"
                          }
                        ],
                        "dhcp_server": {
                           "enabled": true,
                           "dhcp_pool": {
                             "start": "192.168.1.100",
                             "end": "192.168.1.254",
                             "lease_time": "1d"
                           },
                           "static_ip_allocations": [
                           {
                             "device_name": "device1",
                             "ip_address": "192.168.1.250",
                             "mac_address": "00:01:02:03:04:05"
                           },
                           {
                             "device_name": "device2",
                             "ip_address": "192.168.2.251",
                             "mac_address": "00:01:02:03:04:06"
                           }
                           ]
                        }
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#,
                Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                    path: "".to_string(),
                    error: "DhcpPool is not related to any IP address".to_string()
                })),
            ),
            (
                "bad dhcp allocation mac address",
                r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "lan",
                    "type": "IF_ETHERNET"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "cidr_address": "192.168.1.1/24"
                          }
                        ],
                        "dhcp_server": {
                           "enabled": true,
                           "dhcp_pool": {
                             "start": "192.168.1.100",
                             "end": "192.168.1.254",
                             "lease_time": "1d"
                           },
                           "static_ip_allocations": [{
                             "device_name": "device1",
                             "ip_address": "192.168.1.200",
                             "mac_address": "01:01:02:03:04:05"
                           }]
                        }
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#,
                Err(error::NetworkManager::Config(error::Config::FailedToValidateConfig {
                    path: "".to_string(),
                    error: "not a valid MAC address".to_string()
                })),
            ),
        ] {
            match serde_json::from_str(&config) {
                Ok(j) => test_config.device_config = j,
                Err(e) => panic!("{} Got unexpected error result: {}", test_name, e),
            }
            let got = test_config.final_validation().await;
            assert_eq!(&got, want, "{}: got {:?} want: {:?}", test_name, got, want);
        }
    }

    #[test]
    fn test_get_device() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        assert_eq!(test_config.device().unwrap(), &build_full_config().device);
    }

    #[test]
    fn test_get_interfaces() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        assert_eq!(
            test_config.interfaces().unwrap(),
            &build_full_config().device.interfaces.unwrap()
        );
    }

    #[test]
    fn test_get_interface_by_device_id() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());

        assert_eq!(
            test_config.get_interface_by_device_id("/dev/sys/pci/test_wan_up_id/ethernet").unwrap(),
            &build_full_config().device.interfaces.unwrap()[1].interface
        );

        assert_eq!(
            test_config.get_interface_by_device_id("does_not_exist").unwrap(),
            &build_full_config().device.default_interface.unwrap()
        );
    }

    #[test]
    fn test_device_id_is_an_uplink() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());

        assert!(test_config.device_id_is_an_uplink("/dev/sys/pci/test_wan_up_id/ethernet"),);
        assert_eq!(
            test_config.device_id_is_an_uplink("/dev/sys/pci/test_lan_up_id/ethernet"),
            false
        );
        assert!(test_config.device_id_is_an_uplink("does_not_exist"));
    }

    #[test]
    fn test_device_id_is_a_downlink() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());

        assert_eq!(
            test_config.device_id_is_a_downlink("/dev/sys/pci/test_lan_up_id/ethernet"),
            true
        );
        assert_eq!(
            test_config.device_id_is_a_downlink("/dev/sys/pci/test_wan_up_id/ethernet"),
            false
        );
        assert_eq!(test_config.device_id_is_a_downlink("does_not_exist"), false);
    }

    #[test]
    fn test_get_vlans() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let v = test_config.get_vlans("routed_vlan");
        assert_eq!(v, vec![2]);

        let v = test_config.get_vlans("test_wan_up_id");
        let empty_vec: Vec<u16> = Vec::new();
        assert_eq!(v, empty_vec);
    }

    #[test]
    fn test_get_services() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let services = test_config.get_services();
        assert_eq!(*services.unwrap(), build_full_config().device.services.unwrap());
    }

    #[test]
    fn test_get_ip_forwarding() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        assert_eq!(test_config.get_ip_forwarding_state(), true);

        // removing the ip forwarding config should still return the default of false.
        test_config.device_config = Some(DeviceConfig {
            device: Device {
                acls: None,
                default_interface: None,
                interfaces: None,
                services: None,
            },
        });
        assert_eq!(test_config.get_ip_forwarding_state(), false);
    }

    #[test]
    fn test_get_nat_state() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        assert_eq!(test_config.get_nat_state(), true);

        // removing the nat section of the config should still return the default of false.
        test_config.device_config = Some(DeviceConfig {
            device: Device {
                acls: None,
                default_interface: None,
                interfaces: None,
                services: None,
            },
        });
        assert_eq!(test_config.get_nat_state(), false);
    }

    #[test]
    fn test_set_ip_address_config() {
        let test_config = create_test_config_no_paths();
        let ipconfig = IpAddress {
            dhcp_client: Some(false),
            cidr_address: Some(CidrAddress {
                ip: NetIpAddr("127.0.0.1".parse().unwrap()),
                prefix_length: 32,
            }),
        };
        let mut properties: lifmgr::LIFProperties = lifmgr::LIFProperties::default();
        test_config.set_ip_address_config(&mut properties, Some(&ipconfig));
        // The set_ip_address_config() function should not decide if the LIFProperties are enabled
        // or not, that should come from the AdminState in the config. So we shouldn't alter the
        // `enabled` field.
        assert_eq!(properties.enabled, false);
        assert_eq!(properties.dhcp, lifmgr::Dhcp::None);
        assert_eq!(
            properties.address_v4,
            Some(LifIpAddr { address: "127.0.0.1".parse().unwrap(), prefix: 32 })
        );

        // Make sure DHCP client can be enabled when no static IP configuration is present.
        let ipconfig = IpAddress { dhcp_client: Some(true), cidr_address: None };
        let mut properties: lifmgr::LIFProperties =
            lifmgr::LIFProperties { enabled: true, ..Default::default() };
        test_config.set_ip_address_config(&mut properties, Some(&ipconfig));
        assert_eq!(properties.enabled, true);
        assert_eq!(properties.dhcp, lifmgr::Dhcp::Client);
        assert_eq!(properties.address_v4, None);

        // Both DHCP client and static IP cannot be set simultaneously, make sure that DHCP client
        // is turned off when both are set.
        let ipconfig = IpAddress {
            dhcp_client: Some(true),
            cidr_address: Some(CidrAddress {
                ip: NetIpAddr("127.0.0.1".parse().unwrap()),
                prefix_length: 32,
            }),
        };
        let mut properties: lifmgr::LIFProperties =
            lifmgr::LIFProperties { enabled: true, ..Default::default() };
        test_config.set_ip_address_config(&mut properties, Some(&ipconfig));
        assert_eq!(properties.enabled, true);
        assert_eq!(properties.dhcp, lifmgr::Dhcp::None);
        assert_eq!(
            properties.address_v4,
            Some(LifIpAddr { address: "127.0.0.1".parse().unwrap(), prefix: 32 })
        );
    }

    #[test]
    fn test_get_ip_address() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let intfs = build_full_config().device.interfaces.unwrap();
        let subif = &intfs[0].interface.subinterfaces.as_ref().unwrap()[0];
        let expected_addr = &subif.ipv4.as_ref().unwrap().addresses[0];
        let (actual_v4, actual_v6) = test_config.get_ip_address(&intfs[0].interface).unwrap();
        assert_eq!(actual_v4.unwrap(), expected_addr);
        assert_eq!(actual_v6, None);
    }

    #[test]
    fn test_create_wan_properties() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        for (path, enabled, dhcp, address) in &[
            (
                "test_wan_no_admin_state_id",
                false,
                lifmgr::Dhcp::None,
                Some(LifIpAddr { address: "192.0.2.1".parse().unwrap(), prefix: 24 }),
            ),
            (
                "test_wan_down_id",
                false,
                lifmgr::Dhcp::None,
                Some(LifIpAddr { address: "192.0.2.1".parse().unwrap(), prefix: 24 }),
            ),
            (
                "test_wan_up_id",
                true,
                lifmgr::Dhcp::None,
                Some(LifIpAddr { address: "192.0.2.1".parse().unwrap(), prefix: 24 }),
            ),
            ("test_wan_dhcp_id", true, lifmgr::Dhcp::Client, None),
        ] {
            match test_config.create_wan_properties(path) {
                Ok(p) => {
                    assert_eq!(
                        p.enabled, *enabled,
                        "{} enabled: got {} want {}",
                        path, p.enabled, enabled
                    );
                    assert_eq!(p.dhcp, *dhcp, "{} dhcp: got {:?} want {:?}", path, p.dhcp, dhcp);
                    assert_eq!(
                        p.address_v4, *address,
                        "{} address: got {:?} want {:?}",
                        path, p.address_v4, address
                    );
                }
                Err(e) => panic!("{} Got unexpected result pair: {:?}", path, e),
            }
        }
    }

    #[test]
    fn test_create_lan_properties() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        for (path, enabled, dhcp, address) in &[
            (
                "test_lan_no_admin_state_id",
                false,
                lifmgr::Dhcp::None,
                Some(LifIpAddr { address: "192.0.2.1".parse().unwrap(), prefix: 24 }),
            ),
            (
                "test_lan_down_id",
                false,
                lifmgr::Dhcp::None,
                Some(LifIpAddr { address: "192.0.2.1".parse().unwrap(), prefix: 24 }),
            ),
            (
                "test_lan_up_id",
                true,
                lifmgr::Dhcp::None,
                Some(LifIpAddr { address: "192.0.2.1".parse().unwrap(), prefix: 24 }),
            ),
        ] {
            match test_config.create_lan_properties(path) {
                Ok(p) => {
                    assert_eq!(
                        &p.enabled, enabled,
                        "{} enabled: got {} want {}",
                        path, p.enabled, enabled
                    );
                    assert_eq!(&p.dhcp, dhcp, "{} dhcp: got {:?} want {:?}", path, p.dhcp, dhcp);
                    assert_eq!(
                        &p.address_v4, address,
                        "{} address: got {:?} want {:?}",
                        path, p.address_v4, address
                    );
                }
                Err(e) => panic!("{} Got unexpected result: {:?}", path, e),
            }
        }
    }

    #[test]
    fn test_get_interface_name() {
        let mut test_config = create_test_config_no_paths();

        // Ensure that if no default interface is configured and no topological path is found, then
        // make sure that an error is raised.
        test_config.device_config = Some(DeviceConfig {
            device: Device {
                default_interface: None,
                interfaces: Some(vec![Interfaces {
                    interface: Interface {
                        config: InterfaceConfig {
                            name: "some_interface_name".to_string(),
                            interface_type: InterfaceType::IfUplink,
                        },
                        oper_state: None,
                        device_id: None,
                        ethernet: None,
                        tcp_offload: None,
                        routed_vlan: None,
                        switched_vlan: None,
                        subinterfaces: Some(vec![Subinterface {
                            admin_state: Some(AdminState::Up),
                            ipv4: Some(IpAddressConfig {
                                addresses: vec![IpAddress {
                                    dhcp_client: Some(true),
                                    cidr_address: None,
                                }],
                                dhcp_server: None,
                            }),
                            ipv6: None,
                        }]),
                    },
                }]),
                acls: None,
                services: None,
            },
        });

        assert_eq!(
            test_config.get_interface_name("empty"),
            Err(error::NetworkManager::Config(error::Config::NotFound {
                msg: "Getting interface name for empty failed.".to_string()
            }))
        );

        // Make sure that the interface name is returned as expected.
        test_config.device_config = Some(build_full_config());
        assert_eq!(
            test_config.get_interface_name("test_wan_up_id").unwrap(),
            "test_wan_up".to_string()
        );

        // Test that if a default interface is configured and the topological path is not found, the
        // default interface configuration is used.
        assert_eq!(
            test_config.get_interface_name("some_device_id").unwrap(),
            "some_device_id".to_string()
        );
    }

    #[test]
    fn test_get_switched_vlan_by_device_id() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        assert!(test_config.device_id_is_a_switched_vlan("/dev/foo/whatever/switched_vlan/test"));
    }

    #[test]
    fn test_get_routed_vlan_interfaces() {
        let mut test_config = create_test_config_no_paths();

        test_config.device_config = Some(build_full_config());
        match test_config.get_routed_vlan_interfaces() {
            Some(it) => {
                // Turn the iterator into a vector.
                let v: Vec<&Interface> = it.collect();
                // Check the length is correct.
                assert_eq!(v.len(), 1);
                // Check that the interface is the correct one.
                assert_eq!(v[0].device_id, Some("routed_vlan".to_string()),);
            }
            None => {
                panic!("Unexpected 'None' result: Expecting interface containing the RoutedVlan")
            }
        }

        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(DeviceConfig {
            device: Device {
                default_interface: None,
                acls: None,
                interfaces: Some(vec![Interfaces {
                    interface: Interface {
                        config: InterfaceConfig {
                            name: "its_a_wan".to_string(),
                            interface_type: InterfaceType::IfUplink,
                        },
                        oper_state: None,
                        device_id: Some("not_a_routed_vlan".to_string()),
                        ethernet: None,
                        tcp_offload: None,
                        routed_vlan: None,
                        switched_vlan: None,
                        subinterfaces: None,
                    },
                }]),
                services: None,
            },
        });
        match test_config.get_routed_vlan_interfaces() {
            // If there is no RoutedVlan configured, then return an empty iterator.
            Some(it) => {
                let v: Vec<&Interface> = it.collect();
                assert_eq!(v.len(), 0);
            }
            None => panic!("Unexpected 'None' result: Was expecting an empty iterator"),
        };

        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(DeviceConfig {
            device: Device {
                default_interface: None,
                acls: None,
                interfaces: None,
                services: None,
            },
        });
        // If there are no interfaces, then should return 'None'.
        assert!(test_config.get_routed_vlan_interfaces().is_none());
    }

    #[test]
    fn test_resolve_to_routed_vlan() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let switched_vlan = SwitchedVlan {
            interface_mode: InterfaceMode::Access,
            access_vlan: Some(2),
            trunk_vlans: None,
        };

        let expected_if = test_config.get_routed_vlan_interfaces().unwrap().nth(0).unwrap();
        assert_eq!(
            expected_if.routed_vlan.as_ref().unwrap().vlan_id,
            test_config.resolve_to_routed_vlans(&switched_vlan).unwrap().vlan_id
        );

        let new_sv = SwitchedVlan {
            interface_mode: InterfaceMode::Access,
            access_vlan: Some(4000),
            trunk_vlans: None,
        };
        let new_config = Some(DeviceConfig {
            device: Device {
                default_interface: None,
                interfaces: Some(vec![Interfaces {
                    interface: Interface {
                        device_id: Some("doesntmatter".to_string()),
                        config: InterfaceConfig {
                            name: "doesntmatter".to_string(),
                            interface_type: InterfaceType::IfEthernet,
                        },
                        oper_state: None,
                        subinterfaces: None,
                        switched_vlan: Some(new_sv.clone()),
                        routed_vlan: None,
                        ethernet: None,
                        tcp_offload: None,
                    },
                }]),
                acls: None,
                services: None,
            },
        });
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = new_config;
        // Should fail because VLAN ID 4000 does not match the routed_vlan configuration.
        assert_eq!(
            test_config.resolve_to_routed_vlans(&new_sv),
            Err(error::NetworkManager::Config(error::Config::NotFound {
                msg: concat!(
                    "Switched VLAN port does not resolve to a routed VLAN: SwitchedVlan {",
                    " interface_mode: Access, access_vlan: Some(4000), trunk_vlans: None }"
                )
                .to_string()
            }))
        );
    }

    #[test]
    fn test_all_ports_have_same_bridge() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let sv = SwitchedVlan {
            interface_mode: InterfaceMode::Access,
            access_vlan: Some(2),
            trunk_vlans: None,
        };
        let vec_of_switched_vlans = vec![&sv, &sv, &sv];
        let routed_vlan = test_config
            .all_ports_have_same_bridge(vec_of_switched_vlans.into_iter().map(Result::Ok))
            .expect("got error");
        assert_eq!(routed_vlan.vlan_id, 2);
    }

    #[test]
    fn test_get_bridge_name() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let rvi = RoutedVlan { vlan_id: 2, ipv4: None, ipv6: None };
        assert_eq!(test_config.get_bridge_name(&rvi), "bridge".to_string());

        // bridge name should be the unnamed default.
        let rvi = RoutedVlan { vlan_id: 440, ipv4: None, ipv6: None };
        assert_eq!(test_config.get_bridge_name(&rvi), "unnamed_bridge".to_string());
    }

    #[test]
    fn test_create_routed_vlan_properties() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let rvi = RoutedVlan {
            vlan_id: 2,
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: Some(false),
                    cidr_address: Some(CidrAddress {
                        ip: NetIpAddr("192.168.0.1".parse().unwrap()),
                        prefix_length: 32,
                    }),
                }],
                dhcp_server: None,
            }),
            ipv6: None,
        };
        match test_config.create_routed_vlan_properties(&rvi) {
            Ok(p) => {
                assert_eq!(p.enabled, true);
                assert_eq!(p.dhcp, lifmgr::Dhcp::None);
                assert_eq!(
                    p.address_v4,
                    Some(LifIpAddr { address: "192.168.0.1".parse().unwrap(), prefix: 32 })
                );
            }
            Err(e) => panic!("Got unexpected result: {:?}", e),
        }
    }

    #[test]
    fn test_get_dhcp_server_config() {
        for (name, interface, want) in &[(
            "good",
            Interface {
                config: InterfaceConfig {
                    name: "test_lan_has_dhcp_server".to_string(),
                    interface_type: InterfaceType::IfEthernet,
                },
                oper_state: None,
                device_id: Some("test_lan_has_dhcp_server_id".to_string()),
                ethernet: None,
                tcp_offload: None,
                routed_vlan: None,
                switched_vlan: None,
                subinterfaces: Some(vec![Subinterface {
                    admin_state: Some(AdminState::Up),
                    ipv4: Some(IpAddressConfig {
                        addresses: vec![IpAddress {
                            dhcp_client: None,
                            cidr_address: Some(CidrAddress {
                                ip: NetIpAddr("192.0.2.1".parse().unwrap()),
                                prefix_length: 24,
                            }),
                        }],
                        dhcp_server: Some(DhcpServer {
                            enabled: true,
                            dhcp_pool: DhcpPool {
                                start: "192.0.2.100".parse().unwrap(),
                                end: "192.0.2.254".parse().unwrap(),
                                lease_time: "1d".to_string(),
                            },
                            static_ip_allocations: None,
                        }),
                    }),
                    ipv6: None,
                }]),
            },
            Some(DhcpServer {
                enabled: true,
                dhcp_pool: DhcpPool {
                    start: "192.0.2.100".parse().unwrap(),
                    end: "192.0.2.254".parse().unwrap(),
                    lease_time: "1d".to_string(),
                },
                static_ip_allocations: None,
            }),
        )] {
            let got = interface.get_dhcp_server_config();
            assert_eq!(got, want.as_ref(), "{} got: {:?}, want {:?}", name, interface, want);
        }
    }

    #[test]
    fn test_from_static_allocation() {
        for (testcase, allocation, want) in &[
            (
                "good",
                StaticIpAllocations {
                    device_name: "name1".to_string(),
                    ip_address: "192.168.0.1".parse().unwrap(),
                    mac_address: MacAddress::parse_str("00:01:02:03:04:05").unwrap(),
                },
                Ok(lifmgr::DhcpReservation {
                    id: ElementId::default(),
                    name: Some("name1".to_string()),
                    address: "192.168.0.1".parse().unwrap(),
                    mac: MacAddress::parse_str("00:01:02:03:04:05").unwrap(),
                }),
            ),
            (
                "good, no name",
                StaticIpAllocations {
                    device_name: "".to_string(),
                    ip_address: "192.168.0.1".parse().unwrap(),
                    mac_address: MacAddress::parse_str("00:01:02:03:04:05").unwrap(),
                },
                Ok(lifmgr::DhcpReservation {
                    id: ElementId::default(),
                    name: None,
                    address: "192.168.0.1".parse().unwrap(),
                    mac: MacAddress::parse_str("00:01:02:03:04:05").unwrap(),
                }),
            ),
            (
                "bad mac",
                StaticIpAllocations {
                    device_name: "name1".to_string(),
                    ip_address: "192.168.0.1".parse().unwrap(),
                    mac_address: MacAddress::parse_str("00:00:00:00:00:00").unwrap(),
                },
                Err(error::NetworkManager::Config(error::Config::NotSupported {
                    msg: "Invalid mac address".to_string(),
                })),
            ),
            (
                "broadcast mac",
                StaticIpAllocations {
                    device_name: "name1".to_string(),
                    ip_address: "192.168.0.1".parse().unwrap(),
                    mac_address: MacAddress::parse_str("ff:ff:ff:ff:ff:ff").unwrap(),
                },
                Err(error::NetworkManager::Config(error::Config::NotSupported {
                    msg: "Invalid mac address".to_string(),
                })),
            ),
            (
                "mcast mac",
                StaticIpAllocations {
                    device_name: "name1".to_string(),
                    ip_address: "192.168.0.1".parse().unwrap(),
                    mac_address: MacAddress::parse_str("01:01:02:03:04:05").unwrap(),
                },
                Err(error::NetworkManager::Config(error::Config::NotSupported {
                    msg: "Invalid mac address".to_string(),
                })),
            ),
        ] {
            let got = allocation.try_into();
            assert_eq!(&got, want, "{}: got {:?} want {:?}", testcase, got, want);
        }
    }

    #[test]
    fn test_get_acl_entries() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        match test_config.get_acl_entries("test_wan_up_id") {
            Some(v) => {
                let mut p = v.peekable();
                assert_eq!(p.peek().unwrap().config.forwarding_action, ForwardingAction::Drop);
                assert_eq!(p.peek().unwrap().config.direction.as_ref(), Some(&Direction::In));
                assert_eq!(
                    p.peek().unwrap().ipv4.as_ref(),
                    Some(&IpFilter {
                        src_address: None,
                        src_ports: None,
                        dst_address: Some("192.168.0.0/24".to_string().parse().unwrap()),
                        dst_ports: Some(PortRange { from: 8080, to: 8081 }),
                        protocol: Some(Protocol::Any),
                    })
                );
            }
            None => panic!("Unexpected 'None' response."),
        };
    }

    #[test]
    fn test_acl_entry_deserialize() {
        let valid_config = r#"{
            "device": {
                "acls": {
                    "acl_entries": [
                        {
                            "config": {
                                "forwarding_action": "DROP",
                                "device_id": "wlanif-client",
                                "comment": "Block traffic to sshd from the wlan"
                            },
                            "ipv4": {
                                "dst_address": "0.0.0.0/0",
                                "dst_ports": "22"
                            }
                        }
                    ]
                }
            }
        }"#;
        match serde_json::from_str::<serde_json::Value>(&valid_config) {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
    }

    #[test]
    fn test_to_cidr_address_fromstr() {
        let v4 = "192.168.0.32";
        let v6 = "::1";

        // Valid IPv4 CIDR address format.
        match format!("{}/24", v4).parse::<CidrAddress>() {
            Ok(addr) => {
                assert_eq!(addr.ip, NetIpAddr(v4.parse().unwrap()));
                assert_eq!(addr.prefix_length, 24);
            }
            Err(e) => panic!("Unexpected 'Error' result: {:?}", e),
        }

        // Valid IPv6 CIDR address format.
        match format!("{}/128", v6).parse::<CidrAddress>() {
            Ok(addr) => {
                assert_eq!(addr.ip, NetIpAddr(v6.parse().unwrap()));
                assert_eq!(addr.prefix_length, 128);
            }
            Err(e) => panic!("Unexpected 'Error' result: {:?}", e),
        }

        // Invalid IPv4 CIDR address: Missing a prefix length.
        match v4.parse::<CidrAddress>() {
            Ok(v) => panic!("Unexpected 'Ok' result: {:?}", v),
            Err(_) => (),
        }

        // Invalid IPv6 CIDR address: Missing a prefix length.
        match v6.parse::<CidrAddress>() {
            Ok(v) => panic!("Unexpected 'Ok' result: {:?}", v),
            Err(_) => (),
        }

        // Invalid IPv4 CIDR address: Bad IP address.
        match "192.168.1/24".parse::<CidrAddress>() {
            Ok(v) => panic!("Unexpected 'Ok' result: {:?}", v),
            Err(_) => (),
        }
    }

    #[test]
    fn test_parse_port_range_fromstr() {
        // A single `port` value between 0-65k should parse: the start and end values should be the
        // same.
        let actual = "22".parse::<PortRange>().unwrap();
        assert_eq!(actual.from, 22u16);
        assert_eq!(actual.to, 22u16);

        // Valid port range should parse successfully.
        let actual = "6667-6669".parse::<PortRange>().unwrap();
        assert_eq!(actual.from, 6667u16);
        assert_eq!(actual.to, 6669u16);

        // TODO(fxbug.dev/45891): Multiple port ranges are not supported yet.
        assert!("6666,6667-6669".parse::<PortRange>().is_err());
    }

    #[test]
    fn test_get_default_interface() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(DeviceConfig {
            device: Device {
                default_interface: None,
                interfaces: None,
                acls: None,
                services: None,
            },
        });
        assert_eq!(test_config.default_interface(), None);

        test_config.device_config = Some(build_full_config());
        let expected = &build_full_config().device.default_interface.unwrap();
        assert_eq!(test_config.default_interface().unwrap(), expected);
    }

    #[test]
    fn test_unknown_device_id() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        assert_eq!(test_config.is_unknown_device_id("this_doesnt_exist"), true);
        assert_eq!(test_config.is_unknown_device_id("test_wan_no_admin_state_id"), false);
    }
}

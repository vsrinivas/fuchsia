// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This modules contains the definition of the Network Manager CLI
//!
//! This module defines all the commands and subcommands that are implemented
//! for the CLI. This is done using the `structopt` crate.
use anyhow::{format_err, Error};
use eui48::MacAddress;
use fidl_fuchsia_router_config::{CidrAddress, Id};
use std::convert::TryInto;
use std::net::IpAddr;
use std::net::Ipv4Addr;
use std::str::FromStr;
pub use structopt::StructOpt;

// Custom parser used for creating interface identifiers
fn construct_id(src: &str) -> Result<Id, Error> {
    let mut id = Id { uuid: [0; 16], version: 0 };
    id.uuid[0] = match src.parse() {
        Ok(n) => n,
        Err(_err) => return Err(format_err!("Invalid ID format")),
    };
    // id.uuid[0] = src.parse().unwrap();
    Ok(id)
}

// This struct is used to hold IP addresses in CIDR format
#[derive(Debug, Clone, PartialEq)]
pub struct Ipv4AddrPrefix {
    pub address: std::net::Ipv4Addr,
    pub prefix: u8,
}

// This trait is used by structopt to parse IP addresses in CIDR notation
impl FromStr for Ipv4AddrPrefix {
    type Err = Error;
    // Custom parser used for IPv4 in CIDR format
    fn from_str(src: &str) -> Result<Self, Self::Err> {
        let ipv4_string_vector: Vec<&str> = src.split('/').collect();
        if ipv4_string_vector.len() != 2 {
            return Err(format_err!("Invalid IP format. Please use CIDR notation"));
        }
        let prefix = match ipv4_string_vector[1].parse() {
            Ok(n) => n,
            Err(_err) => return Err(format_err!("Cannot parse IP prefix")),
        };
        if prefix > 32 {
            return Err(format_err!("Prefix cannot be greater than 32"));
        }
        let ipv4_addr = match ipv4_string_vector[0].parse() {
            Ok(n) => n,
            Err(_err) => return Err(format_err!("Error parsing IP address")),
        };
        let ipv4_addr_prefix = Ipv4AddrPrefix { address: ipv4_addr, prefix };
        Ok(ipv4_addr_prefix)
    }
}

/// Converts an `Ipv4AddrPrefix` to a `router_config::CidrAddress`.
impl TryInto<CidrAddress> for Ipv4AddrPrefix {
    type Error = anyhow::Error;
    fn try_into(self) -> Result<CidrAddress, anyhow::Error> {
        let (ipv4_address, prefix_length) =
            (fidl_fuchsia_net::Ipv4Address { addr: self.address.octets() }, self.prefix);
        Ok(CidrAddress {
            address: Some(fidl_fuchsia_net::IpAddress::Ipv4(ipv4_address)),
            prefix_length: Some(prefix_length),
        })
    }
}

#[derive(StructOpt, Debug)]
pub struct Opt {
    #[structopt(long, short)]
    pub overnet: bool,
    #[structopt(subcommand)]
    pub cmd: Command,
}

#[derive(StructOpt, Debug)]
#[structopt(
    name = "Network Manager CLI",
    about = "This CLI is used to invoke the FIDL interface for the network manager app",
    version = "1.0"
)]
pub enum Command {
    #[structopt(name = "add")]
    /// Create a new LAN or WAN interface
    ADD(Add),
    #[structopt(name = "remove")]
    /// Remove a LAN or WAN interface
    REMOVE(Remove),
    #[structopt(name = "show")]
    /// Read device configuration
    SHOW(Show),
    #[structopt(name = "set")]
    /// Write device configuration
    SET(Set),
}

#[derive(StructOpt, Clone, Debug, PartialEq)]
pub enum SecurityFeature {
    #[structopt(name = "nat")]
    NAT,
}
impl FromStr for SecurityFeature {
    type Err = Error;
    fn from_str(feature: &str) -> Result<Self, Self::Err> {
        match feature.to_lowercase().as_str() {
            "nat" => Ok(SecurityFeature::NAT),
            _ => Err(format_err!("Invalid security feature: '{}'", feature)),
        }
    }
}

#[derive(StructOpt, Clone, Debug)]
pub enum Add {
    #[structopt(name = "wan")]
    /// Add a WAN interface
    Wan {
        #[structopt(raw(required = "true"))]
        name: String,
        #[structopt(short, long, raw(required = "true"))]
        ports: Vec<u32>,
        #[structopt(short, long)]
        vlan: Option<u16>,
    },
    #[structopt(name = "lan")]
    /// Add a LAN interface
    Lan {
        #[structopt(raw(required = "true"))]
        name: String,
        #[structopt(short, long, raw(required = "true"))]
        ports: Vec<u32>,
        #[structopt(short, long)]
        vlan: Option<u16>,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum Remove {
    #[structopt(name = "wan")]
    /// Remove a WAN interface
    Wan {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target WAN interface
        wan_id: Id,
    },
    #[structopt(name = "lan")]
    /// Remove a LAN interface
    Lan {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target LAN interface
        lan_id: Id,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum Show {
    #[structopt(name = "wan")]
    /// Show a WAN interface
    Wan {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        wan_id: Id,
    },
    #[structopt(name = "lan")]
    /// Show a LAN interface
    Lan {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        lan_id: Id,
    },
    #[structopt(name = "wanconfig")]
    /// Show configuration for a WAN interface
    WanConfig {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        wan_id: Id,
    },
    #[structopt(name = "lanconfig")]
    /// Show configuration for a LAN interface
    LanConfig {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        lan_id: Id,
    },
    #[structopt(name = "wans")]
    /// List all WAN interfaces
    Wans {},
    #[structopt(name = "lans")]
    /// List all LAN interfaces
    Lans {},
    #[structopt(name = "wanports")]
    /// List all ports for a WAN interface
    WanPorts {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        wan_id: Id,
    },

    #[structopt(name = "filterstate")]
    /// Show active Packet Filter rules
    FilterState {},

    #[structopt(name = "lanports")]
    /// List all ports for a LAN interface
    LanPorts {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        lan_id: Id,
    },
    #[structopt(name = "ports")]
    /// List all the ports
    Ports {},
    #[structopt(name = "routes")]
    /// List all the routes
    Routes {},
    #[structopt(name = "security-config")]
    /// Shows the security configuration.
    Security {},
    #[structopt(name = "port")]
    /// Show a port
    Port {
        #[structopt(raw(required = "true"))]
        port: u32,
    },
    #[structopt(name = "dnsconfig")]
    /// Show DNS configuration
    DnsConfig {},
    #[structopt(name = "dhcpconfig")]
    /// Show DHCP configuration
    DhcpConfig {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target LAN interface
        lan_id: Id,
    },
    #[structopt(name = "forwardstate")]
    /// Show forward state for a LAN interface
    ForwardState {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target WAN interface
        lan_id: Id,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum Set {
    #[structopt(name = "wan-state")]
    /// Enable/Disable a WAN interface
    WanState {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target WAN interface
        wan_id: Id,
        #[structopt(raw(required = "true"))]
        /// Use "up" or "down" keywords to change the specified WAN's state
        state: String,
    },

    #[structopt(name = "wan-connection")]
    /// Configure the connection type for a WAN interface: {pppoe, pptp, l2tp}, set mtu or metric
    WanConnection {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target WAN interface
        wan_id: Id,
        /// Use "direct", "pppoe", "pptp" and "l2tp" keywords to set the connection type. Pay attention to required args that follow for each connection type
        connection: Option<String>,
        /// Required for "pppoe", "pptp" and "l2tp" connection types
        username: Option<String>,
        /// Required for "pppoe", "pptp" and "l2tp" connection types
        password: Option<String>,
        /// Required for "pptp" and "l2tp" connection types. Format: X.X.X.X where 0 < X < 255
        server: Option<Ipv4Addr>,
        #[structopt(short, long)]
        /// Optional flag for setting metric
        metric: Option<u32>,
        #[structopt(short = "t", long)]
        /// Optional flag for setting mtu
        mtu: Option<u32>,
    },

    #[structopt(name = "wan-ip")]
    /// Set IP settings to manual or DHCP for a WAN interface or set an optional hostname
    WanIp {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target WAN interface
        wan_id: Id,
        /// The IP setting can be set to either "dhcp" or "manual". "manual" should be followed by ipv4 and gateway.
        mode: Option<String>,
        // TODO: Use a data struct for ipv4 with cidr format
        /// IPv4 address for manual address method. Format: X.X.X.X/X where 0 < X < 255
        ipv4: Option<Ipv4AddrPrefix>,
        /// Gateway address for manual address method. Format: X.X.X.X where 0 < X < 255
        gateway: Option<Ipv4Addr>,
        #[structopt(short = "n", long)]
        /// Optional flag for setting a hostname
        hostname: Option<String>,
    },

    #[structopt(name = "wan-mac")]
    /// Provice a MAC address for a WAN interface
    WanCloneMac {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target WAN interface
        wan_id: Id,
        #[structopt(raw(required = "true"))]
        /// MAC address. Format: X:X:X:X:X:X where 00 < X < FF (Hexadecimal)
        mac: MacAddress,
    },

    #[structopt(name = "lan-state")]
    /// Enable/Disable a LAN interface
    LanState {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target LAN interface
        lan_id: Id,
        #[structopt(raw(required = "true"))]
        /// Use "up" or "down" keywords to change the specified LAN's state
        state: String,
    },

    #[structopt(name = "lan-ip")]
    /// Set a manual IPv4 address for a LAN interface
    LanIp {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target LAN interface
        lan_id: Id,
        /// Manual IPv4 address. Format: X.X.X.X/X where 0 < X < 255
        ipv4: Option<Ipv4AddrPrefix>,
        // ipv4: Option<[u8; 5]>,
    },

    #[structopt(name = "lan-dhcp")]
    /// Configure a LAN interface
    LanDhcp {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        /// ID of the target LAN interface
        lan_id: Id,
        #[structopt(raw(required = "true"))]
        /// Use "up" or "down" keywords to change the DHCP server state
        state: String,
        #[structopt(short, long = "lease-time")]
        /// Lease time in seconds
        lease_time_sec: Option<u32>,
        #[structopt(short, long = "gateway")]
        /// Deafault gateway that is advertised
        gateway: Option<Ipv4Addr>,
    },

    #[structopt(name = "dhcp-config")]
    /// Configure DHCP settings for a LAN interface
    DhcpConfig {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        lan_id: Id,
        dhcp_config: String,
    },

    #[structopt(name = "dns-config")]
    /// Configure DNS settings
    DnsConfig {
        #[structopt(raw(required = "true"))]
        server: IpAddr,
    },

    #[structopt(name = "dns-forwarder")]
    /// Configure DNS forwarder for a LAN interface
    DnsForwarder {
        #[structopt(parse(try_from_str = "construct_id"), raw(required = "true"))]
        lan_id: Id,
        enabled: bool,
    },

    #[structopt(name = "route")]
    /// Create a route
    Route {
        #[structopt(raw(required = "true"))]
        route: String,
    },

    #[structopt(name = "security-config")]
    /// Set security configuration
    SecurityConfig {
        #[structopt(raw(required = "true"))]
        feature: SecurityFeature,
        enabled: bool,
    },

    #[structopt(name = "port-forward")]
    /// Configure port forwarding rules
    PortForward {
        #[structopt(raw(required = "true"))]
        rule: String,
    },

    #[structopt(name = "filter")]
    /// Add a new packet filter rule.
    Filter {
        action: String,
        src_address: Option<Ipv4AddrPrefix>,
        src_port_range: Option<String>,
        dst_address: Option<Ipv4AddrPrefix>,
        dst_port_range: Option<String>,
        protocol: Option<String>,
    },
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::opts::Ipv4AddrPrefix;
    use fidl_fuchsia_net::Ipv4Address;
    use std::convert::TryInto;
    use std::net::Ipv4Addr;
    use std::str::FromStr;

    #[test]
    fn construct_id_test() {
        // TODO: Add testing for this
    }

    #[test]
    fn ipv4_prefix_from_str_test() {
        assert_eq!(
            // TODO: Add more tests here
            Ipv4AddrPrefix::from_str(&r"1.1.1.1/1").unwrap(),
            Ipv4AddrPrefix { address: Ipv4Addr::new(1, 1, 1, 1), prefix: 1 }
        );
    }

    #[test]
    fn test_into_cidr_address() {
        let addr: std::net::Ipv4Addr = "169.254.0.0".parse().unwrap();
        let ipv4addr: Ipv4AddrPrefix = Ipv4AddrPrefix { address: addr.clone(), prefix: 16 };
        let expected = fidl_fuchsia_router_config::CidrAddress {
            address: Some(fidl_fuchsia_net::IpAddress::Ipv4(Ipv4Address { addr: addr.octets() })),
            prefix_length: Some(16),
        };
        let actual: fidl_fuchsia_router_config::CidrAddress = ipv4addr.try_into().unwrap();
        assert_eq!(actual, expected);
    }

    #[test]
    fn test_security_feature_from_str() {
        assert_eq!(SecurityFeature::from_str("nat").unwrap(), SecurityFeature::NAT);
        assert_eq!(SecurityFeature::from_str("NAT").unwrap(), SecurityFeature::NAT);
        assert_eq!(SecurityFeature::from_str("NaT").unwrap(), SecurityFeature::NAT);
        assert_eq!(
            SecurityFeature::from_str("nnat").unwrap_err().to_string(),
            format!("Invalid security feature: 'nnat'")
        );
    }
}

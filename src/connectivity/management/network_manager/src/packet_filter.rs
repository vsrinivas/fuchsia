// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles packet filtering requests for Router Manager.

use failure::{format_err, Error};
use fidl_fuchsia_net_filter::{self as netfilter, Direction, FilterMarker, FilterProxy, Status};
use fidl_fuchsia_router_config as router_config;
use fuchsia_component::client::connect_to_service;

/// Storage for this PacketFilter's attributes.
pub struct PacketFilter {
    filter_svc: FilterProxy,
}

// Parses a `netfilter::Rule` into a `router_config::FilterRule`.
fn to_filter_rule(rule: netfilter::Rule) -> Result<router_config::FilterRule, Error> {
    // This is a good candidate to refactor to use TryInto/TryFrom.
    Ok(router_config::FilterRule {
        element: router_config::Id { uuid: [0; 16], version: 0 },
        action: to_filter_action(rule.action),
        selector: router_config::FlowSelector {
            src_address: to_cidr_address(rule.src_subnet),
            dst_address: to_cidr_address(rule.dst_subnet),
            // TODO(cgibson): netfilter2 does not currently support port ranges (NET-2182)
            // Put the `uint16` from the `netfilter::Rule` directly into the
            // `router_config::PortRange::from` field for now.
            src_ports: to_port_range(rule.src_port, rule.src_port),
            dst_ports: to_port_range(rule.dst_port, rule.dst_port),
            protocol: to_protocol(rule.proto),
        },
    })
}

// Parses a `netfilter::Action` and turns it into a `router_config::FilterAction`.
fn to_filter_action(action: netfilter::Action) -> router_config::FilterAction {
    match action {
        netfilter::Action::Pass => router_config::FilterAction::Allow,
        // TODO(cgibson): What is our default drop policy? Should we gloss over the difference
        // to users of the Router Manager or should it become a parse error?
        netfilter::Action::Drop => router_config::FilterAction::Drop,
        netfilter::Action::DropReset => router_config::FilterAction::Drop,
    }
}

// Parses a `fidl_fuchsia_net::Subnet` and turns it into a `router_config::CidrAddress`.
fn to_cidr_address(
    subnet: Option<Box<fidl_fuchsia_net::Subnet>>,
) -> Option<router_config::CidrAddress> {
    match subnet {
        Some(s) => Some(router_config::CidrAddress {
            address: Some(s.addr),
            prefix_length: Some(s.prefix_len),
        }),
        None => None,
    }
}

// TODO(cgibson): netfilter2 currently does not support port ranges (NET-2182)
fn to_port_range(from: u16, to: u16) -> Option<Vec<router_config::PortRange>> {
    Some(vec![router_config::PortRange { from, to }])
}

// Parses a `netfilter::SocketProtocol` to a `router_config::Protocol`.
//
// `netfilter::SocketProtocol` cannot represent multiple protocols at once (i.e: It does not have
// a representation for "Both", or "All", etc.).
fn to_protocol(proto: netfilter::SocketProtocol) -> Option<router_config::Protocol> {
    match proto {
        netfilter::SocketProtocol::Tcp => Some(router_config::Protocol::Tcp),
        netfilter::SocketProtocol::Udp => Some(router_config::Protocol::Udp),
        _ => None,
    }
}

// Parses a `router_config::FilterRule` into a `netfilter::Rule`.
fn from_filter_rule(rule: router_config::FilterRule) -> Result<Vec<netfilter::Rule>, Error> {
    let mut netfilter_rules = Vec::new();
    let netfilter_rule = gen_netfilter_rule(&rule)?;
    match from_protocol(rule.selector.protocol) {
        Some(proto) => {
            netfilter_rules.push(netfilter::Rule { proto, ..netfilter_rule });
        }
        None => {
            netfilter_rules
                .push(netfilter::Rule { proto: netfilter::SocketProtocol::Tcp, ..netfilter_rule });

            // FIDL doesn't have the `Clone` trait on `netfilter::Rule`'s, so we have to resort to
            // parsing the rule again to work around netfilter's lack of a `Both` definition in it's
            // `SocketProtocol` API. Luckily it's a fairly trivial operation.
            let udp_netfilter_rule = gen_netfilter_rule(&rule)?;
            netfilter_rules.push(netfilter::Rule {
                proto: netfilter::SocketProtocol::Udp,
                ..udp_netfilter_rule
            });
        }
    }
    Ok(netfilter_rules)
}

// Takes a `router_config::FilterRule` and converts it into a `netfilter::Rule`.
fn gen_netfilter_rule(rule: &router_config::FilterRule) -> Result<netfilter::Rule, Error> {
    // This is a good candidate to refactor to use TryInto/TryFrom.
    let src_port: u16 = match from_port_range(&rule.selector.src_ports) {
        Ok(port) => match port {
            Some(p) => p,
            // TODO(cgibson): I think `src_port` not being optional is a bug in the netfilter
            // FIDL API?
            None => 0,
        },
        Err(e) => return Err(format_err!("Invalid source port: {:?}", e)),
    };
    let dst_port: u16 = match from_port_range(&rule.selector.dst_ports) {
        Ok(port) => match port {
            Some(p) => p,
            // TODO(cgibson): I think `dst_port` not being optional is a bug in the netfilter
            // FIDL API?
            None => 0,
        },
        Err(e) => return Err(format_err!("Invalid destination port: {:?}", e)),
    };
    Ok(netfilter::Rule {
        action: from_filter_action(&rule.action),
        // TODO(cgibson): We need a way to specify the direction of traffic.
        direction: Direction::Incoming,
        dst_subnet: from_cidr_address(&rule.selector.dst_address)?,
        dst_subnet_invert_match: false,
        keep_state: true,
        log: false,
        quick: false,
        src_subnet: from_cidr_address(&rule.selector.src_address)?,
        src_subnet_invert_match: false,
        src_port,
        dst_port,
        // TODO(cgibson): NIC 0 applies to *all* interfaces, however that doesn't seem like what we
        // want to do at all. The `router_config::FilterRule` FIDL API doesn't specify interface
        // names, and in fact should probably be a *property* of the WAN or LAN router_config FIDL
        // APIs since we can have packet filters installed on every interface.
        nic: 0,
        // The proto field requires further processing, just set any value for now.
        proto: netfilter::SocketProtocol::Ip,
    })
}

// Parses a `router_config::Protocol` and returns the equivalent `netfilter::SocketProtocol`.
//
// `netfilter::SocketProtocol` cannot represent multiple protocols at once (i.e: It does not have
// a representation for "Both", or "All", etc.). "Both" is also the default when no protocol is
// provided. Return `None` as the representation of "Both".
fn from_protocol(proto: Option<router_config::Protocol>) -> Option<netfilter::SocketProtocol> {
    match proto {
        Some(proto) => match proto {
            router_config::Protocol::Tcp => Some(netfilter::SocketProtocol::Tcp),
            router_config::Protocol::Udp => Some(netfilter::SocketProtocol::Udp),
            router_config::Protocol::Both => None,
        },
        None => None,
    }
}

// Parses a `router_config::FilterAction` and turns it into a `netfilter::Action`
fn from_filter_action(action: &router_config::FilterAction) -> netfilter::Action {
    match action {
        router_config::FilterAction::Allow => netfilter::Action::Pass,
        // TODO(cgibson): What is our default drop policy? Should we gloss over the difference
        // to users of the Router Manager or should it become a parse error?
        router_config::FilterAction::Drop => netfilter::Action::Drop,
    }
}

// Parses a `router_config::PortRange` and turns it into a `u16` result.
fn from_port_range(range: &Option<Vec<router_config::PortRange>>) -> Result<Option<u16>, Error> {
    // TODO(cgibson): netfilter2 does not currently support port ranges (NET-2182)
    // For now, we'll put the first `router_config::PortRange`'s `from` value into
    // `netfilter::Rule`'s src or dst port field.
    match range {
        Some(v) => Ok(Some(v[0].from)),
        None => Ok(None),
    }
}

// Parses a `router_config::CidrAddress` and turns it into a `fidl_fuchsia_net::Subnet`.
fn from_cidr_address(
    cidr_address: &Option<router_config::CidrAddress>,
) -> Result<Option<Box<fidl_fuchsia_net::Subnet>>, Error> {
    let (addr, prefix_len) = match cidr_address {
        Some(cidr_addr) => {
            let ip = match cidr_addr.address {
                Some(a) => a,
                None => return Err(format_err!("CidrAddress is missing an IPv4 address")),
            };
            let prefix = match cidr_addr.prefix_length {
                Some(p) => p,
                None => return Err(format_err!("CidrAddress is missing the prefix length")),
            };
            (ip, prefix)
        }
        None => return Err(format_err!("CidrAddress does not have an IpAddress field")),
    };
    Ok(Some(Box::new(fidl_fuchsia_net::Subnet { addr, prefix_len })))
}

/// Manages a Packet Filter connection to netstack filter service (netfilter).
///
/// Mainly serves as a wrapper around the netfilter service. Converts Router Manager FIDL APIs into
/// something that the netfilter service can understand. Finally converts the netfilter response
/// back into a Router Manager FIDL for consumption by the caller.
impl PacketFilter {
    /// Starts a new instance of a PacketFilter.
    pub fn start() -> Result<Self, Error> {
        let filter_svc = connect_to_service::<FilterMarker>()?;
        info!("Connected to filter service");
        Ok(PacketFilter { filter_svc })
    }

    /// Returns the current set of netfilter packet filters.
    ///
    /// Using the existing handle to the netfilter service, request the set of packet filter rules
    /// and converts them to a vector of `router_config::FilterRule`'s.
    ///
    /// # Error
    ///
    /// If the response from netfilter is anything other than `netfilter::Status::Ok` then
    /// produce an error result. Failure to convert from the `netfilter::Rule` to a
    /// `router_config::FilterRule` produces an error result to the caller.
    pub async fn get_filters(&self) -> Result<Vec<router_config::FilterRule>, Error> {
        info!("Received request to get all active packet filters");
        let netfilter_rules: Vec<netfilter::Rule> = match self.filter_svc.get_rules().await {
            Ok((rules, _, Status::Ok)) => rules,
            Ok((_, _, status)) => {
                return Err(format_err!("Failed to get filters: Status was: {:?}", status))
            }
            Err(e) => return Err(format_err!("fidl error: {:?}", e)),
        };

        netfilter_rules
            .into_iter()
            .map(|rule| match to_filter_rule(rule) {
                Ok(f) => Ok(f),
                Err(e) => Err(format_err!("Failed to parse filter rule: {:?}", e)),
            })
            .collect::<Result<Vec<router_config::FilterRule>, Error>>()
    }

    /// Installs a new packet filter rule.
    ///
    /// We convert the `router_config::FilterRule` and parse it into a `netfilter::Rule` that we
    /// can send on to the netfilter service. We also need to get a `generation` number from to
    /// include in the request.
    ///
    /// # Error
    /// If we fail to get the generation number from the netfilter service, or the result of the
    /// request to netfilter is anything other than `netfilter::Status::Ok` then produce an error
    /// result. Failure to convert the `router_config::FilterRule` to a `netfilter::Rule` will
    /// produce an error result to the caller.
    pub async fn set_filter(&self, rule: router_config::FilterRule) -> Result<(), Error> {
        info!("Received request to add new packet filter rule");
        let generation: u32 = match self.filter_svc.get_rules().await {
            Ok((_, generation, Status::Ok)) => generation,
            Ok((_, _, status)) => {
                return Err(format_err!(
                    "Failed to get generation number! Status was: {:?}",
                    status
                ))
            }
            Err(e) => return Err(format_err!("fidl error: {:?}", e)),
        };
        let mut netfilter_rules = from_filter_rule(rule)?;
        match self.filter_svc.update_rules(&mut netfilter_rules.iter_mut(), generation).await {
            Ok(Status::Ok) => Ok(()),
            Ok(status) => Err(format_err!("Failed to add new packet filter: {:?}", status)),
            Err(e) => Err(format_err!("fidl error: {:?}", e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_net::IpAddress::Ipv4;
    use fidl_fuchsia_net::Ipv4Address;
    use fidl_fuchsia_router_config::{
        CidrAddress, FilterAction, FilterRule, FlowSelector, Id, PortRange, Protocol,
    };
    use std::net::IpAddr;

    #[test]
    fn test_convert_subnet_to_cidr_address() {
        let addr = [169, 254, 1, 1];
        let prefix = 32;
        let test_ip = fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr });
        let test_subnet = fidl_fuchsia_net::Subnet { addr: test_ip, prefix_len: prefix };

        // Convert our `test_subnet` to a `CidrAddress`.
        let cidr_addr = to_cidr_address(Some(Box::new(test_subnet))).unwrap();

        // Check that it suceeded: We can't access the IpAddress octets directly, so we need to use
        // a match statement.
        match cidr_addr.address.unwrap() {
            fidl_fuchsia_net::IpAddress::Ipv4(v4addr) => {
                assert_eq!(v4addr.addr, addr);
            }
            _ => panic!("Failed to match CidrAddress!"),
        }

        match cidr_addr.prefix_length {
            Some(p) => assert_eq!(p, prefix),
            None => panic!("CidrAddress prefix length is None, expecting: {}", prefix),
        }
    }

    #[test]
    fn test_to_port_range() {
        let port_ranges = to_port_range(1000, 2000);
        assert_eq!(port_ranges.is_some(), true);

        let p = port_ranges.unwrap();
        assert_eq!(p[0].from, 1000);
        assert_eq!(p[0].to, 2000);
    }

    #[test]
    fn test_to_protocol() {
        let tcp = to_protocol(netfilter::SocketProtocol::Tcp);
        assert_eq!(tcp.unwrap(), router_config::Protocol::Tcp);

        let udp = to_protocol(netfilter::SocketProtocol::Udp);
        assert_eq!(udp.unwrap(), router_config::Protocol::Udp);

        let icmpv6 = to_protocol(netfilter::SocketProtocol::Icmpv6);
        assert_eq!(icmpv6.is_none(), true);
    }

    #[test]
    fn test_to_filter_rule() {
        let ip: IpAddr = "169.254.0.1".parse().unwrap();
        let src_subnet = Some(Box::new(fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(Ipv4Address {
                addr: match ip {
                    std::net::IpAddr::V4(v4addr) => v4addr.octets(),
                    std::net::IpAddr::V6(_) => panic!("unexpected ipv6 address"),
                },
            }),
            prefix_len: 32,
        }));
        let dst_subnet = src_subnet.clone();
        let test_netfilter_rule = netfilter::Rule {
            action: netfilter::Action::Pass,
            direction: netfilter::Direction::Incoming,
            quick: false,
            src_subnet,
            src_subnet_invert_match: false,
            src_port: 1024,
            dst_subnet,
            dst_subnet_invert_match: false,
            dst_port: 80,
            proto: netfilter::SocketProtocol::Tcp,
            nic: 0,
            log: false,
            keep_state: true,
        };

        let expected = FilterRule {
            element: Id { uuid: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], version: 0 },
            action: FilterAction::Allow,
            selector: FlowSelector {
                src_address: Some(CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                src_ports: Some([PortRange { from: 1024, to: 1024 }].to_vec()),
                dst_address: Some(CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                dst_ports: Some([PortRange { from: 80, to: 80 }].to_vec()),
                protocol: Some(Protocol::Tcp),
            },
        };
        let actual = to_filter_rule(test_netfilter_rule);
        assert_eq!(expected, actual.unwrap());
    }

    #[test]
    fn test_from_filter_rule() {
        let ip: IpAddr = "169.254.0.1".parse().unwrap();
        let src_subnet = Some(Box::new(fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(Ipv4Address {
                addr: match ip {
                    std::net::IpAddr::V4(v4addr) => v4addr.octets(),
                    std::net::IpAddr::V6(_) => panic!("unexpected ipv6 address"),
                },
            }),
            prefix_len: 32,
        }));
        let dst_subnet = src_subnet.clone();
        let expected = netfilter::Rule {
            action: netfilter::Action::Pass,
            // TODO(cgibson): Temporary workaround for the lack of a "direction" field in
            // `FilterRule`. The conversion logic assumes that if a direction is not specified,
            // then we will default it to `Incoming`.
            direction: netfilter::Direction::Incoming,
            quick: false,
            src_subnet,
            src_subnet_invert_match: false,
            src_port: 1024,
            dst_subnet,
            dst_subnet_invert_match: false,
            dst_port: 80,
            proto: netfilter::SocketProtocol::Tcp,
            nic: 0,
            log: false,
            keep_state: true,
        };

        let filter_rule = FilterRule {
            element: Id { uuid: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], version: 0 },
            action: FilterAction::Allow,
            selector: FlowSelector {
                src_address: Some(CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                src_ports: Some([PortRange { from: 1024, to: 1024 }].to_vec()),
                dst_address: Some(CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                dst_ports: Some([PortRange { from: 80, to: 80 }].to_vec()),
                protocol: Some(Protocol::Tcp),
            },
        };
        let actual = from_filter_rule(filter_rule).unwrap();
        assert_eq!(1, actual.len());
        assert_eq!(expected, actual[0]);
    }

    #[test]
    fn test_from_protocol() {
        let tcp = from_protocol(Some(router_config::Protocol::Tcp)).unwrap();
        assert_eq!(netfilter::SocketProtocol::Tcp, tcp);

        let udp = from_protocol(Some(router_config::Protocol::Udp)).unwrap();
        assert_eq!(netfilter::SocketProtocol::Udp, udp);

        let both = from_protocol(Some(router_config::Protocol::Both));
        assert_eq!(both.is_none(), true);
    }
}

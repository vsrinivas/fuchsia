// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles packet filtering requests for Network Manager.

use {
    crate::config::{self, AclEntry},
    crate::error,
    crate::servicemgr::NatConfig,
    fidl_fuchsia_net_filter::{self as netfilter, Direction, FilterMarker, FilterProxy, Status},
    fidl_fuchsia_router_config::{self as netconfig},
    fuchsia_component::client::connect_to_service,
    std::convert::{From, TryFrom, TryInto},
    std::net::IpAddr,
};

impl From<config::ForwardingAction> for netconfig::FilterAction {
    fn from(action: config::ForwardingAction) -> Self {
        match action {
            config::ForwardingAction::Accept => netconfig::FilterAction::Allow,
            config::ForwardingAction::Drop => netconfig::FilterAction::Drop,
        }
    }
}

impl From<config::Protocol> for netconfig::Protocol {
    fn from(proto: config::Protocol) -> Self {
        match proto {
            config::Protocol::Tcp => netconfig::Protocol::Tcp,
            config::Protocol::Udp => netconfig::Protocol::Udp,
            config::Protocol::Any => netconfig::Protocol::Both,
        }
    }
}

impl TryFrom<&config::IpFilter> for netconfig::FilterRule {
    type Error = error::NetworkManager;

    fn try_from(ipfilter: &config::IpFilter) -> error::Result<Self> {
        // Packet filter rules have various optional fields that can not be provided and the overall
        // filter rule is still valid. For example, missing a source IP address is not an error. We
        // have to distinguish between empty fields and invalid values.
        let src_address = ipfilter
            .src_address
            .as_ref()
            .map(|s| {
                s.try_into().map_err(|e| {
                    error::NetworkManager::Config(error::Config::Malformed {
                        msg: format!("Failed to convert ip filter rule: {:?}", e),
                    })
                })
            })
            .transpose()?;

        let dst_address = ipfilter
            .dst_address
            .as_ref()
            .map(|s| {
                Some(s.try_into().map_err(|e| {
                    error::NetworkManager::Config(error::Config::Malformed {
                        msg: format!("Failed to convert ip filter rule: {:?}", e),
                    })
                }))
            })
            .flatten()
            .transpose()?;

        let mut src_ports = None;
        if let Some(range) = ipfilter.src_ports.as_ref() {
            // TODO(45891): Multiple port ranges are not supported yet.
            src_ports = Some(vec![range.into()]);
        }

        let mut dst_ports = None;
        if let Some(range) = ipfilter.dst_ports.as_ref() {
            // TODO(45891): Multiple port ranges are not supported yet.
            dst_ports = Some(vec![range.into()]);
        }

        Ok(netconfig::FilterRule {
            element: netconfig::Id { uuid: [0; 16], version: 0 },
            // The forwarding action requires further processing, so set any value here for the
            // moment.
            action: netconfig::FilterAction::Allow,
            selector: netconfig::FlowSelector {
                src_address,
                src_ports,
                dst_address,
                dst_ports,
                protocol: ipfilter.protocol.clone().map(|proto| proto.into()),
            },
        })
    }
}

/// Storage for [`PacketFilter`]'s attributes.
pub struct PacketFilter {
    filter_svc: FilterProxy,
}

/// Parses a [`netfilter::Rule`] into a [`netconfig::FilterRule`].
fn to_filter_rule(rule: netfilter::Rule) -> error::Result<netconfig::FilterRule> {
    // TODO(cgibson): This is a good candidate to refactor to use TryInto/TryFrom.
    Ok(netconfig::FilterRule {
        element: netconfig::Id { uuid: [0; 16], version: 0 },
        action: to_filter_action(rule.action),
        selector: netconfig::FlowSelector {
            src_address: to_cidr_address(rule.src_subnet),
            dst_address: to_cidr_address(rule.dst_subnet),
            src_ports: to_port_range(rule.src_port_range),
            dst_ports: to_port_range(rule.dst_port_range),
            protocol: to_protocol(rule.proto),
        },
    })
}

/// Parses a [`netfilter::Action`] and turns it into a [`netconfig::FilterAction`].
fn to_filter_action(action: netfilter::Action) -> netconfig::FilterAction {
    match action {
        netfilter::Action::Pass => netconfig::FilterAction::Allow,
        // TODO(cgibson): What is our default drop policy? Should we gloss over the difference
        // to users of the Network Manager or should it become a parse error? fxbug.dev/45024
        netfilter::Action::Drop => netconfig::FilterAction::Drop,
        netfilter::Action::DropReset => netconfig::FilterAction::Drop,
    }
}

/// Parses a [`fidl_fuchsia_net::Subnet`] and turns it into a [`netconfig::CidrAddress`].
fn to_cidr_address(
    subnet: Option<Box<fidl_fuchsia_net::Subnet>>,
) -> Option<netconfig::CidrAddress> {
    match subnet {
        Some(s) => Some(netconfig::CidrAddress {
            address: Some(s.addr),
            prefix_length: Some(s.prefix_len),
        }),
        None => None,
    }
}

/// Parses a [`netfilter::PortRange`] and turns it into a vector of [`netconfig::PortRange`]'s.
fn to_port_range(i: netfilter::PortRange) -> Option<Vec<netconfig::PortRange>> {
    Some(vec![netconfig::PortRange { from: i.start, to: i.end }])
}

/// Parses a [`netfilter::SocketProtocol`] to a [`netconfig::Protocol`].
///
/// [`netfilter::SocketProtocol`] cannot represent multiple protocols at once (i.e: It does not have
/// a representation for "Both", or "All", etc.).
fn to_protocol(proto: netfilter::SocketProtocol) -> Option<netconfig::Protocol> {
    match proto {
        netfilter::SocketProtocol::Tcp => Some(netconfig::Protocol::Tcp),
        netfilter::SocketProtocol::Udp => Some(netconfig::Protocol::Udp),
        _ => None,
    }
}

/// Parses a [`netconfig::FilterRule`] into a [`netfilter::Rule`].
fn from_filter_rule(
    rule: &netconfig::FilterRule,
    nicid: u32,
) -> error::Result<Vec<netfilter::Rule>> {
    let mut netfilter_rules = Vec::new();
    let netfilter_rule = gen_netfilter_rule(rule, nicid)?;
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
            let udp_netfilter_rule = gen_netfilter_rule(rule, nicid)?;
            netfilter_rules.push(netfilter::Rule {
                proto: netfilter::SocketProtocol::Udp,
                ..udp_netfilter_rule
            });
        }
    }
    Ok(netfilter_rules)
}

/// Takes a [`netconfig::FilterRule`] and converts it into a [`netfilter::Rule`].
fn gen_netfilter_rule(rule: &netconfig::FilterRule, nicid: u32) -> error::Result<netfilter::Rule> {
    // This is a good candidate to refactor to use TryInto/TryFrom.
    let src_port_range = from_port_range(&rule.selector.src_ports)
        .unwrap_or_else(|| netfilter::PortRange { start: 0, end: 0 });
    let dst_port_range = from_port_range(&rule.selector.dst_ports)
        .unwrap_or_else(|| netfilter::PortRange { start: 0, end: 0 });
    Ok(netfilter::Rule {
        action: from_filter_action(rule.action),
        // TODO(cgibson): We need a way to specify the direction of traffic.
        direction: Direction::Incoming,
        dst_subnet: from_cidr_address(&rule.selector.dst_address)?,
        dst_subnet_invert_match: false,
        keep_state: true,
        log: false,
        quick: false,
        src_subnet: from_cidr_address(&rule.selector.src_address)?,
        src_subnet_invert_match: false,
        src_port_range,
        dst_port_range,
        nic: nicid,
        // The proto field requires further processing, just set any value for now.
        proto: netfilter::SocketProtocol::Tcp,
    })
}

/// Parses a [`netconfig::Protocol`] and returns the equivalent [`netfilter::SocketProtocol`].
///
/// [`netfilter::SocketProtocol`] cannot represent multiple protocols at once (i.e: It does not have
/// a representation for "Both", or "All", etc.). "Both" is also the default when no protocol is
/// provided. Return `None` as the representation of "Both".
fn from_protocol(proto: Option<netconfig::Protocol>) -> Option<netfilter::SocketProtocol> {
    match proto {
        Some(proto) => match proto {
            netconfig::Protocol::Tcp => Some(netfilter::SocketProtocol::Tcp),
            netconfig::Protocol::Udp => Some(netfilter::SocketProtocol::Udp),
            netconfig::Protocol::Both => None,
        },
        None => None,
    }
}

/// Parses a [`netconfig::FilterAction`] and turns it into a [`netfilter::Action`].
fn from_filter_action(action: netconfig::FilterAction) -> netfilter::Action {
    match action {
        netconfig::FilterAction::Allow => netfilter::Action::Pass,
        // TODO(cgibson): What is our default drop policy? Should we gloss over the difference
        // to users of the Network Manager or should it become a parse error?
        netconfig::FilterAction::Drop => netfilter::Action::Drop,
    }
}

/// Parses a [`netconfig::PortRange`] and turns it into a [`netfilter::PortRange`].
fn from_port_range(range: &Option<Vec<netconfig::PortRange>>) -> Option<netfilter::PortRange> {
    match range {
        Some(ranges) => ranges
            .iter()
            .find(|_| true)
            .map(|range| netfilter::PortRange { start: range.from, end: range.to }),
        None => None,
    }
}

/// Parses a [`netconfig::CidrAddress`] and turns it into a [`fidl_fuchsia_net::Subnet`].
fn from_cidr_address(
    cidr_address: &Option<netconfig::CidrAddress>,
) -> error::Result<Option<Box<fidl_fuchsia_net::Subnet>>> {
    let (addr, prefix_len) = match cidr_address {
        Some(cidr_addr) => {
            let ip = match cidr_addr.address {
                Some(a) => a,
                None => {
                    return Err(error::NetworkManager::Service(
                        error::Service::ErrorParsingPacketFilterRule {
                            msg: "CidrAddress is missing an IP address".to_string(),
                        },
                    ))
                }
            };
            let prefix = match cidr_addr.prefix_length {
                Some(p) => p,
                None => {
                    return Err(error::NetworkManager::Service(
                        error::Service::ErrorParsingPacketFilterRule {
                            msg: "CidrAddress is missing the prefix length".to_string(),
                        },
                    ))
                }
            };
            (ip, prefix)
        }
        // A filter rule does not need a `CidrAddress`, it can be empty in the config for example.
        None => return Ok(None),
    };
    Ok(Some(Box::new(fidl_fuchsia_net::Subnet { addr, prefix_len })))
}

/// Parses a [`servicemgr::NatConfig`] and extracts the required fields.
fn from_nat_config(
    nat_config: &NatConfig,
) -> error::Result<(fidl_fuchsia_net::Subnet, fidl_fuchsia_net::IpAddress, u32)> {
    let src_subnet = match &nat_config.local_subnet {
        Some(subnet) => fidl_fuchsia_net::Subnet::from(subnet),
        None => {
            return Err(error::NetworkManager::Service(error::Service::NatConfigError {
                msg: "NatConfig must have a local_subnet set".to_string(),
            }))
        }
    };
    let wan_ip = match nat_config.global_ip {
        Some(lif) => match lif.address {
            IpAddr::V4(a) => fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                addr: a.octets(),
            }),
            IpAddr::V6(_) => {
                return Err(error::NetworkManager::Service(error::Service::NatConfigError {
                    msg: "IPv6 is not supported".to_string(),
                }))
            }
        },
        None => {
            return Err(error::NetworkManager::Service(error::Service::NatConfigError {
                msg: "NatConfig must have a global_ip set".to_string(),
            }))
        }
    };
    let nicid = match nat_config.pid {
        Some(pid) => pid.to_u32(),
        None => {
            return Err(error::NetworkManager::Service(error::Service::NatConfigError {
                msg: "NatConfig must have a pid set".to_string(),
            }))
        }
    };
    Ok((src_subnet, wan_ip, nicid))
}

/// Parses an [`config::AclEntry`]'s and turns it into a vector of [`netconfig::FilterRule`]'s.
///
/// A single `AclEntry` can break out a into multiple `FilterRule`'s for a variety of different
/// reasons, but mainly due to IPv4 and IPv6 packet filtering rules needing two separate entries.
fn parse_aclentry(entry: &AclEntry) -> error::Result<Vec<netconfig::FilterRule>> {
    let mut filter_rules = Vec::new();
    if let Some(ipv4) = entry.ipv4.as_ref() {
        let rule = ipv4.try_into()?;
        filter_rules.push(netconfig::FilterRule {
            action: netconfig::FilterAction::from(entry.config.forwarding_action.clone()),
            ..rule
        });
    }
    // TODO(cgibson): Add support for IPv6 rules.
    Ok(filter_rules)
}

/// Manages a Packet Filter connection to netstack filter service (netfilter).
///
/// Mainly serves as a wrapper around the netfilter service. Converts Network Manager FIDL APIs into
/// something that the netfilter service can understand. Finally converts the netfilter response
/// back into a Network Manager FIDL for consumption by the caller.
impl PacketFilter {
    /// Starts a new instance of a PacketFilter.
    pub fn start() -> error::Result<Self> {
        let filter_svc = connect_to_service::<FilterMarker>()?;
        info!("Connected to filter service");
        Ok(PacketFilter { filter_svc })
    }

    /// Returns the current set of netfilter packet filters.
    ///
    /// Using the existing handle to the netfilter service, request the set of packet filter rules
    /// and converts them to a vector of [`netconfig::FilterRule`]'s.
    ///
    /// # Error
    ///
    /// If the response from netfilter is anything other than [`netfilter::Status::Ok`] then
    /// produce an error result. Failure to convert from the [`netfilter::Rule`] to a
    /// [`netconfig::FilterRule`] produces an error result to the caller.
    pub(crate) async fn get_filters(&self) -> error::Result<Vec<netconfig::FilterRule>> {
        info!("Received request to get all active packet filters");
        let netfilter_rules: Vec<netfilter::Rule> = match self.filter_svc.get_rules().await {
            Ok((rules, _, Status::Ok)) => rules,
            Ok((_, _, status)) => {
                return Err(error::NetworkManager::Service(error::Service::FidlError {
                    msg: format!("Failed to get filters: Status was: {:?}", status),
                }))
            }
            Err(e) => {
                return Err(error::NetworkManager::Service(error::Service::FidlError {
                    msg: format!("Request to packet filter FIDL service failed: {}", e),
                }))
            }
        };

        netfilter_rules
            .into_iter()
            .map(|rule| match to_filter_rule(rule) {
                Ok(f) => Ok(f),
                Err(e) => Err(error::NetworkManager::Service(
                    error::Service::ErrorParsingPacketFilterRule {
                        msg: format!("Failed to parse filter rule: {:?}", e),
                    },
                )),
            })
            .collect::<error::Result<Vec<netconfig::FilterRule>>>()
    }

    /// Installs a new packet filter rule.
    ///
    /// We convert the [`netconfig::FilterRule`] and parse it into a [`netfilter::Rule`] that we
    /// can send on to the netfilter service. We also need to get a `generation` number from to
    /// include in the request.
    ///
    /// # Error
    ///
    /// If we fail to get the generation number from the netfilter service, or the result of the
    /// request to netfilter is anything other than [`netfilter::Status::Ok`] then produce an error
    /// result. Failure to convert the [`netconfig::FilterRule`] to a [`netfilter::Rule`] will
    /// produce an error result to the caller.
    pub(crate) async fn set_filter(
        &self,
        rule: &netconfig::FilterRule,
        nicid: u32,
    ) -> error::Result<()> {
        info!("Received request to add new packet filter rule");
        let new_rules = from_filter_rule(rule, nicid)?;

        let (mut existing_rules, generation) = match self.filter_svc.get_rules().await {
            Ok((rules, generation, Status::Ok)) => (rules, generation),
            Ok((_, _, status)) => {
                return Err(error::NetworkManager::Service(error::Service::FidlError {
                    msg: format!("Failed to get filters: Status was: {:?}", status),
                }))
            }
            Err(e) => {
                return Err(error::NetworkManager::Service(error::Service::FidlError {
                    msg: format!("Request to packet filter FIDL service failed: {}", e),
                }))
            }
        };

        // Join both the new rules and any existing rules that were already installed in the
        // system.
        existing_rules.extend(new_rules);

        info!("Installing new packet filter ruleset with generation number: {}", generation);

        // We don't update the generation number here, this is to avoid potential race conditions.
        match self.filter_svc.update_rules(&mut existing_rules.iter_mut(), generation).await {
            Ok(Status::Ok) => Ok(()),
            Ok(status) => {
                Err(error::NetworkManager::Service(error::Service::ErrorParsingPacketFilterRule {
                    msg: format!("Failed to add new packet filter: {:?}", status),
                }))
            }
            Err(e) => Err(error::NetworkManager::Service(error::Service::FidlError {
                msg: format!("Request to packet filter FIDL service failed: {:?}", e),
            })),
        }
    }

    /// Clears any existing filter rules.
    ///
    /// Netfilter does not have a `clear` or `delete` filter API. Instead, to clear all the filter
    /// ruleset, we have to install a new, empty, ruleset.
    pub(crate) async fn clear_filters(&self) -> error::Result<()> {
        let mut empty_ruleset = Vec::<netfilter::Rule>::new();
        let generation: u32 = match self.filter_svc.get_rules().await {
            Ok((_, generation, Status::Ok)) => generation,
            Ok((_, _, status)) => {
                warn!("Failed to get generation number! Status was: {:?}", status);
                return Err(error::NetworkManager::Service(
                    error::Service::ErrorClearingPacketFilterRules,
                ));
            }
            Err(e) => {
                warn!("fidl error: {:?}", e);
                return Err(error::NetworkManager::Service(
                    error::Service::ErrorClearingPacketFilterRules,
                ));
            }
        };
        match self.filter_svc.update_rules(&mut empty_ruleset.iter_mut(), generation).await {
            Ok(Status::Ok) => Ok(()),
            Ok(status) => {
                warn!("failed to clear filter state: {:?}", status);
                Err(error::NetworkManager::Service(error::Service::ErrorClearingPacketFilterRules))
            }
            Err(e) => {
                warn!("fidl error: {:?}", e);
                Err(error::NetworkManager::Service(error::Service::ErrorClearingPacketFilterRules))
            }
        }
    }

    /// Updates the NAT configuration.
    ///
    /// Allow incremental updates to the NAT configuration before installing the actual NAT rules
    /// once we have a complete configuration.
    pub(crate) async fn update_nat_config(&self, nat_config: &NatConfig) -> error::Result<()> {
        info!("Received request to update NAT config");
        let (src_subnet, wan_ip, nicid) = from_nat_config(nat_config).map_err(|e| {
            warn!("Failed to update NatConfig: {}", e);
            error::Service::UpdateNatPendingConfig
        })?;
        // Make sure that NAT is enabled before we try to install any rules.
        if !nat_config.enable {
            return Err(error::NetworkManager::Service(error::Service::NatNotEnabled));
        }
        // TODO(cgibson): NAT should work on IP packets, we shouldn't need to provide a proto field
        // here. This is a bug: fxbug.dev/35950.
        //
        // Ultimately this should all collapse into a single rule.
        let mut nat_rules = vec![
            netfilter::Nat {
                proto: netfilter::SocketProtocol::Tcp,
                src_subnet,
                new_src_addr: wan_ip,
                nic: nicid,
            },
            netfilter::Nat {
                proto: netfilter::SocketProtocol::Udp,
                src_subnet,
                new_src_addr: wan_ip,
                nic: nicid,
            },
            netfilter::Nat {
                proto: netfilter::SocketProtocol::Icmp,
                src_subnet,
                new_src_addr: wan_ip,
                nic: nicid,
            },
        ];

        // TODO(cgibson): We need to add an integration test that actually runs traffic so we can
        // see it being correctly forwarded with NAT enabled and disabled.
        self.install_nat_rules(&mut nat_rules).await
    }

    /// Installs a Network Address Translation (NAT) rule.
    ///
    /// This method calls the netfilter API to install a rule that rewrites source addresses on the
    /// LAN side with a publicly routable IP address (SNAT). Reverse translation (DNAT) is handled
    /// by netstack's connection state tracker. A complete [`servicemgr::NatConfig`] is required
    /// at this point.
    ///
    /// # Error
    ///
    /// If we fail to get the generation number from the netfilter service, or fail to update the
    /// the NAT ruleset, then we'll return an error result to the caller.
    async fn install_nat_rules(&self, nat_rules: &mut Vec<netfilter::Nat>) -> error::Result<()> {
        // Since we discard any existing NAT rules here, this is a pure "update-only" situation.
        let generation: u32 = match self.filter_svc.get_nat_rules().await {
            Ok((_, generation, Status::Ok)) => generation,
            Ok((_, _, status)) => {
                warn!("Failed to update NAT, status was: {:?}", status);
                return Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed));
            }
            Err(e) => {
                warn!("fidl error: {:?}", e);
                return Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed));
            }
        };
        match self.filter_svc.update_nat_rules(&mut nat_rules.iter_mut(), generation).await {
            Ok(Status::Ok) => Ok(()),
            Ok(status) => {
                warn!("Failed to set NAT state: {:?}", status);
                Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed))
            }
            Err(e) => {
                warn!("fidl error: {:?}", e);
                Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed))
            }
        }
    }

    /// Clears any existing Network Address Translation (NAT) rules.
    ///
    /// This is a convenience wrapper around netfilter's `update_nat_rules()` and always sets an
    /// empty ruleset.
    pub(crate) async fn clear_nat_rules(&self) -> error::Result<()> {
        let mut empty_ruleset = Vec::<netfilter::Nat>::new();
        let generation: u32 = match self.filter_svc.get_nat_rules().await {
            Ok((_, generation, Status::Ok)) => generation,
            Ok((_, _, status)) => {
                warn!("Failed to get generation number! Status was: {:?}", status);
                return Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed));
            }
            Err(e) => {
                warn!("fidl error: {:?}", e);
                return Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed));
            }
        };
        match self.filter_svc.update_nat_rules(&mut empty_ruleset.iter_mut(), generation).await {
            Ok(Status::Ok) => Ok(()),
            Ok(status) => {
                warn!("failed to set NAT state: {:?}", status);
                Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed))
            }
            Err(e) => {
                warn!("fidl error: {:?}", e);
                Err(error::NetworkManager::Service(error::Service::ErrorUpdateNatFailed))
            }
        }
    }

    /// Parses an [`config::AclEntry`] and turns it into a vector of [`netconfig::FilterRule`]'s.
    pub async fn parse_aclentry(
        &self,
        entry: &AclEntry,
    ) -> error::Result<Vec<netconfig::FilterRule>> {
        info!("Received a request to parse an AclEntry");
        parse_aclentry(entry)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::address::LifIpAddr;
    use crate::config::NetIpAddr;
    use crate::hal;
    use fidl_fuchsia_net::IpAddress::Ipv4;
    use fidl_fuchsia_net::Ipv4Address;
    use fidl_fuchsia_router_config::FilterAction;
    use std::net::IpAddr;

    #[test]
    fn test_convert_subnet_to_cidr_address() {
        let addr = [169, 254, 1, 1];
        let prefix = 32;
        let test_ip = fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr });
        let test_subnet = fidl_fuchsia_net::Subnet { addr: test_ip, prefix_len: prefix };

        // Convert our `test_subnet` to a `CidrAddress`.
        let cidr_addr = to_cidr_address(Some(Box::new(test_subnet))).unwrap();

        // Check that it succeeded: We can't access the IpAddress octets directly, so we need to use
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
        let port_ranges = to_port_range(netfilter::PortRange { start: 1000, end: 2000 });
        assert_eq!(port_ranges.is_some(), true);

        let p = port_ranges.unwrap();
        assert_eq!(p[0].from, 1000);
        assert_eq!(p[0].to, 2000);
    }

    #[test]
    fn test_to_protocol() {
        let tcp = to_protocol(netfilter::SocketProtocol::Tcp);
        assert_eq!(tcp.unwrap(), netconfig::Protocol::Tcp);

        let udp = to_protocol(netfilter::SocketProtocol::Udp);
        assert_eq!(udp.unwrap(), netconfig::Protocol::Udp);

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
            src_port_range: netfilter::PortRange { start: 1024, end: 1024 },
            dst_subnet,
            dst_subnet_invert_match: false,
            dst_port_range: netfilter::PortRange { start: 80, end: 80 },
            proto: netfilter::SocketProtocol::Tcp,
            nic: 0,
            log: false,
            keep_state: true,
        };

        let expected = netconfig::FilterRule {
            element: netconfig::Id {
                uuid: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                version: 0,
            },
            action: netconfig::FilterAction::Allow,
            selector: netconfig::FlowSelector {
                src_address: Some(netconfig::CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                src_ports: Some([netconfig::PortRange { from: 1024, to: 1024 }].to_vec()),
                dst_address: Some(netconfig::CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                dst_ports: Some([netconfig::PortRange { from: 80, to: 80 }].to_vec()),
                protocol: Some(netconfig::Protocol::Tcp),
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
            src_port_range: netfilter::PortRange { start: 1024, end: 1024 },
            dst_subnet,
            dst_subnet_invert_match: false,
            dst_port_range: netfilter::PortRange { start: 80, end: 80 },
            proto: netfilter::SocketProtocol::Tcp,
            nic: 1,
            log: false,
            keep_state: true,
        };

        let filter_rule = netconfig::FilterRule {
            element: netconfig::Id {
                uuid: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                version: 0,
            },
            action: FilterAction::Allow,
            selector: netconfig::FlowSelector {
                src_address: Some(netconfig::CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                src_ports: Some([netconfig::PortRange { from: 1024, to: 1024 }].to_vec()),
                dst_address: Some(netconfig::CidrAddress {
                    address: Some(Ipv4(Ipv4Address { addr: [169, 254, 0, 1] })),
                    prefix_length: Some(32),
                }),
                dst_ports: Some([netconfig::PortRange { from: 80, to: 80 }].to_vec()),
                protocol: Some(netconfig::Protocol::Tcp),
            },
        };
        let actual = from_filter_rule(&filter_rule, 1u32).unwrap();
        assert_eq!(1, actual.len());
        assert_eq!(expected, actual[0]);
    }

    #[test]
    fn test_from_protocol() {
        let tcp = from_protocol(Some(netconfig::Protocol::Tcp)).unwrap();
        assert_eq!(netfilter::SocketProtocol::Tcp, tcp);

        let udp = from_protocol(Some(netconfig::Protocol::Udp)).unwrap();
        assert_eq!(netfilter::SocketProtocol::Udp, udp);

        let both = from_protocol(Some(netconfig::Protocol::Both));
        assert_eq!(both.is_none(), true);
    }

    #[test]
    fn test_from_nat_config() {
        let expected_subnet_lifip =
            LifIpAddr { address: "192.168.0.0".parse().unwrap(), prefix: 24 };
        let expected_lifip = LifIpAddr { address: "17.0.0.1".parse().unwrap(), prefix: 32 };
        let ip: IpAddr = "192.168.0.0".parse().unwrap();
        let expected_subnet = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(Ipv4Address {
                addr: match ip {
                    std::net::IpAddr::V4(v4addr) => v4addr.octets(),
                    std::net::IpAddr::V6(_) => panic!("unexpected ipv6 address"),
                },
            }),
            prefix_len: 24,
        };
        let expected_pid = hal::PortId::from(1);

        // should fail if all fields are set to None.
        let mut nat_config = NatConfig {
            enable: true, // not being validated, so we don't care what this is set to.
            local_subnet: None,
            global_ip: None,
            pid: None,
        };
        let result = from_nat_config(&mut nat_config);
        assert_eq!(result.is_err(), true);

        // should fail if local_subnet is missing.
        let mut nat_config = NatConfig {
            enable: true, // not being validated, so we don't care what this is set to.
            local_subnet: None,
            global_ip: Some(expected_lifip.clone()),
            pid: Some(expected_pid),
        };
        let result = from_nat_config(&mut nat_config);
        assert_eq!(result.is_err(), true);

        // should fail if global_ip is missing.
        let mut nat_config = NatConfig {
            enable: true, // not being validated, so we don't care what this is set to.
            local_subnet: Some(expected_subnet_lifip.clone()),
            global_ip: None,
            pid: Some(expected_pid),
        };
        let result = from_nat_config(&mut nat_config);
        assert_eq!(result.is_err(), true);

        // should fail if pid is missing.
        let mut nat_config = NatConfig {
            enable: true, // not being validated, so we don't care what this is set to.
            local_subnet: Some(expected_subnet_lifip.clone()),
            global_ip: Some(expected_lifip.clone()),
            pid: None,
        };
        let result = from_nat_config(&mut nat_config);
        assert_eq!(result.is_err(), true);

        // should pass when all fields are present.
        let expected_wan_ip = fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
            addr: match expected_lifip.clone().address {
                IpAddr::V4(v4addr) => v4addr.octets(),
                IpAddr::V6(_) => panic!("unexpected ipv6 address"),
            },
        });
        let mut nat_config = NatConfig {
            enable: true, // not being validated, so we don't care what this is set to.
            local_subnet: Some(expected_subnet_lifip.clone()),
            global_ip: Some(expected_lifip.clone()),
            pid: Some(expected_pid),
        };
        let result = from_nat_config(&mut nat_config);
        assert_eq!(result.is_ok(), true);
        let (actual_subnet, actual_wan_ip, actual_nicid) = match result {
            Ok((s, w, n)) => (s, w, n),
            Err(e) => panic!("NatConfig test failed: {:?}", e),
        };
        assert_eq!(expected_subnet, actual_subnet);
        assert_eq!(expected_wan_ip, actual_wan_ip);
        assert_eq!(expected_pid.to_u32(), actual_nicid);
    }

    #[test]
    fn test_parse_aclentry() {
        let unspecified_ip_address = "0.0.0.0";
        let unspecified_prefix = 0u8;
        let unspecified_v4 = NetIpAddr(unspecified_ip_address.parse().unwrap());
        let ssh_port = 22u16;
        let block_ssh_on_wlan = AclEntry {
            config: config::FilterConfig {
                forwarding_action: config::ForwardingAction::Drop,
                device_id: Some("device_id".to_string()),
                direction: None,
                comment: Some("Blocks SSH access over the wlan".to_string()),
            },
            ipv4: Some(config::IpFilter {
                src_address: None,
                src_ports: None,
                dst_address: Some(config::CidrAddress {
                    ip: unspecified_v4,
                    prefix_length: unspecified_prefix,
                }),
                dst_ports: Some(config::PortRange { from: ssh_port, to: ssh_port }),
                protocol: Some(config::Protocol::Tcp),
            }),
            ipv6: None,
        };
        match parse_aclentry(&block_ssh_on_wlan) {
            Ok(v) => {
                assert_eq!(v.len(), 1);
                assert_eq!(v[0].element, netconfig::Id { uuid: [0; 16], version: 0 });
                assert_eq!(v[0].action, netconfig::FilterAction::Drop);
                assert_eq!(v[0].selector.src_address, None);
                assert_eq!(v[0].selector.src_ports, None);
                assert_eq!(
                    v[0].selector.dst_address,
                    Some(netconfig::CidrAddress {
                        address: Some(unspecified_v4.into()),
                        prefix_length: Some(unspecified_prefix),
                    })
                );
                assert_eq!(
                    v[0].selector.dst_ports,
                    Some(vec![netconfig::PortRange { from: ssh_port, to: ssh_port }])
                );
                assert_eq!(v[0].selector.protocol, Some(netconfig::Protocol::Tcp));
            }
            Err(e) => panic!("Unexpected 'Error' result: {:?}", e),
        }
    }

    #[test]
    fn test_gen_netfilter_rule() {
        let unspecified_ip_address = "0.0.0.0";
        let unspecified_prefix = 0u8;
        let unspecified_v4 = NetIpAddr(unspecified_ip_address.parse::<IpAddr>().unwrap());
        let ssh_port = 22u16;
        let unspecified_subnet = Some(Box::new(fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(Ipv4Address {
                addr: match unspecified_ip_address.parse().unwrap() {
                    std::net::IpAddr::V4(v4addr) => v4addr.octets(),
                    std::net::IpAddr::V6(_) => panic!("unexpected ipv6 address"),
                },
            }),
            prefix_len: 0,
        }));
        let rule = netconfig::FilterRule {
            element: netconfig::Id { uuid: [0; 16], version: 0 },
            action: FilterAction::Drop,
            selector: netconfig::FlowSelector {
                src_address: None,
                src_ports: None,
                dst_address: Some(netconfig::CidrAddress {
                    address: Some(unspecified_v4.into()),
                    prefix_length: Some(unspecified_prefix),
                }),
                dst_ports: Some(vec![netconfig::PortRange { from: ssh_port, to: ssh_port }]),
                protocol: Some(netconfig::Protocol::Tcp),
            },
        };
        let wlan_nicid = 31337;
        match gen_netfilter_rule(&rule, wlan_nicid) {
            Ok(actual) => {
                assert_eq!(actual.action, netfilter::Action::Drop);
                assert_eq!(actual.direction, netfilter::Direction::Incoming);
                assert_eq!(actual.quick, false);
                assert_eq!(actual.proto, netfilter::SocketProtocol::Tcp);
                assert_eq!(actual.src_subnet, None);
                assert_eq!(actual.src_subnet_invert_match, false);
                // src_port_range is not optional, so a range of 0-0 means any/not specified.
                assert_eq!(actual.src_port_range, netfilter::PortRange { start: 0, end: 0 });
                assert_eq!(actual.dst_subnet, unspecified_subnet);
                assert_eq!(actual.dst_subnet_invert_match, false);
                assert_eq!(
                    actual.dst_port_range,
                    netfilter::PortRange { start: ssh_port, end: ssh_port }
                );
                assert_eq!(actual.nic, wlan_nicid);
                assert_eq!(actual.log, false);
                assert_eq!(actual.keep_state, true);
            }
            Err(e) => panic!("Unexpected 'Error' result: {:?}", e),
        }
    }
}

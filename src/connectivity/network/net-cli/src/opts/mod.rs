// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use argh::FromArgs;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_interfaces as finterfaces;
use fidl_fuchsia_net_interfaces_ext as finterfaces_ext;
use fidl_fuchsia_net_stack as fnet_stack;
use std::convert::{TryFrom as _, TryInto as _};

pub(crate) mod dhcpd;
pub(crate) mod dns;

#[derive(thiserror::Error, Clone, Debug)]
#[error("{msg}")]
pub struct UserFacingError {
    pub msg: String,
}

pub fn user_facing_error(cause: impl Into<String>) -> anyhow::Error {
    UserFacingError { msg: cause.into() }.into()
}

pub fn underlying_user_facing_error(error: &anyhow::Error) -> Option<UserFacingError> {
    error.root_cause().downcast_ref::<UserFacingError>().cloned()
}

fn parse_ip_version_str(value: &str) -> Result<fnet::IpVersion, String> {
    match &value.to_lowercase()[..] {
        "ipv4" => Ok(fnet::IpVersion::V4),
        "ipv6" => Ok(fnet::IpVersion::V6),
        _ => Err("invalid IP version".to_string()),
    }
}

#[derive(FromArgs, Debug)]
/// commands for net-cli
pub struct Command {
    #[argh(subcommand)]
    pub cmd: CommandEnum,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum CommandEnum {
    Filter(Filter),
    If(If),
    Log(Log),
    Neigh(Neigh),
    Route(Route),
    Dhcp(Dhcp),
    Dhcpd(dhcpd::Dhcpd),
    Dns(dns::Dns),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "filter")]
/// commands for packet filter
pub struct Filter {
    #[argh(subcommand)]
    pub filter_cmd: FilterEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum FilterEnum {
    GetNatRules(FilterGetNatRules),
    GetRdrRules(FilterGetRdrRules),
    GetRules(FilterGetRules),
    SetNatRules(FilterSetNatRules),
    SetRdrRules(FilterSetRdrRules),
    SetRules(FilterSetRules),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "get-nat-rules")]
/// gets nat rules
pub struct FilterGetNatRules {}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "get-rdr-rules")]
/// gets rdr rules
pub struct FilterGetRdrRules {}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "get-rules")]
/// gets filter rules
pub struct FilterGetRules {}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "set-nat-rules")]
/// sets nat rules (see the netfilter::parser library for the NAT rules format)
pub struct FilterSetNatRules {
    #[argh(positional)]
    pub rules: String,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "set-rdr-rules")]
/// sets rdr rules (see the netfilter::parser library for the RDR rules format)
pub struct FilterSetRdrRules {
    #[argh(positional)]
    pub rules: String,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "set-rules")]
/// sets filter rules (see the netfilter::parser library for the rules format)
pub struct FilterSetRules {
    #[argh(positional)]
    pub rules: String,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "if")]
/// commands for network interfaces
pub struct If {
    #[argh(subcommand)]
    pub if_cmd: IfEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum IfEnum {
    Add(IfAdd),
    Addr(IfAddr),
    Bridge(IfBridge),
    Del(IfDel),
    Disable(IfDisable),
    Enable(IfEnable),
    Get(IfGet),
    IpForward(IfIpForward),
    List(IfList),
}

#[derive(Clone, Debug, PartialEq)]
pub enum InterfaceIdentifier {
    Id(u64),
    Name(String),
}

impl InterfaceIdentifier {
    pub async fn find_nicid<C>(&self, connector: &C) -> Result<u64, anyhow::Error>
    where
        C: crate::ServiceConnector<finterfaces::StateMarker>,
    {
        match self {
            Self::Id(id) => Ok(*id),
            Self::Name(name) => {
                let interfaces_state = crate::connect_with_context(connector).await?;
                let stream = finterfaces_ext::event_stream_from_state(&interfaces_state)?;
                let response =
                    finterfaces_ext::existing(stream, std::collections::HashMap::new()).await?;
                response
                    .values()
                    .find_map(|interface| (&interface.name == name).then(|| interface.id))
                    .ok_or_else(|| user_facing_error(format!("No interface with name {}", name)))
            }
        }
    }

    pub async fn find_u32_nicid<C>(&self, connector: &C) -> Result<u32, anyhow::Error>
    where
        C: crate::ServiceConnector<finterfaces::StateMarker>,
    {
        let id = self.find_nicid(connector).await?;
        u32::try_from(id).with_context(|| format!("nicid {} does not fit in u32", id))
    }
}

impl core::str::FromStr for InterfaceIdentifier {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let nicid_parse_result = s.parse::<u64>();
        nicid_parse_result.map_or_else(
            |nicid_parse_error| {
                if !s.starts_with("name:") {
                    Err(user_facing_error(format!(
                        "Failed to parse as NICID (error: {}) or as interface name \
                        (error: interface names must be specified as `name:ifname`, where \
                        ifname is the actual interface name in this example)",
                        nicid_parse_error
                    )))
                } else {
                    Ok(Self::Name(s["name:".len()..].to_string()))
                }
            },
            |nicid| Ok(Self::Id(nicid)),
        )
    }
}

impl From<u64> for InterfaceIdentifier {
    fn from(nicid: u64) -> InterfaceIdentifier {
        InterfaceIdentifier::Id(nicid)
    }
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "add")]
/// adds a network interface by path
pub struct IfAdd {
    // The path must yield a handle to a fuchsia.hardware.ethernet.Device interface.
    // Currently this means paths under /dev/class/ethernet.
    #[argh(positional)]
    pub path: String,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "addr")]
/// commands for updating network interface addresses
pub struct IfAddr {
    #[argh(subcommand)]
    pub addr_cmd: IfAddrEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum IfAddrEnum {
    Add(IfAddrAdd),
    Del(IfAddrDel),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "add")]
/// adds an address to the network interface
pub struct IfAddrAdd {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
    #[argh(positional)]
    pub addr: String,
    #[argh(positional, from_str_fn(parse_netmask_or_prefix_length))]
    pub prefix: u8,
    #[argh(switch)]
    /// skip adding a local subnet route for this interface and address
    pub no_subnet_route: bool,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "del")]
/// deletes an address from the network interface
pub struct IfAddrDel {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
    #[argh(positional)]
    pub addr: String,
    #[argh(positional, from_str_fn(parse_netmask_or_prefix_length))]
    pub prefix: Option<u8>,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "bridge")]
/// creates a bridge between network interfaces
pub struct IfBridge {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interfaces: Vec<InterfaceIdentifier>,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "del")]
/// removes a network interface
pub struct IfDel {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "disable")]
/// disables a network interface
pub struct IfDisable {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "enable")]
/// enables a network interface
pub struct IfEnable {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
/// queries a network interface
pub struct IfGet {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "ip-forward")]
/// get or set IP forwarding for an interface
pub struct IfIpForward {
    #[argh(subcommand)]
    pub cmd: IfIpForwardEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum IfIpForwardEnum {
    Show(IfIpForwardShow),
    Set(IfIpForwardSet),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "show")]
/// get IP forwarding for an interface
pub struct IfIpForwardShow {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,

    #[argh(positional, from_str_fn(parse_ip_version_str))]
    pub ip_version: fnet::IpVersion,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "set")]
/// set IP forwarding for an interface
pub struct IfIpForwardSet {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,

    #[argh(positional, from_str_fn(parse_ip_version_str))]
    pub ip_version: fnet::IpVersion,

    #[argh(positional)]
    pub enable: bool,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
/// lists network interfaces (supports ffx machine output)
pub struct IfList {
    #[argh(positional)]
    pub name_pattern: Option<String>,
    #[argh(switch)]
    /// format output as JSON
    pub json: bool,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "log")]
/// commands for logging
pub struct Log {
    #[argh(subcommand)]
    pub log_cmd: LogEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum LogEnum {
    SetPackets(LogSetPackets),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "set-packets")]
/// log packets to stdout
pub struct LogSetPackets {
    #[argh(positional)]
    pub enabled: bool,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "neigh")]
/// commands for neighbor tables
pub struct Neigh {
    #[argh(subcommand)]
    pub neigh_cmd: NeighEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum NeighEnum {
    Add(NeighAdd),
    Clear(NeighClear),
    Del(NeighDel),
    List(NeighList),
    Watch(NeighWatch),
    Config(NeighConfig),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "add")]
/// adds an entry to the neighbor table
pub struct NeighAdd {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
    #[argh(positional)]
    pub ip: fnet_ext::IpAddress,
    #[argh(positional)]
    pub mac: fnet_ext::MacAddress,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "clear")]
/// removes all entries associated with a network interface from the neighbor table
pub struct NeighClear {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,

    #[argh(positional, from_str_fn(parse_ip_version_str))]
    pub ip_version: fnet::IpVersion,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
/// lists neighbor table entries (supports ffx machine output)
pub struct NeighList {
    #[argh(switch)]
    /// format output as JSON
    pub json: bool,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "del")]
/// removes an entry from the neighbor table
pub struct NeighDel {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
    #[argh(positional)]
    pub ip: fnet_ext::IpAddress,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "watch")]
/// watches neighbor table entries for state changes (supports ffx machine output)
pub struct NeighWatch {
    #[argh(switch)]
    /// format output as newline-delimited JSON
    pub json_lines: bool,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "config")]
/// commands for the Neighbor Unreachability Detection configuration
pub struct NeighConfig {
    #[argh(subcommand)]
    pub neigh_config_cmd: NeighConfigEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum NeighConfigEnum {
    Get(NeighGetConfig),
    Update(NeighUpdateConfig),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
/// returns the current NUD configuration options for the provided interface
pub struct NeighGetConfig {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,

    #[argh(positional, from_str_fn(parse_ip_version_str))]
    pub ip_version: fnet::IpVersion,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "update")]
/// updates the current NUD configuration options for the provided interface
pub struct NeighUpdateConfig {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,

    #[argh(positional, from_str_fn(parse_ip_version_str))]
    pub ip_version: fnet::IpVersion,

    /// a base duration, in nanoseconds, for computing the random reachable
    /// time
    #[argh(option)]
    pub base_reachable_time: Option<i64>,

    /// learn `base_reachable_time` during runtime from the neighbor discovery
    /// protocol, if supported
    #[argh(option)]
    pub learn_base_reachable_time: Option<bool>,

    /// the minimum value of the random factor used for computing reachable
    /// time
    #[argh(option)]
    pub min_random_factor: Option<f32>,

    /// the maximum value of the random factor used for computing reachable
    /// time
    #[argh(option)]
    pub max_random_factor: Option<f32>,

    /// duration, in nanoseconds, between retransmissions of reachability
    /// probes in the PROBE state
    #[argh(option)]
    pub retransmit_timer: Option<i64>,

    /// learn `retransmit_timer` during runtime from the neighbor discovery
    /// protocol, if supported
    #[argh(option)]
    pub learn_retransmit_timer: Option<bool>,

    /// duration, in nanoseconds, to wait for a non-Neighbor-Discovery related
    /// protocol to reconfirm reachability after entering the DELAY state
    #[argh(option)]
    pub delay_first_probe_time: Option<i64>,

    /// the number of reachability probes to send before concluding negative
    /// reachability and deleting the entry from the INCOMPLETE state
    #[argh(option)]
    pub max_multicast_probes: Option<u32>,

    /// the number of reachability probes to send before concluding
    /// retransmissions from within the PROBE state should cease and the entry
    /// SHOULD be deleted
    #[argh(option)]
    pub max_unicast_probes: Option<u32>,

    /// if the target address is an anycast address, the stack SHOULD delay
    /// sending a response for a random time between 0 and this duration, in
    /// nanoseconds
    #[argh(option)]
    pub max_anycast_delay_time: Option<i64>,

    /// a node MAY send up to this amount of unsolicited reachability
    /// confirmations messages to all-nodes multicast address when a node
    /// determines its link-layer address has changed
    #[argh(option)]
    pub max_reachability_confirmations: Option<u32>,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "route")]
/// commands for routing tables
pub struct Route {
    #[argh(subcommand)]
    pub route_cmd: RouteEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum RouteEnum {
    List(RouteList),
    Add(RouteAdd),
    Del(RouteDel),
}

fn parse_netmask_or_prefix_length(s: &str) -> Result<u8, String> {
    let netmask_parse_result = s.parse::<std::net::IpAddr>();
    let prefix_len_parse_result = s.parse::<u8>();
    match (netmask_parse_result, prefix_len_parse_result) {
        (Err(netmask_parse_error), Err(prefix_len_parse_error)) => Err(format!(
            "Failed to parse as netmask (error: {}) or prefix length (error: {})",
            netmask_parse_error, prefix_len_parse_error,
        )),
        (Ok(_), Ok(_)) => Err(format!(
            "Input parses both as netmask and as prefix length. This should never happen."
        )),
        (Ok(netmask), Err(_)) => Ok(subnet_mask_to_prefix_length(netmask)),
        (Err(_), Ok(prefix_len)) => Ok(prefix_len),
    }
}

fn subnet_mask_to_prefix_length(addr: std::net::IpAddr) -> u8 {
    match addr {
        std::net::IpAddr::V4(addr) => (!u32::from_be_bytes(addr.octets())).leading_zeros(),
        std::net::IpAddr::V6(addr) => (!u128::from_be_bytes(addr.octets())).leading_zeros(),
    }
    .try_into()
    .unwrap()
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
/// lists devices (supports ffx machine output)
pub struct RouteList {
    #[argh(switch)]
    /// format output as JSON
    pub json: bool,
}

macro_rules! route_struct {
    ($ty_name:ident, $name:literal, $comment:expr) => {
        #[derive(FromArgs, Clone, Debug, PartialEq)]
        #[argh(subcommand, name = $name)]
        #[doc = $comment]
        pub struct $ty_name {
            #[argh(option)]
            /// the network id of the destination network
            pub destination: std::net::IpAddr,
            #[argh(
                option,
                arg_name = "netmask or prefix length",
                from_str_fn(parse_netmask_or_prefix_length),
                long = "netmask"
            )]
            /// the netmask or prefix length corresponding to destination
            pub prefix_len: u8,
            #[argh(option)]
            /// the ip address of the first hop router
            pub gateway: Option<std::net::IpAddr>,
            #[argh(
                option,
                arg_name = "nicid or name:ifname",
                default = "InterfaceIdentifier::Id(0)",
                long = "nicid"
            )]
            /// the outgoing network interface of the route
            pub interface: InterfaceIdentifier,
            #[argh(option, default = "0")]
            /// the metric for the route
            pub metric: u32,
        }

        impl $ty_name {
            pub fn into_route_table_entry(self, nicid: u32) -> fnet_stack::ForwardingEntry {
                let Self { destination, prefix_len, gateway, interface: _, metric } = self;
                fnet_stack::ForwardingEntry {
                    subnet: fnet_ext::apply_subnet_mask(fnet::Subnet {
                        addr: fnet_ext::IpAddress(destination).into(),
                        prefix_len,
                    }),
                    device_id: nicid.into(),
                    next_hop: gateway.map(|gateway| Box::new(fnet_ext::IpAddress(gateway).into())),
                    metric,
                }
            }
        }
    };
}

// TODO(https://github.com/google/argh/issues/48): do this more sanely.
route_struct!(RouteAdd, "add", "adds a route to the route table");
route_struct!(RouteDel, "del", "deletes a route from the route table");

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "dhcp")]
/// commands for an interfaces dhcp client
pub struct Dhcp {
    #[argh(subcommand)]
    pub dhcp_cmd: DhcpEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum DhcpEnum {
    Start(DhcpStart),
    Stop(DhcpStop),
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "start")]
/// starts a dhcp client on the interface
pub struct DhcpStart {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "stop")]
/// stops the dhcp client on the interface
pub struct DhcpStop {
    #[argh(positional, arg_name = "nicid or name:ifname")]
    pub interface: InterfaceIdentifier,
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use test_case::test_case;

    #[test_case("24", 24 ; "from prefix length")]
    #[test_case("255.255.254.0", 23 ; "from ipv4 netmask")]
    #[test_case("ffff:fff0::", 28 ; "from ipv6 netmask")]
    fn parse_prefix_len(to_parse: &str, want: u8) {
        let got = parse_netmask_or_prefix_length(to_parse).unwrap();
        assert_eq!(got, want)
    }

    proptest::proptest! {
        #[test]
        fn cant_parse_as_both_netmask_and_prefix_len(s: String) {
            let netmask_parse_result = s.parse::<std::net::IpAddr>();
            let prefix_len_parse_result = s.parse::<u8>();
            assert_matches!((netmask_parse_result, prefix_len_parse_result), (Ok(_), Err(_)) | (Err(_), Ok(_)) | (Err(_), Err(_)));
        }
    }

    #[test_case("1", InterfaceIdentifier::Id(1) ; "as nicid")]
    #[test_case("name:lo", InterfaceIdentifier::Name("lo".to_string()) ; "as ifname")]
    #[test_case("name:name:lo", InterfaceIdentifier::Name("name:lo".to_string()) ; "as ifname with 'name:' as part of name")]
    #[test_case("name:1", InterfaceIdentifier::Name("1".to_string()) ; "as numerical ifname")]
    fn parse_interface(to_parse: &str, want: InterfaceIdentifier) {
        let got = to_parse.parse::<InterfaceIdentifier>().unwrap();
        assert_eq!(got, want)
    }

    #[test]
    fn parse_interface_without_prefix_fails() {
        assert_matches!("lo".parse::<InterfaceIdentifier>(), Err(_))
    }

    #[test]
    fn into_route_table_entry_applies_subnet_mask() {
        // Leave off last two 16-bit segments.
        const PREFIX_LEN: u8 = 128 - 32;
        const ORIGINAL_NICID: u32 = 1;
        const NICID_TO_OVERWRITE_WITH: u32 = 2;
        assert_eq!(
            RouteAdd {
                destination: std::net::IpAddr::V6(std::net::Ipv6Addr::new(
                    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
                )),
                prefix_len: PREFIX_LEN,
                gateway: None,
                interface: u64::from(ORIGINAL_NICID).into(),
                metric: 100,
            }
            .into_route_table_entry(NICID_TO_OVERWRITE_WITH),
            fnet_stack::ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: net_declare::fidl_ip!("ffff:ffff:ffff:ffff:ffff:ffff:0:0"),
                    prefix_len: PREFIX_LEN,
                },
                // The interface ID in the RouteAdd struct should be overwritten by the NICID
                // parameter passed to `into_route_table_entry`.
                device_id: NICID_TO_OVERWRITE_WITH.into(),
                next_hop: None,
                metric: 100,
            }
        )
    }
}

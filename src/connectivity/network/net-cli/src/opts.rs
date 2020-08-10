// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use fidl_fuchsia_logger as logger;

fn parse_log_level_str(value: &str) -> Result<logger::LogLevelFilter, String> {
    match &value.to_lowercase()[..] {
        "trace" => Ok(logger::LogLevelFilter::Trace),
        "debug" => Ok(logger::LogLevelFilter::Debug),
        "info" => Ok(logger::LogLevelFilter::Info),
        "warn" => Ok(logger::LogLevelFilter::Warn),
        "error" => Ok(logger::LogLevelFilter::Error),
        "fatal" => Ok(logger::LogLevelFilter::Fatal),
        _ => Err("invalid log level".to_string()),
    }
}

#[derive(FromArgs)]
/// commands for net-cli
pub struct Command {
    #[argh(subcommand)]
    pub cmd: CommandEnum,
}

#[derive(FromArgs)]
#[argh(subcommand)]
pub enum CommandEnum {
    Filter(Filter),
    Fwd(Fwd),
    If(If),
    Log(Log),
    Route(Route),
    Stat(Stat),
    Metric(Metric),
    Dhcp(Dhcp),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "filter")]
/// commands for packet filter
pub struct Filter {
    #[argh(subcommand)]
    pub filter_cmd: FilterEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum FilterEnum {
    Disable(FilterDisable),
    Enable(FilterEnable),
    GetNatRules(FilterGetNatRules),
    GetRdrRules(FilterGetRdrRules),
    GetRules(FilterGetRules),
    IsEnabled(FilterIsEnabled),
    SetNatRules(FilterSetNatRules),
    SetRdrRules(FilterSetRdrRules),
    SetRules(FilterSetRules),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "disable")]
/// disables the packet filter
pub struct FilterDisable {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "enable")]
/// enables the packet filter
pub struct FilterEnable {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "get-nat-rules")]
/// gets nat rules
pub struct FilterGetNatRules {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "get-rdr-rules")]
/// gets rdr rules
pub struct FilterGetRdrRules {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "get-rules")]
/// gets filter rules
pub struct FilterGetRules {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "is-enabled")]
/// is the packet filter enabled?
pub struct FilterIsEnabled {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "set-nat-rules")]
/// sets nat rules (see the netfilter::parser library for the NAT rules format)
pub struct FilterSetNatRules {
    #[argh(positional)]
    /// nat rules
    pub rules: String,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "set-rdr-rules")]
/// sets rdr rules (see the netfilter::parser library for the RDR rules format)
pub struct FilterSetRdrRules {
    #[argh(positional)]
    /// rdr rules
    pub rules: String,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "set-rules")]
/// sets filter rules (see the netfilter::parser library for the rules format)
pub struct FilterSetRules {
    #[argh(positional)]
    /// rules
    pub rules: String,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "fwd")]
/// commands for forwarding tables
pub struct Fwd {
    #[argh(subcommand)]
    pub fwd_cmd: FwdEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum FwdEnum {
    AddDevice(FwdAddDevice),
    AddHop(FwdAddHop),
    Del(FwdDel),
    List(FwdList),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "add-device")]
/// adds a forwarding table entry to route to a device
pub struct FwdAddDevice {
    #[argh(positional)]
    /// id of the network interface to route to
    pub id: u64,
    #[argh(positional)]
    /// address portion of the subnet for this forwarding rule
    pub addr: String,
    #[argh(positional)]
    /// routing prefix for this forwarding rule
    pub prefix: u8,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "add-hop")]
/// adds a forwarding table entry to route to a IP address
pub struct FwdAddHop {
    #[argh(positional)]
    /// ip address of the next hop to route to
    pub next_hop: String,
    #[argh(positional)]
    /// address portion of the subnet for this forwarding rule
    pub addr: String,
    #[argh(positional)]
    /// routing prefix for this forwarding rule
    pub prefix: u8,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "del")]
/// deletes a forwarding table entry
pub struct FwdDel {
    #[argh(positional)]
    /// address portion of the subnet for this forwarding rule
    pub addr: String,
    #[argh(positional)]
    /// routing prefix for this forwarding rule
    pub prefix: u8,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "list")]
/// lists forwarding table entries
pub struct FwdList {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "if")]
/// commands for network interfaces
pub struct If {
    #[argh(subcommand)]
    pub if_cmd: IfEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum IfEnum {
    Add(IfAdd),
    Addr(IfAddr),
    Del(IfDel),
    Disable(IfDisable),
    Enable(IfEnable),
    Get(IfGet),
    List(IfList),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "add")]
/// adds a network interface by path
pub struct IfAdd {
    // The path must yield a handle to a fuchsia.hardware.ethernet.Device interface.
    // Currently this means paths under /dev/class/ethernet.
    #[argh(positional)]
    /// path to the device to add
    pub path: String,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "addr")]
/// commands for updates network interface addresses
pub struct IfAddr {
    #[argh(subcommand)]
    pub addr_cmd: IfAddrEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum IfAddrEnum {
    Add(IfAddrAdd),
    Del(IfAddrDel),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "add")]
/// adds an address to the network interface
pub struct IfAddrAdd {
    #[argh(positional)]
    /// id of the network interface
    pub id: u64,
    #[argh(positional)]
    /// addr of the network interface
    pub addr: String,
    #[argh(positional)]
    /// prefix of the network interface
    pub prefix: u8,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "del")]
/// deletes an address from the network interface
pub struct IfAddrDel {
    #[argh(positional)]
    /// id of the network interface
    pub id: u64,
    #[argh(positional)]
    /// addr of the network interface
    pub addr: String,
    #[argh(positional)]
    /// optional address subnet prefix (defaults to 32 for v4, 128 for v6)
    pub prefix: Option<u8>,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "del")]
/// removes a network interface
pub struct IfDel {
    #[argh(positional)]
    /// id of the network interface to remove
    pub id: u64,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "disable")]
/// disables a network interface
pub struct IfDisable {
    #[argh(positional)]
    /// id of the network interface to disable
    pub id: u64,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "enable")]
/// enables a network interface
pub struct IfEnable {
    #[argh(positional)]
    /// id of the network interface to enable
    pub id: u64,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "get")]
/// queries a network interface
pub struct IfGet {
    #[argh(positional)]
    /// id of the network interface to query
    pub id: u64,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "list")]
/// lists network interfaces
pub struct IfList {
    #[argh(positional)]
    /// name substring to be matched
    pub name_pattern: Option<String>,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "log")]
/// commands for logging
pub struct Log {
    #[argh(subcommand)]
    pub log_cmd: LogEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum LogEnum {
    SetLevel(LogSetLevel),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "set-level")]
/// syslog severity level / loglevel
pub struct LogSetLevel {
    #[argh(positional, from_str_fn(parse_log_level_str))]
    /// log level
    pub log_level: logger::LogLevelFilter,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "route")]
/// commands for routing tables
pub struct Route {
    #[argh(subcommand)]
    pub route_cmd: RouteEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum RouteEnum {
    List(RouteList),
    Add(RouteAdd),
    Del(RouteDel),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "list")]
/// lists devices
pub struct RouteList {}

macro_rules! route_struct {
    ($ty_name:ident, $name:literal, $comment:expr) => {
        #[derive(FromArgs, Clone, Debug)]
        #[argh(subcommand, name = $name)]
        #[doc = $comment]
        pub struct $ty_name {
            #[argh(option)]
            /// the network id of the destination network
            pub destination: std::net::IpAddr,
            #[argh(option)]
            /// the netmask corresponding to destination
            pub netmask: std::net::IpAddr,
            #[argh(option)]
            /// the ip address of the first hop router
            pub gateway: Option<std::net::IpAddr>,
            #[argh(option)]
            /// the outgoing network interface id of the route
            pub nicid: u32,
            #[argh(option)]
            /// the metric for the route
            pub metric: u32,
        }

        impl Into<fidl_fuchsia_netstack::RouteTableEntry2> for $ty_name {
            fn into(self) -> fidl_fuchsia_netstack::RouteTableEntry2 {
                let Self {
                    destination,
                    netmask,
                    gateway,
                    nicid,
                    metric,
                } = self;
                fidl_fuchsia_netstack::RouteTableEntry2 {
                    destination: fidl_fuchsia_net_ext::IpAddress(destination).into(),
                    netmask: fidl_fuchsia_net_ext::IpAddress(netmask).into(),
                    gateway: gateway.map(|gateway| {
                        Box::new(fidl_fuchsia_net_ext::IpAddress(gateway).into())
                    }),
                    nicid,
                    metric,
                }
            }
        }
    };
}

// TODO(https://github.com/google/argh/issues/48): do this more sanely.
route_struct!(RouteAdd, "add", "adds a route to the route table");
route_struct!(RouteDel, "del", "deletes a route from the route table");

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "stat")]
/// commands for aggregates statistics
pub struct Stat {
    #[argh(subcommand)]
    pub stat_cmd: StatEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum StatEnum {
    Show(StatShow),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "show")]
/// shows classified netstack stats
pub struct StatShow {}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "metric")]
/// commands for interface route metrics
pub struct Metric {
    #[argh(subcommand)]
    pub metric_cmd: MetricEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum MetricEnum {
    Set(MetricSet),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "set")]
/// assigns a route metric to the network interface
pub struct MetricSet {
    #[argh(positional)]
    /// id of the network interface to assign the route metric to
    // NOTE: id is a u32 because fuchsia.netstack interfaces take u32 interface ids.
    // TODO: change id to u64 once fuchsia.netstack is no longer in use.
    pub id: u32,
    #[argh(positional)]
    /// route metric to be assigned
    pub metric: u32,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "dhcp")]
/// commands for an interfaces dhcp client
pub struct Dhcp {
    #[argh(subcommand)]
    pub dhcp_cmd: DhcpEnum,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand)]
pub enum DhcpEnum {
    Start(DhcpStart),
    Stop(DhcpStop),
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "start")]
/// starts a dhcp client on the interface
pub struct DhcpStart {
    #[argh(positional)]
    /// id of the network interface for which a dhcp client will be started
    pub id: u32,
}

#[derive(FromArgs, Clone, Debug)]
#[argh(subcommand, name = "stop")]
/// stops the dhcp client on the interface
pub struct DhcpStop {
    #[argh(positional)]
    /// id of the network interface for which the dhcp client will be stopped
    pub id: u32,
}

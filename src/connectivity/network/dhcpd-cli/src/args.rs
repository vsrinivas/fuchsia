// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use argh::FromArgs;
use fidl_fuchsia_hardware_ethernet_ext::MacAddress as MacAddr;
use std::net::Ipv4Addr;
use std::string::ToString;

/// A command-line tool for administrating dhcpd.
#[derive(Debug, FromArgs)]
#[argh(name = "dhcpd-cli")]
pub struct Cli {
    /// the primary command to execute.
    #[argh(subcommand)]
    pub cmd: Command,
}

/// The primary command to execute.
#[derive(Debug, FromArgs)]
#[argh(subcommand)]
pub enum Command {
    /// a primary command to retrieve the value of a DHCP option or server parameter.
    Get(Get),
    /// a primary command to set the value of a DHCP option or server parameter.
    Set(Set),
    /// a primary command to list the values of all DHCP options or server parameters.
    List(List),
}

/// A primary command to retrieve the value of a DHCP option or server parameter.
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "get")]
pub struct Get {
    #[argh(subcommand)]
    pub arg: GetArg,
}

/// A primary command to set the value of a DHCP option or server parameter.
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "set")]
pub struct Set {
    #[argh(subcommand)]
    pub arg: SetArg,
}

/// A primary command to list the values of all DHCP options or server parameters.
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "list")]
pub struct List {
    #[argh(subcommand)]
    pub arg: ListArg,
}

#[derive(Debug, FromArgs)]
#[argh(subcommand)]
pub enum GetArg {
    /// a subcommand to specify retrieving a DHCP option.
    Option(OptionArg),
    /// a subcommand to specify retrieving a server paramter.
    Parameter(ParameterArg),
}
/// A primary command argument to set the value of a DHCP option or server parameter.
#[derive(Debug, FromArgs)]
#[argh(subcommand)]
pub enum SetArg {
    Option(OptionArg),
    Parameter(ParameterArg),
}

/// A primary command argument to list the values of all DHCP options or server parameters.
#[derive(Debug, FromArgs)]
#[argh(subcommand)]
pub enum ListArg {
    Option(OptionToken),
    Parameter(ParameterToken),
}

/// A secondary command indicating a DHCP option argument.
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "option")]
pub struct OptionArg {
    /// the name of the DHCP option to operate on.
    #[argh(subcommand)]
    pub name: Option_,
}

/// A secondary command indicating a server parameter argument.
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "parameter")]
pub struct ParameterArg {
    /// the name of the server parameter to operate on.
    #[argh(subcommand)]
    pub name: Parameter,
}
/// Perform the command on DHCP options.
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "option")]
pub struct OptionToken {}

/// Perform the command on server parameters.
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "parameter")]
pub struct ParameterToken {}

/// The name of the DHCP option to operate on.
#[derive(Clone, Debug, FromArgs)]
#[argh(subcommand)]
pub enum Option_ {
    SubnetMask(SubnetMask),
    TimeOffset(TimeOffset),
    Router(Router),
    TimeServer(TimeServer),
    NameServer(NameServer),
    DomainNameServer(DomainNameServer),
    LogServer(LogServer),
    CookieServer(CookieServer),
    LprServer(LprServer),
    ImpressServer(ImpressServer),
    ResourceLocationServer(ResourceLocationServer),
    HostName(HostName),
    BootFileSize(BootFileSize),
    MeritDumpFile(MeritDumpFile),
    DomainName(DomainName),
    SwapServer(SwapServer),
    RootPath(RootPath),
    ExtensionsPath(ExtensionsPath),
    IpForwarding(IpForwarding),
    NonLocalSourceRouting(NonLocalSourceRouting),
    PolicyFilter(PolicyFilter),
    MaxDatagramReassemblySize(MaxDatagramReassemblySize),
    DefaultIpTtl(DefaultIpTtl),
    PathMtuAgingTimeout(PathMtuAgingTimeout),
    PathMtuPlateauTable(PathMtuPlateauTable),
    InterfaceMtu(InterfaceMtu),
    AllSubnetsLocal(AllSubnetsLocal),
    BroadcastAddress(BroadcastAddress),
    PerformMaskDiscovery(PerformMaskDiscovery),
    MaskSupplier(MaskSupplier),
    PerformRouterDiscovery(PerformRouterDiscovery),
    RouterSolicitationAddress(RouterSolicitationAddress),
    StaticRoute(StaticRoute),
    TrailerEncapsulation(TrailerEncapsulation),
    ArpCacheTimeout(ArpCacheTimeout),
    EthernetEncapsulation(EthernetEncapsulation),
    TcpDefaultTtl(TcpDefaultTtl),
    TcpKeepaliveInterval(TcpKeepaliveInterval),
    TcpKeepaliveGarbage(TcpKeepaliveGarbage),
    NetworkInformationServiceDomain(NetworkInformationServiceDomain),
    NetworkInformationServers(NetworkInformationServers),
    NetworkTimeProtocolServers(NetworkTimeProtocolServers),
    VendorSpecificInformation(VendorSpecificInformation),
    NetBiosOverTcpipNameServer(NetBiosOverTcpipNameServer),
    NetBiosOverTcpipDatagramDistributionServer(NetBiosOverTcpipDatagramDistributionServer),
    NetBiosOverTcpipNodeType(NetBiosOverTcpipNodeType),
    NetBiosOverTcpipScope(NetBiosOverTcpipScope),
    XWindowSystemFontServer(XWindowSystemFontServer),
    XWindowSystemDisplayManager(XWindowSystemDisplayManager),
    NetworkInformationServicePlusDomain(NetworkInformationServicePlusDomain),
    NetworkInformationServicePlusServers(NetworkInformationServicePlusServers),
    MobileIpHomeAgent(MobileIpHomeAgent),
    SmtpServer(SmtpServer),
    Pop3Server(Pop3Server),
    NntpServer(NntpServer),
    DefaultWwwServer(DefaultWwwServer),
    DefaultFingerServer(DefaultFingerServer),
    DefaultIrcServer(DefaultIrcServer),
    StreettalkServer(StreettalkServer),
    StreettalkDirectoryAssistanceServer(StreettalkDirectoryAssistanceServer),
    TftpServerName(TftpServerName),
    BootfileName(BootfileName),
    MaxDhcpMessageSize(MaxDhcpMessageSize),
    RenewalTimeValue(RenewalTimeValue),
    RebindingTimeValue(RebindingTimeValue),
}

impl Into<fidl_fuchsia_net_dhcp::OptionCode> for Option_ {
    fn into(self) -> fidl_fuchsia_net_dhcp::OptionCode {
        match self {
            Option_::SubnetMask(_) => fidl_fuchsia_net_dhcp::OptionCode::SubnetMask,
            Option_::TimeOffset(_) => fidl_fuchsia_net_dhcp::OptionCode::TimeOffset,
            Option_::Router(_) => fidl_fuchsia_net_dhcp::OptionCode::Router,
            Option_::TimeServer(_) => fidl_fuchsia_net_dhcp::OptionCode::TimeServer,
            Option_::NameServer(_) => fidl_fuchsia_net_dhcp::OptionCode::NameServer,
            Option_::DomainNameServer(_) => fidl_fuchsia_net_dhcp::OptionCode::DomainNameServer,
            Option_::LogServer(_) => fidl_fuchsia_net_dhcp::OptionCode::LogServer,
            Option_::CookieServer(_) => fidl_fuchsia_net_dhcp::OptionCode::CookieServer,
            Option_::LprServer(_) => fidl_fuchsia_net_dhcp::OptionCode::LprServer,
            Option_::ImpressServer(_) => fidl_fuchsia_net_dhcp::OptionCode::ImpressServer,
            Option_::ResourceLocationServer(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::ResourceLocationServer
            }
            Option_::HostName(_) => fidl_fuchsia_net_dhcp::OptionCode::HostName,
            Option_::BootFileSize(_) => fidl_fuchsia_net_dhcp::OptionCode::BootFileSize,
            Option_::MeritDumpFile(_) => fidl_fuchsia_net_dhcp::OptionCode::MeritDumpFile,
            Option_::DomainName(_) => fidl_fuchsia_net_dhcp::OptionCode::DomainName,
            Option_::SwapServer(_) => fidl_fuchsia_net_dhcp::OptionCode::SwapServer,
            Option_::RootPath(_) => fidl_fuchsia_net_dhcp::OptionCode::RootPath,
            Option_::ExtensionsPath(_) => fidl_fuchsia_net_dhcp::OptionCode::ExtensionsPath,
            Option_::IpForwarding(_) => fidl_fuchsia_net_dhcp::OptionCode::IpForwarding,
            Option_::NonLocalSourceRouting(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NonLocalSourceRouting
            }
            Option_::PolicyFilter(_) => fidl_fuchsia_net_dhcp::OptionCode::PolicyFilter,
            Option_::MaxDatagramReassemblySize(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::MaxDatagramReassemblySize
            }
            Option_::DefaultIpTtl(_) => fidl_fuchsia_net_dhcp::OptionCode::DefaultIpTtl,
            Option_::PathMtuAgingTimeout(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::PathMtuAgingTimeout
            }
            Option_::PathMtuPlateauTable(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::PathMtuPlateauTable
            }
            Option_::InterfaceMtu(_) => fidl_fuchsia_net_dhcp::OptionCode::InterfaceMtu,
            Option_::AllSubnetsLocal(_) => fidl_fuchsia_net_dhcp::OptionCode::AllSubnetsLocal,
            Option_::BroadcastAddress(_) => fidl_fuchsia_net_dhcp::OptionCode::BroadcastAddress,
            Option_::PerformMaskDiscovery(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::PerformMaskDiscovery
            }
            Option_::MaskSupplier(_) => fidl_fuchsia_net_dhcp::OptionCode::MaskSupplier,
            Option_::PerformRouterDiscovery(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::PerformRouterDiscovery
            }
            Option_::RouterSolicitationAddress(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::RouterSolicitationAddress
            }
            Option_::StaticRoute(_) => fidl_fuchsia_net_dhcp::OptionCode::StaticRoute,
            Option_::TrailerEncapsulation(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::TrailerEncapsulation
            }
            Option_::ArpCacheTimeout(_) => fidl_fuchsia_net_dhcp::OptionCode::ArpCacheTimeout,
            Option_::EthernetEncapsulation(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::EthernetEncapsulation
            }
            Option_::TcpDefaultTtl(_) => fidl_fuchsia_net_dhcp::OptionCode::TcpDefaultTtl,
            Option_::TcpKeepaliveInterval(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::TcpKeepaliveInterval
            }
            Option_::TcpKeepaliveGarbage(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::TcpKeepaliveGarbage
            }
            Option_::NetworkInformationServiceDomain(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetworkInformationServiceDomain
            }
            Option_::NetworkInformationServers(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetworkInformationServers
            }
            Option_::NetworkTimeProtocolServers(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetworkTimeProtocolServers
            }
            Option_::VendorSpecificInformation(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::VendorSpecificInformation
            }
            Option_::NetBiosOverTcpipNameServer(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetbiosOverTcpipNameServer
            }
            Option_::NetBiosOverTcpipDatagramDistributionServer(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetbiosOverTcpipDatagramDistributionServer
            }
            Option_::NetBiosOverTcpipNodeType(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetbiosOverTcpipNodeType
            }
            Option_::NetBiosOverTcpipScope(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetbiosOverTcpipScope
            }
            Option_::XWindowSystemFontServer(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::XWindowSystemFontServer
            }
            Option_::XWindowSystemDisplayManager(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::XWindowSystemDisplayManager
            }
            Option_::NetworkInformationServicePlusDomain(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetworkInformationServicePlusDomain
            }
            Option_::NetworkInformationServicePlusServers(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::NetworkInformationServicePlusServers
            }
            Option_::MobileIpHomeAgent(_) => fidl_fuchsia_net_dhcp::OptionCode::MobileIpHomeAgent,
            Option_::SmtpServer(_) => fidl_fuchsia_net_dhcp::OptionCode::SmtpServer,
            Option_::Pop3Server(_) => fidl_fuchsia_net_dhcp::OptionCode::Pop3Server,
            Option_::NntpServer(_) => fidl_fuchsia_net_dhcp::OptionCode::NntpServer,
            Option_::DefaultWwwServer(_) => fidl_fuchsia_net_dhcp::OptionCode::DefaultWwwServer,
            Option_::DefaultFingerServer(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::DefaultFingerServer
            }
            Option_::DefaultIrcServer(_) => fidl_fuchsia_net_dhcp::OptionCode::DefaultIrcServer,
            Option_::StreettalkServer(_) => fidl_fuchsia_net_dhcp::OptionCode::StreettalkServer,
            Option_::StreettalkDirectoryAssistanceServer(_) => {
                fidl_fuchsia_net_dhcp::OptionCode::StreettalkDirectoryAssistanceServer
            }
            Option_::TftpServerName(_) => fidl_fuchsia_net_dhcp::OptionCode::TftpServerName,
            Option_::BootfileName(_) => fidl_fuchsia_net_dhcp::OptionCode::BootfileName,
            Option_::MaxDhcpMessageSize(_) => fidl_fuchsia_net_dhcp::OptionCode::MaxDhcpMessageSize,
            Option_::RenewalTimeValue(_) => fidl_fuchsia_net_dhcp::OptionCode::RenewalTimeValue,
            Option_::RebindingTimeValue(_) => fidl_fuchsia_net_dhcp::OptionCode::RebindingTimeValue,
        }
    }
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for Option_ {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        match self {
            Option_::SubnetMask(v) => v.into(),
            Option_::TimeOffset(v) => v.into(),
            Option_::Router(v) => v.into(),
            Option_::TimeServer(v) => v.into(),
            Option_::NameServer(v) => v.into(),
            Option_::DomainNameServer(v) => v.into(),
            Option_::LogServer(v) => v.into(),
            Option_::CookieServer(v) => v.into(),
            Option_::LprServer(v) => v.into(),
            Option_::ImpressServer(v) => v.into(),
            Option_::ResourceLocationServer(v) => v.into(),
            Option_::HostName(v) => v.into(),
            Option_::BootFileSize(v) => v.into(),
            Option_::MeritDumpFile(v) => v.into(),
            Option_::DomainName(v) => v.into(),
            Option_::SwapServer(v) => v.into(),
            Option_::RootPath(v) => v.into(),
            Option_::ExtensionsPath(v) => v.into(),
            Option_::IpForwarding(v) => v.into(),
            Option_::NonLocalSourceRouting(v) => v.into(),
            Option_::PolicyFilter(v) => v.into(),
            Option_::MaxDatagramReassemblySize(v) => v.into(),
            Option_::DefaultIpTtl(v) => v.into(),
            Option_::PathMtuAgingTimeout(v) => v.into(),
            Option_::PathMtuPlateauTable(v) => v.into(),
            Option_::InterfaceMtu(v) => v.into(),
            Option_::AllSubnetsLocal(v) => v.into(),
            Option_::BroadcastAddress(v) => v.into(),
            Option_::PerformMaskDiscovery(v) => v.into(),
            Option_::MaskSupplier(v) => v.into(),
            Option_::PerformRouterDiscovery(v) => v.into(),
            Option_::RouterSolicitationAddress(v) => v.into(),
            Option_::StaticRoute(v) => v.into(),
            Option_::TrailerEncapsulation(v) => v.into(),
            Option_::ArpCacheTimeout(v) => v.into(),
            Option_::EthernetEncapsulation(v) => v.into(),
            Option_::TcpDefaultTtl(v) => v.into(),
            Option_::TcpKeepaliveInterval(v) => v.into(),
            Option_::TcpKeepaliveGarbage(v) => v.into(),
            Option_::NetworkInformationServiceDomain(v) => v.into(),
            Option_::NetworkInformationServers(v) => v.into(),
            Option_::NetworkTimeProtocolServers(v) => v.into(),
            Option_::VendorSpecificInformation(v) => v.into(),
            Option_::NetBiosOverTcpipNameServer(v) => v.into(),
            Option_::NetBiosOverTcpipDatagramDistributionServer(v) => v.into(),
            Option_::NetBiosOverTcpipNodeType(v) => v.into(),
            Option_::NetBiosOverTcpipScope(v) => v.into(),
            Option_::XWindowSystemFontServer(v) => v.into(),
            Option_::XWindowSystemDisplayManager(v) => v.into(),
            Option_::NetworkInformationServicePlusDomain(v) => v.into(),
            Option_::NetworkInformationServicePlusServers(v) => v.into(),
            Option_::MobileIpHomeAgent(v) => v.into(),
            Option_::SmtpServer(v) => v.into(),
            Option_::Pop3Server(v) => v.into(),
            Option_::NntpServer(v) => v.into(),
            Option_::DefaultWwwServer(v) => v.into(),
            Option_::DefaultFingerServer(v) => v.into(),
            Option_::DefaultIrcServer(v) => v.into(),
            Option_::StreettalkServer(v) => v.into(),
            Option_::StreettalkDirectoryAssistanceServer(v) => v.into(),
            Option_::TftpServerName(v) => v.into(),
            Option_::BootfileName(v) => v.into(),
            Option_::MaxDhcpMessageSize(v) => v.into(),
            Option_::RenewalTimeValue(v) => v.into(),
            Option_::RebindingTimeValue(v) => v.into(),
        }
    }
}

/// The client's subnet mask.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "subnet-mask")]
pub struct SubnetMask {
    /// a 32-bit IPv4 subnet mask.
    #[argh(option)]
    mask: Option<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for SubnetMask {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let mask = self.mask.unwrap_or(Ipv4Addr::new(0, 0, 0, 0));
        fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_fuchsia_net::Ipv4Address {
            addr: mask.octets(),
        })
    }
}

/// The client's offset from UTC.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "time-offset")]
pub struct TimeOffset {
    /// the client's offset from UTC in seconds. A positive offset is east of the zero meridian, and
    /// a negative offset is west of the zero meridian.
    #[argh(option)]
    offset: Option<i32>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for TimeOffset {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let offset = self.offset.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::TimeOffset(offset)
    }
}

/// The routers within a client's subnet.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "router")]
pub struct Router {
    /// a list of the routers in a client's subnet, listed in order of preference.
    #[argh(option)]
    routers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for Router {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let routers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .routers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::Router(routers)
    }
}

/// Time Protocol servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "time-server")]
pub struct TimeServer {
    /// a list of time servers available to the client, in order of preference.
    #[argh(option)]
    time_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for TimeServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let time_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .time_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::TimeServer(time_servers)
    }
}

/// IEN 116 Name servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "name-server")]
pub struct NameServer {
    /// a list of IEN 116 Name servers available to the client, in order of preference.
    #[argh(option)]
    name_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NameServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let name_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .name_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::NameServer(name_servers)
    }
}

/// Domain Name System servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "domain-name-server")]
pub struct DomainNameServer {
    /// a list of DNS servers available to the client, in order of preference;
    #[argh(option)]
    domain_name_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for DomainNameServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let domain_name_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .domain_name_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::DomainNameServer(domain_name_servers)
    }
}

/// MIT-LCS UDP Log servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "log-server")]
pub struct LogServer {
    /// a list of MIT-LCS UDP Log servers available to the client, in order of preference.
    #[argh(option)]
    log_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for LogServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let log_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .log_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::LogServer(log_servers)
    }
}

/// RFC 865 Cookie servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "cookie-server")]
pub struct CookieServer {
    /// a list of RFC 865 Cookie servers available to the client, in order of preference.
    #[argh(option)]
    cookie_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for CookieServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let cookie_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .cookie_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::CookieServer(cookie_servers)
    }
}

/// RFC 1179 Line Printer servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "lpr-server")]
pub struct LprServer {
    /// a list of RFC 1179 Line Printer servers available to the client, in order of preference.
    #[argh(option)]
    lpr_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for LprServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let lpr_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .lpr_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::LprServer(lpr_servers)
    }
}

/// Imagen Impress servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "impress-server")]
pub struct ImpressServer {
    /// a list of Imagen Impress servers available to the client, in order of preference.
    #[argh(option)]
    impress_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for ImpressServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let impress_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .impress_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::ImpressServer(impress_servers)
    }
}

/// RFC 887 Resource Location servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "resource-location-server")]
pub struct ResourceLocationServer {
    /// a list of RFC 887 Resource Location servers available to the client, in order of preference.
    #[argh(option)]
    resource_location_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for ResourceLocationServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let resource_location_server: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .resource_location_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::ResourceLocationServer(resource_location_server)
    }
}

/// Name of the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "host-name")]
pub struct HostName {
    /// the name of client, which may or may not be qualified with the local domain name.
    #[argh(option)]
    name: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for HostName {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let name = self.name.unwrap_or("".to_string());
        fidl_fuchsia_net_dhcp::Option_::HostName(name)
    }
}

/// Size of the default boot image for the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "boot-file-size")]
pub struct BootFileSize {
    /// the size of the client's default boot image in 512-octet blocks.
    #[argh(option)]
    size: Option<u16>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for BootFileSize {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let size = self.size.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::BootFileSize(size)
    }
}

/// Path name of a core dump file.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "merit-dump-file")]
pub struct MeritDumpFile {
    /// the path name to the client's core dump in the event the client crashes.
    #[argh(option)]
    path: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for MeritDumpFile {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let path: String = self.path.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::MeritDumpFile(path)
    }
}

/// Domain name of the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "domain-name")]
pub struct DomainName {
    /// the client's domain name for use in resolving hostnames in the DNS.
    #[argh(option)]
    name: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for DomainName {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let name = self.name.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::DomainName(name)
    }
}

/// Address of the client's swap server.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "swap-server")]
pub struct SwapServer {
    /// the address of the client's swap server.
    #[argh(option)]
    address: Option<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for SwapServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let address = fidl_fuchsia_net::Ipv4Address {
            addr: self.address.unwrap_or(Ipv4Addr::new(0, 0, 0, 0)).octets(),
        };
        fidl_fuchsia_net_dhcp::Option_::SwapServer(address)
    }
}

/// Path name to a TFTP-retrievable file containing vendor-extension information.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "root-path")]
pub struct RootPath {
    /// the path name to a TFTP-retrievable file. This file contains data which can be interpreted
    /// as the BOOTP vendor-extension field. Unlike the BOOTP vendor-extension field, this file has
    /// an unconstrained length and any references to Tag 18 are ignored.
    #[argh(option)]
    path: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for RootPath {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let path = self.path.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::RootPath(path)
    }
}

/// Path name to a TFTP-retrievable file containing vendor-extension information.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "extensions-path")]
pub struct ExtensionsPath {
    /// the path name to a TFTP-retrievable file. This file contains data which can be interpreted
    /// as the BOOTP vendor-extension field. Unlike the BOOTP vendor-extension field, this file has
    /// an unconstrained length and any references to Tag 18 are ignored.
    #[argh(option)]
    path: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for ExtensionsPath {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let path = self.path.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::ExtensionsPath(path)
    }
}

/// Flag enabling/disabling IP layer packet forwarding.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "ip-forwarding")]
pub struct IpForwarding {
    /// a flag which will enabled IP layer packet forwarding when true.
    #[argh(switch)]
    enabled: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for IpForwarding {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::IpForwarding(self.enabled)
    }
}

/// Flag enabling/disabling forwarding of IP packets with non-local source routes.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "non-local-source-routing")]
pub struct NonLocalSourceRouting {
    /// a flag which will enable forwarding of IP packets with non-local source routes.
    #[argh(switch)]
    enabled: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NonLocalSourceRouting {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::NonLocalSourceRouting(self.enabled)
    }
}

/// Policy filters for non-local source routing.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "policy-filter")]
pub struct PolicyFilter {
    /// a list of IP Address and Subnet Mask pairs. If an incoming source-routed packet has a
    /// next-hop that does not match one of these pairs, then the packet will be dropped.
    #[argh(option)]
    addresses: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for PolicyFilter {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let addresses: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .addresses
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::PolicyFilter(addresses)
    }
}

/// Maximum sized datagram that the client should be able to reassemble.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "max-datagram-reassembly-size")]
pub struct MaxDatagramReassemblySize {
    /// the maximum sized datagram that the client should be able to reassemble, in octets. The
    /// minimum legal value is 576.
    #[argh(option)]
    size: Option<u16>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for MaxDatagramReassemblySize {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let size = self.size.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::MaxDatagramReassemblySize(size)
    }
}

/// Default time-to-live to use on outgoing IP datagrams.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "default-ip-ttl")]
pub struct DefaultIpTtl {
    /// the default time-to-live to use on outgoing IP datagrams. The value must be between 1 and
    /// 255.
    #[argh(option)]
    ttl: Option<u8>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for DefaultIpTtl {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let ttl = self.ttl.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::DefaultIpTtl(ttl)
    }
}

/// Timeout to use when aging Path MTU values.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "path-mtu-aging-timeout")]
pub struct PathMtuAgingTimeout {
    /// the timeout, in seconds, to be used when again Path MTU values by the mechanism in RFC 1191.
    #[argh(option)]
    timeout: Option<u32>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for PathMtuAgingTimeout {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let timeout = self.timeout.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::PathMtuAgingTimeout(timeout)
    }
}

/// Table of MTU sizes for Path MTU Discovery.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "path-mtu-plateau-table")]
pub struct PathMtuPlateauTable {
    /// A list of MTU sizes, ordered from smallest to largest. The smallest value cannot be smaller
    /// than 68.
    #[argh(option)]
    mtu_sizes: Vec<u16>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for PathMtuPlateauTable {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::PathMtuPlateauTable(self.mtu_sizes)
    }
}

/// MTU for the client's interface.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "interface-mtu")]
pub struct InterfaceMtu {
    /// the MTU for the client's interface. Minimum value of 68.
    #[argh(option)]
    mtu: Option<u16>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for InterfaceMtu {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let mtu = self.mtu.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::InterfaceMtu(mtu)
    }
}

/// Flag indicating if all subnets of the connected network have the same MTU.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "all-subnets-local")]
pub struct AllSubnetsLocal {
    /// a flag indicating if all subents of the IP network to which the client is connected have the
    /// same MTU.
    #[argh(switch)]
    local: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for AllSubnetsLocal {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::AllSubnetsLocal(self.local)
    }
}

/// Broadcast address of the client's subnet.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "broadcast-address")]
pub struct BroadcastAddress {
    /// the broadcast address of the client's subnet. Legal values are defined in RFC 1122.
    #[argh(option)]
    addr: Option<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for BroadcastAddress {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let addr = fidl_fuchsia_net::Ipv4Address {
            addr: self.addr.unwrap_or(Ipv4Addr::new(0, 0, 0, 0)).octets(),
        };
        fidl_fuchsia_net_dhcp::Option_::BroadcastAddress(addr)
    }
}

/// Flag indicating whether the client should perform subnet mask discovery via ICMP.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "perform-mask-discovery")]
pub struct PerformMaskDiscovery {
    /// a flag indicating whether the client should perform subnet mask discovery via ICMP.
    #[argh(switch)]
    do_discovery: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for PerformMaskDiscovery {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::PerformMaskDiscovery(self.do_discovery)
    }
}

/// Flag indicating whether the client should respond to subnet mask discovery requests via ICMP.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "mask-supplier")]
pub struct MaskSupplier {
    /// a flag indicating whether the client should respond to subnet mask discovery requests via
    /// ICMP.
    #[argh(switch)]
    supplier: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for MaskSupplier {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::MaskSupplier(self.supplier)
    }
}

/// Flag indicating whether the client should solicit routers using Router Discovery.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "perform-router-discovery")]
pub struct PerformRouterDiscovery {
    /// A flag indicating whether the client should solicit routers using Router Discovery as
    /// defined in RFC 1256.
    #[argh(switch)]
    do_discovery: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for PerformRouterDiscovery {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::PerformRouterDiscovery(self.do_discovery)
    }
}

/// Destination address for Router Solicitation requests.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "router-solicitation-address")]
pub struct RouterSolicitationAddress {
    /// the address to which the client should transmit Router Solicitation requests.
    #[argh(option)]
    addr: Option<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for RouterSolicitationAddress {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let addr = fidl_fuchsia_net::Ipv4Address {
            addr: self.addr.unwrap_or(Ipv4Addr::new(0, 0, 0, 0)).octets(),
        };
        fidl_fuchsia_net_dhcp::Option_::RouterSolicitationAddress(addr)
    }
}

/// Static Routes which the client should put in its routing cache.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "static-route")]
pub struct StaticRoute {
    /// a list of Destination address/Next-hop address pairs defining static routes for the client's
    /// routing table. The routes should be listed in descending order of priority. It is illegal
    /// to use 0.0.0.0 as the destination in a static route.
    #[argh(option)]
    routes: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for StaticRoute {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let routes: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .routes
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::StaticRoute(routes)
    }
}

/// Flag specifying whether the client should negotiate the use of trailers in ARP.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "trailer-encapsulation")]
pub struct TrailerEncapsulation {
    /// a flag specifying whether the client negotiate the use of trailers when using ARP, per RFC
    /// 893.
    #[argh(switch)]
    trailers: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for TrailerEncapsulation {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::TrailerEncapsulation(self.trailers)
    }
}

/// Timeout for ARP cache entries.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "arp-cache-timeout")]
pub struct ArpCacheTimeout {
    /// the timeout for ARP cache entries, in seconds.
    #[argh(option)]
    timeout: Option<u32>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for ArpCacheTimeout {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let timeout = self.timeout.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::ArpCacheTimeout(timeout)
    }
}

/// Flag specifying whether the client should use Ethernet v2 or IEEE 802.3 encapsulation.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "ethernet-encapsulation")]
pub struct EthernetEncapsulation {
    /// a flag specifying that the client should use Ethernet v2 encapsulation when false, and IEEE
    /// 802.3 encapsulation when true.
    #[argh(switch)]
    encapsulate: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for EthernetEncapsulation {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::EthernetEncapsulation(self.encapsulate)
    }
}

/// Default time-to-live for outgoing TCP segments.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "tcp-default-ttl")]
pub struct TcpDefaultTtl {
    /// the default time-to-live that the client should use for outgoing TCP segments. The minimum
    /// value is 1.
    #[argh(option)]
    ttl: Option<u8>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for TcpDefaultTtl {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let ttl = self.ttl.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::TcpDefaultTtl(ttl)
    }
}

/// Interval the client should wait before sending a TCP keepalive message.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "tcp-keepalive-interval")]
pub struct TcpKeepaliveInterval {
    /// the interval, in seconds, the client should wait before sending a TCP keepalive message. A
    /// value of 0 indicates that the client should not send keepalive messages unless specifically
    /// requested by an application.
    #[argh(option)]
    interval: Option<u32>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for TcpKeepaliveInterval {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let interval = self.interval.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::TcpKeepaliveInterval(interval)
    }
}

/// Flag specifying whether the client should send TCP keepalive messages with an octet of garbage.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "tcp-keepalive-garbage")]
pub struct TcpKeepaliveGarbage {
    /// a flag specifying whether the client should send TCP keepalive messages with an octet of
    /// garbage for compatibility with older implementations.
    #[argh(switch)]
    send_garbage: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for TcpKeepaliveGarbage {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::TcpKeepaliveGarbage(self.send_garbage)
    }
}

/// Name of the client's Network Information Service domain.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "network-information-service-domain")]
pub struct NetworkInformationServiceDomain {
    /// the name of the client's Network Information Service domain.
    #[argh(option)]
    domain_name: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetworkInformationServiceDomain {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let domain_name = self.domain_name.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::NetworkInformationServiceDomain(domain_name)
    }
}

/// Network Information Service servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "network-information-servers")]
pub struct NetworkInformationServers {
    /// a list of Network Information Service server addresses available to the client, listed in
    /// order of preference.
    #[argh(option)]
    servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetworkInformationServers {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::NetworkInformationServers(servers)
    }
}

/// Network Time Protocol servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "network-time-protocol-servers")]
pub struct NetworkTimeProtocolServers {
    /// a list of Network Time Protocol (NTP) server addresses available to the client, listed in
    /// order of preference.
    #[argh(option)]
    servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetworkTimeProtocolServers {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::NetworkTimeProtocolServers(servers)
    }
}

/// Option for exchanging vendor-specific information between the DHCP client and DHCP server.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "vendor-specific-information")]
pub struct VendorSpecificInformation {
    /// an opaque object of octets for exchanging vendor-specific information.
    #[argh(option)]
    data: Vec<u8>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for VendorSpecificInformation {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        fidl_fuchsia_net_dhcp::Option_::VendorSpecificInformation(self.data)
    }
}

/// NetBIOS name servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "net-bios-over-tcpip-name-server")]
pub struct NetBiosOverTcpipNameServer {
    /// a list of NetBIOS name server addresses available to the client, listed in order of
    /// preference.
    #[argh(option)]
    servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetBiosOverTcpipNameServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipNameServer(servers)
    }
}

/// NetBIOS datagram distribution servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "net-bios-over-tcpip-distribution-server")]
pub struct NetBiosOverTcpipDatagramDistributionServer {
    /// a list of NetBIOS datagram distribution servers available to the client, listed in order of
    /// preference.
    #[argh(option)]
    servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetBiosOverTcpipDatagramDistributionServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipDatagramDistributionServer(servers)
    }
}

/// The NetBIOS node type which should be used by the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "net-bios-over-tcpip-node-type")]
pub struct NetBiosOverTcpipNodeType {
    /// the NetBIOS node type which should be used by the client.
    #[argh(subcommand)]
    node_type: Option<NodeType>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetBiosOverTcpipNodeType {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let node_type = self.node_type.unwrap_or(NodeType::BNode(BNode {}));
        let fidl_node_type = match node_type {
            NodeType::BNode(_) => fidl_fuchsia_net_dhcp::NodeTypes::BNode,
            NodeType::HNode(_) => fidl_fuchsia_net_dhcp::NodeTypes::HNode,
            NodeType::MNode(_) => fidl_fuchsia_net_dhcp::NodeTypes::MNode,
            NodeType::PNode(_) => fidl_fuchsia_net_dhcp::NodeTypes::PNode,
        };
        fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipNodeType(fidl_node_type)
    }
}

/// NetBIOS scope parameter for the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "net-bios-over-tcpip-scope")]
pub struct NetBiosOverTcpipScope {
    /// the NetBIOS over TCP/IP scope parameter, as defined in RFC 1001, for the client.
    #[argh(option)]
    scope: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetBiosOverTcpipScope {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let scope = self.scope.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipScope(scope)
    }
}

/// X Window System Font servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "x-window-system-font-server")]
pub struct XWindowSystemFontServer {
    /// a list of X Window System Font server addresses available to the client, listed in order of
    /// preference.
    #[argh(option)]
    servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for XWindowSystemFontServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::XWindowSystemFontServer(servers)
    }
}

/// X window System Display Manager systems available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "x-window-system-display-manager")]
pub struct XWindowSystemDisplayManager {
    /// a list of X Window System Display Manager system addresses available to the client, listed
    /// in order of preference.
    #[argh(option)]
    display_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for XWindowSystemDisplayManager {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let display_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .display_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::XWindowSystemDisplayManager(display_servers)
    }
}

/// Network Information System+ domain name.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "network-information-service-plus-domain")]
pub struct NetworkInformationServicePlusDomain {
    /// the name of the client's Network Information System+ domain.
    #[argh(option)]
    domain_name: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetworkInformationServicePlusDomain {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let domain_name = self.domain_name.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::NetworkInformationServicePlusDomain(domain_name)
    }
}

/// Network Information System+ servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "network-information-service-plus-servers")]
pub struct NetworkInformationServicePlusServers {
    /// a list of Network Information System+ server addresses available to the client, listed in
    /// order of preference.
    #[argh(option)]
    servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NetworkInformationServicePlusServers {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::NetworkInformationServicePlusServers(servers)
    }
}

/// Mobile IP home agents available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "mobile-ip-home-agent")]
pub struct MobileIpHomeAgent {
    /// a list of mobile IP home agent addresses available to the client, listed in order of
    /// preference.
    #[argh(option)]
    home_agents: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for MobileIpHomeAgent {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let home_agents: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .home_agents
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::MobileIpHomeAgent(home_agents)
    }
}

/// SMTP servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "smtp-server")]
pub struct SmtpServer {
    /// a list of Simple Mail Transport Protocol (SMTP) server address available to the client,
    /// listed in order of preference.
    #[argh(option)]
    smtp_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for SmtpServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let smtp_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .smtp_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::SmtpServer(smtp_servers)
    }
}

/// POP3 servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "pop3-server")]
pub struct Pop3Server {
    /// a list of Post Office Protocol (POP3) server addresses available to the client, listed in
    /// order of preference.
    #[argh(option)]
    pop3_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for Pop3Server {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let pop3_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .pop3_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::Pop3Server(pop3_servers)
    }
}

/// NNTP servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "nntp-server")]
pub struct NntpServer {
    /// a list Network News Transport Protocol (NNTP) server addresses available to the client,
    /// listed in order of preference.
    #[argh(option)]
    nntp_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for NntpServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let nntp_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .nntp_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::NntpServer(nntp_servers)
    }
}

/// Default WWW servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "default-www-server")]
pub struct DefaultWwwServer {
    /// a list of default World Wide Web (WWW) server addresses available to the client, listed in
    /// order of preference.
    #[argh(option)]
    www_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for DefaultWwwServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let www_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .www_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::DefaultWwwServer(www_servers)
    }
}

/// Default Finger servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "default-finger-server")]
pub struct DefaultFingerServer {
    /// a list of default Finger server addresses available to the client, listed in order of
    /// preference.
    #[argh(option)]
    finger_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for DefaultFingerServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let finger_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .finger_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::DefaultFingerServer(finger_servers)
    }
}

/// Default IRC servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "default-irc-server")]
pub struct DefaultIrcServer {
    /// a list of Internet Relay Chat server addresses available to the client, listed in order of
    /// preference.
    #[argh(option)]
    irc_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for DefaultIrcServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let irc_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .irc_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::DefaultIrcServer(irc_servers)
    }
}

/// StreetTalk servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "street-talk-server")]
pub struct StreettalkServer {
    /// a list of StreetTalk server addresses available to the client, listed in order of
    /// preference.
    #[argh(option)]
    streettalk_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for StreettalkServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let streettalk_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .streettalk_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::StreettalkServer(streettalk_servers)
    }
}

/// StreetTalk Directory Assistance servers available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "stda-server")]
pub struct StreettalkDirectoryAssistanceServer {
    /// a list of StreetTalk Directory Assistance server addresses available to the client, listed
    /// in order of preference.
    #[argh(option)]
    stda_servers: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for StreettalkDirectoryAssistanceServer {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let stda_servers: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .stda_servers
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Option_::StreettalkDirectoryAssistanceServer(stda_servers)
    }
}

/// TFTP server available to the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "tftp-server-name")]
pub struct TftpServerName {
    /// the TFTP server name available to the client. This option should be used when the `sname`
    /// field has been overloaded to carry options.
    #[argh(option)]
    name: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for TftpServerName {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let name = self.name.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::TftpServerName(name)
    }
}

/// Bootfile name for the client.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "bootfile-name")]
pub struct BootfileName {
    /// the bootfile name for the client. This option should be used when the `file` field has been
    /// overloaded to carry options.
    #[argh(option)]
    name: Option<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for BootfileName {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let name = self.name.unwrap_or(String::new());
        fidl_fuchsia_net_dhcp::Option_::BootfileName(name)
    }
}

/// Maximum length of a DHCP message that the participant is willing to accept.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "renewal-time-value")]
pub struct MaxDhcpMessageSize {
    /// the maximum length in octets of a DHCP message that the participant is willing to accept.
    /// The minimum value is 576.
    #[argh(option)]
    length: Option<u16>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for MaxDhcpMessageSize {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let length = self.length.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::MaxDhcpMessageSize(length)
    }
}

/// Time interval from address assignment at which the client transitions to a Renewing state.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "renewal-time-value")]
pub struct RenewalTimeValue {
    /// the time interval, in seconds, after address assignment at which the client will transition
    /// to the Renewing state.
    #[argh(option)]
    interval: Option<u32>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for RenewalTimeValue {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let interval = self.interval.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::RenewalTimeValue(interval)
    }
}

/// Time interval from address assignment at which the client transitions to a Rebinding state.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "rebinding-time-value")]
pub struct RebindingTimeValue {
    /// the time interval, in seconds, after address assignment at which the client will transition
    /// to the Rebinding state.
    #[argh(option)]
    interval: Option<u32>,
}

impl Into<fidl_fuchsia_net_dhcp::Option_> for RebindingTimeValue {
    fn into(self) -> fidl_fuchsia_net_dhcp::Option_ {
        let interval = self.interval.unwrap_or(0);
        fidl_fuchsia_net_dhcp::Option_::RebindingTimeValue(interval)
    }
}

/// The name of the server parameter to operate on.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum Parameter {
    IpAddrs(IpAddrs),
    AddressPool(AddressPool),
    LeaseLength(LeaseLength),
    PermittedMacs(PermittedMacs),
    StaticallyAssignedAddrs(StaticallyAssignedAddrs),
    ArpProbe(ArpProbe),
    BoundDevices(BoundDevices),
}

impl Into<fidl_fuchsia_net_dhcp::ParameterName> for Parameter {
    fn into(self) -> fidl_fuchsia_net_dhcp::ParameterName {
        match self {
            Parameter::IpAddrs(_) => fidl_fuchsia_net_dhcp::ParameterName::IpAddrs,
            Parameter::AddressPool(_) => fidl_fuchsia_net_dhcp::ParameterName::AddressPool,
            Parameter::LeaseLength(_) => fidl_fuchsia_net_dhcp::ParameterName::LeaseLength,
            Parameter::PermittedMacs(_) => fidl_fuchsia_net_dhcp::ParameterName::PermittedMacs,
            Parameter::StaticallyAssignedAddrs(_) => {
                fidl_fuchsia_net_dhcp::ParameterName::StaticallyAssignedAddrs
            }
            Parameter::ArpProbe(_) => fidl_fuchsia_net_dhcp::ParameterName::ArpProbe,
            Parameter::BoundDevices(_) => fidl_fuchsia_net_dhcp::ParameterName::BoundDeviceNames,
        }
    }
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for Parameter {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        match self {
            Parameter::IpAddrs(v) => v.into(),
            Parameter::AddressPool(v) => v.into(),
            Parameter::LeaseLength(v) => v.into(),
            Parameter::PermittedMacs(v) => v.into(),
            Parameter::StaticallyAssignedAddrs(v) => v.into(),
            Parameter::ArpProbe(v) => v.into(),
            Parameter::BoundDevices(v) => v.into(),
        }
    }
}

/// The IPv4 addresses to which the server is bound.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "ip-addrs")]
pub struct IpAddrs {
    /// A list of IPv4 Addresses to which the server is bound.
    #[argh(option)]
    addrs: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for IpAddrs {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        let addrs: Vec<fidl_fuchsia_net::Ipv4Address> = self
            .addrs
            .iter()
            .map(|addr| fidl_fuchsia_net::Ipv4Address { addr: addr.octets() })
            .collect();
        fidl_fuchsia_net_dhcp::Parameter::IpAddrs(addrs)
    }
}

/// The pool of addresses which the DHCP server manages.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "address-pool")]
pub struct AddressPool {
    /// the network ID of the address pool's subnet.
    #[argh(option)]
    network_id: Option<Ipv4Addr>,
    /// the broadcast address of the address pool's subnet.
    /// By default, this address will be the equivalent of setting
    /// the host part of the network ID to all ones.
    #[argh(option)]
    broadcast: Option<Ipv4Addr>,
    /// the subnet mask of the address pool's network.
    #[argh(option)]
    mask: Option<Ipv4Addr>,
    /// the starting address, inclusive, of the range of addresses which the DHCP server
    /// will lease to clients. This address must be in the subnet defined by the network_id
    /// and mask members of the AddressPool.
    #[argh(option)]
    pool_range_start: Option<Ipv4Addr>,
    /// the ending address, inclusive, of the range of addresses which the server will
    /// to clients. This address must be in the subnet defined by the network_id and mask
    /// members of the AddressPool.
    #[argh(option)]
    pool_range_stop: Option<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for AddressPool {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        let pool = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: self
                .network_id
                .map_or(None, |v| Some(fidl_fuchsia_net::Ipv4Address { addr: v.octets() })),
            broadcast: self
                .broadcast
                .map_or(None, |v| Some(fidl_fuchsia_net::Ipv4Address { addr: v.octets() })),
            mask: self
                .mask
                .map_or(None, |v| Some(fidl_fuchsia_net::Ipv4Address { addr: v.octets() })),
            pool_range_start: self
                .pool_range_start
                .map_or(None, |v| Some(fidl_fuchsia_net::Ipv4Address { addr: v.octets() })),
            pool_range_stop: self
                .pool_range_stop
                .map_or(None, |v| Some(fidl_fuchsia_net::Ipv4Address { addr: v.octets() })),
        };
        fidl_fuchsia_net_dhcp::Parameter::AddressPool(pool)
    }
}

/// The client MAC addresses which the server will issue leases to.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "permitted-macs")]
pub struct PermittedMacs {
    /// the client MAC addresses which the server will issue leases to. By default,
    /// the server will not have a permitted MAC list, in which case it will attempt to
    /// issue a lease to every client which requests one. If permitted_macs has a non-zero length
    /// then the server will only respond to lease requests from clients with a MAC in the list.
    #[argh(option)]
    macs: Vec<MacAddr>,
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for PermittedMacs {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        let addrs: Vec<fidl_fuchsia_net::MacAddress> = self
            .macs
            .iter()
            .map(|addr| fidl_fuchsia_net::MacAddress { octets: addr.octets })
            .collect();
        fidl_fuchsia_net_dhcp::Parameter::PermittedMacs(addrs)
    }
}

/// Addresses in the AddressPool which will only be leased to specified clients.
/// Assigned addresses will be paired with hosts in order, e.g. hosts (A, B, C) and addresses (1, 2, 3)
/// pair as ((A, 1), (B, 2), (C, 3)).
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "statically-assigned-addrs")]
pub struct StaticallyAssignedAddrs {
    /// hosts which will be leased the addresses reserved by `assigned_addrs`.
    #[argh(option)]
    hosts: Vec<MacAddr>,
    /// addresses in the AddressPool which will not be leased to clients. Typically, a network
    /// administrator will statically assign these addresses to always-on network
    /// devices which should always have the same IP address, such as network printers.
    #[argh(option)]
    assigned_addrs: Vec<Ipv4Addr>,
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for StaticallyAssignedAddrs {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        let assignments: Vec<fidl_fuchsia_net_dhcp::StaticAssignment> = self
            .hosts
            .iter()
            .zip(self.assigned_addrs)
            .map(|(host, addr)| {
                (
                    fidl_fuchsia_net::MacAddress { octets: host.octets },
                    fidl_fuchsia_net::Ipv4Address { addr: addr.octets() },
                )
            })
            .map(|(host, assigned_addr)| fidl_fuchsia_net_dhcp::StaticAssignment {
                host: Some(host),
                assigned_addr: Some(assigned_addr),
            })
            .collect();
        fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(assignments)
    }
}

/// Enables server behavior where the server ARPs an IP address prior to issuing
/// it in a lease.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "arp-probe")]
pub struct ArpProbe {
    /// enables server behavior where the server ARPs an IP address prior to issuing
    /// it in a lease. If the server receives a response, the server will mark the
    /// address as in-use and try again with a different address. By default, this
    /// behavior is disabled.
    #[argh(switch)]
    enabled: bool,
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for ArpProbe {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        fidl_fuchsia_net_dhcp::Parameter::ArpProbe(self.enabled)
    }
}

/// The duration of leases offered by the server.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "lease-length")]
pub struct LeaseLength {
    /// the default lease length, in seconds, to be issued to clients.
    #[argh(option)]
    default: Option<u32>,
    /// the maximum lease length value, in seconds, which the server will issue to clients who
    /// have requested a specific lease length. With the default value of 0, the max lease length is
    /// equivalent to the default lease length.
    #[argh(option)]
    max: Option<u32>,
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for LeaseLength {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
            default: self.default,
            max: self.max,
        })
    }
}

/// A NetBIOS over TCP/IP node type as defined in RFC 1001/1002.
#[derive(Clone, Copy, Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum NodeType {
    BNode(BNode),
    PNode(PNode),
    MNode(MNode),
    HNode(HNode),
}

/// A B node type.
#[derive(Clone, Copy, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "b-node")]
pub struct BNode {}

/// A P node type.
#[derive(Clone, Copy, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "p-node")]
pub struct PNode {}

/// A M node type.
#[derive(Clone, Copy, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "m-node")]
pub struct MNode {}

/// A H node type.
#[derive(Clone, Copy, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "h-node")]
pub struct HNode {}

/// The names of the network devices on which the server will listen.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "bound-device-names")]
pub struct BoundDevices {
    /// the names of the network devices on which the server will listen. If
    /// this vector is empty, the server will listen on all devices and will
    /// process incoming DHCP messages regardless of the device on which they
    /// arrive. If this vector is not empty, then the server will only listen
    /// for incoming DHCP messages on the named network devices contained by
    /// this vector.
    #[argh(positional)]
    pub names: Vec<String>,
}

impl Into<fidl_fuchsia_net_dhcp::Parameter> for BoundDevices {
    fn into(self) -> fidl_fuchsia_net_dhcp::Parameter {
        fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(self.names)
    }
}

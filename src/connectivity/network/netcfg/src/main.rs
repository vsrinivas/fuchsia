// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used because we use `futures::select!`.
//
// From https://docs.rs/futures/0.3.1/futures/macro.select.html:
//   Note that select! relies on proc-macro-hack, and may require to set the compiler's
//   recursion limit very high, e.g. #![recursion_limit="1024"].
#![recursion_limit = "512"]

mod devices;
mod dhcpv4;
mod dhcpv6;
mod dns;
mod errors;
mod interface;
mod matchers;

use dhcp::protocol::FromFidlExt as _;
use std::collections::{hash_map::Entry, HashMap, HashSet};
use std::convert::TryFrom as _;
use std::convert::TryInto as _;
use std::fs;
use std::io;
use std::path;
use std::pin::Pin;
use std::str::FromStr;

use fidl_fuchsia_hardware_ethernet as feth;
use fidl_fuchsia_hardware_ethernet_ext as feth_ext;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcp as fnet_dhcp;
use fidl_fuchsia_net_dhcpv6 as fnet_dhcpv6;
use fidl_fuchsia_net_ext::{self as fnet_ext, DisplayExt as _, IntoExt as _, IpExt as _};
use fidl_fuchsia_net_filter as fnet_filter;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_ext::{self as fnet_interfaces_ext, Update as _};
use fidl_fuchsia_net_name as fnet_name;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async::DurationExt as _;
use fuchsia_component::client::{clone_namespace_svc, new_protocol_connector_in_dir};
use fuchsia_syslog as fsyslog;
use fuchsia_vfs_watcher as fvfs_watcher;
use fuchsia_zircon::{self as zx, DurationNum as _};

use anyhow::Context as _;
use argh::FromArgs;
use dns_server_watcher::{DnsServers, DnsServersUpdateSource, DEFAULT_DNS_PORT};
use futures::stream::{self, StreamExt as _, TryStreamExt as _};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use log::{debug, error, info, trace, warn};
use net_declare::fidl_ip_v4;
use serde::Deserialize;

use self::devices::{Device as _, DeviceInfo as _};
use self::errors::ContextExt as _;

/// Interface metrics.
///
/// Interface metrics are used to sort the route table. An interface with a
/// lower metric is favored over one with a higher metric.
/// For now favor WLAN over Ethernet.
const INTF_METRIC_WLAN: u32 = 90;
const INTF_METRIC_ETH: u32 = 100;

/// Path to devfs.
const DEV_PATH: &str = "/dev";

/// File that stores persistent interface configurations.
const PERSISTED_INTERFACE_CONFIG_FILEPATH: &str = "/data/net_interfaces.cfg.json";

/// A node that represents the directory it is in.
///
/// `/dir` and `/dir/.` point to the same directory.
const THIS_DIRECTORY: &str = ".";

/// The string present in the topological path of a WLAN AP interface.
const WLAN_AP_TOPO_PATH_CONTAINS: &str = "wlanif-ap";

/// The prefix length for the address assigned to a WLAN AP interface.
const WLAN_AP_PREFIX_LEN: u8 = 29;

/// The address for the network the WLAN AP interface is a part of.
const WLAN_AP_NETWORK_ADDR: fnet::Ipv4Address = fidl_ip_v4!("192.168.255.248");

/// The lease time for a DHCP lease.
///
/// 1 day in seconds.
const WLAN_AP_DHCP_LEASE_TIME_SECONDS: u32 = 24 * 60 * 60;

/// The maximum number of times to attempt to add a device.
const MAX_ADD_DEVICE_ATTEMPTS: u8 = 3;

/// A map of DNS server watcher streams that yields `DnsServerWatcherEvent` as DNS
/// server updates become available.
///
/// DNS server watcher streams may be added or removed at runtime as the watchers
/// are started or stopped.
type DnsServerWatchers<'a> = async_utils::stream::StreamMap<
    DnsServersUpdateSource,
    stream::BoxStream<
        'a,
        (DnsServersUpdateSource, Result<Vec<fnet_name::DnsServer_>, anyhow::Error>),
    >,
>;

/// Defines log levels.
#[derive(Debug, Copy, Clone)]
pub enum LogLevel {
    /// ALL log level.
    All,

    /// TRACE log level.
    Trace,

    /// DEBUG log level.
    Debug,

    /// INFO log level.
    Info,

    /// WARN log level.
    Warn,

    /// ERROR log level.
    Error,

    /// FATAL log level.
    Fatal,
}

impl Into<fsyslog::levels::LogLevel> for LogLevel {
    fn into(self) -> fsyslog::levels::LogLevel {
        match self {
            LogLevel::All => fsyslog::levels::ALL,
            LogLevel::Trace => fsyslog::levels::TRACE,
            LogLevel::Debug => fsyslog::levels::DEBUG,
            LogLevel::Info => fsyslog::levels::INFO,
            LogLevel::Warn => fsyslog::levels::WARN,
            LogLevel::Error => fsyslog::levels::ERROR,
            LogLevel::Fatal => fsyslog::levels::FATAL,
        }
    }
}

impl FromStr for LogLevel {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, anyhow::Error> {
        match s.to_uppercase().as_str() {
            "ALL" => Ok(LogLevel::All),
            "TRACE" => Ok(LogLevel::Trace),
            "DEBUG" => Ok(LogLevel::Debug),
            "INFO" => Ok(LogLevel::Info),
            "WARN" => Ok(LogLevel::Warn),
            "ERROR" => Ok(LogLevel::Error),
            "FATAL" => Ok(LogLevel::Fatal),
            _ => Err(anyhow::anyhow!("unrecognized log level = {}", s)),
        }
    }
}

/// Network Configuration tool.
///
/// Configures network components in response to events.
#[derive(FromArgs, Debug)]
struct Opt {
    /// should netemul specific configurations be used?
    #[argh(switch)]
    allow_virtual_devices: bool,

    /// minimum severity for logs
    #[argh(option, default = "LogLevel::Info")]
    min_severity: LogLevel,

    /// config file to use
    #[argh(option, default = "\"/config/data/default.json\".to_string()")]
    config_data: String,
}

#[derive(Debug, Deserialize)]
pub struct DnsConfig {
    pub servers: Vec<std::net::IpAddr>,
}

#[derive(Debug, Deserialize)]
pub struct FilterConfig {
    pub rules: Vec<String>,
    pub nat_rules: Vec<String>,
    pub rdr_rules: Vec<String>,
}

#[derive(Debug, PartialEq, Eq, Hash, Clone)]
enum InterfaceType {
    UNKNOWN(String),
    ETHERNET,
    WLAN,
}

#[derive(Debug, Deserialize)]
struct Config {
    pub dns_config: DnsConfig,
    #[serde(deserialize_with = "matchers::InterfaceSpec::parse_as_tuples")]
    pub rules: Vec<matchers::InterfaceSpec>,
    pub filter_config: FilterConfig,
    pub filter_enabled_interface_types: Vec<String>,
}

impl Config {
    pub fn load<P: AsRef<path::Path>>(path: P) -> Result<Self, anyhow::Error> {
        let path = path.as_ref();
        let file = fs::File::open(path)
            .with_context(|| format!("could not open the config file {}", path.display()))?;
        let config = serde_json::from_reader(io::BufReader::new(file))
            .with_context(|| format!("could not deserialize the config file {}", path.display()))?;
        Ok(config)
    }
}

// We use Compare-And-Swap (CAS) protocol to update filter rules. $get_rules returns the current
// generation number. $update_rules will send it with new rules to make sure we are updating the
// intended generation. If the generation number doesn't match, $update_rules will return a
// GenerationMismatch error, then we have to restart from $get_rules.

const FILTER_CAS_RETRY_MAX: i32 = 3;
const FILTER_CAS_RETRY_INTERVAL_MILLIS: i64 = 500;

macro_rules! cas_filter_rules {
    ($filter:expr, $get_rules:ident, $update_rules:ident, $rules:expr) => {
        for retry in 0..FILTER_CAS_RETRY_MAX {
            let (_, generation, status) = $filter.$get_rules().await?;
            if status != fnet_filter::Status::Ok {
                let () = Err(anyhow::anyhow!("{} failed: {:?}", stringify!($get_rules), status))?;
            }
            let status =
                $filter.$update_rules(&mut $rules.iter_mut(), generation).await.with_context(
                    || format!("error getting response from {}", stringify!($update_rules)),
                )?;
            match status {
                fnet_filter::Status::Ok => {
                    break;
                }
                fnet_filter::Status::ErrGenerationMismatch if retry < FILTER_CAS_RETRY_MAX - 1 => {
                    fuchsia_async::Timer::new(
                        FILTER_CAS_RETRY_INTERVAL_MILLIS.millis().after_now(),
                    )
                    .await;
                }
                _ => {
                    let () =
                        Err(anyhow::anyhow!("{} failed: {:?}", stringify!($update_rules), status))?;
                }
            }
        }
    };
}

impl From<String> for InterfaceType {
    fn from(s: String) -> InterfaceType {
        match s.as_ref() {
            "ethernet" => InterfaceType::ETHERNET,
            "wlan" => InterfaceType::WLAN,
            _ => InterfaceType::UNKNOWN(s),
        }
    }
}

fn should_enable_filter(
    filter_enabled_interface_types: &HashSet<InterfaceType>,
    features: &feth::Features,
) -> bool {
    if features.contains(feth::Features::Loopback) {
        false
    } else if features.contains(feth::Features::Wlan) {
        filter_enabled_interface_types.contains(&InterfaceType::WLAN)
    } else {
        filter_enabled_interface_types.contains(&InterfaceType::ETHERNET)
    }
}

/// State for an interface.
#[derive(Debug)]
enum InterfaceState {
    Host(HostInterfaceState),
    WlanAp(WlanApInterfaceState),
}

#[derive(Debug)]
struct HostInterfaceState {
    dhcpv6_client_addr: Option<fnet::Ipv6SocketAddress>,
}

#[derive(Debug)]
struct WlanApInterfaceState {}

impl InterfaceState {
    fn new_host() -> Self {
        Self::Host(HostInterfaceState { dhcpv6_client_addr: None })
    }

    fn new_wlan_ap() -> Self {
        Self::WlanAp(WlanApInterfaceState {})
    }

    fn is_wlan_ap(&self) -> bool {
        match self {
            InterfaceState::Host(_) => false,
            InterfaceState::WlanAp(_) => true,
        }
    }

    /// Handles the interface being discovered.
    async fn on_discovery(
        &mut self,
        properties: &fnet_interfaces_ext::Properties,
        dhcpv6_client_provider: Option<&fnet_dhcpv6::ClientProviderProxy>,
        watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<(), errors::Error> {
        let fnet_interfaces_ext::Properties { online, .. } = properties;
        match self {
            InterfaceState::Host(HostInterfaceState { dhcpv6_client_addr }) => {
                if !online {
                    return Ok(());
                }
                let dhcpv6_client_provider = match dhcpv6_client_provider {
                    Some(p) => p,
                    None => return Ok(()),
                };
                *dhcpv6_client_addr =
                    start_dhcpv6_client(properties, dhcpv6_client_provider, watchers)?;
            }
            InterfaceState::WlanAp(WlanApInterfaceState {}) => {}
        }
        Ok(())
    }
}

/// Network Configuration state.
struct NetCfg<'a> {
    stack: fnet_stack::StackProxy,
    netstack: fnetstack::NetstackProxy,
    lookup_admin: fnet_name::LookupAdminProxy,
    filter: fnet_filter::FilterProxy,
    interface_state: fnet_interfaces::StateProxy,
    dhcp_server: Option<fnet_dhcp::Server_Proxy>,
    dhcpv6_client_provider: Option<fnet_dhcpv6::ClientProviderProxy>,

    allow_virtual_devices: bool,

    persisted_interface_config: interface::FileBackedConfig<'a>,

    filter_enabled_interface_types: HashSet<InterfaceType>,
    default_config_rules: Vec<matchers::InterfaceSpec>,

    // TODO(fxbug.dev/67407) These two hashmaps are both indexed by interface ID and store
    // per-interface state, and should be merged.
    interface_states: HashMap<u64, InterfaceState>,
    interface_properties: HashMap<u64, fnet_interfaces_ext::Properties>,

    dns_servers: DnsServers,
}

/// Returns a [`fnet_name::DnsServer_`] with a static source from a [`std::net::IpAddr`].
fn static_source_from_ip(f: std::net::IpAddr) -> fnet_name::DnsServer_ {
    let socket_addr = match fnet_ext::IpAddress(f).into() {
        fnet::IpAddress::Ipv4(addr) => fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: addr,
            port: DEFAULT_DNS_PORT,
        }),
        fnet::IpAddress::Ipv6(addr) => fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: addr,
            port: DEFAULT_DNS_PORT,
            zone_index: 0,
        }),
    };

    fnet_name::DnsServer_ {
        address: Some(socket_addr),
        source: Some(fnet_name::DnsServerSource::StaticSource(
            fnet_name::StaticDnsServerSource::EMPTY,
        )),
        ..fnet_name::DnsServer_::EMPTY
    }
}

/// Connect to a service, returning an error if the service does not exist in
/// the service directory.
async fn svc_connect<S: fidl::endpoints::DiscoverableProtocolMarker>(
    svc_dir: &fio::DirectoryProxy,
) -> Result<S::Proxy, anyhow::Error> {
    optional_svc_connect::<S>(svc_dir).await?.ok_or(anyhow::anyhow!("service does not exist"))
}

/// Attempt to connect to a service, returning `None` if the service does not
/// exist in the service directory.
async fn optional_svc_connect<S: fidl::endpoints::DiscoverableProtocolMarker>(
    svc_dir: &fio::DirectoryProxy,
) -> Result<Option<S::Proxy>, anyhow::Error> {
    let req = new_protocol_connector_in_dir::<S>(&svc_dir);
    if !req.exists().await.context("error checking for service existence")? {
        Ok(None)
    } else {
        req.connect().context("error connecting to service").map(Some)
    }
}

/// Start a DHCPv6 client if there is a unicast link-local IPv6 address in `addresses` to use as
/// the address.
fn start_dhcpv6_client(
    fnet_interfaces_ext::Properties { id, name, addresses, .. }: &fnet_interfaces_ext::Properties,
    dhcpv6_client_provider: &fnet_dhcpv6::ClientProviderProxy,
    watchers: &mut DnsServerWatchers<'_>,
) -> Result<Option<fnet::Ipv6SocketAddress>, errors::Error> {
    let sockaddr = addresses.iter().find_map(
        |&fnet_interfaces_ext::Address {
             addr: fnet::Subnet { addr, prefix_len: _ },
             valid_until: _,
         }| match addr {
            fnet::IpAddress::Ipv6(address) => {
                if address.is_unicast_linklocal() {
                    Some(fnet::Ipv6SocketAddress {
                        address,
                        port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
                        zone_index: *id,
                    })
                } else {
                    None
                }
            }
            fnet::IpAddress::Ipv4(_) => None,
        },
    );

    if let Some(sockaddr) = sockaddr {
        let () = dhcpv6::start_client(dhcpv6_client_provider, *id, sockaddr, watchers)
            .with_context(|| {
                format!(
                    "failed to start DHCPv6 client on interface {} (id={}) w/ sockaddr {}",
                    name,
                    id,
                    sockaddr.display_ext()
                )
            })?;
        info!(
            "started DHCPv6 client on host interface {} (id={}) w/ sockaddr {}",
            name,
            id,
            sockaddr.display_ext(),
        );
    }
    Ok(sockaddr)
}

impl<'a> NetCfg<'a> {
    /// Returns a new `NetCfg`.
    async fn new(
        allow_virtual_devices: bool,
        default_config_rules: Vec<matchers::InterfaceSpec>,
        filter_enabled_interface_types: HashSet<InterfaceType>,
    ) -> Result<NetCfg<'a>, anyhow::Error> {
        let svc_dir = clone_namespace_svc().context("error cloning svc directory handle")?;
        let stack = svc_connect::<fnet_stack::StackMarker>(&svc_dir)
            .await
            .context("could not connect to stack")?;
        let netstack = svc_connect::<fnetstack::NetstackMarker>(&svc_dir)
            .await
            .context("could not connect to netstack")?;
        let lookup_admin = svc_connect::<fnet_name::LookupAdminMarker>(&svc_dir)
            .await
            .context("could not connect to lookup admin")?;
        let filter = svc_connect::<fnet_filter::FilterMarker>(&svc_dir)
            .await
            .context("could not connect to filter")?;
        let interface_state = svc_connect::<fnet_interfaces::StateMarker>(&svc_dir)
            .await
            .context("could not connect to interfaces state")?;
        let dhcp_server = optional_svc_connect::<fnet_dhcp::Server_Marker>(&svc_dir)
            .await
            .context("could not connect to DHCP Server")?;
        let dhcpv6_client_provider =
            optional_svc_connect::<fnet_dhcpv6::ClientProviderMarker>(&svc_dir)
                .await
                .context("could not connect to DHCPv6 client provider")?;
        let persisted_interface_config =
            interface::FileBackedConfig::load(&PERSISTED_INTERFACE_CONFIG_FILEPATH)
                .context("error loading persistent interface configurations")?;

        Ok(NetCfg {
            stack,
            netstack,
            lookup_admin,
            filter,
            interface_state,
            dhcp_server,
            dhcpv6_client_provider,
            allow_virtual_devices,
            persisted_interface_config,
            filter_enabled_interface_types,
            default_config_rules,
            interface_properties: HashMap::new(),
            interface_states: HashMap::new(),
            dns_servers: Default::default(),
        })
    }

    /// Updates the network filter configurations.
    async fn update_filters(&mut self, config: FilterConfig) -> Result<(), anyhow::Error> {
        let FilterConfig { rules, nat_rules, rdr_rules } = config;

        if !rules.is_empty() {
            let mut rules = netfilter::parser::parse_str_to_rules(&rules.join(""))
                .context("error parsing filter rules")?;
            cas_filter_rules!(self.filter, get_rules, update_rules, rules);
        }

        if !nat_rules.is_empty() {
            let mut nat_rules = netfilter::parser::parse_str_to_nat_rules(&nat_rules.join(""))
                .context("error parsing NAT rules")?;
            cas_filter_rules!(self.filter, get_nat_rules, update_nat_rules, nat_rules);
        }

        if !rdr_rules.is_empty() {
            let mut rdr_rules = netfilter::parser::parse_str_to_rdr_rules(&rdr_rules.join(""))
                .context("error parsing RDR rules")?;
            cas_filter_rules!(self.filter, get_rdr_rules, update_rdr_rules, rdr_rules);
        }

        Ok(())
    }

    /// Updates the DNS servers used by the DNS resolver.
    async fn update_dns_servers(
        &mut self,
        source: DnsServersUpdateSource,
        servers: Vec<fnet_name::DnsServer_>,
    ) -> Result<(), errors::Error> {
        dns::update_servers(&self.lookup_admin, &mut self.dns_servers, source, servers).await
    }

    /// Handles the completion of the DNS server watcher associated with `source`.
    ///
    /// Clears the servers for `source` and removes the watcher from `dns_watchers`.
    async fn handle_dns_server_watcher_done(
        &mut self,
        source: DnsServersUpdateSource,
        dns_watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<(), anyhow::Error> {
        match source {
            DnsServersUpdateSource::Default => {
                return Err(anyhow::anyhow!(
                    "should not have a DNS server watcher for the default source"
                ));
            }
            DnsServersUpdateSource::Netstack => {}
            DnsServersUpdateSource::Dhcpv6 { interface_id } => {
                let state = self
                    .interface_states
                    .get_mut(&interface_id)
                    .ok_or(anyhow::anyhow!("no interface state found for id={}", interface_id))?;

                match state {
                    InterfaceState::Host(HostInterfaceState { dhcpv6_client_addr }) => {
                        let _: fnet::Ipv6SocketAddress =
                            dhcpv6_client_addr.take().ok_or(anyhow::anyhow!(
                                "DHCPv6 was not being performed on host interface with id={}",
                                interface_id
                            ))?;
                    }
                    InterfaceState::WlanAp(WlanApInterfaceState {}) => {
                        return Err(anyhow::anyhow!(
                            "should not have a DNS watcher for a WLAN AP interface with id={}",
                            interface_id
                        ));
                    }
                }
            }
        }

        let () = self
            .update_dns_servers(source, vec![])
            .await
            .with_context(|| {
                format!("error clearing DNS servers for {:?} after completing DNS watcher", source)
            })
            .or_else(|e| match e {
                errors::Error::NonFatal(e) => {
                    error!("non-fatal error: {:?}", e);
                    Ok(())
                }
                errors::Error::Fatal(e) => Err(e),
            })?;

        // The watcher stream may have already been removed if it was exhausted so we don't
        // care what the return value is. At the end of this function, it is guaranteed that
        // `dns_watchers` will not have a stream for `source` anyways.
        let _: Option<Pin<Box<stream::BoxStream<'_, _>>>> = dns_watchers.remove(&source);

        Ok(())
    }

    /// Run the network configuration eventloop.
    ///
    /// The device directory will be monitored for device events and the netstack will be
    /// configured with a new interface on new device discovery.
    async fn run(mut self) -> Result<(), anyhow::Error> {
        let ethdev_dir_path = &format!("{}/{}", DEV_PATH, devices::EthernetDevice::PATH);
        let mut ethdev_dir_watcher_stream = fvfs_watcher::Watcher::new(
            open_directory_in_namespace(ethdev_dir_path, OPEN_RIGHT_READABLE)
                .context("error opening ethdev directory")?,
        )
        .await
        .with_context(|| format!("creating watcher for ethdevs {}", ethdev_dir_path))?
        .fuse();

        let netdev_dir_path = &format!("{}/{}", DEV_PATH, devices::NetworkDevice::PATH);
        let mut netdev_dir_watcher_stream = fvfs_watcher::Watcher::new(
            open_directory_in_namespace(netdev_dir_path, OPEN_RIGHT_READABLE)
                .context("error opening netdev directory")?,
        )
        .await
        .with_context(|| format!("error creating watcher for netdevs {}", netdev_dir_path))?
        .fuse();

        let if_watcher_event_stream =
            fnet_interfaces_ext::event_stream_from_state(&self.interface_state)
                .context("error creating interface watcher event stream")?
                .fuse();
        futures::pin_mut!(if_watcher_event_stream);

        let (dns_server_watcher, dns_server_watcher_req) =
            fidl::endpoints::create_proxy::<fnet_name::DnsServerWatcherMarker>()
                .context("error creating dns server watcher")?;
        let () = self
            .stack
            .get_dns_server_watcher(dns_server_watcher_req)
            .context("get dns server watcher")?;
        let netstack_dns_server_stream = dns_server_watcher::new_dns_server_stream(
            DnsServersUpdateSource::Netstack,
            dns_server_watcher,
        )
        .map(move |(s, r)| (s, r.context("error getting next Netstack DNS server update event")))
        .boxed();

        let dns_watchers = DnsServerWatchers::empty();
        // `Fuse` (the return of `fuse`) guarantees that once the underlying stream is
        // exhausted, future attempts to poll the stream will return `None`. This would
        // be undesirable if we needed to support a scenario where all streams are
        // exhausted before adding a new stream to the `StreamMap`. However,
        // `netstack_dns_server_stream` is not expected to end so we can fuse the
        // `StreamMap` without issue.
        let mut dns_watchers = dns_watchers.fuse();
        assert!(
            dns_watchers
                .get_mut()
                .insert(DnsServersUpdateSource::Netstack, netstack_dns_server_stream)
                .is_none(),
            "dns watchers should be empty"
        );

        debug!("starting eventloop...");

        loop {
            let () = futures::select! {
                ethdev_dir_watcher_res = ethdev_dir_watcher_stream.try_next() => {
                    let event = ethdev_dir_watcher_res
                        .with_context(|| format!("error watching ethdevs at {}", ethdev_dir_path))?
                        .ok_or_else(|| anyhow::anyhow!(
                            "ethdev directory {} watcher stream ended unexpectedly",
                            ethdev_dir_path
                        ))?;
                    self
                        .handle_dev_event::<devices::EthernetDevice>(ethdev_dir_path, event)
                        .await
                        .context("handle ethdev event")?
                }
                netdev_dir_watcher_res = netdev_dir_watcher_stream.try_next() => {
                    let event = netdev_dir_watcher_res
                        .with_context(|| format!("error watching netdevs at {}", netdev_dir_path))?
                        .ok_or_else(|| anyhow::anyhow!(
                            "netdev directory {} watcher stream ended unexpectedly",
                            netdev_dir_path
                        ))?;
                    self
                        .handle_dev_event::<devices::NetworkDevice>(netdev_dir_path, event)
                        .await
                        .context("handle netdev event")?
                }
                if_watcher_res = if_watcher_event_stream.try_next() => {
                    let event = if_watcher_res
                        .context("error watching interface property changes")?
                        .ok_or(
                            anyhow::anyhow!("interface watcher event stream ended unexpectedly")
                        )?;
                    trace!("got interfaces watcher event = {:?}", event);

                    self
                        .handle_interface_watcher_event(
                            event, dns_watchers.get_mut()
                        )
                        .await
                        .context("handle interface watcher event")
                        .or_else(|e| match e {
                            errors::Error::NonFatal(e) => {
                                error!("non-fatal error: {:?}", e);
                                Ok(())
                            }
                            errors::Error::Fatal(e) => Err(e),
                        })?
                }
                dns_watchers_res = dns_watchers.next() => {
                    let (source, res) = dns_watchers_res
                        .ok_or(anyhow::anyhow!("dns watchers stream should never be exhausted"))?;
                    let servers = match res {
                        Ok(s) => s,
                        Err(e) => {
                            // TODO(fxbug.dev/57484): Restart the DNS server watcher.
                            error!("non-fatal error getting next event \
                                from DNS server watcher stream with source = {:?}: {:?}", source, e);
                            let () = self
                                .handle_dns_server_watcher_done(source, dns_watchers.get_mut())
                                .await
                                .with_context(|| {
                                    format!("error handling completion of DNS server watcher for \
                                        {:?}", source)
                                })?;
                            continue;
                        }
                    };

                    self.update_dns_servers(source, servers).await.with_context(|| {
                        format!("error handling DNS servers update from {:?}", source)
                    }).or_else(|e| match e {
                        errors::Error::NonFatal(e) => {
                            error!("non-fatal error: {:?}", e);
                            Ok(())
                        }
                        errors::Error::Fatal(e) => Err(e),
                    })?
                }
                complete => break,
            };
        }

        Err(anyhow::anyhow!("eventloop ended unexpectedly"))
    }

    /// Handles an interface watcher event (existing, added, changed, or removed).
    async fn handle_interface_watcher_event(
        &mut self,
        event: fnet_interfaces::Event,
        watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<(), errors::Error> {
        match self
            .interface_properties
            .update(event)
            .context("failed to update interface properties with watcher event")
            .map_err(errors::Error::Fatal)?
        {
            fnet_interfaces_ext::UpdateResult::Added(properties) => {
                match self.interface_states.get_mut(&properties.id) {
                    Some(state) => state
                        .on_discovery(properties, self.dhcpv6_client_provider.as_ref(), watchers)
                        .await
                        .context("failed to handle interface added event"),
                    // An interface netcfg won't be configuring was added, do nothing.
                    None => Ok(()),
                }
            }
            fnet_interfaces_ext::UpdateResult::Existing(properties) => {
                match self.interface_states.get_mut(&properties.id) {
                    Some(state) => state
                        .on_discovery(properties, self.dhcpv6_client_provider.as_ref(), watchers)
                        .await
                        .context("failed to handle existing interface event"),
                    // An interface netcfg won't be configuring was discovered, do nothing.
                    None => Ok(()),
                }
            }
            fnet_interfaces_ext::UpdateResult::Changed {
                previous: fnet_interfaces::Properties { online: previous_online, .. },
                current: current_properties,
            } => {
                let &fnet_interfaces_ext::Properties {
                    id, ref name, online, ref addresses, ..
                } = current_properties;
                match self.interface_states.get_mut(&id) {
                    // An interface netcfg is not configuring was changed, do nothing.
                    None => return Ok(()),
                    Some(InterfaceState::Host(HostInterfaceState { dhcpv6_client_addr })) => {
                        let dhcpv6_client_provider =
                            if let Some(dhcpv6_client_provider) = &self.dhcpv6_client_provider {
                                dhcpv6_client_provider
                            } else {
                                return Ok(());
                            };

                        // Stop DHCPv6 client if interface went down.
                        if !online {
                            let sockaddr = match dhcpv6_client_addr.take() {
                                Some(s) => s,
                                None => return Ok(()),
                            };

                            info!(
                                "host interface {} (id={}) went down \
                                so stopping DHCPv6 client w/ sockaddr = {}",
                                name,
                                id,
                                sockaddr.display_ext(),
                            );

                            return dhcpv6::stop_client(
                                &self.lookup_admin,
                                &mut self.dns_servers,
                                id,
                                watchers,
                            )
                            .await
                            .with_context(|| {
                                format!(
                                    "error stopping DHCPv6 client on down interface {} (id={})",
                                    name, id
                                )
                            });
                        }

                        // Stop the DHCPv6 client if its address can no longer be found on the
                        // interface.
                        if let Some(sockaddr) = dhcpv6_client_addr {
                            let &mut fnet::Ipv6SocketAddress { address, port: _, zone_index: _ } =
                                sockaddr;
                            if !addresses.iter().any(
                                |&fnet_interfaces_ext::Address {
                                     addr: fnet::Subnet { addr, prefix_len: _ },
                                     valid_until: _,
                                 }| {
                                    addr == fnet::IpAddress::Ipv6(address)
                                },
                            ) {
                                let sockaddr = *sockaddr;
                                *dhcpv6_client_addr = None;

                                info!(
                                    "stopping DHCPv6 client on host interface {} (id={}) \
                                    w/ removed sockaddr = {}",
                                    name,
                                    id,
                                    sockaddr.display_ext(),
                                );

                                let () =
                                    dhcpv6::stop_client(
                                        &self.lookup_admin,
                                        &mut self.dns_servers,
                                        id,
                                        watchers,
                                    )
                                    .await
                                    .with_context(|| {
                                        format!(
                                            "error stopping DHCPv6 client on  interface {} (id={}) \
                                            since sockaddr {} was removed",
                                            name, id, sockaddr.display_ext()
                                        )
                                    })?;
                            }
                        }

                        // Start a DHCPv6 client if there isn't one.
                        if dhcpv6_client_addr.is_none() {
                            *dhcpv6_client_addr = start_dhcpv6_client(
                                current_properties,
                                dhcpv6_client_provider,
                                watchers,
                            )?;
                        }
                        Ok(())
                    }
                    Some(InterfaceState::WlanAp(WlanApInterfaceState {})) => {
                        // TODO(fxbug.dev/55879): Stop the DHCP server when the address it is
                        // listening on is removed.
                        let dhcp_server = if let Some(dhcp_server) = &self.dhcp_server {
                            dhcp_server
                        } else {
                            return Ok(());
                        };

                        if previous_online.map_or(true, |previous_online| previous_online == online)
                        {
                            return Ok(());
                        }

                        if online {
                            info!(
                                "WLAN AP interface {} (id={}) came up so starting DHCP server",
                                name, id
                            );
                            dhcpv4::start_server(dhcp_server)
                                .await
                                .context("error starting DHCP server")
                        } else {
                            info!(
                                "WLAN AP interface {} (id={}) went down so stopping DHCP server",
                                name, id
                            );
                            dhcpv4::stop_server(dhcp_server)
                                .await
                                .context("error stopping DHCP server")
                        }
                    }
                }
            }
            fnet_interfaces_ext::UpdateResult::Removed(fnet_interfaces_ext::Properties {
                id,
                name,
                ..
            }) => {
                match self.interface_states.remove(&id) {
                    // An interface netcfg was not responsible for configuring was removed, do
                    // nothing.
                    None => Ok(()),
                    Some(InterfaceState::Host(HostInterfaceState { mut dhcpv6_client_addr })) => {
                        let sockaddr = match dhcpv6_client_addr.take() {
                            Some(s) => s,
                            None => return Ok(()),
                        };

                        info!(
                            "host interface {} (id={}) removed \
                            so stopping DHCPv6 client w/ sockaddr = {}",
                            name,
                            id,
                            sockaddr.display_ext()
                        );

                        dhcpv6::stop_client(&self.lookup_admin, &mut self.dns_servers, id, watchers)
                            .await
                            .with_context(|| {
                                format!(
                                    "error stopping DHCPv6 client on removed interface {} (id={})",
                                    name, id
                                )
                            })
                    }
                    Some(InterfaceState::WlanAp(WlanApInterfaceState {})) => {
                        if let Some(dhcp_server) = &self.dhcp_server {
                            // The DHCP server should only run on the WLAN AP interface, so stop it
                            // since the AP interface is removed.
                            info!(
                                "WLAN AP interface {} (id={}) is removed, stopping DHCP server",
                                name, id
                            );
                            dhcpv4::stop_server(dhcp_server)
                                .await
                                .context("error stopping DHCP server")
                        } else {
                            Ok(())
                        }
                    }
                }
                .context("failed to handle interface removed event")
            }
            fnet_interfaces_ext::UpdateResult::NoChange => Ok(()),
        }
    }

    /// Handle an event from `D`'s device directory.
    async fn handle_dev_event<D: devices::Device>(
        &mut self,
        dev_dir_path: &str,
        event: fvfs_watcher::WatchMessage,
    ) -> Result<(), anyhow::Error> {
        let fvfs_watcher::WatchMessage { event, filename } = event;
        trace!("got {:?} {} event for {}", event, D::NAME, filename.display());

        if filename == path::PathBuf::from(THIS_DIRECTORY) {
            debug!("skipping {} w/ filename = {}", D::NAME, filename.display());
            return Ok(());
        }

        match event {
            fvfs_watcher::WatchEvent::ADD_FILE | fvfs_watcher::WatchEvent::EXISTING => {
                let filepath = path::Path::new(dev_dir_path).join(filename);

                info!("found new {} at {}", D::NAME, filepath.display());

                let mut i = 0;
                loop {
                    // TODO(fxbug.dev/56559): The same interface may flap so instead of using a
                    // temporary name, try to determine if the interface flapped and wait
                    // for the teardown to complete.
                    match self.add_new_device::<D>(&filepath, i == 0 /* stable_name */).await {
                        Ok(()) => {
                            break;
                        }
                        Err(devices::AddDeviceError::AlreadyExists) => {
                            i += 1;
                            if i == MAX_ADD_DEVICE_ATTEMPTS {
                                error!(
                                    "failed to add {} at {} after {} attempts due to already exists error",
                                    D::NAME,
                                    filepath.display(),
                                    MAX_ADD_DEVICE_ATTEMPTS,
                                );

                                break;
                            }

                            warn!(
                                "got already exists error on attempt {} of adding {} at {}, trying again...",
                                i,
                                D::NAME,
                                filepath.display()
                            );
                        }
                        Err(devices::AddDeviceError::Other(errors::Error::NonFatal(e))) => {
                            error!(
                                "non-fatal error adding {} at {}: {:?}",
                                D::NAME,
                                filepath.display(),
                                e
                            );

                            break;
                        }
                        Err(devices::AddDeviceError::Other(errors::Error::Fatal(e))) => {
                            return Err(e.context(format!(
                                "error adding new {} at {}",
                                D::NAME,
                                filepath.display()
                            )));
                        }
                    }
                }
            }
            fvfs_watcher::WatchEvent::IDLE | fvfs_watcher::WatchEvent::REMOVE_FILE => {}
            event => {
                debug!(
                    "unrecognized event {:?} for {} filename {}",
                    event,
                    D::NAME,
                    filename.display()
                );
            }
        }

        Ok(())
    }

    /// Add a device at `filepath` to the netstack.
    async fn add_new_device<D: devices::Device>(
        &mut self,
        filepath: &path::PathBuf,
        stable_name: bool,
    ) -> Result<(), devices::AddDeviceError> {
        let (topological_path, device_instance) = D::get_topo_path_and_device(filepath)
            .await
            .context("error getting topological path and device")?;

        info!("{} {} has topological path {}", D::NAME, filepath.display(), topological_path);

        let (info, mac) = D::device_info_and_mac(&device_instance)
            .await
            .context("error getting device info and MAC")?;
        if !self.allow_virtual_devices && !info.is_physical() {
            warn!("{} at {} is not a physical device, skipping", D::NAME, filepath.display());
            return Ok(());
        }

        let wlan = info.is_wlan();
        let (metric, features) = crate::get_metric_and_features(wlan);
        let eth_info = D::eth_device_info(info, mac, features);
        let interface_name = if stable_name {
            match self.persisted_interface_config.get_stable_name(
                &topological_path, /* TODO(tamird): we can probably do
                                    * better with std::borrow::Cow. */
                mac,
                wlan,
            ) {
                Ok(name) => name.to_string(),
                Err(interface::NameGenerationError::FileUpdateError { name, err }) => {
                    warn!("failed to update interface (topo path = {}, mac = {}, wlan = {}) with new name = {}: {}", topological_path, mac, wlan, name, err);
                    name.to_string()
                }
                Err(interface::NameGenerationError::GenerationError(e)) => {
                    return Err(devices::AddDeviceError::Other(errors::Error::Fatal(
                        e.context("error getting stable name"),
                    )))
                }
            }
        } else {
            self.persisted_interface_config.get_temporary_name(wlan)
        };

        info!(
            "adding {} at {} to stack with name = {}",
            D::NAME,
            filepath.display(),
            interface_name
        );

        let mut config = matchers::config_for_device(
            &eth_info,
            interface_name.clone(),
            &topological_path,
            metric,
            &self.default_config_rules,
            filepath,
        );

        let interface_id =
            D::add_to_stack(self, topological_path.clone(), &mut config.fidl, device_instance)
                .await
                .context("error adding to stack")?;

        self.configure_eth_interface(interface_id, &topological_path, &eth_info, &config)
            .await
            .context("error configuring ethernet interface")
            .map_err(devices::AddDeviceError::Other)
    }

    /// Configure an ethernet interface.
    ///
    /// If the device has `WLAN_AP_TOPO_PATH_CONTAINS` in its topological path, it will
    /// be configured as a WLAN AP (see `configure_wlan_ap_and_dhcp_server` for more details).
    /// Otherwise, it will be configured as a host (see `configure_host` for more details).
    async fn configure_eth_interface(
        &mut self,
        interface_id: u64,
        topological_path: &str,
        info: &feth_ext::EthernetInfo,
        config: &matchers::InterfaceConfig,
    ) -> Result<(), errors::Error> {
        let interface_id_u32: u32 = interface_id.try_into().expect("NIC ID should fit in a u32");

        if topological_path.contains(WLAN_AP_TOPO_PATH_CONTAINS) {
            if let Some(id) = self.interface_states.iter().find_map(|(id, state)| {
                if state.is_wlan_ap() {
                    Some(id)
                } else {
                    None
                }
            }) {
                return Err(errors::Error::NonFatal(anyhow::anyhow!("multiple WLAN AP interfaces are not supported, have WLAN AP interface with id = {}", id)));
            }

            match self.interface_states.entry(interface_id) {
                Entry::Occupied(entry) => {
                    return Err(errors::Error::Fatal(anyhow::anyhow!("multiple interfaces with the same ID = {}; attempting to add state for a WLAN AP, existing state = {:?}", entry.key(), entry.get())));
                }
                Entry::Vacant(entry) => {
                    let _: &mut InterfaceState = entry.insert(InterfaceState::new_wlan_ap());
                }
            }

            info!(
                "discovered WLAN AP interface with id={}, configuring interface and DHCP server",
                interface_id
            );

            let () = self
                .configure_wlan_ap_and_dhcp_server(interface_id_u32, config.fidl.name.clone())
                .await
                .context("error configuring wlan ap and dhcp server")?;
        } else {
            match self.interface_states.entry(interface_id) {
                Entry::Occupied(entry) => {
                    return Err(errors::Error::Fatal(anyhow::anyhow!("multiple interfaces with the same ID = {}; attempting to add state for a host, existing state = {:?}", entry.key(), entry.get())));
                }
                Entry::Vacant(entry) => {
                    let _: &mut InterfaceState = entry.insert(InterfaceState::new_host());
                }
            }

            info!("discovered host interface with id={}, configuring interface", interface_id);

            let () = self
                .configure_host(interface_id_u32, info, config)
                .await
                .context("error configuring host")?;
        }

        self.netstack
            .set_interface_status(interface_id_u32, true)
            .context("error sending set interface status request")
            .map_err(errors::Error::Fatal)
    }

    /// Configure host interface.
    async fn configure_host(
        &mut self,
        interface_id: u32,
        info: &feth_ext::EthernetInfo,
        config: &matchers::InterfaceConfig,
    ) -> Result<(), errors::Error> {
        if should_enable_filter(&self.filter_enabled_interface_types, &info.features) {
            info!("enable filter for nic {}", interface_id);
            let status = self
                .filter
                .enable_interface(interface_id.into())
                .await
                .with_context(|| {
                    format!("error sending enable filter request on nic {}", interface_id)
                })
                .map_err(errors::Error::Fatal)?;
            if status != fnet_filter::Status::Ok {
                return Err(errors::Error::NonFatal(anyhow::anyhow!(
                    "failed to enable filter on nic {} with status = {:?}",
                    interface_id,
                    status
                )));
            }
        } else {
            info!("disable filter for nic {}", interface_id);
            let status = self
                .filter
                .disable_interface(interface_id.into())
                .await
                .with_context(|| {
                    format!("error sending disable filter request on nic {}", interface_id)
                })
                .map_err(errors::Error::Fatal)?;
            if status != fnet_filter::Status::Ok {
                return Err(errors::Error::NonFatal(anyhow::anyhow!(
                    "failed to disable filter on nic {} with status = {:?}",
                    interface_id,
                    status
                )));
            }
        };

        match config.ip_address_config {
            matchers::IpAddressConfig::Dhcp => {
                let (dhcp_client, server_end) =
                    fidl::endpoints::create_proxy::<fnet_dhcp::ClientMarker>()
                        .context("dhcp client: failed to create fidl endpoints")
                        .map_err(errors::Error::Fatal)?;
                let () = self
                    .netstack
                    .get_dhcp_client(interface_id, server_end)
                    .await
                    .context("failed to call netstack.get_dhcp_client")
                    .map_err(errors::Error::Fatal)?
                    .map_err(zx::Status::from_raw)
                    .context("failed to get dhcp client")
                    .map_err(errors::Error::NonFatal)?;
                let () = dhcp_client
                    .start()
                    .await
                    .context("error sending start DHCP client request")
                    .map_err(errors::Error::Fatal)?
                    .map_err(zx::Status::from_raw)
                    .context("failed to start dhcp client")
                    .map_err(errors::Error::NonFatal)?;
            }
            matchers::IpAddressConfig::StaticIp(subnet) => {
                let fnet::Subnet { mut addr, prefix_len } = subnet.into();
                let fnetstack::NetErr { status, message } = self
                    .netstack
                    .set_interface_address(interface_id, &mut addr, prefix_len)
                    .await
                    .context("error sending set interface address request")
                    .map_err(errors::Error::Fatal)?;
                if status != fnetstack::Status::Ok {
                    // Do not consider this a fatal error because the interface could
                    // have been removed after it was added, but before we reached
                    // this point.
                    return Err(errors::Error::NonFatal(anyhow::anyhow!(
                        "failed to set interface (id={}) address to {:?} with status = {:?}: {}",
                        interface_id,
                        addr,
                        status,
                        message
                    )));
                }
            }
        }

        Ok(())
    }

    /// Configure the WLAN AP and the DHCP server to serve requests on its network.
    ///
    /// Note, this method will not start the DHCP server, it will only be configured
    /// with the parameters so it is ready to be started when an interface UP event
    /// is received for the WLAN AP.
    async fn configure_wlan_ap_and_dhcp_server(
        &mut self,
        interface_id: u32,
        name: String,
    ) -> Result<(), errors::Error> {
        // Calculate and set the interface address based on the network address.
        // The interface address should be the first available address.
        let network_addr_as_u32 = u32::from_be_bytes(WLAN_AP_NETWORK_ADDR.addr);
        let interface_addr_as_u32 = network_addr_as_u32 + 1;
        let addr = fnet::Ipv4Address { addr: interface_addr_as_u32.to_be_bytes() };
        let fnetstack::NetErr { status, message } = self
            .netstack
            .set_interface_address(interface_id, &mut addr.into_ext(), WLAN_AP_PREFIX_LEN)
            .await
            .context("error sending set interface address request")
            .map_err(errors::Error::Fatal)?;
        if status != fnetstack::Status::Ok {
            // Do not consider this a fatal error because the interface could
            // have been removed after it was added, but before we reached
            // this point.
            return Err(errors::Error::NonFatal(anyhow::anyhow!(
                "failed to set interface address for WLAN AP with status = {:?}: {}",
                status,
                message
            )));
        }

        let dhcp_server = if let Some(dhcp_server) = &self.dhcp_server {
            dhcp_server
        } else {
            warn!("cannot configure DHCP server for WLAN AP (interface ID={}) since DHCP server service is not available", interface_id);
            return Ok(());
        };

        // First we clear any leases that the server knows about since the server
        // will be used on a new interface. If leases exist, configuring the DHCP
        // server parameters may fail (AddressPool).
        debug!("clearing DHCP leases");
        let () = dhcp_server
            .clear_leases()
            .await
            .context("error sending clear DHCP leases request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error clearing DHCP leases request")
            .map_err(errors::Error::NonFatal)?;

        // Configure the DHCP server.
        let v = vec![addr.into()];
        debug!("setting DHCP IpAddrs parameter to {:?}", v);
        let () = dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::IpAddrs(v))
            .await
            .context("error sending set DHCP IpAddrs parameter request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error setting DHCP IpAddrs parameter")
            .map_err(errors::Error::NonFatal)?;

        let v = vec![name];
        debug!("setting DHCP BoundDeviceNames parameter to {:?}", v);
        let () = dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::BoundDeviceNames(v))
            .await
            .context("error sending set DHCP BoundDeviceName parameter request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error setting DHCP BoundDeviceNames parameter")
            .map_err(errors::Error::NonFatal)?;

        let v = fnet_dhcp::LeaseLength {
            default: Some(WLAN_AP_DHCP_LEASE_TIME_SECONDS),
            max: Some(WLAN_AP_DHCP_LEASE_TIME_SECONDS),
            ..fnet_dhcp::LeaseLength::EMPTY
        };
        debug!("setting DHCP LeaseLength parameter to {:?}", v);
        let () = dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::Lease(v))
            .await
            .context("error sending set DHCP LeaseLength parameter request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error setting DHCP LeaseLength parameter")
            .map_err(errors::Error::NonFatal)?;

        let host_mask = dhcp::configuration::SubnetMask::try_from(WLAN_AP_PREFIX_LEN)
            .context("error creating host mask from prefix length")
            .map_err(errors::Error::NonFatal)?;
        let broadcast_addr =
            host_mask.broadcast_of(&std::net::Ipv4Addr::from_fidl(WLAN_AP_NETWORK_ADDR));
        let broadcast_addr_as_u32: u32 = broadcast_addr.into();
        // The start address of the DHCP pool should be the first address after the WLAN AP
        // interface address.
        let dhcp_pool_start =
            fnet::Ipv4Address { addr: (interface_addr_as_u32 + 1).to_be_bytes() }.into();
        // The last address of the DHCP pool should be the last available address
        // in the interface's network. This is the address immediately before the
        // network's broadcast address.
        let dhcp_pool_end = fnet::Ipv4Address { addr: (broadcast_addr_as_u32 - 1).to_be_bytes() };

        let v = fnet_dhcp::AddressPool {
            prefix_length: Some(WLAN_AP_PREFIX_LEN),
            range_start: Some(dhcp_pool_start),
            range_stop: Some(dhcp_pool_end),
            ..fnet_dhcp::AddressPool::EMPTY
        };
        debug!("setting DHCP AddressPool parameter to {:?}", v);
        dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::AddressPool(v))
            .await
            .context("error sending set DHCP AddressPool parameter request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error setting DHCP AddressPool parameter")
            .map_err(errors::Error::NonFatal)
    }
}

/// Return the metric and ethernet features.
fn get_metric_and_features(wlan: bool) -> (u32, feth::Features) {
    // Hardcode the interface metric. Eventually this should
    // be part of the config file.
    if wlan {
        (INTF_METRIC_WLAN, feth::Features::Wlan)
    } else {
        (INTF_METRIC_ETH, feth::Features::empty())
    }
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    // We use a closure so we can grab the error and log it before returning from main.
    let f = || async {
        let opt: Opt = argh::from_env();
        let Opt { allow_virtual_devices, min_severity, config_data } = &opt;

        let () = fuchsia_syslog::init().context("cannot init logger")?;
        fsyslog::set_severity((*min_severity).into());

        info!("starting");
        debug!("starting with options = {:?}", opt);

        let Config {
            dns_config: DnsConfig { servers },
            rules: default_config_rules,
            filter_config,
            filter_enabled_interface_types,
        } = Config::load(config_data)?;

        let filter_enabled_interface_types: HashSet<InterfaceType> =
            filter_enabled_interface_types.into_iter().map(Into::into).collect();
        if filter_enabled_interface_types.iter().any(|interface_type| match interface_type {
            &InterfaceType::UNKNOWN(_) => true,
            _ => false,
        }) {
            return Err(anyhow::anyhow!(
                "failed to parse filter_enabled_interface_types: {:?}",
                filter_enabled_interface_types
            ));
        };

        let mut netcfg = NetCfg::new(
            *allow_virtual_devices,
            default_config_rules,
            filter_enabled_interface_types,
        )
        .await
        .context("error creating new netcfg instance")?;

        let () =
            netcfg.update_filters(filter_config).await.context("update filters based on config")?;

        let servers = servers.into_iter().map(static_source_from_ip).collect();
        debug!("updating default servers to {:?}", servers);
        let () = netcfg
            .update_dns_servers(DnsServersUpdateSource::Default, servers)
            .await
            .context("error updating default DNS servers")
            .or_else(|e| match e {
                errors::Error::NonFatal(e) => {
                    error!("non-fatal error: {:?}", e);
                    Ok(())
                }
                errors::Error::Fatal(e) => Err(e),
            })?;

        // Should never return.
        netcfg.run().await.context("error running eventloop")
    };

    match f().await {
        Ok(()) => unreachable! {},
        Err(e) => {
            let err_str = format!("fatal error running main: {:?}", e);
            error!("{}", err_str);
            panic!("{}", err_str);
        }
    }
}

#[cfg(test)]
mod tests {
    use futures::future::{self, FutureExt as _, TryFutureExt as _};
    use futures::stream::TryStreamExt as _;
    use matchers::{ConfigOption, InterfaceMatcher, InterfaceSpec};
    use net_declare::{fidl_ip, fidl_ip_v6};

    use super::*;

    impl Config {
        pub fn load_str(s: &str) -> Result<Self, anyhow::Error> {
            let config = serde_json::from_str(s)
                .with_context(|| format!("could not deserialize the config data {}", s))?;
            Ok(config)
        }
    }

    struct ServerEnds {
        lookup_admin: fnet_name::LookupAdminRequestStream,
        dhcpv6_client_provider: fnet_dhcpv6::ClientProviderRequestStream,
    }

    impl Into<anyhow::Error> for errors::Error {
        fn into(self) -> anyhow::Error {
            match self {
                errors::Error::NonFatal(e) => e,
                errors::Error::Fatal(e) => e,
            }
        }
    }

    fn test_netcfg<'a>() -> Result<(NetCfg<'a>, ServerEnds), anyhow::Error> {
        let (stack, _stack_server) = fidl::endpoints::create_proxy::<fnet_stack::StackMarker>()
            .context("error creating stack endpoints")?;
        let (netstack, _netstack_server) =
            fidl::endpoints::create_proxy::<fnetstack::NetstackMarker>()
                .context("error creating netstack endpoints")?;
        let (lookup_admin, lookup_admin_server) =
            fidl::endpoints::create_proxy::<fnet_name::LookupAdminMarker>()
                .context("error creating lookup_admin endpoints")?;
        let (filter, _filter_server) = fidl::endpoints::create_proxy::<fnet_filter::FilterMarker>()
            .context("create filter endpoints")?;
        let (interface_state, _interface_state_server) =
            fidl::endpoints::create_proxy::<fnet_interfaces::StateMarker>()
                .context("create interface state endpoints")?;
        let (dhcp_server, _dhcp_server_server) =
            fidl::endpoints::create_proxy::<fnet_dhcp::Server_Marker>()
                .context("error creating dhcp_server endpoints")?;
        let (dhcpv6_client_provider, dhcpv6_client_provider_server) =
            fidl::endpoints::create_proxy::<fnet_dhcpv6::ClientProviderMarker>()
                .context("error creating dhcpv6_client_provider endpoints")?;
        let persisted_interface_config =
            interface::FileBackedConfig::load(&PERSISTED_INTERFACE_CONFIG_FILEPATH)
                .context("error loading persistent interface configurations")?;

        Ok((
            NetCfg {
                stack,
                netstack,
                lookup_admin,
                filter,
                interface_state,
                dhcp_server: Some(dhcp_server),
                dhcpv6_client_provider: Some(dhcpv6_client_provider),
                persisted_interface_config,
                allow_virtual_devices: false,
                filter_enabled_interface_types: Default::default(),
                default_config_rules: Default::default(),
                interface_properties: Default::default(),
                interface_states: Default::default(),
                dns_servers: Default::default(),
            },
            ServerEnds {
                lookup_admin: lookup_admin_server
                    .into_stream()
                    .context("error converting lookup_admin server to stream")?,
                dhcpv6_client_provider: dhcpv6_client_provider_server
                    .into_stream()
                    .context("error converting dhcpv6_client_provider server to stream")?,
            },
        ))
    }

    const INTERFACE_ID: u64 = 1;
    const DHCPV6_DNS_SOURCE: DnsServersUpdateSource =
        DnsServersUpdateSource::Dhcpv6 { interface_id: INTERFACE_ID };
    const LINK_LOCAL_SOCKADDR1: fnet::Ipv6SocketAddress = fnet::Ipv6SocketAddress {
        address: fidl_ip_v6!("fe80::1"),
        port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
        zone_index: INTERFACE_ID,
    };
    const LINK_LOCAL_SOCKADDR2: fnet::Ipv6SocketAddress = fnet::Ipv6SocketAddress {
        address: fidl_ip_v6!("fe80::2"),
        port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
        zone_index: INTERFACE_ID,
    };
    const GLOBAL_ADDR: fnet::Subnet = fnet::Subnet { addr: fidl_ip!("2000::1"), prefix_len: 64 };
    const DNS_SERVER1: fnet::SocketAddress = fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
        address: fidl_ip_v6!("2001::1"),
        port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
        zone_index: 0,
    });
    const DNS_SERVER2: fnet::SocketAddress = fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
        address: fidl_ip_v6!("2001::2"),
        port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
        zone_index: 0,
    });

    fn ipv6addrs(a: Option<fnet::Ipv6SocketAddress>) -> Vec<fnet_interfaces::Address> {
        // The DHCPv6 client will only use a link-local address but we include a global address
        // and expect it to not be used.
        std::iter::once(fnet_interfaces::Address {
            addr: Some(GLOBAL_ADDR),
            valid_until: Some(fuchsia_zircon::Time::INFINITE.into_nanos()),
            ..fnet_interfaces::Address::EMPTY
        })
        .chain(a.map(|fnet::Ipv6SocketAddress { address, port: _, zone_index: _ }| {
            fnet_interfaces::Address {
                addr: Some(fnet::Subnet { addr: fnet::IpAddress::Ipv6(address), prefix_len: 64 }),
                valid_until: Some(fuchsia_zircon::Time::INFINITE.into_nanos()),
                ..fnet_interfaces::Address::EMPTY
            }
        }))
        .collect()
    }

    /// Handle receiving a netstack interface changed event.
    async fn handle_interface_changed_event(
        netcfg: &mut NetCfg<'_>,
        dns_watchers: &mut DnsServerWatchers<'_>,
        online: Option<bool>,
        addresses: Option<Vec<fnet_interfaces::Address>>,
    ) -> Result<(), errors::Error> {
        let event = fnet_interfaces::Event::Changed(fnet_interfaces::Properties {
            id: Some(INTERFACE_ID),
            name: None,
            device_class: None,
            online,
            addresses,
            has_default_ipv4_route: None,
            has_default_ipv6_route: None,
            ..fnet_interfaces::Properties::EMPTY
        });
        netcfg.handle_interface_watcher_event(event, dns_watchers).await
    }

    /// Make sure that a new DHCPv6 client was requested, and verify its parameters.
    async fn check_new_client(
        server: &mut fnet_dhcpv6::ClientProviderRequestStream,
        sockaddr: fnet::Ipv6SocketAddress,
        dns_watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<fnet_dhcpv6::ClientRequestStream, anyhow::Error> {
        let evt =
            server.try_next().await.context("error getting next dhcpv6 client provider event")?;
        let client_server =
            match evt.ok_or(anyhow::anyhow!("expected dhcpv6 client provider request"))? {
                fnet_dhcpv6::ClientProviderRequest::NewClient {
                    params,
                    request,
                    control_handle: _,
                } => {
                    assert_eq!(
                        params,
                        fnet_dhcpv6::NewClientParams {
                            interface_id: Some(INTERFACE_ID),
                            address: Some(sockaddr),
                            config: Some(fnet_dhcpv6::ClientConfig {
                                information_config: Some(fnet_dhcpv6::InformationConfig {
                                    dns_servers: Some(true),
                                    ..fnet_dhcpv6::InformationConfig::EMPTY
                                }),
                                ..fnet_dhcpv6::ClientConfig::EMPTY
                            }),
                            ..fnet_dhcpv6::NewClientParams::EMPTY
                        }
                    );

                    request.into_stream().context("error converting client server end to stream")?
                }
            };
        assert!(dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should have a watcher");
        Ok(client_server)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stopping_dhcpv6_with_down_lookup_admin() -> Result<(), anyhow::Error> {
        let (mut netcfg, ServerEnds { lookup_admin, mut dhcpv6_client_provider }) =
            test_netcfg().context("error creating test netcfg")?;
        let mut dns_watchers = DnsServerWatchers::empty();

        // Mock a new interface being discovered by NetCfg (we only need to make NetCfg aware of a
        // NIC with ID `INTERFACE_ID` to test DHCPv6).
        matches::assert_matches!(
            netcfg.interface_states.insert(INTERFACE_ID, InterfaceState::new_host()),
            None
        );

        // Should start the DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = netcfg
            .handle_interface_watcher_event(
                fnet_interfaces::Event::Added(fnet_interfaces::Properties {
                    id: Some(INTERFACE_ID),
                    name: Some("testif01".to_string()),
                    device_class: Some(fnet_interfaces::DeviceClass::Loopback(
                        fnet_interfaces::Empty {},
                    )),
                    online: Some(true),
                    addresses: Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR1))),
                    has_default_ipv4_route: Some(false),
                    has_default_ipv6_route: Some(false),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                &mut dns_watchers,
            )
            .await
            .context("error handling interface added event with interface up and sockaddr1")
            .map_err::<anyhow::Error, _>(Into::into)?;
        let _: fnet_dhcpv6::ClientRequestStream =
            check_new_client(&mut dhcpv6_client_provider, LINK_LOCAL_SOCKADDR1, &mut dns_watchers)
                .await
                .context("error checking for new client with sockaddr1")?;

        // Drop the server-end of the lookup admin to simulate a down lookup admin service.
        let () = std::mem::drop(lookup_admin);

        // Not having any more link local IPv6 addresses should terminate the client.
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            None,
            Some(ipv6addrs(None)),
        )
        .await
        .context("error handling interface changed event with sockaddr1 removed")
        .or_else(|e| match e {
            errors::Error::NonFatal(e) => {
                println!("non-fatal error: {:?}", e);
                Ok(())
            }
            errors::Error::Fatal(e) => Err(e),
        })?;

        // Another update without any link-local IPv6 addresses should do nothing
        // since the DHCPv6 client was already stopped.
        let () =
            handle_interface_changed_event(&mut netcfg, &mut dns_watchers, None, Some(Vec::new()))
                .await
                .context("error handling interface changed event with sockaddr1 removed")
                .or_else(|e| match e {
                    errors::Error::NonFatal(e) => {
                        println!("non-fatal error: {:?}", e);
                        Ok(())
                    }
                    errors::Error::Fatal(e) => Err(e),
                })?;

        // Update interface with a link-local address to create a new DHCPv6 client.
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            None,
            Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR1))),
        )
        .await
        .context("error handling interface changed event with sockaddr1 removed")
        .or_else(|e| match e {
            errors::Error::NonFatal(e) => {
                println!("non-fatal error: {:?}", e);
                Ok(())
            }
            errors::Error::Fatal(e) => Err(e),
        })?;
        let _: fnet_dhcpv6::ClientRequestStream =
            check_new_client(&mut dhcpv6_client_provider, LINK_LOCAL_SOCKADDR1, &mut dns_watchers)
                .await
                .context("error checking for new client with sockaddr1")?;

        // Update offline status to down to stop DHCPv6 client.
        let () = handle_interface_changed_event(&mut netcfg, &mut dns_watchers, Some(false), None)
            .await
            .context("error handling interface changed event with sockaddr1 removed")
            .or_else(|e| match e {
                errors::Error::NonFatal(e) => {
                    println!("non-fatal error: {:?}", e);
                    Ok(())
                }
                errors::Error::Fatal(e) => Err(e),
            })?;

        // Update interface with new addresses but leave offline status as down.
        handle_interface_changed_event(&mut netcfg, &mut dns_watchers, None, Some(ipv6addrs(None)))
            .await
            .context("error handling interface changed event with sockaddr1 removed")
            .or_else(|e| match e {
                errors::Error::NonFatal(e) => {
                    println!("non-fatal error: {:?}", e);
                    Ok(())
                }
                errors::Error::Fatal(e) => Err(e),
            })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dhcpv6() -> Result<(), anyhow::Error> {
        /// Waits for a `SetDnsServers` request with the specified servers.
        async fn run_lookup_admin_once(
            server: &mut fnet_name::LookupAdminRequestStream,
            expected_servers: &Vec<fnet::SocketAddress>,
        ) -> Result<(), anyhow::Error> {
            let req = server
                .try_next()
                .await
                .context("error getting next lookup admin request")?
                .ok_or(anyhow::anyhow!("expected lookup admin request"))?;
            if let fnet_name::LookupAdminRequest::SetDnsServers { servers, responder } = req {
                if expected_servers != &servers {
                    return Err(anyhow::anyhow!(
                        "got SetDnsServers servers = {:?}, want = {:?}",
                        servers,
                        expected_servers
                    ));
                }
                responder.send(&mut Ok(())).context("error sending set dns servers response")
            } else {
                Err(anyhow::anyhow!("unknown request = {:?}", req))
            }
        }

        let (mut netcfg, mut servers) = test_netcfg().context("error creating test netcfg")?;
        let mut dns_watchers = DnsServerWatchers::empty();

        // Mock a fake DNS update from the netstack.
        let netstack_servers = vec![DNS_SERVER1];
        let ((), ()) = future::try_join(
            netcfg
                .update_dns_servers(
                    DnsServersUpdateSource::Netstack,
                    vec![fnet_name::DnsServer_ {
                        address: Some(DNS_SERVER1),
                        source: Some(fnet_name::DnsServerSource::StaticSource(
                            fnet_name::StaticDnsServerSource::EMPTY,
                        )),
                        ..fnet_name::DnsServer_::EMPTY
                    }],
                )
                .map(|r| r.context("error updating netstack DNS servers").map_err(Into::into)),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error setting netstack DNS servers")?;

        // Mock a new interface being discovered by NetCfg (we only need to make NetCfg aware of a
        // NIC with ID `INTERFACE_ID` to test DHCPv6).
        matches::assert_matches!(
            netcfg.interface_states.insert(INTERFACE_ID, InterfaceState::new_host()),
            None
        );

        // Should start the DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = netcfg
            .handle_interface_watcher_event(
                fnet_interfaces::Event::Added(fnet_interfaces::Properties {
                    id: Some(INTERFACE_ID),
                    name: Some("testif01".to_string()),
                    device_class: Some(fnet_interfaces::DeviceClass::Loopback(
                        fnet_interfaces::Empty {},
                    )),
                    online: Some(true),
                    addresses: Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR1))),
                    has_default_ipv4_route: Some(false),
                    has_default_ipv6_route: Some(false),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                &mut dns_watchers,
            )
            .await
            .context("error handling interface added event with interface up and sockaddr1")
            .map_err::<anyhow::Error, _>(Into::into)?;
        let mut client_server = check_new_client(
            &mut servers.dhcpv6_client_provider,
            LINK_LOCAL_SOCKADDR1,
            &mut dns_watchers,
        )
        .await
        .context("error checking for new client with sockaddr1")?;

        // Mock a fake DNS update from the DHCPv6 client.
        let ((), ()) = future::try_join(
            netcfg
                .update_dns_servers(
                    DHCPV6_DNS_SOURCE,
                    vec![fnet_name::DnsServer_ {
                        address: Some(DNS_SERVER2),
                        source: Some(fnet_name::DnsServerSource::Dhcpv6(
                            fnet_name::Dhcpv6DnsServerSource {
                                source_interface: Some(INTERFACE_ID.into()),
                                ..fnet_name::Dhcpv6DnsServerSource::EMPTY
                            },
                        )),
                        ..fnet_name::DnsServer_::EMPTY
                    }],
                )
                .map(|r| r.context("error updating netstack DNS servers").map_err(Into::into)),
            run_lookup_admin_once(&mut servers.lookup_admin, &vec![DNS_SERVER2, DNS_SERVER1])
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error setting dhcpv6 DNS servers")?;

        // Not having any more link local IPv6 addresses should terminate the client.
        let ((), ()) = future::try_join(
            handle_interface_changed_event(
                &mut netcfg,
                &mut dns_watchers,
                None,
                Some(ipv6addrs(None)),
            )
            .map(|r| r.context("error handling interface changed event with sockaddr1 removed"))
            .map_err(Into::into),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to empty addresses")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        matches::assert_matches!(client_server.try_next().await, Ok(None));

        // Should start a new DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            None,
            Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR2))),
        )
        .await
        .context("error handling netstack event with sockaddr2 added")
        .map_err(Into::<anyhow::Error>::into)?;
        let mut client_server = check_new_client(
            &mut servers.dhcpv6_client_provider,
            LINK_LOCAL_SOCKADDR2,
            &mut dns_watchers,
        )
        .await
        .context("error checking for new client with sockaddr2")?;

        // Interface being down should terminate the client.
        let ((), ()) = future::try_join(
            handle_interface_changed_event(
                &mut netcfg,
                &mut dns_watchers,
                Some(false), /* down */
                None,
            )
            .map(|r| r.context("error handling interface changed event with interface down"))
            .map_err(Into::into),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to interface down")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        matches::assert_matches!(client_server.try_next().await, Ok(None));

        // Should start a new DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            Some(true), /* up */
            None,
        )
        .await
        .context("error handling interface up event")
        .map_err(Into::<anyhow::Error>::into)?;
        let mut client_server = check_new_client(
            &mut servers.dhcpv6_client_provider,
            LINK_LOCAL_SOCKADDR2,
            &mut dns_watchers,
        )
        .await
        .context("error checking for new client with sockaddr2 after interface up again")?;

        // Should start a new DHCPv6 client when we get an interface changed event that shows the
        // interface as up with a new link-local address.
        let ((), ()) = future::try_join(
            handle_interface_changed_event(
                &mut netcfg,
                &mut dns_watchers,
                None,
                Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR1))),
            )
            .map(|r| {
                r.context(
                    "error handling interface change event with sockaddr1 replacing sockaddr2",
                )
            })
            .map_err(Into::into),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to address change")?;
        matches::assert_matches!(client_server.try_next().await, Ok(None));
        let _client_server = check_new_client(
            &mut servers.dhcpv6_client_provider,
            LINK_LOCAL_SOCKADDR1,
            &mut dns_watchers,
        )
        .await
        .context("error checking for new client with sockaddr1 after address change")?;

        // Complete the DNS server watcher then start a new one.
        let ((), ()) = future::try_join(
            netcfg
                .handle_dns_server_watcher_done(DHCPV6_DNS_SOURCE, &mut dns_watchers)
                .map(|r| r.context("error handling completion of dns server watcher")),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling DNS server watcher completion")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            None,
            Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR2))),
        )
        .await
        .context("error handling interface change event with sockaddr2 replacing sockaddr1")
        .map_err(Into::<anyhow::Error>::into)?;
        let mut client_server = check_new_client(
            &mut servers.dhcpv6_client_provider,
            LINK_LOCAL_SOCKADDR2,
            &mut dns_watchers,
        )
        .await
        .context("error checking for new client with sockaddr2 after completing dns watcher")?;

        // An event that indicates the interface is removed should stop the client.
        let ((), ()) = future::try_join(
            netcfg
                .handle_interface_watcher_event(
                    fnet_interfaces::Event::Removed(INTERFACE_ID),
                    &mut dns_watchers,
                )
                .map(|r| r.context("error handling interface removed event"))
                .map_err(Into::into),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to interface removal")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        matches::assert_matches!(client_server.try_next().await, Ok(None));
        assert!(!netcfg.interface_states.contains_key(&INTERFACE_ID));

        Ok(())
    }

    #[test]
    fn test_config() {
        let config_str = r#"
{
  "dns_config": {
    "servers": ["8.8.8.8"]
  },
  "rules": [
    [ ["all", "all"], ["ip_address", "dhcp"] ]
  ],
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": ["wlan"]
}
"#;

        let Config {
            dns_config: DnsConfig { servers },
            rules: default_config_rules,
            filter_config,
            filter_enabled_interface_types,
        } = Config::load_str(config_str).unwrap();

        assert_eq!(vec!["8.8.8.8".parse::<std::net::IpAddr>().unwrap()], servers);
        assert_eq!(
            vec![InterfaceSpec {
                matcher: InterfaceMatcher::All,
                config: ConfigOption::IpConfig(matchers::IpAddressConfig::Dhcp),
            }],
            default_config_rules
        );
        let FilterConfig { rules, nat_rules, rdr_rules } = filter_config;
        assert_eq!(Vec::<String>::new(), rules);
        assert_eq!(Vec::<String>::new(), nat_rules);
        assert_eq!(Vec::<String>::new(), rdr_rules);

        assert_eq!(vec!["wlan"], filter_enabled_interface_types);
    }

    #[test]
    fn test_to_interface_type() {
        assert_eq!(InterfaceType::ETHERNET, "ethernet".to_string().into());
        assert_eq!(InterfaceType::WLAN, "wlan".to_string().into());
        assert_eq!(InterfaceType::UNKNOWN("bluetooth".to_string()), "bluetooth".to_string().into());
        assert_eq!(InterfaceType::UNKNOWN("Ethernet".to_string()), "Ethernet".to_string().into());
        assert_eq!(InterfaceType::UNKNOWN("Wlan".to_string()), "Wlan".to_string().into());
    }

    #[test]
    fn test_should_enable_filter() {
        let types_empty: HashSet<InterfaceType> = [].iter().cloned().collect();
        let types_ethernet: HashSet<InterfaceType> =
            [InterfaceType::ETHERNET].iter().cloned().collect();
        let types_wlan: HashSet<InterfaceType> = [InterfaceType::WLAN].iter().cloned().collect();
        let types_ethernet_wlan: HashSet<InterfaceType> =
            [InterfaceType::ETHERNET, InterfaceType::WLAN].iter().cloned().collect();

        let features_wlan = feth::Features::Wlan;
        let features_loopback = feth::Features::Loopback;
        let features_synthetic = feth::Features::Synthetic;
        let features_empty = feth::Features::empty();

        assert_eq!(should_enable_filter(&types_empty, &features_empty), false);
        assert_eq!(should_enable_filter(&types_empty, &features_wlan), false);
        assert_eq!(should_enable_filter(&types_empty, &features_synthetic), false);
        assert_eq!(should_enable_filter(&types_empty, &features_loopback), false);
        assert_eq!(should_enable_filter(&types_ethernet, &features_empty), true);
        assert_eq!(should_enable_filter(&types_ethernet, &features_wlan), false);
        assert_eq!(should_enable_filter(&types_ethernet, &features_synthetic), true);
        assert_eq!(should_enable_filter(&types_ethernet, &features_loopback), false);
        assert_eq!(should_enable_filter(&types_wlan, &features_empty), false);
        assert_eq!(should_enable_filter(&types_wlan, &features_wlan), true);
        assert_eq!(should_enable_filter(&types_wlan, &features_synthetic), false);
        assert_eq!(should_enable_filter(&types_wlan, &features_loopback), false);
        assert_eq!(should_enable_filter(&types_ethernet_wlan, &features_empty), true);
        assert_eq!(should_enable_filter(&types_ethernet_wlan, &features_wlan), true);
        assert_eq!(should_enable_filter(&types_ethernet_wlan, &features_synthetic), true);
        assert_eq!(should_enable_filter(&types_ethernet_wlan, &features_loopback), false);
    }
}

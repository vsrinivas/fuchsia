// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used because we use `futures::select!`.
//
// From https://docs.rs/futures/0.3.1/futures/macro.select.html:
//   Note that select! relies on proc-macro-hack, and may require to set the compiler's
//   recursion limit very high, e.g. #![recursion_limit="1024"].
#![recursion_limit = "512"]

extern crate network_manager_core_interface as interface;

mod devices;
mod dhcpv6;
mod dns;
mod errors;
mod matchers;

use std::collections::{hash_map::Entry, HashMap, HashSet};
use std::convert::TryInto as _;
use std::fs;
use std::io;
use std::path;
use std::pin::Pin;
use std::str::FromStr;

use fidl_fuchsia_hardware_ethernet as feth;
use fidl_fuchsia_hardware_ethernet_ext as feth_ext;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcp as fnet_dhcp;
use fidl_fuchsia_net_dhcpv6 as fnet_dhcpv6;
use fidl_fuchsia_net_ext::{self as fnet_ext, IntoExt as _, IpExt as _};
use fidl_fuchsia_net_filter as fnet_filter;
use fidl_fuchsia_net_name as fnet_name;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_stack_ext as fnet_stack_ext;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async::DurationExt as _;
use fuchsia_component::client::connect_to_service;
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

/// Path to devfs in netemul environments.
const NETEMUL_DEV_PATH: &str = "/vdev";

/// File that stores persistant interface configurations.
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
const WLAN_AP_NETWORK_ADDR: fnet::Ipv4Address = fidl_ip_v4!(192.168.255.248);

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
type DnsServerWatchers<'a> = async_helpers::stream::StreamMap<
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
    #[argh(switch, short = 'n')]
    netemul: bool,

    /// minimum severity for logs.
    #[argh(option, short = 'm', default = "LogLevel::Info")]
    min_severity: LogLevel,

    /// config file to use
    #[argh(option, short = 'c', default = "\"default.json\".to_string()")]
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

/// Common state for all interfaces that hold interface-specific state.
#[derive(Debug)]
struct CommonInterfaceState {
    /// Is this interface up?
    up: bool,

    /// Has this interface been observed by the Netstack yet?
    seen: bool,

    /// Interface specific state.
    specific: InterfaceState,
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

impl CommonInterfaceState {
    fn new_host() -> CommonInterfaceState {
        Self::new(InterfaceState::Host(HostInterfaceState { dhcpv6_client_addr: None }))
    }

    fn new_wlan_ap() -> CommonInterfaceState {
        Self::new(InterfaceState::WlanAp(WlanApInterfaceState {}))
    }

    fn new(specific: InterfaceState) -> CommonInterfaceState {
        CommonInterfaceState { up: false, seen: false, specific }
    }

    fn is_wlan_ap(&self) -> bool {
        match self.specific {
            InterfaceState::Host(_) => false,
            InterfaceState::WlanAp(_) => true,
        }
    }
}

/// Network Configuration state.
struct NetCfg<'a> {
    stack: fnet_stack::StackProxy,
    netstack: fnetstack::NetstackProxy,
    lookup_admin: fnet_name::LookupAdminProxy,
    filter: fnet_filter::FilterProxy,
    dhcp_server: fnet_dhcp::Server_Proxy,
    dhcpv6_client_provider: fnet_dhcpv6::ClientProviderProxy,

    device_dir_path: &'a str,
    allow_virtual_devices: bool,

    persisted_interface_config: interface::FileBackedConfig<'a>,

    filter_enabled_interface_types: HashSet<InterfaceType>,
    default_config_rules: Vec<matchers::InterfaceSpec>,

    interface_states: HashMap<u64, CommonInterfaceState>,

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
        source: Some(fnet_name::DnsServerSource::StaticSource(fnet_name::StaticDnsServerSource {})),
    }
}

impl<'a> NetCfg<'a> {
    /// Returns a new `NetCfg`.
    fn new(
        device_dir_path: &'a str,
        allow_virtual_devices: bool,
        default_config_rules: Vec<matchers::InterfaceSpec>,
        filter_enabled_interface_types: HashSet<InterfaceType>,
    ) -> Result<NetCfg<'a>, anyhow::Error> {
        // Note, just because `connect_to_service` returns without an error does not mean
        // the service is available to us. We will observe an error when we try to use the
        // proxy if the service is not available.
        let stack = connect_to_service::<fnet_stack::StackMarker>()
            .context("could not connect to stack")?;
        let netstack = connect_to_service::<fnetstack::NetstackMarker>()
            .context("could not connect to netstack")?;
        let lookup_admin = connect_to_service::<fnet_name::LookupAdminMarker>()
            .context("could not connect to lookup admin")?;
        let filter = connect_to_service::<fnet_filter::FilterMarker>()
            .context("could not connect to filter")?;
        let dhcp_server = connect_to_service::<fnet_dhcp::Server_Marker>()
            .context("could not connect to DHCP Server")?;
        let dhcpv6_client_provider = connect_to_service::<fnet_dhcpv6::ClientProviderMarker>()
            .context("could not connect to DHCPv6 client provider")?;
        let persisted_interface_config =
            interface::FileBackedConfig::load(&PERSISTED_INTERFACE_CONFIG_FILEPATH)
                .context("error loading persistent interface configurations")?;

        Ok(NetCfg {
            stack,
            netstack,
            lookup_admin,
            filter,
            dhcp_server,
            dhcpv6_client_provider,
            device_dir_path,
            allow_virtual_devices,
            persisted_interface_config,
            filter_enabled_interface_types,
            default_config_rules,
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

                match &mut state.specific {
                    InterfaceState::Host(state) => {
                        let _: fnet::Ipv6SocketAddress =
                            state.dhcpv6_client_addr.take().ok_or(anyhow::anyhow!(
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

        match self.update_dns_servers(source, vec![]).await {
            Ok(()) => {}
            Err(errors::Error::NonFatal(e)) => {
                error!("non-fatal error clearing DNS servers for {:?} after completing DNS watcher: {}", source, e);
            }
            Err(errors::Error::Fatal(e)) => {
                return Err(e.context(format!("error learing DNS servers for {:?}", source)));
            }
        }

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
        let ethdev_dir_path = format!("{}/{}", self.device_dir_path, devices::EthernetDevice::PATH);
        let ethdev_dir_path = &ethdev_dir_path;
        let mut ethdev_dir_watcher_stream = fvfs_watcher::Watcher::new(
            open_directory_in_namespace(ethdev_dir_path, OPEN_RIGHT_READABLE)
                .context("error opening ethdev directory")?,
        )
        .await
        .with_context(|| format!("creating watcher for ethdevs {}", ethdev_dir_path))?
        .fuse();

        let netdev_dir_path = format!("{}/{}", self.device_dir_path, devices::NetworkDevice::PATH);
        let netdev_dir_path = &netdev_dir_path;
        let mut netdev_dir_watcher_stream = fvfs_watcher::Watcher::new(
            open_directory_in_namespace(netdev_dir_path, OPEN_RIGHT_READABLE)
                .context("error opening netdev directory")?,
        )
        .await
        .with_context(|| format!("error creating watcher for netdevs {}", netdev_dir_path))?
        .fuse();

        let mut netstack_stream = self.netstack.take_event_stream().fuse();

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
                    self.handle_dev_event::<devices::EthernetDevice>(ethdev_dir_path, event).await.context("handle ethdev event")?
                }
                netdev_dir_watcher_res = netdev_dir_watcher_stream.try_next() => {
                    let event = netdev_dir_watcher_res
                        .with_context(|| format!("error watching netdevs at {}", netdev_dir_path))?
                        .ok_or_else(|| anyhow::anyhow!(
                            "netdev directory {} watcher stream ended unexpectedly",
                            netdev_dir_path
                        ))?;
                    self.handle_dev_event::<devices::NetworkDevice>(netdev_dir_path, event).await.context("handle netdev event")?
                }
                netstack_res = netstack_stream.try_next() => {
                    let event = netstack_res
                        .context("Netstack event stream")?
                        .ok_or(
                            anyhow::anyhow!("Netstack event stream ended unexpectedly",
                            ))?;
                    self.handle_netstack_event(event, dns_watchers.get_mut()).await.context("error handling netstack event")?
                }
                dns_watchers_res = dns_watchers.next() => {
                    let (source, res) = dns_watchers_res.ok_or(anyhow::anyhow!("dns watchers stream should never be exhausted"))?;
                    let servers = match res {
                        Ok(s) => s,
                        Err(e) => {
                            // TODO(57484): Restart the DNS server watcher.
                            error!("non-fatal error getting next event from DNS server watcher stream with source = {:?}: {}", source, e);
                            let () = self.handle_dns_server_watcher_done(source, dns_watchers.get_mut())
                                .await
                                .with_context(|| format!("error handling completion of DNS serever watcher for {:?}", source))?;
                            continue;
                        }
                    };

                    match self.update_dns_servers(source, servers).await {
                        Ok(()) => {},
                        Err(errors::Error::NonFatal(e)) => {
                            error!("non-fatal error handling DNS servers update from {:?}: {}", source, e);
                        }
                        Err(errors::Error::Fatal(e)) => {
                            return Err(e.context(format!("error handling {:?} DNS servers update", source)));
                        }
                    }
                }
                complete => break,
            };
        }

        Err(anyhow::anyhow!("eventloop ended unexpectedly"))
    }

    /// Handles an event from fuchsia.netstack.Netstack.
    ///
    /// Starts or stops the DHCP server when a known WLAN AP interface is brought up or down,
    /// respectively.
    async fn handle_netstack_event(
        &mut self,
        event: fnetstack::NetstackEvent,
        watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<(), anyhow::Error> {
        trace!("got netstack event = {:?}", event);

        // Do not mark an interface for removal if an interface changed event has not been
        // received for it yet. Even if the interface was removed from the Netstack
        // immediately after it was added, we should get an event with the interface. This
        // is so that we do not prematurely clear the interface state.
        let mut removable_ids: HashSet<_> = self
            .interface_states
            .iter()
            .filter_map(|(id, s)| if s.seen { Some(*id) } else { None })
            .collect();

        let fnetstack::NetstackEvent::OnInterfacesChanged { interfaces } = event;
        for interface in &interfaces {
            let id = interface.id;
            let _: bool = removable_ids.remove(&u64::from(id));
            match self.handle_netstack_interface_update(interface, watchers).await {
                Ok(()) => {}
                Err(errors::Error::NonFatal(e)) => {
                    error!(
                        "non-fatal error handling netstack update event for interface w/ id={}: {}",
                        id, e
                    );
                }
                Err(errors::Error::Fatal(e)) => {
                    return Err(
                        e.context(format!("error handling netstack interface (id={}) update", id))
                    );
                }
            }
        }

        for id in removable_ids.into_iter() {
            match self.handle_interface_removed(id, watchers).await {
                Ok(()) => {}
                Err(errors::Error::NonFatal(e)) => {
                    error!(
                        "non-fatal error handling removed event for interface with id={}: {}",
                        id, e
                    );
                }
                Err(errors::Error::Fatal(e)) => {
                    return Err(e.context(format!("error handling removed interface (id={})", id)));
                }
            }
        }

        Ok(())
    }

    async fn handle_interface_removed(
        &mut self,
        interface_id: u64,
        watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<(), errors::Error> {
        // TODO(56136): Configure interfaces that were not discovered through devfs.
        let state = match self.interface_states.remove(&interface_id) {
            Some(s) => s,
            None => {
                return Err(errors::Error::Fatal(anyhow::anyhow!(
                    "attempted to remove state for an unknown interface with ID = {}",
                    interface_id
                )));
            }
        };

        match state.specific {
            InterfaceState::Host(mut state) => {
                let sockaddr = match &state.dhcpv6_client_addr {
                    Some(s) => s,
                    None => return Ok(()),
                };

                info!("host interface with id={} removed so stopping DHCPv6 client w/ sockaddr = {:?}", interface_id, sockaddr);

                let () = dhcpv6::stop_client(
                    &self.lookup_admin,
                    &mut self.dns_servers,
                    interface_id,
                    watchers,
                )
                .await
                .with_context(|| {
                    format!(
                        "error stopping DHCPv6 client on removed interface with id={}",
                        interface_id
                    )
                })?;
                state.dhcpv6_client_addr = None;
            }
            InterfaceState::WlanAp(WlanApInterfaceState {}) => {
                // The DHCP server should only run on the WLAN AP interface, so stop it
                // since the AP interface is removed.
                info!(
                    "WLAN AP interface with id={} is removed, stopping DHCP server",
                    interface_id
                );
                let () = self.stop_dhcp_server().await.context("error stopping DHCP server")?;
            }
        }

        Ok(())
    }

    async fn handle_netstack_interface_update(
        &mut self,
        interface: &fnetstack::NetInterface,
        watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<(), errors::Error> {
        let fnetstack::NetInterface { id, flags, name, ipv6addrs, .. } = interface;
        let up = flags.contains(fnetstack::Flags::Up);

        let state = match self.interface_states.get_mut(&From::from(*id)) {
            Some(s) => s,
            None => return Ok(()),
        };

        state.seen = true;
        let prev_up = state.up;
        state.up = up;

        match &mut state.specific {
            InterfaceState::Host(state) => {
                let id = Into::into(*id);

                if !up {
                    let sockaddr = match &state.dhcpv6_client_addr {
                        Some(s) => s,
                        None => return Ok(()),
                    };

                    info!(
                        "host interface {} (id={}) went down so stopping DHCPv6 client w/ sockaddr = {:?}",
                        name, id, sockaddr,
                    );

                    let () = dhcpv6::stop_client(
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
                    })?;

                    state.dhcpv6_client_addr = None;
                    return Ok(());
                }

                // Make sure the address we used for the DHCPv6 client is still assigned.
                if let Some(sockaddr) = &state.dhcpv6_client_addr {
                    if !ipv6addrs.iter().any(|x| x.addr == fnet::IpAddress::Ipv6(sockaddr.address))
                    {
                        info!(
                            "stopping DHCPv6 client on host interface {} (id={}) w/ removed sockaddr = {:?}",
                            name, id, sockaddr,
                        );

                        let () = dhcpv6::stop_client(
                            &self.lookup_admin,
                            &mut self.dns_servers,
                            id,
                            watchers,
                        )
                            .await
                            .with_context(|| {
                                format!("error stopping DHCPv6 client on interface {} (id={}) since sockaddr {:?} was removed", name, id, sockaddr)
                            })?;
                        state.dhcpv6_client_addr = None;
                    }
                }

                if state.dhcpv6_client_addr.is_some() {
                    return Ok(());
                }

                // Create a new DHCPv6 client with a link-local address assigned to the
                // interface.
                let sockaddr = match ipv6addrs.iter().find_map(|x| {
                    match x.addr {
                        fnet::IpAddress::Ipv6(a) => {
                            // Only use unicast link-local addresses.
                            if a.is_unicast_linklocal() {
                                return Some(fnet::Ipv6SocketAddress {
                                    address: a,
                                    port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
                                    zone_index: id,
                                });
                            }
                        }
                        fnet::IpAddress::Ipv4(_) => {}
                    }

                    None
                }) {
                    Some(s) => s,
                    None => return Ok(()),
                };

                info!(
                    "host interface {} (id={}) up with a link-local address so starting DHCPv6 client using socketaddr {:?}",
                    name, id, sockaddr,
                );

                match dhcpv6::start_client(&self.dhcpv6_client_provider, id, sockaddr, watchers) {
                    Ok(()) => {
                        state.dhcpv6_client_addr = Some(sockaddr);
                    }
                    Err(errors::Error::NonFatal(e)) => {
                        error!(
                            "failed to start DHCPv6 client on interface {} (id={}) w/ sockaddr {:?}: {}",
                            name, id, sockaddr, e
                        );
                    }
                    Err(errors::Error::Fatal(e)) => {
                        return Err(errors::Error::Fatal(e.context(format!(
                            "error starting DHCPv6 client on interface {} (id={})",
                            name, id
                        ))));
                    }
                }
            }
            InterfaceState::WlanAp(WlanApInterfaceState {}) => {
                // TODO(55879): Stop the DHCP server when the address it is listening on
                // is removed.
                if prev_up == up {
                    return Ok(());
                }

                if up {
                    info!("WLAN AP interface {} (id={}) came up so starting DHCP server", name, id);
                    let () =
                        self.start_dhcp_server().await.context("error starting DHCP server")?;
                } else {
                    info!(
                        "WLAN AP interface {} (id={}) went down so stopping DHCP server",
                        name, id
                    );
                    let () = self.stop_dhcp_server().await.context("error stopping DHCP server")?;
                }
            }
        }

        Ok(())
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
                    // TODO(56559): The same interface may flap so instead of using a
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
                                "non-fatal error adding {} at {}: {}",
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
            self.persisted_interface_config
                .get_stable_name(
                    &topological_path, /* TODO(tamird): we can probably do
                                        * better with std::borrow::Cow. */
                    mac,
                    wlan,
                )
                .context("error getting stable name")
                .map_err(errors::Error::Fatal)?
                .to_string()
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
            interface_name,
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

    /// Start the DHCP server.
    async fn start_dhcp_server(&self) -> Result<(), errors::Error> {
        self.dhcp_server
            .start_serving()
            .await
            .context("eerror sending start DHCP server request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error starting DHCP server")
            .map_err(errors::Error::NonFatal)
    }

    /// Stop the DHCP server.
    async fn stop_dhcp_server(&self) -> Result<(), errors::Error> {
        self.dhcp_server
            .stop_serving()
            .await
            .context("error sending stop DHCP server request")
            .map_err(errors::Error::NonFatal)
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
                    let _: &mut CommonInterfaceState =
                        entry.insert(CommonInterfaceState::new_wlan_ap());
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
                    let _: &mut CommonInterfaceState =
                        entry.insert(CommonInterfaceState::new_host());
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
            let () = self
                .stack
                .enable_packet_filter(interface_id.into())
                .await
                .context("error sending enable packet filter request")
                .map_err(errors::Error::Fatal)?
                .map_err(fnet_stack_ext::NetstackError)
                .context("failed to enable packet filter")
                .map_err(errors::Error::NonFatal)?;
        } else {
            info!("disable filter for nic {}", interface_id);
            let () = self
                .stack
                .disable_packet_filter(interface_id.into())
                .await
                .context("error sending disable packet filter request")
                .map_err(errors::Error::Fatal)?
                .map_err(fnet_stack_ext::NetstackError)
                .context("failed to disable packet filter")
                .map_err(errors::Error::NonFatal)?;
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

        // First we clear any leases that the server knows about since the server
        // will be used on a new interface. If leases exist, configuring the DHCP
        // server parameters may fail (AddressPool).
        debug!("clearing DHCP leases");
        let () = self
            .dhcp_server
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
        let () = self
            .dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::IpAddrs(v))
            .await
            .context("error sending set DHCP IpAddrs parameter request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error setting DHCP IpAddrs parameter")
            .map_err(errors::Error::NonFatal)?;

        let v = vec![name];
        debug!("setting DHCP BoundDeviceNames parameter to {:?}", v);
        let () = self
            .dhcp_server
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
        };
        debug!("setting DHCP LeaseLength parameter to {:?}", v);
        let () = self
            .dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::Lease(v))
            .await
            .context("error sending set DHCP LeaseLength parameter request")
            .map_err(errors::Error::NonFatal)?
            .map_err(zx::Status::from_raw)
            .context("error setting DHCP LeaseLength parameter")
            .map_err(errors::Error::NonFatal)?;

        let host_mask = u32::max_value() >> WLAN_AP_PREFIX_LEN;
        let broadcast_addr_as_u32 = network_addr_as_u32 | host_mask;
        let broadcast_addr = fnet::Ipv4Address { addr: broadcast_addr_as_u32.to_be_bytes() };
        // The start address of the DHCP pool should be the first address after the WLAN AP
        // interface address.
        let dhcp_pool_start =
            fnet::Ipv4Address { addr: (interface_addr_as_u32 + 1).to_be_bytes() }.into();
        // The last address of the DHCP pool should be the last available address
        // in the interface's network. This is the address immediately before the
        // network's broadcast address.
        let dhcp_pool_end = fnet::Ipv4Address { addr: (broadcast_addr_as_u32 - 1).to_be_bytes() };

        let v = fnet_dhcp::AddressPool {
            network_id: Some(WLAN_AP_NETWORK_ADDR),
            broadcast: Some(broadcast_addr),
            mask: Some(fnet::Ipv4Address { addr: (!host_mask).to_be_bytes() }),
            pool_range_start: Some(dhcp_pool_start),
            pool_range_stop: Some(dhcp_pool_end),
        };
        debug!("setting DHCP AddressPool parameter to {:?}", v);
        self.dhcp_server
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

        fsyslog::init_with_tags(&["netcfg"])?;
        fsyslog::set_severity(opt.min_severity.into());

        info!("starting");
        debug!("starting with options = {:?}", opt);

        let Config {
            dns_config: DnsConfig { servers },
            rules: default_config_rules,
            filter_config,
            filter_enabled_interface_types,
        } = Config::load(format!("/config/data/{}", opt.config_data))?;

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

        let (path, allow_virtual) =
            if opt.netemul { (NETEMUL_DEV_PATH, true) } else { (DEV_PATH, false) };

        let mut netcfg =
            NetCfg::new(path, allow_virtual, default_config_rules, filter_enabled_interface_types)
                .context("error creating new netcfg instance")?;

        let () =
            netcfg.update_filters(filter_config).await.context("update filters based on config")?;

        let servers = servers.into_iter().map(static_source_from_ip).collect();
        debug!("updating default servers to {:?}", servers);
        match netcfg.update_dns_servers(DnsServersUpdateSource::Default, servers).await {
            Ok(()) => {}
            Err(errors::Error::NonFatal(e)) => {
                error!("non-fatal error updating default DNS servers: {}", e);
            }
            Err(errors::Error::Fatal(e)) => {
                return Err(e.context("error updating default DNS servers"));
            }
        }

        // Should never return.
        netcfg.run().await.context("error running eventloop")
    };

    match f().await {
        Ok(()) => unreachable! {},
        Err(e) => {
            let err_str = format!("fatal error running main: {:?}", e);
            error!("{}", err_str);
            panic!(err_str);
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
        let (dhcp_server, _dhcp_server_server) =
            fidl::endpoints::create_proxy::<fnet_dhcp::Server_Marker>()
                .context("error creating dhcp_serveer endpoints")?;
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
                dhcp_server,
                dhcpv6_client_provider,
                device_dir_path: "/vdev",
                persisted_interface_config: persisted_interface_config,
                allow_virtual_devices: false,
                filter_enabled_interface_types: Default::default(),
                default_config_rules: Default::default(),
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dhcpv6() -> Result<(), anyhow::Error> {
        const INTERFACE_ID: u32 = 1;
        const DHCPV6_DNS_SOURCE: DnsServersUpdateSource =
            DnsServersUpdateSource::Dhcpv6 { interface_id: INTERFACE_ID as u64 };
        const LINK_LOCAL_SOCKADDR1: fnet::Ipv6SocketAddress = fnet::Ipv6SocketAddress {
            address: fidl_ip_v6!(fe80::1),
            port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
            zone_index: INTERFACE_ID as u64,
        };
        const LINK_LOCAL_SOCKADDR2: fnet::Ipv6SocketAddress = fnet::Ipv6SocketAddress {
            address: fidl_ip_v6!(fe80::2),
            port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
            zone_index: INTERFACE_ID as u64,
        };
        const GLOBAL_ADDR: fnet::Subnet = fnet::Subnet { addr: fidl_ip!(2000::1), prefix_len: 64 };
        const DNS_SERVER1: fnet::SocketAddress =
            fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                address: fidl_ip_v6!(2001::1),
                port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
                zone_index: 0,
            });
        const DNS_SERVER2: fnet::SocketAddress =
            fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                address: fidl_ip_v6!(2001::2),
                port: fnet_dhcpv6::DEFAULT_CLIENT_PORT,
                zone_index: 0,
            });

        fn ipv6addrs(a: Option<fnet::Ipv6SocketAddress>) -> Vec<fnet::Subnet> {
            // The DHCPv6 client will only use a link-local address but we include a global address
            // and expect it to not be used.
            let mut v = vec![GLOBAL_ADDR];
            if let Some(a) = a {
                v.push(fnet::Subnet { addr: fnet::IpAddress::Ipv6(a.address), prefix_len: 64 })
            }
            v
        }

        /// Handle receving a netstack event with a single interface.
        async fn handle_netstack_event(
            netcfg: &mut NetCfg<'_>,
            dns_watchers: &mut DnsServerWatchers<'_>,
            up: bool,
            ipv6addrs: Vec<fnet::Subnet>,
        ) -> Result<(), anyhow::Error> {
            let flags = if up { fnetstack::Flags::Up } else { fnetstack::Flags::empty() };
            let event = fnetstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![fnetstack::NetInterface {
                    id: INTERFACE_ID,
                    flags,
                    features: feth::Features::empty(),
                    configuration: 0,
                    name: "test".to_string(),
                    addr: fidl_ip!(0.0.0.0),
                    netmask: fidl_ip!(0.0.0.0),
                    broadaddr: fidl_ip!(0.0.0.0),
                    ipv6addrs,
                    hwaddr: vec![2, 3, 4, 5, 6, 7],
                }],
            };
            netcfg
                .handle_netstack_event(event, dns_watchers)
                .await
                .context("error handling netstack event")
                .map_err(Into::into)
        };

        /// Make sure that a new DHCPv6 client was requested, and verify its parameters.
        async fn check_new_client(
            server: &mut fnet_dhcpv6::ClientProviderRequestStream,
            sockaddr: fnet::Ipv6SocketAddress,
            dns_watchers: &mut DnsServerWatchers<'_>,
        ) -> Result<fnet_dhcpv6::ClientRequestStream, anyhow::Error> {
            let evt = server
                .try_next()
                .await
                .context("error getting next dhcpv6 client provider event")?;
            let client_server = match evt
                .ok_or(anyhow::anyhow!("expected dhcpv6 client provider request"))?
            {
                fnet_dhcpv6::ClientProviderRequest::NewClient {
                    params,
                    request,
                    control_handle: _,
                } => {
                    assert_eq!(
                        params,
                        fnet_dhcpv6::NewClientParams {
                            interface_id: Some(INTERFACE_ID.into()),
                            address: Some(sockaddr),
                            models: Some(fnet_dhcpv6::OperationalModels {
                                stateless: Some(fnet_dhcpv6::Stateless {
                                    options_to_request: Some(vec![
                                        fnet_dhcpv6::RequestableOptionCode::DnsServers
                                    ]),
                                }),
                            }),
                        }
                    );

                    request.into_stream().context("error converting client server end to stream")?
                }
            };
            assert!(dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should have a watcher");
            Ok(client_server)
        }

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
                responder.send(&mut Ok(())).context("error sending set dns servres response")
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
                            fnet_name::StaticDnsServerSource {},
                        )),
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
            netcfg.interface_states.insert(INTERFACE_ID.into(), CommonInterfaceState::new_host()),
            None
        );

        // Should start the DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = handle_netstack_event(
            &mut netcfg,
            &mut dns_watchers,
            true, /* up */
            ipv6addrs(Some(LINK_LOCAL_SOCKADDR1)),
        )
        .await
        .context("error handling netstack event with sockaddr1")?;
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
                            },
                        )),
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
            handle_netstack_event(
                &mut netcfg,
                &mut dns_watchers,
                true, /* up */
                ipv6addrs(None),
            )
            .map(|r| r.context("error handling netstack event with sockaddr1"))
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
        let () = handle_netstack_event(
            &mut netcfg,
            &mut dns_watchers,
            true, /* up */
            ipv6addrs(Some(LINK_LOCAL_SOCKADDR2)),
        )
        .await
        .context("error handling netstack event with sockaddr2")?;
        let mut client_server = check_new_client(
            &mut servers.dhcpv6_client_provider,
            LINK_LOCAL_SOCKADDR2,
            &mut dns_watchers,
        )
        .await
        .context("error checking for new client with sockaddr2")?;

        // Interface being down should terminate the client.
        let ((), ()) = future::try_join(
            handle_netstack_event(
                &mut netcfg,
                &mut dns_watchers,
                false, /* down */
                ipv6addrs(Some(LINK_LOCAL_SOCKADDR2)),
            )
            .map(|r| r.context("error handling netstack event with sockaddr2 and interface down")),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to interface down")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        matches::assert_matches!(client_server.try_next().await, Ok(None));

        // Should start a new DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = handle_netstack_event(
            &mut netcfg,
            &mut dns_watchers,
            true, /* up */
            ipv6addrs(Some(LINK_LOCAL_SOCKADDR2)),
        )
        .await
        .context("error handling netstack event with sockaddr2")?;
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
            handle_netstack_event(
                &mut netcfg,
                &mut dns_watchers,
                true, /* up */
                ipv6addrs(Some(LINK_LOCAL_SOCKADDR1)),
            )
            .map(|r| r.context("error handling netstack event with sockaddr1 replacing sockaddr2")),
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
        let () = handle_netstack_event(
            &mut netcfg,
            &mut dns_watchers,
            true, /* up */
            ipv6addrs(Some(LINK_LOCAL_SOCKADDR1)),
        )
        .await
        .context("error handling netstack event with sockaddr1 after completing dns watcher")?;
        let mut client_server = check_new_client(
            &mut servers.dhcpv6_client_provider,
            LINK_LOCAL_SOCKADDR1,
            &mut dns_watchers,
        )
        .await
        .context("error checking for new client with sockaddr1 after completing dns watcher")?;

        // An event that indicates the interface is removed should stop the client.
        let ((), ()) = future::try_join(
            netcfg
                .handle_netstack_event(
                    fnetstack::NetstackEvent::OnInterfacesChanged { interfaces: vec![] },
                    &mut dns_watchers,
                )
                .map(|r| r.context("error handling netstack event with empty list of interfaces"))
                .map_err(Into::into),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to interface removal")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        matches::assert_matches!(client_server.try_next().await, Ok(None));
        assert!(!netcfg.interface_states.contains_key(&INTERFACE_ID.into()));

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

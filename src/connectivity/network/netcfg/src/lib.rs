// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod devices;
mod dhcpv4;
mod dhcpv6;
mod dns;
mod errors;
mod interface;
mod virtualization;

use ::dhcpv4::protocol::FromFidlExt as _;
use std::collections::{hash_map::Entry, HashMap, HashSet};
use std::fs;
use std::io;
use std::path;
use std::pin::Pin;
use std::str::FromStr;

use fidl::endpoints::RequestStream as _;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_dhcp as fnet_dhcp;
use fidl_fuchsia_net_dhcpv6 as fnet_dhcpv6;
use fidl_fuchsia_net_ext::{self as fnet_ext, DisplayExt as _, IpExt as _};
use fidl_fuchsia_net_filter as fnet_filter;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_interfaces_ext::{self as fnet_interfaces_ext, Update as _};
use fidl_fuchsia_net_name as fnet_name;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async::DurationExt as _;
use fuchsia_component::client::{clone_namespace_svc, new_protocol_connector_in_dir};
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_vfs_watcher as fvfs_watcher;
use fuchsia_zircon::{self as zx, DurationNum as _};

use anyhow::{anyhow, Context as _};
use async_trait::async_trait;
use async_utils::stream::TryFlattenUnorderedExt as _;
use dns_server_watcher::{DnsServers, DnsServersUpdateSource, DEFAULT_DNS_PORT};
use fuchsia_fs::OpenFlags;
use futures::{StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::fidl_ip_v4;
use serde::Deserialize;
use tracing::{debug, error, info, trace, warn};

use self::devices::DeviceInfo;
use self::errors::ContextExt as _;

/// Interface metrics.
///
/// Interface metrics are used to sort the route table. An interface with a
/// lower metric is favored over one with a higher metric.
#[derive(Clone, Copy, Debug, Deserialize, PartialEq)]
pub struct Metric(u32);

impl Default for Metric {
    // A default value of 600 is chosen for Metric: this provides plenty of space for routing
    // policy on devices with many interfaces (physical or logical) while remaining in the same
    // magnitude as our current default ethernet metric.
    fn default() -> Self {
        Self(600)
    }
}

impl std::fmt::Display for Metric {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Metric(u) = self;
        write!(f, "{}", u)
    }
}

impl From<Metric> for u32 {
    fn from(Metric(u): Metric) -> u32 {
        u
    }
}

/// File that stores persistent interface configurations.
const PERSISTED_INTERFACE_CONFIG_FILEPATH: &str = "/data/net_interfaces.cfg.json";

/// A node that represents the directory it is in.
///
/// `/dir` and `/dir/.` point to the same directory.
const THIS_DIRECTORY: &str = ".";

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
    futures::stream::BoxStream<
        'a,
        (DnsServersUpdateSource, Result<Vec<fnet_name::DnsServer_>, anyhow::Error>),
    >,
>;

/// Defines log levels.
#[derive(Debug, Copy, Clone)]
pub struct LogLevel(diagnostics_log::Severity);

impl Default for LogLevel {
    fn default() -> Self {
        Self(diagnostics_log::Severity::Info)
    }
}

impl FromStr for LogLevel {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, anyhow::Error> {
        match s.to_uppercase().as_str() {
            "TRACE" => Ok(Self(diagnostics_log::Severity::Trace)),
            "DEBUG" => Ok(Self(diagnostics_log::Severity::Debug)),
            "INFO" => Ok(Self(diagnostics_log::Severity::Info)),
            "WARN" => Ok(Self(diagnostics_log::Severity::Warn)),
            "ERROR" => Ok(Self(diagnostics_log::Severity::Error)),
            "FATAL" => Ok(Self(diagnostics_log::Severity::Fatal)),
            _ => Err(anyhow::anyhow!("unrecognized log level = {}", s)),
        }
    }
}

/// Network Configuration tool.
///
/// Configures network components in response to events.
#[derive(argh::FromArgs, Debug)]
struct Opt {
    // TODO(https://fxbug.dev/85683): remove once we use netdevice and netcfg no
    // longer has to differentiate between virtual or physical devices.
    /// should netemul specific configurations be used?
    #[argh(switch)]
    allow_virtual_devices: bool,

    /// minimum severity for logs
    #[argh(option, default = "Default::default()")]
    min_severity: LogLevel,

    /// config file to use
    #[argh(option, default = "\"/config/data/default.json\".to_string()")]
    config_data: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DnsConfig {
    pub servers: Vec<std::net::IpAddr>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FilterConfig {
    pub rules: Vec<String>,
    pub nat_rules: Vec<String>,
    pub rdr_rules: Vec<String>,
}

#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "lowercase")]
enum InterfaceType {
    Ethernet,
    Wlan,
}

#[cfg_attr(test, derive(PartialEq))]
#[derive(Debug, Default, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InterfaceMetrics {
    #[serde(default)]
    pub wlan_metric: Metric,
    #[serde(default)]
    pub eth_metric: Metric,
}

#[derive(Debug, PartialEq, Eq, Hash, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "lowercase")]
pub enum DeviceClass {
    Virtual,
    Ethernet,
    Wlan,
    Ppp,
    Bridge,
    WlanAp,
}

impl From<fidl_fuchsia_hardware_network::DeviceClass> for DeviceClass {
    fn from(device_class: fidl_fuchsia_hardware_network::DeviceClass) -> Self {
        match device_class {
            fidl_fuchsia_hardware_network::DeviceClass::Virtual => DeviceClass::Virtual,
            fidl_fuchsia_hardware_network::DeviceClass::Ethernet => DeviceClass::Ethernet,
            fidl_fuchsia_hardware_network::DeviceClass::Wlan => DeviceClass::Wlan,
            fidl_fuchsia_hardware_network::DeviceClass::Ppp => DeviceClass::Ppp,
            fidl_fuchsia_hardware_network::DeviceClass::Bridge => DeviceClass::Bridge,
            fidl_fuchsia_hardware_network::DeviceClass::WlanAp => DeviceClass::WlanAp,
        }
    }
}

#[derive(Debug, PartialEq, Deserialize)]
#[serde(transparent)]
struct AllowedDeviceClasses(HashSet<DeviceClass>);

impl Default for AllowedDeviceClasses {
    fn default() -> Self {
        // When new variants are added, this exhaustive match will cause a compilation failure as a
        // reminder to add the new variant to the default array.
        match DeviceClass::Virtual {
            DeviceClass::Virtual
            | DeviceClass::Ethernet
            | DeviceClass::Wlan
            | DeviceClass::Ppp
            | DeviceClass::Bridge
            | DeviceClass::WlanAp => {}
        }
        Self(HashSet::from([
            DeviceClass::Virtual,
            DeviceClass::Ethernet,
            DeviceClass::Wlan,
            DeviceClass::Ppp,
            DeviceClass::Bridge,
            DeviceClass::WlanAp,
        ]))
    }
}

// TODO(https://github.com/serde-rs/serde/issues/368): use an inline literal for the default value
// rather than defining a one-off function.
fn dhcpv6_enabled_default() -> bool {
    true
}

#[derive(Debug, Default, Deserialize, PartialEq)]
#[serde(deny_unknown_fields, rename_all = "lowercase")]
struct ForwardedDeviceClasses {
    #[serde(default)]
    pub ipv4: HashSet<DeviceClass>,
    #[serde(default)]
    pub ipv6: HashSet<DeviceClass>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct Config {
    pub dns_config: DnsConfig,
    pub filter_config: FilterConfig,
    pub filter_enabled_interface_types: HashSet<InterfaceType>,
    #[serde(default)]
    pub interface_metrics: InterfaceMetrics,
    #[serde(default)]
    pub allowed_upstream_device_classes: AllowedDeviceClasses,
    #[serde(default)]
    pub allowed_bridge_upstream_device_classes: AllowedDeviceClasses,
    // TODO(https://fxbug.dev/92096): default to false.
    #[serde(default = "dhcpv6_enabled_default")]
    pub enable_dhcpv6: bool,
    #[serde(default)]
    pub forwarded_device_classes: ForwardedDeviceClasses,
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

#[derive(Clone, Debug)]
struct InterfaceConfig {
    name: String,
    metric: u32,
}

// We use Compare-And-Swap (CAS) protocol to update filter rules. $get_rules returns the current
// generation number. $update_rules will send it with new rules to make sure we are updating the
// intended generation. If the generation number doesn't match, $update_rules will return a
// GenerationMismatch error, then we have to restart from $get_rules.

const FILTER_CAS_RETRY_MAX: i32 = 3;
const FILTER_CAS_RETRY_INTERVAL_MILLIS: i64 = 500;

macro_rules! cas_filter_rules {
    ($filter:expr, $get_rules:ident, $update_rules:ident, $rules:expr, $error_type:ident) => {
        for retry in 0..FILTER_CAS_RETRY_MAX {
            let generation = match $filter.$get_rules().await? {
                Ok((_rules, generation)) => generation,
                Err(e) => {
                    return Err(anyhow::anyhow!("{} failed: {:?}", stringify!($get_rules), e));
                }
            };
            match $filter.$update_rules(&mut $rules.iter_mut(), generation).await.with_context(
                || format!("error getting response from {}", stringify!($update_rules)),
            )? {
                Ok(()) => {
                    break;
                }
                Err(fnet_filter::$error_type::GenerationMismatch)
                    if retry < FILTER_CAS_RETRY_MAX - 1 =>
                {
                    fuchsia_async::Timer::new(
                        FILTER_CAS_RETRY_INTERVAL_MILLIS.millis().after_now(),
                    )
                    .await;
                }
                Err(e) => {
                    let () = Err(anyhow::anyhow!("{} failed: {:?}", stringify!($update_rules), e))?;
                }
            }
        }
    };
}

// This is a placeholder macro while some update operations are not supported.
macro_rules! no_update_filter_rules {
    ($filter:expr, $get_rules:ident, $update_rules:ident, $rules:expr, $error_type:ident) => {
        let generation = match $filter.$get_rules().await? {
            Ok((_rules, generation)) => generation,
            Err(e) => {
                return Err(anyhow::anyhow!("{} failed: {:?}", stringify!($get_rules), e));
            }
        };
        match $filter
            .$update_rules(&mut $rules.iter_mut(), generation)
            .await
            .with_context(|| format!("error getting response from {}", stringify!($update_rules)))?
        {
            Ok(()) => {}
            Err(fnet_filter::$error_type::NotSupported) => {
                error!("{} not supported", stringify!($update_rules));
            }
        }
    };
}

fn should_enable_filter(
    filter_enabled_interface_types: &HashSet<InterfaceType>,
    info: &DeviceInfo,
) -> bool {
    filter_enabled_interface_types.contains(&info.interface_type())
}

#[derive(Debug)]
struct InterfaceState {
    // Hold on to control to enforce interface ownership, even if unused.
    control: fidl_fuchsia_net_interfaces_ext::admin::Control,
    config: InterfaceConfigState,
}

/// State for an interface.
#[derive(Debug)]
enum InterfaceConfigState {
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
    fn new_host(control: fidl_fuchsia_net_interfaces_ext::admin::Control) -> Self {
        Self {
            control,
            config: InterfaceConfigState::Host(HostInterfaceState { dhcpv6_client_addr: None }),
        }
    }

    fn new_wlan_ap(control: fidl_fuchsia_net_interfaces_ext::admin::Control) -> Self {
        Self { control, config: InterfaceConfigState::WlanAp(WlanApInterfaceState {}) }
    }

    fn is_wlan_ap(&self) -> bool {
        let Self { control: _, config } = self;
        match config {
            InterfaceConfigState::Host(_) => false,
            InterfaceConfigState::WlanAp(_) => true,
        }
    }

    /// Handles the interface being discovered.
    async fn on_discovery(
        &mut self,
        properties: &fnet_interfaces_ext::Properties,
        dhcpv6_client_provider: Option<&fnet_dhcpv6::ClientProviderProxy>,
        watchers: &mut DnsServerWatchers<'_>,
    ) -> Result<(), errors::Error> {
        let Self { control: _, config } = self;
        let fnet_interfaces_ext::Properties { online, .. } = properties;
        match config {
            InterfaceConfigState::Host(HostInterfaceState { dhcpv6_client_addr }) => {
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
            InterfaceConfigState::WlanAp(WlanApInterfaceState {}) => {}
        }

        Ok(())
    }
}

/// Network Configuration state.
pub struct NetCfg<'a> {
    stack: fnet_stack::StackProxy,
    netstack: fnetstack::NetstackProxy,
    lookup_admin: fnet_name::LookupAdminProxy,
    filter: fnet_filter::FilterProxy,
    interface_state: fnet_interfaces::StateProxy,
    installer: fidl_fuchsia_net_interfaces_admin::InstallerProxy,
    // TODO(https://fxbug.dev/74532): We won't need to reach out to debug once
    // we don't have Ethernet interfaces anymore.
    debug: fidl_fuchsia_net_debug::InterfacesProxy,
    dhcp_server: Option<fnet_dhcp::Server_Proxy>,
    dhcpv6_client_provider: Option<fnet_dhcpv6::ClientProviderProxy>,

    allow_virtual_devices: bool,

    persisted_interface_config: interface::FileBackedConfig<'a>,

    filter_enabled_interface_types: HashSet<InterfaceType>,

    // TODO(https://fxbug.dev/67407): These hashmaps are all indexed by
    // interface ID and store per-interface state, and should be merged.
    interface_states: HashMap<u64, InterfaceState>,
    interface_properties: HashMap<u64, fnet_interfaces_ext::Properties>,
    interface_metrics: InterfaceMetrics,

    dns_servers: DnsServers,

    forwarded_device_classes: ForwardedDeviceClasses,
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
                if address.is_unicast_link_local() {
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
    async fn new(
        allow_virtual_devices: bool,
        filter_enabled_interface_types: HashSet<InterfaceType>,
        interface_metrics: InterfaceMetrics,
        enable_dhcpv6: bool,
        forwarded_device_classes: ForwardedDeviceClasses,
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
        let dhcpv6_client_provider = if enable_dhcpv6 {
            let dhcpv6_client_provider = svc_connect::<fnet_dhcpv6::ClientProviderMarker>(&svc_dir)
                .await
                .context("could not connect to DHCPv6 client provider")?;
            Some(dhcpv6_client_provider)
        } else {
            None
        };
        let installer = svc_connect::<fnet_interfaces_admin::InstallerMarker>(&svc_dir)
            .await
            .context("could not connect to installer")?;
        let debug = svc_connect::<fnet_debug::InterfacesMarker>(&svc_dir)
            .await
            .context("could not connect to debug")?;
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
            installer,
            debug,
            dhcpv6_client_provider,
            allow_virtual_devices,
            persisted_interface_config,
            filter_enabled_interface_types,
            interface_properties: Default::default(),
            interface_states: Default::default(),
            interface_metrics,
            dns_servers: Default::default(),
            forwarded_device_classes,
        })
    }

    /// Updates the network filter configurations.
    async fn update_filters(&mut self, config: FilterConfig) -> Result<(), anyhow::Error> {
        let FilterConfig { rules, nat_rules, rdr_rules } = config;

        if !rules.is_empty() {
            let mut rules = netfilter::parser::parse_str_to_rules(&rules.join(""))
                .context("error parsing filter rules")?;
            cas_filter_rules!(self.filter, get_rules, update_rules, rules, FilterUpdateRulesError);
        }

        if !nat_rules.is_empty() {
            let mut nat_rules = netfilter::parser::parse_str_to_nat_rules(&nat_rules.join(""))
                .context("error parsing NAT rules")?;
            cas_filter_rules!(
                self.filter,
                get_nat_rules,
                update_nat_rules,
                nat_rules,
                FilterUpdateNatRulesError
            );
        }

        if !rdr_rules.is_empty() {
            let mut rdr_rules = netfilter::parser::parse_str_to_rdr_rules(&rdr_rules.join(""))
                .context("error parsing RDR rules")?;
            // TODO(https://fxbug.dev/68279): Change this to cas_filter_rules once update is supported.
            no_update_filter_rules!(
                self.filter,
                get_rdr_rules,
                update_rdr_rules,
                rdr_rules,
                FilterUpdateRdrRulesError
            );
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
                let InterfaceState { control: _, config } = self
                    .interface_states
                    .get_mut(&interface_id)
                    .ok_or(anyhow::anyhow!("no interface state found for id={}", interface_id))?;

                match config {
                    InterfaceConfigState::Host(HostInterfaceState { dhcpv6_client_addr }) => {
                        let _: fnet::Ipv6SocketAddress =
                            dhcpv6_client_addr.take().ok_or(anyhow::anyhow!(
                                "DHCPv6 was not being performed on host interface with id={}",
                                interface_id
                            ))?;
                    }
                    InterfaceConfigState::WlanAp(WlanApInterfaceState {}) => {
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
            .or_else(errors::Error::accept_non_fatal)?;

        // The watcher stream may have already been removed if it was exhausted so we don't
        // care what the return value is. At the end of this function, it is guaranteed that
        // `dns_watchers` will not have a stream for `source` anyways.
        let _: Option<Pin<Box<futures::stream::BoxStream<'_, _>>>> = dns_watchers.remove(&source);

        Ok(())
    }

    /// Run the network configuration eventloop.
    ///
    /// The device directory will be monitored for device events and the netstack will be
    /// configured with a new interface on new device discovery.
    async fn run(
        &mut self,
        mut virtualization_handler: impl virtualization::Handler,
    ) -> Result<(), anyhow::Error> {
        let ethdev_stream = self
            .create_device_stream::<devices::EthernetDevice>()
            .await
            .context("create ethernet device stream")?
            .fuse();
        futures::pin_mut!(ethdev_stream);

        let netdev_stream = self
            .create_device_stream::<devices::NetworkDevice>()
            .await
            .context("create netdevice stream")?
            .fuse();
        futures::pin_mut!(netdev_stream);

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

        // Serve fuchsia.net.virtualization/Control.
        let mut fs = ServiceFs::new_local();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(virtualization::Event::ControlRequestStream);
        let _: &mut ServiceFs<_> =
            fs.take_and_serve_directory_handle().context("take and serve directory handle")?;

        // Maintain a queue of virtualization events to be dispatched to the virtualization handler.
        let mut virtualization_events = futures::stream::SelectAll::new();
        virtualization_events.push(fs.boxed_local());

        // Lifecycle handle takes no args, must be set to zero.
        // See zircon/processargs.h.
        const LIFECYCLE_HANDLE_ARG: u16 = 0;
        let lifecycle = fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
            fuchsia_runtime::HandleType::Lifecycle,
            LIFECYCLE_HANDLE_ARG,
        ))
        .ok_or_else(|| anyhow::anyhow!("lifecycle handle not present"))?;
        let lifecycle = fuchsia_async::Channel::from_channel(lifecycle.into())
            .context("lifecycle async channel")?;
        let mut lifecycle = fidl_fuchsia_process_lifecycle::LifecycleRequestStream::from_channel(
            fidl::AsyncChannel::from(lifecycle),
        );

        debug!("starting eventloop...");

        enum Event {
            EthernetDeviceResult(Result<Option<devices::EthernetInstance>, anyhow::Error>),
            NetworkDeviceResult(Result<Option<devices::NetworkDeviceInstance>, anyhow::Error>),
            InterfaceWatcherResult(Result<Option<fidl_fuchsia_net_interfaces::Event>, fidl::Error>),
            DnsWatcherResult(
                Option<(
                    dns_server_watcher::DnsServersUpdateSource,
                    Result<Vec<fnet_name::DnsServer_>, anyhow::Error>,
                )>,
            ),
            VirtualizationEvent(virtualization::Event),
            LifecycleRequest(
                Result<Option<fidl_fuchsia_process_lifecycle::LifecycleRequest>, fidl::Error>,
            ),
        }
        loop {
            let event = futures::select! {
                ethdev_res = ethdev_stream.try_next() => {
                    Event::EthernetDeviceResult(ethdev_res)
                }
                netdev_res = netdev_stream.try_next() => {
                    Event::NetworkDeviceResult(netdev_res)
                }
                if_watcher_res = if_watcher_event_stream.try_next() => {
                    Event::InterfaceWatcherResult(if_watcher_res)
                }
                dns_watchers_res = dns_watchers.next() => {
                    Event::DnsWatcherResult(dns_watchers_res)
                }
                event = virtualization_events.select_next_some() => {
                    Event::VirtualizationEvent(event)
                }
                req = lifecycle.try_next() => {
                    Event::LifecycleRequest(req)
                }
                complete => return Err(anyhow::anyhow!("eventloop ended unexpectedly")),
            };
            match event {
                Event::EthernetDeviceResult(ethdev_res) => {
                    let instance =
                        ethdev_res.context("error retrieving ethernet instance")?.ok_or_else(
                            || anyhow::anyhow!("ethdev instance watcher stream ended unexpectedly"),
                        )?;
                    self.handle_device_instance::<devices::EthernetDevice>(instance)
                        .await
                        .context("handle ethdev instance")?
                }
                Event::NetworkDeviceResult(netdev_res) => {
                    let instance =
                        netdev_res.context("error retrieving netdev instance")?.ok_or_else(
                            || anyhow::anyhow!("netdev instance watcher stream ended unexpectedly"),
                        )?;
                    self.handle_device_instance::<devices::NetworkDevice>(instance)
                        .await
                        .context("handle netdev instance")?
                }
                Event::InterfaceWatcherResult(if_watcher_res) => {
                    let event = if_watcher_res
                        .context("error watching interface property changes")?
                        .ok_or(anyhow::anyhow!(
                            "interface watcher event stream ended unexpectedly"
                        ))?;
                    trace!("got interfaces watcher event = {:?}", event);

                    self.handle_interface_watcher_event(
                        event,
                        dns_watchers.get_mut(),
                        &mut virtualization_handler,
                    )
                    .await
                    .context("handle interface watcher event")?
                }
                Event::DnsWatcherResult(dns_watchers_res) => {
                    let (source, res) = dns_watchers_res
                        .ok_or(anyhow::anyhow!("dns watchers stream should never be exhausted"))?;
                    let servers = match res {
                        Ok(s) => s,
                        Err(e) => {
                            // TODO(fxbug.dev/57484): Restart the DNS server watcher.
                            error!(
                                "non-fatal error getting next event from DNS server watcher stream
                                with source = {:?}: {:?}",
                                source, e
                            );
                            let () = self
                                .handle_dns_server_watcher_done(source, dns_watchers.get_mut())
                                .await
                                .with_context(|| {
                                    format!(
                                        "error handling completion of DNS server watcher for \
                                        {:?}",
                                        source
                                    )
                                })?;
                            continue;
                        }
                    };

                    self.update_dns_servers(source, servers)
                        .await
                        .with_context(|| {
                            format!("error handling DNS servers update from {:?}", source)
                        })
                        .or_else(errors::Error::accept_non_fatal)?
                }
                Event::VirtualizationEvent(event) => virtualization_handler
                    .handle_event(event, &mut virtualization_events)
                    .await
                    .context("handle virtualization event")
                    .or_else(errors::Error::accept_non_fatal)?,
                Event::LifecycleRequest(req) => {
                    let req = req.context("lifecycle request")?.ok_or_else(|| {
                        anyhow::anyhow!("LifecycleRequestStream ended unexpectedly")
                    })?;
                    match req {
                        fidl_fuchsia_process_lifecycle::LifecycleRequest::Stop {
                            control_handle,
                        } => {
                            info!("received shutdown request");
                            // Shutdown request is acknowledged by the lifecycle
                            // channel shutting down. Intentionally leak the
                            // channel so it'll only be closed on process
                            // termination, allowing clean process termination
                            // to always be observed.

                            // Must drop the control_handle to unwrap the
                            // lifecycle channel.
                            std::mem::drop(control_handle);
                            let (inner, _terminated): (_, bool) = lifecycle.into_inner();
                            let inner = std::sync::Arc::try_unwrap(inner).map_err(
                                |_: std::sync::Arc<_>| {
                                    anyhow::anyhow!("failed to retrieve lifecycle channel")
                                },
                            )?;
                            let inner: zx::Channel = inner.into_channel().into_zx_channel();
                            std::mem::forget(inner);

                            return Ok(());
                        }
                    }
                }
            }
        }
    }

    /// Handles an interface watcher event (existing, added, changed, or removed).
    async fn handle_interface_watcher_event(
        &mut self,
        event: fnet_interfaces::Event,
        watchers: &mut DnsServerWatchers<'_>,
        virtualization_handler: &mut impl virtualization::Handler,
    ) -> Result<(), anyhow::Error> {
        let Self {
            interface_properties,
            dns_servers,
            interface_states,
            lookup_admin,
            dhcp_server,
            dhcpv6_client_provider,
            ..
        } = self;
        let update_result = interface_properties
            .update(event)
            .context("failed to update interface properties with watcher event")?;
        Self::handle_interface_update_result(
            &update_result,
            watchers,
            dns_servers,
            interface_states,
            lookup_admin,
            dhcp_server,
            dhcpv6_client_provider,
        )
        .await
        .context("handle interface update")
        .or_else(errors::Error::accept_non_fatal)?;
        virtualization_handler
            .handle_interface_update_result(&update_result)
            .await
            .context("handle interface update for virtualization")
            .or_else(errors::Error::accept_non_fatal)
    }

    // This method takes mutable references to several fields of `NetCfg` separately as parameters,
    // rather than `&mut self` directly, because `update_result` already holds a reference into
    // `self.interface_properties`.
    async fn handle_interface_update_result(
        update_result: &fnet_interfaces_ext::UpdateResult<'_>,
        watchers: &mut DnsServerWatchers<'_>,
        dns_servers: &mut DnsServers,
        interface_states: &mut HashMap<u64, InterfaceState>,
        lookup_admin: &fnet_name::LookupAdminProxy,
        dhcp_server: &Option<fnet_dhcp::Server_Proxy>,
        dhcpv6_client_provider: &Option<fnet_dhcpv6::ClientProviderProxy>,
    ) -> Result<(), errors::Error> {
        match update_result {
            fnet_interfaces_ext::UpdateResult::Added(properties) => {
                match interface_states.get_mut(&properties.id) {
                    Some(state) => state
                        .on_discovery(properties, dhcpv6_client_provider.as_ref(), watchers)
                        .await
                        .context("failed to handle interface added event"),
                    // An interface netcfg won't be configuring was added, do nothing.
                    None => Ok(()),
                }
            }
            fnet_interfaces_ext::UpdateResult::Existing(properties) => {
                match interface_states.get_mut(&properties.id) {
                    Some(state) => state
                        .on_discovery(properties, dhcpv6_client_provider.as_ref(), watchers)
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
                match interface_states.get_mut(&id) {
                    // An interface netcfg is not configuring was changed, do nothing.
                    None => return Ok(()),
                    Some(InterfaceState {
                        control: _,
                        config:
                            InterfaceConfigState::Host(HostInterfaceState { dhcpv6_client_addr }),
                    }) => {
                        let dhcpv6_client_provider =
                            if let Some(dhcpv6_client_provider) = dhcpv6_client_provider {
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

                            return dhcpv6::stop_client(&lookup_admin, dns_servers, *id, watchers)
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
                                    dhcpv6::stop_client(&lookup_admin, dns_servers, *id, watchers)
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
                                &dhcpv6_client_provider,
                                watchers,
                            )?;
                        }
                        Ok(())
                    }
                    Some(InterfaceState {
                        control: _,
                        config: InterfaceConfigState::WlanAp(WlanApInterfaceState {}),
                    }) => {
                        // TODO(fxbug.dev/55879): Stop the DHCP server when the address it is
                        // listening on is removed.
                        let dhcp_server = if let Some(dhcp_server) = dhcp_server {
                            dhcp_server
                        } else {
                            return Ok(());
                        };

                        if previous_online
                            .map_or(true, |previous_online| previous_online == *online)
                        {
                            return Ok(());
                        }

                        if *online {
                            info!(
                                "WLAN AP interface {} (id={}) came up so starting DHCP server",
                                name, id
                            );
                            dhcpv4::start_server(&dhcp_server)
                                .await
                                .context("error starting DHCP server")
                        } else {
                            info!(
                                "WLAN AP interface {} (id={}) went down so stopping DHCP server",
                                name, id
                            );
                            dhcpv4::stop_server(&dhcp_server)
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
                match interface_states.remove(&id) {
                    // An interface netcfg was not responsible for configuring was removed, do
                    // nothing.
                    None => Ok(()),
                    Some(InterfaceState { control: _, config }) => {
                        match config {
                            InterfaceConfigState::Host(HostInterfaceState {
                                mut dhcpv6_client_addr,
                            }) => {
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

                                dhcpv6::stop_client(
                                    &lookup_admin,
                                    dns_servers,
                                    *id,
                                    watchers,
                                )
                                .await
                                .with_context(|| {
                                    format!(
                                        "error stopping DHCPv6 client on removed interface {} (id={})",
                                        name, id
                                    )
                                })
                            }
                            InterfaceConfigState::WlanAp(WlanApInterfaceState {}) => {
                                if let Some(dhcp_server) = dhcp_server {
                                    // The DHCP server should only run on the WLAN AP interface, so stop it
                                    // since the AP interface is removed.
                                    info!(
                                        "WLAN AP interface {} (id={}) is removed, stopping DHCP server",
                                        name, id
                                    );
                                    dhcpv4::stop_server(&dhcp_server)
                                        .await
                                        .context("error stopping DHCP server")
                                } else {
                                    Ok(())
                                }
                            }
                        }
                        .context("failed to handle interface removed event")
                    }
                }
            }
            fnet_interfaces_ext::UpdateResult::NoChange => Ok(()),
        }
    }

    /// Handle an event from `D`'s device directory.
    async fn create_device_stream<D: devices::Device>(
        &self,
    ) -> Result<impl futures::Stream<Item = Result<D::DeviceInstance, anyhow::Error>>, anyhow::Error>
    {
        let installer = self.installer.clone();
        let stream_of_streams = fvfs_watcher::Watcher::new(
            fuchsia_fs::directory::open_in_namespace(D::PATH, OpenFlags::RIGHT_READABLE)
                .with_context(|| format!("error opening {} directory", D::NAME))?,
        )
        .await
        .with_context(|| format!("creating watcher for {}", D::PATH))?
        .err_into()
        .try_filter_map(move |fvfs_watcher::WatchMessage { event, filename }| {
            let installer = installer.clone();
            async move {
                trace!("got {:?} {} event for {}", event, D::NAME, filename.display());

                if filename == path::PathBuf::from(THIS_DIRECTORY) {
                    debug!("skipping {} w/ filename = {}", D::NAME, filename.display());
                    return Ok(None);
                }

                match event {
                    fvfs_watcher::WatchEvent::ADD_FILE | fvfs_watcher::WatchEvent::EXISTING => {
                        let filepath = path::Path::new(D::PATH).join(filename);
                        info!("found new {} at {:?}", D::NAME, filepath);
                        match D::get_instance_stream(&installer, &filepath)
                            .await
                            .context("create instance stream")
                        {
                            Ok(stream) => Ok(Some(stream.filter_map(move |r| {
                                futures::future::ready(match r {
                                    Ok(instance) => Some(Ok(instance)),
                                    Err(errors::Error::NonFatal(nonfatal)) => {
                                        error!(
                                        "non-fatal error operating device stream {} for {:?}: {:?}",
                                        D::NAME,
                                        filepath,
                                        nonfatal
                                    );
                                        None
                                    }
                                    Err(errors::Error::Fatal(fatal)) => Some(Err(fatal)),
                                })
                            }))),
                            Err(errors::Error::NonFatal(nonfatal)) => {
                                error!(
                                    "non-fatal error fetching device stream {} for {:?}: {:?}",
                                    D::NAME,
                                    filepath,
                                    nonfatal
                                );
                                Ok(None)
                            }
                            Err(errors::Error::Fatal(fatal)) => Err(fatal),
                        }
                    }
                    fvfs_watcher::WatchEvent::IDLE | fvfs_watcher::WatchEvent::REMOVE_FILE => {
                        Ok(None)
                    }
                    event => Err(anyhow::anyhow!(
                        "unrecognized event {:?} for {} filename {}",
                        event,
                        D::NAME,
                        filename.display()
                    )),
                }
            }
        })
        .fuse()
        .try_flatten_unordered();
        Ok(stream_of_streams)
    }

    async fn handle_device_instance<D: devices::Device>(
        &mut self,
        instance: D::DeviceInstance,
    ) -> Result<(), anyhow::Error> {
        let mut i = 0;
        loop {
            // TODO(fxbug.dev/56559): The same interface may flap so instead of using a
            // temporary name, try to determine if the interface flapped and wait
            // for the teardown to complete.
            match self.add_new_device::<D>(&instance, i == 0 /* stable_name */).await {
                Ok(()) => {
                    break Ok(());
                }
                Err(devices::AddDeviceError::AlreadyExists) => {
                    i += 1;
                    if i == MAX_ADD_DEVICE_ATTEMPTS {
                        error!(
                            "failed to add {} {:?} after {} attempts due to already exists error",
                            D::NAME,
                            instance,
                            MAX_ADD_DEVICE_ATTEMPTS,
                        );

                        break Ok(());
                    }

                    warn!(
                        "got already exists error on attempt {} of adding {} {:?}, trying again...",
                        i,
                        D::NAME,
                        instance,
                    );
                }
                Err(devices::AddDeviceError::Other(errors::Error::NonFatal(e))) => {
                    error!("non-fatal error adding {} {:?}: {:?}", D::NAME, instance, e);

                    break Ok(());
                }
                Err(devices::AddDeviceError::Other(errors::Error::Fatal(e))) => {
                    break Err(e.context(format!("error adding new {} {:?}", D::NAME, instance)));
                }
            }
        }
    }

    /// Add a device at `filepath` to the netstack.
    async fn add_new_device<D: devices::Device>(
        &mut self,
        device_instance: &D::DeviceInstance,
        stable_name: bool,
    ) -> Result<(), devices::AddDeviceError> {
        let info = D::get_device_info(&device_instance)
            .await
            .context("error getting device info and MAC")?;

        let DeviceInfo { mac, is_synthetic, device_class: _, topological_path } = &info;

        if !self.allow_virtual_devices && *is_synthetic {
            warn!("{} {:?} is not a physical device, skipping", D::NAME, device_instance);
            return Ok(());
        }

        let mac = mac.ok_or_else(|| {
            warn!("devices without mac address not supported yet");
            devices::AddDeviceError::Other(errors::Error::NonFatal(anyhow::anyhow!(
                "device without mac not supported"
            )))
        })?;

        let interface_type = info.interface_type();
        let metric = match interface_type {
            InterfaceType::Wlan => self.interface_metrics.wlan_metric,
            InterfaceType::Ethernet => self.interface_metrics.eth_metric,
        }
        .into();
        let interface_name = if stable_name {
            match self.persisted_interface_config.generate_stable_name(
                &topological_path, /* TODO(tamird): we can probably do
                                    * better with std::borrow::Cow. */
                mac,
                interface_type,
            ) {
                Ok(name) => name.to_string(),
                Err(interface::NameGenerationError::FileUpdateError { name, err }) => {
                    warn!("failed to update interface (topo path = {}, mac = {}, interface_type = {:?}) with new name = {}: {}", topological_path, mac, interface_type, name, err);
                    name.to_string()
                }
                Err(interface::NameGenerationError::GenerationError(e)) => {
                    return Err(devices::AddDeviceError::Other(errors::Error::Fatal(
                        e.context("error getting stable name"),
                    )))
                }
            }
        } else {
            self.persisted_interface_config.generate_temporary_name(interface_type)
        };

        info!("adding {} {:?} to stack with name = {}", D::NAME, device_instance, interface_name);

        let (interface_id, control) = D::add_to_stack(
            self,
            InterfaceConfig { name: interface_name.clone(), metric },
            device_instance,
        )
        .await
        .context("error adding to stack")?;

        self.configure_eth_interface(interface_id, control, interface_name, &info)
            .await
            .context("error configuring ethernet interface")
            .map_err(devices::AddDeviceError::Other)
    }

    /// Configure an ethernet interface.
    ///
    /// If the device is a WLAN AP, it will be configured as a WLAN AP (see
    /// `configure_wlan_ap_and_dhcp_server` for more details). Otherwise, it
    /// will be configured as a host (see `configure_host` for more details).
    async fn configure_eth_interface(
        &mut self,
        interface_id: u64,
        control: fidl_fuchsia_net_interfaces_ext::admin::Control,
        interface_name: String,
        info: &DeviceInfo,
    ) -> Result<(), errors::Error> {
        let class: DeviceClass = info.device_class.into();
        let ForwardedDeviceClasses { ipv4, ipv6 } = &self.forwarded_device_classes;
        let config: fnet_interfaces_admin::Configuration = control
            .set_configuration(fnet_interfaces_admin::Configuration {
                ipv6: Some(fnet_interfaces_admin::Ipv6Configuration {
                    forwarding: Some(ipv6.contains(&class)),
                    ..fnet_interfaces_admin::Ipv6Configuration::EMPTY
                }),
                ipv4: Some(fnet_interfaces_admin::Ipv4Configuration {
                    forwarding: Some(ipv4.contains(&class)),
                    ..fnet_interfaces_admin::Ipv4Configuration::EMPTY
                }),
                ..fnet_interfaces_admin::Configuration::EMPTY
            })
            .await
            .map_err(map_control_error("setting configuration"))
            .and_then(|res| {
                res.map_err(|e: fnet_interfaces_admin::ControlSetConfigurationError| {
                    errors::Error::Fatal(anyhow::anyhow!("{:?}", e))
                })
            })?;
        info!("installed configuration with result {:?}", config);

        if info.is_wlan_ap() {
            if let Some(id) = self.interface_states.iter().find_map(|(id, state)| {
                if state.is_wlan_ap() {
                    Some(id)
                } else {
                    None
                }
            }) {
                return Err(errors::Error::NonFatal(anyhow::anyhow!("multiple WLAN AP interfaces are not supported, have WLAN AP interface with id = {}", id)));
            }
            let InterfaceState { control, config: _ } = match self
                .interface_states
                .entry(interface_id)
            {
                Entry::Occupied(entry) => {
                    return Err(errors::Error::Fatal(anyhow::anyhow!("multiple interfaces with the same ID = {}; attempting to add state for a WLAN AP, existing state = {:?}", entry.key(), entry.get())));
                }
                Entry::Vacant(entry) => entry.insert(InterfaceState::new_wlan_ap(control)),
            };

            info!("discovered WLAN AP (interface ID={})", interface_id);

            if let Some(dhcp_server) = &self.dhcp_server {
                info!("configuring DHCP server for WLAN AP (interface ID={})", interface_id);
                let () = Self::configure_wlan_ap_and_dhcp_server(
                    interface_id,
                    dhcp_server,
                    control,
                    &self.stack,
                    interface_name,
                )
                .await
                .context("error configuring wlan ap and dhcp server")?;
            } else {
                warn!("cannot configure DHCP server for WLAN AP (interface ID={}) since DHCP server service is not available", interface_id);
            }
        } else {
            let InterfaceState { control, config: _ } = match self
                .interface_states
                .entry(interface_id)
            {
                Entry::Occupied(entry) => {
                    return Err(errors::Error::Fatal(anyhow::anyhow!("multiple interfaces with the same ID = {}; attempting to add state for a host, existing state = {:?}", entry.key(), entry.get())));
                }
                Entry::Vacant(entry) => entry.insert(InterfaceState::new_host(control)),
            };

            info!("discovered host interface with id={}, configuring interface", interface_id);

            let () = Self::configure_host(
                &self.filter_enabled_interface_types,
                &self.filter,
                &self.stack,
                interface_id,
                info,
            )
            .await
            .context("error configuring host")?;

            let _did_enable: bool = control
                .enable()
                .await
                .map_err(map_control_error("error sending enable request"))
                .and_then(|res| {
                    // ControlEnableError is an empty *flexible* enum, so we can't match on it, but
                    // the operation is infallible at the time of writing.
                    res.map_err(|e: fidl_fuchsia_net_interfaces_admin::ControlEnableError| {
                        errors::Error::Fatal(anyhow::anyhow!("enable interface: {:?}", e))
                    })
                })?;
        }

        Ok(())
    }

    /// Configure host interface.
    async fn configure_host(
        filter_enabled_interface_types: &HashSet<InterfaceType>,
        filter: &fnet_filter::FilterProxy,
        stack: &fnet_stack::StackProxy,
        interface_id: u64,
        info: &DeviceInfo,
    ) -> Result<(), errors::Error> {
        if should_enable_filter(filter_enabled_interface_types, info) {
            info!("enable filter for nic {}", interface_id);
            let () = filter
                .enable_interface(interface_id)
                .await
                .with_context(|| {
                    format!("error sending enable filter request on nic {}", interface_id)
                })
                .map_err(errors::Error::Fatal)?
                .map_err(|e| {
                    anyhow::anyhow!(
                        "failed to enable filter on nic {} with error = {:?}",
                        interface_id,
                        e
                    )
                })
                .map_err(errors::Error::NonFatal)?;
        } else {
            info!("disable filter for nic {}", interface_id);
            let () = filter
                .disable_interface(interface_id)
                .await
                .with_context(|| {
                    format!("error sending disable filter request on nic {}", interface_id)
                })
                .map_err(errors::Error::Fatal)?
                .map_err(|e| {
                    anyhow::anyhow!(
                        "failed to disable filter on nic {} with error = {:?}",
                        interface_id,
                        e
                    )
                })
                .map_err(errors::Error::NonFatal)?;
        };

        // Enable DHCP.
        let () = stack
            .set_dhcp_client_enabled(interface_id, true)
            .await
            .context("failed to call set dhcp client enabled")
            .map_err(errors::Error::Fatal)?
            .map_err(|e| anyhow!("failed to start dhcp client: {:?}", e))
            .map_err(errors::Error::NonFatal)?;

        Ok(())
    }

    /// Configure the WLAN AP and the DHCP server to serve requests on its network.
    ///
    /// Note, this method will not start the DHCP server, it will only be configured
    /// with the parameters so it is ready to be started when an interface UP event
    /// is received for the WLAN AP.
    async fn configure_wlan_ap_and_dhcp_server(
        interface_id: u64,
        dhcp_server: &fnet_dhcp::Server_Proxy,
        control: &fidl_fuchsia_net_interfaces_ext::admin::Control,
        stack: &fidl_fuchsia_net_stack::StackProxy,
        name: String,
    ) -> Result<(), errors::Error> {
        let (address_state_provider, server_end) = fidl::endpoints::create_proxy::<
            fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
        >()
        .context("address state provider: failed to create fidl endpoints")
        .map_err(errors::Error::Fatal)?;

        // Calculate and set the interface address based on the network address.
        // The interface address should be the first available address.
        let network_addr_as_u32 = u32::from_be_bytes(WLAN_AP_NETWORK_ADDR.addr);
        let interface_addr_as_u32 = network_addr_as_u32 + 1;
        let ipv4 = fnet::Ipv4Address { addr: interface_addr_as_u32.to_be_bytes() };
        let addr = fidl_fuchsia_net::Subnet {
            addr: fnet::IpAddress::Ipv4(ipv4.clone()),
            prefix_len: WLAN_AP_PREFIX_LEN,
        };

        let () = control
            .add_address(
                &mut addr.clone(),
                fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                server_end,
            )
            .map_err(map_control_error("error sending add address request"))?;

        // Allow the address to outlive this scope. At the time of writing its lifetime is
        // identical to the interface's lifetime and no updates to its properties are made. We may
        // wish to retain the handle in the future to allow external address removal (e.g. by a
        // user) to be observed so that an error can be emitted (as such removal would break a
        // critical user journey).
        let () = address_state_provider
            .detach()
            .map_err(Into::into)
            .map_err(map_address_state_provider_error("error sending detach request"))?;

        // Enable the interface to allow DAD to proceed.
        let _did_enable: bool = control
            .enable()
            .await
            .map_err(map_control_error("error sending enable request"))
            .and_then(|res| {
                // ControlEnableError is an empty *flexible* enum, so we can't match on it, but the
                // operation is infallible at the time of writing.
                res.map_err(|e: fidl_fuchsia_net_interfaces_admin::ControlEnableError| {
                    errors::Error::Fatal(anyhow::anyhow!("enable interface: {:?}", e))
                })
            })?;

        let state_stream =
            fidl_fuchsia_net_interfaces_ext::admin::assignment_state_stream(address_state_provider);
        futures::pin_mut!(state_stream);
        let () = fidl_fuchsia_net_interfaces_ext::admin::wait_assignment_state(
            &mut state_stream,
            fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Assigned,
        )
        .await
        .map_err(map_address_state_provider_error("failed to add interface address for WLAN AP"))?;

        let subnet = fidl_fuchsia_net_ext::apply_subnet_mask(addr);
        let () = stack
            .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                subnet,
                device_id: interface_id,
                next_hop: None,
                metric: 0,
            })
            .await
            .context("error sending add route request")
            .map_err(errors::Error::Fatal)?
            .map_err(|e| {
                let severity = match e {
                    fidl_fuchsia_net_stack::Error::InvalidArgs => {
                        // Do not consider this a fatal error because the interface could have been
                        // removed after it was added, but before we reached this point.
                        //
                        // NB: this error is returned by Netstack2 when the interface doesn't
                        // exist. 🤷
                        errors::Error::NonFatal
                    }
                    fidl_fuchsia_net_stack::Error::Internal
                    | fidl_fuchsia_net_stack::Error::NotSupported
                    | fidl_fuchsia_net_stack::Error::BadState
                    | fidl_fuchsia_net_stack::Error::TimeOut
                    | fidl_fuchsia_net_stack::Error::NotFound
                    | fidl_fuchsia_net_stack::Error::AlreadyExists
                    | fidl_fuchsia_net_stack::Error::Io => errors::Error::Fatal,
                };
                severity(anyhow::anyhow!("adding route: {:?}", e))
            })?;

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
        let v = vec![ipv4];
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

        let host_mask = ::dhcpv4::configuration::SubnetMask::new(WLAN_AP_PREFIX_LEN)
            .ok_or_else(|| anyhow!("error creating host mask from prefix length"))
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

pub async fn run<M: Mode>() -> Result<(), anyhow::Error> {
    let opt: Opt = argh::from_env();
    let Opt { allow_virtual_devices, min_severity: LogLevel(min_severity), config_data } = &opt;

    // Use the diagnostics_log library directly rather than e.g. the #[fuchsia::main] macro on
    // the main function, so that we can specify the logging severity level at runtime based on a
    // command line argument.
    diagnostics_log::init!(
        &[],
        diagnostics_log::Interest {
            min_severity: Some(*min_severity),
            ..diagnostics_log::Interest::EMPTY
        }
    );
    info!("starting");
    debug!("starting with options = {:?}", opt);

    let Config {
        dns_config: DnsConfig { servers },
        filter_config,
        filter_enabled_interface_types,
        interface_metrics,
        allowed_upstream_device_classes: AllowedDeviceClasses(allowed_upstream_device_classes),
        allowed_bridge_upstream_device_classes:
            AllowedDeviceClasses(allowed_bridge_upstream_device_classes),
        enable_dhcpv6,
        forwarded_device_classes,
    } = Config::load(config_data)?;

    let mut netcfg = NetCfg::new(
        *allow_virtual_devices,
        filter_enabled_interface_types,
        interface_metrics,
        enable_dhcpv6,
        forwarded_device_classes,
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
        .or_else(errors::Error::accept_non_fatal)?;

    M::run(netcfg, allowed_upstream_device_classes, allowed_bridge_upstream_device_classes)
        .map_err(|e| {
            let err_str = format!("fatal error running main: {:?}", e);
            error!("{}", err_str);
            anyhow!(err_str)
        })
        .await
}

/// Allows callers of `netcfg::run` to configure at compile time which features
/// should be enabled.
///
/// This trait may be expanded to support combinations of features that can be
/// assembled together for specific netcfg builds.
#[async_trait(?Send)]
pub trait Mode {
    async fn run(
        netcfg: NetCfg<'_>,
        allowed_upstream_device_classes: HashSet<DeviceClass>,
        allowed_bridge_upstream_device_classes: HashSet<DeviceClass>,
    ) -> Result<(), anyhow::Error>;
}

/// In this configuration, netcfg acts as the policy manager for netstack,
/// watching for device events and configuring the netstack with new interfaces
/// as needed on new device discovery. It does not implement any FIDL protocols.
pub enum BasicMode {}

#[async_trait(?Send)]
impl Mode for BasicMode {
    async fn run(
        mut netcfg: NetCfg<'_>,
        _allowed_upstream_device_classes: HashSet<DeviceClass>,
        _allowed_bridge_upstream_device_classes: HashSet<DeviceClass>,
    ) -> Result<(), anyhow::Error> {
        netcfg.run(virtualization::Stub).await.context("event loop")
    }
}

/// In this configuration, netcfg implements the base functionality included in
/// `BasicMode`, and also serves the `fuchsia.net.virtualization/Control`
/// protocol, allowing clients to create virtual networks.
pub enum VirtualizationEnabled {}

#[async_trait(?Send)]
impl Mode for VirtualizationEnabled {
    async fn run(
        mut netcfg: NetCfg<'_>,
        allowed_upstream_device_classes: HashSet<DeviceClass>,
        allowed_bridge_upstream_device_classes: HashSet<DeviceClass>,
    ) -> Result<(), anyhow::Error> {
        let handler = virtualization::Virtualization::new(
            allowed_upstream_device_classes,
            allowed_bridge_upstream_device_classes,
            virtualization::BridgeHandlerImpl::new(
                netcfg.stack.clone(),
                netcfg.netstack.clone(),
                netcfg.debug.clone(),
            ),
            netcfg.installer.clone(),
        );
        netcfg.run(handler).await.context("event loop")
    }
}

fn map_control_error(
    ctx: &'static str,
) -> impl FnOnce(
    fnet_interfaces_ext::admin::TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>,
) -> errors::Error {
    move |e| {
        let severity = match &e {
            fidl_fuchsia_net_interfaces_ext::admin::TerminalError::Fidl(e) => {
                if e.is_closed() {
                    // Control handle can close when interface is
                    // removed; not a fatal error.
                    errors::Error::NonFatal
                } else {
                    errors::Error::Fatal
                }
            }
            fidl_fuchsia_net_interfaces_ext::admin::TerminalError::Terminal(e) => match e {
                fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::DuplicateName
                | fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::PortAlreadyBound
                | fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::BadPort => {
                    errors::Error::Fatal
                }
                fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::PortClosed
                | fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::User
                | _ => errors::Error::NonFatal,
            },
        };
        severity(anyhow::Error::new(e).context(ctx))
    }
}

fn map_address_state_provider_error(
    ctx: &'static str,
) -> impl FnOnce(fnet_interfaces_ext::admin::AddressStateProviderError) -> errors::Error {
    move |e| {
        let severity = match &e {
            fnet_interfaces_ext::admin::AddressStateProviderError::Fidl(e) => {
                if e.is_closed() {
                    // TODO(https://fxbug.dev/89290): Reconsider whether this
                    // should be a fatal error, as it can be caused by a
                    // netstack bug.
                    errors::Error::NonFatal
                } else {
                    errors::Error::Fatal
                }
            }
            fnet_interfaces_ext::admin::AddressStateProviderError::ChannelClosed => {
                // TODO(https://fxbug.dev/89290): Reconsider whether this should
                // be a fatal error, as it can be caused by a netstack bug.
                errors::Error::NonFatal
            }
            fnet_interfaces_ext::admin::AddressStateProviderError::AddressRemoved(e) => match e {
                fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::Invalid => {
                    errors::Error::Fatal
                }
                fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::AlreadyAssigned
                | fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::DadFailed
                | fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::InterfaceRemoved
                | fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::UserRemoved => {
                    errors::Error::NonFatal
                }
            },
        };
        severity(anyhow::Error::new(e).context(ctx))
    }
}

#[cfg(test)]
mod tests {
    use futures::future::{self, FutureExt as _, TryFutureExt as _};
    use futures::stream::TryStreamExt as _;
    use net_declare::{fidl_ip, fidl_ip_v6};
    use test_case::test_case;

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
        let (installer, _installer_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
                .context("error creating installer endpoints")?;
        let (debug, _debug_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_debug::InterfacesMarker>()
                .context("error creating installer endpoints")?;
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
                installer,
                debug,
                dhcp_server: Some(dhcp_server),
                dhcpv6_client_provider: Some(dhcpv6_client_provider),
                persisted_interface_config,
                allow_virtual_devices: false,
                filter_enabled_interface_types: Default::default(),
                interface_properties: Default::default(),
                interface_states: Default::default(),
                interface_metrics: Default::default(),
                dns_servers: Default::default(),
                forwarded_device_classes: Default::default(),
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
    ) -> Result<(), anyhow::Error> {
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
        netcfg.handle_interface_watcher_event(event, dns_watchers, &mut virtualization::Stub).await
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
        let (control, _control_server_end) =
            fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                .expect("create endpoints");
        assert_matches::assert_matches!(
            netcfg.interface_states.insert(INTERFACE_ID, InterfaceState::new_host(control)),
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
                &mut virtualization::Stub,
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
        .context("error handling interface changed event with sockaddr1 removed")?;

        // Another update without any link-local IPv6 addresses should do nothing
        // since the DHCPv6 client was already stopped.
        let () =
            handle_interface_changed_event(&mut netcfg, &mut dns_watchers, None, Some(Vec::new()))
                .await
                .context("error handling interface changed event with sockaddr1 removed")?;

        // Update interface with a link-local address to create a new DHCPv6 client.
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            None,
            Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR1))),
        )
        .await
        .context("error handling interface changed event with sockaddr1 removed")?;
        let _: fnet_dhcpv6::ClientRequestStream =
            check_new_client(&mut dhcpv6_client_provider, LINK_LOCAL_SOCKADDR1, &mut dns_watchers)
                .await
                .context("error checking for new client with sockaddr1")?;

        // Update offline status to down to stop DHCPv6 client.
        let () = handle_interface_changed_event(&mut netcfg, &mut dns_watchers, Some(false), None)
            .await
            .context("error handling interface changed event with sockaddr1 removed")?;

        // Update interface with new addresses but leave offline status as down.
        handle_interface_changed_event(&mut netcfg, &mut dns_watchers, None, Some(ipv6addrs(None)))
            .await
            .context("error handling interface changed event with sockaddr1 removed")
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
        let (control, _control_server_end) =
            fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                .expect("create endpoints");
        assert_matches::assert_matches!(
            netcfg.interface_states.insert(INTERFACE_ID, InterfaceState::new_host(control)),
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
                &mut virtualization::Stub,
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
                                source_interface: Some(INTERFACE_ID),
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
            .map(|r| r.context("error handling interface changed event with sockaddr1 removed")),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to empty addresses")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        assert_matches::assert_matches!(client_server.try_next().await, Ok(None));

        // Should start a new DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            None,
            Some(ipv6addrs(Some(LINK_LOCAL_SOCKADDR2))),
        )
        .await
        .context("error handling netstack event with sockaddr2 added")?;
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
            .map(|r| r.context("error handling interface changed event with interface down")),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to interface down")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        assert_matches::assert_matches!(client_server.try_next().await, Ok(None));

        // Should start a new DHCPv6 client when we get an interface changed event that shows the
        // interface as up with an link-local address.
        let () = handle_interface_changed_event(
            &mut netcfg,
            &mut dns_watchers,
            Some(true), /* up */
            None,
        )
        .await
        .context("error handling interface up event")?;
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
            }),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to address change")?;
        assert_matches::assert_matches!(client_server.try_next().await, Ok(None));
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
        .context("error handling interface change event with sockaddr2 replacing sockaddr1")?;
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
                    &mut virtualization::Stub,
                )
                .map(|r| r.context("error handling interface removed event"))
                .map_err(Into::into),
            run_lookup_admin_once(&mut servers.lookup_admin, &netstack_servers)
                .map(|r| r.context("error running lookup admin")),
        )
        .await
        .context("error handling client termination due to interface removal")?;
        assert!(!dns_watchers.contains_key(&DHCPV6_DNS_SOURCE), "should not have a watcher");
        assert_matches::assert_matches!(client_server.try_next().await, Ok(None));
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
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": ["wlan"],
  "interface_metrics": {
    "wlan_metric": 100,
    "eth_metric": 10
  },
  "allowed_upstream_device_classes": ["ethernet", "wlan"],
  "allowed_bridge_upstream_device_classes": ["ethernet"],
  "enable_dhcpv6": true,
  "forwarded_device_classes": { "ipv4": [ "ethernet" ], "ipv6": [ "wlan" ] }
}
"#;

        let Config {
            dns_config: DnsConfig { servers },
            filter_config,
            filter_enabled_interface_types,
            interface_metrics,
            allowed_upstream_device_classes,
            allowed_bridge_upstream_device_classes,
            enable_dhcpv6,
            forwarded_device_classes,
        } = Config::load_str(config_str).unwrap();

        assert_eq!(vec!["8.8.8.8".parse::<std::net::IpAddr>().unwrap()], servers);
        let FilterConfig { rules, nat_rules, rdr_rules } = filter_config;
        assert_eq!(Vec::<String>::new(), rules);
        assert_eq!(Vec::<String>::new(), nat_rules);
        assert_eq!(Vec::<String>::new(), rdr_rules);

        assert_eq!(HashSet::from([InterfaceType::Wlan]), filter_enabled_interface_types);

        let expected_metrics =
            InterfaceMetrics { wlan_metric: Metric(100), eth_metric: Metric(10) };
        assert_eq!(interface_metrics, expected_metrics);

        assert_eq!(
            AllowedDeviceClasses(HashSet::from([DeviceClass::Ethernet, DeviceClass::Wlan])),
            allowed_upstream_device_classes
        );
        assert_eq!(
            AllowedDeviceClasses(HashSet::from([DeviceClass::Ethernet])),
            allowed_bridge_upstream_device_classes
        );

        assert_eq!(enable_dhcpv6, true);

        let expected_classes = ForwardedDeviceClasses {
            ipv4: HashSet::from([DeviceClass::Ethernet]),
            ipv6: HashSet::from([DeviceClass::Wlan]),
        };
        assert_eq!(forwarded_device_classes, expected_classes);
    }

    #[test]
    fn test_config_defaults() {
        let config_str = r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": []
}
"#;

        let Config {
            dns_config: _,
            filter_config: _,
            filter_enabled_interface_types: _,
            allowed_upstream_device_classes,
            allowed_bridge_upstream_device_classes,
            interface_metrics,
            enable_dhcpv6,
            forwarded_device_classes: _,
        } = Config::load_str(config_str).unwrap();

        assert_eq!(allowed_upstream_device_classes, Default::default());
        assert_eq!(allowed_bridge_upstream_device_classes, Default::default());
        assert_eq!(interface_metrics, Default::default());
        assert_eq!(enable_dhcpv6, true);
    }

    #[test_case(
        "eth_metric", Default::default(), Metric(1), Metric(1);
        "wlan assumes default metric when unspecified")]
    #[test_case("wlan_metric", Metric(1), Default::default(), Metric(1);
        "eth assumes default metric when unspecified")]
    fn test_config_metric_individual_defaults(
        metric_name: &'static str,
        wlan_metric: Metric,
        eth_metric: Metric,
        expect_metric: Metric,
    ) {
        let config_str = format!(
            r#"
{{
  "dns_config": {{ "servers": [] }},
  "filter_config": {{
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  }},
  "filter_enabled_interface_types": [],
  "interface_metrics": {{ "{}": {} }}
}}
"#,
            metric_name, expect_metric
        );

        let Config {
            dns_config: _,
            filter_config: _,
            filter_enabled_interface_types: _,
            allowed_upstream_device_classes: _,
            allowed_bridge_upstream_device_classes: _,
            enable_dhcpv6: _,
            interface_metrics,
            forwarded_device_classes: _,
        } = Config::load_str(&config_str).unwrap();

        let expected_metrics = InterfaceMetrics { wlan_metric, eth_metric };
        assert_eq!(interface_metrics, expected_metrics);
    }

    #[test]
    fn test_config_denies_unknown_fields() {
        let config_str = r#"{
            "filter_enabled_interface_types": ["wlan"],
            "foobar": "baz"
        }"#;

        let err = Config::load_str(config_str).expect_err("config shouldn't accept unknown fields");
        let err = err.downcast::<serde_json::Error>().expect("downcast error");
        assert_eq!(err.classify(), serde_json::error::Category::Data);
        assert_eq!(err.line(), 3);
        // Ensure the error is complaining about unknown field.
        assert!(format!("{:?}", err).contains("foobar"));
    }

    #[test]
    fn test_config_denies_unknown_fields_nested() {
        let bad_configs = vec![
            r#"
{
  "dns_config": { "speling": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": []
}
"#,
            r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "speling": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": []
}
"#,
            r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": ["speling"]
}
"#,
            r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "interface_metrics": {
    "eth_metric": 1,
    "wlan_metric": 2,
    "speling": 3,
  },
  "filter_enabled_interface_types": []
}
"#,
            r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": ["speling"]
}
"#,
            r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": [],
  "allowed_upstream_device_classes": ["speling"]
}
"#,
            r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": [],
  "allowed_bridge_upstream_device_classes": ["speling"]
}
"#,
            r#"
{
  "dns_config": { "servers": [] },
  "filter_config": {
    "rules": [],
    "nat_rules": [],
    "rdr_rules": []
  },
  "filter_enabled_interface_types": [],
  "allowed_upstream_device_classes": [],
  "forwarded_device_classes": { "ipv4": [], "ipv6": [], "speling": [] }
}
"#,
        ];

        for config_str in bad_configs {
            let err =
                Config::load_str(config_str).expect_err("config shouldn't accept unknown fields");
            let err = err.downcast::<serde_json::Error>().expect("downcast error");
            assert_eq!(err.classify(), serde_json::error::Category::Data);
            // Ensure the error is complaining about unknown field.
            assert!(format!("{:?}", err).contains("speling"));
        }
    }

    #[test]
    fn test_should_enable_filter() {
        let types_empty: HashSet<InterfaceType> = [].iter().cloned().collect();
        let types_ethernet: HashSet<InterfaceType> =
            [InterfaceType::Ethernet].iter().cloned().collect();
        let types_wlan: HashSet<InterfaceType> = [InterfaceType::Wlan].iter().cloned().collect();

        let make_info = |device_class| DeviceInfo {
            device_class,
            mac: None,
            is_synthetic: false,
            topological_path: "".to_string(),
        };

        let wlan_info = make_info(fidl_fuchsia_hardware_network::DeviceClass::Wlan);
        let wlan_ap_info = make_info(fidl_fuchsia_hardware_network::DeviceClass::WlanAp);
        let ethernet_info = make_info(fidl_fuchsia_hardware_network::DeviceClass::Ethernet);

        assert_eq!(should_enable_filter(&types_empty, &wlan_info), false);
        assert_eq!(should_enable_filter(&types_empty, &wlan_ap_info), false);
        assert_eq!(should_enable_filter(&types_empty, &ethernet_info), false);

        assert_eq!(should_enable_filter(&types_ethernet, &wlan_info), false);
        assert_eq!(should_enable_filter(&types_ethernet, &wlan_ap_info), false);
        assert_eq!(should_enable_filter(&types_ethernet, &ethernet_info), true);

        assert_eq!(should_enable_filter(&types_wlan, &wlan_info), true);
        assert_eq!(should_enable_filter(&types_wlan, &wlan_ap_info), true);
        assert_eq!(should_enable_filter(&types_wlan, &ethernet_info), false);
    }
}

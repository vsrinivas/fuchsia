// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate network_manager_core_interface as interface;

use std::collections::HashSet;
use std::fs;
use std::io;
use std::os::unix::io::IntoRawFd as _;
use std::path;
use std::str::FromStr;

use fidl_fuchsia_hardware_ethernet as feth;
use fidl_fuchsia_hardware_ethernet_ext as feth_ext;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcp as fnet_dhcp;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_ext::IntoExt as _;
use fidl_fuchsia_net_filter as fnet_filter;
use fidl_fuchsia_net_name as fnet_name;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async::DurationExt as _;
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog as fsyslog;
use fuchsia_vfs_watcher as fvfs_watcher;
use fuchsia_zircon::{self as zx, DurationNum as _};

use anyhow::Context as _;
use dns_server_watcher::{
    DnsServerWatcherEvent, DnsServers, DnsServersUpdateSource, DEFAULT_DNS_PORT,
};
use futures::stream::{StreamExt as _, TryStreamExt as _};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use log::{debug, error, info, trace, warn};
use net_declare::fidl_ip_v4;
use serde::Deserialize;
use structopt::StructOpt;

mod matchers;

/// Interface metrics.
///
/// Interface metrics are used to sort the route table. An interface with a
/// lower metric is favored over one with a higher metric.
/// For now favor WLAN over Ethernet.
const INTF_METRIC_WLAN: u32 = 90;
const INTF_METRIC_ETH: u32 = 100;

/// Path to the directory where ethernet devices are found.
const ETH_DEV_PATH: &str = "/dev/class/ethernet";

/// Path to the directory where ethernet devices are found when running in a
/// Netemul environment.
const NETEMUL_ETH_DEV_PATH: &str = "/vdev/class/ethernet";

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

/// Options.
#[derive(StructOpt, Debug)]
#[structopt(
    name = "Network Configuration tool",
    about = "Configures network components in response to events."
)]
struct Opt {
    /// Should netemul specific configurations be used?
    #[structopt(short, long)]
    netemul: bool,

    /// The minimum severity for logs.
    #[structopt(short, long, default_value = "INFO")]
    min_severity: LogLevel,

    /// The config file to use
    #[structopt(short, long, default_value = "default.json")]
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
    features: &feth_ext::EthernetFeatures,
) -> bool {
    if features.contains(feth_ext::EthernetFeatures::LOOPBACK) {
        false
    } else if features.contains(feth_ext::EthernetFeatures::WLAN) {
        filter_enabled_interface_types.contains(&InterfaceType::WLAN)
    } else {
        filter_enabled_interface_types.contains(&InterfaceType::ETHERNET)
    }
}

/// State for a WLAN AP interface.
struct WlanApInterfaceState {
    /// ID of the interface.
    id: u32,

    /// Is this interface up?
    up: bool,

    /// Has this interface been observed by the Netstack yet?
    seen: bool,
}

impl WlanApInterfaceState {
    fn new(id: u32) -> WlanApInterfaceState {
        WlanApInterfaceState { id, up: false, seen: false }
    }
}

/// Network Configuration state.
struct NetCfg<'a> {
    stack: fnet_stack::StackProxy,
    netstack: fnetstack::NetstackProxy,
    lookup_admin: fnet_name::LookupAdminProxy,
    filter: fnet_filter::FilterProxy,
    dhcp_server: fnet_dhcp::Server_Proxy,

    device_dir_path: String,
    allow_virtual_devices: bool,

    persisted_interface_config: interface::FileBackedConfig<'a>,

    filter_enabled_interface_types: HashSet<InterfaceType>,
    default_config_rules: Vec<matchers::InterfaceSpec>,

    /// The WLAN-AP interface state.
    wlan_ap_interface_state: Option<WlanApInterfaceState>,

    dns_servers: DnsServers,
}

/// Returns a [`fnet_name::DnsServer_`] with a static source from a [`fnet::IpAddress`].
fn static_source_from_ip(f: fnet::IpAddress) -> fnet_name::DnsServer_ {
    let socket_addr = match f {
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
        device_dir_path: String,
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
        let persisted_interface_config =
            interface::FileBackedConfig::load(&PERSISTED_INTERFACE_CONFIG_FILEPATH)
                .context("loading persistent interface configurations")?;

        Ok(NetCfg {
            stack,
            netstack,
            lookup_admin,
            filter,
            dhcp_server,
            device_dir_path,
            allow_virtual_devices,
            persisted_interface_config,
            filter_enabled_interface_types,
            default_config_rules,
            wlan_ap_interface_state: None,
            dns_servers: Default::default(),
        })
    }

    /// Updates the network filter configurations.
    async fn update_filters(&mut self, config: FilterConfig) -> Result<(), anyhow::Error> {
        let FilterConfig { rules, nat_rules, rdr_rules } = config;

        if !rules.is_empty() {
            let mut rules = netfilter::parser::parse_str_to_rules(&rules.join(""))
                .context("parsing filter rules")?;
            cas_filter_rules!(self.filter, get_rules, update_rules, rules);
        }

        if !nat_rules.is_empty() {
            let mut nat_rules = netfilter::parser::parse_str_to_nat_rules(&nat_rules.join(""))
                .context("parsing NAT rules")?;
            cas_filter_rules!(self.filter, get_nat_rules, update_nat_rules, nat_rules);
        }

        if !rdr_rules.is_empty() {
            let mut rdr_rules = netfilter::parser::parse_str_to_rdr_rules(&rdr_rules.join(""))
                .context("parsing RDR rules")?;
            cas_filter_rules!(self.filter, get_rdr_rules, update_rdr_rules, rdr_rules);
        }

        Ok(())
    }

    /// Updates the default DNS servers used by the DNS resolver.
    async fn update_default_dns_servers(
        &mut self,
        mut servers: Vec<fnet::IpAddress>,
    ) -> Result<(), anyhow::Error> {
        trace!("updating default DNS servers to {:?}", servers);

        // fuchsia.net.name/LookupAdmin.SetDefaultDnsServers is deprecated so the
        // LookupAdmin service may not implement it. We still try to set the default
        // servers with this method until it is removed just in case the LookupAdmin
        // in the enclosing environment still uses it.
        //
        // TODO(52055): Remove the call to fuchsia.net.name/LookupAdmin.SetDefaultDnsServers
        let () = self
            .lookup_admin
            .set_default_dns_servers(&mut servers.iter_mut())
            .await
            .context("set default DNS servers request")?
            .map_err(zx::Status::from_raw)
            .or_else(|e| {
                if e == zx::Status::NOT_SUPPORTED {
                    warn!("LookupAdmin does not support setting default DNS servers");
                    Ok(())
                } else {
                    Err(e)
                }
            })
            .context("set default DNS servers")?;

        self.update_dns_servers(
            DnsServersUpdateSource::Default,
            servers.into_iter().map(static_source_from_ip).collect(),
        )
        .await
    }

    /// Updates the DNS servers used by the DNS resolver.
    async fn update_dns_servers(
        &mut self,
        source: DnsServersUpdateSource,
        servers: Vec<fnet_name::DnsServer_>,
    ) -> Result<(), anyhow::Error> {
        trace!("updating DNS servers from {:?} to {:?}", source, servers);

        let () = self.dns_servers.set_servers_from_source(source, servers);
        let servers = self.dns_servers.consolidated();
        trace!("updating LookupAdmin with DNS servers = {:?}", servers);

        // Netstack2's LookupAdmin service does not implement
        // fuchsia.net.name/LookupAdmin.SetDnsServers in anticipation of the transition
        // to dns-resolver.
        //
        // TODO(51998): Remove the NOT_SUPPORTED check and consider all errors as
        // a true error.
        let () = self
            .lookup_admin
            .set_dns_servers(&mut servers.into_iter())
            .await
            .context("set DNS servers request")?
            .map_err(zx::Status::from_raw)
            .or_else(|e| {
                if e == zx::Status::NOT_SUPPORTED {
                    warn!("LookupAdmin does not support setting consolidated list of DNS servers");
                    Ok(())
                } else {
                    Err(e)
                }
            })
            .context("set DNS servers")?;

        Ok(())
    }

    /// Run the network configuration eventloop.
    ///
    /// The device directory will be monitored for device events and the netstack will be
    /// configured with a new interface on new device discovery.
    async fn run(mut self) -> Result<(), anyhow::Error> {
        let device_dir_path = self.device_dir_path.clone();
        let mut device_dir_watcher_stream = fvfs_watcher::Watcher::new(
            open_directory_in_namespace(&self.device_dir_path, OPEN_RIGHT_READABLE)
                .context("opening device directory")?,
        )
        .await
        .with_context(|| format!("creating watcher for {}", self.device_dir_path))?
        .map(|r| r.with_context(|| format!("watching {}", device_dir_path)))
        .fuse();

        let mut netstack_stream =
            self.netstack.take_event_stream().map(|r| r.context("Netstack event stream")).fuse();

        let (dns_server_watcher, dns_server_watcher_req) =
            fidl::endpoints::create_proxy::<fnet_name::DnsServerWatcherMarker>()
                .context("create dns server watcher")?;
        let () = self
            .stack
            .get_dns_server_watcher(dns_server_watcher_req)
            .context("get dns server watcher")?;
        let mut netstack_dns_server_stream = dns_server_watcher::new_dns_server_stream(
            DnsServersUpdateSource::Netstack,
            dns_server_watcher,
        )
        .map(|r| r.context("Netstack DNS server event stream"))
        .fuse();

        debug!("starting eventloop...");

        loop {
            let () = futures::select! {
                device_dir_watcher_res = device_dir_watcher_stream.try_next() => {
                    let event = device_dir_watcher_res?
                        .ok_or(anyhow::anyhow!(
                            "device directory {} watcher stream ended unexpectedly",
                            self.device_dir_path
                        ))?;
                    self.handle_device_event(event).await?
                }
                netstack_res = netstack_stream.try_next() => {
                    let event = netstack_res?.ok_or(anyhow::anyhow!("Netstack event stream ended unexpectedly"))?;
                    self.handle_netstack_event(event).await?
                }
                netstack_dns_server_res = netstack_dns_server_stream.try_next() => {
                    let DnsServerWatcherEvent { source, servers } = netstack_dns_server_res?
                        .ok_or(anyhow::anyhow!("Netstack DNS Server watcher stream ended unexpectedly"))?;
                    self.update_dns_servers(source, servers).await?
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
    ) -> Result<(), anyhow::Error> {
        trace!("got netstack event = {:?}", event);

        let wlan_ap_state = match &mut self.wlan_ap_interface_state {
            // If we do not have a known WLAN AP interface, do nothing.
            None => return Ok(()),
            Some(s) => s,
        };

        let fnetstack::NetstackEvent::OnInterfacesChanged { interfaces } = event;
        if let Some(fnetstack::NetInterface { id, flags, name, .. }) =
            interfaces.iter().find(|i| i.id == wlan_ap_state.id)
        {
            wlan_ap_state.seen = true;

            // If the interface's up status has not changed, do nothing further.
            let up = flags & fnetstack::NET_INTERFACE_FLAG_UP != 0;
            if wlan_ap_state.up == up {
                return Ok(());
            }

            wlan_ap_state.up = up;

            if up {
                info!("WLAN AP interface {} (id={}) came up so starting DHCP server", name, id);
                match self.start_dhcp_server().await {
                    Ok(()) => {}
                    Err(e) => error!("error starting DHCP server: {}", e),
                }
            } else {
                info!("WLAN AP interface {} (id={}) went down so stopping DHCP server", name, id);
                match self.stop_dhcp_server().await {
                    Ok(()) => {}
                    Err(e) => error!("error stopping DHCP server: {}", e),
                }
            }

            return Ok(());
        }

        // If we have not received an event with the WLAN AP interface, do not remove it yet.
        // Even if the interface was removed from the Netstack immediately after it was added,
        // we should get an event with the interface. This is so that we do not prematurely
        // clear the WLAN AP interface state.
        if !wlan_ap_state.seen {
            return Ok(());
        }

        // The DHCP server should only run on the WLAN AP interface, so stop it since the AP
        // interface is removed.
        info!("WLAN AP interface with id={} is removed, stopping DHCP server", wlan_ap_state.id);
        self.wlan_ap_interface_state = None;
        match self.stop_dhcp_server().await {
            Ok(()) => {}
            Err(e) => error!("error stopping DHCP server: {}", e),
        }

        Ok(())
    }

    /// Handles an event on the device directory.
    async fn handle_device_event(
        &mut self,
        event: fvfs_watcher::WatchMessage,
    ) -> Result<(), anyhow::Error> {
        let fvfs_watcher::WatchMessage { event, filename } = event;
        trace!("got {:?} event for {}", event, filename.display());

        if filename == path::PathBuf::from(THIS_DIRECTORY) {
            debug!("skipping filename = {}", filename.display());
            return Ok(());
        }

        match event {
            fvfs_watcher::WatchEvent::ADD_FILE | fvfs_watcher::WatchEvent::EXISTING => {
                let filepath = path::Path::new(&self.device_dir_path).join(filename);
                let device = fs::File::open(&filepath)
                    .with_context(|| format!("could not open {}", filepath.display()))?;
                let topological_path = fdio::device_get_topo_path(&device).with_context(|| {
                    format!("fdio::device_get_topo_path({})", filepath.display())
                })?;

                debug!(
                    "topological path for device at {} is {}",
                    filepath.display(),
                    topological_path
                );

                let device = device.into_raw_fd();
                let mut client = 0;
                // Safe because we're passing a valid fd.
                let () = zx::Status::ok(unsafe {
                    fdio::fdio_sys::fdio_get_service_handle(device, &mut client)
                })
                .with_context(|| {
                    format!("zx::sys::fdio_get_service_handle({})", filepath.display())
                })?;
                // Safe because we checked the return status above.
                let client = zx::Channel::from(unsafe { zx::Handle::from_raw(client) });

                let device = fidl::endpoints::ClientEnd::<feth::DeviceMarker>::new(client)
                    .into_proxy()
                    .context("client end proxy")?;

                let device_info: feth_ext::EthernetInfo = match device.get_info().await {
                    Ok(d) => d.into(),
                    Err(e) => {
                        error!("error getting device info for {}: {}", filepath.display(), e);
                        return Ok(());
                    }
                };

                if !self.allow_virtual_devices && !device_info.features.is_physical() {
                    warn!("device at {} is not a physical device, skipping", filepath.display());
                    return Ok(());
                }

                let client = device
                    .into_channel()
                    .map_err(|feth::DeviceProxy { .. }| {
                        anyhow::anyhow!("failed to convert device proxy into channel",)
                    })?
                    .into_zx_channel();

                let name = self.persisted_interface_config.get_stable_name(
                    &topological_path, /* TODO(tamird): we can probably do
                                        * better with std::borrow::Cow. */
                    device_info.mac,
                    device_info.features.contains(feth_ext::EthernetFeatures::WLAN),
                )?;

                // Hardcode the interface metric. Eventually this should
                // be part of the config file.
                let metric: u32 =
                    match device_info.features.contains(feth_ext::EthernetFeatures::WLAN) {
                        true => INTF_METRIC_WLAN,
                        false => INTF_METRIC_ETH,
                    };
                let mut derived_interface_config = matchers::config_for_device(
                    &device_info,
                    name.to_string(),
                    &topological_path,
                    metric,
                    &self.default_config_rules,
                    &filepath,
                );
                let nic_id = self
                    .netstack
                    .add_ethernet_device(
                        &topological_path,
                        &mut derived_interface_config,
                        fidl::endpoints::ClientEnd::<feth::DeviceMarker>::new(client),
                    )
                    .await
                    .with_context(|| {
                        format!(
                            "fidl_netstack::Netstack::add_ethernet_device({})",
                            filepath.display()
                        )
                    })?;

                if topological_path.contains(WLAN_AP_TOPO_PATH_CONTAINS) {
                    if let Some(s) = &self.wlan_ap_interface_state {
                        return Err(anyhow::anyhow!("multiple WLAN AP interfaces are not supported, have WLAN AP interface with id = {}", s.id));
                    }
                    self.wlan_ap_interface_state = Some(WlanApInterfaceState::new(nic_id));

                    info!(
                        "discovered WLAN AP interface with id={}, configuring interface and DHCP server",
                        nic_id
                    );

                    let () = self
                        .configure_wlan_ap_and_dhcp_server(nic_id, derived_interface_config.name)
                        .await?;
                } else {
                    let () = self
                        .configure_host(nic_id, &device_info, &derived_interface_config)
                        .await?;
                }

                let () = self.netstack.set_interface_status(nic_id, true)?;
            }
            fvfs_watcher::WatchEvent::IDLE | fvfs_watcher::WatchEvent::REMOVE_FILE => {}
            event => {
                debug!("unrecognized event {:?} for filename {}", event, filename.display());
            }
        }

        Ok(())
    }

    /// Start the DHCP server.
    async fn start_dhcp_server(&mut self) -> Result<(), anyhow::Error> {
        self.dhcp_server
            .start_serving()
            .await
            .context("start DHCP server request")?
            .map_err(zx::Status::from_raw)
            .context("start DHCP server")
    }

    /// Stop the DHCP server.
    async fn stop_dhcp_server(&mut self) -> Result<(), anyhow::Error> {
        self.dhcp_server.stop_serving().await.context("stop DHCP server request")
    }

    /// Configure host interface.
    async fn configure_host(
        &mut self,
        nic_id: u32,
        info: &feth_ext::EthernetInfo,
        config: &fnetstack::InterfaceConfig,
    ) -> Result<(), anyhow::Error> {
        if should_enable_filter(&self.filter_enabled_interface_types, &info.features) {
            info!("enable filter for nic {}", nic_id);
            let () = self
                .stack
                .enable_packet_filter(nic_id.into())
                .await
                .context("couldn't call enable_packet_filter")?
                .map_err(|e: fnet_stack::Error| {
                    anyhow::anyhow!("failed to enable packet filter with error = {:?}", e)
                })?;
        } else {
            info!("disable filter for nic {}", nic_id);
            let () = self
                .stack
                .disable_packet_filter(nic_id.into())
                .await
                .context("couldn't call disable_packet_filter")?
                .map_err(|e: fnet_stack::Error| {
                    anyhow::anyhow!("failed to disable packet filter with error = {:?}", e)
                })?;
        };

        match config.ip_address_config {
            fnetstack::IpAddressConfig::Dhcp(_) => {
                let (dhcp_client, server_end) =
                    fidl::endpoints::create_proxy::<fnet_dhcp::ClientMarker>()
                        .context("dhcp client: failed to create fidl endpoints")?;
                let () = self
                    .netstack
                    .get_dhcp_client(nic_id, server_end)
                    .await
                    .context("failed to call netstack.get_dhcp_client")?
                    .map_err(zx::Status::from_raw)
                    .context("failed to get dhcp client")?;
                let () = dhcp_client
                    .start()
                    .await
                    .context("start DHCP client request")?
                    .map_err(zx::Status::from_raw)
                    .context("failed to start dhcp client")?;
            }
            fnetstack::IpAddressConfig::StaticIp(fnet::Subnet {
                addr: mut address,
                prefix_len,
            }) => {
                let fnetstack::NetErr { status, message } = self
                    .netstack
                    .set_interface_address(nic_id, &mut address, prefix_len)
                    .await
                    .context("set interface address request")?;
                if status != fnetstack::Status::Ok {
                    // Do not consider this a fatal error because the interface could
                    // have been removed after it was added, but before we reached
                    // this point.
                    error!(
                        "failed to set interface address with status = {:?}: {}",
                        status, message
                    );
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
        nic_id: u32,
        name: String,
    ) -> Result<(), anyhow::Error> {
        // Calculate and set the interface address based on the network address.
        // The interface address should be the first available address.
        let network_addr_as_u32 = u32::from_be_bytes(WLAN_AP_NETWORK_ADDR.addr);
        let interface_addr_as_u32 = network_addr_as_u32 + 1;
        let addr = fnet::Ipv4Address { addr: interface_addr_as_u32.to_be_bytes() };
        let fnetstack::NetErr { status, message } = self
            .netstack
            .set_interface_address(nic_id, &mut addr.into_ext(), WLAN_AP_PREFIX_LEN)
            .await
            .context("set interface address request")?;
        if status != fnetstack::Status::Ok {
            // Do not consider this a fatal error because the interface could
            // have been removed after it was added, but before we reached
            // this point.
            return Err(anyhow::anyhow!(
                "failed to set interface address for WLAN AP with status = {:?}: {}",
                status,
                message
            ));
        }

        // First we clear any leases that the server knows about since the server
        // will be used on a new interface. If leases exist, configuring the DHCP
        // server parameters may fail (AddressPool).
        debug!("clearing DHCP leases");
        let () = self
            .dhcp_server
            .clear_leases()
            .await
            .context("clear DHCP leases request")?
            .map_err(zx::Status::from_raw)
            .context("clear DHCP leases request")?;

        // Configure the DHCP server.
        let v = vec![addr.into()];
        debug!("setting DHCP IpAddrs parameter to {:?}", v);
        let () = self
            .dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::IpAddrs(v))
            .await
            .context("set DHCP IpAddrs parameter request")?
            .map_err(zx::Status::from_raw)
            .context("set DHCP IpAddrs parameter")?;

        let v = vec![name];
        debug!("setting DHCP BoundDeviceNames parameter to {:?}", v);
        let () = self
            .dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::BoundDeviceNames(v))
            .await
            .context("set DHCP BoundDeviceName parameter request")?
            .map_err(zx::Status::from_raw)
            .context("set DHCP BoundDeviceNames parameter")?;

        let v = fnet_dhcp::LeaseLength {
            default: Some(WLAN_AP_DHCP_LEASE_TIME_SECONDS),
            max: Some(WLAN_AP_DHCP_LEASE_TIME_SECONDS),
        };
        debug!("setting DHCP LeaseLength parameter to {:?}", v);
        let () = self
            .dhcp_server
            .set_parameter(&mut fnet_dhcp::Parameter::Lease(v))
            .await
            .context("set DHCP LeaseLength parameter request")?
            .map_err(zx::Status::from_raw)
            .context("set DHCP LeaseLength parameter")?;

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
            .context("set DHCP AddressPool parameter request")?
            .map_err(zx::Status::from_raw)
            .context("set DHCP AddressPool parameter")
    }
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let opt = Opt::from_args();

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
        if opt.netemul { (NETEMUL_ETH_DEV_PATH, true) } else { (ETH_DEV_PATH, false) };

    let mut netcfg = NetCfg::new(
        path.to_string(),
        allow_virtual,
        default_config_rules,
        filter_enabled_interface_types,
    )
    .context("new instance")?;

    let () =
        netcfg.update_filters(filter_config).await.context("update filters based on config")?;

    let servers = servers
        .into_iter()
        .map(fnet_ext::IpAddress)
        .map(Into::into)
        .collect::<Vec<fnet::IpAddress>>();
    debug!("updating default servers to {:?}", servers);
    let () =
        netcfg.update_default_dns_servers(servers).await.context("updating default servers")?;

    netcfg.run().await.context("run eventloop")
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_netstack_ext as fnetstack_ext;

    use matchers::{ConfigOption, InterfaceMatcher, InterfaceSpec};

    use super::*;

    impl Config {
        pub fn load_str(s: &str) -> Result<Self, anyhow::Error> {
            let config = serde_json::from_str(s)
                .with_context(|| format!("could not deserialize the config data {}", s))?;
            Ok(config)
        }
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
                config: ConfigOption::IpConfig(fnetstack_ext::IpAddressConfig::Dhcp),
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

        let features_wlan = feth_ext::EthernetFeatures::WLAN;
        let features_loopback = feth_ext::EthernetFeatures::LOOPBACK;
        let features_synthetic = feth_ext::EthernetFeatures::SYNTHETIC;
        let features_empty = feth_ext::EthernetFeatures::empty();

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

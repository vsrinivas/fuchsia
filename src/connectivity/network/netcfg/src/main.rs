// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate network_manager_core_interface as interface;

use std::collections::HashSet;
use std::fs;
use std::io;
use std::os::unix::io::IntoRawFd as _;
use std::path;

use fidl_fuchsia_hardware_ethernet as feth;
use fidl_fuchsia_hardware_ethernet_ext as feth_ext;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcp as fnet_dhcp;
use fidl_fuchsia_net_ext as fnet_ext;
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
use futures::stream::{StreamExt as _, TryStreamExt as _};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use log::{debug, error, info, trace, warn};
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
    #[structopt(short, long, default_value = "2")]
    min_severity: i32,

    /// The config file to use.
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

/// The source of a DNS Servers update.
enum DnsServersSource {
    /// A list of default servers to use.
    Default,
}

/// Network Configuration state.
struct NetCfg<'a> {
    stack: fnet_stack::StackProxy,
    netstack: fnetstack::NetstackProxy,
    lookup_admin: fnet_name::LookupAdminProxy,
    filter: fnet_filter::FilterProxy,

    device_dir_path: String,
    allow_virtual_devices: bool,

    persisted_interface_config: interface::FileBackedConfig<'a>,

    filter_enabled_interface_types: HashSet<InterfaceType>,
    default_config_rules: Vec<matchers::InterfaceSpec>,
}

impl<'a> NetCfg<'a> {
    /// Returns a new `NetCfg`.
    fn new(
        device_dir_path: String,
        allow_virtual_devices: bool,
        default_config_rules: Vec<matchers::InterfaceSpec>,
        filter_enabled_interface_types: HashSet<InterfaceType>,
    ) -> Result<NetCfg<'a>, anyhow::Error> {
        let stack = connect_to_service::<fnet_stack::StackMarker>()
            .context("could not connect to stack")?;
        let netstack = connect_to_service::<fnetstack::NetstackMarker>()
            .context("could not connect to netstack")?;
        let lookup_admin = connect_to_service::<fnet_name::LookupAdminMarker>()
            .context("could not connect to lookup admin")?;
        let filter = connect_to_service::<fnet_filter::FilterMarker>()
            .context("could not connect to filter")?;
        let persisted_interface_config =
            interface::FileBackedConfig::load(&PERSISTED_INTERFACE_CONFIG_FILEPATH)
                .context("loading persistent interface configurations")?;

        Ok(NetCfg {
            stack,
            netstack,
            lookup_admin,
            filter,
            device_dir_path,
            allow_virtual_devices,
            persisted_interface_config,
            filter_enabled_interface_types,
            default_config_rules,
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

    /// Updates the DNS servers used by the DNS resolver.
    async fn update_dns_servers(
        &mut self,
        source: DnsServersSource,
        mut servers: Vec<fnet::IpAddress>,
    ) -> Result<(), anyhow::Error> {
        match source {
            DnsServersSource::Default => {
                // fuchsia.net.name/LookupAdmin.SetDefaultDnsServers is deprecated so the
                // LookupAdmin service may not implement it. We still try to set the default
                // servers with this method until it is removed just in case the LookupAdmin
                // in the enclosing environment still uses it.
                //
                // TODO(52055): Remove the call to fuchsia.net.name/LookupAdmin.SetDefaultDnsServers
                let ret = self
                    .lookup_admin
                    .set_default_dns_servers(&mut servers.iter_mut())
                    .await
                    .context("set default DNS servers request")?;
                if ret == Err(zx::Status::NOT_SUPPORTED.into_raw()) {
                    Ok(())
                } else {
                    ret.map_err(zx::Status::from_raw).context("failed to set default DNS servers")
                }
            }
        }
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

        debug!("starting eventloop...");

        loop {
            let () = futures::select! {
                device_dir_watcher_res = device_dir_watcher_stream.try_next() => {
                    let event = device_dir_watcher_res?
                        .ok_or(anyhow::anyhow!(
                            "device directory {} watcher stream unexpectedly ended",
                            self.device_dir_path
                        ))?;
                    self.handle_device_event(event).await?
                }
                complete => break,
            };
        }

        Err(anyhow::anyhow!("unexpectedly ended eventloop"))
    }

    /// Handles an event on the device directory.
    async fn handle_device_event(
        &mut self,
        event: fvfs_watcher::WatchMessage,
    ) -> Result<(), anyhow::Error> {
        let fvfs_watcher::WatchMessage { event, filename } = event;
        trace!("got {:?} event for {}", event, filename.display());

        if filename == path::PathBuf::from(THIS_DIRECTORY) {
            info!("skipping filename = {}", filename.display());
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

                if should_enable_filter(&self.filter_enabled_interface_types, &device_info.features)
                {
                    info!("enable filter for nic {}", nic_id);
                    let () = self
                        .stack
                        .enable_packet_filter(nic_id as u64)
                        .await
                        .context("couldn't call enable_packet_filter")?
                        .map_err(|e| {
                            anyhow::anyhow!("failed to enable packet filter with error = {:?}", e)
                        })?;
                } else {
                    info!("disable filter for nic {}", nic_id);
                    let () = self
                        .stack
                        .disable_packet_filter(nic_id as u64)
                        .await
                        .context("couldn't call disable_packet_filter")?
                        .map_err(|e| {
                            anyhow::anyhow!("failed to disable packet filter with error = {:?}", e)
                        })?;
                };

                match derived_interface_config.ip_address_config {
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
                        let err = self
                            .netstack
                            .set_interface_address(nic_id, &mut address, prefix_len)
                            .await
                            .context("set interface address request")?;
                        if err.status != fnetstack::Status::Ok {
                            error!("failed to set interface address with err = {:?}", err);
                        }
                    }
                };
                let () = self.netstack.set_interface_status(nic_id, true)?;
            }
            fvfs_watcher::WatchEvent::IDLE | fvfs_watcher::WatchEvent::REMOVE_FILE => {}
            event => {
                debug!("unrecognized event {:?} for filename {}", event, filename.display());
            }
        }

        Ok(())
    }
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let opt = Opt::from_args();

    fsyslog::init_with_tags(&["netcfg"])?;
    fsyslog::set_severity(opt.min_severity);

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
    let () = netcfg
        .update_dns_servers(DnsServersSource::Default, servers)
        .await
        .context("updating default servers")?;

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

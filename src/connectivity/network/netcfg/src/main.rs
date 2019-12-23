// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::collections::HashSet;
use std::fs;
use std::io;
use std::os::unix::io::AsRawFd;
use std::path;
use std::sync::Arc;

use failure::{self, ResultExt};
use fuchsia_async::DurationExt;
use fuchsia_component::client::connect_to_service;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::fx_log_info;
use fuchsia_zircon::DurationNum;
use futures::lock::Mutex;
use futures::{future::try_join3, FutureExt, StreamExt, TryFutureExt, TryStreamExt};
use serde_derive::Deserialize;

mod dns_policy_service;
mod interface;
mod matchers;

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
    pub fn load<P: AsRef<path::Path>>(path: P) -> Result<Self, failure::Error> {
        let path = path.as_ref();
        let file = fs::File::open(path)
            .with_context(|_| format!("could not open the config file {}", path.display()))?;
        let config = serde_json::from_reader(io::BufReader::new(file)).with_context(|_| {
            format!("could not deserialize the config file {}", path.display())
        })?;
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
    ($filter:ident, $get_rules:ident, $update_rules:ident, $rules:expr) => {
        for retry in 0..FILTER_CAS_RETRY_MAX {
            let (_, generation, status) = $filter.$get_rules().await?;
            if status != fidl_fuchsia_net_filter::Status::Ok {
                let () =
                    Err(failure::format_err!("{} failed: {:?}", stringify!($get_rules), status))?;
            }
            let status = $filter
                .$update_rules(&mut $rules.iter_mut(), generation)
                .await
                .context("error getting response")?;
            match status {
                fidl_fuchsia_net_filter::Status::Ok => {
                    break;
                }
                fidl_fuchsia_net_filter::Status::ErrGenerationMismatch
                    if retry < FILTER_CAS_RETRY_MAX - 1 =>
                {
                    fuchsia_async::Timer::new(
                        FILTER_CAS_RETRY_INTERVAL_MILLIS.millis().after_now(),
                    )
                    .await;
                }
                _ => {
                    let () = Err(failure::format_err!(
                        "{} failed: {:?}",
                        stringify!($update_rules),
                        status
                    ))?;
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
    features: &fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures,
) -> bool {
    if features.contains(fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::LOOPBACK) {
        false
    } else if features.contains(fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::WLAN) {
        filter_enabled_interface_types.contains(&InterfaceType::WLAN)
    } else {
        filter_enabled_interface_types.contains(&InterfaceType::ETHERNET)
    }
}

fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init_with_tags(&["netcfg"])?;
    fx_log_info!("Started");

    let Config {
        dns_config: DnsConfig { servers },
        rules: default_config_rules,
        filter_config,
        filter_enabled_interface_types,
    } = Config::load("/config/data/default.json")?;

    let parse_result: HashSet<InterfaceType> =
        filter_enabled_interface_types.into_iter().map(Into::into).collect();
    if parse_result.iter().any(|interface_type| match interface_type {
        &InterfaceType::UNKNOWN(_) => true,
        _ => false,
    }) {
        return Err(failure::format_err!(
            "failed to parse filter_enabled_interface_types: {:?}",
            parse_result
        ));
    };
    let filter_enabled_interface_types = parse_result;

    let mut persisted_interface_config =
        interface::FileBackedConfig::load(&"/data/net_interfaces.cfg.json")?;
    let mut executor = fuchsia_async::Executor::new().context("could not create executor")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>()
        .context("could not connect to stack")?;
    let netstack = connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("could not connect to netstack")?;
    let resolver_admin = connect_to_service::<fidl_fuchsia_netstack::ResolverAdminMarker>()
        .context("could not connect to resolver admin")?;
    let filter = connect_to_service::<fidl_fuchsia_net_filter::FilterMarker>()
        .context("could not connect to filter")?;

    let filter_setup = async {
        if !filter_config.rules.is_empty() {
            let mut rules = netfilter::parser::parse_str_to_rules(&filter_config.rules.join(""))
                .map_err(|e| failure::format_err!("could not parse filter rules: {:?}", e))?;
            cas_filter_rules!(filter, get_rules, update_rules, rules);
        }
        if !filter_config.nat_rules.is_empty() {
            let mut nat_rules =
                netfilter::parser::parse_str_to_nat_rules(&filter_config.nat_rules.join(""))
                    .map_err(|e| failure::format_err!("could not parse nat rules: {:?}", e))?;
            cas_filter_rules!(filter, get_nat_rules, update_nat_rules, nat_rules);
        }
        if !filter_config.rdr_rules.is_empty() {
            let mut rdr_rules =
                netfilter::parser::parse_str_to_rdr_rules(&filter_config.rdr_rules.join(""))
                    .map_err(|e| failure::format_err!("could not parse nat rules: {:?}", e))?;
            cas_filter_rules!(filter, get_rdr_rules, update_rdr_rules, rdr_rules);
        }
        Ok::<(), failure::Error>(())
    };

    let mut servers = servers
        .into_iter()
        .map(fidl_fuchsia_net_ext::IpAddress)
        .map(Into::into)
        .collect::<Vec<fidl_fuchsia_net::IpAddress>>();

    let () = resolver_admin
        .set_name_servers(&mut servers.iter_mut())
        .context("could not set name servers")?;

    // Interface metrics are used to sort the route table. An interface with a
    // lower metric is favored over one with a higher metric.
    // For now favor WLAN over Ethernet.
    const INTF_METRIC_WLAN: u32 = 90;
    const INTF_METRIC_ETH: u32 = 100;

    const ETHDIR: &str = "/dev/class/ethernet";
    let mut interface_ids = HashMap::new();
    // TODO(chunyingw): Add the loopback interfaces through netcfg
    // Remove the following hardcoded nic id 1.
    interface_ids.insert("lo".to_owned(), 1);
    let interface_ids = Arc::new(Mutex::new(interface_ids));

    let ethdir = fs::File::open(ETHDIR).with_context(|_| format!("could not open {}", ETHDIR))?;
    let ethernet_device = async {
        let mut watcher = fuchsia_vfs_watcher::Watcher::new(&ethdir)
            .await
            .with_context(|_| format!("could not watch {}", ETHDIR))?;

        while let Some(fuchsia_vfs_watcher::WatchMessage { event, filename }) =
            watcher.try_next().await.with_context(|_| format!("watching {}", ETHDIR))?
        {
            match event {
                fuchsia_vfs_watcher::WatchEvent::ADD_FILE
                | fuchsia_vfs_watcher::WatchEvent::EXISTING => {
                    let filepath = path::Path::new(ETHDIR).join(filename);
                    let device = fs::File::open(&filepath)
                        .with_context(|_| format!("could not open {}", filepath.display()))?;
                    let topological_path =
                        fdio::device_get_topo_path(&device).with_context(|_| {
                            format!("fdio::device_get_topo_path({})", filepath.display())
                        })?;
                    let device = device.as_raw_fd();
                    let mut client = 0;
                    // Safe because we're passing a valid fd.
                    let () = fuchsia_zircon::Status::ok(unsafe {
                        fdio::fdio_sys::fdio_get_service_handle(device, &mut client)
                    })
                    .with_context(|_| {
                        format!(
                            "fuchsia_zircon::sys::fdio_get_service_handle({})",
                            filepath.display()
                        )
                    })?;
                    // Safe because we checked the return status above.
                    let client = fuchsia_zircon::Channel::from(unsafe {
                        fuchsia_zircon::Handle::from_raw(client)
                    });

                    let device = fidl::endpoints::ClientEnd::<
                        fidl_fuchsia_hardware_ethernet::DeviceMarker,
                    >::new(client)
                    .into_proxy()?;

                    if let Ok(device_info) = device.get_info().await {
                        let device_info: fidl_fuchsia_hardware_ethernet_ext::EthernetInfo =
                            device_info.into();

                        if device_info.features.is_physical() {
                            let client = device
                                .into_channel()
                                .map_err(
                                    |fidl_fuchsia_hardware_ethernet::DeviceProxy { .. }| {
                                        failure::err_msg(
                                            "failed to convert device proxy into channel",
                                        )
                                    },
                                )?
                                .into_zx_channel();

                            let name = persisted_interface_config.get_stable_name(
                                topological_path.clone(), /* TODO(tamird): we can probably do
                                                           * better with std::borrow::Cow. */
                                device_info.mac,
                                device_info.features.contains(
                                    fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::WLAN,
                                ),
                            )?;

                            // Hardcode the interface metric. Eventually this should
                            // be part of the config file.
                            let metric: u32 = match device_info.features.contains(
                                fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::WLAN,
                            ) {
                                true => INTF_METRIC_WLAN,
                                false => INTF_METRIC_ETH,
                            };
                            let mut derived_interface_config = matchers::config_for_device(
                                &device_info,
                                name.to_string(),
                                &topological_path,
                                metric,
                                &default_config_rules,
                                &filepath,
                            );
                            let nic_id = netstack
                                .add_ethernet_device(
                                    &topological_path,
                                    &mut derived_interface_config,
                                    fidl::endpoints::ClientEnd::<
                                        fidl_fuchsia_hardware_ethernet::DeviceMarker,
                                    >::new(client),
                                )
                                .await
                                .with_context(|_| {
                                    format!(
                                        "fidl_netstack::Netstack::add_ethernet_device({})",
                                        filepath.display()
                                    )
                                })?;

                            if should_enable_filter(
                                &filter_enabled_interface_types,
                                &device_info.features,
                            ) {
                                fx_log_info!("enable filter for nic {}", nic_id);
                                let result = stack
                                    .enable_packet_filter(nic_id as u64)
                                    .await
                                    .context("couldn't call enable_packet_filter")?;
                                let () = result.map_err(|e| {
                                    failure::format_err!("failed to enable packet filter: {:?}", e)
                                })?;
                            } else {
                                fx_log_info!("disable filter for nic {}", nic_id);
                                let result = stack
                                    .disable_packet_filter(nic_id as u64)
                                    .await
                                    .context("couldn't call disable_packet_filter")?;
                                let () = result.map_err(|e| {
                                    failure::format_err!("failed to disable packet filter: {:?}", e)
                                })?;
                            };

                            match derived_interface_config.ip_address_config {
                                fidl_fuchsia_netstack::IpAddressConfig::Dhcp(_) => {
                                    let (dhcp_client, server_end) =
                                        fidl::endpoints::create_proxy::<
                                            fidl_fuchsia_net_dhcp::ClientMarker,
                                        >()
                                        .context("dhcp client: failed to create fidl endpoints")?;
                                    netstack
                                        .get_dhcp_client(nic_id, server_end)
                                        .await
                                        .context("failed to call netstack.get_dhcp_client")?
                                        .map_err(fuchsia_zircon::Status::from_raw)
                                        .context("failed to get dhcp client")?;
                                    dhcp_client
                                        .start()
                                        .map_ok(|result| match result {
                                            Ok(()) => fidl_fuchsia_netstack::NetErr {
                                                status: fidl_fuchsia_netstack::Status::Ok,
                                                message: "".to_string(),
                                            },
                                            Err(status) => fidl_fuchsia_netstack::NetErr {
                                                status: fidl_fuchsia_netstack::Status::UnknownError,
                                                message: fuchsia_zircon::Status::from_raw(status)
                                                    .to_string(),
                                            },
                                        })
                                        .await
                                        .context("failed to start dhcp client")?
                                }
                                fidl_fuchsia_netstack::IpAddressConfig::StaticIp(
                                    fidl_fuchsia_net::Subnet { addr: mut address, prefix_len },
                                ) => {
                                    netstack
                                        .set_interface_address(
                                            nic_id as u32,
                                            &mut address,
                                            prefix_len,
                                        )
                                        .await?
                                }
                            };
                            let () = netstack.set_interface_status(nic_id as u32, true)?;

                            // TODO(chunyingw): when netcfg switches to stack.add_ethernet_interface,
                            // remove casting nic_id to u64.
                            interface_ids
                                .lock()
                                .await
                                .insert(derived_interface_config.name, nic_id as u64);
                        }
                    }
                }

                fuchsia_vfs_watcher::WatchEvent::IDLE
                | fuchsia_vfs_watcher::WatchEvent::REMOVE_FILE => {}
                event => {
                    let () = Err(failure::format_err!("unknown WatchEvent {:?}", event))?;
                }
            }
        }
        Ok(())
    };

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        dns_policy_service::spawn_net_dns_fidl_server(resolver_admin.clone(), stream);
    });
    fs.take_and_serve_directory_handle()?;

    let ((), (), ()) = executor.run_singlethreaded(try_join3(
        filter_setup,
        ethernet_device,
        fs.collect().map(Ok),
    ))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use matchers::{ConfigOption, InterfaceMatcher, InterfaceSpec};

    impl Config {
        pub fn load_str(s: &str) -> Result<Self, failure::Error> {
            let config = serde_json::from_str(s)
                .with_context(|_| format!("could not deserialize the config data {}", s))?;
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
                config: ConfigOption::IpConfig(fidl_fuchsia_netstack_ext::IpAddressConfig::Dhcp),
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

        let features_wlan = fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::WLAN;
        let features_loopback = fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::LOOPBACK;
        let features_synthetic = fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::SYNTHETIC;
        let features_empty = fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::empty();

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

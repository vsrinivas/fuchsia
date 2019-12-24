// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_hardware_ethernet as zx_eth;
use fidl_fuchsia_inspect_deprecated as inspect;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_ext as net_ext;
use fidl_fuchsia_net_filter::{FilterMarker, FilterProxy};
use fidl_fuchsia_net_stack::{
    self as netstack, InterfaceInfo, LogMarker, LogProxy, StackMarker, StackProxy,
};
use fidl_fuchsia_net_stack_ext::{self as pretty, exec_fidl as stack_fidl, FidlReturn};
use fidl_fuchsia_netstack::{NetstackMarker, NetstackProxy};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use glob::glob;
use log::{info, Level, Log, Metadata, Record, SetLoggerError};
use netfilter::FidlReturn as FilterFidlReturn;
use prettytable::{cell, format, row, Table};
use std::fs::File;
use std::os::unix::io::AsRawFd;
use std::str::FromStr;

mod opts;

use crate::opts::*;

macro_rules! filter_fidl {
    ($method:expr, $context:expr) => {
        $method.await.transform_result().context($context)
    };
}

/// Logger which prints levels at or below info to stdout and levels at or
/// above warn to stderr.
struct Logger;

const LOG_LEVEL: Level = Level::Info;

impl Log for Logger {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        metadata.level() <= LOG_LEVEL
    }

    fn log(&self, record: &Record<'_>) {
        if self.enabled(record.metadata()) {
            match record.metadata().level() {
                Level::Trace | Level::Debug | Level::Info => println!("{}", record.args()),
                Level::Warn | Level::Error => eprintln!("{}", record.args()),
            }
        }
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;

fn logger_init() -> Result<(), SetLoggerError> {
    log::set_logger(&LOGGER).map(|()| log::set_max_level(LOG_LEVEL.to_level_filter()))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = logger_init()?;
    let command: Command = argh::from_env();
    let stack = connect_to_service::<StackMarker>().context("failed to connect to netstack")?;
    let netstack =
        connect_to_service::<NetstackMarker>().context("failed to connect to netstack")?;
    let filter = connect_to_service::<FilterMarker>().context("failed to connect to netfilter")?;
    let log = connect_to_service::<LogMarker>().context("failed to connect to netstack log")?;

    match command.cmd {
        CommandEnum::If(cmd) => do_if(cmd.if_cmd, &stack).await,
        CommandEnum::Fwd(cmd) => do_fwd(cmd.fwd_cmd, stack).await,
        CommandEnum::Route(cmd) => do_route(cmd.route_cmd, netstack).await,
        CommandEnum::Filter(cmd) => do_filter(cmd.filter_cmd, filter).await,
        CommandEnum::Log(cmd) => do_log(cmd.log_cmd, log).await,
        CommandEnum::Stat(cmd) => do_stat(cmd.stat_cmd).await,
    }
}

fn shortlist_interfaces(name_pattern: &str, interfaces: &mut Vec<InterfaceInfo>) {
    interfaces.retain(|i| i.properties.name.contains(name_pattern))
}

async fn tabulate_interfaces_info(interfaces: Vec<InterfaceInfo>) -> Result<String, Error> {
    let mut t = Table::new();
    t.set_format(format::FormatBuilder::new().padding(2, 2).build());

    for (i, info) in interfaces.into_iter().enumerate() {
        if i > 0 {
            t.add_row(row![]);
        }

        let pretty::InterfaceInfo {
            id,
            properties:
                pretty::InterfaceProperties {
                    name,
                    topopath,
                    filepath,
                    mac,
                    mtu,
                    features,
                    administrative_status,
                    physical_status,
                    addresses,
                },
        } = info.into();

        t.add_row(row!["nicid", id]);
        t.add_row(row!["name", name]);
        t.add_row(row!["topopath", topopath]);
        t.add_row(row!["filepath", filepath]);

        if let Some(mac) = mac {
            t.add_row(row!["mac", mac]);
        } else {
            t.add_row(row!["mac", "-"]);
        }

        t.add_row(row!["mtu", mtu]);
        t.add_row(row!["features", format!("{:?}", features)]);
        t.add_row(row!["status", format!("{} | {}", administrative_status, physical_status)]);
        for addr in addresses {
            t.add_row(row!["addr", addr]);
        }
    }
    Ok(t.to_string())
}

async fn do_if(cmd: opts::IfEnum, stack: &StackProxy) -> Result<(), Error> {
    match cmd {
        IfEnum::List(IfList { name_pattern }) => {
            let mut response = stack.list_interfaces().await.context("error getting response")?;
            if let Some(name_pattern) = name_pattern {
                let () = shortlist_interfaces(&name_pattern, &mut response);
            }
            let result = tabulate_interfaces_info(response)
                .await
                .context("error tabulating interface info")?;
            println!("{}", result);
        }
        IfEnum::Add(IfAdd { path }) => {
            let dev = File::open(&path).context("failed to open device")?;
            let topological_path =
                fdio::device_get_topo_path(&dev).context("failed to get topological path")?;
            let fd = dev.as_raw_fd();
            let mut client = 0;
            zx::Status::ok(unsafe { fdio::fdio_sys::fdio_get_service_handle(fd, &mut client) })
                .context("failed to get fdio service handle")?;
            let dev = fidl::endpoints::ClientEnd::<zx_eth::DeviceMarker>::new(
                // Safe because we checked the return status above.
                zx::Channel::from(unsafe { zx::Handle::from_raw(client) }),
            );
            let id = stack_fidl!(
                stack.add_ethernet_interface(&topological_path, dev),
                "error adding interface"
            )?;
            info!("Added interface {}", id);
        }
        IfEnum::Del(IfDel { id }) => {
            let () = stack_fidl!(stack.del_ethernet_interface(id), "error removing interface")?;
            info!("Deleted interface {}", id);
        }
        IfEnum::Get(IfGet { id }) => {
            let info = stack_fidl!(stack.get_interface_info(id), "error getting interface")?;
            println!("{}", pretty::InterfaceInfo::from(info));
        }
        IfEnum::Enable(IfEnable { id }) => {
            let () = stack_fidl!(stack.enable_interface(id), "error enabling interface")?;
            info!("Interface {} enabled", id);
        }
        IfEnum::Disable(IfDisable { id }) => {
            let () = stack_fidl!(stack.disable_interface(id), "error disabling interface")?;
            info!("Interface {} disabled", id);
        }
        IfEnum::Addr(IfAddr { addr_cmd }) => match addr_cmd {
            IfAddrEnum::Add(IfAddrAdd { id, addr, prefix }) => {
                let parsed_addr = net_ext::IpAddress::from_str(&addr)?.into();
                let mut fidl_addr =
                    netstack::InterfaceAddress { ip_address: parsed_addr, prefix_len: prefix };
                let () = stack_fidl!(
                    stack.add_interface_address(id, &mut fidl_addr),
                    "error adding interface address"
                )?;
                info!(
                    "Address {} added to interface {}",
                    pretty::InterfaceAddress::from(fidl_addr),
                    id
                );
            }
            IfAddrEnum::Del(IfAddrDel { id, addr, prefix }) => {
                let parsed_addr = net_ext::IpAddress::from_str(&addr)?.into();
                let prefix_len = prefix.unwrap_or_else(|| match parsed_addr {
                    net::IpAddress::Ipv4(_) => 32,
                    net::IpAddress::Ipv6(_) => 128,
                });
                let mut fidl_addr =
                    netstack::InterfaceAddress { ip_address: parsed_addr, prefix_len };
                let () = stack_fidl!(
                    stack.del_interface_address(id, &mut fidl_addr),
                    "error deleting interface address"
                )?;
                info!(
                    "Address {} deleted from interface {}",
                    pretty::InterfaceAddress::from(fidl_addr),
                    id
                );
            }
        },
    }
    Ok(())
}

async fn do_fwd(cmd: opts::FwdEnum, stack: StackProxy) -> Result<(), Error> {
    match cmd {
        FwdEnum::List(_) => {
            let response =
                stack.get_forwarding_table().await.context("error retrieving forwarding table")?;
            for entry in response {
                println!("{}", pretty::ForwardingEntry::from(entry));
            }
        }
        FwdEnum::AddDevice(FwdAddDevice { id, addr, prefix }) => {
            let mut entry = netstack::ForwardingEntry {
                subnet: net::Subnet {
                    addr: net_ext::IpAddress::from_str(&addr)?.into(),
                    prefix_len: prefix,
                },
                destination: netstack::ForwardingDestination::DeviceId(id),
            };
            let () = stack_fidl!(
                stack.add_forwarding_entry(&mut entry),
                "error adding device forwarding entry"
            )?;
            info!("Added forwarding entry for {}/{} to device {}", addr, prefix, id);
        }
        FwdEnum::AddHop(FwdAddHop { next_hop, addr, prefix }) => {
            let mut entry = netstack::ForwardingEntry {
                subnet: net::Subnet {
                    addr: net_ext::IpAddress::from_str(&addr)?.into(),
                    prefix_len: prefix,
                },
                destination: netstack::ForwardingDestination::NextHop(
                    net_ext::IpAddress::from_str(&next_hop)?.into(),
                ),
            };
            let () = stack_fidl!(
                stack.add_forwarding_entry(&mut entry),
                "error adding next-hop forwarding entry"
            )?;
            info!("Added forwarding entry for {}/{} to {}", addr, prefix, next_hop);
        }
        FwdEnum::Del(FwdDel { addr, prefix }) => {
            let mut entry = net::Subnet {
                addr: net_ext::IpAddress::from_str(&addr)?.into(),
                prefix_len: prefix,
            };
            let () = stack_fidl!(
                stack.del_forwarding_entry(&mut entry),
                "error removing forwarding entry"
            )?;
            info!("Removed forwarding entry for {}/{}", addr, prefix);
        }
    }
    Ok(())
}

async fn do_route(cmd: opts::RouteEnum, netstack: NetstackProxy) -> Result<(), Error> {
    match cmd {
        RouteEnum::List(_) => {
            let response =
                netstack.get_route_table2().await.context("error retrieving routing table")?;

            let mut t = Table::new();
            t.set_format(format::FormatBuilder::new().padding(2, 2).build());

            t.set_titles(row!["Destination", "Netmask", "Gateway", "NICID", "Metric"]);
            for entry in response {
                let route = fidl_fuchsia_netstack_ext::RouteTableEntry2::from(entry);
                let gateway_str = match route.gateway {
                    None => "-".to_string(),
                    Some(g) => format!("{}", g),
                };
                t.add_row(row![
                    route.destination,
                    route.netmask,
                    gateway_str,
                    route.nicid,
                    route.metric
                ]);
            }

            t.printstd();
            println!();
        }
    }
    Ok(())
}

async fn do_filter(cmd: opts::FilterEnum, filter: FilterProxy) -> Result<(), Error> {
    match cmd {
        FilterEnum::Enable(_) => {
            let () = filter_fidl!(filter.enable(true), "error enabling filter")?;
            info!("successfully enabled filter");
        }
        FilterEnum::Disable(_) => {
            let () = filter_fidl!(filter.enable(false), "error disabling filter")?;
            info!("successfully disabled filter");
        }
        FilterEnum::IsEnabled(_) => {
            let is_enabled = filter.is_enabled().await.context("FIDL error")?;
            println!("{:?}", is_enabled);
        }
        FilterEnum::GetRules(_) => {
            let (rules, generation) =
                filter_fidl!(filter.get_rules(), "error getting filter rules")?;
            println!("{:?} (generation {})", rules, generation);
        }
        FilterEnum::SetRules(FilterSetRules { rules }) => {
            let (_cur_rules, generation) =
                filter_fidl!(filter.get_rules(), "error getting filter rules")?;
            let mut rules = netfilter::parser::parse_str_to_rules(&rules)?;
            let () = filter_fidl!(
                filter.update_rules(&mut rules.iter_mut(), generation),
                "error setting filter rules"
            )?;
            info!("successfully set filter rules");
        }
        FilterEnum::GetNatRules(_) => {
            let (rules, generation) =
                filter_fidl!(filter.get_nat_rules(), "error getting NAT rules")?;
            println!("{:?} (generation {})", rules, generation);
        }
        FilterEnum::SetNatRules(FilterSetNatRules { rules }) => {
            let (_cur_rules, generation) =
                filter_fidl!(filter.get_nat_rules(), "error getting NAT rules")?;
            let mut rules = netfilter::parser::parse_str_to_nat_rules(&rules)?;
            let () = filter_fidl!(
                filter.update_nat_rules(&mut rules.iter_mut(), generation),
                "error setting NAT rules"
            )?;
            info!("successfully set NAT rules");
        }
        FilterEnum::GetRdrRules(_) => {
            let (rules, generation) =
                filter_fidl!(filter.get_rdr_rules(), "error getting RDR rules")?;
            println!("{:?} (generation {})", rules, generation);
        }
        FilterEnum::SetRdrRules(FilterSetRdrRules { rules }) => {
            let (_cur_rules, generation) =
                filter_fidl!(filter.get_rdr_rules(), "error getting RDR rules")?;
            let mut rules = netfilter::parser::parse_str_to_rdr_rules(&rules)?;
            let () = filter_fidl!(
                filter.update_rdr_rules(&mut rules.iter_mut(), generation),
                "error setting RDR rules"
            )?;
            info!("successfully set RDR rules");
        }
    }
    Ok(())
}

async fn do_log(cmd: opts::LogEnum, log: LogProxy) -> Result<(), Error> {
    match cmd {
        LogEnum::SetLevel(LogSetLevel { log_level }) => {
            let () = stack_fidl!(log.set_log_level(log_level.into()), "error setting log level")?;
            info!("log level set to {:?}", log_level);
        }
    }
    Ok(())
}

async fn do_stat(cmd: opts::StatEnum) -> Result<(), Error> {
    match cmd {
        StatEnum::Show(_) => {
            let mut entries = Vec::new();
            let globs = [
                "/hub",         // accessed in "sys" realm
                "/hub/r/sys/*", // accessed in "app" realm
            ]
            .iter()
            .map(|prefix| {
                format!(
                    "{}/c/netstack.cmx/*/out/diagnostics/counters/{}",
                    prefix,
                    <inspect::InspectMarker as fidl::endpoints::ServiceMarker>::NAME
                )
            })
            .collect::<Vec<_>>();
            globs.iter().try_for_each(|pattern| {
                Ok::<_, glob::PatternError>(entries.extend(glob(pattern)?))
            })?;
            if entries.is_empty() {
                let () = Err(format_err!("failed to find netstack counters in {:?}", globs))?;
            }
            let multiple = entries.len() > 1;
            for (i, entry) in entries.into_iter().enumerate() {
                let path = entry?;
                if multiple {
                    if i > 0 {
                        println!();
                    }
                    // Show the stats path for distinction.
                    println!("Stats from {}", path.display());
                }

                let object = cs::inspect::generate_inspect_object_tree(&path, &vec![])?;

                let mut t = Table::new();
                t.set_format(format::FormatBuilder::new().padding(2, 2).build());
                t.set_titles(row!["Packet Count", "Classification"]);
                let () = visit_inspect_object(&mut t, "", &object);
                t.printstd();
            }
        }
    }
    Ok(())
}

fn visit_inspect_object(t: &mut Table, prefix: &str, inspect_object: &cs::inspect::InspectObject) {
    for metric in &inspect_object.inspect_object.metrics {
        t.add_row(row![
        r->match metric.value {
                inspect::MetricValue::IntValue(v) => v.to_string(),
                inspect::MetricValue::UintValue(v) => v.to_string(),
                inspect::MetricValue::DoubleValue(v) => v.to_string(),
            },
            format!("{}{}", prefix, metric.key),
            ]);
    }
    for child in &inspect_object.child_inspect_objects {
        let prefix = format!("{}{}/", prefix, child.inspect_object.name);
        visit_inspect_object(t, &prefix, child);
    }
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_net as net;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use {super::*, fidl_fuchsia_net_stack::*};

    fn get_fake_interface(id: u64, name: &str) -> InterfaceInfo {
        InterfaceInfo {
            id: id,
            properties: InterfaceProperties {
                name: name.to_string(),
                topopath: "loopback".to_string(),
                filepath: "[none]".to_string(),
                mac: None,
                mtu: 65536,
                features: 4,
                administrative_status: AdministrativeStatus::Enabled,
                physical_status: PhysicalStatus::Up,
                addresses: vec![],
            },
        }
    }

    fn get_fake_interfaces() -> Vec<InterfaceInfo> {
        vec![
            get_fake_interface(1, "lo"),
            get_fake_interface(10, "eth001"),
            get_fake_interface(20, "eth002"),
            get_fake_interface(30, "eth003"),
            get_fake_interface(100, "wlan001"),
            get_fake_interface(200, "wlan002"),
            get_fake_interface(300, "wlan003"),
        ]
    }

    fn shortlist_interfaces_by_nicid(name_pattern: &str) -> Vec<u64> {
        let mut interfaces = get_fake_interfaces();
        let () = shortlist_interfaces(name_pattern, &mut interfaces);
        interfaces.into_iter().map(|i| i.id).collect()
    }

    #[test]
    fn test_shortlist_interfaces() {
        assert_eq!(vec![1, 10, 20, 30, 100, 200, 300], shortlist_interfaces_by_nicid(""));
        assert_eq!(vec![0_u64; 0], shortlist_interfaces_by_nicid("no such thing"));

        assert_eq!(vec![1], shortlist_interfaces_by_nicid("lo"));
        assert_eq!(vec![10, 20, 30], shortlist_interfaces_by_nicid("eth"));
        assert_eq!(vec![10, 20, 30], shortlist_interfaces_by_nicid("th"));
        assert_eq!(vec![100, 200, 300], shortlist_interfaces_by_nicid("wlan"));
        assert_eq!(vec![10, 100], shortlist_interfaces_by_nicid("001"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_if_del_addr() {
        async fn next_request(
            requests: &mut StackRequestStream,
        ) -> (u64, InterfaceAddress, StackDelInterfaceAddressResponder) {
            requests
                .try_next()
                .await
                .expect("del interface address FIDL error")
                .expect("request stream should not have ended")
                .into_del_interface_address()
                .expect("request should be of type DelInterfaceAddress")
        }

        let (stack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<StackMarker>().unwrap();

        fasync::spawn_local(async move {
            // Verify that the first request is as expected and return OK.
            let (id, addr, responder) = next_request(&mut requests).await;
            assert_eq!(id, 1);
            assert_eq!(
                addr,
                InterfaceAddress {
                    ip_address: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 0, 1] }),
                    prefix_len: 32
                }
            );
            responder.send(&mut Ok(())).unwrap();

            // Verify that the second request is as expected and return a NotFound error.
            let (id, addr, responder) = next_request(&mut requests).await;
            assert_eq!(id, 2);
            assert_eq!(
                addr,
                InterfaceAddress {
                    ip_address: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: [0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]
                    }),
                    prefix_len: 128
                }
            );
            responder.send(&mut Err(fidl_fuchsia_net_stack::Error::NotFound)).unwrap();
        });

        // Make the first request.
        let () = do_if(
            IfEnum::Addr(IfAddr {
                addr_cmd: IfAddrEnum::Del(IfAddrDel {
                    id: 1,
                    addr: String::from("192.168.0.1"),
                    prefix: None, // The prefix should be set to the default of 32 for IPv4.
                }),
            }),
            &stack,
        )
        .await
        .expect("net-cli do_if del address should succeed");

        // Make the second request.
        do_if(
            IfEnum::Addr(IfAddr {
                addr_cmd: IfAddrEnum::Del(IfAddrDel {
                    id: 2,
                    addr: String::from("fd00::1"),
                    prefix: None, // The prefix should be set to the default of 128 for IPv6.
                }),
            }),
            &stack,
        )
        .await
        .expect_err("net-cli do_if del address should have failed");
    }
}

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
        CommandEnum::If(If { if_cmd: cmd }) => do_if(cmd, &stack, &netstack).await,
        CommandEnum::Fwd(Fwd { fwd_cmd: cmd }) => do_fwd(cmd, stack).await,
        CommandEnum::Route(Route { route_cmd: cmd }) => do_route(cmd, netstack).await,
        CommandEnum::Filter(Filter { filter_cmd: cmd }) => do_filter(cmd, filter).await,
        CommandEnum::Log(crate::opts::Log { log_cmd: cmd }) => do_log(cmd, log).await,
        CommandEnum::Stat(Stat { stat_cmd: cmd }) => do_stat(cmd).await,
        CommandEnum::Metric(Metric { metric_cmd: cmd }) => do_metric(cmd, netstack).await,
        CommandEnum::Dhcp(Dhcp { dhcp_cmd: cmd }) => do_dhcp(cmd, netstack).await,
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

async fn do_if(
    cmd: opts::IfEnum,
    stack: &StackProxy,
    netstack: &NetstackProxy,
) -> Result<(), Error> {
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
                let mut fidl_addr = net::Subnet { addr: parsed_addr, prefix_len: prefix };
                let () = stack_fidl!(
                    stack.add_interface_address(id, &mut fidl_addr),
                    "error adding interface address"
                )?;
                info!("Address {} added to interface {}", net_ext::Subnet::from(fidl_addr), id);
            }
            IfAddrEnum::Del(IfAddrDel { id, addr, prefix }) => {
                let parsed_addr = net_ext::IpAddress::from_str(&addr)?.into();
                let prefix_len = prefix.unwrap_or_else(|| match parsed_addr {
                    net::IpAddress::Ipv4(_) => 32,
                    net::IpAddress::Ipv6(_) => 128,
                });
                let mut fidl_addr = net::Subnet { addr: parsed_addr, prefix_len };
                let () = stack_fidl!(
                    stack.del_interface_address(id, &mut fidl_addr),
                    "error deleting interface address"
                )?;
                info!("Address {} deleted from interface {}", net_ext::Subnet::from(fidl_addr), id);
            }
        },
        IfEnum::Bridge(IfBridge { ids }) => {
            let (result, bridge_id) = netstack.bridge_interfaces(&ids).await?;
            if result.status != fidl_fuchsia_netstack::Status::Ok {
                return Err(anyhow::anyhow!("{:?}: {}", result.status, result.message));
            } else {
                info!("network bridge created with id {}", bridge_id);
            }
        }
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
        RouteEnum::List(RouteList {}) => {
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
        RouteEnum::Add(route) => {
            let () = with_route_table_transaction_and_entry(&netstack, |transaction| {
                transaction.add_route(&mut route.into())
            })
            .await?;
        }
        RouteEnum::Del(route) => {
            let () = with_route_table_transaction_and_entry(&netstack, |transaction| {
                transaction.del_route(&mut route.into())
            })
            .await?;
        }
    }
    Ok(())
}

async fn with_route_table_transaction_and_entry<T, F>(
    netstack: &fidl_fuchsia_netstack::NetstackProxy,
    func: T,
) -> Result<(), Error>
where
    F: core::future::Future<Output = Result<i32, fidl::Error>>,
    T: FnOnce(&fidl_fuchsia_netstack::RouteTableTransactionProxy) -> F,
{
    let (route_table, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_netstack::RouteTableTransactionMarker>()?;
    let () = fuchsia_zircon::Status::ok(netstack.start_route_table_transaction(server_end).await?)?;
    let status = func(&route_table).await?;
    let () = fuchsia_zircon::Status::ok(status)?;
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
        LogEnum::SetPackets(LogSetPackets { enabled }) => {
            let () = log.set_log_packets(enabled).await.context("error setting log packets")?;
            info!("log packets set to {:?}", enabled);
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

                let path = path
                    .to_str()
                    .ok_or_else(|| format_err!("failed to convert {} to str", path.display()))?;
                let object = inspect_fidl_load::load_hierarchy_from_path(path).await?;

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

async fn do_metric(cmd: opts::MetricEnum, netstack: NetstackProxy) -> Result<(), Error> {
    match cmd {
        MetricEnum::Set(MetricSet { id, metric }) => {
            let result = netstack.set_interface_metric(id, metric).await?;
            if result.status != fidl_fuchsia_netstack::Status::Ok {
                Err(anyhow::anyhow!("{:?}: {}", result.status, result.message))
            } else {
                info!("interface {} metric set to {}", id, metric);
                Ok(())
            }
        }
    }
}

async fn do_dhcp(cmd: opts::DhcpEnum, netstack: NetstackProxy) -> Result<(), Error> {
    let (dhcp, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_dhcp::ClientMarker>()?;
    match cmd {
        DhcpEnum::Start(DhcpStart { id }) => {
            let () = netstack
                .get_dhcp_client(id, server_end)
                .await?
                .map_err(fuchsia_zircon::Status::from_raw)?;
            let () = dhcp.start().await?.map_err(fuchsia_zircon::Status::from_raw)?;
            info!("dhcp client started on interface {}", id);
        }
        DhcpEnum::Stop(DhcpStop { id }) => {
            let () = netstack
                .get_dhcp_client(id, server_end)
                .await?
                .map_err(fuchsia_zircon::Status::from_raw)?;
            let () = dhcp.stop().await?.map_err(fuchsia_zircon::Status::from_raw)?;
            info!("dhcp client stopped on interface {}", id);
        }
    }
    Ok(())
}

fn visit_inspect_object(
    t: &mut Table,
    prefix: &str,
    fuchsia_inspect::reader::NodeHierarchy { name: _, properties, children, missing: _ }: &fuchsia_inspect::reader::NodeHierarchy,
) {
    use fuchsia_inspect::reader::Property::*;

    for property in properties {
        let (key, value) = match property {
            Int(key, value) => (key, value.to_string()),
            Uint(key, value) => (key, value.to_string()),
            Double(key, value) => (key, value.to_string()),
            String(_, _)
            | Bytes(_, _)
            | Bool(_, _)
            | DoubleArray(_, _)
            | IntArray(_, _)
            | UintArray(_, _) => continue,
        };
        t.add_row(row![r->value, format!("{}{}", prefix, key)]);
    }
    for child in children {
        let prefix = format!("{}{}/", prefix, child.name);
        visit_inspect_object(t, &prefix, child);
    }
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_net as net;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use {super::*, fidl_fuchsia_net_stack::*, fidl_fuchsia_netstack::*};

    fn get_fake_interface(id: u64, name: &str) -> InterfaceInfo {
        InterfaceInfo {
            id: id,
            properties: InterfaceProperties {
                name: name.to_string(),
                topopath: "loopback".to_string(),
                filepath: "[none]".to_string(),
                mac: None,
                mtu: 65536,
                features: zx_eth::Features::Loopback,
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
        ) -> (u64, net::Subnet, StackDelInterfaceAddressResponder) {
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
        let (netstack, _) = fidl::endpoints::create_proxy::<NetstackMarker>().unwrap();

        // Make the first request.
        let succeeds = do_if(
            IfEnum::Addr(IfAddr {
                addr_cmd: IfAddrEnum::Del(IfAddrDel {
                    id: 1,
                    addr: String::from("192.168.0.1"),
                    prefix: None, // The prefix should be set to the default of 32 for IPv4.
                }),
            }),
            &stack,
            &netstack,
        );
        let success_response = async {
            // Verify that the first request is as expected and return OK.
            let (id, addr, responder) = next_request(&mut requests).await;
            assert_eq!(id, 1);
            assert_eq!(
                addr,
                net::Subnet {
                    addr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [192, 168, 0, 1] }),
                    prefix_len: 32
                }
            );
            responder.send(&mut Ok(())).map_err(anyhow::Error::new)
        };
        let ((), ()) = futures::future::try_join(success_response, succeeds)
            .await
            .expect("do_if should succeed");

        // Make the second request.
        let fails = do_if(
            IfEnum::Addr(IfAddr {
                addr_cmd: IfAddrEnum::Del(IfAddrDel {
                    id: 2,
                    addr: String::from("fd00::1"),
                    prefix: None, // The prefix should be set to the default of 128 for IPv6.
                }),
            }),
            &stack,
            &netstack,
        );
        let fail_response = async {
            // Verify that the second request is as expected and return a NotFound error.
            let (id, addr, responder) = next_request(&mut requests).await;
            assert_eq!(id, 2);
            assert_eq!(
                addr,
                net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: [0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]
                    }),
                    prefix_len: 128
                }
            );
            responder
                .send(&mut Err(fidl_fuchsia_net_stack::Error::NotFound))
                .map_err(anyhow::Error::new)
        };
        let (fails_response, fails) = futures::future::join(fail_response, fails).await;
        let () = fails_response.expect("responder.send should succeed");
        let fidl_err = fails.expect_err("do_if should fail");
        let fidl_fuchsia_net_stack_ext::NetstackError(underlying_error) = fidl_err
            .root_cause()
            .downcast_ref::<fidl_fuchsia_net_stack_ext::NetstackError>()
            .expect("fidl_err should downcast to NetstackError");
        assert_eq!(*underlying_error, fidl_fuchsia_net_stack::Error::NotFound);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_metric_set() {
        async fn next_request(
            requests: &mut NetstackRequestStream,
        ) -> (u32, u32, NetstackSetInterfaceMetricResponder) {
            requests
                .try_next()
                .await
                .expect("set interface metric FIDL error")
                .expect("request stream should not have ended")
                .into_set_interface_metric()
                .expect("request should be of type SetInterfaceMetric")
        }

        let (netstack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<NetstackMarker>().unwrap();

        // both test values have been arbitrarily selected
        let expected_id = 1;
        let expected_metric = 64;
        let succeeds = do_metric(
            MetricEnum::Set(MetricSet { id: expected_id, metric: expected_metric }),
            netstack,
        );
        let response = async move {
            // Verify that the request is as expected and return OK.
            let (id, metric, responder) = next_request(&mut requests).await;
            assert_eq!(id, expected_id);
            assert_eq!(metric, expected_metric);
            responder
                .send(&mut NetErr { status: Status::Ok, message: String::from("") })
                .map_err(anyhow::Error::new)
        };
        let ((), ()) =
            futures::future::try_join(succeeds, response).await.expect("metric set should succeed");
    }

    async fn test_do_dhcp(cmd: DhcpEnum) {
        let (netstack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<NetstackMarker>().unwrap();
        let op = do_dhcp(cmd.clone(), netstack.clone());
        let op_succeeds = async move {
            let (received_id, dhcp_requests, netstack_responder) = requests
                .try_next()
                .await
                .expect("get dhcp client FIDL error")
                .expect("request stream should not have ended")
                .into_get_dhcp_client()
                .expect("request should be of type GetDhcpClient");
            let mut dhcp_requests =
                dhcp_requests.into_stream().expect("should convert to request stream");
            netstack_responder.send(&mut Ok(())).expect("netstack_responder.send should succeed");
            match cmd {
                DhcpEnum::Start(DhcpStart { id: expected_id }) => {
                    assert_eq!(received_id, expected_id);
                    dhcp_requests
                        .try_next()
                        .await
                        .expect("start FIDL error")
                        .expect("request stream should not have ended")
                        .into_start()
                        .expect("request should be of type Start")
                        .send(&mut Ok(()))
                        .map_err(anyhow::Error::new)
                }
                DhcpEnum::Stop(DhcpStop { id: expected_id }) => {
                    assert_eq!(received_id, expected_id);
                    dhcp_requests
                        .try_next()
                        .await
                        .expect("stop FIDL error")
                        .expect("request stream should not have ended")
                        .into_stop()
                        .expect("request should be of type Stop")
                        .send(&mut Ok(()))
                        .map_err(anyhow::Error::new)
                }
            }
        };
        let ((), ()) =
            futures::future::try_join(op, op_succeeds).await.expect("dhcp command should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_dhcp_start() {
        let () = test_do_dhcp(DhcpEnum::Start(DhcpStart { id: 1 })).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_dhcp_stop() {
        let () = test_do_dhcp(DhcpEnum::Stop(DhcpStop { id: 1 })).await;
    }

    async fn test_modify_route(cmd: RouteEnum) {
        let (netstack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<NetstackMarker>().unwrap();
        let op = do_route(cmd.clone(), netstack.clone());
        let op_succeeds = async move {
            let (route_table_requests, netstack_responder) = requests
                .try_next()
                .await
                .expect("start route table transaction FIDL error")
                .expect("request stream should not have ended")
                .into_start_route_table_transaction()
                .expect("request should be of type StartRouteTableTransaction");
            let mut route_table_requests =
                route_table_requests.into_stream().expect("should convert to request stream");
            netstack_responder
                .send(fuchsia_zircon::Status::OK.into_raw())
                .expect("netstack_responder.send should succeed");
            let () = match cmd {
                RouteEnum::List(RouteList {}) => {
                    panic!("test_modify_route should not take a List command")
                }
                RouteEnum::Add(route) => {
                    let expected_entry = route.into();
                    let (entry, responder) = route_table_requests
                        .try_next()
                        .await
                        .expect("add route FIDL error")
                        .expect("request stream should not have ended")
                        .into_add_route()
                        .expect("request should be of type AddRoute");
                    assert_eq!(entry, expected_entry);
                    responder.send(fuchsia_zircon::Status::OK.into_raw())
                }
                RouteEnum::Del(route) => {
                    let expected_entry = route.into();
                    let (entry, responder) = route_table_requests
                        .try_next()
                        .await
                        .expect("del route FIDL error")
                        .expect("request stream should not have ended")
                        .into_del_route()
                        .expect("request should be of type DelRoute");
                    assert_eq!(entry, expected_entry);
                    responder.send(fuchsia_zircon::Status::OK.into_raw())
                }
            }?;
            Ok(())
        };
        let ((), ()) =
            futures::future::try_join(op, op_succeeds).await.expect("dhcp command should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_route_add() {
        // Test arguments have been arbitrarily selected.
        let () = test_modify_route(RouteEnum::Add(RouteAdd {
            destination: std::net::IpAddr::V4(std::net::Ipv4Addr::new(192, 168, 1, 0)),
            netmask: std::net::IpAddr::V4(std::net::Ipv4Addr::new(255, 255, 255, 0)),
            gateway: None,
            nicid: 2,
            metric: 100,
        }))
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_route_del() {
        // Test arguments have been arbitrarily selected.
        let () = test_modify_route(RouteEnum::Del(RouteDel {
            destination: std::net::IpAddr::V4(std::net::Ipv4Addr::new(192, 168, 1, 0)),
            netmask: std::net::IpAddr::V4(std::net::Ipv4Addr::new(255, 255, 255, 0)),
            gateway: None,
            nicid: 2,
            metric: 100,
        }))
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_bridge() {
        let (stack, _) = fidl::endpoints::create_proxy::<StackMarker>().unwrap();
        let (netstack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<NetstackMarker>().unwrap();
        // interface id test values have been selected arbitrarily
        let bridge_ifs = vec![1, 2, 3];
        let bridge_id = 4;
        let bridge = do_if(IfEnum::Bridge(IfBridge { ids: bridge_ifs.clone() }), &stack, &netstack);
        let bridge_succeeds = async move {
            let (requested_ifs, netstack_responder) = requests
                .try_next()
                .await
                .expect("bridge_interfaces FIDL error")
                .expect("request stream should not have ended")
                .into_bridge_interfaces()
                .expect("request should be of type BridgeInterfaces");
            assert_eq!(requested_ifs, bridge_ifs);
            let () = netstack_responder
                .send(
                    &mut fidl_fuchsia_netstack::NetErr {
                        status: fidl_fuchsia_netstack::Status::Ok,
                        message: String::from(""),
                    },
                    bridge_id,
                )
                .expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(bridge, bridge_succeeds)
            .await
            .expect("if bridge should succeed");
    }
}

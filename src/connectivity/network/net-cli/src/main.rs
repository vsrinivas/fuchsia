// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{err_msg, format_err, Error, ResultExt};
use fidl_fuchsia_hardware_ethernet as zx_eth;
use fidl_fuchsia_io as io;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_filter::{self as filter, FilterMarker, FilterProxy};
use fidl_fuchsia_net_stack::{
    self as netstack, InterfaceInfo, LogMarker, LogProxy, StackMarker, StackProxy,
};
use fidl_fuchsia_net_stack_ext as pretty;
use fidl_fuchsia_netstack::{NetstackMarker, NetstackProxy};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use glob::glob;
use prettytable::{cell, format, row, Table};
use std::convert::TryInto;
use std::fs::File;
use std::os::unix::io::AsRawFd;
use std::path::Path;
use structopt::StructOpt;

mod opts;

use crate::opts::*;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let stack = connect_to_service::<StackMarker>().context("failed to connect to netstack")?;
    let netstack =
        connect_to_service::<NetstackMarker>().context("failed to connect to netstack")?;
    let filter = connect_to_service::<FilterMarker>().context("failed to connect to netfilter")?;
    let log = connect_to_service::<LogMarker>().context("failed to connect to netstack log")?;

    let fut = async {
        match opt {
            Opt::If(cmd) => do_if(cmd, stack, netstack).await,
            Opt::Fwd(cmd) => do_fwd(cmd, stack).await,
            Opt::Route(cmd) => do_route(cmd, netstack).await,
            Opt::Filter(cmd) => do_filter(cmd, filter).await,
            Opt::Log(cmd) => do_log(cmd, log).await,
            Opt::Stat(cmd) => do_stat(cmd).await,
        }
    };
    exec.run_singlethreaded(fut)
}

fn shortlist_interfaces(name_pattern: &str, interfaces: &mut Vec<InterfaceInfo>) {
    interfaces.retain(|i| i.properties.name.contains(name_pattern))
}

fn display_unit(value: u64) -> (u64, char) {
    const KILO: u64 = 1024;
    let units = [' ', 'K', 'M', 'G', 'T', 'P'];

    let (i, unit) = units
        .iter()
        .enumerate()
        .find(|(i, _)| KILO.pow((i + 1) as u32) > value)
        .unwrap_or((units.len(), units.last().unwrap()));
    (value / (KILO.pow(i as u32)), *unit)
}

async fn tabulate_interfaces_info(
    interfaces: Vec<InterfaceInfo>,
    netstack: &NetstackProxy,
) -> Result<String, Error> {
    let mut t = Table::new();
    t.set_format(format::FormatBuilder::new().padding(2, 2).build());

    for (i, info) in interfaces.into_iter().enumerate() {
        if i > 0 {
            t.add_row(row![]);
        }

        let stats =
            netstack.get_stats(info.id as u32).await.context("error retrieving interface stats")?;
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

        t.add_row(row!["nicid", H2->id]);
        t.add_row(row!["name", H2->name]);
        t.add_row(row!["topopath", H2->topopath]);
        t.add_row(row!["filepath", H2->filepath]);

        if let Some(mac) = mac {
            t.add_row(row!["mac", H2->mac]);
        } else {
            t.add_row(row!["mac", H2->"-"]);
        }

        t.add_row(row!["mtu", H2->mtu]);
        t.add_row(row!["features", H2->format!("{:?}", features)]);
        t.add_row(row!["status", H2->format!("{} | {}", administrative_status, physical_status)]);
        for addr in addresses {
            t.add_row(row!["addr", H2->addr]);
        }

        for (direction, stats) in [("Rx", stats.rx), ("Tx", stats.tx)].into_iter() {
            for (stat, value) in
                [("pkts", stats.pkts_total), ("bytes", stats.bytes_total)].into_iter()
            {
                let (unit_value, unit) = display_unit(*value);
                t.add_row(
                    row![format!("{} {}", direction, stat), r->format!("{} {}", unit_value, unit), cell!(), r->value],
                );
            }
        }
    }
    Ok(t.to_string())
}

async fn do_if(cmd: opts::IfCmd, stack: StackProxy, netstack: NetstackProxy) -> Result<(), Error> {
    match cmd {
        IfCmd::List { name_pattern } => {
            let mut response = stack.list_interfaces().await.context("error getting response")?;
            if let Some(name_pattern) = name_pattern {
                let () = shortlist_interfaces(&name_pattern, &mut response);
            }
            let result = tabulate_interfaces_info(response, &netstack)
                .await
                .context("error tabulating interface info")?;
            println!("{}", result);
        }
        IfCmd::Add { path } => {
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
            let response = stack
                .add_ethernet_interface(&topological_path, dev)
                .await
                .context("error adding device")?;
            match response {
                Ok(id) => println!("Added interface {}", id),
                Err(e) => println!("Error adding interface {}: {:?}", path, e),
            }
        }
        IfCmd::Del { id } => {
            let response =
                stack.del_ethernet_interface(id).await.context("error removing device")?;
            match response {
                Ok(()) => println!("Interface {} deleted", id),
                Err(e) => println!("Error removing interface {}: {:?}", id, e),
            }
        }
        IfCmd::Get { id } => {
            let response = stack.get_interface_info(id).await.context("error getting response")?;
            match response {
                Ok(info) => println!("{}", pretty::InterfaceInfo::from(info)),
                Err(e) => println!("Error getting interface {}: {:?}", id, e),
            }
        }
        IfCmd::Enable { id } => {
            let response = stack.enable_interface(id).await.context("error getting response")?;
            match response {
                Ok(()) => println!("Interface {} enabled", id),
                Err(e) => println!("Error enabling interface {}: {:?}", id, e),
            }
        }
        IfCmd::Disable { id } => {
            let response = stack.disable_interface(id).await.context("error getting response")?;
            match response {
                Ok(()) => println!("Interface {} disabled", id),
                Err(e) => println!("Error disabling interface {}: {:?}", id, e),
            }
        }
        IfCmd::Addr(AddrCmd::Add { id, addr, prefix }) => {
            let parsed_addr = parse_ip_addr(&addr)?;
            let mut fidl_addr =
                netstack::InterfaceAddress { ip_address: parsed_addr, prefix_len: prefix };
            let response = stack
                .add_interface_address(id, &mut fidl_addr)
                .await
                .context("error setting interface address")?;
            match response {
                Ok(()) => println!(
                    "Address {} added to interface {}",
                    pretty::InterfaceAddress::from(fidl_addr),
                    id
                ),
                Err(e) => println!("Error adding interface address {}: {:?}", id, e),
            }
        }
        IfCmd::Addr(AddrCmd::Del { .. }) => {
            println!("{:?} not implemented!", cmd);
        }
    }
    Ok(())
}

async fn do_fwd(cmd: opts::FwdCmd, stack: StackProxy) -> Result<(), Error> {
    match cmd {
        FwdCmd::List => {
            let response =
                stack.get_forwarding_table().await.context("error retrieving forwarding table")?;
            for entry in response {
                println!("{}", pretty::ForwardingEntry::from(entry));
            }
        }
        FwdCmd::AddDevice { id, addr, prefix } => {
            let response = stack
                .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                    subnet: net::Subnet { addr: parse_ip_addr(&addr)?, prefix_len: prefix },
                    destination: fidl_fuchsia_net_stack::ForwardingDestination::DeviceId(id),
                })
                .await
                .context("error adding forwarding entry")?;
            match response {
                Ok(()) => {
                    println!("Added forwarding entry for {}/{} to device {}", addr, prefix, id)
                }
                Err(e) => println!("Error adding forwarding entry: {:?}", e),
            }
        }
        FwdCmd::AddHop { next_hop, addr, prefix } => {
            let response = stack
                .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                    subnet: net::Subnet { addr: parse_ip_addr(&addr)?, prefix_len: prefix },
                    destination: fidl_fuchsia_net_stack::ForwardingDestination::NextHop(
                        parse_ip_addr(&next_hop)?,
                    ),
                })
                .await
                .context("error adding forwarding entry")?;
            match response {
                Ok(()) => {
                    println!("Added forwarding entry for {}/{} to {}", addr, prefix, next_hop)
                }
                Err(e) => println!("Error adding forwarding entry: {:?}", e),
            }
        }
        FwdCmd::Del { addr, prefix } => {
            let response = stack
                .del_forwarding_entry(&mut net::Subnet {
                    addr: parse_ip_addr(&addr)?,
                    prefix_len: prefix,
                })
                .await
                .context("error removing forwarding entry")?;
            match response {
                Ok(()) => println!("Removed forwarding entry for {}/{}", addr, prefix),
                Err(e) => println!("Error removing forwarding entry: {:?}", e),
            }
        }
    }
    Ok(())
}

async fn do_route(cmd: opts::RouteCmd, netstack: NetstackProxy) -> Result<(), Error> {
    match cmd {
        RouteCmd::List => {
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

fn parse_ip_addr(addr: &str) -> Result<net::IpAddress, Error> {
    match addr.parse()? {
        ::std::net::IpAddr::V4(ipv4) => {
            Ok(net::IpAddress::Ipv4(net::Ipv4Address { addr: ipv4.octets() }))
        }
        ::std::net::IpAddr::V6(ipv6) => {
            Ok(net::IpAddress::Ipv6(net::Ipv6Address { addr: ipv6.octets() }))
        }
    }
}

async fn do_filter(cmd: opts::FilterCmd, filter: FilterProxy) -> Result<(), Error> {
    match cmd {
        FilterCmd::Enable => {
            let status = filter.enable(true).await.context("error getting response")?;
            println!("{:?}", status);
        }
        FilterCmd::Disable => {
            let status = filter.enable(false).await.context("error getting response")?;
            println!("{:?}", status);
        }
        FilterCmd::IsEnabled => {
            let is_enabled = filter.is_enabled().await.context("error getting response")?;
            println!("{:?}", is_enabled);
        }
        FilterCmd::GetRules => {
            let (rules, generation, status) =
                filter.get_rules().await.context("error getting response")?;
            if status == filter::Status::Ok {
                println!("{:?} (generation {})", rules, generation);
            } else {
                eprintln!("{:?}", status);
            }
        }
        FilterCmd::SetRules { rules } => {
            let (_cur_rules, generation, status) =
                filter.get_rules().await.context("error getting response")?;
            if status != filter::Status::Ok {
                println!("{:?}", status);
                return Ok(());
            }
            match netfilter::parser::parse_str_to_rules(&rules) {
                Ok(mut rules) => {
                    let status = filter
                        .update_rules(&mut rules.iter_mut(), generation)
                        .await
                        .context("error getting response")?;
                    println!("{:?}", status);
                }
                Err(e) => eprintln!("{:?}", e),
            }
        }
        FilterCmd::GetNatRules => {
            let (rules, generation, status) =
                filter.get_nat_rules().await.context("error getting response")?;
            if status == filter::Status::Ok {
                println!("{:?} (generation {})", rules, generation);
            } else {
                eprintln!("{:?}", status);
            }
        }
        FilterCmd::SetNatRules { rules } => {
            let (_cur_rules, generation, status) =
                filter.get_nat_rules().await.context("error getting response")?;
            if status != filter::Status::Ok {
                println!("{:?}", status);
                return Ok(());
            }
            match netfilter::parser::parse_str_to_nat_rules(&rules) {
                Ok(mut rules) => {
                    let status = filter
                        .update_nat_rules(&mut rules.iter_mut(), generation)
                        .await
                        .context("error getting response")?;
                    println!("{:?}", status);
                }
                Err(e) => eprintln!("{:?}", e),
            }
        }
        FilterCmd::GetRdrRules => {
            let (rules, generation, status) =
                filter.get_rdr_rules().await.context("error getting response")?;
            if status == filter::Status::Ok {
                println!("{:?} (generation {})", rules, generation);
            } else {
                eprintln!("{:?}", status);
            }
        }
        FilterCmd::SetRdrRules { rules } => {
            let (_cur_rules, generation, status) =
                filter.get_rdr_rules().await.context("error getting response")?;
            if status != filter::Status::Ok {
                println!("{:?}", status);
                return Ok(());
            }
            match netfilter::parser::parse_str_to_rdr_rules(&rules) {
                Ok(mut rules) => {
                    let status = filter
                        .update_rdr_rules(&mut rules.iter_mut(), generation)
                        .await
                        .context("error getting response")?;
                    println!("{:?}", status);
                }
                Err(e) => eprintln!("{:?}", e),
            }
        }
    }
    Ok(())
}

async fn do_log(cmd: opts::LogCmd, log: LogProxy) -> Result<(), Error> {
    match cmd {
        LogCmd::SetLevel { log_level } => {
            match log.set_log_level(log_level.into()).await.context("failed to set log level")? {
                Ok(()) => println!("log level set to {:?}", log_level),
                Err(e) => eprintln!("failed to set log level to {:?}: {:?}", log_level, e),
            }
        }
    }
    Ok(())
}

async fn do_stat(cmd: opts::StatCmd) -> Result<(), Error> {
    match cmd {
        StatCmd::Show => {
            let mut entries = Vec::new();
            let globs = [
                "/hub/c/netstack.cmx/*/out/debug/counters", // accessed in "sys" realm
                "/hub/r/sys/*/c/netstack.cmx/*/out/debug/counters", // accessed in "app" realm
            ];
            globs.into_iter().try_for_each(|pattern| {
                Ok::<_, glob::PatternError>(entries.extend(glob(pattern)?))
            })?;
            if entries.is_empty() {
                let () = Err(format_err!("failed to find netstack counters in {:?}", globs))?;
            }
            let multiple = entries.len() > 1;
            for (i, entry) in entries.into_iter().enumerate() {
                let path = entry?;
                let path_str = path
                    .to_str()
                    .ok_or_else(|| format_err!("path {} is not valid UTF-8", path.display()))?;
                if multiple {
                    if i > 0 {
                        println!();
                    }
                    // Show the stats path for distinction.
                    println!("Stats from {}", path.display());
                }
                dump_stat(path_str).await?;
            }
        }
    }
    Ok(())
}

async fn dump_stat(path: &str) -> Result<(), Error> {
    let dir_proxy = io_util::open_directory_in_namespace(path, io::OPEN_RIGHT_READABLE)?;
    let dir_entries = files_async::readdir_recursive(&dir_proxy)
        .await
        .context("failed to read dir recursively")?;

    let mut t = Table::new();
    t.set_format(format::FormatBuilder::new().padding(2, 2).build());
    t.set_titles(row!["Packet Count", "Classification"]);

    let mut has_error = false;
    for entry in dir_entries {
        let file_proxy =
            io_util::open_file(&dir_proxy, &Path::new(&entry.name), io::OPEN_RIGHT_READABLE)?;
        let (status, contents) = file_proxy
            .read(std::mem::size_of::<u64>() as u64 + 1) // Read one more byte than needed for sanity check
            .await
            .context("failed to call fuchsia.io.File.Read")?;
        if let Err(status) = zx::ok(status) {
            has_error = true;
            t.add_row(row![r->format!("({})", status), entry.name]);
            continue;
        }

        let bytes_for_u64: Result<[u8; 8], _> = contents[..].try_into();
        match bytes_for_u64 {
            Ok(c) => {
                let pkt_count = u64::from_le_bytes(c);
                t.add_row(row![r->pkt_count, entry.name]);
            }
            Err(std::array::TryFromSliceError { .. }) => {
                t.add_row(row![r->format!("(observed {}/{} bytes)", contents.len(), std::mem::size_of::<u64>()), entry.name]);
            }
        }
    }
    t.printstd();
    if has_error {
        Err(err_msg("failed to access statistics"))
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
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

    #[test]
    fn test_display_unit() {
        const KILO: u64 = 1024;

        let mut x: u64 = 249;
        assert_eq!((249, ' '), display_unit(x));
        x = x * (1 + KILO);
        assert_eq!((249, 'K'), display_unit(x));
        x = x * KILO;
        assert_eq!((249, 'M'), display_unit(x));
        x = x * KILO;
        assert_eq!((249, 'G'), display_unit(x));
        x = x * KILO;
        assert_eq!((249, 'T'), display_unit(x));
        x = x * KILO;
        assert_eq!((249, 'P'), display_unit(x));
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_hardware_ethernet as fethernet;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_filter as ffilter;
use fidl_fuchsia_net_neighbor as fneighbor;
use fidl_fuchsia_net_neighbor_ext as fneighbor_ext;
use fidl_fuchsia_net_stack as fstack;
use fidl_fuchsia_net_stack_ext::{self as fstack_ext, FidlReturn as _};
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_zircon_status as zx;
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use log::info;
use netfilter::FidlReturn as _;
use prettytable::{cell, format, row, Row, Table};
use std::str::FromStr as _;

mod opts;
pub use opts::{Command, CommandEnum};

use crate::opts::*;

macro_rules! filter_fidl {
    ($method:expr, $context:expr) => {
        $method.await.transform_result().context($context)
    };
}

fn add_row(t: &mut Table, row: Row) {
    let _: &mut Row = t.add_row(row);
}

/// A network device to be added to the stack.
pub struct Device {
    /// The path to the device in the driver tree.
    pub topological_path: String,
    /// A channel to the device's I/O protocol.
    pub dev: fidl::endpoints::ClientEnd<fethernet::DeviceMarker>,
}

/// An interface for acquiring a proxy to a FIDL service.
#[async_trait::async_trait]
pub trait ServiceConnector<S: fidl::endpoints::ProtocolMarker> {
    /// Acquires a proxy to the parameterized FIDL interface.
    async fn connect(&self) -> Result<S::Proxy, Error>;
}

/// An interface for acquiring all system dependencies required by net-cli.
///
/// FIDL dependencies are specified as supertraits. These supertraits are a complete enumeration of
/// all FIDL dependencies required by net-cli.
#[async_trait::async_trait]
pub trait NetCliDepsConnector:
    ServiceConnector<fstack::StackMarker>
    + ServiceConnector<fnetstack::NetstackMarker>
    + ServiceConnector<ffilter::FilterMarker>
    + ServiceConnector<fstack::LogMarker>
    + ServiceConnector<fneighbor::ControllerMarker>
    + ServiceConnector<fneighbor::ViewMarker>
{
    /// Acquires a connection to the device at the provided path.
    ///
    /// This method exists because this library can make no assumptions about the namespace of its
    /// clients. Implementers which do not have device nodes within their namespace must return an
    /// error.
    ///
    /// The semantics of the path are not fully specified other than that it describes a path to a
    /// device node within the client's namespace. The semantics of path and of this method are
    /// likely to be unstable as Fuchsia system device discovery mechanisms are undergoing active
    /// change.
    async fn connect_device(&self, devfs_path: &str) -> Result<Device, Error>;
}

pub async fn do_root<C: NetCliDepsConnector>(
    Command { cmd }: Command,
    connector: &C,
) -> Result<(), Error> {
    match cmd {
        CommandEnum::If(If { if_cmd: cmd }) => {
            do_if(cmd, connector).await.context("failed during if command")
        }
        CommandEnum::Fwd(Fwd { fwd_cmd: cmd }) => {
            do_fwd(cmd, connector).await.context("failed during fwd command")
        }
        CommandEnum::Route(Route { route_cmd: cmd }) => {
            do_route(cmd, connector).await.context("failed during route command")
        }
        CommandEnum::Filter(Filter { filter_cmd: cmd }) => {
            do_filter(cmd, connector).await.context("failed during filter command")
        }
        CommandEnum::IpFwd(IpFwd { ip_fwd_cmd: cmd }) => {
            do_ip_fwd(cmd, connector).await.context("failed during ip-fwd command")
        }
        CommandEnum::Log(crate::opts::Log { log_cmd: cmd }) => {
            do_log(cmd, connector).await.context("failed during log command")
        }
        CommandEnum::Metric(Metric { metric_cmd: cmd }) => {
            do_metric(cmd, connector).await.context("failed during metric command")
        }
        CommandEnum::Dhcp(Dhcp { dhcp_cmd: cmd }) => {
            do_dhcp(cmd, connector).await.context("failed during dhcp command")
        }
        CommandEnum::Neigh(Neigh { neigh_cmd: cmd }) => {
            do_neigh(cmd, connector).await.context("failed during neigh command")
        }
    }
}

fn shortlist_interfaces(name_pattern: &str, interfaces: &mut Vec<fstack::InterfaceInfo>) {
    interfaces.retain(|i| i.properties.name.contains(name_pattern))
}

async fn tabulate_interfaces_info(interfaces: Vec<fstack::InterfaceInfo>) -> Result<String, Error> {
    let mut t = Table::new();
    t.set_format(format::FormatBuilder::new().padding(2, 2).build());

    for (i, info) in interfaces.into_iter().enumerate() {
        if i > 0 {
            let () = add_row(&mut t, row![]);
        }

        let fstack_ext::InterfaceInfo {
            id,
            properties:
                fstack_ext::InterfaceProperties {
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

        let () = add_row(&mut t, row!["nicid", id]);
        let () = add_row(&mut t, row!["name", name]);
        let () = add_row(&mut t, row!["topopath", topopath]);
        let () = add_row(&mut t, row!["filepath", filepath]);

        let () = if let Some(mac) = mac {
            add_row(&mut t, row!["mac", mac])
        } else {
            add_row(&mut t, row!["mac", "-"])
        };

        let () = add_row(&mut t, row!["mtu", mtu]);
        let () = add_row(&mut t, row!["features", format!("{:?}", features)]);
        let () = add_row(
            &mut t,
            row!["status", format!("{} | {}", administrative_status, physical_status)],
        );
        for addr in addresses {
            let () = add_row(&mut t, row!["addr", addr]);
        }
    }
    Ok(t.to_string())
}

async fn connect_with_context<S, C>(connector: &C) -> Result<S::Proxy, Error>
where
    C: ServiceConnector<S>,
    S: fidl::endpoints::ProtocolMarker,
{
    connector.connect().await.with_context(|| format!("failed to connect to {}", S::NAME))
}

async fn do_if<C: NetCliDepsConnector>(cmd: opts::IfEnum, connector: &C) -> Result<(), Error> {
    match cmd {
        IfEnum::List(IfList { name_pattern }) => {
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            let mut response = stack.list_interfaces().await.context("error getting response")?;
            if let Some(name_pattern) = name_pattern {
                let () = shortlist_interfaces(&name_pattern, &mut response);
            }
            let result = tabulate_interfaces_info(response)
                .await
                .context("error tabulating interface info")?;
            println!("{}", result);
        }
        IfEnum::Add(IfAdd { path }) => match connector.connect_device(&path).await {
            Ok(Device { topological_path, dev }) => {
                let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
                let id = fstack_ext::exec_fidl!(
                    stack.add_ethernet_interface(&topological_path, dev),
                    "error adding interface"
                )?;
                info!("Added interface {}", id);
            }
            Err(e) => {
                println!("{}", e);
            }
        },
        IfEnum::Del(IfDel { id }) => {
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            let () = fstack_ext::exec_fidl!(
                stack.del_ethernet_interface(id),
                "error removing interface"
            )?;
            info!("Deleted interface {}", id);
        }
        IfEnum::Get(IfGet { id }) => {
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            let info =
                fstack_ext::exec_fidl!(stack.get_interface_info(id), "error getting interface")?;
            println!("{}", fstack_ext::InterfaceInfo::from(info));
        }
        IfEnum::IpForward(IfIpForward { cmd }) => {
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            match cmd {
                IfIpForwardEnum::Show(IfIpForwardShow { id, ip_version }) => {
                    let enabled = fstack_ext::exec_fidl!(
                        stack.get_interface_ip_forwarding(id, ip_version),
                        "error getting interface IP forwarding"
                    )?;
                    info!("IP forwarding for {:?} is {} on interface {}", ip_version, enabled, id);
                }
                IfIpForwardEnum::Set(IfIpForwardSet { id, ip_version, enable }) => {
                    let () = fstack_ext::exec_fidl!(
                        stack.set_interface_ip_forwarding(id, ip_version, enable),
                        "error setting interface IP forwarding"
                    )?;
                    info!(
                        "IP forwarding for {:?} set to {} on interface {}",
                        ip_version, enable, id
                    );
                }
            }
        }
        IfEnum::Enable(IfEnable { id }) => {
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            let () =
                fstack_ext::exec_fidl!(stack.enable_interface(id), "error enabling interface")?;
            info!("Interface {} enabled", id);
        }
        IfEnum::Disable(IfDisable { id }) => {
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            let () =
                fstack_ext::exec_fidl!(stack.disable_interface(id), "error disabling interface")?;
            info!("Interface {} disabled", id);
        }
        IfEnum::Addr(IfAddr { addr_cmd }) => {
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            match addr_cmd {
                IfAddrEnum::Add(IfAddrAdd { id, addr, prefix }) => {
                    let parsed_addr = fnet_ext::IpAddress::from_str(&addr)?.into();
                    let mut fidl_addr = fnet::Subnet { addr: parsed_addr, prefix_len: prefix };
                    let () = fstack_ext::exec_fidl!(
                        stack.add_interface_address(id, &mut fidl_addr),
                        "error adding interface address"
                    )?;
                    info!(
                        "Address {} added to interface {}",
                        fnet_ext::Subnet::from(fidl_addr),
                        id
                    );
                }
                IfAddrEnum::Del(IfAddrDel { id, addr, prefix }) => {
                    let parsed_addr = fnet_ext::IpAddress::from_str(&addr)?.into();
                    let prefix_len = prefix.unwrap_or_else(|| match parsed_addr {
                        fnet::IpAddress::Ipv4(_) => 32,
                        fnet::IpAddress::Ipv6(_) => 128,
                    });
                    let mut fidl_addr = fnet::Subnet { addr: parsed_addr, prefix_len };
                    let () = fstack_ext::exec_fidl!(
                        stack.del_interface_address(id, &mut fidl_addr),
                        "error deleting interface address"
                    )?;
                    info!(
                        "Address {} deleted from interface {}",
                        fnet_ext::Subnet::from(fidl_addr),
                        id
                    );
                }
            }
        }
        IfEnum::Bridge(IfBridge { ids }) => {
            let netstack = connect_with_context::<fnetstack::NetstackMarker, _>(connector).await?;
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

async fn do_fwd<C: NetCliDepsConnector>(cmd: opts::FwdEnum, connector: &C) -> Result<(), Error> {
    let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
    match cmd {
        FwdEnum::List(_) => {
            let response =
                stack.get_forwarding_table().await.context("error retrieving forwarding table")?;
            for entry in response {
                println!("{}", fstack_ext::ForwardingEntry::from(entry));
            }
        }
        FwdEnum::AddDevice(FwdAddDevice { id, addr, prefix }) => {
            let mut entry = fstack::ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: fnet_ext::IpAddress::from_str(&addr)?.into(),
                    prefix_len: prefix,
                },
                destination: fstack::ForwardingDestination::DeviceId(id),
            };
            let () = fstack_ext::exec_fidl!(
                stack.add_forwarding_entry(&mut entry),
                "error adding device forwarding entry"
            )?;
            info!("Added forwarding entry for {}/{} to device {}", addr, prefix, id);
        }
        FwdEnum::AddHop(FwdAddHop { next_hop, addr, prefix }) => {
            let mut entry = fstack::ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: fnet_ext::IpAddress::from_str(&addr)?.into(),
                    prefix_len: prefix,
                },
                destination: fstack::ForwardingDestination::NextHop(
                    fnet_ext::IpAddress::from_str(&next_hop)?.into(),
                ),
            };
            let () = fstack_ext::exec_fidl!(
                stack.add_forwarding_entry(&mut entry),
                "error adding next-hop forwarding entry"
            )?;
            info!("Added forwarding entry for {}/{} to {}", addr, prefix, next_hop);
        }
        FwdEnum::Del(FwdDel { addr, prefix }) => {
            let mut entry = fnet::Subnet {
                addr: fnet_ext::IpAddress::from_str(&addr)?.into(),
                prefix_len: prefix,
            };
            let () = fstack_ext::exec_fidl!(
                stack.del_forwarding_entry(&mut entry),
                "error removing forwarding entry"
            )?;
            info!("Removed forwarding entry for {}/{}", addr, prefix);
        }
    }
    Ok(())
}

async fn do_route<C: NetCliDepsConnector>(
    cmd: opts::RouteEnum,
    connector: &C,
) -> Result<(), Error> {
    let netstack = connect_with_context::<fnetstack::NetstackMarker, _>(connector).await?;
    match cmd {
        RouteEnum::List(RouteList {}) => {
            let response =
                netstack.get_route_table().await.context("error retrieving routing table")?;

            let mut t = Table::new();
            t.set_format(format::FormatBuilder::new().padding(2, 2).build());

            t.set_titles(row!["Destination", "Gateway", "NICID", "Metric"]);
            for entry in response {
                let fidl_fuchsia_netstack_ext::RouteTableEntry {
                    destination,
                    gateway,
                    nicid,
                    metric,
                } = entry.into();
                let gateway = match gateway {
                    None => "-".to_string(),
                    Some(g) => format!("{}", g),
                };
                let () = add_row(&mut t, row![destination, gateway, nicid, metric]);
            }

            let _lines_printed: usize = t.printstd();
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
    let () = zx::Status::ok(netstack.start_route_table_transaction(server_end).await?)?;
    let status = func(&route_table).await?;
    let () = zx::Status::ok(status)?;
    Ok(())
}

async fn do_filter<C: NetCliDepsConnector>(
    cmd: opts::FilterEnum,
    connector: &C,
) -> Result<(), Error> {
    let filter = connect_with_context::<ffilter::FilterMarker, _>(connector).await?;
    match cmd {
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

async fn do_ip_fwd<C: NetCliDepsConnector>(
    cmd: opts::IpFwdEnum,
    connector: &C,
) -> Result<(), Error> {
    let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
    match cmd {
        IpFwdEnum::Enable(IpFwdEnable {}) => {
            let () = stack
                .enable_ip_forwarding()
                .await
                .context("fuchsia.net.stack/Stack.EnableIpForwarding FIDL error")?;
            info!("Enabled IP forwarding");
        }
        IpFwdEnum::Disable(IpFwdDisable {}) => {
            let () = stack
                .disable_ip_forwarding()
                .await
                .context("fuchsia.net.stack/Stack.DisableIpForwarding FIDL error")?;
            info!("Disabled IP forwarding");
        }
    }
    Ok(())
}

async fn do_log<C: NetCliDepsConnector>(cmd: opts::LogEnum, connector: &C) -> Result<(), Error> {
    let log = connect_with_context::<fstack::LogMarker, _>(connector).await?;
    match cmd {
        LogEnum::SetPackets(LogSetPackets { enabled }) => {
            let () = log.set_log_packets(enabled).await.context("error setting log packets")?;
            info!("log packets set to {:?}", enabled);
        }
    }
    Ok(())
}

async fn do_metric<C: NetCliDepsConnector>(
    cmd: opts::MetricEnum,
    connector: &C,
) -> Result<(), Error> {
    match cmd {
        MetricEnum::Set(MetricSet { id, metric }) => {
            let netstack = connect_with_context::<fnetstack::NetstackMarker, _>(connector).await?;
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

async fn do_dhcp<C: NetCliDepsConnector>(cmd: opts::DhcpEnum, connector: &C) -> Result<(), Error> {
    let netstack = connect_with_context::<fnetstack::NetstackMarker, _>(connector).await?;
    let (dhcp, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_dhcp::ClientMarker>()?;
    match cmd {
        DhcpEnum::Start(DhcpStart { id }) => {
            let () =
                netstack.get_dhcp_client(id, server_end).await?.map_err(zx::Status::from_raw)?;
            let () = dhcp.start().await?.map_err(zx::Status::from_raw)?;
            info!("dhcp client started on interface {}", id);
        }
        DhcpEnum::Stop(DhcpStop { id }) => {
            let () =
                netstack.get_dhcp_client(id, server_end).await?.map_err(zx::Status::from_raw)?;
            let () = dhcp.stop().await?.map_err(zx::Status::from_raw)?;
            info!("dhcp client stopped on interface {}", id);
        }
    }
    Ok(())
}

async fn do_neigh<C: NetCliDepsConnector>(
    cmd: opts::NeighEnum,
    connector: &C,
) -> Result<(), Error> {
    match cmd {
        NeighEnum::Add(NeighAdd { interface, ip, mac }) => {
            let controller =
                connect_with_context::<fneighbor::ControllerMarker, _>(connector).await?;
            let () = do_neigh_add(interface, ip.into(), mac.into(), controller)
                .await
                .context("failed during neigh add command")?;
            info!("Added entry ({}, {}) for interface {}", ip, mac, interface);
        }
        NeighEnum::Clear(NeighClear { interface, ip_version }) => {
            let controller =
                connect_with_context::<fneighbor::ControllerMarker, _>(connector).await?;
            let () = do_neigh_clear(interface, ip_version, controller)
                .await
                .context("failed during neigh clear command")?;
            info!("Cleared entries for interface {}", interface);
        }
        NeighEnum::Del(NeighDel { interface, ip }) => {
            let controller =
                connect_with_context::<fneighbor::ControllerMarker, _>(connector).await?;
            let () = do_neigh_del(interface, ip.into(), controller)
                .await
                .context("failed during neigh del command")?;
            info!("Deleted entry {} for interface {}", ip, interface);
        }
        NeighEnum::List(NeighList {}) => {
            let view = connect_with_context::<fneighbor::ViewMarker, _>(connector).await?;
            let () = print_neigh_entries(false /* watch_for_changes */, view)
                .await
                .context("error listing neighbor entries")?;
        }
        NeighEnum::Watch(NeighWatch {}) => {
            let view = connect_with_context::<fneighbor::ViewMarker, _>(connector).await?;
            let () = print_neigh_entries(true /* watch_for_changes */, view)
                .await
                .context("error watching for changes to the neighbor table")?;
        }
        NeighEnum::Config(NeighConfig { neigh_config_cmd }) => match neigh_config_cmd {
            NeighConfigEnum::Get(NeighGetConfig { interface, ip_version }) => {
                let view = connect_with_context::<fneighbor::ViewMarker, _>(connector).await?;
                let () = print_neigh_config(interface, ip_version, view)
                    .await
                    .context("failed during neigh config get command")?;
            }
            NeighConfigEnum::Update(NeighUpdateConfig {
                interface,
                ip_version,
                base_reachable_time,
                learn_base_reachable_time,
                min_random_factor,
                max_random_factor,
                retransmit_timer,
                learn_retransmit_timer,
                delay_first_probe_time,
                max_multicast_probes,
                max_unicast_probes,
                max_anycast_delay_time,
                max_reachability_confirmations,
            }) => {
                let updates = fneighbor::UnreachabilityConfig {
                    base_reachable_time,
                    learn_base_reachable_time,
                    min_random_factor,
                    max_random_factor,
                    retransmit_timer,
                    learn_retransmit_timer,
                    delay_first_probe_time,
                    max_multicast_probes,
                    max_unicast_probes,
                    max_anycast_delay_time,
                    max_reachability_confirmations,
                    ..fneighbor::UnreachabilityConfig::EMPTY
                };
                let controller =
                    connect_with_context::<fneighbor::ControllerMarker, _>(connector).await?;
                let () = update_neigh_config(interface, ip_version, updates, controller)
                    .await
                    .context("failed during neigh config update command")?;
                info!("Updated config for interface {}", interface);
            }
        },
    }
    Ok(())
}

async fn do_neigh_add(
    interface: u64,
    neighbor: fnet::IpAddress,
    mac: fnet::MacAddress,
    controller: fneighbor::ControllerProxy,
) -> Result<(), Error> {
    controller
        .add_entry(interface, &mut neighbor.into(), &mut mac.into())
        .await
        .context("FIDL error adding neighbor entry")?
        .map_err(zx::Status::from_raw)
        .context("error adding neighbor entry")
}

async fn do_neigh_clear(
    interface: u64,
    ip_version: fnet::IpVersion,
    controller: fneighbor::ControllerProxy,
) -> Result<(), Error> {
    controller
        .clear_entries(interface, ip_version)
        .await
        .context("FIDL error clearing neighbor table")?
        .map_err(zx::Status::from_raw)
        .context("error clearing neighbor table")
}

async fn do_neigh_del(
    interface: u64,
    neighbor: fnet::IpAddress,
    controller: fneighbor::ControllerProxy,
) -> Result<(), Error> {
    controller
        .remove_entry(interface, &mut neighbor.into())
        .await
        .context("FIDL error removing neighbor entry")?
        .map_err(zx::Status::from_raw)
        .context("error removing neighbor entry")
}

async fn print_neigh_entries(
    watch_for_changes: bool,
    view: fneighbor::ViewProxy,
) -> Result<(), Error> {
    let (it_client, it_server) =
        fidl::endpoints::create_endpoints::<fneighbor::EntryIteratorMarker>()
            .context("error creating channel for entry iterator")?;
    let it = it_client.into_proxy().context("error creating proxy to entry iterator")?;

    let () = view
        .open_entry_iterator(it_server, fneighbor::EntryIteratorOptions::EMPTY)
        .context("error opening a connection to the entry iterator")?;

    neigh_entry_stream(it, watch_for_changes)
        .map_ok(|item| {
            write_neigh_entry(&mut std::io::stdout(), item, watch_for_changes)
                .context("error writing entry")
        })
        .try_fold((), |(), r| futures::future::ready(r))
        .await
}

async fn print_neigh_config(
    interface: u64,
    version: fnet::IpVersion,
    view: fneighbor::ViewProxy,
) -> Result<(), Error> {
    let config = view
        .get_unreachability_config(interface, version)
        .await
        .context("get_unreachability_config FIDL error")?
        .map_err(zx::Status::from_raw)
        .context("get_unreachability_config failed")?;

    println!("{:#?}", config);
    Ok(())
}

async fn update_neigh_config(
    interface: u64,
    version: fnet::IpVersion,
    updates: fneighbor::UnreachabilityConfig,
    controller: fneighbor::ControllerProxy,
) -> Result<(), Error> {
    controller
        .update_unreachability_config(interface, version, updates)
        .await
        .context("update_unreachability_config FIDL error")?
        .map_err(zx::Status::from_raw)
        .context("update_unreachability_config failed")
}

fn neigh_entry_stream(
    iterator: fneighbor::EntryIteratorProxy,
    watch_for_changes: bool,
) -> impl futures::Stream<Item = Result<fneighbor::EntryIteratorItem, Error>> {
    futures::stream::try_unfold(iterator, |iterator| {
        iterator
            .get_next()
            .map_ok(|items| Some((items, iterator)))
            .map(|r| r.context("error getting items from iterator"))
    })
    .map_ok(|items| futures::stream::iter(items.into_iter().map(Ok)))
    .try_flatten()
    .take_while(move |item| {
        futures::future::ready(item.as_ref().map_or(false, |item| {
            if let fneighbor::EntryIteratorItem::Idle(fneighbor::IdleEvent {}) = item {
                watch_for_changes
            } else {
                true
            }
        }))
    })
}

fn write_neigh_entry<W: std::io::Write>(
    f: &mut W,
    item: fneighbor::EntryIteratorItem,
    watch_for_changes: bool,
) -> Result<(), std::io::Error> {
    match item {
        fneighbor::EntryIteratorItem::Existing(entry) => {
            if watch_for_changes {
                writeln!(f, "EXISTING | {}", fneighbor_ext::Entry::from(entry))
            } else {
                writeln!(f, "{}", fneighbor_ext::Entry::from(entry))
            }
        }
        fneighbor::EntryIteratorItem::Idle(fneighbor::IdleEvent {}) => writeln!(f, "IDLE"),
        fneighbor::EntryIteratorItem::Added(entry) => {
            writeln!(f, "ADDED    | {}", fneighbor_ext::Entry::from(entry))
        }
        fneighbor::EntryIteratorItem::Changed(entry) => {
            writeln!(f, "CHANGED  | {}", fneighbor_ext::Entry::from(entry))
        }
        fneighbor::EntryIteratorItem::Removed(entry) => {
            writeln!(f, "REMOVED  | {}", fneighbor_ext::Entry::from(entry))
        }
    }
}

#[cfg(test)]
mod tests {
    use anyhow::Error;
    use fidl::endpoints::ProtocolMarker;
    use fidl_fuchsia_net as fnet;
    use fuchsia_async::{self as fasync, TimeoutExt as _};
    use futures::prelude::*;
    use net_declare::{fidl_mac, fidl_subnet};
    use std::convert::TryFrom as _;
    use {super::*, fidl_fuchsia_net_stack::*, fidl_fuchsia_netstack::*};

    const SUBNET_V4: fnet::Subnet = fidl_subnet!("192.168.0.1/32");
    const SUBNET_V6: fnet::Subnet = fidl_subnet!("fd00::1/128");

    const MAC_1: fnet::MacAddress = fidl_mac!("01:02:03:04:05:06");
    const MAC_2: fnet::MacAddress = fidl_mac!("02:03:04:05:06:07");

    struct TestConnector {
        stack: Option<StackProxy>,
        netstack: Option<NetstackProxy>,
    }

    #[async_trait::async_trait]
    impl ServiceConnector<StackMarker> for TestConnector {
        async fn connect(&self) -> Result<<fstack::StackMarker as ProtocolMarker>::Proxy, Error> {
            match &self.stack {
                Some(stack) => Ok(stack.clone()),
                None => Err(anyhow::anyhow!("connector has no stack instance")),
            }
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<NetstackMarker> for TestConnector {
        async fn connect(
            &self,
        ) -> Result<<fnetstack::NetstackMarker as ProtocolMarker>::Proxy, Error> {
            match &self.netstack {
                Some(netstack) => Ok(netstack.clone()),
                None => Err(anyhow::anyhow!("connector has no netstack instance")),
            }
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<ffilter::FilterMarker> for TestConnector {
        async fn connect(&self) -> Result<<ffilter::FilterMarker as ProtocolMarker>::Proxy, Error> {
            Err(anyhow::anyhow!("connect filter unimplemented for test connector"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<LogMarker> for TestConnector {
        async fn connect(&self) -> Result<<fstack::LogMarker as ProtocolMarker>::Proxy, Error> {
            Err(anyhow::anyhow!("connect log unimplemented for test connector"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<fneighbor::ControllerMarker> for TestConnector {
        async fn connect(
            &self,
        ) -> Result<<fneighbor::ControllerMarker as ProtocolMarker>::Proxy, Error> {
            Err(anyhow::anyhow!("connect neighbor controller unimplemented for test connector"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<fneighbor::ViewMarker> for TestConnector {
        async fn connect(&self) -> Result<<fneighbor::ViewMarker as ProtocolMarker>::Proxy, Error> {
            Err(anyhow::anyhow!("connect neighbor view unimplemented for test connector"))
        }
    }

    #[async_trait::async_trait]
    impl NetCliDepsConnector for TestConnector {
        async fn connect_device(&self, _path: &str) -> Result<Device, Error> {
            Err(anyhow::anyhow!("connect interface unimplmented for test connector"))
        }
    }

    fn get_fake_interface(id: u64, name: &str) -> InterfaceInfo {
        InterfaceInfo {
            id,
            properties: InterfaceProperties {
                name: name.to_string(),
                topopath: "loopback".to_string(),
                filepath: "[none]".to_string(),
                mac: None,
                mtu: 65536,
                features: fethernet::Features::Loopback,
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
    async fn test_if_ip_forward() {
        const IP_FORWARD: bool = true;

        let (stack_controller, mut stack_requests) =
            fidl::endpoints::create_proxy_and_stream::<StackMarker>().unwrap();
        let (netstack_controller, _netstack_requests) =
            fidl::endpoints::create_proxy_and_stream::<NetstackMarker>().unwrap();
        let connector =
            TestConnector { stack: Some(stack_controller), netstack: Some(netstack_controller) };

        let do_if_fut = do_if(
            opts::IfEnum::IpForward(IfIpForward {
                cmd: IfIpForwardEnum::Set(IfIpForwardSet {
                    id: INTERFACE_ID,
                    ip_version: IP_VERSION,
                    enable: IP_FORWARD,
                }),
            }),
            &connector,
        );
        let requests_fut = async {
            let (got_interface_id, got_ip_version, got_enable, responder) = stack_requests
                .try_next()
                .await
                .expect("stack FIDL error")
                .expect("request stream should not have ended")
                .into_set_interface_ip_forwarding()
                .expect("request should be of type SetInterfaceIpForwarding");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_version, IP_VERSION);
            assert_eq!(got_enable, IP_FORWARD);
            let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(do_if_fut, requests_fut)
            .await
            .expect("setting interface ip forwarding should succeed");

        let do_if_fut = do_if(
            opts::IfEnum::IpForward(IfIpForward {
                cmd: IfIpForwardEnum::Show(IfIpForwardShow {
                    id: INTERFACE_ID,
                    ip_version: IP_VERSION,
                }),
            }),
            &connector,
        );
        let requests_fut = async {
            let (got_interface_id, got_ip_version, responder) = stack_requests
                .try_next()
                .await
                .expect("stack FIDL error")
                .expect("request stream should not have ended")
                .into_get_interface_ip_forwarding()
                .expect("request should be of type InterfaceIpForwarding");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_version, IP_VERSION);
            let () = responder.send(&mut Ok(true)).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(do_if_fut, requests_fut)
            .await
            .expect("getting interface ip forwarding should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_if_del_addr() {
        async fn next_request(
            requests: &mut StackRequestStream,
        ) -> (u64, fnet::Subnet, StackDelInterfaceAddressResponder) {
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
        let connector = TestConnector { stack: Some(stack), netstack: Some(netstack) };

        // Make the first request.
        let succeeds = do_if(
            IfEnum::Addr(IfAddr {
                addr_cmd: IfAddrEnum::Del(IfAddrDel {
                    id: 1,
                    addr: fnet_ext::IpAddress::from(SUBNET_V4.addr).to_string(),
                    prefix: None, // The prefix should be set to the default of 32 for IPv4.
                }),
            }),
            &connector,
        );
        let success_response = async {
            // Verify that the first request is as expected and return OK.
            let (id, addr, responder) = next_request(&mut requests).await;
            assert_eq!(id, 1);
            assert_eq!(addr, SUBNET_V4);
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
                    addr: fnet_ext::IpAddress::from(SUBNET_V6.addr).to_string(),
                    prefix: None, // The prefix should be set to the default of 128 for IPv6.
                }),
            }),
            &connector,
        );
        let fail_response = async {
            // Verify that the second request is as expected and return a NotFound error.
            let (id, addr, responder) = next_request(&mut requests).await;
            assert_eq!(id, 2);
            assert_eq!(addr, SUBNET_V6);
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
        let connector = TestConnector { stack: None, netstack: Some(netstack) };

        // both test values have been arbitrarily selected
        let expected_id = 1;
        let expected_metric = 64;
        let succeeds = do_metric(
            MetricEnum::Set(MetricSet { id: expected_id, metric: expected_metric }),
            &connector,
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
        let connector = TestConnector { stack: None, netstack: Some(netstack) };
        let op = do_dhcp(cmd.clone(), &connector);
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
            let () = netstack_responder
                .send(&mut Ok(()))
                .expect("netstack_responder.send should succeed");
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
        let connector = TestConnector { stack: None, netstack: Some(netstack) };
        let op = do_route(cmd.clone(), &connector);
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
            let () = netstack_responder
                .send(zx::Status::OK.into_raw())
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
                    responder.send(zx::Status::OK.into_raw())
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
                    responder.send(zx::Status::OK.into_raw())
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
        let connector = TestConnector { stack: Some(stack), netstack: Some(netstack) };
        // interface id test values have been selected arbitrarily
        let bridge_ifs = vec![1, 2, 3];
        let bridge_id = 4;
        let bridge = do_if(IfEnum::Bridge(IfBridge { ids: bridge_ifs.clone() }), &connector);
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

    async fn test_get_neigh_entries(
        watch_for_changes: bool,
        batches: Vec<Vec<fneighbor::EntryIteratorItem>>,
        want: String,
    ) {
        let (it, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::EntryIteratorMarker>().unwrap();

        let server = async {
            for mut items in batches {
                let responder = requests
                    .try_next()
                    .await
                    .expect("neigh FIDL error")
                    .expect("request stream should not have ended")
                    .into_get_next()
                    .expect("request should be of type GetNext");
                let () =
                    responder.send(&mut items.iter_mut()).expect("responder.send should succeed");
            }
        }
        .on_timeout(std::time::Duration::from_secs(60), || panic!("server responder timed out"));

        let client = async {
            let mut stream = neigh_entry_stream(it, watch_for_changes);

            let item_to_string = |item| {
                let mut buf = Vec::new();
                let () = write_neigh_entry(&mut buf, item, watch_for_changes)
                    .expect("write_neigh_entry should succeed");
                String::from_utf8(buf).expect("string should be UTF-8")
            };

            // Check each string sent by get_neigh_entries
            for want_line in want.lines() {
                let got = stream
                    .next()
                    .await
                    .map(|item| item_to_string(item.expect("neigh_entry_stream should succeed")));
                assert_eq!(got, Some(format!("{}\n", want_line)));
            }

            // When listing entries, the sender should close after sending all existing entries.
            if !watch_for_changes {
                match stream.next().await {
                    Some(Ok(item)) => {
                        panic!("unexpected item from stream: {}", item_to_string(item))
                    }
                    Some(Err(err)) => panic!("unexpected error from stream: {}", err),
                    None => {}
                }
            }
        };

        let ((), ()) = futures::future::join(client, server).await;
    }

    async fn test_neigh_none(watch_for_changes: bool, want: String) {
        test_get_neigh_entries(
            watch_for_changes,
            vec![vec![fneighbor::EntryIteratorItem::Idle(fneighbor::IdleEvent {})]],
            want,
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_list_none() {
        test_neigh_none(false /* watch_for_changes */, "".to_string()).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_watch_none() {
        test_neigh_none(true /* watch_for_changes */, "IDLE".to_string()).await
    }

    fn timestamp_60s_ago() -> i64 {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::SystemTime::UNIX_EPOCH)
            .expect("failed to get duration since epoch");
        let past = now - std::time::Duration::from_secs(60);
        i64::try_from(past.as_nanos()).expect("failed to convert duration to i64")
    }

    async fn test_neigh_one(watch_for_changes: bool, want: fn(fneighbor_ext::Entry) -> String) {
        fn new_entry(updated_at: i64) -> fneighbor::Entry {
            fneighbor::Entry {
                interface: Some(1),
                neighbor: Some(SUBNET_V4.addr),
                state: Some(fneighbor::EntryState::Reachable),
                mac: Some(MAC_1),
                updated_at: Some(updated_at),
                ..fneighbor::Entry::EMPTY
            }
        }

        let updated_at = timestamp_60s_ago();

        test_get_neigh_entries(
            watch_for_changes,
            vec![vec![
                fneighbor::EntryIteratorItem::Existing(new_entry(updated_at)),
                fneighbor::EntryIteratorItem::Idle(fneighbor::IdleEvent {}),
            ]],
            want(fneighbor_ext::Entry::from(new_entry(updated_at))),
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_list_one() {
        test_neigh_one(false /* watch_for_changes */, |entry| format!("{}\n", entry)).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_watch_one() {
        test_neigh_one(true /* watch_for_changes */, |entry| {
            format!(
                "EXISTING | {}\n\
                 IDLE\n",
                entry
            )
        })
        .await
    }

    async fn test_neigh_many(
        watch_for_changes: bool,
        want: fn(fneighbor_ext::Entry, fneighbor_ext::Entry) -> String,
    ) {
        fn new_entry(
            ip: fnet::IpAddress,
            mac: fnet::MacAddress,
            updated_at: i64,
        ) -> fneighbor::Entry {
            fneighbor::Entry {
                interface: Some(1),
                neighbor: Some(ip),
                state: Some(fneighbor::EntryState::Reachable),
                mac: Some(mac),
                updated_at: Some(updated_at),
                ..fneighbor::Entry::EMPTY
            }
        }

        let updated_at = timestamp_60s_ago();
        let offset = i64::try_from(std::time::Duration::from_secs(60).as_nanos())
            .expect("failed to convert duration to i64");

        test_get_neigh_entries(
            watch_for_changes,
            vec![vec![
                fneighbor::EntryIteratorItem::Existing(new_entry(
                    SUBNET_V4.addr,
                    MAC_1,
                    updated_at,
                )),
                fneighbor::EntryIteratorItem::Existing(new_entry(
                    SUBNET_V6.addr,
                    MAC_2,
                    updated_at - offset,
                )),
                fneighbor::EntryIteratorItem::Idle(fneighbor::IdleEvent {}),
            ]],
            want(
                fneighbor_ext::Entry::from(new_entry(SUBNET_V4.addr, MAC_1, updated_at)),
                fneighbor_ext::Entry::from(new_entry(SUBNET_V6.addr, MAC_2, updated_at - offset)),
            ),
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_list_many() {
        test_neigh_many(false /* watch_for_changes */, |a, b| format!("{}\n{}\n", a, b)).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_watch_many() {
        test_neigh_many(true /* watch_for_changes */, |a, b| {
            format!(
                "EXISTING | {}\n\
                 EXISTING | {}\n\
                 IDLE\n",
                a, b
            )
        })
        .await
    }

    const INTERFACE_ID: u64 = 1;
    const IP_VERSION: fnet::IpVersion = fnet::IpVersion::V4;

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_add() {
        let (controller, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::ControllerMarker>().unwrap();
        let neigh = do_neigh_add(INTERFACE_ID, SUBNET_V4.addr, MAC_1, controller);
        let neigh_succeeds = async {
            let (got_interface_id, got_ip_address, got_mac, responder) = requests
                .try_next()
                .await
                .expect("neigh FIDL error")
                .expect("request stream should not have ended")
                .into_add_entry()
                .expect("request should be of type AddEntry");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_address, SUBNET_V4.addr);
            assert_eq!(got_mac, MAC_1);
            let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(neigh, neigh_succeeds)
            .await
            .expect("neigh add should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_clear() {
        let (controller, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::ControllerMarker>().unwrap();
        let neigh = do_neigh_clear(INTERFACE_ID, IP_VERSION, controller);
        let neigh_succeeds = async {
            let (got_interface_id, got_ip_version, responder) = requests
                .try_next()
                .await
                .expect("neigh FIDL error")
                .expect("request stream should not have ended")
                .into_clear_entries()
                .expect("request should be of type ClearEntries");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_version, IP_VERSION);
            let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(neigh, neigh_succeeds)
            .await
            .expect("neigh clear should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_del() {
        let (controller, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::ControllerMarker>().unwrap();
        let neigh = do_neigh_del(INTERFACE_ID, SUBNET_V4.addr, controller);
        let neigh_succeeds = async {
            let (got_interface_id, got_ip_address, responder) = requests
                .try_next()
                .await
                .expect("neigh FIDL error")
                .expect("request stream should not have ended")
                .into_remove_entry()
                .expect("request should be of type RemoveEntry");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_address, SUBNET_V4.addr);
            let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(neigh, neigh_succeeds)
            .await
            .expect("neigh remove should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_config_get() {
        let (view, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::ViewMarker>()
                .expect("creating a request stream and proxy for testing should succeed");
        let neigh = print_neigh_config(INTERFACE_ID, IP_VERSION, view);
        let neigh_succeeds = async {
            let (got_interface_id, got_ip_version, responder) = requests
                .try_next()
                .await
                .expect("neigh FIDL error")
                .expect("request stream should not have ended")
                .into_get_unreachability_config()
                .expect("request should be of type GetUnreachabilityConfig");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_version, IP_VERSION);
            let () = responder
                .send(&mut Ok(fneighbor::UnreachabilityConfig::EMPTY))
                .expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(neigh, neigh_succeeds)
            .await
            .expect("neigh config get should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_neigh_config_update() {
        const CONFIG: fneighbor::UnreachabilityConfig = fneighbor::UnreachabilityConfig::EMPTY;

        let (controller, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::ControllerMarker>()
                .expect("creating a request stream and proxy for testing should succeed");
        let neigh = update_neigh_config(
            INTERFACE_ID,
            IP_VERSION,
            fneighbor::UnreachabilityConfig::EMPTY,
            controller,
        );
        let neigh_succeeds = async {
            let (got_interface_id, got_ip_version, got_config, responder) = requests
                .try_next()
                .await
                .expect("neigh FIDL error")
                .expect("request stream should not have ended")
                .into_update_unreachability_config()
                .expect("request should be of type UpdateUnreachabilityConfig");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_version, IP_VERSION);
            assert_eq!(got_config, CONFIG);
            let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(neigh, neigh_succeeds)
            .await
            .expect("neigh config update should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ip_fwd_enable() {
        let (stack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fstack::StackMarker>()
                .expect("creating a request stream and proxy for testing should succeed");
        let connector = TestConnector { stack: Some(stack), netstack: None };
        let op = do_ip_fwd(IpFwdEnum::Enable(IpFwdEnable {}), &connector);
        let op_succeeds = async {
            let responder = requests
                .try_next()
                .await
                .expect("ip-fwd FIDL error")
                .expect("request stream should not have ended")
                .into_enable_ip_forwarding()
                .expect("request should be of type EnableIpForwarding");
            let () = responder.send().expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) =
            futures::future::try_join(op, op_succeeds).await.expect("fwd enable should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ip_fwd_disable() {
        let (stack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fstack::StackMarker>()
                .expect("creating a request stream and proxy for testing should succeed");
        let connector = TestConnector { stack: Some(stack), netstack: None };
        let op = do_ip_fwd(IpFwdEnum::Disable(IpFwdDisable {}), &connector);
        let op_succeeds = async {
            let responder = requests
                .try_next()
                .await
                .expect("fwd FIDL error")
                .expect("request stream should not have ended")
                .into_disable_ip_forwarding()
                .expect("request should be of type DisableIpForwarding");
            let () = responder.send().expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) =
            futures::future::try_join(op, op_succeeds).await.expect("fwd disable should succeed");
    }
}

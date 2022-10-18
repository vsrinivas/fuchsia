// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_hardware_ethernet as fethernet;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fdebug;
use fidl_fuchsia_net_dhcp as fdhcp;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_filter as ffilter;
use fidl_fuchsia_net_interfaces as finterfaces;
use fidl_fuchsia_net_interfaces_admin as finterfaces_admin;
use fidl_fuchsia_net_interfaces_ext as finterfaces_ext;
use fidl_fuchsia_net_name as fname;
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
use serde_json::{json, value::Value};
use std::collections::hash_map::HashMap;
use std::{convert::TryFrom as _, iter::FromIterator as _, str::FromStr as _};

mod opts;
pub use opts::{
    underlying_user_facing_error, user_facing_error, Command, CommandEnum, UserFacingError,
};

mod ser;

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
    ServiceConnector<fdebug::InterfacesMarker>
    + ServiceConnector<fdhcp::Server_Marker>
    + ServiceConnector<ffilter::FilterMarker>
    + ServiceConnector<finterfaces::StateMarker>
    + ServiceConnector<fneighbor::ControllerMarker>
    + ServiceConnector<fneighbor::ViewMarker>
    + ServiceConnector<fnetstack::NetstackMarker>
    + ServiceConnector<fstack::LogMarker>
    + ServiceConnector<fstack::StackMarker>
    + ServiceConnector<fname::LookupMarker>
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
    mut out: ffx_writer::Writer,
    Command { cmd }: Command,
    connector: &C,
) -> Result<(), Error> {
    match cmd {
        CommandEnum::If(opts::If { if_cmd: cmd }) => {
            do_if(&mut out, cmd, connector).await.context("failed during if command")
        }
        CommandEnum::Route(opts::Route { route_cmd: cmd }) => {
            do_route(&mut out, cmd, connector).await.context("failed during route command")
        }
        CommandEnum::Filter(opts::Filter { filter_cmd: cmd }) => {
            do_filter(out, cmd, connector).await.context("failed during filter command")
        }
        CommandEnum::Log(opts::Log { log_cmd: cmd }) => {
            do_log(cmd, connector).await.context("failed during log command")
        }
        CommandEnum::Dhcp(opts::Dhcp { dhcp_cmd: cmd }) => {
            do_dhcp(cmd, connector).await.context("failed during dhcp command")
        }
        CommandEnum::Dhcpd(opts::dhcpd::Dhcpd { dhcpd_cmd: cmd }) => {
            do_dhcpd(cmd, connector).await.context("failed during dhcpd command")
        }
        CommandEnum::Neigh(opts::Neigh { neigh_cmd: cmd }) => {
            do_neigh(out, cmd, connector).await.context("failed during neigh command")
        }
        CommandEnum::Dns(opts::dns::Dns { dns_cmd: cmd }) => {
            do_dns(out, cmd, connector).await.context("failed during dns command")
        }
    }
}

fn shortlist_interfaces(
    name_pattern: &str,
    interfaces: &mut HashMap<u64, finterfaces_ext::Properties>,
) {
    interfaces.retain(
        |_: &u64,
         finterfaces_ext::Properties {
             id: _,
             name,
             device_class: _,
             online: _,
             addresses: _,
             has_default_ipv4_route: _,
             has_default_ipv6_route: _,
         }| name.contains(name_pattern),
    )
}

fn write_tabulated_interfaces_info<
    W: std::io::Write,
    I: IntoIterator<Item = ser::InterfaceView>,
>(
    mut out: W,
    interfaces: I,
) -> Result<(), Error> {
    let mut t = Table::new();
    t.set_format(format::FormatBuilder::new().padding(2, 2).build());
    for (i, ser::InterfaceView { nicid, name, device_class, online, addresses, mac }) in
        interfaces.into_iter().enumerate()
    {
        if i > 0 {
            let () = add_row(&mut t, row![]);
        }

        let () = add_row(&mut t, row!["nicid", nicid]);
        let () = add_row(&mut t, row!["name", name]);
        let () = add_row(
            &mut t,
            row![
                "device class",
                match device_class {
                    ser::DeviceClass::Loopback => "loopback",
                    ser::DeviceClass::Virtual => "virtual",
                    ser::DeviceClass::Ethernet => "ethernet",
                    ser::DeviceClass::Wlan => "wlan",
                    ser::DeviceClass::Ppp => "ppp",
                    ser::DeviceClass::Bridge => "bridge",
                    ser::DeviceClass::WlanAp => "wlan-ap",
                }
            ],
        );
        let () = add_row(&mut t, row!["online", online]);
        let ser::Addresses { ipv4, ipv6 } = addresses;
        for ser::Subnet { addr, prefix_len } in ipv4 {
            let () = add_row(&mut t, row!["addr", format!("{}/{}", addr, prefix_len)]);
        }
        for ser::Subnet { addr, prefix_len } in ipv6 {
            let () = add_row(&mut t, row!["addr", format!("{}/{}", addr, prefix_len)]);
        }
        match mac {
            None => add_row(&mut t, row!["mac", "-"]),
            Some(mac) => add_row(&mut t, row!["mac", mac]),
        }
    }
    writeln!(out, "{}", t)?;
    Ok(())
}

pub(crate) async fn connect_with_context<S, C>(connector: &C) -> Result<S::Proxy, Error>
where
    C: ServiceConnector<S>,
    S: fidl::endpoints::ProtocolMarker,
{
    connector.connect().await.with_context(|| format!("failed to connect to {}", S::DEBUG_NAME))
}

async fn get_control<C>(connector: &C, id: u64) -> Result<finterfaces_ext::admin::Control, Error>
where
    C: ServiceConnector<fdebug::InterfacesMarker>,
{
    let debug_interfaces = connect_with_context::<fdebug::InterfacesMarker, _>(connector).await?;
    let (control, server_end) = finterfaces_ext::admin::Control::create_endpoints()
        .context("create admin control endpoints")?;
    let () = debug_interfaces.get_admin(id, server_end).context("send get admin request")?;
    Ok(control)
}

fn configuration_with_ip_forwarding_set(
    ip_version: fnet::IpVersion,
    forwarding: bool,
) -> finterfaces_admin::Configuration {
    match ip_version {
        fnet::IpVersion::V4 => finterfaces_admin::Configuration {
            ipv4: Some(finterfaces_admin::Ipv4Configuration {
                forwarding: Some(forwarding),
                ..finterfaces_admin::Ipv4Configuration::EMPTY
            }),
            ..finterfaces_admin::Configuration::EMPTY
        },
        fnet::IpVersion::V6 => finterfaces_admin::Configuration {
            ipv6: Some(finterfaces_admin::Ipv6Configuration {
                forwarding: Some(forwarding),
                ..finterfaces_admin::Ipv6Configuration::EMPTY
            }),
            ..finterfaces_admin::Configuration::EMPTY
        },
    }
}

fn extract_ip_forwarding(
    finterfaces_admin::Configuration {
        ipv4: ipv4_config, ipv6: ipv6_config, ..
    }: finterfaces_admin::Configuration,
    ip_version: fnet::IpVersion,
) -> Result<bool, Error> {
    match ip_version {
        fnet::IpVersion::V4 => {
            let finterfaces_admin::Ipv4Configuration { forwarding, .. } =
                ipv4_config.context("get IPv4 configuration")?;
            forwarding.context("get IPv4 forwarding configuration")
        }
        fnet::IpVersion::V6 => {
            let finterfaces_admin::Ipv6Configuration { forwarding, .. } =
                ipv6_config.context("get IPv6 configuration")?;
            forwarding.context("get IPv6 forwarding configuration")
        }
    }
}

async fn do_if<C: NetCliDepsConnector>(
    out: &mut ffx_writer::Writer,
    cmd: opts::IfEnum,
    connector: &C,
) -> Result<(), Error> {
    match cmd {
        opts::IfEnum::List(opts::IfList { name_pattern, json }) => {
            let debug_interfaces =
                connect_with_context::<fdebug::InterfacesMarker, _>(connector).await?;
            let interface_state =
                connect_with_context::<finterfaces::StateMarker, _>(connector).await?;
            let stream = finterfaces_ext::event_stream_from_state(&interface_state)?;
            let mut response = finterfaces_ext::existing(stream, HashMap::new()).await?;
            if let Some(name_pattern) = name_pattern {
                let () = shortlist_interfaces(&name_pattern, &mut response);
            }
            let response = response.into_values().map(|properties| async {
                let finterfaces_ext::Properties { id, .. } = &properties;
                let mac = debug_interfaces.get_mac(*id).await.context("call get_mac")?;
                Ok::<_, Error>((properties, mac))
            });
            let response = futures::future::try_join_all(response).await?;
            let mut response: Vec<_> = response
                .into_iter()
                .filter_map(|(properties, mac)| match mac {
                    Err(fdebug::InterfacesGetMacError::NotFound) => None,
                    Ok(mac) => {
                        let mac = mac.map(|box_| *box_);
                        Some((properties, mac).into())
                    }
                })
                .collect();
            let () = response.sort_by_key(|ser::InterfaceView { nicid, .. }| *nicid);
            if out.is_machine() {
                out.machine(&response)?;
            } else if json {
                serde_json::to_writer(out, &response).context("serialize")?;
            } else {
                write_tabulated_interfaces_info(out, response.into_iter())
                    .context("error tabulating interface info")?;
            }
        }
        opts::IfEnum::Add(opts::IfAdd { path }) => match connector.connect_device(&path).await {
            Ok(Device { topological_path, dev }) => {
                let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
                let id = fstack_ext::exec_fidl!(
                    stack.add_ethernet_interface(&topological_path, dev),
                    "error adding interface"
                )?;
                info!("Added interface {}", id);
            }
            Err(e) => {
                out.line(e)?;
            }
        },
        opts::IfEnum::Del(opts::IfDel { interface }) => {
            let id = interface.find_nicid(connector).await?;
            let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
            let () = fstack_ext::exec_fidl!(
                stack.del_ethernet_interface(id),
                "error removing interface"
            )?;
            info!("Deleted interface {}", id);
        }
        opts::IfEnum::Get(opts::IfGet { interface }) => {
            let id = interface.find_nicid(connector).await?;
            let debug_interfaces =
                connect_with_context::<fdebug::InterfacesMarker, _>(connector).await?;
            let interface_state =
                connect_with_context::<finterfaces::StateMarker, _>(connector).await?;
            let stream = finterfaces_ext::event_stream_from_state(&interface_state)?;
            let response =
                finterfaces_ext::existing(stream, finterfaces_ext::InterfaceState::Unknown(id))
                    .await?;
            match response {
                finterfaces_ext::InterfaceState::Unknown(id) => {
                    return Err(user_facing_error(format!("interface with id={} not found", id)));
                }
                finterfaces_ext::InterfaceState::Known(properties) => {
                    let finterfaces_ext::Properties { id, .. } = &properties;
                    let mac = debug_interfaces.get_mac(*id).await.context("call get_mac")?;
                    match mac {
                        Err(fdebug::InterfacesGetMacError::NotFound) => {
                            return Err(user_facing_error(format!(
                                "interface with id={} not found",
                                id
                            )));
                        }
                        Ok(mac) => {
                            let mac = mac.map(|box_| *box_);
                            let view = (properties, mac).into();
                            write_tabulated_interfaces_info(out, std::iter::once(view))
                                .context("error tabulating interface info")?;
                        }
                    };
                }
            }
        }
        opts::IfEnum::IpForward(opts::IfIpForward { cmd }) => match cmd {
            opts::IfIpForwardEnum::Show(opts::IfIpForwardShow { interface, ip_version }) => {
                let id = interface.find_nicid(connector).await.context("find nicid")?;
                let control = get_control(connector, id).await.context("get control")?;
                let configuration = control
                    .get_configuration()
                    .await
                    .map_err(anyhow::Error::new)
                    .and_then(|res| {
                        res.map_err(|e: finterfaces_admin::ControlGetConfigurationError| {
                            anyhow::anyhow!("{:?}", e)
                        })
                    })
                    .context("get configuration")?;

                out.line(format!(
                    "IP forwarding for {:?} is {} on interface {}",
                    ip_version,
                    extract_ip_forwarding(configuration, ip_version)
                        .context("extract IP forwarding configuration")?,
                    id
                ))?;
            }
            opts::IfIpForwardEnum::Set(opts::IfIpForwardSet { interface, ip_version, enable }) => {
                let id = interface.find_nicid(connector).await.context("find nicid")?;
                let control = get_control(connector, id).await.context("get control")?;
                let prev_config = control
                    .set_configuration(configuration_with_ip_forwarding_set(ip_version, enable))
                    .await
                    .map_err(anyhow::Error::new)
                    .and_then(|res| {
                        res.map_err(|e: finterfaces_admin::ControlSetConfigurationError| {
                            anyhow::anyhow!("{:?}", e)
                        })
                    })
                    .context("set configuration")?;
                info!(
                    "IP forwarding for {:?} set to {} on interface {}; previously set to {}",
                    ip_version,
                    enable,
                    id,
                    extract_ip_forwarding(prev_config, ip_version)
                        .context("extract IP forwarding configuration")?
                );
            }
        },
        opts::IfEnum::Enable(opts::IfEnable { interface }) => {
            let id = interface.find_nicid(connector).await?;
            let control = get_control(connector, id).await?;
            let did_enable = control
                .enable()
                .await
                .map_err(anyhow::Error::new)
                .and_then(|res| {
                    res.map_err(|e: finterfaces_admin::ControlEnableError| {
                        anyhow::anyhow!("{:?}", e)
                    })
                })
                .context("error enabling interface")?;
            if did_enable {
                info!("Interface {} enabled", id);
            } else {
                info!("Interface {} already enabled", id);
            }
        }
        opts::IfEnum::Disable(opts::IfDisable { interface }) => {
            let id = interface.find_nicid(connector).await?;
            let control = get_control(connector, id).await?;
            let did_disable = control
                .disable()
                .await
                .map_err(anyhow::Error::new)
                .and_then(|res| {
                    res.map_err(|e: finterfaces_admin::ControlDisableError| {
                        anyhow::anyhow!("{:?}", e)
                    })
                })
                .context("error disabling interface")?;
            if did_disable {
                info!("Interface {} disabled", id);
            } else {
                info!("Interface {} already disabled", id);
            }
        }
        opts::IfEnum::Addr(opts::IfAddr { addr_cmd }) => match addr_cmd {
            opts::IfAddrEnum::Add(opts::IfAddrAdd { interface, addr, prefix, no_subnet_route }) => {
                let id = interface.find_nicid(connector).await?;
                let control = get_control(connector, id).await?;
                let addr = fnet_ext::IpAddress::from_str(&addr)?.into();
                let subnet = fnet_ext::Subnet { addr, prefix_len: prefix };
                let (address_state_provider, server_end) = fidl::endpoints::create_proxy::<
                    finterfaces_admin::AddressStateProviderMarker,
                >()
                .context("create proxy")?;
                // TODO(https://fxbug.dev/93439): Call `detach` at the end
                // (after `add_forwarding_entry` below). This will ensure that
                // the address is only added if all intermediate operations
                // succeed. In the meantime, `detach` is called before the
                // `assignment_state_stream` is operated on to ensure that it is
                // not racy.
                let () = address_state_provider.detach().context("detach address lifetime")?;
                let () = control
                    .add_address(
                        &mut subnet.into(),
                        finterfaces_admin::AddressParameters::EMPTY,
                        server_end,
                    )
                    .context("call add address")?;
                let state_stream =
                    finterfaces_ext::admin::assignment_state_stream(address_state_provider);

                state_stream
                    .try_filter_map(|state| {
                        futures::future::ok(match state {
                            finterfaces_admin::AddressAssignmentState::Tentative => None,
                            finterfaces_admin::AddressAssignmentState::Assigned => Some(()),
                            finterfaces_admin::AddressAssignmentState::Unavailable => Some(()),
                        })
                    })
                    .try_next()
                    .await
                    .context("error after adding address")?
                    .ok_or_else(|| {
                        anyhow::anyhow!(
                            "Address assignment state stream unexpectedly ended \
                                 before reaching Assigned or Unavailable state. \
                                 This is probably a bug."
                        )
                    })?;

                if !no_subnet_route {
                    let stack: fstack::StackProxy =
                        connect_with_context::<fstack::StackMarker, _>(connector).await?;
                    let mut forwarding_entry = fstack::ForwardingEntry {
                        subnet: fnet_ext::apply_subnet_mask(subnet.into()),
                        device_id: id,
                        next_hop: None,
                        metric: 0,
                    };
                    let () = fstack_ext::exec_fidl!(
                        stack.add_forwarding_entry(&mut forwarding_entry),
                        "error adding forwarding entry"
                    )?;
                    info!("Added forwarding entry {:?}", forwarding_entry);
                }

                info!("Address {}/{} added to interface {}", addr, prefix, id);
            }
            opts::IfAddrEnum::Del(opts::IfAddrDel { interface, addr, prefix }) => {
                let id = interface.find_nicid(connector).await?;
                let control = get_control(connector, id).await?;
                let addr = fnet_ext::IpAddress::from_str(&addr)?;
                let did_remove = {
                    let addr = addr.into();
                    let mut subnet = fnet::Subnet {
                        addr,
                        prefix_len: prefix.unwrap_or_else(|| {
                            8 * u8::try_from(match addr {
                                fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => addr.len(),
                                fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => addr.len(),
                            })
                            .expect("prefix length doesn't fit u8")
                        }),
                    };
                    control
                        .remove_address(&mut subnet)
                        .await
                        .map_err(anyhow::Error::new)
                        .and_then(|res| {
                            res.map_err(|e: finterfaces_admin::ControlRemoveAddressError| {
                                anyhow::anyhow!("{:?}", e)
                            })
                        })
                        .context("call remove address")?
                };
                if !did_remove {
                    return Err(user_facing_error(format!(
                        "Address {} not found on interface {}",
                        addr, id
                    )));
                }
                info!("Address {} deleted from interface {}", addr, id);
            }
        },
        opts::IfEnum::Bridge(opts::IfBridge { interfaces }) => {
            let netstack: fnetstack::NetstackProxy =
                connect_with_context::<fnetstack::NetstackMarker, _>(connector).await?;

            let build_name_to_id_map = || async {
                let interface_state =
                    connect_with_context::<finterfaces::StateMarker, _>(connector).await?;
                let stream = finterfaces_ext::event_stream_from_state(&interface_state)?;
                let response = finterfaces_ext::existing(stream, HashMap::new()).await?;
                Ok::<HashMap<String, u64>, Error>(
                    response
                        .into_iter()
                        .map(
                            |(
                                id,
                                finterfaces_ext::Properties {
                                    name,
                                    id: _,
                                    device_class: _,
                                    online: _,
                                    addresses: _,
                                    has_default_ipv4_route: _,
                                    has_default_ipv6_route: _,
                                },
                            )| (name, id),
                        )
                        .collect(),
                )
            };

            let num_interfaces = interfaces.len();

            let (_name_to_id, ids): (Option<HashMap<String, u64>>, Vec<u32>) =
                futures::stream::iter(interfaces)
                    .map(Ok::<_, Error>)
                    .try_fold(
                        (None, Vec::with_capacity(num_interfaces)),
                        |(name_to_id, mut ids), interface| async move {
                            let (name_to_id, id) = match interface {
                                opts::InterfaceIdentifier::Id(id) => (name_to_id, id),
                                opts::InterfaceIdentifier::Name(name) => {
                                    let name_to_id = match name_to_id {
                                        Some(name_to_id) => name_to_id,
                                        None => build_name_to_id_map().await?,
                                    };
                                    let id = name_to_id.get(&name).copied().ok_or_else(|| {
                                        user_facing_error(format!("no interface named {}", name))
                                    })?;
                                    (Some(name_to_id), id)
                                }
                            };
                            ids.push(
                                u32::try_from(id)
                                    .with_context(|| format!("nicid {} does not fit in u32", id))?,
                            );
                            Ok((name_to_id, ids))
                        },
                    )
                    .await?;

            let bridge_id = netstack
                .bridge_interfaces(&ids)
                .await
                .map_err(anyhow::Error::new)
                .and_then(|result| match result {
                    fnetstack::Result_::Message(message) => Err(anyhow::Error::msg(message)),
                    fnetstack::Result_::Nicid(id) => Ok(id),
                })
                .with_context(|| format!("bridge_interfaces({:?}", ids))?;
            info!("network bridge created with id {}", bridge_id);
        }
    }
    Ok(())
}

async fn do_route<C: NetCliDepsConnector>(
    out: &mut ffx_writer::Writer,
    cmd: opts::RouteEnum,
    connector: &C,
) -> Result<(), Error> {
    let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
    match cmd {
        opts::RouteEnum::List(opts::RouteList { json }) => {
            let response =
                stack.get_forwarding_table().await.context("error retrieving forwarding table")?;
            if out.is_machine() || json {
                let response: Vec<_> = response
                    .into_iter()
                    .map(fstack_ext::ForwardingEntry::from)
                    .map(ser::ForwardingEntry::from)
                    .collect();
                if out.is_machine() {
                    out.machine(&response).context("serialize")?;
                } else {
                    let () = serde_json::to_writer(out, &response).context("serialize")?;
                }
            } else {
                let mut t = Table::new();
                t.set_format(format::FormatBuilder::new().padding(2, 2).build());

                t.set_titles(row!["Destination", "Gateway", "NICID", "Metric"]);
                for entry in response {
                    let fstack_ext::ForwardingEntry { subnet, device_id, next_hop, metric } =
                        entry.into();
                    let next_hop = next_hop.map(|next_hop| next_hop.to_string());
                    let next_hop = next_hop.as_ref().map_or("-", |s| s.as_str());
                    let () = add_row(&mut t, row![subnet, next_hop, device_id, metric]);
                }

                let _lines_printed: usize = t.print(out)?;
                out.line("")?;
            }
        }
        opts::RouteEnum::Add(route) => {
            let nicid = route.interface.find_u32_nicid(connector).await?;
            let mut entry = route.into_route_table_entry(nicid);
            let () = fstack_ext::exec_fidl!(
                stack.add_forwarding_entry(&mut entry),
                "error adding next-hop forwarding entry"
            )?;
        }
        opts::RouteEnum::Del(route) => {
            let nicid = route.interface.find_u32_nicid(connector).await?;
            let mut entry = route.into_route_table_entry(nicid);
            let () = fstack_ext::exec_fidl!(
                stack.del_forwarding_entry(&mut entry),
                "error removing forwarding entry"
            )?;
        }
    }
    Ok(())
}

async fn do_filter<C: NetCliDepsConnector, W: std::io::Write>(
    mut out: W,
    cmd: opts::FilterEnum,
    connector: &C,
) -> Result<(), Error> {
    let filter = connect_with_context::<ffilter::FilterMarker, _>(connector).await?;
    match cmd {
        opts::FilterEnum::GetRules(opts::FilterGetRules {}) => {
            let (rules, generation): (Vec<ffilter::Rule>, u32) =
                filter_fidl!(filter.get_rules(), "error getting filter rules")?;
            writeln!(out, "{:?} (generation {})", rules, generation)?;
        }
        opts::FilterEnum::SetRules(opts::FilterSetRules { rules }) => {
            let (_cur_rules, generation) =
                filter_fidl!(filter.get_rules(), "error getting filter rules")?;
            let mut rules = netfilter::parser::parse_str_to_rules(&rules)?;
            let () = filter_fidl!(
                filter.update_rules(&mut rules.iter_mut(), generation),
                "error setting filter rules"
            )?;
            info!("successfully set filter rules");
        }
        opts::FilterEnum::GetNatRules(opts::FilterGetNatRules {}) => {
            let (rules, generation): (Vec<ffilter::Nat>, u32) =
                filter_fidl!(filter.get_nat_rules(), "error getting NAT rules")?;
            writeln!(out, "{:?} (generation {})", rules, generation)?;
        }
        opts::FilterEnum::SetNatRules(opts::FilterSetNatRules { rules }) => {
            let (_cur_rules, generation) =
                filter_fidl!(filter.get_nat_rules(), "error getting NAT rules")?;
            let mut rules = netfilter::parser::parse_str_to_nat_rules(&rules)?;
            let () = filter_fidl!(
                filter.update_nat_rules(&mut rules.iter_mut(), generation),
                "error setting NAT rules"
            )?;
            info!("successfully set NAT rules");
        }
        opts::FilterEnum::GetRdrRules(opts::FilterGetRdrRules {}) => {
            let (rules, generation): (Vec<ffilter::Rdr>, u32) =
                filter_fidl!(filter.get_rdr_rules(), "error getting RDR rules")?;
            writeln!(out, "{:?} (generation {})", rules, generation)?;
        }
        opts::FilterEnum::SetRdrRules(opts::FilterSetRdrRules { rules }) => {
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

async fn do_log<C: NetCliDepsConnector>(cmd: opts::LogEnum, connector: &C) -> Result<(), Error> {
    let log = connect_with_context::<fstack::LogMarker, _>(connector).await?;
    match cmd {
        opts::LogEnum::SetPackets(opts::LogSetPackets { enabled }) => {
            let () = log.set_log_packets(enabled).await.context("error setting log packets")?;
            info!("log packets set to {:?}", enabled);
        }
    }
    Ok(())
}

async fn do_dhcp<C: NetCliDepsConnector>(cmd: opts::DhcpEnum, connector: &C) -> Result<(), Error> {
    let stack = connect_with_context::<fstack::StackMarker, _>(connector).await?;
    match cmd {
        opts::DhcpEnum::Start(opts::DhcpStart { interface }) => {
            let id = interface.find_nicid(connector).await?;
            let () = fstack_ext::exec_fidl!(
                stack.set_dhcp_client_enabled(id, true),
                "error stopping DHCP client"
            )?;
            info!("dhcp client started on interface {}", id);
        }
        opts::DhcpEnum::Stop(opts::DhcpStop { interface }) => {
            let id = interface.find_nicid(connector).await?;
            let () = fstack_ext::exec_fidl!(
                stack.set_dhcp_client_enabled(id, false),
                "error stopping DHCP client"
            )?;
            info!("dhcp client stopped on interface {}", id);
        }
    }
    Ok(())
}

async fn do_dhcpd<C: NetCliDepsConnector>(
    cmd: opts::dhcpd::DhcpdEnum,
    connector: &C,
) -> Result<(), Error> {
    let dhcpd_server = connect_with_context::<fdhcp::Server_Marker, _>(connector).await?;
    match cmd {
        opts::dhcpd::DhcpdEnum::Start(opts::dhcpd::Start {}) => {
            Ok(do_dhcpd_start(dhcpd_server).await?)
        }
        opts::dhcpd::DhcpdEnum::Stop(opts::dhcpd::Stop {}) => {
            Ok(do_dhcpd_stop(dhcpd_server).await?)
        }
        opts::dhcpd::DhcpdEnum::Get(get_arg) => Ok(do_dhcpd_get(get_arg, dhcpd_server).await?),
        opts::dhcpd::DhcpdEnum::Set(set_arg) => Ok(do_dhcpd_set(set_arg, dhcpd_server).await?),
        opts::dhcpd::DhcpdEnum::List(list_arg) => Ok(do_dhcpd_list(list_arg, dhcpd_server).await?),
        opts::dhcpd::DhcpdEnum::Reset(reset_arg) => {
            Ok(do_dhcpd_reset(reset_arg, dhcpd_server).await?)
        }
        opts::dhcpd::DhcpdEnum::ClearLeases(opts::dhcpd::ClearLeases {}) => {
            Ok(do_dhcpd_clear_leases(dhcpd_server).await?)
        }
    }
}

async fn do_neigh<C: NetCliDepsConnector>(
    out: ffx_writer::Writer,
    cmd: opts::NeighEnum,
    connector: &C,
) -> Result<(), Error> {
    match cmd {
        opts::NeighEnum::Add(opts::NeighAdd { interface, ip, mac }) => {
            let interface = interface.find_nicid(connector).await?;
            let controller =
                connect_with_context::<fneighbor::ControllerMarker, _>(connector).await?;
            let () = do_neigh_add(interface, ip.into(), mac.into(), controller)
                .await
                .context("failed during neigh add command")?;
            info!("Added entry ({}, {}) for interface {}", ip, mac, interface);
        }
        opts::NeighEnum::Clear(opts::NeighClear { interface, ip_version }) => {
            let interface = interface.find_nicid(connector).await?;
            let controller =
                connect_with_context::<fneighbor::ControllerMarker, _>(connector).await?;
            let () = do_neigh_clear(interface, ip_version, controller)
                .await
                .context("failed during neigh clear command")?;
            info!("Cleared entries for interface {}", interface);
        }
        opts::NeighEnum::Del(opts::NeighDel { interface, ip }) => {
            let interface = interface.find_nicid(connector).await?;
            let controller =
                connect_with_context::<fneighbor::ControllerMarker, _>(connector).await?;
            let () = do_neigh_del(interface, ip.into(), controller)
                .await
                .context("failed during neigh del command")?;
            info!("Deleted entry {} for interface {}", ip, interface);
        }
        opts::NeighEnum::List(opts::NeighList { json }) => {
            let view = connect_with_context::<fneighbor::ViewMarker, _>(connector).await?;
            let () = print_neigh_entries(out, false /* watch_for_changes */, view, json)
                .await
                .context("error listing neighbor entries")?;
        }
        opts::NeighEnum::Watch(opts::NeighWatch { json_lines }) => {
            let view = connect_with_context::<fneighbor::ViewMarker, _>(connector).await?;
            let () = print_neigh_entries(out, true /* watch_for_changes */, view, json_lines)
                .await
                .context("error watching for changes to the neighbor table")?;
        }
        opts::NeighEnum::Config(opts::NeighConfig { neigh_config_cmd }) => match neigh_config_cmd {
            opts::NeighConfigEnum::Get(opts::NeighGetConfig { interface, ip_version }) => {
                let interface = interface.find_nicid(connector).await?;
                let view = connect_with_context::<fneighbor::ViewMarker, _>(connector).await?;
                let () = print_neigh_config(interface, ip_version, view)
                    .await
                    .context("failed during neigh config get command")?;
            }
            opts::NeighConfigEnum::Update(opts::NeighUpdateConfig {
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
                let interface = interface.find_nicid(connector).await?;
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

fn unpack_neigh_iter_item(
    item: fneighbor::EntryIteratorItem,
) -> Result<(&'static str, Option<fneighbor_ext::Entry>), Error> {
    let displayed_state_change_status = ser::DISPLAYED_NEIGH_ENTRY_VARIANTS.select(&item);

    Ok((
        displayed_state_change_status,
        match item {
            fneighbor::EntryIteratorItem::Existing(entry)
            | fneighbor::EntryIteratorItem::Added(entry)
            | fneighbor::EntryIteratorItem::Changed(entry)
            | fneighbor::EntryIteratorItem::Removed(entry) => {
                Some(fneighbor_ext::Entry::try_from(entry)?)
            }
            fneighbor::EntryIteratorItem::Idle(fneighbor::IdleEvent) => None,
        },
    ))
}

fn jsonify_neigh_iter_item(
    item: fneighbor::EntryIteratorItem,
    include_entry_state: bool,
) -> Result<Value, Error> {
    let (state_change_status, entry) = unpack_neigh_iter_item(item)?;
    let entry_json = entry
        .map(ser::NeighborTableEntry::from)
        .map(serde_json::to_value)
        .map(|res| res.map_err(Error::new))
        .unwrap_or(Err(anyhow::anyhow!("failed to jsonify NeighborTableEntry")))?;
    if include_entry_state {
        Ok(json!({
            "state_change_status": state_change_status,
            "entry": entry_json,
        }))
    } else {
        Ok(entry_json)
    }
}

async fn print_neigh_entries(
    mut out: ffx_writer::Writer,
    watch_for_changes: bool,
    view: fneighbor::ViewProxy,
    json: bool,
) -> Result<(), Error> {
    let (it_client, it_server) =
        fidl::endpoints::create_endpoints::<fneighbor::EntryIteratorMarker>()
            .context("error creating channel for entry iterator")?;
    let it = it_client.into_proxy().context("error creating proxy to entry iterator")?;

    let () = view
        .open_entry_iterator(it_server, fneighbor::EntryIteratorOptions::EMPTY)
        .context("error opening a connection to the entry iterator")?;

    let out_ref = &mut out;
    if watch_for_changes {
        neigh_entry_stream(it, watch_for_changes)
            .map_ok(|item| {
                write_neigh_entry(
                    out_ref,
                    item,
                    /* include_entry_state= */ watch_for_changes,
                    json,
                )
                .context("error writing entry")
            })
            .try_fold((), |(), r| futures::future::ready(r))
            .await?;
    } else {
        let results: Vec<Result<fneighbor::EntryIteratorItem, _>> =
            neigh_entry_stream(it, watch_for_changes).collect().await;
        if out.is_machine() || json {
            let jsonified_items: Value =
                itertools::process_results(results.into_iter(), |items| {
                    itertools::process_results(
                        items.map(|item| {
                            jsonify_neigh_iter_item(
                                item,
                                /* include_entry_state= */ watch_for_changes,
                            )
                        }),
                        |json_values| Value::from_iter(json_values),
                    )
                })??;
            if out.is_machine() {
                out.machine(&jsonified_items)?;
            } else {
                out.write(&jsonified_items)?;
            }
        } else {
            itertools::process_results(results.into_iter(), |mut items| {
                items.try_for_each(|item| {
                    write_tabular_neigh_entry(
                        &mut out,
                        item,
                        /* include_entry_state= */ watch_for_changes,
                    )
                })
            })??;
        }
    }

    Ok(())
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

fn write_tabular_neigh_entry<W: std::io::Write>(
    mut f: W,
    item: fneighbor::EntryIteratorItem,
    include_entry_state: bool,
) -> Result<(), Error> {
    let (state_change_status, entry) = unpack_neigh_iter_item(item)?;
    match entry {
        Some(entry) => {
            if include_entry_state {
                writeln!(
                    &mut f,
                    "{:width$} | {}",
                    state_change_status,
                    entry,
                    width = ser::DISPLAYED_NEIGH_ENTRY_VARIANTS
                        .into_iter()
                        .map(|s| s.len())
                        .max()
                        .unwrap_or(0),
                )?
            } else {
                writeln!(&mut f, "{}", entry)?
            }
        }
        None => writeln!(&mut f, "{}", state_change_status)?,
    }
    Ok(())
}

fn write_neigh_entry(
    f: &mut ffx_writer::Writer,
    item: fneighbor::EntryIteratorItem,
    include_entry_state: bool,
    json: bool,
) -> Result<(), Error> {
    if f.is_machine() || json {
        let entry = jsonify_neigh_iter_item(item, include_entry_state)?;
        if f.is_machine() {
            f.machine(&entry)?;
        } else {
            f.line(&entry)?;
        }
    } else {
        write_tabular_neigh_entry(f, item, include_entry_state)?
    }
    Ok(())
}

async fn do_dhcpd_start(server: fdhcp::Server_Proxy) -> Result<(), Error> {
    server.start_serving().await?.map_err(zx::Status::from_raw).context("failed to start server")
}

async fn do_dhcpd_stop(server: fdhcp::Server_Proxy) -> Result<(), Error> {
    server.stop_serving().await.context("failed to stop server")
}

async fn do_dhcpd_get(get_arg: opts::dhcpd::Get, server: fdhcp::Server_Proxy) -> Result<(), Error> {
    match get_arg.arg {
        opts::dhcpd::GetArg::Option(opts::dhcpd::OptionArg { name }) => {
            let res = server
                .get_option(name.clone().into())
                .await?
                .map_err(zx::Status::from_raw)
                .with_context(|| format!("get_option({:?}) failed", name))?;
            println!("{:#?}", res);
        }
        opts::dhcpd::GetArg::Parameter(opts::dhcpd::ParameterArg { name }) => {
            let res = server
                .get_parameter(name.clone().into())
                .await?
                .map_err(zx::Status::from_raw)
                .with_context(|| format!("get_parameter({:?}) failed", name))?;
            println!("{:#?}", res);
        }
    };
    Ok(())
}

async fn do_dhcpd_set(set_arg: opts::dhcpd::Set, server: fdhcp::Server_Proxy) -> Result<(), Error> {
    match set_arg.arg {
        opts::dhcpd::SetArg::Option(opts::dhcpd::OptionArg { name }) => {
            let () = server
                .set_option(&mut name.clone().into())
                .await?
                .map_err(zx::Status::from_raw)
                .with_context(|| format!("set_option({:?}) failed", name))?;
        }
        opts::dhcpd::SetArg::Parameter(opts::dhcpd::ParameterArg { name }) => {
            let () = server
                .set_parameter(&mut name.clone().into())
                .await?
                .map_err(zx::Status::from_raw)
                .with_context(|| format!("set_parameter({:?}) failed", name))?;
        }
    };
    Ok(())
}

async fn do_dhcpd_list(
    list_arg: opts::dhcpd::List,
    server: fdhcp::Server_Proxy,
) -> Result<(), Error> {
    match list_arg.arg {
        opts::dhcpd::ListArg::Option(opts::dhcpd::OptionToken {}) => {
            let res = server
                .list_options()
                .await?
                .map_err(zx::Status::from_raw)
                .context("list_options() failed")?;

            println!("{:#?}", res);
        }
        opts::dhcpd::ListArg::Parameter(opts::dhcpd::ParameterToken {}) => {
            let res = server
                .list_parameters()
                .await?
                .map_err(zx::Status::from_raw)
                .context("list_parameters() failed")?;
            println!("{:#?}", res);
        }
    };
    Ok(())
}

async fn do_dhcpd_reset(
    reset_arg: opts::dhcpd::Reset,
    server: fdhcp::Server_Proxy,
) -> Result<(), Error> {
    match reset_arg.arg {
        opts::dhcpd::ResetArg::Option(opts::dhcpd::OptionToken {}) => {
            let () = server
                .reset_options()
                .await?
                .map_err(zx::Status::from_raw)
                .context("reset_options() failed")?;
        }
        opts::dhcpd::ResetArg::Parameter(opts::dhcpd::ParameterToken {}) => {
            let () = server
                .reset_parameters()
                .await?
                .map_err(zx::Status::from_raw)
                .context("reset_parameters() failed")?;
        }
    };
    Ok(())
}

async fn do_dhcpd_clear_leases(server: fdhcp::Server_Proxy) -> Result<(), Error> {
    server.clear_leases().await?.map_err(zx::Status::from_raw).context("clear_leases() failed")
}

async fn do_dns<W: std::io::Write, C: NetCliDepsConnector>(
    mut out: W,
    cmd: opts::dns::DnsEnum,
    connector: &C,
) -> Result<(), Error> {
    let lookup = connect_with_context::<fname::LookupMarker, _>(connector).await?;
    let opts::dns::DnsEnum::Lookup(opts::dns::Lookup { hostname, ipv4, ipv6, sort }) = cmd;
    let result = lookup
        .lookup_ip(
            &hostname,
            fname::LookupIpOptions {
                ipv4_lookup: Some(ipv4),
                ipv6_lookup: Some(ipv6),
                sort_addresses: Some(sort),
                ..fname::LookupIpOptions::EMPTY
            },
        )
        .await?
        .map_err(|e| anyhow::anyhow!("DNS lookup failed: {:?}", e))?;
    let fname::LookupResult { addresses, .. } = result;
    let addrs = addresses.context("`addresses` not set in response from DNS resolver")?;
    for addr in addrs {
        writeln!(out, "{}", fnet_ext::IpAddress::from(addr))?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use anyhow::Error;
    use assert_matches::assert_matches;
    use fidl::endpoints::ProtocolMarker;
    use fidl_fuchsia_hardware_network as fhardware_network;
    use fuchsia_async::{self as fasync, TimeoutExt as _};
    use net_declare::{fidl_ip, fidl_ip_v4};
    use std::convert::TryInto as _;
    use test_case::test_case;

    const IF_ADDR_V4: fnet::Subnet = net_declare::fidl_subnet!("192.168.0.1/32");
    const IF_ADDR_V6: fnet::Subnet = net_declare::fidl_subnet!("fd00::1/64");

    const MAC_1: fnet::MacAddress = net_declare::fidl_mac!("01:02:03:04:05:06");
    const MAC_2: fnet::MacAddress = net_declare::fidl_mac!("02:03:04:05:06:07");

    #[derive(Default)]
    struct TestConnector {
        debug_interfaces: Option<fdebug::InterfacesProxy>,
        dhcpd: Option<fdhcp::Server_Proxy>,
        interfaces_state: Option<finterfaces::StateProxy>,
        netstack: Option<fnetstack::NetstackProxy>,
        stack: Option<fstack::StackProxy>,
        name_lookup: Option<fname::LookupProxy>,
    }

    #[async_trait::async_trait]
    impl ServiceConnector<fdebug::InterfacesMarker> for TestConnector {
        async fn connect(
            &self,
        ) -> Result<<fdebug::InterfacesMarker as ProtocolMarker>::Proxy, Error> {
            self.debug_interfaces
                .as_ref()
                .cloned()
                .ok_or(anyhow::anyhow!("connector has no dhcp server instance"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<fdhcp::Server_Marker> for TestConnector {
        async fn connect(&self) -> Result<<fdhcp::Server_Marker as ProtocolMarker>::Proxy, Error> {
            self.dhcpd
                .as_ref()
                .cloned()
                .ok_or(anyhow::anyhow!("connector has no dhcp server instance"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<ffilter::FilterMarker> for TestConnector {
        async fn connect(&self) -> Result<<ffilter::FilterMarker as ProtocolMarker>::Proxy, Error> {
            Err(anyhow::anyhow!("connect filter unimplemented for test connector"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<finterfaces::StateMarker> for TestConnector {
        async fn connect(
            &self,
        ) -> Result<<finterfaces::StateMarker as ProtocolMarker>::Proxy, Error> {
            self.interfaces_state
                .as_ref()
                .cloned()
                .ok_or(anyhow::anyhow!("connector has no interfaces state instance"))
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
    impl ServiceConnector<fnetstack::NetstackMarker> for TestConnector {
        async fn connect(
            &self,
        ) -> Result<<fnetstack::NetstackMarker as ProtocolMarker>::Proxy, Error> {
            self.netstack
                .as_ref()
                .cloned()
                .ok_or(anyhow::anyhow!("connector has no netstack instance"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<fstack::LogMarker> for TestConnector {
        async fn connect(&self) -> Result<<fstack::LogMarker as ProtocolMarker>::Proxy, Error> {
            Err(anyhow::anyhow!("connect log unimplemented for test connector"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<fstack::StackMarker> for TestConnector {
        async fn connect(&self) -> Result<<fstack::StackMarker as ProtocolMarker>::Proxy, Error> {
            self.stack.as_ref().cloned().ok_or(anyhow::anyhow!("connector has no stack instance"))
        }
    }

    #[async_trait::async_trait]
    impl ServiceConnector<fname::LookupMarker> for TestConnector {
        async fn connect(&self) -> Result<<fname::LookupMarker as ProtocolMarker>::Proxy, Error> {
            self.name_lookup
                .as_ref()
                .cloned()
                .ok_or(anyhow::anyhow!("connector has no name lookup instance"))
        }
    }

    #[async_trait::async_trait]
    impl NetCliDepsConnector for TestConnector {
        async fn connect_device(&self, _path: &str) -> Result<Device, Error> {
            Err(anyhow::anyhow!("connect interface unimplmented for test connector"))
        }
    }

    fn trim_whitespace_for_comparison(s: &str) -> String {
        s.trim().lines().map(|s| s.trim()).collect::<Vec<&str>>().join("\n")
    }

    fn get_fake_interface(
        id: u64,
        name: &'static str,
        device_class: finterfaces::DeviceClass,
        octets: Option<[u8; 6]>,
    ) -> (finterfaces_ext::Properties, Option<fnet::MacAddress>) {
        (
            finterfaces_ext::Properties {
                id,
                name: name.to_string(),
                device_class,
                online: true,
                addresses: Vec::new(),
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
            },
            octets.map(|octets| fnet::MacAddress { octets }),
        )
    }

    fn shortlist_interfaces_by_nicid(name_pattern: &str) -> Vec<u64> {
        let mut interfaces = [
            get_fake_interface(
                1,
                "lo",
                finterfaces::DeviceClass::Loopback(finterfaces::Empty),
                None,
            ),
            get_fake_interface(
                10,
                "eth001",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Ethernet),
                Some([1, 2, 3, 4, 5, 6]),
            ),
            get_fake_interface(
                20,
                "eth002",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Ethernet),
                Some([1, 2, 3, 4, 5, 7]),
            ),
            get_fake_interface(
                30,
                "eth003",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Ethernet),
                Some([1, 2, 3, 4, 5, 8]),
            ),
            get_fake_interface(
                100,
                "wlan001",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Wlan),
                Some([2, 2, 3, 4, 5, 6]),
            ),
            get_fake_interface(
                200,
                "wlan002",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Wlan),
                Some([2, 2, 3, 4, 5, 7]),
            ),
            get_fake_interface(
                300,
                "wlan003",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Wlan),
                Some([2, 2, 3, 4, 5, 8]),
            ),
        ]
        .into_iter()
        .map(|(properties, _): (_, Option<fnet::MacAddress>)| {
            let finterfaces_ext::Properties { id, .. } = &properties;
            (*id, properties)
        })
        .collect();
        let () = shortlist_interfaces(name_pattern, &mut interfaces);
        let mut interfaces: Vec<_> = interfaces.into_keys().collect();
        let () = interfaces.sort();
        interfaces
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

    #[test_case(fnet::IpVersion::V4, true ; "IPv4 enable routing")]
    #[test_case(fnet::IpVersion::V4, false ; "IPv4 disable routing")]
    #[test_case(fnet::IpVersion::V6, true ; "IPv6 enable routing")]
    #[test_case(fnet::IpVersion::V6, false ; "IPv6 disable routing")]
    #[fasync::run_singlethreaded(test)]
    async fn if_ip_forward(ip_version: fnet::IpVersion, enable: bool) {
        let interface1 = TestInterface { nicid: 1, name: "interface1" };
        let (debug_interfaces, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fdebug::InterfacesMarker>().unwrap();
        let connector =
            TestConnector { debug_interfaces: Some(debug_interfaces), ..Default::default() };

        let requests_fut = async {
            let (id, control, _control_handle) = requests
                .next()
                .await
                .expect("debug request stream not ended")
                .expect("debug request stream not error")
                .into_get_admin()
                .expect("get admin request");
            assert_eq!(id, interface1.nicid);

            let mut control: finterfaces_admin::ControlRequestStream =
                control.into_stream().expect("control request stream");
            let (configuration, responder) = control
                .next()
                .await
                .expect("control request stream not ended")
                .expect("control request stream not error")
                .into_set_configuration()
                .expect("set configuration request");
            assert_eq!(
                extract_ip_forwarding(configuration, ip_version)
                    .expect("extract IP forwarding configuration"),
                enable
            );
            // net-cli does not check the returned configuration so we do not
            // return a populated one.
            let () = responder
                .send(&mut Ok(finterfaces_admin::Configuration::EMPTY))
                .expect("responder.send should succeed");
            Ok(())
        };
        let mut out = ffx_writer::Writer::new_test(None);
        let do_if_fut = do_if(
            &mut out,
            opts::IfEnum::IpForward(opts::IfIpForward {
                cmd: opts::IfIpForwardEnum::Set(opts::IfIpForwardSet {
                    interface: interface1.identifier(false /* use_ifname */),
                    ip_version,
                    enable,
                }),
            }),
            &connector,
        );
        let ((), ()) = futures::future::try_join(do_if_fut, requests_fut)
            .await
            .expect("setting interface ip forwarding should succeed");

        let requests_fut = async {
            let (id, control, _control_handle) = requests
                .next()
                .await
                .expect("debug request stream not ended")
                .expect("debug request stream not error")
                .into_get_admin()
                .expect("get admin request");
            assert_eq!(id, interface1.nicid);

            let mut control: finterfaces_admin::ControlRequestStream =
                control.into_stream().expect("control request stream");
            let responder = control
                .next()
                .await
                .expect("control request stream not ended")
                .expect("control request stream not error")
                .into_get_configuration()
                .expect("get configuration request");
            let () = responder
                .send(&mut Ok(configuration_with_ip_forwarding_set(ip_version, enable)))
                .expect("responder.send should succeed");
            Ok(())
        };
        let mut output_buf = ffx_writer::Writer::new_test(None);
        let do_if_fut = do_if(
            &mut output_buf,
            opts::IfEnum::IpForward(opts::IfIpForward {
                cmd: opts::IfIpForwardEnum::Show(opts::IfIpForwardShow {
                    interface: interface1.identifier(false /* use_ifname */),
                    ip_version,
                }),
            }),
            &connector,
        );
        let ((), ()) = futures::future::try_join(do_if_fut, requests_fut)
            .await
            .expect("getting interface ip forwarding should succeed");
        let got_output = output_buf.test_output().unwrap();
        pretty_assertions::assert_eq!(
            trim_whitespace_for_comparison(&got_output),
            trim_whitespace_for_comparison(&format!(
                "IP forwarding for {:?} is {} on interface {}",
                ip_version, enable, interface1.nicid
            )),
        )
    }

    async fn always_answer_with_interfaces(
        interfaces_state_requests: finterfaces::StateRequestStream,
        interfaces: Vec<finterfaces::Properties>,
    ) {
        interfaces_state_requests
            .try_for_each(|request| {
                let interfaces = interfaces.clone();
                async move {
                    let (finterfaces::WatcherOptions { .. }, server_end, _): (
                        _,
                        _,
                        finterfaces::StateControlHandle,
                    ) = request.into_get_watcher().expect("request type should be GetWatcher");

                    let mut watcher_request_stream: finterfaces::WatcherRequestStream =
                        server_end.into_stream().expect("watcher FIDL error");

                    for mut event in interfaces
                        .into_iter()
                        .map(finterfaces::Event::Existing)
                        .chain(std::iter::once(finterfaces::Event::Idle(finterfaces::Empty)))
                    {
                        let () = watcher_request_stream
                            .try_next()
                            .await
                            .expect("watcher watch FIDL error")
                            .expect("watcher request stream should not have ended")
                            .into_watch()
                            .expect("request should be of type Watch")
                            .send(&mut event)
                            .expect("responder.send should succeed");
                    }

                    assert_matches!(
                        watcher_request_stream.try_next().await.expect("watcher watch FIDL error"),
                        None,
                        "remaining watcher request stream should be empty"
                    );
                    Ok(())
                }
            })
            .await
            .expect("interfaces state FIDL error")
    }

    #[derive(Clone)]
    struct TestInterface {
        nicid: u64,
        name: &'static str,
    }

    impl TestInterface {
        fn identifier(&self, use_ifname: bool) -> opts::InterfaceIdentifier {
            let Self { nicid, name } = self;
            if use_ifname {
                opts::InterfaceIdentifier::Name(name.to_string())
            } else {
                opts::InterfaceIdentifier::Id(*nicid)
            }
        }
    }

    #[test_case(true, false ; "when interface is up, and adding subnet route")]
    #[test_case(true, true ; "when interface is up, and not adding subnet route")]
    #[test_case(false, false ; "when interface is down, and adding subnet route")]
    #[test_case(false, true ; "when interface is down, and not adding subnet route")]
    #[fasync::run_singlethreaded(test)]
    async fn if_addr_add(interface_is_up: bool, no_subnet_route: bool) {
        const TEST_PREFIX_LENGTH: u8 = 64;

        let interface1 = TestInterface { nicid: 1, name: "interface1" };
        let (debug_interfaces, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fdebug::InterfacesMarker>().unwrap();

        let (stack_proxy, stack_stream) = match (!no_subnet_route)
            .then(|| fidl::endpoints::create_proxy_and_stream::<fstack::StackMarker>().unwrap())
        {
            Some((proxy, stream)) => (Some(proxy), Some(stream)),
            None => (None, None),
        };

        let connector = TestConnector {
            debug_interfaces: Some(debug_interfaces),
            stack: stack_proxy,
            ..Default::default()
        };
        let mut out = ffx_writer::Writer::new_test(None);
        let do_if_fut = do_if(
            &mut out,
            opts::IfEnum::Addr(opts::IfAddr {
                addr_cmd: opts::IfAddrEnum::Add(opts::IfAddrAdd {
                    interface: interface1.identifier(false /* use_ifname */),
                    addr: fnet_ext::IpAddress::from(IF_ADDR_V6.addr).to_string(),
                    prefix: TEST_PREFIX_LENGTH,
                    no_subnet_route,
                }),
            }),
            &connector,
        )
        .map(|res| res.expect("success"));

        let admin_fut = async {
            let (id, control, _control_handle) = requests
                .next()
                .await
                .expect("debug request stream not ended")
                .expect("debug request stream not error")
                .into_get_admin()
                .expect("get admin request");
            assert_eq!(id, interface1.nicid);

            let mut control: finterfaces_admin::ControlRequestStream =
                control.into_stream().expect("control request stream");
            let (
                addr,
                _addr_params,
                address_state_provider_server_end,
                _admin_control_control_handle,
            ) = control
                .next()
                .await
                .expect("control request stream not ended")
                .expect("control request stream not error")
                .into_add_address()
                .expect("add address request");
            assert_eq!(addr, IF_ADDR_V6);

            let mut address_state_provider_request_stream = address_state_provider_server_end
                .into_stream()
                .expect("address state provider FIDL error");
            async fn next_request(
                stream: &mut finterfaces_admin::AddressStateProviderRequestStream,
            ) -> finterfaces_admin::AddressStateProviderRequest {
                stream
                    .next()
                    .await
                    .expect("address state provider request stream not ended")
                    .expect("address state provider request stream not error")
            }

            let _address_state_provider_control_handle =
                next_request(&mut address_state_provider_request_stream)
                    .await
                    .into_detach()
                    .expect("detach request");

            for _ in 0..3 {
                let () = next_request(&mut address_state_provider_request_stream)
                    .await
                    .into_watch_address_assignment_state()
                    .expect("watch address assignment state request")
                    .send(finterfaces_admin::AddressAssignmentState::Tentative)
                    .expect("send address assignment state succeeds");
            }

            let () = next_request(&mut address_state_provider_request_stream)
                .await
                .into_watch_address_assignment_state()
                .expect("watch address assignment state request")
                .send(if interface_is_up {
                    finterfaces_admin::AddressAssignmentState::Assigned
                } else {
                    finterfaces_admin::AddressAssignmentState::Unavailable
                })
                .expect("send address assignment state succeeds");
        };

        let stack_fut = async {
            if let Some(mut stack_requests) = stack_stream {
                let (entry, responder) = stack_requests
                    .try_next()
                    .await
                    .expect("add route FIDL error")
                    .expect("request stream should not have ended")
                    .into_add_forwarding_entry()
                    .expect("request should be of type AddRoute");
                assert_eq!(
                    entry,
                    fstack::ForwardingEntry {
                        subnet: fnet_ext::apply_subnet_mask(fnet::Subnet {
                            addr: IF_ADDR_V6.addr,
                            prefix_len: TEST_PREFIX_LENGTH
                        }),
                        device_id: interface1.nicid,
                        next_hop: None,
                        metric: 0,
                    }
                );
                responder.send(&mut Ok(())).expect("responder.send should succeed");
            }
        };

        let ((), (), ()) = futures::join!(admin_fut, do_if_fut, stack_fut);
    }

    #[test_case(false ; "providing nicids")]
    #[test_case(true ; "providing interface names")]
    #[fasync::run_singlethreaded(test)]
    async fn if_del_addr(use_ifname: bool) {
        let interface1 = TestInterface { nicid: 1, name: "interface1" };
        let interface2 = TestInterface { nicid: 2, name: "interface2" };

        let (debug_interfaces, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fdebug::InterfacesMarker>().unwrap();
        let (interfaces_state, interfaces_requests) =
            fidl::endpoints::create_proxy_and_stream::<finterfaces::StateMarker>().unwrap();

        let (interface1_properties, _mac) = get_fake_interface(
            interface1.nicid,
            interface1.name,
            finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Ethernet),
            None,
        );

        let interfaces_fut =
            always_answer_with_interfaces(interfaces_requests, vec![interface1_properties.into()])
                .fuse();
        futures::pin_mut!(interfaces_fut);

        let connector = TestConnector {
            debug_interfaces: Some(debug_interfaces),
            interfaces_state: Some(interfaces_state),
            ..Default::default()
        };

        let mut out = ffx_writer::Writer::new_test(None);
        // Make the first request.
        let succeeds = do_if(
            &mut out,
            opts::IfEnum::Addr(opts::IfAddr {
                addr_cmd: opts::IfAddrEnum::Del(opts::IfAddrDel {
                    interface: interface1.identifier(use_ifname),
                    addr: fnet_ext::IpAddress::from(IF_ADDR_V4.addr).to_string(),
                    prefix: None, // The prefix should be set to the default of 32 for IPv4.
                }),
            }),
            &connector,
        )
        .map(|res| res.expect("success"));
        let handler_fut = async {
            let (id, control, _control_handle) = requests
                .next()
                .await
                .expect("debug request stream not ended")
                .expect("debug request stream not error")
                .into_get_admin()
                .expect("get admin request");
            assert_eq!(id, interface1.nicid);
            let mut control = control.into_stream().expect("control request stream");
            let (addr, responder) = control
                .next()
                .await
                .expect("control request stream not ended")
                .expect("control request stream not error")
                .into_remove_address()
                .expect("del address request");
            assert_eq!(addr, IF_ADDR_V4);
            let () = responder.send(&mut Ok(true)).expect("responder send");
        };

        futures::select! {
            () = interfaces_fut => panic!("interfaces_fut should never complete"),
            ((), ()) = futures::future::join(handler_fut, succeeds).fuse() => {},
        }

        let mut out = ffx_writer::Writer::new_test(None);
        // Make the second request.
        let fails = do_if(
            &mut out,
            opts::IfEnum::Addr(opts::IfAddr {
                addr_cmd: opts::IfAddrEnum::Del(opts::IfAddrDel {
                    interface: interface2.identifier(use_ifname),
                    addr: fnet_ext::IpAddress::from(IF_ADDR_V6.addr).to_string(),
                    prefix: Some(IF_ADDR_V6.prefix_len),
                }),
            }),
            &connector,
        )
        .map(|res| res.expect_err("failure"));

        if use_ifname {
            // The caller will have failed to find an interface matching the name,
            // so we don't expect any requests to make it to us.
            futures::select! {
                () = interfaces_fut => panic!("interfaces_fut should never complete"),
                e = fails.fuse() => {
                    assert_eq!(e.to_string(), format!("No interface with name {}", interface2.name));
                },
            }
        } else {
            let handler_fut = async {
                let (id, control, _control_handle) = requests
                    .next()
                    .await
                    .expect("debug request stream not ended")
                    .expect("debug request stream not error")
                    .into_get_admin()
                    .expect("get admin request");
                assert_eq!(id, interface2.nicid);
                let mut control = control.into_stream().expect("control request stream");
                let (addr, responder) = control
                    .next()
                    .await
                    .expect("control request stream not ended")
                    .expect("control request stream not error")
                    .into_remove_address()
                    .expect("del address request");
                assert_eq!(addr, IF_ADDR_V6);
                let () = responder.send(&mut Ok(false)).expect("responder send");
            };
            futures::select! {
                () = interfaces_fut => panic!("interfaces_fut should never complete"),
                ((), e) = futures::future::join(handler_fut, fails).fuse() => {
                    let fnet_ext::IpAddress(addr) = IF_ADDR_V6.addr.into();
                    assert_eq!(e.to_string(), format!("Address {} not found on interface {}", addr, interface2.nicid));
                },
            }
        }
    }

    fn wanted_net_if_list_json() -> String {
        json!([
            {
                "addresses": {
                    "ipv4": [],
                    "ipv6": [],
                },
                "device_class": "Loopback",
                "mac": "00:00:00:00:00:00",
                "name": "lo",
                "nicid": 1,
                "online": true,
            },
            {
                "addresses": {
                    "ipv4": [],
                    "ipv6": [],
                },
                "device_class": "Ethernet",
                "mac": "01:02:03:04:05:06",
                "name": "eth001",
                "nicid": 10,
                "online": true,
            },
            {
                "addresses": {
                    "ipv4": [],
                    "ipv6": [],
                },
                "device_class": "Virtual",
                "mac": null,
                "name": "virt001",
                "nicid": 20,
                "online": true,
            },
        ])
        .to_string()
    }

    fn wanted_net_if_list_tabular() -> String {
        String::from(
            r#"
nicid           1
name            lo
device class    loopback
online          true
mac             00:00:00:00:00:00

nicid           10
name            eth001
device class    ethernet
online          true
mac             01:02:03:04:05:06

nicid           20
name            virt001
device class    virtual
online          true
mac             -
"#,
        )
    }

    #[test_case(true, wanted_net_if_list_json() ; "in json format")]
    #[test_case(false, wanted_net_if_list_tabular() ; "in tabular format")]
    #[fasync::run_singlethreaded(test)]
    async fn if_list(json: bool, wanted_output: String) {
        let (debug_interfaces, debug_interfaces_stream) =
            fidl::endpoints::create_proxy_and_stream::<fdebug::InterfacesMarker>().unwrap();
        let (interfaces_state, interfaces_state_stream) =
            fidl::endpoints::create_proxy_and_stream::<finterfaces::StateMarker>().unwrap();

        let mut output = if json {
            ffx_writer::Writer::new_test(Some(ffx_writer::Format::Json))
        } else {
            ffx_writer::Writer::new_test(None)
        };
        let output_ref = &mut output;

        let do_if_fut = async {
            let connector = TestConnector {
                debug_interfaces: Some(debug_interfaces),
                interfaces_state: Some(interfaces_state),
                ..Default::default()
            };
            do_if(
                output_ref,
                opts::IfEnum::List(opts::IfList { name_pattern: None, json }),
                &connector,
            )
            .map(|res| res.expect("if list"))
            .await
        };
        let watcher_stream = interfaces_state_stream
            .and_then(|req| match req {
                finterfaces::StateRequest::GetWatcher {
                    options: _,
                    watcher,
                    control_handle: _,
                } => futures::future::ready(watcher.into_stream()),
            })
            .try_flatten()
            .map(|res| res.expect("watcher stream error"));
        let (interfaces, mac_addresses): (Vec<_>, HashMap<_, _>) = [
            get_fake_interface(
                1,
                "lo",
                finterfaces::DeviceClass::Loopback(finterfaces::Empty),
                Some([0, 0, 0, 0, 0, 0]),
            ),
            get_fake_interface(
                10,
                "eth001",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Ethernet),
                Some([1, 2, 3, 4, 5, 6]),
            ),
            get_fake_interface(
                20,
                "virt001",
                finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Virtual),
                None,
            ),
        ]
        .into_iter()
        .map(|(properties, mac)| {
            let finterfaces_ext::Properties { id, .. } = &properties;
            let id = *id;
            (properties, (id, mac))
        })
        .unzip();
        let interfaces =
            futures::stream::iter(interfaces.into_iter().map(Some).chain(std::iter::once(None)));
        let watcher_fut = watcher_stream.zip(interfaces).for_each(|(req, properties)| match req {
            finterfaces::WatcherRequest::Watch { responder } => {
                let mut event = properties.map_or(
                    finterfaces::Event::Idle(finterfaces::Empty),
                    |finterfaces_ext::Properties {
                         id,
                         name,
                         device_class,
                         online,
                         addresses,
                         has_default_ipv4_route,
                         has_default_ipv6_route,
                     }| {
                        finterfaces::Event::Existing(finterfaces::Properties {
                            id: Some(id),
                            name: Some(name),
                            device_class: Some(device_class),
                            online: Some(online),
                            addresses: Some(
                                addresses
                                    .into_iter()
                                    .map(|finterfaces_ext::Address { addr, valid_until }| {
                                        finterfaces::Address {
                                            addr: Some(addr),
                                            valid_until: Some(valid_until),
                                            ..finterfaces::Address::EMPTY
                                        }
                                    })
                                    .collect(),
                            ),
                            has_default_ipv4_route: Some(has_default_ipv4_route),
                            has_default_ipv6_route: Some(has_default_ipv6_route),
                            ..finterfaces::Properties::EMPTY
                        })
                    },
                );
                let () = responder.send(&mut event).expect("send watcher event");
                futures::future::ready(())
            }
        });
        let debug_fut = debug_interfaces_stream
            .map(|res| res.expect("debug interfaces stream error"))
            .for_each_concurrent(None, |req| {
                let (id, responder) = req.into_get_mac().expect("get_mac request");
                let () = responder
                    .send(
                        &mut mac_addresses
                            .get(&id)
                            .copied()
                            .map(|option| option.map(Box::new))
                            .ok_or(fdebug::InterfacesGetMacError::NotFound),
                    )
                    .expect("send get_mac response");
                futures::future::ready(())
            });
        let ((), (), ()) = futures::future::join3(do_if_fut, watcher_fut, debug_fut).await;

        let got_output = output.test_output().unwrap();

        if json {
            let got: Value = serde_json::from_str(&got_output).unwrap();
            let want: Value = serde_json::from_str(&wanted_output).unwrap();
            pretty_assertions::assert_eq!(got, want);
        } else {
            pretty_assertions::assert_eq!(
                trim_whitespace_for_comparison(&got_output),
                trim_whitespace_for_comparison(&wanted_output),
            );
        }
    }

    async fn test_do_dhcp(cmd: opts::DhcpEnum) {
        let (stack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fstack::StackMarker>().unwrap();
        let connector = TestConnector { stack: Some(stack), ..Default::default() };
        let op = do_dhcp(cmd.clone(), &connector);
        let op_succeeds = async move {
            let (expected_id, expected_enable) = match cmd {
                opts::DhcpEnum::Start(opts::DhcpStart { interface }) => (interface, true),
                opts::DhcpEnum::Stop(opts::DhcpStop { interface }) => (interface, false),
            };
            let request = requests
                .try_next()
                .await
                .expect("start FIDL error")
                .expect("request stream should not have ended");
            let (received_id, enable, responder) = request
                .into_set_dhcp_client_enabled()
                .expect("request should be of type StopDhcpClient");
            assert_eq!(opts::InterfaceIdentifier::Id(u64::from(received_id)), expected_id);
            assert_eq!(enable, expected_enable);
            responder.send(&mut Ok(())).map_err(anyhow::Error::new)
        };
        let ((), ()) =
            futures::future::try_join(op, op_succeeds).await.expect("dhcp command should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn dhcp_start() {
        let () = test_do_dhcp(opts::DhcpEnum::Start(opts::DhcpStart { interface: 1.into() })).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn dhcp_stop() {
        let () = test_do_dhcp(opts::DhcpEnum::Stop(opts::DhcpStop { interface: 1.into() })).await;
    }

    async fn test_modify_route(cmd: opts::RouteEnum) {
        let expected_interface = match &cmd {
            opts::RouteEnum::List(_) => panic!("test_modify_route should not take a List command"),
            opts::RouteEnum::Add(opts::RouteAdd { interface, .. }) => interface,
            opts::RouteEnum::Del(opts::RouteDel { interface, .. }) => interface,
        }
        .clone();
        let expected_id = match expected_interface {
            opts::InterfaceIdentifier::Id(ref id) => *id,
            opts::InterfaceIdentifier::Name(_) => {
                panic!("expected test to work only with ids")
            }
        };

        let (stack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fstack::StackMarker>().unwrap();
        let connector = TestConnector { stack: Some(stack), ..Default::default() };
        let mut out = ffx_writer::Writer::new_test(None);
        let op = do_route(&mut out, cmd.clone(), &connector);
        let op_succeeds = async move {
            let () = match cmd {
                opts::RouteEnum::List(opts::RouteList { json: _ }) => {
                    panic!("test_modify_route should not take a List command")
                }
                opts::RouteEnum::Add(route) => {
                    let expected_entry = route.into_route_table_entry(
                        expected_id.try_into().expect("nicid does not fit in u32"),
                    );
                    let (entry, responder) = requests
                        .try_next()
                        .await
                        .expect("add route FIDL error")
                        .expect("request stream should not have ended")
                        .into_add_forwarding_entry()
                        .expect("request should be of type AddRoute");
                    assert_eq!(entry, expected_entry);
                    responder.send(&mut Ok(()))
                }
                opts::RouteEnum::Del(route) => {
                    let expected_entry = route.into_route_table_entry(
                        expected_id.try_into().expect("nicid does not fit in u32"),
                    );
                    let (entry, responder) = requests
                        .try_next()
                        .await
                        .expect("del route FIDL error")
                        .expect("request stream should not have ended")
                        .into_del_forwarding_entry()
                        .expect("request should be of type DelRoute");
                    assert_eq!(entry, expected_entry);
                    responder.send(&mut Ok(()))
                }
            }?;
            Ok(())
        };
        let ((), ()) =
            futures::future::try_join(op, op_succeeds).await.expect("dhcp command should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn route_add() {
        // Test arguments have been arbitrarily selected.
        let () = test_modify_route(opts::RouteEnum::Add(opts::RouteAdd {
            destination: std::net::IpAddr::V4(std::net::Ipv4Addr::new(192, 168, 1, 0)),
            prefix_len: 24,
            gateway: None,
            interface: 2.into(),
            metric: 100,
        }))
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn route_del() {
        // Test arguments have been arbitrarily selected.
        let () = test_modify_route(opts::RouteEnum::Del(opts::RouteDel {
            destination: std::net::IpAddr::V4(std::net::Ipv4Addr::new(192, 168, 1, 0)),
            prefix_len: 24,
            gateway: None,
            interface: 2.into(),
            metric: 100,
        }))
        .await;
    }

    fn wanted_route_list_json() -> String {
        json!([
            {
                "destination":{"addr":"1.1.1.1","prefix_len":24},
                "gateway":"1.1.1.2",
                "metric":4,
                "nicid":3
            },
            {
                "destination":{"addr":"10.10.10.10","prefix_len":24},
                "gateway":"10.10.10.20",
                "metric":40,
                "nicid":30
            }
        ])
        .to_string()
    }

    fn wanted_route_list_tabular() -> String {
        "Destination       Gateway        NICID    Metric
         1.1.1.1/24        1.1.1.2        3        4
         10.10.10.10/24    10.10.10.20    30       40"
            .to_string()
    }

    #[test_case(true, wanted_route_list_json() ; "in json format")]
    #[test_case(false, wanted_route_list_tabular() ; "in tabular format")]
    #[fasync::run_singlethreaded(test)]
    async fn route_list(json: bool, wanted_output: String) {
        let (stack_controller, mut stack_requests) =
            fidl::endpoints::create_proxy_and_stream::<fstack::StackMarker>().unwrap();
        let connector = TestConnector { stack: Some(stack_controller), ..Default::default() };

        let mut output = if json {
            ffx_writer::Writer::new_test(Some(ffx_writer::Format::Json))
        } else {
            ffx_writer::Writer::new_test(None)
        };

        let do_route_fut =
            do_route(&mut output, opts::RouteEnum::List(opts::RouteList { json }), &connector);

        let requests_fut = async {
            let responder = stack_requests
                .try_next()
                .await
                .expect("stack FIDL error")
                .expect("request stream should not have ended")
                .into_get_forwarding_table()
                .expect("request should be of type GetForwardingTable");
            let () = responder
                .send(
                    &mut vec![
                        fstack::ForwardingEntry {
                            subnet: fnet::Subnet {
                                addr: fnet_ext::IpAddress::from_str("1.1.1.1")?.into(),
                                prefix_len: 24,
                            },
                            device_id: 3,
                            next_hop: Some(Box::new(
                                fnet_ext::IpAddress::from_str("1.1.1.2")?.into(),
                            )),
                            metric: 4,
                        },
                        fstack::ForwardingEntry {
                            subnet: fnet::Subnet {
                                addr: fnet_ext::IpAddress::from_str("10.10.10.10")?.into(),
                                prefix_len: 24,
                            },
                            device_id: 30,
                            next_hop: Some(Box::new(
                                fnet_ext::IpAddress::from_str("10.10.10.20")?.into(),
                            )),
                            metric: 40,
                        },
                    ]
                    .iter_mut(),
                )
                .expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(do_route_fut, requests_fut)
            .await
            .expect("listing forwarding table entries should succeed");

        let got_output = output.test_output().unwrap();

        if json {
            let got: Value = serde_json::from_str(&got_output).unwrap();
            let want: Value = serde_json::from_str(&wanted_output).unwrap();
            pretty_assertions::assert_eq!(got, want);
        } else {
            pretty_assertions::assert_eq!(
                trim_whitespace_for_comparison(&got_output),
                trim_whitespace_for_comparison(&wanted_output),
            );
        }
    }

    #[test_case(false ; "providing nicids")]
    #[test_case(true ; "providing interface names")]
    #[fasync::run_singlethreaded(test)]
    async fn bridge(use_ifname: bool) {
        let (netstack, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fnetstack::NetstackMarker>().unwrap();
        let (interfaces_state, interfaces_state_requests) =
            fidl::endpoints::create_proxy_and_stream::<finterfaces::StateMarker>().unwrap();
        let connector = TestConnector {
            netstack: Some(netstack),
            interfaces_state: Some(interfaces_state),
            ..Default::default()
        };

        let bridge_ifs = vec![
            TestInterface { nicid: 1, name: "interface1" },
            TestInterface { nicid: 2, name: "interface2" },
            TestInterface { nicid: 3, name: "interface3" },
        ];

        let interface_fidls = bridge_ifs
            .iter()
            .map(|interface| {
                let (interface, _mac) = get_fake_interface(
                    interface.nicid,
                    interface.name,
                    finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Ethernet),
                    None,
                );
                interface.into()
            })
            .collect::<Vec<_>>();

        let interfaces_fut =
            always_answer_with_interfaces(interfaces_state_requests, interface_fidls);

        let bridge_id = 4;
        let mut out = ffx_writer::Writer::new_test(None);
        let bridge = do_if(
            &mut out,
            opts::IfEnum::Bridge(opts::IfBridge {
                interfaces: bridge_ifs
                    .iter()
                    .map(|interface| interface.identifier(use_ifname))
                    .collect(),
            }),
            &connector,
        );

        let bridge_succeeds = async move {
            let (requested_ifs, netstack_responder) = requests
                .try_next()
                .await
                .expect("bridge_interfaces FIDL error")
                .expect("request stream should not have ended")
                .into_bridge_interfaces()
                .expect("request should be of type BridgeInterfaces");
            assert_eq!(
                requested_ifs,
                bridge_ifs
                    .iter()
                    .map(|interface| u32::try_from(interface.nicid).unwrap_or_else(|_| panic!(
                        "nicid {} does not fit in u32",
                        interface.nicid
                    )))
                    .collect::<Vec<_>>()
            );
            let () = netstack_responder
                .send(&mut fnetstack::Result_::Nicid(bridge_id))
                .expect("responder.send should succeed");
            Ok(())
        };
        futures::select! {
            () = interfaces_fut.fuse() => panic!("interfaces_fut should never complete"),
            result = futures::future::try_join(bridge, bridge_succeeds).fuse() => {
                let ((), ()) = result.expect("if bridge should succeed");
            }
        }
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
                let mut buf = ffx_writer::Writer::new_test(None);
                let () =
                    write_neigh_entry(&mut buf, item, watch_for_changes, /* json= */ false)
                        .expect("write_neigh_entry should succeed");
                buf.test_output().expect("string should be UTF-8")
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
    async fn neigh_list_none() {
        test_neigh_none(false /* watch_for_changes */, "".to_string()).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn neigh_watch_none() {
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
                neighbor: Some(IF_ADDR_V4.addr),
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
            want(fneighbor_ext::Entry::try_from(new_entry(updated_at)).unwrap()),
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn neigh_list_one() {
        test_neigh_one(false /* watch_for_changes */, |entry| format!("{}\n", entry)).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn neigh_watch_one() {
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
                    IF_ADDR_V4.addr,
                    MAC_1,
                    updated_at,
                )),
                fneighbor::EntryIteratorItem::Existing(new_entry(
                    IF_ADDR_V6.addr,
                    MAC_2,
                    updated_at - offset,
                )),
                fneighbor::EntryIteratorItem::Idle(fneighbor::IdleEvent {}),
            ]],
            want(
                fneighbor_ext::Entry::try_from(new_entry(IF_ADDR_V4.addr, MAC_1, updated_at))
                    .unwrap(),
                fneighbor_ext::Entry::try_from(new_entry(
                    IF_ADDR_V6.addr,
                    MAC_2,
                    updated_at - offset,
                ))
                .unwrap(),
            ),
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn neigh_list_many() {
        test_neigh_many(false /* watch_for_changes */, |a, b| format!("{}\n{}\n", a, b)).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn neigh_watch_many() {
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

    fn wanted_neigh_list_json() -> String {
        json!({
            "interface": 1,
            "mac": "01:02:03:04:05:06",
            "neighbor": "192.168.0.1",
            "state": "REACHABLE",
        })
        .to_string()
    }

    fn wanted_neigh_watch_json() -> String {
        json!({
            "entry": {
                "interface": 1,
                "mac": "01:02:03:04:05:06",
                "neighbor": "192.168.0.1",
                "state": "REACHABLE",
            },
            "state_change_status": "EXISTING",
        })
        .to_string()
    }

    #[test_case(true, false, &wanted_neigh_list_json() ; "in json format, not including entry state")]
    #[test_case(false, false, "Interface 1 | IP 192.168.0.1 | MAC 01:02:03:04:05:06 | REACHABLE" ; "in tabular format, not including entry state")]
    #[test_case(true, true, &wanted_neigh_watch_json() ; "in json format, including entry state")]
    #[test_case(false, true, "EXISTING | Interface 1 | IP 192.168.0.1 | MAC 01:02:03:04:05:06 | REACHABLE" ; "in tabular format, including entry state")]
    fn neigh_write_entry(json: bool, include_entry_state: bool, wanted_output: &str) {
        let entry = fneighbor::EntryIteratorItem::Existing(fneighbor::Entry {
            interface: Some(1),
            neighbor: Some(IF_ADDR_V4.addr),
            state: Some(fneighbor::EntryState::Reachable),
            mac: Some(MAC_1),
            updated_at: Some(timestamp_60s_ago()),
            ..fneighbor::Entry::EMPTY
        });

        let mut output = if json {
            ffx_writer::Writer::new_test(Some(ffx_writer::Format::Json))
        } else {
            ffx_writer::Writer::new_test(None)
        };
        write_neigh_entry(&mut output, entry, include_entry_state, json)
            .expect("write_neigh_entry should succeed");
        let got_output = output.test_output().unwrap();
        pretty_assertions::assert_eq!(
            trim_whitespace_for_comparison(&got_output),
            trim_whitespace_for_comparison(wanted_output),
        );
    }

    const INTERFACE_ID: u64 = 1;
    const IP_VERSION: fnet::IpVersion = fnet::IpVersion::V4;

    #[fasync::run_singlethreaded(test)]
    async fn neigh_add() {
        let (controller, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::ControllerMarker>().unwrap();
        let neigh = do_neigh_add(INTERFACE_ID, IF_ADDR_V4.addr, MAC_1, controller);
        let neigh_succeeds = async {
            let (got_interface_id, got_ip_address, got_mac, responder) = requests
                .try_next()
                .await
                .expect("neigh FIDL error")
                .expect("request stream should not have ended")
                .into_add_entry()
                .expect("request should be of type AddEntry");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_address, IF_ADDR_V4.addr);
            assert_eq!(got_mac, MAC_1);
            let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(neigh, neigh_succeeds)
            .await
            .expect("neigh add should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn neigh_clear() {
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
    async fn neigh_del() {
        let (controller, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fneighbor::ControllerMarker>().unwrap();
        let neigh = do_neigh_del(INTERFACE_ID, IF_ADDR_V4.addr, controller);
        let neigh_succeeds = async {
            let (got_interface_id, got_ip_address, responder) = requests
                .try_next()
                .await
                .expect("neigh FIDL error")
                .expect("request stream should not have ended")
                .into_remove_entry()
                .expect("request should be of type RemoveEntry");
            assert_eq!(got_interface_id, INTERFACE_ID);
            assert_eq!(got_ip_address, IF_ADDR_V4.addr);
            let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
            Ok(())
        };
        let ((), ()) = futures::future::try_join(neigh, neigh_succeeds)
            .await
            .expect("neigh remove should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn neigh_config_get() {
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
    async fn neigh_config_update() {
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

    #[test_case(opts::dhcpd::DhcpdEnum::Get(opts::dhcpd::Get {
            arg: opts::dhcpd::GetArg::Option(
                opts::dhcpd::OptionArg {
                    name: opts::dhcpd::Option_::SubnetMask(
                        opts::dhcpd::SubnetMask { mask: None }) }),
        }); "get option")]
    #[test_case(opts::dhcpd::DhcpdEnum::Get(opts::dhcpd::Get {
            arg: opts::dhcpd::GetArg::Parameter(opts::dhcpd::ParameterArg {
                name: opts::dhcpd::Parameter::LeaseLength(
                    opts::dhcpd::LeaseLength { default: None, max: None }),
            }),
        }); "get parameter")]
    #[test_case(opts::dhcpd::DhcpdEnum::Set(opts::dhcpd::Set {
            arg: opts::dhcpd::SetArg::Option(opts::dhcpd::OptionArg {
                name: opts::dhcpd::Option_::SubnetMask(opts::dhcpd::SubnetMask {
                    mask: Some(net_declare::std_ip_v4!("255.255.255.0")),
                }),
            }),
        }); "set option")]
    #[test_case(opts::dhcpd::DhcpdEnum::Set(opts::dhcpd::Set {
            arg: opts::dhcpd::SetArg::Parameter(opts::dhcpd::ParameterArg {
                name: opts::dhcpd::Parameter::LeaseLength(
                    opts::dhcpd::LeaseLength { max: Some(42), default: Some(42) }),
            }),
        }); "set parameter")]
    #[test_case(opts::dhcpd::DhcpdEnum::List(opts::dhcpd::List { arg:
        opts::dhcpd::ListArg::Option(opts::dhcpd::OptionToken {}) }); "list option")]
    #[test_case(opts::dhcpd::DhcpdEnum::List(
        opts::dhcpd::List { arg: opts::dhcpd::ListArg::Parameter(opts::dhcpd::ParameterToken {}) });
        "list parameter")]
    #[test_case(opts::dhcpd::DhcpdEnum::Reset(opts::dhcpd::Reset {
        arg: opts::dhcpd::ResetArg::Option(opts::dhcpd::OptionToken {}) }); "reset option")]
    #[test_case(opts::dhcpd::DhcpdEnum::Reset(
        opts::dhcpd::Reset {
            arg: opts::dhcpd::ResetArg::Parameter(opts::dhcpd::ParameterToken {}) });
        "reset parameter")]
    #[test_case(opts::dhcpd::DhcpdEnum::ClearLeases(opts::dhcpd::ClearLeases {}); "clear leases")]
    #[test_case(opts::dhcpd::DhcpdEnum::Start(opts::dhcpd::Start {}); "start")]
    #[test_case(opts::dhcpd::DhcpdEnum::Stop(opts::dhcpd::Stop {}); "stop")]
    #[fasync::run_singlethreaded(test)]
    async fn test_do_dhcpd(cmd: opts::dhcpd::DhcpdEnum) {
        let (dhcpd, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fdhcp::Server_Marker>()
                .expect("failed to create proxy and request stream for dhcp server");

        let connector = TestConnector { dhcpd: Some(dhcpd), ..Default::default() };
        let op = do_dhcpd(cmd.clone(), &connector);
        let op_succeeds = async move {
            let req = requests
                .try_next()
                .await
                .expect("receiving request")
                .expect("request stream should not have ended");
            match cmd {
                opts::dhcpd::DhcpdEnum::Get(opts::dhcpd::Get { arg }) => match arg {
                    opts::dhcpd::GetArg::Option(opts::dhcpd::OptionArg { name }) => {
                        let (code, responder) =
                            req.into_get_option().expect("request should be of type get option");
                        assert_eq!(
                            <opts::dhcpd::Option_ as Into<fdhcp::OptionCode>>::into(name),
                            code
                        );
                        // We don't care what the value is here, we just need something to give as
                        // an argument to responder.send().
                        let mut dummy_result =
                            Ok(fdhcp::Option_::SubnetMask(fidl_ip_v4!("255.255.255.0")));
                        let () = responder
                            .send(&mut dummy_result)
                            .expect("responder.send should succeed");
                        Ok(())
                    }
                    opts::dhcpd::GetArg::Parameter(opts::dhcpd::ParameterArg { name }) => {
                        let (param, responder) = req
                            .into_get_parameter()
                            .expect("request should be of type get parameter");
                        assert_eq!(
                            <opts::dhcpd::Parameter as Into<fdhcp::ParameterName>>::into(name),
                            param
                        );
                        // We don't care what the value is here, we just need something to give as
                        // an argument to responder.send().
                        let mut dummy_result = Ok(fdhcp::Parameter::Lease(fdhcp::LeaseLength {
                            ..fdhcp::LeaseLength::EMPTY
                        }));
                        let () = responder
                            .send(&mut dummy_result)
                            .expect("responder.send should succeed");
                        Ok(())
                    }
                },
                opts::dhcpd::DhcpdEnum::Set(opts::dhcpd::Set { arg }) => match arg {
                    opts::dhcpd::SetArg::Option(opts::dhcpd::OptionArg { name }) => {
                        let (opt, responder) =
                            req.into_set_option().expect("request should be of type set option");
                        assert_eq!(<opts::dhcpd::Option_ as Into<fdhcp::Option_>>::into(name), opt);
                        let () =
                            responder.send(&mut Ok(())).expect("responder.send should succeed");
                        Ok(())
                    }
                    opts::dhcpd::SetArg::Parameter(opts::dhcpd::ParameterArg { name }) => {
                        let (opt, responder) = req
                            .into_set_parameter()
                            .expect("request should be of type set parameter");
                        assert_eq!(
                            <opts::dhcpd::Parameter as Into<fdhcp::Parameter>>::into(name),
                            opt
                        );
                        let () =
                            responder.send(&mut Ok(())).expect("responder.send should succeed");
                        Ok(())
                    }
                },
                opts::dhcpd::DhcpdEnum::List(opts::dhcpd::List { arg }) => match arg {
                    opts::dhcpd::ListArg::Option(opts::dhcpd::OptionToken {}) => {
                        let responder = req
                            .into_list_options()
                            .expect("request should be of type list options");
                        let () =
                            responder.send(&mut Ok(vec![])).expect("responder.send should succeed");
                        Ok(())
                    }
                    opts::dhcpd::ListArg::Parameter(opts::dhcpd::ParameterToken {}) => {
                        let responder = req
                            .into_list_parameters()
                            .expect("request should be of type list options");
                        let () =
                            responder.send(&mut Ok(vec![])).expect("responder.send should succeed");
                        Ok(())
                    }
                },
                opts::dhcpd::DhcpdEnum::Reset(opts::dhcpd::Reset { arg }) => match arg {
                    opts::dhcpd::ResetArg::Option(opts::dhcpd::OptionToken {}) => {
                        let responder = req
                            .into_reset_options()
                            .expect("request should be of type reset options");
                        let () =
                            responder.send(&mut Ok(())).expect("responder.send should succeed");
                        Ok(())
                    }
                    opts::dhcpd::ResetArg::Parameter(opts::dhcpd::ParameterToken {}) => {
                        let responder = req
                            .into_reset_parameters()
                            .expect("request should be of type reset parameters");
                        let () =
                            responder.send(&mut Ok(())).expect("responder.send should succeed");
                        Ok(())
                    }
                },
                opts::dhcpd::DhcpdEnum::ClearLeases(opts::dhcpd::ClearLeases {}) => {
                    let responder =
                        req.into_clear_leases().expect("request should be of type clear leases");
                    let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
                    Ok(())
                }
                opts::dhcpd::DhcpdEnum::Start(opts::dhcpd::Start {}) => {
                    let responder =
                        req.into_start_serving().expect("request should be of type start serving");
                    let () = responder.send(&mut Ok(())).expect("responder.send should succeed");
                    Ok(())
                }
                opts::dhcpd::DhcpdEnum::Stop(opts::dhcpd::Stop {}) => {
                    let responder =
                        req.into_stop_serving().expect("request should be of type stop serving");
                    let () = responder.send().expect("responder.send should succeed");
                    Ok(())
                }
            }
        };
        let ((), ()) = futures::future::try_join(op, op_succeeds)
            .await
            .expect("dhcp server command should succeed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn dns_lookup() {
        let (lookup, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fname::LookupMarker>().unwrap();
        let connector = TestConnector { name_lookup: Some(lookup), ..Default::default() };

        let cmd = opts::dns::DnsEnum::Lookup(opts::dns::Lookup {
            hostname: "example.com".to_string(),
            ipv4: true,
            ipv6: true,
            sort: true,
        });
        let mut output = Vec::new();
        let dns_command = do_dns(&mut output, cmd.clone(), &connector)
            .map(|result| result.expect("dns command should succeed"));

        let handle_request = async move {
            let (hostname, options, responder) = requests
                .try_next()
                .await
                .expect("FIDL error")
                .expect("request stream should not have ended")
                .into_lookup_ip()
                .expect("request should be of type LookupIp");
            let opts::dns::DnsEnum::Lookup(opts::dns::Lookup {
                hostname: want_hostname,
                ipv4,
                ipv6,
                sort,
            }) = cmd;
            let want_options = fname::LookupIpOptions {
                ipv4_lookup: Some(ipv4),
                ipv6_lookup: Some(ipv6),
                sort_addresses: Some(sort),
                ..fname::LookupIpOptions::EMPTY
            };
            assert_eq!(
                hostname, want_hostname,
                "received IP lookup request for unexpected hostname"
            );
            assert_eq!(options, want_options, "received unexpected IP lookup options");

            responder
                .send(&mut Ok(fname::LookupResult {
                    addresses: Some(vec![fidl_ip!("203.0.113.1"), fidl_ip!("2001:db8::1")]),
                    ..fname::LookupResult::EMPTY
                }))
                .expect("send response");
        };
        let ((), ()) = futures::future::join(dns_command, handle_request).await;

        const WANT_OUTPUT: &str = "
203.0.113.1
2001:db8::1
";
        let got_output = std::str::from_utf8(&output).unwrap();
        pretty_assertions::assert_eq!(
            trim_whitespace_for_comparison(got_output),
            trim_whitespace_for_comparison(WANT_OUTPUT),
        );
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use crate::opts::*;
use failure::{format_err, Error, ResultExt};
use fidl::endpoints::{Proxy, ServiceMarker};
use fidl_fuchsia_net::{Ipv4Address, MacAddress};
use fidl_fuchsia_overnet::{OvernetMarker, OvernetProxy, Peer};
use fidl_fuchsia_overnet_protocol::NodeId;
use fidl_fuchsia_router_config::{
    Credentials, FilterAction, FilterRule, FlowSelector, Id, L2tp, LanProperties, PortRange, Pppoe,
    Pptp, Protocol, RouterAdminMarker, RouterAdminProxy, RouterStateMarker, RouterStateProxy,
    WanConnection, WanProperties,
};
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog as syslog;
use fuchsia_zircon::{self as zx, prelude::DurationNum};
use std::convert::TryInto;
use std::io::{self, BufWriter, Write};
use std::str;
use structopt::StructOpt;
mod opts;

static OVERNET_TIMEOUT_SEC: i64 = 30;

struct Printer<T: Write> {
    writer: BufWriter<T>,
}

impl<T> Printer<T>
where
    T: Write,
{
    fn new(output: T) -> Printer<T> {
        Printer { writer: BufWriter::new(output) }
    }

    fn println(&mut self, contents: String) {
        if let Err(e) = self.writer.write_all(contents.as_bytes()) {
            syslog::fx_log_err!("Error writing to buffer: {}", e);
            return;
        }

        if let Err(e) = self.writer.write_all("\n".as_bytes()) {
            syslog::fx_log_err!("Error writing to buffer: {}", e);
        }
    }

    // Consumes `BufWriter` and returns the underlying buffer.
    // Note that we never call `into_inner()` for the `io::stdout()` variant; so we need to
    // suppress rustc's `dead_code` lint warning here.
    #[allow(dead_code)]
    fn into_inner(self) -> Result<T, Error> {
        match self.writer.into_inner() {
            Ok(w) => Ok(w),
            Err(e) => Err(format_err!("Error getting writer: {}", e)),
        }
    }
}

/// Creates a `router_config::PortRange` from a port range string.
fn make_port_range(input_range: &str) -> Result<PortRange, Error> {
    let port_range = input_range
        .split("-")
        .map(|s| s.parse::<u16>())
        .filter_map(Result::ok)
        .collect::<Vec<u16>>();
    if port_range.len() != 2 {
        return Err(format_err!(
            "Invalid port range! Range is: 1 to 65535, with format: <from>-<to>"
        ));
    }
    if port_range[0] > port_range[1] {
        return Err(format_err!(
            "End of range must be less than or equal to start: {}-{}",
            port_range[0],
            port_range[1]
        ));
    }
    Ok(PortRange { from: port_range[0], to: port_range[1] })
}

/// Takes a string of port ranges as input, and produces a vector of `PortRange`'s as its output.
/// The format of `input_ranges` is: "<from>-<to>,<from>-<to>,...".
fn to_port_ranges(input_ranges: &str) -> Result<Vec<PortRange>, Error> {
    let (oks, fails): (Vec<_>, Vec<_>) =
        input_ranges.split(",").map(|range| make_port_range(range)).partition(Result::is_ok);
    if !fails.is_empty() {
        let errors: Vec<_> = fails.into_iter().map(Result::unwrap_err).collect();
        return Err(format_err!("Invalid port range: {:?}", errors));
    }
    Ok(oks.into_iter().map(Result::unwrap).collect::<Vec<PortRange>>())
}

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["router_manager_cli"]).expect("initialising logging");
    let Opt { overnet, cmd } = Opt::from_args();
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let fut = async {
        let (router_admin, router_state) = match overnet {
            true => {
                await!(connect_overnet().on_timeout(
                    fasync::Time::after(OVERNET_TIMEOUT_SEC.seconds()),
                    || {
                        syslog::fx_log_err!("no suitable overnet peers found");
                        Err(format_err!("could not find a suitable overnet peer"))
                    },
                ))?
            }
            false => connect()?,
        };
        let mut printer = Printer::new(io::stdout());
        await!(run_cmd(cmd, router_admin, router_state, &mut printer))
    };
    exec.run_singlethreaded(fut)
}

fn connect() -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
    let router_admin = connect_to_service::<RouterAdminMarker>()
        .context("failed to connect to router manager admin interface")?;
    let router_state = connect_to_service::<RouterStateMarker>()
        .context("failed to connect to router manager interface")?;
    Ok((router_admin, router_state))
}

fn connect_overnet_node(
    svc: &OvernetProxy,
    name: &str,
    node: &mut NodeId,
) -> Result<fasync::Channel, Error> {
    let (ch0, ch1) = zx::Channel::create()?;
    svc.connect_to_service(node, name, ch0)?;
    fasync::Channel::from_channel(ch1).map_err(|e| e.into())
}

fn supports_router_manager(peer: &Peer) -> bool {
    match peer.description.services {
        None => false,
        Some(ref services) => [RouterAdminMarker::NAME, RouterStateMarker::NAME]
            .iter()
            .map(|svc| services.contains(&svc.to_string()))
            .fold(true, |acc, v| acc && v),
    }
}

async fn connect_overnet() -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
    let svc = connect_to_service::<OvernetMarker>()?;
    let mut version: u64 = 0;
    syslog::fx_log_info!("looking for overnet peers...");
    loop {
        syslog::fx_log_info!("try #{:?}...", version);
        let (v, peers) = await!(svc.list_peers(version))?;
        version = v;
        for mut peer in peers {
            if !supports_router_manager(&peer) {
                continue;
            }
            match (
                connect_overnet_node(&svc, &RouterAdminMarker::NAME, &mut peer.id),
                connect_overnet_node(&svc, &RouterStateMarker::NAME, &mut peer.id),
            ) {
                (Err(_), _) | (_, Err(_)) => {
                    continue;
                }
                (Ok(router_admin_channel), Ok(router_state_channel)) => {
                    syslog::fx_log_info!("connected to peer {:?}", peer.id.id);
                    return Ok((
                        RouterAdminProxy::from_channel(router_admin_channel),
                        RouterStateProxy::from_channel(router_state_channel),
                    ));
                }
            }
        }
    }
}

async fn run_cmd<T: Write>(
    cmd: opts::Command,
    router_admin: RouterAdminProxy,
    router_state: RouterStateProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Command::ADD(cmd) => await!(do_add(cmd, router_admin, printer)),
        Command::SHOW(cmd) => await!(do_show(cmd, router_state, printer)),
        Command::SET(cmd) => await!(do_set(cmd, router_admin, printer)),
        Command::REMOVE(cmd) => await!(do_remove(cmd, router_admin, printer)),
    }
}

/// do_add function handles subcommands for adding WAN and LAN interfaces
async fn do_add<T: Write>(
    cmd: opts::Add,
    router_admin: RouterAdminProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Add::Wan { wan, ports, vlan } => {
            let response = match vlan {
                None => await!(router_admin.create_wan(&wan, 0, &mut ports.into_iter()))
                    .context("error getting response")?,
                Some(vlan) => await!(router_admin.create_wan(&wan, vlan, &mut ports.into_iter()))
                    .context("error getting response")?,
            };
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Add::Lan { lan, vlan, ports } => {
            let response = match vlan {
                None => await!(router_admin.create_lan(&lan, 0, &mut ports.into_iter()))
                    .context("error getting response")?,
                Some(vlan) => await!(router_admin.create_lan(&lan, vlan, &mut ports.into_iter()))
                    .context("error getting response")?,
            };
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

/// do_remove function handles subcommands for removing LAN and WAN interfaces
async fn do_remove<T: Write>(
    cmd: opts::Remove,
    router_admin: RouterAdminProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Remove::Wan { mut wan_id } => {
            let response =
                await!(router_admin.remove_wan(&mut wan_id)).context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Remove::Lan { mut lan_id } => {
            let response =
                await!(router_admin.remove_lan(&mut lan_id)).context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

/// do_show function handles subcommands for getting interfaces, ports and configuration
async fn do_show<T: Write>(
    cmd: opts::Show,
    router_state: RouterStateProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Show::Wan { mut wan } => {
            let response =
                await!(router_state.get_wan(&mut wan)).context("error getting response")?;
            printer.println(format!("{:?}", response));
            Ok(())
        }

        Show::Lan { mut lan } => {
            let response =
                await!(router_state.get_lan(&mut lan)).context("error getting response")?;
            printer.println(format!("{:?}", response));
            Ok(())
        }

        Show::WanConfig { mut wan } => {
            let response = await!(router_state.get_wan_properties(&mut wan))
                .context("error getting response")?;
            printer.println(format!("{:?} Response: {:?}", wan, response));
            Ok(())
        }

        Show::Port { port } => {
            let response = await!(router_state.get_port(port)).context("error getting response")?;
            printer.println(format!("{:?} Response: {:?}", port, response));
            Ok(())
        }

        Show::LanConfig { mut lan } => {
            let response = await!(router_state.get_lan_properties(&mut lan))
                .context("error getting response")?;
            printer.println(format!("{:?} Response: {:?}", lan, response));
            Ok(())
        }

        Show::DnsConfig {} => {
            printer.println("Get DNS config".to_string());
            Ok(())
        }

        Show::DhcpConfig { lan } => {
            printer.println(format!("{:?}", lan));
            Ok(())
        }

        Show::ForwardState { lan } => {
            printer.println(format!("{:?}", lan));
            Ok(())
        }

        Show::FilterState {} => {
            let response = await!(router_state.get_filters()).context("error getting response")?;
            printer.println(format!("{} filters installed", response.len()));
            printer.println(format!("{:?}", response));
            Ok(())
        }

        Show::Wans {} => {
            let mut response = await!(router_state.get_wans()).context("error getting response")?;
            response.sort_by_key(|a| {
                if let Some(id) = a.element {
                    u128::from_ne_bytes(id.uuid)
                } else {
                    0
                }
            });
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::Lans {} => {
            let mut response = await!(router_state.get_lans()).context("error getting response")?;
            response.sort_by_key(|a| {
                if let Some(id) = a.element {
                    u128::from_ne_bytes(id.uuid)
                } else {
                    0
                }
            });
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::WanPorts { mut wan } => {
            let mut response =
                await!(router_state.get_wan_ports(&mut wan)).context("error getting response")?;
            response.0.sort();
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::LanPorts { mut lan } => {
            let mut response =
                await!(router_state.get_lan_ports(&mut lan)).context("error getting response")?;
            response.0.sort();
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        // TODO: Implement pretty_print
        Show::Ports {} => {
            let mut response =
                await!(router_state.get_ports()).context("error getting response")?;
            response.sort_by_key(|a| a.path.clone());
            for port in response {
                printer.println(format!("{:?}", port));
            }
            Ok(())
        }

        Show::Routes {} => {
            let response = await!(router_state.get_routes()).context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

/// do_set function handles several subcommands that are used for setting wan and lan properties
async fn do_set<T: Write>(
    cmd: opts::Set,
    router_admin: RouterAdminProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    // wan_properties contains all the wan related configs that are sent to the server
    let mut wan_properties = WanProperties {
        connection_type: None,
        connection_parameters: None,
        address_method: None,
        address_v4: None,
        gateway_v4: None,
        connection_v6_mode: None,
        address_v6: None,
        gateway_v6: None,
        hostname: None,
        clone_mac: None,
        mtu: None,
        enable: None,
        metric: None,
    };
    // lan_properties contains all the lan related configs that are sent to the server
    let mut lan_properties = LanProperties {
        address_v4: None,
        enable_dhcp_server: None,
        dhcp_config: None,
        address_v6: None,
        enable_dns_forwarder: None,
        enable: None,
    };
    // This match statement populates the specified wan_properties field based on the subcommand
    // and calls the appropriate FIDL interface with the requested configuration changes. It then
    // receives the response and prints it for the CLI user
    match cmd {
        // TODO: Do the validation for "state" in opts.rs using enums
        // This subcommand handles state change request for a specific the WAN interface
        Set::WanState { mut wan_id, state } => {
            // Populating the "wan_properties.enable" field based on the user input
            wan_properties.enable = match state.as_ref() {
                "up" => Some(true),
                "down" => Some(false),
                _ => return Err(format_err!("Please provide a valid state: \"up\" or \"down\"")),
            };
            // Sending the request to the FIDL server and receiving the response
            // This can eventually be its own function with a "pretty print" feature
            printer.println(format!("Sending: {:?}", wan_properties));
            let response = await!(router_admin.set_wan_properties(&mut wan_id, wan_properties))
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
        // TODO: Do the validation for "connection" in opts.rs using enums
        // This subcommand sets the connection type for a specific WAN interface
        Set::WanConnection { mut wan_id, connection, username, password, server, metric, mtu } => {
            wan_properties.metric = metric;
            wan_properties.mtu = mtu;
            match connection {
                Some(m) => {
                    // This match statement populates the appropriate fields for wan_properties
                    // based on the requested connection type
                    match m.as_ref() {
                        // Following handles the cases for direct connection configuration
                        "direct" => {
                            wan_properties.connection_type = Some(WanConnection::Direct);
                        }
                        // Handling pppoe connection configuration
                        "pppoe" => {
                            if username.is_none() {
                                return Err(format_err!(
                                    "No username provided for PPPOE connection"
                                ));
                            }
                            if password.is_none() {
                                return Err(format_err!(
                                    "No password provided for PPPOE connection"
                                ));
                            }
                            wan_properties.connection_type = Some(WanConnection::PpPoE);
                            let pppoe_config = Pppoe {
                                credentials: Some(Credentials {
                                    user: username,
                                    password: password,
                                }),
                            };
                            // Applying the configurations to the wan_properties data structure
                            wan_properties.connection_parameters =
                                Some(fidl_fuchsia_router_config::ConnectionParameters::Pppoe(
                                    pppoe_config,
                                ));
                        }
                        // Handling pptp connection configuration
                        "pptp" => {
                            if username.is_none() {
                                return Err(format_err!(
                                    "No username provided for PPTP connection"
                                ));
                            }
                            if password.is_none() {
                                return Err(format_err!(
                                    "No password provided for PPTP connection"
                                ));
                            }
                            if server.is_none() {
                                return Err(format_err!(
                                    "No server IP provided for PPTP connection"
                                ));
                            }
                            // Configuring connection type
                            wan_properties.connection_type = Some(WanConnection::Pptp);
                            // Wrapping the server ip address in the Ipv4Address structure
                            let server_ipv4 = Ipv4Address { addr: server.unwrap().octets() };
                            // Wrapping username and password in "Credentials" structure and
                            // constructing the Pptp structure
                            let pptp_config = Pptp {
                                credentials: Some(Credentials {
                                    user: username,
                                    password: password,
                                }),
                                server: Some(fidl_fuchsia_net::IpAddress::Ipv4(server_ipv4)),
                            };
                            wan_properties.connection_parameters = Some(
                                fidl_fuchsia_router_config::ConnectionParameters::Pptp(pptp_config),
                            );
                        }
                        // handling l2tp connection configuration
                        "l2tp" => {
                            if username.is_none() {
                                return Err(format_err!(
                                    "No username provided for L2TP connection"
                                ));
                            }
                            if password.is_none() {
                                return Err(format_err!(
                                    "No password provided for L2TP connection"
                                ));
                            }
                            if server.is_none() {
                                return Err(format_err!(
                                    "No server IP provided for L2TP connection"
                                ));
                            }
                            wan_properties.connection_type = Some(WanConnection::L2Tp);
                            let server_ipv4 = Ipv4Address { addr: server.unwrap().octets() };
                            let l2tp_config = L2tp {
                                credentials: Some(Credentials {
                                    user: username,
                                    password: password,
                                }),
                                server: Some(fidl_fuchsia_net::IpAddress::Ipv4(server_ipv4)),
                            };
                            wan_properties.connection_parameters = Some(
                                fidl_fuchsia_router_config::ConnectionParameters::L2tp(l2tp_config),
                            );
                        }
                        _ => return Err(format_err!(
                            "Please provide a valid connection type: direct, pppoe, pptp or l2tp"
                        )),
                    }
                }
                None => {
                    if metric.is_none() && mtu.is_none() {
                        return Err(format_err!("Please either provide a valid connection type: direct/pppoe/pptp/l2tp or use an optional flag. For help, try \"set wan-connection --help\""));
                    }
                }
            }

            printer.println(format!("Sending: {:?}", wan_properties));
            let response = await!(router_admin.set_wan_properties(&mut wan_id, wan_properties))
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
        // This subcommand sets the IP configuration, dhcp, manual, etc... and sets an optional hostname
        Set::WanIp { mut wan_id, mode, ipv4, gateway, hostname } => {
            match mode {
                Some(m) => {
                    match m.as_ref() {
                        // handling DHCP IP configuration
                        "dhcp" => {
                            if !ipv4.is_none() {
                                return Err(format_err!(
                                    "Cannot provide IP address when enabling dhcp"
                                ));
                            }
                            if !gateway.is_none() {
                                return Err(format_err!(
                                    "Cannot provide gateway IP aadress when enabling dhcp"
                                ));
                            }
                            // Setting the address_method option of wan_properties to automatic
                            wan_properties.address_method =
                                Some(fidl_fuchsia_router_config::WanAddressMethod::Automatic);
                        }
                        // handling manual IP configuration
                        "manual" => {
                            // The ipv4 field is in CIDR notation form and it has to be split into an IP address and a prefix before being sent to server
                            let (ipv4_address, prefix_length) = match ipv4 {
                                Some(m) => (Ipv4Address { addr: m.address.octets() }, m.prefix),
                                None => {
                                    return Err(format_err!(
                                        "No IP address provided for manual config"
                                    ))
                                }
                            };
                            // Extracting the gateway IP from Ipv4Addr struct
                            let gateway_ipv4 = match gateway {
                                Some(m) => Ipv4Address { addr: m.octets() },
                                None => {
                                    return Err(format_err!(
                                        "No gateway IP address provided for manual config"
                                    ))
                                }
                            };
                            // Setting the address_method option of wan_properties to manual
                            wan_properties.address_method =
                                Some(fidl_fuchsia_router_config::WanAddressMethod::Manual);
                            // Populating "address_v4" using the extracted IP address and the prefix
                            wan_properties.address_v4 =
                                Some(fidl_fuchsia_router_config::CidrAddress {
                                    address: Some(fidl_fuchsia_net::IpAddress::Ipv4(ipv4_address)),
                                    prefix_length: Some(prefix_length),
                                });
                            // gateway_v4 is of type IpAddress::Ipv4, which is constructed using the gateway_ipv4
                            wan_properties.gateway_v4 =
                                Some(fidl_fuchsia_net::IpAddress::Ipv4(gateway_ipv4));
                        }
                        _ => {
                            return Err(format_err!(
                                "Please provide a valid IP configuration: dhcp or manual"
                            ))
                        }
                    }
                }
                None => {
                    if hostname.is_none() {
                        return Err(format_err!("Please either provide a valid connection method: manual/dhcp or use an optional flag"));
                    }
                }
            }
            // Setting the hostname in case this was provided
            wan_properties.hostname = hostname;
            printer.println(format!("Sending: {:?}", wan_properties));
            let response = await!(router_admin.set_wan_properties(&mut wan_id, wan_properties))
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        // This subcommand sets the MAC address for a specific WAN interface
        Set::WanCloneMac { mut wan_id, mac } => {
            wan_properties.clone_mac = Some(MacAddress { octets: mac.to_array() });
            printer.println(format!("Sending: {:?}", wan_properties));
            let response = await!(router_admin.set_wan_properties(&mut wan_id, wan_properties))
                .context("error getting response")?;
            printer.println(format!("{:?}", response));
            Ok(())
        }

        // TODO: Do the validation for "state" in opts.rs using enums
        // This subcommand handles state change request for a specific LAN interface
        Set::LanState { mut lan_id, state } => {
            // Populating the "enable" field based on the user input
            match state.as_ref() {
                "up" => {
                    lan_properties.enable = Some(true);
                }
                "down" => {
                    lan_properties.enable = Some(false);
                }
                _ => return Err(format_err!("Please provide a valid state: up or down")),
            }
            printer.println(format!("Sending: {:?} ID: {:?}", lan_properties, lan_id));
            let response = await!(router_admin.set_lan_properties(&mut lan_id, lan_properties))
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Set::LanIp { mut lan_id, ipv4 } => {
            // The ipv4 field is in CIDR notation form and it has to be split into an IP address and a prefix
            let (ipv4_address, prefix_length) = match ipv4 {
                Some(m) => (Ipv4Address { addr: m.address.octets() }, m.prefix),
                None => return Err(format_err!("No IP address provided for manual config")),
            };
            // Populating "address_v4" using the extracted IP address and the prefix
            lan_properties.address_v4 = Some(fidl_fuchsia_router_config::CidrAddress {
                address: Some(fidl_fuchsia_net::IpAddress::Ipv4(ipv4_address)),
                prefix_length: Some(prefix_length),
            });

            printer.println(format!("Sending: {:?} ID: {:?}", lan_properties, lan_id));
            let response = await!(router_admin.set_lan_properties(&mut lan_id, lan_properties))
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        // TODO: Do the validation for "state" in opts.rs using enums
        Set::LanDhcp { mut lan_id, state, lease_time_sec, gateway } => {
            // Populating the "enable_dhcp_server" field based on the user input
            match state.as_ref() {
                "up" => {
                    lan_properties.enable_dhcp_server = Some(true);
                }
                "down" => {
                    lan_properties.enable_dhcp_server = Some(false);
                }
                _ => return Err(format_err!("Please provide a valid state: up or down")),
            }

            // TODO: Populate these fields with the values received from the user
            if !lease_time_sec.is_none() {}
            if !gateway.is_none() {}

            printer.println(format!("Sending: {:?} ID: {:?}", lan_properties, lan_id));
            let response = await!(router_admin.set_lan_properties(&mut lan_id, lan_properties))
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Set::DhcpConfig { lan, dhcp_config } => {
            printer.println(format!("{:?}, {:?}", lan, dhcp_config));
            Ok(())
        }

        Set::DnsConfig { dns_config } => {
            printer.println(format!("{:?}", dns_config));
            Ok(())
        }

        Set::DnsForwarder { lan, enabled } => {
            printer.println(format!("{:?}, {:?}", lan, enabled));
            Ok(())
        }

        Set::Route { route } => {
            printer.println(format!("{:?}", route));
            Ok(())
        }

        Set::SecurityConfig { feature } => {
            printer.println(format!("{:?}", feature));
            Ok(())
        }

        Set::PortForward { rule } => {
            printer.println(format!("{:?}", rule));
            Ok(())
        }

        Set::Filter {
            action,
            src_address,
            src_port_range,
            dst_address,
            dst_port_range,
            protocol,
        } => {
            if src_address.is_none() && dst_address.is_none() {
                return Err(format_err!(
                    "Must provide at least a 'src_address' or a 'dst_address'"
                ));
            }
            let mut filter_rule = FilterRule {
                element: Id { uuid: [0; 16], version: 0 },
                action: match action.as_ref() {
                    "allow" => FilterAction::Allow,
                    "drop" => FilterAction::Drop,
                    _ => {
                        return Err(format_err!("Filter action must be: 'allow' or 'drop'"));
                    }
                },
                selector: FlowSelector {
                    src_address: match src_address {
                        Some(src_addr) => Some(src_addr.try_into()?),
                        None => None,
                    },
                    dst_address: match dst_address {
                        Some(dst_addr) => Some(dst_addr.try_into()?),
                        None => None,
                    },
                    src_ports: match src_port_range {
                        Some(ranges) => Some(to_port_ranges(&ranges)?),
                        None => None,
                    },
                    dst_ports: match dst_port_range {
                        Some(ranges) => Some(to_port_ranges(&ranges)?),
                        None => None,
                    },
                    protocol: match protocol {
                        Some(proto) => match proto.as_ref() {
                            "tcp" => Some(Protocol::Tcp),
                            "udp" => Some(Protocol::Udp),
                            "both" => Some(Protocol::Both),
                            _ => {
                                return Err(format_err!(
                                    "Protocol choices are: 'tcp', 'udp', or 'both'"
                                ))
                            }
                        },
                        None => Some(Protocol::Both),
                    },
                },
            };
            let response = await!(router_admin.set_filter(&mut filter_rule))
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::{create_proxy, ServiceMarker};
    use fidl_fuchsia_netemul_environment::{
        EnvironmentOptions, LaunchService, LoggerOptions, ManagedEnvironmentMarker,
        ManagedEnvironmentProxy,
    };
    use fidl_fuchsia_netemul_network::{
        EndpointConfig, EndpointManagerMarker, EndpointManagerProxy, EndpointProxy,
        NetworkContextMarker, NetworkContextProxy,
    };
    use fidl_fuchsia_netemul_sandbox::{SandboxMarker, SandboxProxy};
    use fuchsia_component::fuchsia_single_component_package_url as component_url;
    use regex::Regex;

    #[derive(Debug)]
    struct Test {
        command: String,
        expected: String,
    }

    impl Test {
        fn new<A: Into<String>, B: Into<String>>(command: A, expected: B) -> Self {
            Self { command: command.into(), expected: expected.into() }
        }
    }

    #[derive(Debug)]
    struct TestSuite {
        tests: Vec<Test>,
    }

    macro_rules! test_suite {
        [$( ($cmd:expr,$expected:expr), )*] => {
            TestSuite {
                tests: vec![ $(Test::new($cmd,$expected),)*]
            }
        };
    }

    fn generate_launch_services() -> Vec<LaunchService> {
        let names_and_urls = vec![
            ("fuchsia.net.stack.Stack", component_url!("netstack")),
            ("fuchsia.netstack.Netstack", component_url!("netstack")),
            ("fuchsia.net.SocketProvider", component_url!("netstack")),
            ("fuchsia.net.NameLookup", component_url!("netstack")),
            ("fuchsia.posix.socket.Provider", component_url!("netstack")),
            ("fuchsia.net.filter.Filter", component_url!("netstack")),
            ("fuchsia.router.config.RouterAdmin", component_url!("router_manager")),
            ("fuchsia.router.config.RouterState", component_url!("router_manager")),
        ];
        names_and_urls
            .into_iter()
            .map(|(name, url)| LaunchService {
                name: name.to_string(),
                url: url.to_string(),
                arguments: None,
            })
            .collect()
    }

    fn get_network_context(sandbox: &SandboxProxy) -> Result<NetworkContextProxy, Error> {
        let (client, server) = fidl::endpoints::create_proxy::<NetworkContextMarker>()
            .context("failed to create network context proxy")?;
        let () = sandbox.get_network_context(server).context("failed to get network context")?;
        Ok(client)
    }

    fn get_endpoint_manager(
        network_context: &NetworkContextProxy,
    ) -> Result<EndpointManagerProxy, failure::Error> {
        let (client, server) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()
            .context("failed to create endpoint manager proxy")?;
        let () = network_context
            .get_endpoint_manager(server)
            .context("failed to get endpoint manager")?;
        Ok(client)
    }

    async fn create_endpoint<'a>(
        name: &'static str,
        endpoint_manager: &'a EndpointManagerProxy,
    ) -> std::result::Result<EndpointProxy, failure::Error> {
        let (status, endpoint) = await!(endpoint_manager.create_endpoint(
            name,
            &mut EndpointConfig {
                mtu: 1500,
                mac: None,
                backing: fidl_fuchsia_netemul_network::EndpointBacking::Ethertap,
            },
        ))
            .context("failed to create endpoint")?;
        let () = fuchsia_zircon::Status::ok(status).context("failed to create endpoint")?;
        let endpoint = endpoint
            .ok_or(failure::err_msg("failed to create endpoint"))?
            .into_proxy()
            .context("failed to get endpoint proxy")?;
        Ok(endpoint)
    }

    fn create_managed_env(sandbox: &SandboxProxy) -> Result<ManagedEnvironmentProxy, Error> {
        let (env, env_server_end) = create_proxy::<ManagedEnvironmentMarker>()?;
        let services = generate_launch_services();
        sandbox.create_environment(
            env_server_end,
            EnvironmentOptions {
                name: Some(String::from("router_manager_env")),
                services: Some(services),
                devices: None,
                inherit_parent_launch_services: None,
                logger_options: Some(LoggerOptions {
                    enabled: Some(true),
                    klogs_enabled: Some(false),
                    filter_options: None,
                    syslog_output: Some(true),
                }),
            },
        )?;
        Ok(env)
    }

    fn connect_to_sandbox_service<S: fidl::endpoints::ServiceMarker>(
        managed_environment: &ManagedEnvironmentProxy,
    ) -> Result<S::Proxy, failure::Error> {
        let (proxy, server) = fuchsia_zircon::Channel::create()?;
        let () = managed_environment.connect_to_service(S::NAME, server)?;
        let proxy = fuchsia_async::Channel::from_channel(proxy)?;
        Ok(<S::Proxy as fidl::endpoints::Proxy>::from_channel(proxy))
    }

    fn connect_router_manager(
        env: &ManagedEnvironmentProxy,
    ) -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
        let (router_admin, router_admin_server_end) = create_proxy::<RouterAdminMarker>()?;
        env.connect_to_service(RouterAdminMarker::NAME, router_admin_server_end.into_channel())
            .context("Can't connect to router_manager admin endpoint")?;
        let (router_state, router_state_server_end) = create_proxy::<RouterStateMarker>()?;
        env.connect_to_service(RouterStateMarker::NAME, router_state_server_end.into_channel())
            .context("Can't connect to router_manager state endpoint")?;
        Ok((router_admin, router_state))
    }

    async fn add_ethernet_device(
        netstack_proxy: fidl_fuchsia_netstack::NetstackProxy,
        device: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    ) -> Result<(), Error> {
        let name = "/mock_device";
        let id = await!(netstack_proxy.add_ethernet_device(
            name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: name.to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
                ip_address_config: fidl_fuchsia_netstack::IpAddressConfig::Dhcp(true),
            },
            device,
        ))
            .context("failed to add ethernet device")?;

        // Check that the newly added ethernet interface is present before continuing with the
        // actual tests.
        let interface = await!(netstack_proxy.get_interfaces2())
            .expect("failed to get interfaces")
            .into_iter()
            .find(|interface| interface.id == id)
            .ok_or(failure::err_msg("failed to find added ethernet device"))
            .unwrap();
        assert_eq!(interface.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK, 0);
        assert_eq!(interface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP, 0);
        Ok(())
    }

    fn make_cmd(command: &str) -> Result<opts::Command, Error> {
        let mut args = command.split_whitespace().collect::<Vec<&str>>();
        // `from_*` methods silently drop the first element of the vector, since that is /usually/
        // the program name.
        args.insert(0, "cli_test");
        let opts = Opt::from_iter_safe(args.into_iter())?;
        Ok(opts.cmd)
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn test_show_commands() {
        // The pair is constructed of the CLI command to test, and it's expected output as a
        // regular expression parsable by the Regex crate.
        let show_commands = test_suite![
            // ("command to test", "expected regex match statement"),
            ("show dhcpconfig 0",
             "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}"),
            ("show dnsconfig", "Get DNS config"),
            ("show filterstate", "0 filters installed.*"),
            ("show forwardstate 0",
             "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}"),
            ("show lanconfig 0",
             "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\} Response: \\(LanProperties \\{ address_v4: None, enable_dhcp_server: None, dhcp_config: None, address_v6: None, enable_dns_forwarder: None, enable: None \\}, Some\\(Error \\{ code: NotSupported, description: None \\}\\)"),
            ("show ports",
             "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock_device\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" }"),
            ("show routes", "Response: \\[\\]"),
            ("show wans", "Response: \\[\\]"),
        ];

        for test in show_commands.tests {
            let sandbox = connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
            let network_context =
                get_network_context(&sandbox).expect("failed to get network context");
            let endpoint_manager =
                get_endpoint_manager(&network_context).expect("failed to get endpoint manager");
            let endpoint = await!(create_endpoint(stringify!(test_interface), &endpoint_manager))
                .expect("failed to create endpoint");
            let device =
                await!(endpoint.get_ethernet_device()).expect("failed to get ethernet device");
            let env =
                create_managed_env(&sandbox).expect("Failed to create environment with services");
            let netstack_proxy =
                connect_to_sandbox_service::<fidl_fuchsia_netstack::NetstackMarker>(&env)
                    .expect("failed to connect to netstack");
            let _ = await!(add_ethernet_device(netstack_proxy, device));
            let (router_admin, router_state) =
                connect_router_manager(&env).expect("Failed to connect from managed environment");

            let mut printer = Printer::new(Vec::new());
            let cmd = match make_cmd(&test.command) {
                Ok(cmd) => cmd,
                Err(e) => panic!("Failed to parse command: '{}', because: {}", test.command, e),
            };

            let actual_output;
            match await!(run_cmd(cmd, router_admin, router_state, &mut printer)) {
                Ok(_) => {
                    // TODO(cgibson): Figure out a way to add this as a helper method on `Printer`.
                    actual_output =
                        str::from_utf8(&printer.into_inner().unwrap()).unwrap().to_string();
                }
                Err(e) => panic!("Running command '{}' failed: {:?}", test.command, e),
            }
            println!("command: {}", test.command);
            println!("actual: {:?}", actual_output);
            let re = Regex::new(&test.expected).unwrap();
            assert!(re.is_match(&actual_output));
        }
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn test_filters() {
        let commands = test_suite![
            ("show filterstate", "0 filters installed"),
            ("set filter allow 0.0.0.0/0 22-22 0.0.0.0/0 22-22", "Response: \\(None, None\\)"),
            ("show filterstate", "2 filters installed\n\\[FilterRule \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, action: Allow, selector: FlowSelector \\{ src_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), src_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), dst_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), dst_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), protocol: Some\\(Tcp\\) \\} \\}, FilterRule \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, action: Allow, selector: FlowSelector \\{ src_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), src_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), dst_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), dst_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), protocol: Some\\(Udp\\) \\} \\}\\]"),
        ];

        let sandbox = connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
        let env = create_managed_env(&sandbox).expect("Failed to create environment with services");
        for test in commands.tests {
            let (router_admin, router_state) =
                connect_router_manager(&env).expect("Failed to connect from managed environment");
            let mut printer = Printer::new(Vec::new());
            let cmd = match make_cmd(&test.command) {
                Ok(cmd) => cmd,
                Err(e) => panic!("Failed to parse command: '{}', because: {}", test.command, e),
            };

            let actual_output;
            match await!(run_cmd(cmd, router_admin, router_state, &mut printer)) {
                Ok(_) => {
                    // TODO(cgibson): Figure out a way to add this as a helper method on `Printer`.
                    actual_output =
                        str::from_utf8(&printer.into_inner().unwrap()).unwrap().to_string();
                }
                Err(e) => panic!("Running command '{}' failed: {:?}", test.command, e),
            }
            println!("command: {}", test.command);
            println!("actual: {:?}", actual_output);
            let re = Regex::new(&test.expected).unwrap();
            assert!(re.is_match(&actual_output));
        }
    }

    #[test]
    fn test_output_to_buffer() {
        let mut printer = Printer::new(Vec::new());
        let msg = "Hello, World!";
        let newline = "\n";
        printer.println("".into());
        printer.println(msg.to_string());
        printer.println(msg.to_string());
        assert_eq!(
            // TODO(cgibson): Figure out a way to add this as a helper method on `Printer`.
            str::from_utf8(&printer.into_inner().unwrap()).unwrap().to_string(),
            format!("{}{}{}{}{}", newline, msg, newline, msg, newline)
        );
    }

    #[test]
    fn test_to_port_ranges() {
        let from = "22";
        let to = "222";
        let port_range = to_port_ranges(format!("{}-{}", from, to).as_ref()).unwrap()[0];
        assert_eq!(port_range.from, from.parse::<u16>().unwrap());
        assert_eq!(port_range.to, to.parse::<u16>().unwrap());

        let from = "22";
        let to = "22";
        let port_range = to_port_ranges(format!("{}-{}", from, to).as_ref()).unwrap()[0];
        assert_eq!(port_range.from, from.parse::<u16>().unwrap());
        assert_eq!(port_range.to, to.parse::<u16>().unwrap());

        let port_ranges = to_port_ranges("20-21,25-25").unwrap();
        let errors = port_ranges.iter().filter(|port_range| {
            if (port_range.from == 20 && port_range.to == 21)
                || (port_range.from == 25 && port_range.to == 25)
            {
                false
            } else {
                true
            }
        });
        assert_eq!(errors.count(), 0);

        let port_range = to_port_ranges("80,");
        assert_eq!(port_range.is_err(), true);

        let port_range = to_port_ranges("-");
        assert_eq!(port_range.is_err(), true);

        let from = "222";
        let to = "22";
        let port_range = to_port_ranges(format!("{}-{}", from, to).as_ref());
        assert_eq!(port_range.is_err(), true);

        let from = "abc";
        let to = "22";
        let port_range = to_port_ranges(format!("{}-{}", from, to).as_ref());
        assert_eq!(port_range.is_err(), true);
    }
}

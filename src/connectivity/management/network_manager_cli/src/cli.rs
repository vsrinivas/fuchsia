// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::opts::*;
use crate::printer::Printer;
use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::{Proxy, ServiceMarker};
use fidl_fuchsia_net::{Ipv4Address, Ipv6Address, MacAddress};
use fidl_fuchsia_overnet::{Peer, ServiceConsumerMarker, ServiceConsumerProxy};
use fidl_fuchsia_overnet_protocol::NodeId;
use fidl_fuchsia_router_config::{
    Credentials, FilterAction, FilterRule, FlowSelector, Id, L2tp, LanProperties, PortRange, Pppoe,
    Pptp, Protocol, RouterAdminMarker, RouterAdminProxy, RouterStateMarker, RouterStateProxy,
    SecurityFeatures, WanConnection, WanProperties,
};
use fuchsia_async::{self as fasync};
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog as syslog;
use fuchsia_zircon::{self as zx};
use itertools::Itertools;
use std::convert::TryInto;
use std::io::Write;
use std::net::IpAddr;
use std::str;

/// Creates a `router_config::PortRange` from a port range string.
fn make_port_range(input_range: &str) -> Result<PortRange, Error> {
    let port_range = input_range
        .split('-')
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
        input_ranges.split(',').map(|range| make_port_range(range)).partition(Result::is_ok);
    if !fails.is_empty() {
        let errors: Vec<_> = fails.into_iter().map(Result::unwrap_err).collect();
        return Err(format_err!("Invalid port range: {:?}", errors));
    }
    Ok(oks.into_iter().map(Result::unwrap).collect::<Vec<PortRange>>())
}

pub fn connect() -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
    let router_admin = connect_to_service::<RouterAdminMarker>()
        .context("failed to connect to network manager admin interface")?;
    let router_state = connect_to_service::<RouterStateMarker>()
        .context("failed to connect to network manager interface")?;
    Ok((router_admin, router_state))
}

fn connect_overnet_node(
    svc: &ServiceConsumerProxy,
    name: &str,
    node: &mut NodeId,
) -> Result<fasync::Channel, Error> {
    let (ch0, ch1) = zx::Channel::create()?;
    svc.connect_to_service(node, name, ch0)?;
    fasync::Channel::from_channel(ch1).map_err(|e| e.into())
}

fn supports_network_manager(peer: &Peer) -> bool {
    match peer.description.services {
        None => false,
        Some(ref services) => [RouterAdminMarker::NAME, RouterStateMarker::NAME]
            .iter()
            .map(|svc| services.contains(&svc.to_string()))
            .all(|v| v),
    }
}

pub async fn connect_overnet() -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
    let svc = connect_to_service::<ServiceConsumerMarker>()?;
    syslog::fx_log_info!("looking for overnet peers...");
    loop {
        for mut peer in svc.list_peers().await? {
            if !supports_network_manager(&peer) {
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

pub fn make_cmd(command: &str) -> Result<Command, Error> {
    let mut args = command.split_whitespace().collect::<Vec<&str>>();
    // `from_*` methods silently drop the first element of the vector, since that is /usually/
    // the program name.
    args.insert(0, "cli_test");
    let opts = Opt::from_iter_safe(args.into_iter())?;
    Ok(opts.cmd)
}

pub async fn run_cmd<T: Write>(
    cmd: Command,
    router_admin: RouterAdminProxy,
    router_state: RouterStateProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Command::ADD(cmd) => do_add(cmd, router_admin, printer).await,
        Command::SHOW(cmd) => do_show(cmd, router_state, printer).await,
        Command::SET(cmd) => do_set(cmd, router_admin, printer).await,
        Command::REMOVE(cmd) => do_remove(cmd, router_admin, printer).await,
    }
}

/// do_add function handles subcommands for adding WAN and LAN interfaces
async fn do_add<T: Write>(
    cmd: Add,
    router_admin: RouterAdminProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Add::Wan { name, ports, vlan } => {
            let response = match vlan {
                None => router_admin
                    .create_wan(&name, 0, &mut ports.into_iter())
                    .await
                    .context("error getting response")?,
                Some(vlan) => router_admin
                    .create_wan(&name, vlan, &mut ports.into_iter())
                    .await
                    .context("error getting response")?,
            };
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Add::Lan { name, vlan, ports } => {
            let response = match vlan {
                None => router_admin
                    .create_lan(&name, 0, &mut ports.into_iter())
                    .await
                    .context("error getting response")?,
                Some(vlan) => router_admin
                    .create_lan(&name, vlan, &mut ports.into_iter())
                    .await
                    .context("error getting response")?,
            };
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

/// do_remove function handles subcommands for removing LAN and WAN interfaces
async fn do_remove<T: Write>(
    cmd: Remove,
    router_admin: RouterAdminProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Remove::Wan { mut wan_id } => {
            let response =
                router_admin.remove_wan(&mut wan_id).await.context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Remove::Lan { mut lan_id } => {
            let response =
                router_admin.remove_lan(&mut lan_id).await.context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

/// do_show function handles subcommands for getting interfaces, ports and configuration
async fn do_show<T: Write>(
    cmd: Show,
    router_state: RouterStateProxy,
    printer: &mut Printer<T>,
) -> Result<(), Error> {
    match cmd {
        Show::Wan { mut wan_id } => {
            let response =
                router_state.get_wan(&mut wan_id).await.context("error getting response")?;
            printer.println(format!("{:?}", response));
            Ok(())
        }

        Show::Lan { mut lan_id } => {
            let response =
                router_state.get_lan(&mut lan_id).await.context("error getting response")?;
            printer.println(format!("{:?}", response));
            Ok(())
        }

        Show::WanConfig { mut wan_id } => {
            let response = router_state
                .get_wan_properties(&mut wan_id)
                .await
                .context("error getting response")?;
            printer.println(format!("{:?} Response: {:?}", wan_id, response));
            Ok(())
        }

        Show::Port { port } => {
            let response = router_state.get_port(port).await.context("error getting response")?;
            printer.println(format!("{:?} Response: {:?}", port, response));
            Ok(())
        }

        Show::LanConfig { mut lan_id } => {
            let response = router_state
                .get_lan_properties(&mut lan_id)
                .await
                .context("error getting response")?;
            printer.println(format!("{:?} Response: {:?}", lan_id, response));
            Ok(())
        }

        Show::DnsConfig {} => {
            let response =
                router_state.get_dns_resolver().await.context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::DhcpConfig { lan_id } => {
            printer.println(format!("{:?}", lan_id));
            Ok(())
        }

        Show::ForwardState { lan_id } => {
            printer.println(format!("{:?}", lan_id));
            Ok(())
        }

        Show::FilterState {} => {
            let response = router_state.get_filters().await.context("error getting response")?;
            printer.println(format!("{} filters installed", response.len()));
            printer.println(format!("{:?}", response));
            Ok(())
        }

        Show::Wans {} => {
            let response = router_state
                .get_wans()
                .await
                .context("error getting response")?
                .into_iter()
                .map(|mut l| {
                    l.port_ids.as_mut().map(|x| x.sort());
                    l
                })
                .sorted_by_key(|a| a.element.map_or(0, |id| u128::from_ne_bytes(id.uuid)))
                .collect_vec();
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::Lans {} => {
            let response = router_state
                .get_lans()
                .await
                .context("error getting response")?
                .into_iter()
                .map(|mut l| {
                    l.port_ids.as_mut().map(|x| x.sort());
                    l
                })
                .sorted_by_key(|a| a.element.map_or(0, |id| u128::from_ne_bytes(id.uuid)))
                .collect_vec();
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::WanPorts { mut wan_id } => {
            let mut response =
                router_state.get_wan_ports(&mut wan_id).await.context("error getting response")?;
            response.0.sort();
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::LanPorts { mut lan_id } => {
            let mut response =
                router_state.get_lan_ports(&mut lan_id).await.context("error getting response")?;
            response.0.sort();
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        // TODO: Implement pretty_print
        Show::Ports {} => {
            let mut response = router_state.get_ports().await.context("error getting response")?;
            response.sort_by_key(|a| a.path.clone());
            for port in response {
                printer.println(format!("{:?}", port));
            }
            Ok(())
        }

        Show::Routes {} => {
            let response = router_state.get_routes().await.context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Show::Security {} => {
            let response =
                router_state.get_security_features().await.context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

/// do_set function handles several subcommands that are used for setting wan and lan properties
async fn do_set<T: Write>(
    cmd: Set,
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
            let response = router_admin
                .set_wan_properties(&mut wan_id, wan_properties)
                .await
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
                                credentials: Some(Credentials { user: username, password }),
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
                                credentials: Some(Credentials { user: username, password }),
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
                                credentials: Some(Credentials { user: username, password }),
                                server: Some(fidl_fuchsia_net::IpAddress::Ipv4(server_ipv4)),
                            };
                            wan_properties.connection_parameters = Some(
                                fidl_fuchsia_router_config::ConnectionParameters::L2tp(l2tp_config),
                            );
                        }
                        _ => {
                            return Err(format_err!(
                            "Please provide a valid connection type: direct, pppoe, pptp or l2tp"
                        ))
                        }
                    }
                }
                None => {
                    if metric.is_none() && mtu.is_none() {
                        return Err(format_err!("Please either provide a valid connection type: direct/pppoe/pptp/l2tp or use an optional flag. For help, try \"set wan-connection --help\""));
                    }
                }
            }

            printer.println(format!("Sending: {:?}", wan_properties));
            let response = router_admin
                .set_wan_properties(&mut wan_id, wan_properties)
                .await
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
                            if ipv4.is_some() {
                                return Err(format_err!(
                                    "Cannot provide IP address when enabling dhcp"
                                ));
                            }
                            if gateway.is_some() {
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
            let response = router_admin
                .set_wan_properties(&mut wan_id, wan_properties)
                .await
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        // This subcommand sets the MAC address for a specific WAN interface
        Set::WanCloneMac { mut wan_id, mac } => {
            wan_properties.clone_mac = Some(MacAddress { octets: mac.to_array() });
            printer.println(format!("Sending: {:?}", wan_properties));
            let response = router_admin
                .set_wan_properties(&mut wan_id, wan_properties)
                .await
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
            let response = router_admin
                .set_lan_properties(&mut lan_id, lan_properties)
                .await
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
            let response = router_admin
                .set_lan_properties(&mut lan_id, lan_properties)
                .await
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
            if lease_time_sec.is_some() {}
            if gateway.is_some() {}

            printer.println(format!("Sending: {:?} ID: {:?}", lan_properties, lan_id));
            let response = router_admin
                .set_lan_properties(&mut lan_id, lan_properties)
                .await
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Set::DhcpConfig { lan_id, dhcp_config } => {
            printer.println(format!("Not Implemented {:?}, {:?}", lan_id, dhcp_config));
            Ok(())
        }

        Set::DnsConfig { server } => {
            printer.println(format!("Sending: {:?}", server));
            let servers = match server {
                IpAddr::V4(address) => {
                    vec![fidl_fuchsia_net::IpAddress::Ipv4(Ipv4Address { addr: address.octets() })]
                }
                IpAddr::V6(address) => {
                    vec![fidl_fuchsia_net::IpAddress::Ipv6(Ipv6Address { addr: address.octets() })]
                }
            };

            let mut config = fidl_fuchsia_router_config::DnsResolverConfig {
                element: Id { uuid: [0; 16], version: 0 },
                policy: fidl_fuchsia_router_config::DnsPolicy::NotSet,
                search: fidl_fuchsia_router_config::DnsSearch { domain_name: None, servers },
            };
            let response = router_admin.set_dns_resolver(&mut config).await;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Set::DnsForwarder { lan_id, enabled } => {
            printer.println(format!("Not Implemented {:?}, {:?}", lan_id, enabled));
            Ok(())
        }

        Set::Route { route } => {
            printer.println(format!("Not Implemented {:?}", route));
            Ok(())
        }

        Set::SecurityConfig { feature, enabled } => {
            let mut security_features = SecurityFeatures {
                pptp_passthru: None,
                l2_tp_passthru: None,
                ipsec_passthru: None,
                rtsp_passthru: None,
                h323_passthru: None,
                sip_passthru: None,
                allow_multicast: None,
                nat: None,
                firewall: None,
                v6_firewall: None,
                upnp: None,
                drop_icmp_echo: None,
            };
            printer.println(format!(
                "Setting security feature: {:?}, enabled: {:?}",
                feature, enabled
            ));
            match feature {
                SecurityFeature::NAT => security_features.nat = Some(enabled),
                // TODO(cgibson): Support more security features.
            }
            let response = router_admin
                .set_security_features(security_features)
                .await
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }

        Set::PortForward { rule } => {
            printer.println(format!("Not Implemented {:?}", rule));
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
            let response = router_admin
                .set_filter(&mut filter_rule)
                .await
                .context("error getting response")?;
            printer.println(format!("Response: {:?}", response));
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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

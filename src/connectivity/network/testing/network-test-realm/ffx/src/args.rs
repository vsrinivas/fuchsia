// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/76549): Replace with GN config once available in an ffx_plugin.
#![deny(unused_results)]

use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_test_realm as fntr;

#[ffx_core::ffx_command]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "net-test-realm",
    description = "Manage a running Network Test Realm",
    note = "This plugin acts as a thin wrapper around the Network Test Realm Controller protocol.
For more specific information regarding each subcommand, see the underlying protocol definition:
https://osscs.corp.google.com/fuchsia/fuchsia/+/main:src/connectivity/network/testing/network-test-realm/fidl/controller.fidl"
)]
pub struct Command {
    #[argh(positional)]
    /// the moniker of the component that corresponds to the Network Test Realm
    /// instance (e.g.
    /// "core/network/test-components\:net_test_realm_controller").
    pub component_moniker: String,

    #[argh(subcommand)]
    pub subcommand: Subcommand,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Subcommand {
    AddInterface(AddInterface),
    JoinMulticastGroup(JoinMulticastGroup),
    LeaveMulticastGroup(LeaveMulticastGroup),
    Ping(Ping),
    PollUdp(PollUdp),
    StartHermeticNetworkRealm(StartHermeticNetworkRealm),
    StartStub(StartStub),
    StopHermeticNetworkRealm(StopHermeticNetworkRealm),
    StopStub(StopStub),
    Dhcpv6Client(Dhcpv6Client),
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "start-hermetic-network-realm")]
/// Starts a hermetic network realm.
pub struct StartHermeticNetworkRealm {
    #[argh(positional, from_str_fn(parse_netstack_type))]
    /// the Netstack version to start.
    pub netstack: fntr::Netstack,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "stop-hermetic-network-realm")]
/// Stops a running hermetic network realm.
pub struct StopHermeticNetworkRealm {}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "start-stub")]
/// Starts a test stub within the hermetic network realm.
pub struct StartStub {
    #[argh(positional)]
    /// the url of the component to start.
    pub component_url: String,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "stop-stub")]
/// Stops the currently running test stub.
pub struct StopStub {}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "add-interface")]
/// Attaches an interface to the hermetic Netstack.
pub struct AddInterface {
    #[argh(positional)]
    /// address of the interface to be added to the hermetic Netstack.
    /// This should correspond to the address of an existing system interface.
    pub mac_address: fnet_ext::MacAddress,

    #[argh(positional)]
    /// the name to assign to the new interface.
    pub name: String,

    #[argh(switch)]
    /// whether to wait for any IP address to be assigned to the interface before returning. This is
    /// helpful for tests that want to ensure the autoconfigured IP address is assigned and has
    /// completed duplicate address detection before proceeding.
    pub wait_any_ip_address: bool,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "join-multicast-group")]
/// Joins a multicast group.
pub struct JoinMulticastGroup {
    #[argh(positional)]
    /// the group address to join.
    pub address: fnet_ext::IpAddress,

    #[argh(positional)]
    /// the ID of the interface that should be used to join the group.
    pub interface_id: u64,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "leave-multicast-group")]
/// Leaves a multicast group that was previously joined.
pub struct LeaveMulticastGroup {
    #[argh(positional)]
    /// the group address to leave.
    pub address: fnet_ext::IpAddress,

    #[argh(positional)]
    /// the ID of the interface that should be used to leave the group.
    pub interface_id: u64,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "ping")]
/// Sends an ICMP echo request to a target using a socket provided by the
/// hermetic Netstack.
pub struct Ping {
    #[argh(positional)]
    /// the address to ping.
    pub target: fnet_ext::IpAddress,

    #[argh(positional)]
    /// the body size of the ICMP packet. Specifically, the packet body will be
    /// filled with zeros of the provided length.
    pub payload_length: u16,

    #[argh(positional)]
    /// a timeout in nanoseconds to wait for a reply. If less than or equal to
    /// 0, then returns success immediately after the ping is sent.
    pub timeout: i64,

    #[argh(option)]
    /// the name of the source interface.
    pub interface_name: Option<String>,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "poll-udp")]
/// Polls the specified socket address with UDP datagrams containing the specified payload.
/// Waits for a single reply from the target address and prints it to stdout.
pub struct PollUdp {
    #[argh(positional)]
    /// the socket to which to send datagrams
    pub target: std::net::SocketAddr,

    #[argh(positional)]
    /// the datagram to send
    pub payload: String,

    #[argh(positional)]
    /// the timeout in nanos to wait for a reply, per retry.
    pub timeout: i64,

    #[argh(positional)]
    /// the number of attempts to make
    pub num_retries: u16,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "dhcpv6-client")]
/// DHCPv6 client commands.
pub struct Dhcpv6Client {
    #[argh(subcommand)]
    pub subcommand: Dhcpv6ClientSubcommand,
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Dhcpv6ClientSubcommand {
    Start(Dhcpv6ClientStart),
    Stop(Dhcpv6ClientStop),
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "start")]
/// Start a DHCPv6 client.
pub struct Dhcpv6ClientStart {
    #[argh(positional)]
    /// the interface to run the DHCPv6 client on
    pub interface_id: u64,

    #[argh(positional)]
    /// the link-local address the DHCPv6 client uses to communicate with servers
    pub address: fnet_ext::Ipv6Address,

    #[argh(option)]
    /// whether the DHCPv6 client should run in stateful or stateless mode
    pub stateful: bool,

    #[argh(switch)]
    /// request DNS servers configuration from servers
    pub request_dns_servers: bool,
    // TODO(https://fxbug.dev/48867): Add configuration for Rapid Commit.
    // TODO(https://fxbug.dev/105427): Add configuration for acquiring temporary addresses.
}

#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "stop")]
/// Stops all DHCPv6 clients.
pub struct Dhcpv6ClientStop {}

fn parse_netstack_type(value: &str) -> Result<fntr::Netstack, String> {
    match &value.to_lowercase()[..] {
        "v2" => Ok(fntr::Netstack::V2),
        _ => Err("invalid netstack type".to_string()),
    }
}

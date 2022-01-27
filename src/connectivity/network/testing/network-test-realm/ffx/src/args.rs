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
    StartHermeticNetworkRealm(StartHermeticNetworkRealm),
    StartStub(StartStub),
    StopHermeticNetworkRealm(StopHermeticNetworkRealm),
    StopStub(StopStub),
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

fn parse_netstack_type(value: &str) -> Result<fntr::Netstack, String> {
    match &value.to_lowercase()[..] {
        "v2" => Ok(fntr::Netstack::V2),
        _ => Err("invalid netstack type".to_string()),
    }
}

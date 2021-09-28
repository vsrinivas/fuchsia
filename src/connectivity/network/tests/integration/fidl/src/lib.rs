// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Context as _;
use fidl_fuchsia_net_ext::{IntoExt as _, NetTypesIpAddressExt};
use fidl_fuchsia_net_stack as net_stack;
use fidl_fuchsia_net_stack_ext::{exec_fidl, FidlReturn as _};
use fidl_fuchsia_netemul as fnetemul;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryStreamExt as _};
use net_declare::{fidl_ip, fidl_mac, fidl_subnet, std_ip_v4, std_ip_v6, std_socket_addr};
use netemul::RealmUdpSocket as _;
use netstack_testing_common::realms::{
    constants, KnownServiceProvider, Netstack, Netstack2, TestSandboxExt as _,
};
use netstack_testing_common::{
    get_component_moniker, wait_for_interface_up_and_address, ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT,
    ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;
use packet::serialize::Serializer as _;
use packet::ParsablePacket as _;
use packet_formats::error::ParseError;
use packet_formats::ethernet::{
    EthernetFrame, EthernetFrameBuilder, EthernetFrameLengthCheck, EthernetIpExt as _,
};
use packet_formats::icmp::{
    IcmpEchoRequest, IcmpIpExt, IcmpMessage, IcmpPacket, IcmpPacketBuilder, IcmpParseArgs,
    IcmpUnusedCode, MessageBody as _, OriginalPacket,
};
use packet_formats::ip::IpPacketBuilder as _;
use std::collections::HashMap;
use std::convert::TryInto as _;
use test_case::test_case;

/// Regression test: test that Netstack.SetInterfaceStatus does not kill the channel to the client
/// if given an invalid interface id.
#[fuchsia_async::run_singlethreaded(test)]
async fn set_interface_status_unknown_interface() {
    let name = "set_interface_status";
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, netstack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_netstack::NetstackMarker, _>(name)
        .expect("create realm");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create event stream"),
        HashMap::new(),
    )
    .await
    .expect("get existing interfaces");

    let next_id = 1 + interfaces
        .keys()
        .max()
        .expect("can't find any network interfaces (at least loopback should be present)");
    let next_id =
        next_id.try_into().unwrap_or_else(|e| panic!("{} try_into error: {:?}", next_id, e));

    let () = netstack.set_interface_status(next_id, false).expect("set_interface_status");
    let _routes = netstack.get_route_table().await.expect(
        "invoke netstack method after calling set_interface_status with an invalid argument",
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_device() {
    let name = "add_ethernet_device";
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, netstack, device) = sandbox
        .new_netstack_and_device::<Netstack2, netemul::Ethernet, fidl_fuchsia_netstack::NetstackMarker, _>(
            name,
        )
        .await.expect("create realm");

    // We're testing add_ethernet_device (netstack.fidl), which
    // does not have a network device entry point.
    let eth = device.get_ethernet().await.expect("connet to ethernet device");
    let id = netstack
        .add_ethernet_device(
            name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: name[..fidl_fuchsia_net_interfaces::INTERFACE_NAME_LENGTH.into()].to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
            },
            eth,
        )
        .await
        .expect("add_ethernet_device FIDL error")
        .map_err(fuchsia_zircon::Status::from_raw)
        .expect("add_ethernet_device failed");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let mut state = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id.into());
    let (device_class, online) = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create event stream"),
        &mut state,
        |fidl_fuchsia_net_interfaces_ext::Properties {
             id: _,
             name: _,
             device_class,
             online,
             addresses: _,
             has_default_ipv4_route: _,
             has_default_ipv6_route: _,
         }| Some((*device_class, *online)),
    )
    .await
    .expect("observe interface addition");

    assert_eq!(
        device_class,
        fidl_fuchsia_net_interfaces::DeviceClass::Device(
            fidl_fuchsia_hardware_network::DeviceClass::Ethernet
        )
    );
    assert!(!online);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_no_duplicate_interface_names() {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(
            "no_duplicate_interface_names",
        )
        .expect("create realm");
    let netstack = realm
        .connect_to_protocol::<fidl_fuchsia_netstack::NetstackMarker>()
        .expect("connect to protocol");
    // Create one endpoint of each type so we can use all the APIs that add an
    // interface. Note that fuchsia.net.stack/Stack.AddEthernetInterface does
    // not support setting the interface name.
    let eth_ep = sandbox
        .create_endpoint::<netemul::Ethernet, _>("eth-ep")
        .await
        .expect("create ethernet endpoint");
    let netdev_ep = sandbox
        .create_endpoint::<netemul::NetworkDevice, _>("netdev-ep")
        .await
        .expect("create netdevice endpoint");

    const IFNAME: &'static str = "testif";
    const TOPOPATH: &'static str = "/fake/topopath";
    const FILEPATH: &'static str = "/fake/filepath";

    // Add the first ep to the stack so it takes over the name.
    let eth = eth_ep.get_ethernet().await.expect("connect to ethernet device");
    let _id: u32 = netstack
        .add_ethernet_device(
            TOPOPATH,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: IFNAME.to_string(),
                filepath: FILEPATH.to_string(),
                metric: 0,
            },
            eth,
        )
        .await
        .expect("add_ethernet_device FIDL error")
        .map_err(fuchsia_zircon::Status::from_raw)
        .expect("add_ethernet_device error");

    // Now try to add again with the same parameters and expect an error.
    let eth = eth_ep.get_ethernet().await.expect("connect to ethernet device");
    let result = netstack
        .add_ethernet_device(
            TOPOPATH,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: IFNAME.to_string(),
                filepath: FILEPATH.to_string(),
                metric: 0,
            },
            eth,
        )
        .await
        .expect("add_ethernet_device FIDL error")
        .map_err(fuchsia_zircon::Status::from_raw);
    assert_eq!(result, Err(fuchsia_zircon::Status::ALREADY_EXISTS));

    // Same for netdevice.
    let (network_device, mac) =
        netdev_ep.get_netdevice().await.expect("connect to netdevice protocols");
    let result = stack
        .add_interface(
            fidl_fuchsia_net_stack::InterfaceConfig {
                name: Some(IFNAME.to_string()),
                topopath: None,
                metric: None,
                ..fidl_fuchsia_net_stack::InterfaceConfig::EMPTY
            },
            &mut fidl_fuchsia_net_stack::DeviceDefinition::Ethernet(
                fidl_fuchsia_net_stack::EthernetDeviceDefinition { network_device, mac },
            ),
        )
        .await
        .expect("add_interface FIDL error");
    assert_eq!(result, Err(fidl_fuchsia_net_stack::Error::AlreadyExists));
}

// TODO(https://fxbug.dev/75553): Remove this test when fuchsia.net.interfaces is supported in N3
// and test_add_remove_interface can be parameterized on Netstack.
#[variants_test]
async fn add_ethernet_interface<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, stack, device) = sandbox
        .new_netstack_and_device::<N, netemul::Ethernet, fidl_fuchsia_net_stack::StackMarker, _>(
            name,
        )
        .await
        .expect("create realm");

    let id = device.add_to_stack(&realm).await.expect("add device");

    let interface = stack
        .list_interfaces()
        .await
        .expect("list interfaces")
        .into_iter()
        .find(|interface| interface.id == id)
        .expect("find added ethernet interface");
    assert!(
        !interface.properties.features.contains(fidl_fuchsia_hardware_ethernet::Features::Loopback),
        "unexpected interface features: ({:b}).contains({:b})",
        interface.properties.features,
        fidl_fuchsia_hardware_ethernet::Features::Loopback
    );
    assert_eq!(interface.properties.physical_status, fidl_fuchsia_net_stack::PhysicalStatus::Down);
}

#[variants_test]
async fn add_del_interface_address<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, stack, device) = sandbox
        .new_netstack_and_device::<N, netemul::Ethernet, fidl_fuchsia_net_stack::StackMarker, _>(
            name,
        )
        .await
        .expect("create realm");

    let id = device.add_to_stack(&realm).await.expect("add device");

    // Netstack3 doesn't allow addresses to be added while link is down.
    let () = stack.enable_interface(id).await.squash_result().expect("enable interface");
    let () = device.set_link_up(true).await.expect("bring device up");
    loop {
        // TODO(https://fxbug.dev/75553): Remove usage of get_interface_info.
        let info = exec_fidl!(stack.get_interface_info(id), "get interface").unwrap();
        if info.properties.physical_status == net_stack::PhysicalStatus::Up {
            break;
        }
    }

    let mut interface_address = fidl_subnet!("1.1.1.1/32");
    let res = stack
        .add_interface_address(id, &mut interface_address)
        .await
        .expect("add_interface_address");
    assert_eq!(res, Ok(()));

    // Should be an error the second time.
    let res = stack
        .add_interface_address(id, &mut interface_address)
        .await
        .expect("add_interface_address");
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::AlreadyExists));

    let res = stack
        .add_interface_address(id + 1, &mut interface_address)
        .await
        .expect("add_interface_address");
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::NotFound));

    let error = stack
        .add_interface_address(
            id,
            &mut fidl_fuchsia_net::Subnet { prefix_len: 43, ..interface_address },
        )
        .await
        .expect("add_interface_address")
        .unwrap_err();
    assert_eq!(error, fidl_fuchsia_net_stack::Error::InvalidArgs);

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let interface = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create event stream"),
        fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id),
    )
    .await
    .expect("retrieve existing interface");
    // We use contains here because netstack can generate link-local addresses
    // that can't be predicted.
    matches::assert_matches!(
        interface,
        fidl_fuchsia_net_interfaces_ext::InterfaceState::Known(p)
            if p.addresses.contains(&fidl_fuchsia_net_interfaces_ext::Address {
                addr: interface_address,
                valid_until: zx::sys::ZX_TIME_INFINITE,
            })
    );

    let res = stack
        .del_interface_address(id, &mut interface_address)
        .await
        .expect("del_interface_address");
    assert_eq!(res, Ok(()));

    let interface = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create watcher event stream"),
        fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id),
    )
    .await
    .expect("retrieve existing interface");
    // We use contains here because netstack can generate link-local addresses
    // that can't be predicted.
    matches::assert_matches!(
        interface,
        fidl_fuchsia_net_interfaces_ext::InterfaceState::Known(p)
            if !p.addresses.contains(&fidl_fuchsia_net_interfaces_ext::Address {
                addr: interface_address,
                valid_until: zx::sys::ZX_TIME_INFINITE,
            })
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn set_remove_interface_address_errors() {
    let name = "set_remove_interface_address_errors";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, netstack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_netstack::NetstackMarker, _>(name)
        .expect("create realm");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create event stream"),
        HashMap::new(),
    )
    .await
    .expect("get existing interfaces");
    let next_id = 1 + interfaces
        .keys()
        .max()
        .expect("can't find any network interfaces (at least loopback should be present)");
    let next_id =
        next_id.try_into().unwrap_or_else(|e| panic!("{} try_into error: {:?}", next_id, e));

    let mut addr = fidl_ip!("0.0.0.0");

    let prefix_len = 0;

    let error = netstack
        .set_interface_address(next_id, &mut addr, prefix_len)
        .await
        .expect("set_interface_address");
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::UnknownInterface,
            message: "".to_string(),
        },
    );

    let error = netstack
        .remove_interface_address(next_id, &mut addr, prefix_len)
        .await
        .expect("remove_interface_address");
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::UnknownInterface,
            message: "".to_string(),
        },
    );

    let prefix_len = 43;

    let error = netstack
        .set_interface_address(next_id, &mut addr, prefix_len)
        .await
        .expect("set_interface_address");
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::ParseError,
            message: "prefix length exceeds address length".to_string(),
        },
    );

    let error = netstack
        .remove_interface_address(next_id, &mut addr, prefix_len)
        .await
        .expect("remove_interface_address");
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::ParseError,
            message: "prefix length exceeds address length".to_string(),
        },
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_log_packets() {
    let name = "test_log_packets";
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    // Modify debug netstack args so that it does not log packets.
    let (realm, stack_log) = {
        let mut netstack =
            fnetemul::ChildDef::from(&KnownServiceProvider::Netstack(Netstack2::VERSION));
        let fnetemul::ChildDef { program_args, .. } = &mut netstack;
        assert_eq!(
            std::mem::replace(program_args, Some(vec!["--verbosity=debug".to_string()])),
            None,
        );
        let realm = sandbox.create_realm(name, [netstack]).expect("create realm");

        let netstack_proxy =
            realm.connect_to_protocol::<net_stack::LogMarker>().expect("connect to netstack");
        (realm, netstack_proxy)
    };
    let () = stack_log.set_log_packets(true).await.expect("enable packet logging");

    let sock =
        fuchsia_async::net::UdpSocket::bind_in_realm(&realm, std_socket_addr!("127.0.0.1:0"))
            .await
            .expect("create socket");
    let addr = sock.local_addr().expect("get bound socket address");
    const PAYLOAD: [u8; 4] = [1u8, 2, 3, 4];
    let sent = sock.send_to(&PAYLOAD[..], addr).await.expect("send_to failed");
    assert_eq!(sent, PAYLOAD.len());

    let patterns = ["send", "recv"]
        .iter()
        .map(|t| format!("{} udp {} -> {} len:{}", t, addr, addr, PAYLOAD.len()))
        .collect::<Vec<_>>();

    let netstack_moniker = get_component_moniker(&realm, constants::netstack::COMPONENT_NAME)
        .await
        .expect("get netstack moniker");
    let stream = diagnostics_reader::ArchiveReader::new()
        .select_all_for_moniker(&netstack_moniker)
        .snapshot_then_subscribe()
        .expect("subscribe to snapshot");

    let () = async_utils::fold::try_fold_while(stream, patterns, |mut patterns, data| {
        let () = patterns
            .retain(|pattern| !data.msg().map(|msg| msg.contains(pattern)).unwrap_or(false));
        futures::future::ok(if patterns.is_empty() {
            async_utils::fold::FoldWhile::Done(())
        } else {
            async_utils::fold::FoldWhile::Continue(patterns)
        })
    })
    .await
    .expect("observe expected patterns")
    .short_circuited()
    .unwrap_or_else(|patterns| {
        panic!("log stream ended while still waiting for patterns {:?}", patterns)
    });
}

// TODO(https://fxbug.dev/75554): Remove when {list_interfaces,get_interface_info} are removed.
#[variants_test]
async fn get_interface_info_not_found<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (_realm, stack) = sandbox
        .new_netstack::<N, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .expect("create realm");

    let interfaces = stack.list_interfaces().await.expect("list interfaces");
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let res = stack.get_interface_info(max_id + 1).await.expect("get_interface_info");
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::NotFound));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_interface_loopback() {
    let name = "disable_interface_loopback";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .expect("create realm");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    let stream = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
        .expect("get interface event stream");
    pin_utils::pin_mut!(stream);

    let loopback_id = match stream.try_next().await {
        Ok(Some(fidl_fuchsia_net_interfaces::Event::Existing(
            fidl_fuchsia_net_interfaces::Properties {
                id: Some(id),
                device_class:
                    Some(fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                        fidl_fuchsia_net_interfaces::Empty {},
                    )),
                online: Some(true),
                ..
            },
        ))) => id,
        event => panic!("got {:?}, want loopback interface existing event", event),
    };

    let () = match stream.try_next().await {
        Ok(Some(fidl_fuchsia_net_interfaces::Event::Idle(
            fidl_fuchsia_net_interfaces::Empty {},
        ))) => (),
        event => panic!("got {:?}, want idle event", event),
    };

    let () = exec_fidl!(stack.disable_interface(loopback_id), "disable interface").unwrap();

    let () = match stream.try_next().await {
        Ok(Some(fidl_fuchsia_net_interfaces::Event::Changed(
            fidl_fuchsia_net_interfaces::Properties { id: Some(id), online: Some(false), .. },
        ))) if id == loopback_id => (),
        event => panic!("got {:?}, want loopback interface offline event", event),
    };
}

enum ForwardingConfiguration {
    All,
    Iface1Only(fidl_fuchsia_net::IpVersion),
    Iface2Only(fidl_fuchsia_net::IpVersion),
}

struct ForwardingTestCase<I: IcmpIpExt> {
    iface1_addr: fidl_fuchsia_net::Subnet,
    iface2_addr: fidl_fuchsia_net::Subnet,
    forwarding_config: Option<ForwardingConfiguration>,
    src_ip: I::Addr,
    dst_ip: I::Addr,
    expect_forward: bool,
}

fn test_forwarding_v4(
    forwarding_config: Option<ForwardingConfiguration>,
    expect_forward: bool,
) -> ForwardingTestCase<net_types::ip::Ipv4> {
    ForwardingTestCase {
        iface1_addr: fidl_subnet!("192.168.1.1/24"),
        iface2_addr: fidl_subnet!("192.168.2.1/24"),
        forwarding_config,
        // TODO(https://fxbug.dev/77901): Use `std_ip_v4!(..).into()`.
        // TODO(https://fxbug.dev/77965): Use `net_declare` macros to create
        // `net_types` addresses.
        src_ip: net_types::ip::Ipv4Addr::new(std_ip_v4!("192.168.1.2").octets()),
        dst_ip: net_types::ip::Ipv4Addr::new(std_ip_v4!("192.168.2.2").octets()),
        expect_forward,
    }
}

fn test_forwarding_v6(
    forwarding_config: Option<ForwardingConfiguration>,
    expect_forward: bool,
) -> ForwardingTestCase<net_types::ip::Ipv6> {
    ForwardingTestCase {
        iface1_addr: fidl_subnet!("a::1/64"),
        iface2_addr: fidl_subnet!("b::1/64"),
        forwarding_config,
        // TODO(https://fxbug.dev/77901): Use `std_ip_v6!(..).into()`.
        // TODO(https://fxbug.dev/77965): Use `net_declare` macros to create
        // `net_types` addresses.
        src_ip: net_types::ip::Ipv6Addr::from_bytes(std_ip_v6!("a::2").octets()),
        dst_ip: net_types::ip::Ipv6Addr::from_bytes(std_ip_v6!("b::2").octets()),
        expect_forward,
    }
}

#[variants_test]
#[test_case(
    "v4_none_forward_icmp_v4",
    test_forwarding_v4(
        None,
        false,
    ); "v4_none_forward_icmp_v4")]
#[test_case(
    "v4_all_forward_icmp_v4",
    test_forwarding_v4(
        Some(ForwardingConfiguration::All),
        true,
    ); "v4_all_forward_icmp_v4")]
#[test_case(
    "v4_iface1_forward_v4_icmp_v4",
    test_forwarding_v4(
        Some(ForwardingConfiguration::Iface1Only(fidl_fuchsia_net::IpVersion::V4)),
        true,
    ); "v4_iface1_forward_v4_icmp_v4")]
#[test_case(
    "v4_iface1_forward_v6_icmp_v4",
    test_forwarding_v4(
        Some(ForwardingConfiguration::Iface1Only(fidl_fuchsia_net::IpVersion::V6)),
        false,
    ); "v4_iface1_forward_v6_icmp_v4")]
#[test_case(
    "v4_iface2_forward_v4_icmp_v4",
    test_forwarding_v4(
        Some(ForwardingConfiguration::Iface2Only(fidl_fuchsia_net::IpVersion::V4)),
        false,
    ); "v4_iface2_forward_v4_icmp_v4")]
#[test_case(
    "v6_none_forward_icmp_v6",
    test_forwarding_v6(
        None,
        false,
    ); "v6_none_forward_icmp_v6")]
#[test_case(
    "v6_all_forward_icmp_v6",
    test_forwarding_v6(
        Some(ForwardingConfiguration::All),
        true,
    ); "v6_all_forward_icmp_v6")]
#[test_case(
    "v6_iface1_forward_v6_icmp_v6",
    test_forwarding_v6(
        Some(ForwardingConfiguration::Iface1Only(fidl_fuchsia_net::IpVersion::V6)),
        true,
    ); "v6_iface1_forward_v6_icmp_v6")]
#[test_case(
    "v6_iface1_forward_v4_icmp_v6",
    test_forwarding_v6(
        Some(ForwardingConfiguration::Iface1Only(fidl_fuchsia_net::IpVersion::V4)),
        false,
    ); "v6_iface1_forward_v4_icmp_v6")]
#[test_case(
    "v6_iface2_forward_v6_icmp_v6",
    test_forwarding_v6(
        Some(ForwardingConfiguration::Iface2Only(fidl_fuchsia_net::IpVersion::V6)),
        false,
    ); "v6_iface2_forward_v6_icmp_v6")]
async fn test_forwarding<E: netemul::Endpoint, I: IcmpIpExt>(
    test_name: &str,
    sub_test_name: &str,
    test_case: ForwardingTestCase<I>,
) where
    IcmpEchoRequest:
        for<'a> IcmpMessage<I, &'a [u8], Code = IcmpUnusedCode, Body = OriginalPacket<&'a [u8]>>,
    I::Addr: NetTypesIpAddressExt,
{
    const TTL: u8 = 64;
    const ECHO_ID: u16 = 1;
    const ECHO_SEQ: u16 = 2;
    const MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:0A:0B:0C:0D:0E");

    let ForwardingTestCase {
        iface1_addr,
        iface2_addr,
        forwarding_config,
        src_ip,
        dst_ip,
        expect_forward,
    } = test_case;

    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let sandbox = &sandbox;
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create netstack realm");
    let realm = &realm;

    let net_ep_iface = |net_num: u8, addr: fidl_fuchsia_net::Subnet| async move {
        let net = sandbox.create_network(format!("net{}", net_num)).await.expect("create network");
        let fake_ep = net.create_fake_endpoint().expect("create fake endpoint");
        let iface = realm
            .join_network::<E, _>(
                &net,
                format!("iface{}", net_num),
                &netemul::InterfaceConfig::StaticIp(addr),
            )
            .await
            .expect("configure networking");

        (net, fake_ep, iface)
    };

    let (_net1, fake_ep1, iface1) = net_ep_iface(1, iface1_addr).await;
    let (_net2, fake_ep2, iface2) = net_ep_iface(2, iface2_addr).await;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    let ((), ()) = futures::future::join(
        wait_for_interface_up_and_address(&interface_state, iface1.id(), &iface1_addr),
        wait_for_interface_up_and_address(&interface_state, iface2.id(), &iface2_addr),
    )
    .await;

    if let Some(config) = forwarding_config {
        let stack = realm
            .connect_to_protocol::<net_stack::StackMarker>()
            .expect("error connecting to stack");

        match config {
            ForwardingConfiguration::All => {
                let () = stack
                    .enable_ip_forwarding()
                    .await
                    .expect("error enabling IP forwarding request");
            }
            ForwardingConfiguration::Iface1Only(ip_version) => {
                let () = stack
                    .set_interface_ip_forwarding(iface1.id(), ip_version, true)
                    .await
                    .expect("set_interface_ip_forwarding FIDL error for iface1")
                    .expect("error enabling IP forwarding on iface1");
            }
            ForwardingConfiguration::Iface2Only(ip_version) => {
                let () = stack
                    .set_interface_ip_forwarding(iface2.id(), ip_version, true)
                    .await
                    .expect("set_interface_ip_forwarding FIDL error for iface2")
                    .expect("error enabling IP forwarding on iface2");
            }
        }
    }

    let neighbor_controller = realm
        .connect_to_protocol::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .expect("connect to protocol");
    let dst_ip_fidl: <I::Addr as NetTypesIpAddressExt>::Fidl = dst_ip.into_ext();
    let () = neighbor_controller
        .add_entry(iface2.id(), &mut dst_ip_fidl.into_ext(), &mut MAC.clone())
        .await
        .expect("add_entry FIDL error")
        .expect("error adding static entry");

    let mut icmp_body = [1, 2, 3, 4, 5, 6, 7, 8];

    let ser = packet::Buf::new(&mut icmp_body, ..)
        .encapsulate(IcmpPacketBuilder::<I, _, _>::new(
            src_ip,
            dst_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(ECHO_ID, ECHO_SEQ),
        ))
        .encapsulate(<I as packet_formats::ip::IpExt>::PacketBuilder::new(
            src_ip,
            dst_ip,
            TTL,
            I::ICMP_IP_PROTO,
        ))
        .encapsulate(EthernetFrameBuilder::new(
            net_types::ethernet::Mac::new([1, 2, 3, 4, 5, 6]),
            net_types::ethernet::Mac::BROADCAST,
            I::ETHER_TYPE,
        ))
        .serialize_vec_outer()
        .expect("serialize ICMP packet")
        .unwrap_b();

    let duration = if expect_forward {
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
    } else {
        ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT
    };

    let ((), forwarded) = futures::future::join(
        fake_ep1.write(ser.as_ref()).map(|r| r.expect("write to fake endpoint #1")),
        fake_ep2
            .frame_stream()
            .map(|r| r.expect("error getting OnData event"))
            .filter_map(|(data, dropped)| {
                assert_eq!(dropped, 0);

                let mut data = &data[..];

                let eth = EthernetFrame::parse(&mut data, EthernetFrameLengthCheck::NoCheck)
                    .expect("error parsing ethernet frame");

                if eth.ethertype() != Some(I::ETHER_TYPE) {
                    // Ignore other IP packets.
                    return futures::future::ready(None);
                }

                let (mut payload, src_ip, dst_ip, proto, got_ttl) =
                    packet_formats::testutil::parse_ip_packet::<I>(&data)
                        .expect("error parsing IP packet");

                if proto != I::ICMP_IP_PROTO {
                    // Ignore non-ICMP packets.
                    return futures::future::ready(None);
                }

                let icmp = match IcmpPacket::<I, _, IcmpEchoRequest>::parse(
                    &mut payload,
                    IcmpParseArgs::new(src_ip, dst_ip),
                ) {
                    Ok(o) => o,
                    Err(ParseError::NotExpected) => {
                        // Ignore non-echo request packets.
                        return futures::future::ready(None);
                    }
                    Err(e) => {
                        panic!("error parsing ICMP echo request packet: {}", e)
                    }
                };

                let echo_request = icmp.message();
                assert_eq!(echo_request.id(), ECHO_ID);
                assert_eq!(echo_request.seq(), ECHO_SEQ);
                assert_eq!(icmp.body().bytes(), icmp_body);
                assert_eq!(got_ttl, TTL - 1);

                // Our packet was forwarded.
                futures::future::ready(Some(true))
            })
            .next()
            .map(|r| r.expect("stream unexpectedly ended"))
            .on_timeout(duration.after_now(), || {
                // The packet was not forwarded.
                false
            }),
    )
    .await;

    assert_eq!(expect_forward, forwarded);
}

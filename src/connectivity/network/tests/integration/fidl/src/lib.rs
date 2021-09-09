// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::collections::{HashMap, HashSet};
use std::convert::TryInto as _;

use fidl_fuchsia_net_ext::{IntoExt as _, NetTypesIpAddressExt};
use fidl_fuchsia_net_stack as net_stack;
use fidl_fuchsia_net_stack_ext::{exec_fidl, FidlReturn as _};
use fidl_fuchsia_netemul as fnetemul;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::{
    fidl_ip, fidl_ip_v4, fidl_ip_v4_with_prefix, fidl_ip_v6, fidl_mac, fidl_subnet, std_ip,
    std_ip_v4, std_ip_v6, std_socket_addr,
};
use net_types::ip::IpAddress as _;
use netemul::RealmUdpSocket as _;
use netstack_testing_common::realms::{
    constants, KnownServiceProvider, Netstack, Netstack2, TestSandboxExt as _,
};
use netstack_testing_common::{
    get_component_moniker, wait_for_interface_up_and_address, Result,
    ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
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
use test_case::test_case;

/// Regression test: test that Netstack.SetInterfaceStatus does not kill the channel to the client
/// if given an invalid interface id.
#[fuchsia_async::run_singlethreaded(test)]
async fn set_interface_status_unknown_interface() -> Result {
    let name = "set_interface_status";
    let sandbox = netemul::TestSandbox::new()?;
    let (realm, netstack) =
        sandbox.new_netstack::<Netstack2, fidl_fuchsia_netstack::NetstackMarker, _>(name)?;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)?,
        HashMap::new(),
    )
    .await
    .context("failed to get existing interfaces")?;

    let next_id = 1 + interfaces.keys().max().ok_or(anyhow::format_err!(
        "failed to find any network interfaces (at least loopback should be present)"
    ))?;
    let next_id = next_id.try_into().with_context(|| format!("{} overflows id", next_id))?;

    let () = netstack
        .set_interface_status(next_id, false)
        .context("failed to call set_interface_status")?;
    let _routes = netstack
        .get_route_table()
        .await
        .context("failed to invoke netstack method after calling set_interface_status with an invalid argument")?;

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_device() -> Result {
    let name = "add_ethernet_device";
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, netstack, device) = sandbox
        .new_netstack_and_device::<Netstack2, netemul::Ethernet, fidl_fuchsia_netstack::NetstackMarker, _>(
            name,
        )
        .await?;

    let id = netstack
        .add_ethernet_device(
            name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: name[..fidl_fuchsia_net_interfaces::INTERFACE_NAME_LENGTH.into()].to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
            },
            // We're testing add_ethernet_device (netstack.fidl), which
            // does not have a network device entry point.
            device
                .get_ethernet()
                .await
                .context("add_ethernet_device requires an Ethernet endpoint")?,
        )
        .await
        .context("add_ethernet_device FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_ethernet_device failed")?;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let mut state = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id.into());
    let (device_class, online) = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)?,
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
    .context("failed to observe interface addition")?;

    assert_eq!(
        device_class,
        fidl_fuchsia_net_interfaces::DeviceClass::Device(
            fidl_fuchsia_hardware_network::DeviceClass::Ethernet
        )
    );
    assert!(!online);

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_no_duplicate_interface_names() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(
            "no_duplicate_interface_names",
        )
        .context("failed to create realm")?;
    let netstack = realm
        .connect_to_protocol::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;
    // Create one endpoint of each type so we can use all the APIs that add an
    // interface. Note that fuchsia.net.stack/Stack.AddEthernetInterface does
    // not support setting the interface name.
    let eth_ep = sandbox
        .create_endpoint::<netemul::Ethernet, _>("eth-ep")
        .await
        .context("failed to create ethernet endpoint")?;
    let netdev_ep = sandbox
        .create_endpoint::<netemul::NetworkDevice, _>("netdev-ep")
        .await
        .context("failed to create netdevice endpoint")?;

    const IFNAME: &'static str = "testif";
    const TOPOPATH: &'static str = "/fake/topopath";
    const FILEPATH: &'static str = "/fake/filepath";

    // Add the first ep to the stack so it takes over the name.
    let _id: u32 = netstack
        .add_ethernet_device(
            TOPOPATH,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: IFNAME.to_string(),
                filepath: FILEPATH.to_string(),
                metric: 0,
            },
            eth_ep.get_ethernet().await.context("failed to connect to ethernet device")?,
        )
        .await
        .context("add_ethernet_device FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_ethernet_device error")?;

    // Now try to add again with the same parameters and expect an error.
    let result = netstack
        .add_ethernet_device(
            TOPOPATH,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: IFNAME.to_string(),
                filepath: FILEPATH.to_string(),
                metric: 0,
            },
            eth_ep.get_ethernet().await.context("failed to connect to ethernet device")?,
        )
        .await
        .context("add_ethernet_device FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw);
    assert_eq!(result, Err(fuchsia_zircon::Status::ALREADY_EXISTS));

    // Same for netdevice.
    let (network_device, mac) =
        netdev_ep.get_netdevice().await.context("failed to connect to netdevice protocols")?;
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
        .context("add_interface FIDL error")?;
    assert_eq!(result, Err(fidl_fuchsia_net_stack::Error::AlreadyExists));

    Ok(())
}

// TODO(https://fxbug.dev/75553): Remove this test when fuchsia.net.interfaces is supported in N3
// and test_add_remove_interface can be parameterized on Netstack.
#[variants_test]
async fn add_ethernet_interface<N: Netstack>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new()?;
    let (realm, stack, device) = sandbox
        .new_netstack_and_device::<N, netemul::Ethernet, fidl_fuchsia_net_stack::StackMarker, _>(
            name,
        )
        .await?;

    let id = device.add_to_stack(&realm).await.context("failed to add device")?;

    let interface = stack
        .list_interfaces()
        .await
        .context("failed to list interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet interface"))?;
    assert!(
        !interface.properties.features.contains(fidl_fuchsia_hardware_ethernet::Features::Loopback),
        "unexpected interface features: ({:b}).contains({:b})",
        interface.properties.features,
        fidl_fuchsia_hardware_ethernet::Features::Loopback
    );
    assert_eq!(interface.properties.physical_status, fidl_fuchsia_net_stack::PhysicalStatus::Down);
    Ok(())
}

// TODO(https://fxbug.dev/75553): switch to fuchsia.net.interfaces when supported in N3.
#[variants_test]
async fn add_del_interface_address<N: Netstack>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, stack, device) = sandbox
        .new_netstack_and_device::<N, netemul::Ethernet, fidl_fuchsia_net_stack::StackMarker, _>(
            name,
        )
        .await?;

    let id = device.add_to_stack(&realm).await.context("failed to add device")?;

    // Netstack3 doesn't allow addresses to be added while link is down.
    let () =
        stack.enable_interface(id).await.squash_result().context("failed to enable interface")?;
    let () = device.set_link_up(true).await.context("failed to bring device up")?;
    loop {
        let info = exec_fidl!(stack.get_interface_info(id), "failed to get interface")?;
        if info.properties.physical_status == net_stack::PhysicalStatus::Up {
            break;
        }
    }

    let mut interface_address = fidl_subnet!("1.1.1.1/32");
    let res = stack
        .add_interface_address(id, &mut interface_address)
        .await
        .context("failed to call add interface address")?;
    assert_eq!(res, Ok(()));

    // Should be an error the second time.
    let res = stack
        .add_interface_address(id, &mut interface_address)
        .await
        .context("failed to call add interface address")?;
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::AlreadyExists));

    let res = stack
        .add_interface_address(id + 1, &mut interface_address)
        .await
        .context("failed to call add interface address")?;
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::NotFound));

    let error = stack
        .add_interface_address(
            id,
            &mut fidl_fuchsia_net::Subnet { prefix_len: 43, ..interface_address },
        )
        .await
        .context("failed to call add interface address")?
        .unwrap_err();
    assert_eq!(error, fidl_fuchsia_net_stack::Error::InvalidArgs);

    let info = exec_fidl!(stack.get_interface_info(id), "failed to get interface")?;

    assert!(
        info.properties.addresses.contains(&interface_address),
        "couldn't find {:#?} in {:#?}",
        interface_address,
        info.properties.addresses
    );

    let res = stack
        .del_interface_address(id, &mut interface_address)
        .await
        .context("failed to call del interface address")?;
    assert_eq!(res, Ok(()));
    let info = exec_fidl!(stack.get_interface_info(id), "failed to get interface")?;

    assert!(
        !info.properties.addresses.contains(&interface_address),
        "did not expect to find {:#?} in {:#?}",
        interface_address,
        info.properties.addresses
    );

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn set_remove_interface_address_errors() -> Result {
    let name = "set_remove_interface_address_errors";

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, netstack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_netstack::NetstackMarker, _>(name)
        .context("failed to create realm")?;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)?,
        HashMap::new(),
    )
    .await
    .context("failed to get existing interfaces")?;
    let next_id = 1 + interfaces.keys().max().ok_or(anyhow::format_err!(
        "failed to find any network interfaces (at least loopback should be present)"
    ))?;
    let next_id = next_id.try_into().with_context(|| format!("{} overflows id", next_id))?;

    let mut addr = fidl_ip!("0.0.0.0");

    let prefix_len = 0;

    let error = netstack
        .set_interface_address(next_id, &mut addr, prefix_len)
        .await
        .context("failed to call set interface address")?;
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
        .context("failed to call remove interface address")?;
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
        .context("failed to call set interface address")?;
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
        .context("failed to call remove interface address")?;
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::ParseError,
            message: "prefix length exceeds address length".to_string(),
        },
    );

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_log_packets() {
    let name = "test_log_packets";
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    // Modify debug netstack args so that it does not log packets.
    let (realm, stack_log) = {
        let mut netstack =
            fnetemul::ChildDef::from(&KnownServiceProvider::Netstack(Netstack2::VERSION));
        let fnetemul::ChildDef { program_args, .. } = &mut netstack;
        assert_eq!(
            std::mem::replace(program_args, Some(vec!["--verbosity=debug".to_string()])),
            None,
        );
        let realm = sandbox.create_realm(name, [netstack]).expect("failed to create realm");

        let netstack_proxy = realm
            .connect_to_protocol::<net_stack::LogMarker>()
            .expect("failed to connect to netstack");
        (realm, netstack_proxy)
    };
    let () = stack_log.set_log_packets(true).await.expect("failed to enable packet logging");

    let sock =
        fuchsia_async::net::UdpSocket::bind_in_realm(&realm, std_socket_addr!("127.0.0.1:0"))
            .await
            .expect("failed to create socket");
    let addr = sock.local_addr().expect("failed to get bound socket address");
    const PAYLOAD: [u8; 4] = [1u8, 2, 3, 4];
    let sent = sock.send_to(&PAYLOAD[..], addr).await.expect("send_to failed");
    assert_eq!(sent, PAYLOAD.len());

    let patterns = ["send", "recv"]
        .iter()
        .map(|t| format!("{} udp {} -> {} len:{}", t, addr, addr, PAYLOAD.len()))
        .collect::<Vec<_>>();

    let netstack_moniker = get_component_moniker(&realm, constants::netstack::COMPONENT_NAME)
        .await
        .expect("failed to get netstack moniker");
    let stream = diagnostics_reader::ArchiveReader::new()
        .select_all_for_moniker(&netstack_moniker)
        .snapshot_then_subscribe()
        .expect("subscribed");

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
    .expect("failed to observe expected patterns")
    .short_circuited()
    .unwrap_or_else(|patterns| {
        panic!("log stream ended while still waiting for patterns {:?}", patterns)
    });
}

// TODO(https://fxbug.dev/75554): Remove when {list_interfaces,get_interface_info} are removed.
#[variants_test]
async fn get_interface_info_not_found<N: Netstack>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (_realm, stack) = sandbox
        .new_netstack::<N, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .context("failed to create realm")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let res =
        stack.get_interface_info(max_id + 1).await.context("failed to call get interface info")?;
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::NotFound));
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_interface_loopback() -> Result {
    let name = "disable_interface_loopback";

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .context("failed to create realm")?;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;

    let stream = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
        .context("failed to get interface event stream")?;
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

    let () = exec_fidl!(stack.disable_interface(loopback_id), "failed to disable interface")?;

    let () = match stream.try_next().await {
        Ok(Some(fidl_fuchsia_net_interfaces::Event::Changed(
            fidl_fuchsia_net_interfaces::Properties { id: Some(id), online: Some(false), .. },
        ))) if id == loopback_id => (),
        event => panic!("got {:?}, want loopback interface offline event", event),
    };

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn debug_interfaces_get_admin_unknown() {
    let name = "debug_interfaces_get_admin_unknown";

    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let (realm, _) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .expect("new netstack");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect(<fidl_fuchsia_net_interfaces::StateMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("failed to create fuchsia.net.interfaces/Watcher event stream"),
        HashMap::new(),
    )
    .await
    .expect("initial");
    assert_eq!(interfaces.len(), 1);
    let id = interfaces
        .keys()
        .next()
        .expect("interface properties map unexpectedly does not include loopback");

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    {
        // Request unknown NIC ID, expect request channel to be closed.
        let (control, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let () = debug_control.get_admin(*id + 1, server).expect("get admin");
        matches::assert_matches!(
            control.take_event_stream().try_collect::<Vec<_>>().await.as_ref().map(Vec::as_slice),
            // TODO(https://fxbug.dev/76695): Sending epitaphs not supported in Go.
            Ok([])
        );
    }
}

async fn add_address(
    control: &fidl_fuchsia_net_interfaces_admin::ControlProxy,
    mut address: fidl_fuchsia_net::InterfaceAddress,
    address_parameters: fidl_fuchsia_net_interfaces_admin::AddressParameters,
) -> std::result::Result<
    fidl_fuchsia_net_interfaces_admin::AddressStateProviderProxy,
    fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError,
> {
    let (address_state_provider, server) = fidl::endpoints::create_proxy::<
        fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
    >()
    .expect("create AddressStateProvider proxy");
    let () = control
        .add_address(&mut address, address_parameters, server)
        .expect("Control.AddAddress FIDL error");

    {
        let state_stream = fidl_fuchsia_net_interfaces_ext::admin::assignment_state_stream(
            address_state_provider.clone(),
        );
        futures::pin_mut!(state_stream);
        let () = fidl_fuchsia_net_interfaces_ext::admin::wait_assignment_state(
            &mut state_stream,
            fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Assigned,
        )
        .await?;
    }
    Ok(address_state_provider)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn interfaces_admin_add_address_errors() {
    let name = "interfaces_admin_add_address_errors";

    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let (realm, _) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .expect("new netstack");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect(<fidl_fuchsia_net_interfaces::StateMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("failed to create fuchsia.net.interfaces/Watcher event stream"),
        HashMap::new(),
    )
    .await
    .expect("initial");
    assert_eq!(interfaces.len(), 1);
    let (
        key,
        fidl_fuchsia_net_interfaces_ext::Properties {
            id,
            name: _,
            device_class: _,
            online: _,
            addresses,
            has_default_ipv4_route: _,
            has_default_ipv6_route: _,
        },
    ) = interfaces
        .iter()
        .next()
        .expect("interface properties map unexpectedly does not include loopback");
    assert_eq!(key, id);

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let (control, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create Control proxy");
    let () = debug_control.get_admin(*id, server).expect("get admin");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Removing non-existent address returns error.
    {
        let mut address =
            fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_ip_v4_with_prefix!("1.1.1.1/32"));
        let status = control
            .remove_address(&mut address)
            .await
            .expect("FIDL error calling fuchsia.net.interfaces.admin/Control.RemoveAddress")
            .expect_err("removing nonexistent address succeeded unexpectedly");
        assert_eq!(zx::Status::from_raw(status), zx::Status::NOT_FOUND);
    }

    let (v4_count, v6_count) = futures::stream::iter(addresses).fold((0, 0), |(v4, v6), &fidl_fuchsia_net_interfaces_ext::Address {
        addr: fidl_fuchsia_net::Subnet { addr, prefix_len },
        valid_until: _,
    }| {
        let (addr, v4, v6) = match addr {
            fidl_fuchsia_net::IpAddress::Ipv4(addr) => {
                assert!(net_types::ip::Ipv4Addr::new(addr.addr).is_loopback());
                (fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_fuchsia_net::Ipv4AddressWithPrefix {
                    addr,
                    prefix_len,
                }), v4+1, v6)
            }
            fidl_fuchsia_net::IpAddress::Ipv6(addr) => {
                assert!(net_types::ip::Ipv6Addr::from_bytes(addr.addr).is_loopback());
                (fidl_fuchsia_net::InterfaceAddress::Ipv6(addr), v4, v6+1)
            }
        };
        let control = &control;
        async move {
            matches::assert_matches!(
                add_address(control, addr, VALID_ADDRESS_PARAMETERS).await,
                Err(fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError::AddressRemoved(
                    fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::AlreadyAssigned
                ))
            );
            (v4, v6)
        }
    }).await;
    assert_eq!(v6_count, 1);
    assert_eq!(v4_count, 1);

    // Adding an invalid address returns error.
    {
        // NB: fidl_subnet! doesn't allow invalid prefix lengths.
        let invalid_address =
            fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_fuchsia_net::Ipv4AddressWithPrefix {
                addr: fidl_ip_v4!("1.1.1.1"),
                prefix_len: 33,
            });
        matches::assert_matches!(
            add_address(&control, invalid_address, VALID_ADDRESS_PARAMETERS).await,
            Err(fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError::AddressRemoved(
                fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::Invalid
            ))
        );
    }

    // TODO(https://fxbug.dev/80621): adding an address with non-empty properties
    // is currently unsupported. This testcase should be replaced with happy
    // path tests that actually test the effects of the parameters once
    // properly supported.
    {
        let parameters = fidl_fuchsia_net_interfaces_admin::AddressParameters {
            initial_properties: Some(fidl_fuchsia_net_interfaces_admin::AddressProperties {
                preferred_lifetime_info: Some(
                    fidl_fuchsia_net_interfaces_admin::PreferredLifetimeInfo::Deprecated(
                        fidl_fuchsia_net_interfaces_admin::Empty,
                    ),
                ),
                valid_lifetime_end: Some(zx::Time::into_nanos(zx::Time::INFINITE)),
                ..fidl_fuchsia_net_interfaces_admin::AddressProperties::EMPTY
            }),
            temporary: Some(true),
            ..fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY
        };
        matches::assert_matches!(
            add_address(
                &control,
                fidl_fuchsia_net::InterfaceAddress::Ipv6(fidl_ip_v6!("fe80::1")),
                parameters,
            )
            .await,
            Err(fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError::AddressRemoved(
                fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::Invalid
            ))
        );
    }

    // TODO(https://fxbug.dev/80621): updating an address' properties is
    // currently unsupported. This testcase should be replaced with happy path
    // tests that actually test the effects of updating an address' properties
    // once properly supported.
    {
        let address_state_provider = add_address(
            &control,
            fidl_fuchsia_net::InterfaceAddress::Ipv6(fidl_ip_v6!("fe80::1")),
            VALID_ADDRESS_PARAMETERS,
        )
        .await
        .expect("failed to add address");
        let () = address_state_provider
            .update_address_properties(fidl_fuchsia_net_interfaces_admin::AddressProperties::EMPTY)
            .await
            .expect("FIDL error calling AddressStateProvider.UpdateAddressProperties");
        matches::assert_matches!(
            address_state_provider.take_event_stream().try_next().await,
            Ok(Some(
                fidl_fuchsia_net_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved {
                    error: fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::UserRemoved
                }
            ))
        );
    }
}

#[variants_test]
async fn interfaces_admin_add_address_removal<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let (realm, stack, device) = sandbox
        .new_netstack_and_device::<Netstack2, E, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .await
        .expect("failed to create netstack realm");
    let interface =
        device.into_interface_in_realm(&realm).await.expect("failed to add endpoint to Netstack");
    let id = interface.id();

    let () = interface.enable_interface().await.expect("failed to enable interface");
    let () = interface.set_link_up(true).await.expect("failed to bring device up");

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let (control, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create Control proxy");
    let () = debug_control.get_admin(id, server).expect("get admin");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Adding a valid address and observing the address removal.
    {
        let mut address =
            fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_ip_v4_with_prefix!("3.3.3.3/32"));

        let address_state_provider = add_address(&control, address, VALID_ADDRESS_PARAMETERS)
            .await
            .expect("add address failed unexpectedly");

        let () = control
            .remove_address(&mut address)
            .await
            .expect("FIDL error calling Control.RemoveAddress")
            .expect("error calling Control.RemoveAddress");

        let fidl_fuchsia_net_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved {
            error: reason,
        } = address_state_provider
            .take_event_stream()
            .try_next()
            .await
            .expect("failed to read AddressStateProvider event")
            .expect("AddressStateProvider event stream ended unexpectedly");
        assert_eq!(reason, fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::UserRemoved);
    }

    // Adding a valid address and removing the interface.
    {
        let address =
            fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_ip_v4_with_prefix!("4.4.4.4/32"));

        let address_state_provider = add_address(&control, address, VALID_ADDRESS_PARAMETERS)
            .await
            .expect("add address failed unexpectedly");

        let () = stack
            .del_ethernet_interface(id)
            .await
            .squash_result()
            .expect("delete ethernet interface");

        let fidl_fuchsia_net_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved {
            error: reason,
        } = address_state_provider
            .take_event_stream()
            .try_next()
            .await
            .expect("failed to read AddressStateProvider event")
            .expect("AddressStateProvider event stream ended unexpectedly");
        assert_eq!(
            reason,
            fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::InterfaceRemoved
        );

        matches::assert_matches!(
            control.take_event_stream().try_collect::<Vec<_>>().await.as_ref().map(Vec::as_slice),
            // TODO(https://fxbug.dev/76695): Sending epitaphs not supported in Go.
            Ok([])
        );
    }
}

// Add an address while the interface is offline, bring the interface online and ensure that the
// assignment state is set correctly.
#[variants_test]
async fn interfaces_admin_add_address_offline<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let (realm, _, device) = sandbox
        .new_netstack_and_device::<Netstack2, E, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .await
        .expect("failed to create netstack realm");
    let interface =
        device.into_interface_in_realm(&realm).await.expect("failed to add endpoint to Netstack");
    let id = interface.id();

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let (control, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create Control proxy");
    let () = debug_control.get_admin(id, server).expect("get admin");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Adding a valid address and observing the address removal.
    let mut address =
        fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_ip_v4_with_prefix!("5.5.5.5/32"));

    let (address_state_provider, server) = fidl::endpoints::create_proxy::<
        fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
    >()
    .expect("create AddressStateProvider proxy");
    let () = control
        .add_address(&mut address, VALID_ADDRESS_PARAMETERS, server)
        .expect("Control.AddAddress FIDL error");

    let state_stream = fidl_fuchsia_net_interfaces_ext::admin::assignment_state_stream(
        address_state_provider.clone(),
    );
    futures::pin_mut!(state_stream);
    let () = fidl_fuchsia_net_interfaces_ext::admin::wait_assignment_state(
        &mut state_stream,
        fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Unavailable,
    )
    .await
    .expect("failed to wait for UNAVAILABLE address assignment state");

    let () = interface.enable_interface().await.expect("failed to enable interface");
    let () = interface.set_link_up(true).await.expect("failed to bring device up");

    let () = fidl_fuchsia_net_interfaces_ext::admin::wait_assignment_state(
        &mut state_stream,
        fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Assigned,
    )
    .await
    .expect("failed to wait for ASSIGNED address assignment state");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn interfaces_admin_add_address_success() {
    let name = "interfaces_admin_add_address_success";

    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let (realm, _) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .expect("new netstack");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect(<fidl_fuchsia_net_interfaces::StateMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("failed to create fuchsia.net.interfaces/Watcher event stream"),
        HashMap::new(),
    )
    .await
    .expect("initial");
    assert_eq!(interfaces.len(), 1);
    let id = interfaces
        .keys()
        .next()
        .expect("interface properties map unexpectedly does not include loopback");

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let netstack = realm
        .connect_to_protocol::<fidl_fuchsia_netstack::NetstackMarker>()
        .expect("failed to connect to fuchsia.netstack/Netstack");

    let (control, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create Control proxy");
    let () = debug_control.get_admin(*id, server).expect("get admin");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Adding a valid address succeeds.
    {
        let v4_with_prefix = fidl_ip_v4_with_prefix!("1.1.1.1/32");
        let subnet = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(v4_with_prefix.addr),
            prefix_len: v4_with_prefix.prefix_len,
        };
        let address = fidl_fuchsia_net::InterfaceAddress::Ipv4(v4_with_prefix);

        let address_state_provider = add_address(&control, address, VALID_ADDRESS_PARAMETERS)
            .await
            .expect("add address failed unexpectedly");

        // Ensure that no route to the subnet was added as a result of adding the address.
        assert!(netstack
            .get_route_table()
            .await
            .expect("FIDL error calling fuchsia.netstack/Netstack.GetRouteTable")
            .into_iter()
            .all(|r| r.destination != subnet));

        let (watcher, server_endpoint) =
            ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
                .expect("failed to create fuchsia.net.interfaces/Watcher proxy endpoints");
        let () = interface_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server_endpoint)
            .expect("error calling fuchsia.net.interfaces/State.GetWatcher");
        let mut properties = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(*id);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut properties,
            |fidl_fuchsia_net_interfaces_ext::Properties {
                 id: _,
                 name: _,
                 device_class: _,
                 online: _,
                 addresses,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| {
                addresses
                    .iter()
                    .any(|&fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                        addr == subnet
                    })
                    .then(|| ())
            },
        )
        .await
        .expect("failed to wait for address presence");

        // Explicitly drop the AddressStateProvider channel to cause address deletion.
        std::mem::drop(address_state_provider);

        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut properties,
            |fidl_fuchsia_net_interfaces_ext::Properties {
                 id: _,
                 name: _,
                 device_class: _,
                 online: _,
                 addresses,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| {
                addresses
                    .iter()
                    .all(|&fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                        addr != subnet
                    })
                    .then(|| ())
            },
        )
        .await
        .expect("failed to wait for address absence");
    }

    // TODO(https://fxbug.dev/81929): This test case currently tests that when
    // adding an IPv6 address, a prefix length of 128 is observed on the
    // interface watcher, but once the aforementioned bug is closed, this test
    // can be removed.
    {
        let addr = fidl_ip_v6!("11:11::1");
        let interface_addr = fidl_fuchsia_net::InterfaceAddress::Ipv6(addr);
        let subnet = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv6(addr),
            prefix_len: 128,
        };

        // Must hold onto AddressStateProvider since dropping the channel
        // removes the address.
        let _address_state_provider =
            add_address(&control, interface_addr, VALID_ADDRESS_PARAMETERS)
                .await
                .expect("add address failed unexpectedly");

        let mut properties = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(*id);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
                .expect("failed to create interface event stream"),
            &mut properties,
            |fidl_fuchsia_net_interfaces_ext::Properties {
                 id: _,
                 name: _,
                 device_class: _,
                 online: _,
                 addresses,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| {
                addresses
                    .iter()
                    .any(|&fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                        addr == subnet
                    })
                    .then(|| ())
            },
        )
        .await
        .expect("failed to wait for address presence");
    }

    // Adding a valid address and detaching does not cause the address to be removed.
    {
        let addr = fidl_ip_v4_with_prefix!("2.2.2.2/32");
        let address = fidl_fuchsia_net::InterfaceAddress::Ipv4(addr);
        let subnet = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(addr.addr),
            prefix_len: addr.prefix_len,
        };

        let address_state_provider = add_address(&control, address, VALID_ADDRESS_PARAMETERS)
            .await
            .expect("add address failed unexpectedly");

        let () = address_state_provider
            .detach()
            .expect("FIDL error calling fuchsia.net.interfaces.admin/Control.Detach");

        std::mem::drop(address_state_provider);

        let mut properties = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(*id);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
                .expect("failed to create interface event stream"),
            &mut properties,
            |fidl_fuchsia_net_interfaces_ext::Properties {
                 id: _,
                 name: _,
                 device_class: _,
                 online: _,
                 addresses,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| {
                addresses
                    .iter()
                    .all(|&fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                        addr != subnet
                    })
                    .then(|| ())
            },
        )
        .map_ok(|()| panic!("address deleted after detaching and closing channel"))
        .on_timeout(fuchsia_async::Time::after(fuchsia_zircon::Duration::from_millis(100)), || {
            Ok(())
        })
        .await
        .expect("failed to wait for address to not be removed");
    }
}

/// Tests that adding an interface causes an interface changed event.
#[variants_test]
async fn test_add_remove_interface<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, stack, device) = sandbox
        .new_netstack_and_device::<Netstack2, E, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .await
        .context("failed to create netstack realm")?;

    let id = device.add_to_stack(&realm).await.context("failed to add device")?;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .context("failed to initialize interface watcher")?;

    let mut if_map = HashMap::new();
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
        |if_map| if_map.contains_key(&id).then(|| ()),
    )
    .await
    .context("failed to observe interface addition")?;

    let () = stack
        .del_ethernet_interface(id)
        .await
        .squash_result()
        .context("failed to delete device")?;

    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
        |if_map| (!if_map.contains_key(&id)).then(|| ()),
    )
    .await
    .context("failed to observe interface addition")?;

    Ok(())
}

/// Tests that if a device closes (is removed from the system), the
/// corresponding Netstack interface is deleted.
/// if `enabled` is `true`, enables the interface before closing the device.
async fn test_close_interface<E: netemul::Endpoint>(enabled: bool, name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, stack, device) = sandbox
        .new_netstack_and_device::<Netstack2, E, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .await
        .context("failed to create netstack realm")?;

    let id = device.add_to_stack(&realm).await.context("failed to add device")?;

    if enabled {
        let () = stack
            .enable_interface(id)
            .await
            .squash_result()
            .context("failed to enable interface")?;
    }

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .context("failed to initialize interface watcher")?;
    let mut if_map = HashMap::new();
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
        |if_map| if_map.contains_key(&id).then(|| ()),
    )
    .await
    .context("failed to observe interface addition")?;

    // Drop the device, that should cause the interface to be deleted.
    std::mem::drop(device);

    // Wait until we observe the removed interface is missing.
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        |if_map| {
            // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
            (!if_map.contains_key(&id)).then(|| ())
        },
    )
    .await
    .context("failed to observe interface removal")?;

    Ok(())
}

#[variants_test]
async fn test_close_disabled_interface<E: netemul::Endpoint>(name: &str) -> Result {
    test_close_interface::<E>(false, name).await
}

#[variants_test]
async fn test_close_enabled_interface<E: netemul::Endpoint>(name: &str) -> Result {
    test_close_interface::<E>(true, name).await
}

/// Tests races between device link down and close.
#[variants_test]
async fn test_down_close_race<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>(name)
        .context("failed to create netstack realm")?;
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .context("failed to initialize interface watcher")?;
    let mut if_map = HashMap::new();

    for _ in 0..10u64 {
        let dev = sandbox
            .create_endpoint::<E, _>("ep")
            .await
            .context("failed to create endpoint")?
            .into_interface_in_realm(&realm)
            .await
            .context("failed to add endpoint to Netstack")?;

        let () = dev.enable_interface().await.context("failed to enable interface")?;
        let () = dev.start_dhcp().await.context("failed to start DHCP")?;
        let () = dev.set_link_up(true).await.context("failed to bring device up")?;

        let id = dev.id();
        // Wait until the interface is installed and the link state is up.
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                let &fidl_fuchsia_net_interfaces_ext::Properties { online, .. } =
                    if_map.get(&id)?;
                // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
                online.then(|| ())
            },
        )
        .await
        .context("failed to observe interface online")?;

        // Here's where we cause the race. We bring the device's link down
        // and drop it right after; the two signals will race to reach
        // Netstack.
        let () = dev.set_link_up(false).await.context("failed to bring link down")?;
        std::mem::drop(dev);

        // Wait until the interface is removed from Netstack cleanly.
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
                (!if_map.contains_key(&id)).then(|| ())
            },
        )
        .await
        .context("failed to observe interface removal")?;
    }
    Ok(())
}

/// Tests races between data traffic and closing a device.
#[variants_test]
async fn test_close_data_race<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;
    let fake_ep = net.create_fake_endpoint().context("failed to create fake endpoint")?;
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>(name)
        .context("failed to create netstack realm")?;

    // NOTE: We only run this test with IPv4 sockets since we only care about
    // exciting the tx path, the domain is irrelevant.
    const DEVICE_ADDRESS: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");
    // We're going to send data over a UDP socket to a multicast address so we
    // skip ARP resolution.
    const MCAST_ADDR: std::net::IpAddr = std_ip!("224.0.0.1");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .context("failed to initialize interface watcher")?;
    let mut if_map = HashMap::new();
    for _ in 0..10u64 {
        let dev = net
            .create_endpoint::<E, _>("ep")
            .await
            .context("failed to create endpoint")?
            .into_interface_in_realm(&realm)
            .await
            .context("failed to add endpoint to Netstack")?;

        let () = dev.enable_interface().await.context("failed to enable interface")?;
        let () = dev.set_link_up(true).await.context("failed to bring device up")?;
        let () = dev.add_ip_addr(DEVICE_ADDRESS).await.context("failed to add address")?;

        let id = dev.id();
        // Wait until the interface is installed and the link state is up.
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                let &fidl_fuchsia_net_interfaces_ext::Properties { online, .. } =
                    if_map.get(&id)?;
                // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
                online.then(|| ())
            },
        )
        .await
        .context("failed to observe interface online")?;
        // Create a socket and start sending data on it nonstop.
        let fidl_fuchsia_net_ext::IpAddress(bind_addr) = DEVICE_ADDRESS.addr.into();
        let sock = fuchsia_async::net::UdpSocket::bind_in_realm(
            &realm,
            std::net::SocketAddr::new(bind_addr, 0),
        )
        .await
        .context("failed to create socket")?;

        // Keep sending data until writing to the socket fails.
        let io_fut = async {
            loop {
                match sock
                    .send_to(&[1u8, 2, 3, 4], std::net::SocketAddr::new(MCAST_ADDR, 1234))
                    .await
                {
                    Ok(_sent) => {}
                    // We expect only "os errors" to happen, ideally we'd look
                    // only at specific errors (EPIPE, ENETUNREACH), but that
                    // made this test very flaky due to the branching error
                    // paths in gVisor when removing an interface.
                    Err(e) if e.raw_os_error().is_some() => break Result::Ok(()),
                    Err(e) => break Err(e).context("send_to error"),
                }

                // Enqueue some data on the rx path.
                let () = fake_ep
                    // We don't care that it's a valid frame, only that it excites
                    // the rx path.
                    .write(&[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16])
                    .await
                    .context("failed to send frame on fake_ep")?;

                // Wait on a short timer to avoid too much log noise when
                // running the test.
                let () = fuchsia_async::Timer::new(fuchsia_async::Time::after(
                    fuchsia_zircon::Duration::from_micros(10),
                ))
                .await;
            }
        };

        let drop_fut = async move {
            let () = fuchsia_async::Timer::new(fuchsia_async::Time::after(
                fuchsia_zircon::Duration::from_millis(3),
            ))
            .await;
            std::mem::drop(dev);
        };

        let iface_dropped = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
                (!if_map.contains_key(&id)).then(|| ())
            },
        );

        let (io_result, iface_dropped, ()) =
            futures::future::join3(io_fut, iface_dropped, drop_fut).await;
        let () = io_result.context("unexpected error on io future")?;
        let () = iface_dropped.context("failed to observe interface removal")?;
    }
    Ok(())
}

/// Tests that competing interface change events are reported by
/// fuchsia.net.interfaces/Watcher in the correct order.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_interfaces_watcher_race() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>("interfaces_watcher_race")
        .context("failed to create netstack realm")?;
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to fuchsia.net.interfaces/State")?;
    for _ in 0..100 {
        let (watcher, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
        let () = interface_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)
            .context("failed to initialize interface watcher")?;

        let ep = sandbox
            // We don't need to run variants for this test, all we care about is
            // the Netstack race. Use NetworkDevice because it's lighter weight.
            .create_endpoint::<netemul::NetworkDevice, _>("ep")
            .await
            .context("failed to create fixed ep")?
            .into_interface_in_realm(&realm)
            .await
            .context("failed to install in realm")?;

        const ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.1/24");

        // Bring the link up, enable the interface, and add an IP address
        // "non-sequentially" (as much as possible) to cause races in Netstack
        // when reporting events.
        let ((), (), ()) = futures::future::try_join3(
            ep.set_link_up(true).map(|r| r.context("failed to bring link up")),
            ep.enable_interface().map(|r| r.context("failed to enable interface")),
            ep.add_ip_addr(ADDR).map(|r| r.context("failed to add address")),
        )
        .await?;

        let id = ep.id();
        let () = futures::stream::try_unfold(
            (watcher, false, false, false),
            |(watcher, present, up, has_addr)| async move {
                let event = watcher.watch().await.context("failed to watch")?;

                let (mut new_present, mut new_up, mut new_has_addr) = (present, up, has_addr);
                match event {
                    fidl_fuchsia_net_interfaces::Event::Added(properties)
                    | fidl_fuchsia_net_interfaces::Event::Existing(properties) => {
                        if properties.id == Some(id) {
                            assert!(!present, "duplicate added/existing event");
                            new_present = true;
                            new_up = properties
                                .online
                                .expect("added/existing event missing online property");
                            new_has_addr = properties
                                .addresses
                                .expect("added/existing event missing addresses property")
                                .iter()
                                .any(|a| a.addr == Some(ADDR));
                        }
                    }
                    fidl_fuchsia_net_interfaces::Event::Changed(properties) => {
                        if properties.id == Some(id) {
                            assert!(
                                present,
                                "property change event before added or existing event"
                            );
                            if let Some(online) = properties.online {
                                new_up = online;
                            }
                            if let Some(addresses) = properties.addresses {
                                new_has_addr = addresses.iter().any(|a| a.addr == Some(ADDR));
                            }
                        }
                    }
                    fidl_fuchsia_net_interfaces::Event::Removed(removed_id) => {
                        if removed_id == id {
                            assert!(present, "removed event before added or existing");
                            new_present = false;
                        }
                    }
                    _ => {}
                }
                println!(
                    "Observed interfaces, previous state = ({}, {}, {}), new state = ({}, {}, {})",
                    present, up, has_addr, new_present, new_up, new_has_addr
                );

                // Verify that none of the observed states can be seen as
                // "undone" by bad event ordering in Netstack. We don't care
                // about the order in which we see the events since we're
                // intentionally racing some things, only that nothing tracks
                // back.

                if present {
                    // Device should not disappear.
                    assert!(new_present, "out of order events, device disappeared");
                }
                if up {
                    // Device should not go offline.
                    assert!(new_up, "out of order events, device went offline");
                }
                if has_addr {
                    // Address should not disappear.
                    assert!(new_has_addr, "out of order events, address disappeared");
                }
                Result::Ok(if new_present && new_up && new_has_addr {
                    // We got everything we wanted, end the stream.
                    None
                } else {
                    // Continue folding with the new state.
                    Some(((), (watcher, new_present, new_up, new_has_addr)))
                })
            },
        )
        .try_collect()
        .await?;
    }
    Ok(())
}

/// Test interface changes are reported through the interface watcher.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_interfaces_watcher() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (realm, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>("interfaces_watcher")?;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to fuchsia.net.interfaces/State")?;

    let initialize_watcher = || async {
        let (watcher, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
        let () = interface_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)
            .context("failed to initialize interface watcher")?;

        let event = watcher.watch().await.context("failed to watch")?;
        if let fidl_fuchsia_net_interfaces::Event::Existing(properties) = event {
            assert_eq!(
                properties.device_class,
                Some(fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                    fidl_fuchsia_net_interfaces::Empty {}
                ))
            );
        } else {
            panic!("got {:?}, want loopback interface existing event", event);
        }

        assert_eq!(
            watcher.watch().await.context("failed to watch")?,
            fidl_fuchsia_net_interfaces::Event::Idle(fidl_fuchsia_net_interfaces::Empty {})
        );
        Result::Ok(watcher)
    };

    let blocking_watcher = initialize_watcher().await?;
    let blocking_stream = fidl_fuchsia_net_interfaces_ext::event_stream(blocking_watcher.clone());
    pin_utils::pin_mut!(blocking_stream);
    let watcher = initialize_watcher().await?;
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone());
    pin_utils::pin_mut!(stream);

    async fn assert_blocked<S>(stream: &mut S) -> Result
    where
        S: futures::stream::TryStream<Error = fidl::Error> + std::marker::Unpin,
        <S as futures::TryStream>::Ok: std::fmt::Debug,
    {
        stream
            .try_next()
            .map(|event| {
                let event = event.context("event stream error")?;
                let event = event.ok_or_else(|| anyhow::anyhow!("watcher event stream ended"))?;
                Ok(Some(event))
            })
            .on_timeout(
                fuchsia_async::Time::after(fuchsia_zircon::Duration::from_millis(50)),
                || Ok(None),
            )
            .and_then(|e| {
                futures::future::ready(match e {
                    Some(e) => Err(anyhow::anyhow!("did not block but yielded {:?}", e)),
                    None => Ok(()),
                })
            })
            .await
    }
    // Add an interface.
    let () = assert_blocked(&mut blocking_stream).await?;
    let dev = sandbox
        .create_endpoint::<netemul::NetworkDevice, _>("ep")
        .await
        .context("failed to create endpoint")?
        .into_interface_in_realm(&realm)
        .await
        .context("failed to add endpoint to Netstack")?;
    let () = dev.set_link_up(true).await.context("failed to bring device up")?;
    let id = dev.id();
    let want = fidl_fuchsia_net_interfaces::Event::Added(fidl_fuchsia_net_interfaces::Properties {
        id: Some(id),
        // We're not explicitly setting the name when adding the interface, so
        // this may break if Netstack changes how it names interfaces.
        name: Some(format!("eth{}", id)),
        online: Some(false),
        device_class: Some(fidl_fuchsia_net_interfaces::DeviceClass::Device(
            fidl_fuchsia_hardware_network::DeviceClass::Virtual,
        )),
        addresses: Some(vec![]),
        has_default_ipv4_route: Some(false),
        has_default_ipv6_route: Some(false),
        ..fidl_fuchsia_net_interfaces::Properties::EMPTY
    });
    async fn try_next<S>(stream: &mut S) -> Result<fidl_fuchsia_net_interfaces::Event>
    where
        S: futures::stream::TryStream<Ok = fidl_fuchsia_net_interfaces::Event, Error = fidl::Error>
            + Unpin,
    {
        stream.try_next().await?.ok_or_else(|| anyhow::anyhow!("watcher event stream ended"))
    }
    assert_eq!(try_next(&mut blocking_stream).await?, want);
    assert_eq!(try_next(&mut stream).await?, want);

    // Set the link to up.
    let () = assert_blocked(&mut blocking_stream).await?;
    let () = dev.enable_interface().await.context("failed to bring device up")?;
    // NB The following fold function is necessary because IPv6 link-local addresses are configured
    // when the interface is brought up (and removed when the interface is brought down) such
    // that the ordering or the number of events that reports the changes in the online and
    // addresses properties cannot be guaranteed. As such, we assert that:
    //
    // 1. the online property MUST change to the expected value in the first event and never change
    //    again,
    // 2. the addresses property changes over some number of events (including possibly the first
    //    event) and eventually reaches the desired count, and
    // 3. no other properties change in any of the events.
    //
    // It would be ideal to disable IPv6 LL address configuration for this test, which would
    // simplify this significantly.
    let fold_fn = |want_online, want_addr_count| {
        move |(mut online_changed, mut addresses), event| match event {
            fidl_fuchsia_net_interfaces::Event::Changed(
                fidl_fuchsia_net_interfaces::Properties {
                    id: Some(event_id),
                    online,
                    addresses: got_addrs,
                    name: None,
                    device_class: None,
                    has_default_ipv4_route: None,
                    has_default_ipv6_route: None,
                    ..
                },
            ) if event_id == id => {
                if let Some(got_online) = online {
                    if online_changed {
                        return futures::future::err(anyhow::anyhow!(
                            "duplicate online property change to new value of {}",
                            got_online,
                        ));
                    }
                    if got_online != want_online {
                        return futures::future::err(anyhow::anyhow!(
                            "got online: {}, want {}",
                            got_online,
                            want_online
                        ));
                    }
                    online_changed = true;
                }
                if let Some(got_addrs) = got_addrs {
                    if !online_changed {
                        return futures::future::err(anyhow::anyhow!(
                            "addresses changed before online property change, addresses: {:?}",
                            got_addrs
                        ));
                    }
                    let got_addrs = got_addrs
                        .iter()
                        .filter_map(
                            |&fidl_fuchsia_net_interfaces::Address {
                                 addr, valid_until, ..
                             }| {
                                assert_eq!(
                                    valid_until,
                                    Some(fuchsia_zircon::sys::ZX_TIME_INFINITE)
                                );
                                let subnet = addr?;
                                match &subnet.addr {
                                    fidl_fuchsia_net::IpAddress::Ipv4(
                                        fidl_fuchsia_net::Ipv4Address { addr: _ },
                                    ) => None,
                                    fidl_fuchsia_net::IpAddress::Ipv6(
                                        fidl_fuchsia_net::Ipv6Address { addr: _ },
                                    ) => Some(subnet),
                                }
                            },
                        )
                        .collect::<HashSet<_>>();
                    if got_addrs.len() == want_addr_count {
                        return futures::future::ok(async_utils::fold::FoldWhile::Done(got_addrs));
                    }
                    addresses = Some(got_addrs);
                }
                futures::future::ok(async_utils::fold::FoldWhile::Continue((
                    online_changed,
                    addresses,
                )))
            }
            _ => futures::future::err(anyhow::anyhow!(
                "got: {:?}, want online and/or IPv6 link-local address change event",
                event
            )),
        }
    };
    const LL_ADDR_COUNT: usize = 1;
    let want_online = true;
    let ll_addrs = async_utils::fold::try_fold_while(
        blocking_stream.map(|r| r.context("blocking event stream error")),
        (false, None),
        fold_fn(want_online, LL_ADDR_COUNT),
    )
    .await
    .context("error processing events")
    .and_then(|fold_res| {
        fold_res.short_circuited().map_err(|(online_changed, addresses)| {
            anyhow::anyhow!(
                "event stream ended unexpectedly, final state online_changed={} addresses={:?}",
                online_changed,
                addresses
            )
        })
    })
    .with_context(|| {
        format!(
            "error while waiting for interface online={} and LL addr count={}",
            want_online, LL_ADDR_COUNT
        )
    })?;
    let addrs = async_utils::fold::try_fold_while(
        stream.map(|r| r.context("non-blocking event stream error")),
        (false, None),
        fold_fn(true, LL_ADDR_COUNT),
    )
    .await
    .context("error processing events")
    .and_then(|fold_res| {
        fold_res.short_circuited().map_err(|(online_changed, addresses)| {
            anyhow::anyhow!(
                "watcher event stream ended unexpectedly, final state online_changed={} addresses={:?}",
                online_changed,
                addresses
            )
        })
    })
    .with_context(|| format!("error while waiting for interface online={} and LL addr={}",
            want_online, LL_ADDR_COUNT))?;
    assert_eq!(ll_addrs, addrs);
    let blocking_stream = fidl_fuchsia_net_interfaces_ext::event_stream(blocking_watcher.clone());
    pin_utils::pin_mut!(blocking_stream);
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone());
    pin_utils::pin_mut!(stream);

    // Add an address.
    let () = assert_blocked(&mut blocking_stream).await?;
    let mut subnet = fidl_subnet!("192.168.0.1/16");
    let () = stack
        .add_interface_address(id, &mut subnet)
        .await
        .squash_result()
        .context("failed to add address")?;
    let addresses_changed = |event| match event {
        fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
            id: Some(event_id),
            addresses: Some(addresses),
            name: None,
            device_class: None,
            online: None,
            has_default_ipv4_route: None,
            has_default_ipv6_route: None,
            ..
        }) if event_id == id => Ok(addresses
            .iter()
            .filter_map(|&fidl_fuchsia_net_interfaces::Address { addr, valid_until, .. }| {
                assert_eq!(valid_until, Some(fuchsia_zircon::sys::ZX_TIME_INFINITE));
                addr
            })
            .collect::<HashSet<_>>()),
        _ => Err(anyhow::anyhow!("got: {:?}, want changed event with added IPv4 address", event)),
    };
    let want = ll_addrs.iter().cloned().chain(std::iter::once(subnet)).collect();
    assert_eq!(addresses_changed(try_next(&mut blocking_stream).await?)?, want);
    assert_eq!(addresses_changed(try_next(&mut stream).await?)?, want);

    // Add a default route.
    let () = assert_blocked(&mut blocking_stream).await?;
    let mut default_v4_subnet = fidl_subnet!("0.0.0.0/0");
    let () = stack
        .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
            subnet: default_v4_subnet,
            destination: fidl_fuchsia_net_stack::ForwardingDestination::NextHop(fidl_ip!(
                "192.168.255.254"
            )),
        })
        .await
        .squash_result()
        .context("failed to add default route")?;
    let want =
        fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
            id: Some(id),
            has_default_ipv4_route: Some(true),
            ..fidl_fuchsia_net_interfaces::Properties::EMPTY
        });
    assert_eq!(try_next(&mut blocking_stream).await?, want);
    assert_eq!(try_next(&mut stream).await?, want);

    // Remove the default route.
    let () = assert_blocked(&mut blocking_stream).await?;
    let () = stack
        .del_forwarding_entry(&mut default_v4_subnet)
        .await
        .squash_result()
        .context("failed to delete default route")?;
    let want =
        fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
            id: Some(id),
            has_default_ipv4_route: Some(false),
            ..fidl_fuchsia_net_interfaces::Properties::EMPTY
        });
    assert_eq!(try_next(&mut blocking_stream).await?, want);
    assert_eq!(try_next(&mut stream).await?, want);

    // Remove the added address.
    let () = assert_blocked(&mut blocking_stream).await?;
    let () = stack
        .del_interface_address(id, &mut subnet)
        .await
        .squash_result()
        .context("failed to add address")?;
    assert_eq!(addresses_changed(try_next(&mut blocking_stream).await?)?, ll_addrs);
    assert_eq!(addresses_changed(try_next(&mut stream).await?)?, ll_addrs);

    // Set the link to down.
    let () = assert_blocked(&mut blocking_stream).await?;
    let () = dev.set_link_up(false).await.context("failed to bring device up")?;
    const LL_ADDR_COUNT_AFTER_LINK_DOWN: usize = 0;
    let want_online = false;
    let addresses = async_utils::fold::try_fold_while(
        blocking_stream.map(|r| r.context("blocking event stream error")),
        (false, None),
        fold_fn(want_online, LL_ADDR_COUNT_AFTER_LINK_DOWN),
    )
    .await
    .context("error processing events")
    .and_then(|fold_res| {
        fold_res.short_circuited().map_err(|(online_changed, addresses)| {
            anyhow::anyhow!(
                "watcher event stream ended unexpectedly, final state online_changed={} addresses={:?}",
                online_changed,
                addresses
            )
        })
    })
    .with_context(|| format!("error while waiting for interface online={} and LL addr count={}",
            want_online, LL_ADDR_COUNT_AFTER_LINK_DOWN))?;
    assert!(addresses.is_subset(&ll_addrs), "got {:?}, want a subset of {:?}", addresses, ll_addrs);
    assert_eq!(
        async_utils::fold::try_fold_while(
            stream.map(|r| r.context("non-blocking event stream error")),
            (false, None),
            fold_fn(false, LL_ADDR_COUNT_AFTER_LINK_DOWN)
        )
        .await
        .with_context(|| format!(
            "error while waiting for interface online={} and LL addr count={}",
            want_online, LL_ADDR_COUNT_AFTER_LINK_DOWN
        ))?,
        async_utils::fold::FoldResult::ShortCircuited(addresses),
    );
    let blocking_stream = fidl_fuchsia_net_interfaces_ext::event_stream(blocking_watcher);
    pin_utils::pin_mut!(blocking_stream);
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher);
    pin_utils::pin_mut!(stream);

    // Remove the ethernet interface.
    let () = assert_blocked(&mut blocking_stream).await?;
    std::mem::drop(dev);
    let want = fidl_fuchsia_net_interfaces::Event::Removed(id);
    assert_eq!(try_next(&mut blocking_stream).await?, want);
    assert_eq!(try_next(&mut stream).await?, want);

    Ok(())
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

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let sandbox = &sandbox;
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>(name)
        .expect("failed to create netstack realm");
    let realm = &realm;

    let net_ep_iface = |net_num: u8, addr: fidl_fuchsia_net::Subnet| async move {
        let net = sandbox
            .create_network(format!("net{}", net_num))
            .await
            .expect("failed to create network");
        let fake_ep = net.create_fake_endpoint().expect("failed to create fake endpoint");
        let iface = realm
            .join_network::<E, _>(
                &net,
                format!("iface{}", net_num),
                &netemul::InterfaceConfig::StaticIp(addr),
            )
            .await
            .expect("failed to configure networking");

        (net, fake_ep, iface)
    };

    let (_net1, fake_ep1, iface1) = net_ep_iface(1, iface1_addr).await;
    let (_net2, fake_ep2, iface2) = net_ep_iface(2, iface2_addr).await;

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect to fuchsia.net.interfaces/State");

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
        .expect("failed to connect to Controller");
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
        .expect("failed to serialize ICMP packet")
        .unwrap_b();

    let duration = if expect_forward {
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
    } else {
        ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT
    };

    let ((), forwarded) = futures::future::join(
        fake_ep1.write(ser.as_ref()).map(|r| r.expect("failed to write to fake endpoint #1")),
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

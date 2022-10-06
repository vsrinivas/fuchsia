// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use assert_matches::assert_matches;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_interfaces_admin as finterfaces_admin;
use fidl_fuchsia_netemul_network as fnetemul_network;
use fuchsia_async::{self as fasync, TimeoutExt as _};
use fuchsia_zircon_status as zx_status;
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::{fidl_ip, fidl_mac, fidl_subnet, std_ip_v6, std_socket_addr};
use net_types::ip::IpAddress as _;
use netemul::RealmUdpSocket as _;
use netstack_testing_common::{
    devices::create_tun_device,
    interfaces,
    realms::{Netstack, Netstack2, NetstackVersion, TestRealmExt as _, TestSandboxExt as _},
    ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;
use std::collections::{HashMap, HashSet};
use std::convert::TryInto as _;
use test_case::test_case;

#[variants_test]
async fn address_deprecation<E: netemul::Endpoint>(name: &str) {
    // TODO(https://fxbug.dev/105630, https://fxbug.dev/106959): Test against
    // Netstack3 once adding deprecated addresses and updating address lifetimes
    // are supported.
    type N = Netstack2;
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    assert!(interface.control().enable().await.expect("send enable").expect("enable"));
    let () = interface.set_link_up(true).await.expect("bring device up");

    const ADDR1: std::net::Ipv6Addr = std_ip_v6!("abcd::1");
    const ADDR2: std::net::Ipv6Addr = std_ip_v6!("abcd::2");
    // Cannot be const because `std::net::SocketAddrV6:new` isn't const.
    let sock_addr = std_socket_addr!("[abcd::3]:12345");
    // Note that the absence of the preferred_lifetime_info field implies infinite
    // preferred lifetime.
    const PREFERRED_PROPERTIES: fidl_fuchsia_net_interfaces_admin::AddressProperties =
        fidl_fuchsia_net_interfaces_admin::AddressProperties::EMPTY;
    const DEPRECATED_PROPERTIES: fidl_fuchsia_net_interfaces_admin::AddressProperties =
        fidl_fuchsia_net_interfaces_admin::AddressProperties {
            preferred_lifetime_info: Some(
                fidl_fuchsia_net_interfaces::PreferredLifetimeInfo::Deprecated(
                    fidl_fuchsia_net_interfaces::Empty,
                ),
            ),
            ..fidl_fuchsia_net_interfaces_admin::AddressProperties::EMPTY
        };
    let addr1_state_provider = interfaces::add_subnet_address_and_route_wait_assigned(
        &interface,
        fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                addr: ADDR1.octets(),
            }),
            prefix_len: 16,
        },
        // Note that an empty AddressParameters means that the address has
        // infinite preferred lifetime.
        fidl_fuchsia_net_interfaces_admin::AddressParameters {
            initial_properties: Some(PREFERRED_PROPERTIES.clone()),
            ..fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY
        },
    )
    .await
    .expect("failed to add preferred address");

    let addr2_state_provider = interfaces::add_address_wait_assigned(
        interface.control(),
        fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                addr: ADDR2.octets(),
            }),
            prefix_len: (ADDR2.octets().len() * 8).try_into().unwrap(),
        },
        fidl_fuchsia_net_interfaces_admin::AddressParameters {
            initial_properties: Some(DEPRECATED_PROPERTIES.clone()),
            ..fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY
        },
    )
    .await
    .expect("failed to add deprecated address");

    let get_source_addr = || async {
        let sock = realm
            .datagram_socket(
                fidl_fuchsia_posix_socket::Domain::Ipv6,
                fidl_fuchsia_posix_socket::DatagramSocketProtocol::Udp,
            )
            .await
            .expect("failed to create UDP socket");
        sock.connect(&socket2::SockAddr::from(sock_addr)).expect("failed to connect with socket");
        *sock
            .local_addr()
            .expect("failed to get socket local addr")
            .as_socket_ipv6()
            .expect("socket local addr not IPv6")
            .ip()
    };
    assert_eq!(get_source_addr().await, ADDR1);

    addr1_state_provider
        .update_address_properties(DEPRECATED_PROPERTIES)
        .await
        .expect("FIDL error deprecating address");
    addr2_state_provider
        .update_address_properties(PREFERRED_PROPERTIES)
        .await
        .expect("FIDL error setting address to preferred");

    assert_eq!(get_source_addr().await, ADDR2);
}

#[variants_test]
async fn add_address_errors<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");

    let fidl_fuchsia_net_interfaces_ext::Properties {
        id: loopback_id,
        addresses,
        name: _,
        device_class: _,
        online: _,
        has_default_ipv4_route: _,
        has_default_ipv6_route: _,
    } = realm
        .loopback_properties()
        .await
        .expect("failed to get loopback properties")
        .expect("loopback not found");

    let control = realm
        .interface_control(loopback_id)
        .expect("failed to get loopback interface control client proxy");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Removing non-existent address.
    {
        let mut address = fidl_subnet!("1.1.1.1/32");
        let did_remove = control
            .remove_address(&mut address)
            .await
            .expect("FIDL error calling fuchsia.net.interfaces.admin/Control.RemoveAddress")
            .expect("RemoveAddress failed");
        assert!(!did_remove);
    }

    let (control, v4_addr, v6_addr) = futures::stream::iter(addresses).fold((control, None, None), |(control, v4, v6), fidl_fuchsia_net_interfaces_ext::Address {
        addr,
        valid_until: _,
    }| {
        let (v4, v6) = {
            let fidl_fuchsia_net::Subnet { addr, prefix_len } = addr;
            match addr {
                fidl_fuchsia_net::IpAddress::Ipv4(addr) => {
                    let nt_addr = net_types::ip::Ipv4Addr::new(addr.addr);
                    assert!(nt_addr.is_loopback(), "{} is not a loopback address", nt_addr);
                    let addr = fidl_fuchsia_net::Ipv4AddressWithPrefix {
                        addr,
                        prefix_len,
                    };
                    assert_eq!(v4, None, "v4 address already present, found {:?}", addr);
                    (Some(addr), v6)
                }
                fidl_fuchsia_net::IpAddress::Ipv6(addr) => {
                    let nt_addr = net_types::ip::Ipv6Addr::from_bytes(addr.addr);
                    assert!(nt_addr.is_loopback(), "{} is not a loopback address", nt_addr);
                    assert_eq!(v6, None, "v6 address already present, found {:?}", addr);
                    let addr = fidl_fuchsia_net::Ipv6AddressWithPrefix {
                        addr,
                        prefix_len,
                    };
                    (v4, Some(addr))
                }
            }
        };
        async move {
            assert_matches::assert_matches!(
                interfaces::add_address_wait_assigned(&control, addr.clone(), VALID_ADDRESS_PARAMETERS).await,
                Err(fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError::AddressRemoved(
                    fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::AlreadyAssigned
                )));
            (control, v4, v6)
        }
    }).await;
    assert_ne!(v4_addr, None, "expected v4 address");
    assert_ne!(v6_addr, None, "expected v6 address");

    // Adding an invalid address returns error.
    {
        // NB: fidl_subnet! doesn't allow invalid prefix lengths.
        let invalid_address =
            fidl_fuchsia_net::Subnet { addr: fidl_ip!("1.1.1.1"), prefix_len: 33 };
        assert_matches::assert_matches!(
            interfaces::add_address_wait_assigned(
                &control,
                invalid_address,
                VALID_ADDRESS_PARAMETERS
            )
            .await,
            Err(fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError::AddressRemoved(
                fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::Invalid
            ))
        );
    }
}

#[variants_test]
async fn add_address_removal<N: Netstack, E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    let id = interface.id();

    let expect_enable = !(E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
        && N::VERSION == NetstackVersion::Netstack3);
    assert_eq!(
        expect_enable,
        interface.control().enable().await.expect("send enable").expect("enable")
    );
    let () = interface.set_link_up(true).await.expect("bring device up");

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let (control, server) = fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
        .expect("create Control proxy");
    let () = debug_control.get_admin(id, server).expect("get admin");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Adding a valid address and observing the address removal.
    {
        let mut address = fidl_subnet!("3.3.3.3/32");

        let address_state_provider =
            interfaces::add_address_wait_assigned(&control, address, VALID_ADDRESS_PARAMETERS)
                .await
                .expect("add address failed unexpectedly");

        let did_remove = control
            .remove_address(&mut address)
            .await
            .expect("FIDL error calling Control.RemoveAddress")
            .expect("error calling Control.RemoveAddress");
        assert!(did_remove);

        let fidl_fuchsia_net_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved {
            error: reason,
        } = address_state_provider
            .take_event_stream()
            .try_next()
            .await
            .expect("read AddressStateProvider event")
            .expect("AddressStateProvider event stream ended unexpectedly");
        assert_eq!(reason, fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::UserRemoved);
    }

    // Adding a valid address and removing the interface.
    {
        let address = fidl_subnet!("4.4.4.4/32");

        let address_state_provider =
            interfaces::add_address_wait_assigned(&control, address, VALID_ADDRESS_PARAMETERS)
                .await
                .expect("add address failed unexpectedly");

        let (_netemul_endpoint, _device_control_handle) =
            interface.remove().await.expect("failed to remove interface");
        let fidl_fuchsia_net_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved {
            error: reason,
        } = address_state_provider
            .take_event_stream()
            .try_next()
            .await
            .expect("read AddressStateProvider event")
            .expect("AddressStateProvider event stream ended unexpectedly");
        assert_eq!(
            reason,
            fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::InterfaceRemoved
        );

        assert_matches::assert_matches!(
            control.wait_termination().await,
            fidl_fuchsia_net_interfaces_ext::admin::TerminalError::Terminal(
                fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::User
            )
        );
    }
}

// Add an address while the interface is offline, bring the interface online and ensure that the
// assignment state is set correctly.
#[variants_test]
async fn add_address_offline<N: Netstack, E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    let id = interface.id();

    // In Netstack3, Ethernet devices start enabled; disable the interface to
    // ensure a consistent state for each test variant.
    if E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
        && N::VERSION == NetstackVersion::Netstack3
    {
        assert!(interface.control().disable().await.expect("send disable").expect("disable"));
    }

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
    let mut address = fidl_subnet!("5.5.5.5/32");

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
    .expect("wait for UNAVAILABLE address assignment state");

    let did_enable = interface.control().enable().await.expect("send enable").expect("enable");
    assert!(did_enable);
    let () = interface.set_link_up(true).await.expect("bring device up");

    let () = fidl_fuchsia_net_interfaces_ext::admin::wait_assignment_state(
        &mut state_stream,
        fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Assigned,
    )
    .await
    .expect("wait for ASSIGNED address assignment state");
}

// Verify that a request to `WatchAddressAssignmentState` while an existing
// request is pending causes the `AddressStateProvider` protocol to close,
// regardless of whether the protocol is detached.
#[variants_test]
#[test_case(false; "no_detach")]
#[test_case(true; "detach")]
async fn duplicate_watch_address_assignment_state<N: Netstack, E: netemul::Endpoint>(
    name: &str,
    detach: bool,
) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    let expect_enable = !(E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
        && N::VERSION == NetstackVersion::Netstack3);
    assert_eq!(
        expect_enable,
        interface.control().enable().await.expect("send enable").expect("enable")
    );
    let () = interface.set_link_up(true).await.expect("bring device up");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;
    let address = fidl_subnet!("1.1.1.1/32");
    let address_state_provider = interfaces::add_address_wait_assigned(
        interface.control(),
        address,
        VALID_ADDRESS_PARAMETERS,
    )
    .await
    .expect("add address failed unexpectedly");

    if detach {
        address_state_provider.detach().expect("failed to detach");
    }

    // Invoke `WatchAddressAssignmentState` twice and assert that the two
    // requests and the AddressStateProvider protocol are closed.
    assert_matches!(
        futures::future::join(
            address_state_provider.watch_address_assignment_state(),
            address_state_provider.watch_address_assignment_state(),
        )
        .await,
        (
            Err(fidl::Error::ClientChannelClosed { status: zx_status::Status::PEER_CLOSED, .. }),
            Err(fidl::Error::ClientChannelClosed { status: zx_status::Status::PEER_CLOSED, .. }),
        )
    );
    assert_matches!(
        address_state_provider
            .take_event_stream()
            .try_next()
            .await
            .expect("read AddressStateProvider event"),
        None
    );
}

#[variants_test]
async fn add_address_success<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect(<fidl_fuchsia_net_interfaces::StateMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create watcher event stream"),
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

    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol");

    let (control, server) = fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
        .expect("create Control proxy");
    let () = debug_control.get_admin(*id, server).expect("get admin");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Adding a valid address succeeds.
    {
        let subnet = fidl_subnet!("1.1.1.1/32");
        let address_state_provider =
            interfaces::add_address_wait_assigned(&control, subnet, VALID_ADDRESS_PARAMETERS)
                .await
                .expect("add address failed unexpectedly");

        // Ensure that no route to the subnet was added as a result of adding the address.
        assert!(stack
            .get_forwarding_table()
            .await
            .expect("FIDL error calling fuchsia.net.stack/Stack.GetForwardingTable")
            .into_iter()
            .all(|r| r.subnet != subnet));

        let event_stream =
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
                .expect("event stream from state");
        futures::pin_mut!(event_stream);
        let mut properties = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(*id);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            event_stream.by_ref(),
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
        .expect("wait for address presence");

        // Explicitly drop the AddressStateProvider channel to cause address deletion.
        std::mem::drop(address_state_provider);

        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            event_stream.by_ref(),
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
        .expect("wait for address absence");
    }

    // Adding a valid address and detaching does not cause the address to be
    // removed.
    {
        let subnet = fidl_subnet!("2.2.2.2/32");
        let address_state_provider =
            interfaces::add_address_wait_assigned(&control, subnet, VALID_ADDRESS_PARAMETERS)
                .await
                .expect("add address failed unexpectedly");

        let () = address_state_provider
            .detach()
            .expect("FIDL error calling fuchsia.net.interfaces.admin/Control.Detach");

        std::mem::drop(address_state_provider);

        let mut properties = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(*id);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
                .expect("get interface event stream"),
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
        .expect("wait for address to not be removed");
    }
}

#[variants_test]
async fn device_control_create_interface<N: Netstack>(name: &str) {
    // NB: interface names are limited to fuchsia.net.interfaces/INTERFACE_NAME_LENGTH.
    const IF_NAME: &'static str = "ctrl_create_if";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let endpoint =
        sandbox.create_endpoint::<netemul::NetworkDevice, _>(name).await.expect("create endpoint");
    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (device, mut port_id) = endpoint.get_netdevice().await.expect("get netdevice");
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    let (control, control_server_end) =
        fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints().expect("create proxy");
    let () = device_control
        .create_interface(
            &mut port_id,
            control_server_end,
            fidl_fuchsia_net_interfaces_admin::Options {
                name: Some(IF_NAME.to_string()),
                metric: None,
                ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
            },
        )
        .expect("create interface");

    let iface_id = control.get_id().await.expect("get id");

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let interface_state = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
            .expect("create watcher event stream"),
        fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(iface_id),
    )
    .await
    .expect("get interface state");
    let properties = match interface_state {
        fidl_fuchsia_net_interfaces_ext::InterfaceState::Known(properties) => properties,
        fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id) => {
            panic!("failed to retrieve new interface with id {}", id)
        }
    };
    assert_eq!(
        properties,
        fidl_fuchsia_net_interfaces_ext::Properties {
            id: iface_id,
            name: IF_NAME.to_string(),
            device_class: fidl_fuchsia_net_interfaces::DeviceClass::Device(
                fidl_fuchsia_hardware_network::DeviceClass::Virtual
            ),
            online: false,
            // We haven't enabled the interface, it mustn't have any addresses assigned
            // to it yet.
            addresses: vec![],
            has_default_ipv4_route: false,
            has_default_ipv6_route: false
        }
    );
}

// Tests that when a DeviceControl instance is dropped, all interfaces created
// from it are dropped as well.
#[variants_test]
#[test_case(false; "no_detach")]
#[test_case(true; "detach")]
async fn device_control_owns_interfaces_lifetimes<N: Netstack>(name: &str, detach: bool) {
    let detach_str = if detach { "detach" } else { "no_detach" };
    let name = format!("{name}_{detach_str}");
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");

    // Create tun interfaces directly to attach ports to different interfaces.
    let (tun_dev, netdevice_client_end) = create_tun_device();

    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");
    let () = installer
        .install_device(netdevice_client_end, device_control_server_end)
        .expect("install device");

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let watcher = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
        .expect("create event stream")
        .map(|r| r.expect("watcher error"))
        .fuse();
    futures::pin_mut!(watcher);

    // Consume the watcher until we see the idle event.
    let existing = fidl_fuchsia_net_interfaces_ext::existing(
        watcher.by_ref().map(Result::<_, fidl::Error>::Ok),
        HashMap::new(),
    )
    .await
    .expect("existing");
    // Only loopback should exist.
    assert_eq!(existing.len(), 1, "unexpected interfaces in existing: {:?}", existing);

    const PORT_COUNT: u8 = 5;
    let mut interfaces = HashSet::new();
    let mut ports_detached_stream = futures::stream::FuturesUnordered::new();
    let mut control_proxies = Vec::new();
    // NB: For loop here is much more friendly to lifetimes than a closure
    // chain.
    for index in 1..=PORT_COUNT {
        let (iface_id, port, control) = async {
            let (port, port_server_end) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::PortMarker>()
                    .expect("create proxy");
            let () = tun_dev
                .add_port(
                    fidl_fuchsia_net_tun::DevicePortConfig {
                        base: Some(fidl_fuchsia_net_tun::BasePortConfig {
                            id: Some(index),
                            rx_types: Some(vec![
                                fidl_fuchsia_hardware_network::FrameType::Ethernet,
                            ]),
                            tx_types: Some(vec![fidl_fuchsia_hardware_network::FrameTypeSupport {
                                type_: fidl_fuchsia_hardware_network::FrameType::Ethernet,
                                features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                                supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
                            }]),
                            mtu: Some(netemul::DEFAULT_MTU.into()),
                            ..fidl_fuchsia_net_tun::BasePortConfig::EMPTY
                        }),
                        mac: Some(fidl_mac!("02:03:04:05:06:07")),
                        ..fidl_fuchsia_net_tun::DevicePortConfig::EMPTY
                    },
                    port_server_end,
                )
                .expect("add port");
            let mut port_id = {
                let (device_port, server) =
                    fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
                        .expect("create endpoints");
                let () = port.get_port(server).expect("get port");
                device_port.get_info().await.expect("get info").id.expect("missing port id")
            };

            let (control, control_server_end) =
                fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                    .expect("create proxy");

            let () = device_control
                .create_interface(
                    &mut port_id,
                    control_server_end,
                    fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
                )
                .expect("create interface");

            let iface_id = control.get_id().await.expect("get id");

            // Observe interface creation in watcher.
            let event = watcher.select_next_some().await;
            assert_matches::assert_matches!(
                event,
                fidl_fuchsia_net_interfaces::Event::Added(
                    fidl_fuchsia_net_interfaces::Properties { id: Some(id), .. }
                ) if id == iface_id
            );

            (iface_id, port, control)
        }
        .await;
        assert!(
            interfaces.insert(iface_id),
            "unexpected duplicate interface iface_id: {}, interfaces={:?}",
            iface_id,
            interfaces
        );
        // Enable the interface and wait for port to be attached.
        assert!(control.enable().await.expect("calling enable").expect("enable failed"));
        let mut port_has_session_stream = futures::stream::unfold(port, |port| {
            port.watch_state().map(move |state| {
                let fidl_fuchsia_net_tun::InternalState { mac: _, has_session, .. } =
                    state.expect("calling watch_state");
                Some((has_session.expect("has_session missing from table"), port))
            })
        });
        loop {
            if port_has_session_stream.next().await.expect("port stream ended unexpectedly") {
                break;
            }
        }
        let port_detached = port_has_session_stream
            .filter_map(move |has_session| {
                futures::future::ready((!has_session).then(move || index))
            })
            .into_future()
            .map(|(i, _stream)| i.expect("port stream ended unexpectedly"));
        let () = ports_detached_stream.push(port_detached);
        let () = control_proxies.push(control);
    }

    let mut control_wait_termination_stream = control_proxies
        .into_iter()
        .map(|control| control.wait_termination())
        .collect::<futures::stream::FuturesUnordered<_>>();

    if detach {
        // Drop detached device_control and ensure none of the futures resolve.
        let () = device_control.detach().expect("detach");
        std::mem::drop(device_control);

        let watcher_fut = watcher.next().map(|e| panic!("unexpected watcher event {:?}", e));
        let ports_fut = ports_detached_stream
            .next()
            .map(|item| panic!("session detached from port unexpectedly {:?}", item));
        let control_closed_fut = control_wait_termination_stream
            .next()
            .map(|termination| panic!("unexpected control termination event {:?}", termination));

        let ((), (), ()) = futures::future::join3(watcher_fut, ports_fut, control_closed_fut)
            .on_timeout(
                fuchsia_async::Time::after(
                    netstack_testing_common::ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT,
                ),
                || ((), (), ()),
            )
            .await;
    } else {
        // Drop device_control and wait for futures to resolve.
        std::mem::drop(device_control);

        let interfaces_removed_fut = async_utils::fold::fold_while(
            watcher,
            interfaces,
            |mut interfaces, event| match event {
                fidl_fuchsia_net_interfaces::Event::Removed(id) => {
                    assert!(interfaces.remove(&id));
                    futures::future::ready(if interfaces.is_empty() {
                        async_utils::fold::FoldWhile::Done(())
                    } else {
                        async_utils::fold::FoldWhile::Continue(interfaces)
                    })
                }
                event => panic!("unexpected event {:?}", event),
            },
        )
        .map(|fold_result| fold_result.short_circuited().expect("watcher ended"));

        let ports_are_detached_fut =
            ports_detached_stream.map(|_port_index: u8| ()).collect::<()>();
        let control_closed_fut = control_wait_termination_stream.for_each(|termination| {
            assert_matches::assert_matches!(
                termination,
                fidl_fuchsia_net_interfaces_ext::admin::TerminalError::Terminal(
                    fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::PortClosed
                )
            );
            futures::future::ready(())
        });

        let ((), (), ()) = futures::future::join3(
            interfaces_removed_fut,
            ports_are_detached_fut,
            control_closed_fut,
        )
        .await;
    }
}

#[variants_test]
#[test_case(
fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::DuplicateName;
"DuplicateName"
)]
#[test_case(
fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::PortAlreadyBound;
"PortAlreadyBound"
)]
#[test_case(fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::BadPort; "BadPort")]
#[test_case(fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::PortClosed; "PortClosed")]
#[test_case(fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::User; "User")]
async fn control_terminal_events<N: Netstack>(
    name: &str,
    reason: fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason,
) {
    let name = format!("{}_{:?}", name, reason);

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(&name).expect("create realm");

    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (tun_dev, device) = create_tun_device();

    const BASE_PORT_ID: u8 = 13;
    let base_port_config = fidl_fuchsia_net_tun::BasePortConfig {
        id: Some(BASE_PORT_ID),
        rx_types: Some(vec![fidl_fuchsia_hardware_network::FrameType::Ethernet]),
        tx_types: Some(vec![fidl_fuchsia_hardware_network::FrameTypeSupport {
            type_: fidl_fuchsia_hardware_network::FrameType::Ethernet,
            features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
            supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
        }]),
        mtu: Some(netemul::DEFAULT_MTU.into()),
        ..fidl_fuchsia_net_tun::BasePortConfig::EMPTY
    };

    let create_port = |config: fidl_fuchsia_net_tun::BasePortConfig| {
        let (port, port_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::PortMarker>()
                .expect("create proxy");
        let () = tun_dev
            .add_port(
                fidl_fuchsia_net_tun::DevicePortConfig {
                    base: Some(config),
                    mac: Some(fidl_mac!("02:aa:bb:cc:dd:ee")),
                    ..fidl_fuchsia_net_tun::DevicePortConfig::EMPTY
                },
                port_server_end,
            )
            .expect("add port");
        async move {
            // Interact with port to make sure it's installed.
            let () = port.set_online(false).await.expect("calling set_online");

            let (device_port, server) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
                    .expect("create endpoints");
            let () = port.get_port(server).expect("get port");
            let id = device_port.get_info().await.expect("get info").id.expect("missing port id");

            (port, id)
        }
    };

    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    let create_interface = |mut port_id, options| {
        let (control, control_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let () = device_control
            .create_interface(&mut port_id, control_server_end, options)
            .expect("create interface");
        control
    };

    enum KeepResource {
        Control(fidl_fuchsia_net_interfaces_ext::admin::Control),
        Port(fidl_fuchsia_net_tun::PortProxy),
    }

    let (control, _keep_alive): (_, Vec<KeepResource>) = match reason {
        fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::PortAlreadyBound => {
            let (port, port_id) = create_port(base_port_config).await;
            let control1 = {
                let control =
                    fidl_fuchsia_net_interfaces_ext::admin::Control::new(create_interface(
                        port_id.clone(),
                        fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
                    ));
                // Verify that interface was created.
                let _: u64 = control.get_id().await.expect("get id");
                control
            };

            // Create a new interface with the same port identifier.
            let control2 =
                create_interface(port_id, fidl_fuchsia_net_interfaces_admin::Options::EMPTY);
            (control2, vec![KeepResource::Control(control1), KeepResource::Port(port)])
        }
        fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::DuplicateName => {
            let (port1, port1_id) = create_port(base_port_config.clone()).await;
            let if_name = "test_same_name";
            let control1 = {
                let control =
                    fidl_fuchsia_net_interfaces_ext::admin::Control::new(create_interface(
                        port1_id,
                        fidl_fuchsia_net_interfaces_admin::Options {
                            name: Some(if_name.to_string()),
                            ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
                        },
                    ));
                // Verify that interface was created.
                let _: u64 = control.get_id().await.expect("get id");
                control
            };

            // Create a new interface with the same name.
            let (port2, port2_id) = create_port(fidl_fuchsia_net_tun::BasePortConfig {
                id: Some(BASE_PORT_ID + 1),
                ..base_port_config
            })
            .await;

            let control2 = create_interface(
                port2_id,
                fidl_fuchsia_net_interfaces_admin::Options {
                    name: Some(if_name.to_string()),
                    ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
                },
            );
            (
                control2,
                vec![
                    KeepResource::Control(control1),
                    KeepResource::Port(port1),
                    KeepResource::Port(port2),
                ],
            )
        }
        fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::BadPort => {
            let (port, port_id) = create_port(fidl_fuchsia_net_tun::BasePortConfig {
                // netdevice/client.go only accepts IP devices that support both
                // IPv4 and IPv6.
                rx_types: Some(vec![fidl_fuchsia_hardware_network::FrameType::Ipv4]),
                ..base_port_config
            })
            .await;
            let control =
                create_interface(port_id, fidl_fuchsia_net_interfaces_admin::Options::EMPTY);
            (control, vec![KeepResource::Port(port)])
        }
        fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::PortClosed => {
            // Port closed is equivalent to port doesn't exist.
            let control = create_interface(
                fidl_fuchsia_hardware_network::PortId { base: BASE_PORT_ID, salt: 0 },
                fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
            );
            (control, vec![])
        }
        fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::User => {
            let (port, port_id) = create_port(base_port_config).await;
            let control =
                create_interface(port_id, fidl_fuchsia_net_interfaces_admin::Options::EMPTY);
            let interface_id = control.get_id().await.expect("get id");
            // Setup a control handle via the debug API, and drop the original control handle.
            let debug_interfaces = realm
                .connect_to_protocol::<fnet_debug::InterfacesMarker>()
                .expect("connect to protocol");
            let (debug_control, server_end) =
                fidl::endpoints::create_proxy::<finterfaces_admin::ControlMarker>()
                    .expect("create proxy");
            debug_interfaces.get_admin(interface_id, server_end).expect("get admin failed");
            // Wait for the debug handle to be fully installed by synchronizing on `get_id`.
            assert_eq!(debug_control.get_id().await.expect("get id"), interface_id);
            (debug_control, vec![KeepResource::Port(port)])
        }
        unknown_reason => panic!("unknown reason {:?}", unknown_reason),
    };

    // Observe a terminal event and channel closure.
    let got_reason = control
        .take_event_stream()
        .map_ok(|fidl_fuchsia_net_interfaces_admin::ControlEvent::OnInterfaceRemoved { reason }| {
            reason
        })
        .try_collect::<Vec<_>>()
        .await
        .expect("waiting for terminal event");
    assert_eq!(got_reason, [reason]);
}

// Test that destroying a device causes device control instance to close.
#[variants_test]
async fn device_control_closes_on_device_close<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let endpoint =
        sandbox.create_endpoint::<netemul::NetworkDevice, _>(name).await.expect("create endpoint");

    // Create a watcher, we'll use it to ensure the Netstack didn't crash.
    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let watcher = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
        .expect("create watcher");
    futures::pin_mut!(watcher);

    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (device, mut port_id) = endpoint.get_netdevice().await.expect("get netdevice");
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    // Create an interface and get its identifier to ensure the device is
    // installed.
    let (control, control_server_end) =
        fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints().expect("create proxy");
    let () = device_control
        .create_interface(
            &mut port_id,
            control_server_end,
            fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
        )
        .expect("create interface");
    let _iface_id: u64 = control.get_id().await.expect("get id");

    // Drop the device and observe the control channel closing because the
    // device was destroyed.
    std::mem::drop(endpoint);
    assert_matches::assert_matches!(device_control.take_event_stream().next().await, None);

    // The channel could've been closed by a Netstack crash, consume from the
    // watcher to ensure that's not the case.
    let _: fidl_fuchsia_net_interfaces::Event =
        watcher.try_next().await.expect("watcher error").expect("watcher ended uexpectedly");
}

// TODO(https://fxbug.dev/110470) Remove this trait once the source of the
// timeout-induced-flake has been identified.
/// A wrapper for a [`futures::future::Future`] that panics if the future has
/// not resolved within [`ASYNC_EVENT_POSITIVE_CHECK_TIME`].
trait PanicOnTimeout: fasync::TimeoutExt {
    /// Wraps the [`futures::future::Future`] in an [`fasync::OnTimeout`] that
    /// panics if the future has not resolved within
    /// [`ASYNC_EVENT_POSITIVE_CHECK_TIME`].
    fn panic_on_timeout<S: std::fmt::Display + 'static>(
        self,
        name: S,
    ) -> fasync::OnTimeout<Self, Box<dyn FnOnce() -> Self::Output>> {
        self.on_timeout(
            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
            Box::new(move || {
                panic!("{}: Timed out after {:?}", name, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT)
            }),
        )
    }
}

impl<T: fasync::TimeoutExt> PanicOnTimeout for T {}

// Tests that interfaces created through installer have a valid datapath.
#[variants_test]
async fn installer_creates_datapath<N: Netstack, I: net_types::ip::Ip>(test_name: &str) {
    const SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.0/24");
    const ALICE_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.1/24");
    const BOB_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let network = sandbox
        .create_network("net")
        .panic_on_timeout("creating network")
        .await
        .expect("create network");

    struct RealmInfo<'a> {
        realm: netemul::TestRealm<'a>,
        endpoint: netemul::TestEndpoint<'a>,
        addr: std::net::IpAddr,
        iface_id: u64,
        device_control: fidl_fuchsia_net_interfaces_admin::DeviceControlProxy,
        control: fidl_fuchsia_net_interfaces_ext::admin::Control,
        address_state_provider:
            Option<fidl_fuchsia_net_interfaces_admin::AddressStateProviderProxy>,
    }

    let realms_stream = futures::stream::iter([("alice", ALICE_IP_V4), ("bob", BOB_IP_V4)]).then(
        |(name, ipv4_addr)| {
            let sandbox = &sandbox;
            let network = &network;
            async move {
                let test_name = format!("{}_{}", test_name, name);
                let realm =
                    sandbox.create_netstack_realm::<N, _>(test_name.clone()).expect("create realm");
                let endpoint = network
                    .create_endpoint::<netemul::NetworkDevice, _>(test_name)
                    .panic_on_timeout(format!("create {} endpoint", name))
                    .await
                    .expect("create endpoint");
                let () = endpoint
                    .set_link_up(true)
                    .panic_on_timeout(format!("set {} link up", name))
                    .await
                    .expect("set link up");
                let installer = realm
                    .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
                    .expect("connect to protocol");

                let (device, mut port_id) = endpoint
                    .get_netdevice()
                    .panic_on_timeout(format!("get {} netdevice", name))
                    .await
                    .expect("get netdevice");
                let (device_control, device_control_server_end) = fidl::endpoints::create_proxy::<
                    fidl_fuchsia_net_interfaces_admin::DeviceControlMarker,
                >()
                .expect("create proxy");
                let () = installer
                    .install_device(device, device_control_server_end)
                    .expect("install device");

                let (control, control_server_end) =
                    fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                        .expect("create proxy");
                let () = device_control
                    .create_interface(
                        &mut port_id,
                        control_server_end,
                        fidl_fuchsia_net_interfaces_admin::Options {
                            name: Some(name.to_string()),
                            metric: None,
                            ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
                        },
                    )
                    .expect("create interface");
                let iface_id = control
                    .get_id()
                    .panic_on_timeout(format!("get {} interface id", name))
                    .await
                    .expect("get id");

                let did_enable = control
                    .enable()
                    .panic_on_timeout(format!("enable {} interface", name))
                    .await
                    .expect("calling enable")
                    .expect("enable failed");
                assert!(did_enable);

                let (addr, address_state_provider) = match I::VERSION {
                    net_types::ip::IpVersion::V4 => {
                        let address_state_provider = interfaces::add_address_wait_assigned(
                            &control,
                            ipv4_addr,
                            fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                        )
                        .panic_on_timeout(format!("add {} ipv4 address", name))
                        .await
                        .expect("add address");

                        // Adding addresses through Control does not add the subnet
                        // routes.
                        let stack = realm
                            .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
                            .expect("connect to protocol");
                        let () = stack
                            .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                                subnet: SUBNET,
                                device_id: iface_id,
                                next_hop: None,
                                metric: 0,
                            })
                            .panic_on_timeout(format!("add {} route", name))
                            .await
                            .expect("send add route")
                            .expect("add route");
                        let fidl_fuchsia_net_ext::IpAddress(addr) = ipv4_addr.addr.into();
                        (addr, Some(address_state_provider))
                    }
                    net_types::ip::IpVersion::V6 => {
                        let ipv6 = netstack_testing_common::interfaces::wait_for_v6_ll(
                            &realm
                                .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
                                .expect("connect to protocol"),
                            iface_id,
                        )
                        .panic_on_timeout(format!("wait for {} ipv6 address", name))
                        .await
                        .expect("get ipv6 link local");
                        (net_types::ip::IpAddr::V6(ipv6).into(), None)
                    }
                };
                RealmInfo {
                    realm,
                    addr,
                    iface_id,
                    endpoint,
                    device_control,
                    control,
                    address_state_provider,
                }
            }
        },
    );
    futures::pin_mut!(realms_stream);

    // Can't drop any of the fields of RealmInfo to maintain objects alive.
    let RealmInfo {
        realm: alice_realm,
        endpoint: _alice_endpoint,
        addr: alice_addr,
        iface_id: alice_iface_id,
        device_control: _alice_device_control,
        control: _alice_control,
        address_state_provider: _alice_asp,
    } = realms_stream
        .next()
        .panic_on_timeout("setup alice fixture")
        .await
        .expect("create alice realm");
    let RealmInfo {
        realm: bob_realm,
        endpoint: _bob_endpoint,
        addr: bob_addr,
        iface_id: _,
        device_control: _bob_device_control,
        control: _bob_control,
        address_state_provider: _bob_asp,
    } = realms_stream
        .next()
        .panic_on_timeout("setup bob fixture")
        .await
        .expect("create alice realm");

    const PORT: u16 = 8080;
    let (bob_addr, bind_ip) = match bob_addr {
        std::net::IpAddr::V4(addr) => {
            (std::net::SocketAddrV4::new(addr, PORT).into(), std::net::Ipv4Addr::UNSPECIFIED.into())
        }
        std::net::IpAddr::V6(addr) => (
            std::net::SocketAddrV6::new(
                addr,
                PORT,
                0,
                alice_iface_id.try_into().expect("doesn't fit scope id"),
            )
            .into(),
            std::net::Ipv6Addr::UNSPECIFIED.into(),
        ),
    };
    let alice_sock = fuchsia_async::net::UdpSocket::bind_in_realm(
        &alice_realm,
        std::net::SocketAddr::new(bind_ip, 0),
    )
    .panic_on_timeout("bind alice socket")
    .await
    .expect("bind alice sock");
    let bob_sock = fuchsia_async::net::UdpSocket::bind_in_realm(
        &bob_realm,
        std::net::SocketAddr::new(bind_ip, PORT),
    )
    .panic_on_timeout("bind bob socket")
    .await
    .expect("bind bob sock");

    const PAYLOAD: &'static str = "hello bob";
    let payload_bytes = PAYLOAD.as_bytes();
    assert_eq!(
        alice_sock
            .send_to(payload_bytes, bob_addr)
            .panic_on_timeout("alice sendto bob")
            .await
            .expect("sendto"),
        payload_bytes.len()
    );

    let mut buff = [0; PAYLOAD.len() + 1];
    let (read, from) = bob_sock
        .recv_from(&mut buff[..])
        .panic_on_timeout("alice recvfrom bob")
        .await
        .expect("recvfrom");
    assert_eq!(from.ip(), alice_addr);

    assert_eq!(read, payload_bytes.len());
    assert_eq!(&buff[..read], payload_bytes);
}

#[variants_test]
async fn control_enable_disable<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let endpoint =
        sandbox.create_endpoint::<netemul::NetworkDevice, _>(name).await.expect("create endpoint");
    let () = endpoint.set_link_up(true).await.expect("set link up");
    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (device, mut port_id) = endpoint.get_netdevice().await.expect("get netdevice");
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    let (control, control_server_end) =
        fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints().expect("create proxy");

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let watcher = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
        .expect("create event stream")
        .map(|r| r.expect("watcher error"))
        .fuse();
    futures::pin_mut!(watcher);

    // Consume the watcher until we see the idle event.
    let existing = fidl_fuchsia_net_interfaces_ext::existing(
        watcher.by_ref().map(Result::<_, fidl::Error>::Ok),
        HashMap::new(),
    )
    .await
    .expect("existing");
    // Only loopback should exist.
    assert_eq!(existing.len(), 1, "unexpected interfaces in existing: {:?}", existing);

    let () = device_control
        .create_interface(
            &mut port_id,
            control_server_end,
            fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
        )
        .expect("create interface");
    let iface_id = control.get_id().await.expect("get id");

    // Expect the added event.
    let event = watcher.select_next_some().await;
    assert_matches::assert_matches!(event,
        fidl_fuchsia_net_interfaces::Event::Added(
                fidl_fuchsia_net_interfaces::Properties {
                    id: Some(id), online: Some(online), ..
                },
        ) if id == iface_id && !online
    );

    // Starts disabled, it's a no-op.
    let did_disable = control.disable().await.expect("calling disable").expect("disable failed");
    assert!(!did_disable);

    // Enable and observe online.
    let did_enable = control.enable().await.expect("calling enable").expect("enable failed");
    assert!(did_enable);
    let () = watcher
        .by_ref()
        .filter_map(|event| match event {
            fidl_fuchsia_net_interfaces::Event::Changed(
                fidl_fuchsia_net_interfaces::Properties { id: Some(id), online, .. },
            ) if id == iface_id => {
                futures::future::ready(online.and_then(|online| online.then(|| ())))
            }
            event => panic!("unexpected event {:?}", event),
        })
        .select_next_some()
        .await;

    // Enable again should be no-op.
    let did_enable = control.enable().await.expect("calling enable").expect("enable failed");
    assert!(!did_enable);

    // Disable again, expect offline.
    let did_disable = control.disable().await.expect("calling disable").expect("disable failed");
    assert!(did_disable);
    let () = watcher
        .filter_map(|event| match event {
            fidl_fuchsia_net_interfaces::Event::Changed(
                fidl_fuchsia_net_interfaces::Properties { id: Some(id), online, .. },
            ) if id == iface_id => {
                futures::future::ready(online.and_then(|online| (!online).then(|| ())))
            }
            event => panic!("unexpected event {:?}", event),
        })
        .select_next_some()
        .await;
}

#[variants_test]
async fn link_state_interface_state_interaction<N: Netstack, E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    let iface_id = interface.id();

    // Start with the interface disabled, and the link down.
    // Ethernet Devices on NS3 start enabled, otherwise disabling is a No-Op.
    let expect_disable = E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
        && N::VERSION == NetstackVersion::Netstack3;
    assert_eq!(
        expect_disable,
        interface.control().disable().await.expect("send disable").expect("disable")
    );
    interface.set_link_up(false).await.expect("bring device ");

    // Setup the interface watcher.
    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let watcher = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
        .expect("create event stream")
        .map(|r| r.expect("watcher error"))
        .fuse();
    futures::pin_mut!(watcher);
    // Consume the watcher until we see the idle event.
    let existing = fidl_fuchsia_net_interfaces_ext::existing(
        watcher.by_ref().map(Result::<_, fidl::Error>::Ok),
        HashMap::new(),
    )
    .await
    .expect("existing");
    assert_matches!(
        existing.get(&iface_id),
        Some(fidl_fuchsia_net_interfaces_ext::Properties { online: false, .. })
    );

    // Map the `watcher` to only produce `Events` when `online` changes.
    let watcher =
        watcher.filter_map(|event| match event {
            fidl_fuchsia_net_interfaces::Event::Changed(
                fidl_fuchsia_net_interfaces::Properties { id: Some(id), online, .. },
            ) if id == iface_id => futures::future::ready(online),
            event => panic!("unexpected event {:?}", event),
        });
    futures::pin_mut!(watcher);

    // Helper function that polls the watcher and panics if `online` changes.
    async fn expect_online_not_changed<S: futures::Stream<Item = bool> + std::marker::Unpin>(
        mut watcher: S,
        iface_id: u64,
    ) -> S {
        watcher
            .next()
            .map(|online: Option<bool>| match online {
                None => panic!("stream unexpectedly ended"),
                Some(online) => {
                    panic!("online unexpectedly changed to {} for {}", online, iface_id)
                }
            })
            .on_timeout(
                fuchsia_async::Time::after(
                    netstack_testing_common::ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT,
                ),
                || (),
            )
            .await;
        watcher
    }

    // Set the link up, and observe no change in interface state (because the
    // interface is still disabled).
    interface.set_link_up(true).await.expect("bring device up");
    let watcher = expect_online_not_changed(watcher, iface_id).await;
    // Set the link down, and observe no change in interface state.
    interface.set_link_up(false).await.expect("bring device down");
    let watcher = expect_online_not_changed(watcher, iface_id).await;
    // Enable the interface, and observe no change in interface state (because
    // the link is still down).
    assert!(interface.control().enable().await.expect("send enable").expect("enable"));
    let mut watcher = expect_online_not_changed(watcher, iface_id).await;
    // Set the link up, and observe the interface is online.
    interface.set_link_up(true).await.expect("bring device up");
    let online = watcher.next().await.expect("stream unexpectedly ended");
    assert!(online);
    // Set the link down and observe the interface is offline.
    interface.set_link_up(false).await.expect("bring device down");
    let online = watcher.next().await.expect("stream unexpectedly ended");
    assert!(!online);
}

#[variants_test]
// Test add/remove address and observe the events in InterfaceWatcher.
async fn control_add_remove_address<N: Netstack, E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    let expect_enable = !(E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
        && N::VERSION == NetstackVersion::Netstack3);
    assert_eq!(
        expect_enable,
        interface.control().enable().await.expect("send enable").expect("enable")
    );
    interface.set_link_up(true).await.expect("bring device up");
    let id = interface.control().get_id().await.expect("failed to get interface id");
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    async fn add_address(
        address: &mut fnet::Subnet,
        control: &fidl_fuchsia_net_interfaces_ext::admin::Control,
        id: u64,
        interface_state: &fidl_fuchsia_net_interfaces::StateProxy,
    ) -> finterfaces_admin::AddressStateProviderProxy {
        let (address_state_provider, server_end) =
            fidl::endpoints::create_proxy::<finterfaces_admin::AddressStateProviderMarker>()
                .expect("create AddressStateProvider proxy");
        control
            .add_address(
                address,
                fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                server_end,
            )
            .expect("failed to add_address");
        interfaces::wait_for_addresses(&interface_state, id, |addresses| {
            addresses
                .iter()
                .any(|&fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                    addr == *address
                })
                .then(|| ())
        })
        .await
        .expect("wait for address presence");
        address_state_provider
    }

    let addresses = [fidl_subnet!("1.1.1.1/32"), fidl_subnet!("3ffe::1/128")];
    for mut address in addresses {
        // Add an address and explicitly remove it.
        let _address_state_provider =
            add_address(&mut address, &interface.control(), id, &interface_state).await;
        let did_remove = interface
            .control()
            .remove_address(&mut address)
            .await
            .expect("failed to send remove address request")
            .expect("failed to remove address");
        assert!(did_remove);
        interfaces::wait_for_addresses(&interface_state, id, |addresses| {
            addresses
                .iter()
                .all(|&fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                    addr != address
                })
                .then(|| ())
        })
        .await
        .expect("wait for address absence");

        // Add an address and drop the AddressStateProvider handle, verifying
        // the address was removed.
        let address_state_provider =
            add_address(&mut address, &interface.control(), id, &interface_state).await;
        std::mem::drop(address_state_provider);
        interfaces::wait_for_addresses(&interface_state, id, |addresses| {
            addresses
                .iter()
                .all(|&fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                    addr != address
                })
                .then(|| ())
        })
        .await
        .expect("wait for address absence");
    }
}

#[variants_test]
#[test_case(false; "no_detach")]
#[test_case(true; "detach")]
async fn control_owns_interface_lifetime<N: Netstack>(name: &str, detach: bool) {
    let detach_str = if detach { "detach" } else { "no_detach" };
    let name = format!("{}_{}", name, detach_str);

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(&name).expect("create realm");
    let endpoint =
        sandbox.create_endpoint::<netemul::NetworkDevice, _>(&name).await.expect("create endpoint");
    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (device, mut port_id) = endpoint.get_netdevice().await.expect("get netdevice");
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    let (control, control_server_end) =
        fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints().expect("create proxy");

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let watcher = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
        .expect("create event stream")
        .map(|r| r.expect("watcher error"))
        .fuse();
    futures::pin_mut!(watcher);

    // Consume the watcher until we see the idle event.
    let existing = fidl_fuchsia_net_interfaces_ext::existing(
        watcher.by_ref().map(Result::<_, fidl::Error>::Ok),
        HashMap::new(),
    )
    .await
    .expect("existing");
    // Only loopback should exist.
    assert_eq!(existing.len(), 1, "unexpected interfaces in existing: {:?}", existing);

    let () = device_control
        .create_interface(
            &mut port_id,
            control_server_end,
            fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
        )
        .expect("create interface");
    let iface_id = control.get_id().await.expect("get id");

    // Expect the added event.
    let event = watcher.select_next_some().await;
    assert_matches::assert_matches!(event,
        fidl_fuchsia_net_interfaces::Event::Added(
                fidl_fuchsia_net_interfaces::Properties {
                    id: Some(id), ..
                },
        ) if id == iface_id
    );

    let debug = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect("connect to protocol");
    let (debug_control, control_server_end) =
        fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints().expect("create proxy");
    let () = debug.get_admin(iface_id, control_server_end).expect("get admin");
    let same_iface_id = debug_control.get_id().await.expect("get id");
    assert_eq!(same_iface_id, iface_id);

    if detach {
        let () = control.detach().expect("detach");
        // Drop control and expect the interface to NOT be removed.
        std::mem::drop(control);
        let watcher_fut =
            watcher.select_next_some().map(|event| panic!("unexpected event {:?}", event));

        let debug_control_fut = debug_control
            .wait_termination()
            .map(|event| panic!("unexpected termination {:?}", event));

        let ((), ()) = futures::future::join(watcher_fut, debug_control_fut)
            .on_timeout(
                fuchsia_async::Time::after(
                    netstack_testing_common::ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT,
                ),
                || ((), ()),
            )
            .await;
    } else {
        // Drop control and expect the interface to be removed.
        std::mem::drop(control);

        let event = watcher.select_next_some().await;
        assert_matches::assert_matches!(event,
            fidl_fuchsia_net_interfaces::Event::Removed(id) if id == iface_id
        );

        // The debug control channel is a weak ref, it didn't prevent destruction,
        // but is closed now.
        assert_matches::assert_matches!(
            debug_control.wait_termination().await,
            fidl_fuchsia_net_interfaces_ext::admin::TerminalError::Terminal(
                fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::User
            )
        );
    }
}

#[variants_test]
async fn get_set_forwarding<E: netemul::Endpoint>(name: &str) {
    // TODO(https://fxbug.dev/76987): Test against Netstack3 once get/set
    // forwarding is supported.
    type N = Netstack2;
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create netstack realm");
    let net = sandbox.create_network("net").await.expect("create network");
    let iface1 = realm.join_network::<E, _>(&net, "iface1").await.expect("create iface1");
    let iface2 = realm.join_network::<E, _>(&net, "iface2").await.expect("create iface1");

    #[derive(Debug, PartialEq)]
    struct IpForwarding {
        v4: Option<bool>,
        v4_multicast: Option<bool>,
        v6: Option<bool>,
        v6_multicast: Option<bool>,
    }

    fn ip_forwarding_for_v4_and_v6(v4: Option<bool>, v6: Option<bool>) -> IpForwarding {
        IpForwarding { v4: v4, v4_multicast: v4, v6: v6, v6_multicast: v6 }
    }

    fn ip_forwarding_for_unicast_and_multicast(
        unicast: Option<bool>,
        multicast: Option<bool>,
    ) -> IpForwarding {
        IpForwarding { v4: unicast, v4_multicast: multicast, v6: unicast, v6_multicast: multicast }
    }

    async fn get_ip_forwarding(iface: &netemul::TestInterface<'_>) -> IpForwarding {
        let finterfaces_admin::Configuration { ipv4: ipv4_config, ipv6: ipv6_config, .. } = iface
            .control()
            .get_configuration()
            .await
            .expect("get_configuration FIDL error")
            .expect("error getting configuration");

        let finterfaces_admin::Ipv4Configuration {
            forwarding: v4,
            multicast_forwarding: v4_multicast,
            ..
        } = ipv4_config.expect("IPv4 configuration should be populated");
        let finterfaces_admin::Ipv6Configuration {
            forwarding: v6,
            multicast_forwarding: v6_multicast,
            ..
        } = ipv6_config.expect("IPv6 configuration should be populated");

        IpForwarding { v4, v4_multicast, v6, v6_multicast }
    }
    // Initially, interfaces have IP forwarding disabled.
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );

    /// Sets the forwarding configuration and checks the configuration before
    /// the update was applied.
    async fn set_ip_forwarding(
        iface: &netemul::TestInterface<'_>,
        enable: IpForwarding,
        expected_previous: IpForwarding,
    ) {
        let config_with_ip_forwarding_set =
            |IpForwarding { v4, v4_multicast, v6, v6_multicast }| {
                finterfaces_admin::Configuration {
                    ipv4: Some(finterfaces_admin::Ipv4Configuration {
                        forwarding: v4,
                        multicast_forwarding: v4_multicast,
                        ..finterfaces_admin::Ipv4Configuration::EMPTY
                    }),
                    ipv6: Some(finterfaces_admin::Ipv6Configuration {
                        forwarding: v6,
                        multicast_forwarding: v6_multicast,
                        ..finterfaces_admin::Ipv6Configuration::EMPTY
                    }),
                    ..finterfaces_admin::Configuration::EMPTY
                }
            };

        let configuration = iface
            .control()
            .set_configuration(config_with_ip_forwarding_set(enable))
            .await
            .expect("set_configuration FIDL error")
            .expect("error setting configuration");

        assert_eq!(configuration, config_with_ip_forwarding_set(expected_previous))
    }

    // Set nothing.
    set_ip_forwarding(
        &iface1,
        ip_forwarding_for_v4_and_v6(None, None),
        ip_forwarding_for_v4_and_v6(None, None),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );

    // Should do nothing since the interface's IP forwarding is already
    // disabled.
    set_ip_forwarding(
        &iface1,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false)),
        ip_forwarding_for_v4_and_v6(Some(false), Some(false)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );

    // Enabling an interface's IP forwarding should not affect another
    // interface/protocol.
    set_ip_forwarding(
        &iface1,
        ip_forwarding_for_v4_and_v6(Some(true), None),
        ip_forwarding_for_v4_and_v6(Some(false), None),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(true), Some(false))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );
    set_ip_forwarding(
        &iface1,
        ip_forwarding_for_v4_and_v6(None, Some(true)),
        ip_forwarding_for_v4_and_v6(None, Some(false)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );

    // Enabling IP forwarding again should be a no-op.
    set_ip_forwarding(
        &iface1,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true)),
        ip_forwarding_for_v4_and_v6(Some(true), Some(true)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );

    // Enabling an interface's IP forwarding should not affect another
    // interface.
    set_ip_forwarding(
        &iface2,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true)),
        ip_forwarding_for_v4_and_v6(Some(false), Some(false)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true))
    );

    // Disabling an interface's IP forwarding should not affect another
    // interface/protocol.
    set_ip_forwarding(
        &iface2,
        ip_forwarding_for_v4_and_v6(Some(false), Some(true)),
        ip_forwarding_for_v4_and_v6(Some(true), Some(true)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(true))
    );

    // Disabling IP forwarding again should be a no-op if already disabled.
    set_ip_forwarding(
        &iface2,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false)),
        ip_forwarding_for_v4_and_v6(Some(false), Some(true)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface1).await,
        ip_forwarding_for_v4_and_v6(Some(true), Some(true))
    );
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_v4_and_v6(Some(false), Some(false))
    );

    // Verify that the unicast and multicast forwarding settings can be toggled
    // independently.
    set_ip_forwarding(
        &iface2,
        ip_forwarding_for_unicast_and_multicast(Some(false), Some(true)),
        ip_forwarding_for_v4_and_v6(Some(false), Some(false)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_unicast_and_multicast(Some(false), Some(true))
    );

    set_ip_forwarding(
        &iface2,
        ip_forwarding_for_unicast_and_multicast(Some(true), Some(false)),
        ip_forwarding_for_unicast_and_multicast(Some(false), Some(true)),
    )
    .await;
    assert_eq!(
        get_ip_forwarding(&iface2).await,
        ip_forwarding_for_unicast_and_multicast(Some(true), Some(false))
    );
}

// Test that reinstalling a port with the same base port identifier works.
#[variants_test]
async fn reinstall_same_port<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");

    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (tun_dev, device) = create_tun_device();

    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    const PORT_ID: u8 = 15;

    for index in 0..3 {
        let (tun_port, port_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::PortMarker>()
                .expect("create proxy");
        let () = tun_dev
            .add_port(
                fidl_fuchsia_net_tun::DevicePortConfig {
                    base: Some(fidl_fuchsia_net_tun::BasePortConfig {
                        id: Some(PORT_ID),
                        rx_types: Some(vec![fidl_fuchsia_hardware_network::FrameType::Ethernet]),
                        tx_types: Some(vec![fidl_fuchsia_hardware_network::FrameTypeSupport {
                            type_: fidl_fuchsia_hardware_network::FrameType::Ethernet,
                            features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                            supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
                        }]),
                        mtu: Some(netemul::DEFAULT_MTU.into()),
                        ..fidl_fuchsia_net_tun::BasePortConfig::EMPTY
                    }),
                    mac: Some(fidl_mac!("02:03:04:05:06:07")),
                    ..fidl_fuchsia_net_tun::DevicePortConfig::EMPTY
                },
                port_server_end,
            )
            .expect("add port");

        let (dev_port, port_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
                .expect("create proxy");

        tun_port.get_port(port_server_end).expect("get port");
        let mut port_id = dev_port.get_info().await.expect("get info").id.expect("missing port id");

        let (control, control_server_end) =
            fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                .expect("create proxy");
        let () = device_control
            .create_interface(
                &mut port_id,
                control_server_end,
                fidl_fuchsia_net_interfaces_admin::Options {
                    name: Some(format!("test{}", index)),
                    metric: None,
                    ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
                },
            )
            .expect("create interface");

        let did_enable = control.enable().await.expect("calling enable").expect("enable");
        assert!(did_enable);

        {
            // Give the stream a clone of the port proxy. We do this in a closed
            // scope to make sure we have no references to the proxy anymore
            // when we decide to drop it to delete the port.
            let attached_stream = futures::stream::unfold(tun_port.clone(), |port| async move {
                let fidl_fuchsia_net_tun::InternalState { has_session, .. } =
                    port.watch_state().await.expect("watch state");
                Some((has_session.expect("missing session information"), port))
            });
            futures::pin_mut!(attached_stream);
            attached_stream
                .by_ref()
                .filter_map(|attached| futures::future::ready(attached.then(|| ())))
                .next()
                .await
                .expect("stream ended");

            // Drop the interface control handle.
            drop(control);

            // Wait for the session to detach.
            attached_stream
                .filter_map(|attached| futures::future::ready((!attached).then(|| ())))
                .next()
                .await
                .expect("stream ended");
        }

        tun_port.remove().expect("triggered port removal");
        // Wait for the port to close, ensuring we can safely add the port again
        // with the same ID in the next iteration.
        assert_matches::assert_matches!(
            tun_port.take_event_stream().try_next().await.expect("failed to read next event"),
            None
        );
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fuchsia_async::TimeoutExt as _;
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::{
    fidl_if_addr, fidl_ip_v4, fidl_ip_v4_with_prefix, fidl_ip_v6, fidl_subnet, std_socket_addr,
};
use net_types::ip::IpAddress as _;
use netemul::RealmUdpSocket as _;
use netstack_testing_common::{
    interfaces,
    realms::{Netstack2, TestSandboxExt as _},
};
use netstack_testing_macros::variants_test;
use std::collections::{HashMap, HashSet};
use test_case::test_case;

fn create_tun_device() -> (
    fidl_fuchsia_net_tun::DeviceProxy,
    fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_network::DeviceMarker>,
) {
    let tun_ctl =
        fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_net_tun::ControlMarker>()
            .expect("connect to protocol");
    let (tun_dev, tun_dev_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DeviceMarker>()
            .expect("create proxy");
    let () = tun_ctl
        .create_device(fidl_fuchsia_net_tun::DeviceConfig::EMPTY, tun_dev_server_end)
        .expect("create tun device");
    let (netdevice_client_end, netdevice_server_end) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::DeviceMarker>()
            .expect("create endpoints");
    let () = tun_dev.get_device(netdevice_server_end).expect("get device");
    (tun_dev, netdevice_client_end)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_address_errors() {
    let name = "interfaces_admin_add_address_errors";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

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

    let (control, server) = fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
        .expect("create Control proxy");
    let () = debug_control.get_admin(*id, server).expect("get admin");

    const VALID_ADDRESS_PARAMETERS: fidl_fuchsia_net_interfaces_admin::AddressParameters =
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY;

    // Removing non-existent address.
    {
        let mut address = fidl_if_addr!("1.1.1.1/32");
        let did_remove = control
            .remove_address(&mut address)
            .await
            .expect("FIDL error calling fuchsia.net.interfaces.admin/Control.RemoveAddress")
            .expect("RemoveAddress failed");
        assert!(!did_remove);
    }

    let (control, v4_addr, v6_addr) = futures::stream::iter(addresses).fold((control, None, None), |(control, v4, v6), fidl_fuchsia_net_interfaces_ext::Address {
        addr: fidl_fuchsia_net::Subnet { addr, prefix_len },
        valid_until: _,
    }| {
        let (addr, v4, v6) = match addr {
            fidl_fuchsia_net::IpAddress::Ipv4(addr) => {
                let nt_addr = net_types::ip::Ipv4Addr::new(addr.addr);
                assert!(nt_addr.is_loopback(), "{} is not a loopback address", nt_addr);
                let addr = fidl_fuchsia_net::Ipv4AddressWithPrefix {
                    addr: *addr,
                    prefix_len: *prefix_len,
                };
                assert_eq!(v4, None, "v4 address already present, found {:?}", addr);
                (fidl_fuchsia_net::InterfaceAddress::Ipv4(addr), Some(addr), v6)
            }
            fidl_fuchsia_net::IpAddress::Ipv6(addr) => {
                let nt_addr = net_types::ip::Ipv6Addr::from_bytes(addr.addr);
                assert!(nt_addr.is_loopback(), "{} is not a loopback address", nt_addr);
                assert_eq!(v6, None, "v6 address already present, found {:?}", addr);
                (fidl_fuchsia_net::InterfaceAddress::Ipv6(*addr), v4, Some(*addr))
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
    let _: fidl_fuchsia_net::Ipv4AddressWithPrefix = v4_addr.expect("expected v4 address");
    let _: fidl_fuchsia_net::Ipv6Address = v6_addr.expect("expected v6 address");

    // Adding an invalid address returns error.
    {
        // NB: fidl_if_addr! doesn't allow invalid prefix lengths.
        let invalid_address =
            fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_fuchsia_net::Ipv4AddressWithPrefix {
                addr: fidl_ip_v4!("1.1.1.1"),
                prefix_len: 33,
            });
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
        assert_matches::assert_matches!(
            interfaces::add_address_wait_assigned(&control, fidl_if_addr!("fe80::1"), parameters,)
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
        let address_state_provider = interfaces::add_address_wait_assigned(
            &control,
            fidl_if_addr!("fe80::1"),
            VALID_ADDRESS_PARAMETERS,
        )
        .await
        .expect("add address");
        assert_matches::assert_matches!(
            address_state_provider
                .update_address_properties(fidl_fuchsia_net_interfaces_admin::AddressProperties::EMPTY)
                .await,
            Err(err) if err.is_closed()
        );
        assert_matches::assert_matches!(
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
async fn add_address_removal<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    let id = interface.id();

    let did_enable = interface.control().enable().await.expect("send enable").expect("enable");
    assert!(did_enable);
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
        let mut address = fidl_if_addr!("3.3.3.3/32");

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
        let address = fidl_if_addr!("4.4.4.4/32");

        let address_state_provider =
            interfaces::add_address_wait_assigned(&control, address, VALID_ADDRESS_PARAMETERS)
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
async fn add_address_offline<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
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
    let mut address = fidl_if_addr!("5.5.5.5/32");

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

#[fuchsia_async::run_singlethreaded(test)]
async fn add_address_success() {
    let name = "interfaces_admin_add_address_success";

    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

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
        let v4_with_prefix = fidl_ip_v4_with_prefix!("1.1.1.1/32");
        let subnet = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(v4_with_prefix.addr),
            prefix_len: v4_with_prefix.prefix_len,
        };
        let address = fidl_fuchsia_net::InterfaceAddress::Ipv4(v4_with_prefix);

        let address_state_provider =
            interfaces::add_address_wait_assigned(&control, address, VALID_ADDRESS_PARAMETERS)
                .await
                .expect("add address failed unexpectedly");

        // Ensure that no route to the subnet was added as a result of adding the address.
        assert!(stack
            .get_forwarding_table()
            .await
            .expect("FIDL error calling fuchsia.net.stack/Stack.GetForwardingTable")
            .into_iter()
            .all(|r| r.subnet != subnet));

        let (watcher, server_endpoint) =
            ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
                .expect("create watcher proxy endpoints");
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
        .expect("wait for address presence");

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
        .expect("wait for address absence");
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
        let _address_state_provider = interfaces::add_address_wait_assigned(
            &control,
            interface_addr,
            VALID_ADDRESS_PARAMETERS,
        )
        .await
        .expect("add address failed unexpectedly");

        let mut properties = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(*id);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
                .expect("create interface event stream"),
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
    }

    // Adding a valid address and detaching does not cause the address to be removed.
    {
        let addr = fidl_ip_v4_with_prefix!("2.2.2.2/32");
        let address = fidl_fuchsia_net::InterfaceAddress::Ipv4(addr);
        let subnet = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(addr.addr),
            prefix_len: addr.prefix_len,
        };

        let address_state_provider =
            interfaces::add_address_wait_assigned(&control, address, VALID_ADDRESS_PARAMETERS)
                .await
                .expect("add address failed unexpectedly");

        let () = address_state_provider
            .detach()
            .expect("FIDL error calling fuchsia.net.interfaces.admin/Control.Detach");

        std::mem::drop(address_state_provider);

        let mut properties = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(*id);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
                .expect("create interface event stream"),
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

#[fuchsia_async::run_singlethreaded(test)]
async fn device_control_create_interface() {
    const NAME: &'static str = "device_control_create_interface";
    // NB: interface names are limited to fuchsia.net.interfaces/INTERFACE_NAME_LENGTH.
    const IF_NAME: &'static str = "ctrl_create_if";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(NAME).expect("create realm");
    let endpoint =
        sandbox.create_endpoint::<netemul::NetworkDevice, _>(NAME).await.expect("create endpoint");
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
#[test_case(false; "no_detach")]
#[test_case(true; "detach")]
#[fuchsia_async::run_singlethreaded(test)]
async fn device_control_owns_interfaces_lifetimes(detach: bool) {
    let name = if detach { "detach" } else { "no_detach" };
    let name = format!("device_control_owns_interfaces_lifetimes_{}", name);
    const IP_FRAME_TYPES: [fidl_fuchsia_hardware_network::FrameType; 2] = [
        fidl_fuchsia_hardware_network::FrameType::Ipv4,
        fidl_fuchsia_hardware_network::FrameType::Ipv6,
    ];

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

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
                            rx_types: Some(IP_FRAME_TYPES.to_vec()),
                            tx_types: Some(
                                IP_FRAME_TYPES
                                    .iter()
                                    .copied()
                                    .map(|type_| fidl_fuchsia_hardware_network::FrameTypeSupport {
                                        type_,
                                        features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                                        supported_flags:
                                            fidl_fuchsia_hardware_network::TxFlags::empty(),
                                    })
                                    .collect(),
                            ),
                            mtu: Some(netemul::DEFAULT_MTU.into()),
                            ..fidl_fuchsia_net_tun::BasePortConfig::EMPTY
                        }),
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
#[fuchsia_async::run_singlethreaded(test)]
async fn control_terminal_events(
    reason: fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason,
) {
    let name = format!("control_terminal_event_{:?}", reason);

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(&name).expect("create realm");

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

            // Remove the interface using legacy API.
            let stack = realm
                .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
                .expect("connect to protocol");
            let () = stack
                .del_ethernet_interface(interface_id)
                .await
                .expect("calling del_ethernet_interface")
                .expect("del_ethernet_interface failed");

            (control, vec![KeepResource::Port(port)])
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
#[fuchsia_async::run_singlethreaded(test)]
async fn device_control_closes_on_device_close() {
    let name = "device_control_closes_on_device_close";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
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

// Tests that interfaces created through installer have a valid datapath.
#[fuchsia_async::run_singlethreaded(test)]
async fn installer_creates_datapath() {
    const SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.0/24");
    const ALICE: &'static str = "alice";
    const ALICE_IP: fidl_fuchsia_net::Ipv4AddressWithPrefix =
        fidl_ip_v4_with_prefix!("192.168.0.1/24");
    const BOB: &'static str = "bob";
    const BOB_IP: fidl_fuchsia_net::Ipv4AddressWithPrefix =
        fidl_ip_v4_with_prefix!("192.168.0.2/24");

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let network = sandbox.create_network("net").await.expect("create network");

    struct RealmInfo<'a> {
        realm: netemul::TestRealm<'a>,
        endpoint: netemul::TestEndpoint<'a>,
        device_control: fidl_fuchsia_net_interfaces_admin::DeviceControlProxy,
        control: fidl_fuchsia_net_interfaces_ext::admin::Control,
        address_state_provider: fidl_fuchsia_net_interfaces_admin::AddressStateProviderProxy,
    }

    let realms_stream =
        futures::stream::iter([(ALICE, ALICE_IP), (BOB, BOB_IP)]).then(|(name, ip)| {
            let sandbox = &sandbox;
            let network = &network;
            async move {
                let realm =
                    sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
                let endpoint = network
                    .create_endpoint::<netemul::NetworkDevice, _>(name)
                    .await
                    .expect("create endpoint");
                let () = endpoint.set_link_up(true).await.expect("set link up");
                let installer = realm
                    .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
                    .expect("connect to protocol");

                let (device, mut port_id) = endpoint.get_netdevice().await.expect("get netdevice");
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
                let iface_id = control.get_id().await.expect("get id");

                let did_enable =
                    control.enable().await.expect("calling enable").expect("enable failed");
                assert!(did_enable);

                let address_state_provider = interfaces::add_address_wait_assigned(
                    &control,
                    fidl_fuchsia_net::InterfaceAddress::Ipv4(ip),
                    fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                )
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
                    .await
                    .expect("send add route")
                    .expect("add route");

                RealmInfo { realm, endpoint, device_control, control, address_state_provider }
            }
        });
    futures::pin_mut!(realms_stream);

    // Can't drop any of the fields of RealmInfo to maintain objects alive.
    let RealmInfo {
        realm: alice_realm,
        endpoint: _alice_endpoint,
        device_control: _alice_device_control,
        control: _alice_control,
        address_state_provider: _alice_asp,
    } = realms_stream.next().await.expect("create alice realm");
    let RealmInfo {
        realm: bob_realm,
        endpoint: _bob_endpoint,
        device_control: _bob_device_control,
        control: _bob_control,
        address_state_provider: _bob_asp,
    } = realms_stream.next().await.expect("create bob realm");

    let bob_addr = std::net::SocketAddr::V4(std::net::SocketAddrV4::new(
        std::net::Ipv4Addr::from(BOB_IP.addr.addr),
        8080,
    ));
    let alice_sock =
        fuchsia_async::net::UdpSocket::bind_in_realm(&alice_realm, std_socket_addr!("0.0.0.0:0"))
            .await
            .expect("bind alice sock");
    let bob_sock = fuchsia_async::net::UdpSocket::bind_in_realm(&bob_realm, bob_addr)
        .await
        .expect("bind bob sock");

    const PAYLOAD: &'static str = "hello bob";
    let payload_bytes = PAYLOAD.as_bytes();
    assert_eq!(
        alice_sock.send_to(payload_bytes, bob_addr).await.expect("sendto"),
        payload_bytes.len()
    );

    let mut buff = [0; PAYLOAD.len() + 1];
    let (read, from) = bob_sock.recv_from(&mut buff[..]).await.expect("recvfrom");
    assert_matches::assert_matches!(from, std::net::SocketAddr::V4(addr) if addr.ip().octets() == ALICE_IP.addr.addr);
    assert_eq!(read, payload_bytes.len());
    assert_eq!(&buff[..read], payload_bytes);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn control_enable_disable() {
    let name = "control_enable_disable";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
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

#[test_case(false; "no_detach")]
#[test_case(true; "detach")]
#[fuchsia_async::run_singlethreaded(test)]
async fn control_owns_interface_lifetime(detach: bool) {
    let name = if detach { "detach" } else { "no_detach" };
    let name = format!("control_owns_interface_lifetime_{}", name);

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(&name).expect("create realm");
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

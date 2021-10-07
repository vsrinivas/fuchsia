// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fuchsia_async::TimeoutExt as _;
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::{fidl_ip_v4, fidl_ip_v4_with_prefix, fidl_ip_v6, fidl_subnet, std_socket_addr};
use net_types::ip::IpAddress as _;
use netemul::RealmUdpSocket as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_macros::variants_test;
use std::collections::{HashMap, HashSet};
use std::convert::TryInto as _;

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
    .expect("create proxy");
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

async fn add_subnet_route(
    realm: &netemul::TestRealm<'_>,
    interface: u64,
    subnet: fidl_fuchsia_net::Subnet,
) {
    let netstack = realm
        .connect_to_protocol::<fidl_fuchsia_netstack::NetstackMarker>()
        .expect("connect to protocol");
    let (route_table, route_table_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_netstack::RouteTableTransactionMarker>()
            .expect("create endpoints");
    let () = zx::Status::ok(
        netstack
            .start_route_table_transaction(route_table_server_end)
            .await
            .expect("start route table transaction"),
    )
    .expect("start route table transaction returned error");
    let () = zx::Status::ok(
        route_table
            .add_route(&mut fidl_fuchsia_netstack::RouteTableEntry {
                destination: subnet,
                gateway: None,
                nicid: interface.try_into().expect("can't convert NICID"),
                metric: 0,
            })
            .await
            .expect("add route"),
    )
    .expect("add route returned error");
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
                }), v4 + 1, v6)
            }
            fidl_fuchsia_net::IpAddress::Ipv6(addr) => {
                assert!(net_types::ip::Ipv6Addr::from_bytes(addr.addr).is_loopback());
                (fidl_fuchsia_net::InterfaceAddress::Ipv6(addr), v4, v6 + 1)
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
        .expect("add address");
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
async fn add_address_removal<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("new sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let interface = device.into_interface_in_realm(&realm).await.expect("add endpoint to Netstack");
    let id = interface.id();

    let () = interface.enable_interface().await.expect("enable interface");
    let () = interface.set_link_up(true).await.expect("bring device up");

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
            .expect("read AddressStateProvider event")
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
            .expect("read AddressStateProvider event")
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
    .expect("wait for UNAVAILABLE address assignment state");

    let () = interface.enable_interface().await.expect("enable interface");
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

    let netstack = realm
        .connect_to_protocol::<fidl_fuchsia_netstack::NetstackMarker>()
        .expect("connect to protocol");

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
        let _address_state_provider =
            add_address(&control, interface_addr, VALID_ADDRESS_PARAMETERS)
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

    let (device, _mac): (
        _,
        fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_network::MacAddressingMarker>,
    ) = endpoint.get_netdevice().await.expect("get netdevice");
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    let (control, control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create proxy");
    let () = device_control
        .create_interface(
            netemul::PORT_ID,
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
#[fuchsia_async::run_singlethreaded(test)]
async fn device_control_owns_interfaces_lifetimes() {
    let name = "device_control_owns_interfaces_lifetimes";
    const IP_FRAME_TYPES: [fidl_fuchsia_hardware_network::FrameType; 2] = [
        fidl_fuchsia_hardware_network::FrameType::Ipv4,
        fidl_fuchsia_hardware_network::FrameType::Ipv6,
    ];

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

    // Create tun interfaces directly to attach ports to different interfaces.
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
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = tun_dev.get_device(netdevice_server_end).expect("get device");
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
    let mut ports = Vec::new();
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

            let (control, control_server_end) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
                    .expect("create proxy");

            let () = device_control
                .create_interface(
                    index,
                    control_server_end,
                    fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
                )
                .expect("create interface");

            let iface_id = control.get_id().await.expect("get id");

            // Observe interface creation in watcher.
            let event = watcher.select_next_some().await;
            matches::assert_matches!(
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
        let () = ports.push(port);
        let () = control_proxies.push(control);
    }

    // Now drop the device control channel and expect all interfaces to be
    // removed.
    std::mem::drop(device_control);

    let interfaces_removed_fut =
        async_utils::fold::fold_while(watcher, interfaces, |mut interfaces, event| match event {
            fidl_fuchsia_net_interfaces::Event::Removed(id) => {
                assert!(interfaces.remove(&id));
                futures::future::ready(if interfaces.is_empty() {
                    async_utils::fold::FoldWhile::Done(())
                } else {
                    async_utils::fold::FoldWhile::Continue(interfaces)
                })
            }
            event => panic!("unexpected event {:?}", event),
        })
        .map(|fold_result| fold_result.short_circuited().expect("watcher ended"));

    let ports_are_detached_fut =
        futures::stream::iter(ports.into_iter()).for_each_concurrent(None, |port| async move {
            loop {
                let fidl_fuchsia_net_tun::InternalState { mac: _, has_session, .. } =
                    port.watch_state().await.expect("watch state");
                if !has_session.expect("has_session missing from table") {
                    break;
                }
            }
        });

    // TODO(https://fxbug.dev/81579): Once the lifetime is tied to the
    // interfaces, check that control is closed here as well.
    let control_closed_fut = futures::stream::iter(control_proxies.into_iter())
        .for_each(|_: fidl_fuchsia_net_interfaces_admin::ControlProxy| futures::future::ready(()));

    let ((), (), ()) =
        futures::future::join3(interfaces_removed_fut, ports_are_detached_fut, control_closed_fut)
            .await;
}

// Test that the same device port can't be instantiated twice.
#[fuchsia_async::run_singlethreaded(test)]
async fn device_control_enforces_single_port_instance() {
    let name = "device_control_enforces_single_port_instance";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let endpoint =
        sandbox.create_endpoint::<netemul::NetworkDevice, _>(name).await.expect("create endpoint");
    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (device, _mac) = endpoint.get_netdevice().await.expect("get netdevice");
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    {
        let (control, control_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let () = device_control
            .create_interface(
                netemul::PORT_ID,
                control_server_end,
                fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
            )
            .expect("create interface");
        // Verify that interface was created.
        let _: u64 = control.get_id().await.expect("get id");
    }

    {
        // Attempt to create a new interface with the same port identifier.
        let (control, control_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let () = device_control
            .create_interface(
                netemul::PORT_ID,
                control_server_end,
                fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
            )
            .expect("create interface");

        // Observe the control channel closing because of duplicate port
        // identifier.
        matches::assert_matches!(control.take_event_stream().next().await, None);
    }
}

// Test that destroying a device causes device control instance to close.
#[fuchsia_async::run_singlethreaded(test)]
async fn device_control_closes_on_device_close() {
    let name = "device_control_closes_on_device_close";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let endpoint =
        sandbox.create_endpoint::<netemul::NetworkDevice, _>(name).await.expect("create endpoint");
    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");

    let (device, _mac) = endpoint.get_netdevice().await.expect("get netdevice");
    let (device_control, device_control_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let () = installer.install_device(device, device_control_server_end).expect("install device");

    std::mem::drop(endpoint);
    // Observe the control channel closing because the device was destroyed.
    matches::assert_matches!(device_control.take_event_stream().next().await, None);
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
        control: fidl_fuchsia_net_interfaces_admin::ControlProxy,
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

                let (device, _mac): (
                    _,
                    fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_network::MacAddressingMarker>,
                ) = endpoint.get_netdevice().await.expect("get netdevice");
                let (device_control, device_control_server_end) = fidl::endpoints::create_proxy::<
                    fidl_fuchsia_net_interfaces_admin::DeviceControlMarker,
                >()
                .expect("create proxy");
                let () = installer
                    .install_device(device, device_control_server_end)
                    .expect("install device");

                let (control, control_server_end) = fidl::endpoints::create_proxy::<
                    fidl_fuchsia_net_interfaces_admin::ControlMarker,
                >()
                .expect("create proxy");
                let () = device_control
                    .create_interface(
                        netemul::PORT_ID,
                        control_server_end,
                        fidl_fuchsia_net_interfaces_admin::Options {
                            name: Some(name.to_string()),
                            metric: None,
                            ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
                        },
                    )
                    .expect("create interface");
                let iface_id = control.get_id().await.expect("get id");

                {
                    let stack = realm
                        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
                        .expect("connect to protocol");
                    // TODO(https://fxbug.dev/85659): Enable through Control when
                    // available.

                    let () = stack
                        .enable_interface(iface_id)
                        .await
                        .expect("enable interface")
                        .expect("failed to enable interface");
                }

                let address_state_provider = add_address(
                    &control,
                    fidl_fuchsia_net::InterfaceAddress::Ipv4(ip),
                    fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                )
                .await
                .expect("add address");

                // Adding addresses through Control does not add the subnet
                // routes.
                let () = add_subnet_route(&realm, iface_id, SUBNET).await;

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
    matches::assert_matches!(from, std::net::SocketAddr::V4(addr) if addr.ip().octets() == ALICE_IP.addr.addr);
    assert_eq!(read, payload_bytes.len());
    assert_eq!(&buff[..read], payload_bytes);
}

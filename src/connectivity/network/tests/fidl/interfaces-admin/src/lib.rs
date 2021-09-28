// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fuchsia_async::TimeoutExt as _;
use fuchsia_zircon as zx;
use futures::{StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::{fidl_ip_v4, fidl_ip_v4_with_prefix, fidl_ip_v6};
use net_types::ip::IpAddress as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_macros::variants_test;
use std::collections::HashMap;

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

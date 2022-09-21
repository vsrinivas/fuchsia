// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Context as _;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_netemul_network as fnetemul_network;
use fuchsia_async::TimeoutExt as _;
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryStreamExt as _};
use itertools::Itertools as _;
use net_declare::{fidl_ip, fidl_subnet, std_ip};
use netemul::RealmUdpSocket as _;
use netstack_testing_common::Result;
use netstack_testing_common::{
    interfaces,
    realms::{Netstack, Netstack2, NetstackVersion, TestSandboxExt as _},
};
use netstack_testing_macros::variants_test;
use std::collections::{HashMap, HashSet};
use std::convert::TryInto as _;
use test_case::test_case;

#[variants_test]
async fn watcher_existing<N: Netstack>(name: &str) {
    // We're limiting this test to mostly IPv4 because Netstack3 doesn't support
    // updates yet. We wanted the best test we could write just for the Existing
    // case since IPv6 LL addresses are subject to DAD and hard to test with
    // Existing events only.

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let stack =
        realm.connect_to_protocol::<fnet_stack::StackMarker>().expect("connect to protocol");

    #[derive(Clone, Debug, PartialEq, Eq)]
    enum Expectation {
        Loopback(u64),
        Ethernet {
            id: u64,
            name: String,
            addr: fidl_fuchsia_net::Subnet,
            has_default_ipv4_route: bool,
            has_default_ipv6_route: bool,
        },
    }

    impl PartialEq<fidl_fuchsia_net_interfaces_ext::Properties> for Expectation {
        fn eq(&self, other: &fidl_fuchsia_net_interfaces_ext::Properties) -> bool {
            match self {
                Expectation::Loopback(id) => {
                    other
                        == &fidl_fuchsia_net_interfaces_ext::Properties {
                            id: *id,
                            name: "lo".to_owned(),
                            device_class: fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                                fidl_fuchsia_net_interfaces::Empty,
                            ),
                            online: true,
                            addresses: vec![
                                fidl_fuchsia_net_interfaces_ext::Address {
                                    addr: fidl_subnet!("127.0.0.1/8"),
                                    valid_until: zx::sys::ZX_TIME_INFINITE,
                                },
                                fidl_fuchsia_net_interfaces_ext::Address {
                                    addr: fidl_subnet!("::1/128"),
                                    valid_until: zx::sys::ZX_TIME_INFINITE,
                                },
                            ],
                            has_default_ipv4_route: false,
                            has_default_ipv6_route: false,
                        }
                }
                Expectation::Ethernet {
                    id,
                    name,
                    addr,
                    has_default_ipv4_route,
                    has_default_ipv6_route,
                } => {
                    let fidl_fuchsia_net_interfaces_ext::Properties {
                        id: rhs_id,
                        name: rhs_name,
                        device_class,
                        online,
                        addresses,
                        has_default_ipv4_route: rhs_ipv4_route,
                        has_default_ipv6_route: rhs_ipv6_route,
                    } = other;

                    // We use contains here because netstack can generate
                    // link-local addresses that can't be predicted.
                    addresses.contains(&fidl_fuchsia_net_interfaces_ext::Address {
                        addr: *addr,
                        valid_until: zx::sys::ZX_TIME_INFINITE,
                    }) && *online
                        && name == rhs_name
                        && id == rhs_id
                        && has_default_ipv4_route == rhs_ipv4_route
                        && has_default_ipv6_route == rhs_ipv6_route
                        && device_class
                            == &fidl_fuchsia_net_interfaces::DeviceClass::Device(
                                fidl_fuchsia_hardware_network::DeviceClass::Virtual,
                            )
                }
            }
        }
    }

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    let mut eps = Vec::new();
    let mut expectations = HashMap::new();
    for (idx, (has_default_ipv4_route, has_default_ipv6_route)) in
        [true, false].into_iter().cartesian_product([true, false]).enumerate()
    {
        // TODO(https://fxbug.dev/88796): Use TestRealm::join_network_with
        // https://fuchsia-docs.firebaseapp.com/rust/netemul/struct.TestRealm.html#method.join_network_with
        // when `fuchsia.net.interfaces.admin` is supported.

        let if_name = format!("test-ep-{}", idx);

        let ep = sandbox
            .create_endpoint::<netemul::NetworkDevice, _>(if_name.clone())
            .await
            .expect("create endpoint");

        let iface = ep
            .into_interface_in_realm_with_name(
                &realm,
                netemul::InterfaceConfig {
                    name: Some(if_name.clone().into()),
                    ..Default::default()
                },
            )
            .await
            .expect("add device to stack");
        let id = iface.id();

        assert!(iface.control().enable().await.expect("send enable").expect("enable interface"));

        // Interface must be online for us to observe the address in the
        // assigned state later.
        let () = iface.set_link_up(true).await.expect("bring device up");

        let addr = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                addr: [192, 168, idx.try_into().unwrap(), 1],
            }),
            prefix_len: 24,
        };
        let expected = Expectation::Ethernet {
            id,
            name: if_name,
            addr,
            has_default_ipv4_route,
            has_default_ipv6_route,
        };
        assert_eq!(expectations.insert(id, expected), None);
        let address_state_provider = interfaces::add_address_wait_assigned(
            iface.control(),
            addr,
            fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
        )
        .await
        .expect("add address");
        let () = address_state_provider.detach().expect("detach address lifetime");
        eps.push(iface);

        if has_default_ipv4_route {
            stack
                .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                    subnet: fidl_subnet!("0.0.0.0/0"),
                    device_id: id,
                    next_hop: None,
                    metric: 0,
                })
                .await
                .squash_result()
                .expect("add default ipv4 route entry");
        }

        if has_default_ipv6_route {
            stack
                .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                    subnet: fidl_subnet!("::/0"),
                    device_id: id,
                    next_hop: None,
                    metric: 0,
                })
                .await
                .squash_result()
                .expect("add default ipv6 route entry");
        }
    }

    // The netstacks report the loopback interface as NIC 1.
    assert_eq!(expectations.insert(1, Expectation::Loopback(1)), None);

    let mut interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
            .expect("create event stream"),
        HashMap::new(),
    )
    .await
    .expect("fetch existing interfaces");

    for (id, expected) in expectations.iter() {
        assert_eq!(
            expected,
            interfaces.remove(id).as_ref().unwrap_or_else(|| panic!("get interface {}", id))
        );
    }

    assert_eq!(interfaces, HashMap::new());
}

#[variants_test]
async fn watcher_after_state_closed<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");

    // New scope so when we get back the WatcherProxy, the StateProxy is closed.
    let watcher = {
        let interfaces_state = realm
            .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
            .expect("connect to protocol");
        let (watcher, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
                .expect("create watcher proxy");
        let () = interfaces_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)
            .expect("get watcher");
        watcher
    };

    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher);
    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(stream, HashMap::new())
        .await
        .expect("collect interfaces");
    let expected = match N::VERSION {
        NetstackVersion::Netstack3 | NetstackVersion::Netstack2 => std::iter::once((
            1,
            fidl_fuchsia_net_interfaces_ext::Properties {
                id: 1,
                name: "lo".to_owned(),
                device_class: fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                    fidl_fuchsia_net_interfaces::Empty,
                ),
                online: true,
                addresses: vec![
                    fidl_fuchsia_net_interfaces_ext::Address {
                        addr: fidl_subnet!("127.0.0.1/8"),
                        valid_until: zx::sys::ZX_TIME_INFINITE,
                    },
                    fidl_fuchsia_net_interfaces_ext::Address {
                        addr: fidl_subnet!("::1/128"),
                        valid_until: zx::sys::ZX_TIME_INFINITE,
                    },
                ],
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
            },
        ))
        .collect(),
        NetstackVersion::ProdNetstack2 => panic!("unexpected netstack version"),
        NetstackVersion::Netstack2WithFastUdp => panic!("unexpected netstack version"),
    };
    assert_eq!(interfaces, expected);
}

/// Tests that adding an interface causes an interface changed event.
#[variants_test]
async fn test_add_remove_interface<N: Netstack, E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");

    let iface = device.into_interface_in_realm(&realm).await.expect("add device");
    let id = iface.id();

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
            .expect("create watcher");
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .expect("initialize interface watcher");

    let mut if_map = HashMap::new();
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        |if_map| if_map.contains_key(&id).then_some(()),
    )
    .await
    .expect("observe interface addition");

    let (_endpoint, _device_control) = iface.remove().await.expect("failed to remove interface");

    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        |if_map| (!if_map.contains_key(&id)).then_some(()),
    )
    .await
    .expect("observe interface addition");
}

/// Tests that if a device closes (is removed from the system), the
/// corresponding Netstack interface is deleted.
/// if `enabled` is `true`, enables the interface before closing the device.
#[variants_test]
#[test_case("disabled", false; "disabled")]
#[test_case("enabled", true ; "enabled")]
async fn test_close_interface<N: Netstack, E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    enabled: bool,
) {
    // TODO(https://fxbug.dev/102064): Remove this once Ethernet devices are
    // no longer supported.
    if E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
        && N::VERSION == NetstackVersion::Netstack3
    {
        // Netstack3 does not remove Ethernet interfaces when the device is
        // removed.
        return;
    }
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");

    let iface = device.into_interface_in_realm(&realm).await.expect("add device");
    let id = iface.id();

    if enabled {
        let did_enable = iface.control().enable().await.expect("send enable").expect("enable");
        assert!(did_enable);
    }

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
            .expect("create watcher");
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .expect("initialize interface watcher");
    let mut if_map = HashMap::new();
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        |if_map| if_map.contains_key(&id).then_some(()),
    )
    .await
    .expect("observe interface addition");

    let (_control, _device_control) = iface.remove_device();

    // Wait until we observe the removed interface is missing.
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        |if_map| (!if_map.contains_key(&id)).then_some(()),
    )
    .await
    .expect("observe interface removal");
}

/// Tests races between device link down and close.
#[variants_test]
async fn test_down_close_race<N: Netstack, E: netemul::Endpoint>(name: &str) {
    // TODO(https://fxbug.dev/102064): Remove this once Ethernet devices are
    // no longer supported.
    if E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
        && N::VERSION == NetstackVersion::Netstack3
    {
        // Netstack3 does not remove Ethernet interfaces on link down events.
        return;
    }

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create netstack realm");
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
            .expect("create watcher");
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .expect("initialize interface watcher");
    let mut if_map = HashMap::new();

    for _ in 0..10u64 {
        let dev = sandbox
            .create_endpoint::<E, _>("ep")
            .await
            .expect("create endpoint")
            .into_interface_in_realm(&realm)
            .await
            .expect("add endpoint to Netstack");

        let did_enable = dev.control().enable().await.expect("send enable").expect("enable");
        assert!(did_enable);
        let () = dev.set_link_up(true).await.expect("bring device up");

        let id = dev.id();
        // Wait until the interface is installed and the link state is up.
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                let &fidl_fuchsia_net_interfaces_ext::Properties { online, .. } =
                    if_map.get(&id)?;
                online.then_some(())
            },
        )
        .await
        .expect("observe interface online");

        // Here's where we cause the race. We bring the device's link down
        // and drop it right after; the two signals will race to reach
        // Netstack.
        let () = dev.set_link_up(false).await.expect("bring link down");
        std::mem::drop(dev);

        // Wait until the interface is removed from Netstack cleanly.
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| (!if_map.contains_key(&id)).then_some(()),
        )
        .await
        .expect("observe interface removal");
    }
}

/// Tests races between data traffic and closing a device.
#[variants_test]
async fn test_close_data_race<N: Netstack, E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let net = sandbox.create_network("net").await.expect("create network");
    let fake_ep = net.create_fake_endpoint().expect("create fake endpoint");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create netstack realm");

    // NOTE: We only run this test with IPv4 sockets since we only care about
    // exciting the tx path, the domain is irrelevant.
    const DEVICE_ADDRESS_IN_SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");
    // We're going to send data over a UDP socket to a multicast address so we
    // skip ARP resolution.
    const MCAST_ADDR: std::net::IpAddr = std_ip!("224.0.0.1");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
            .expect("create watcher");
    let () = interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .expect("initialize interface watcher");
    let mut if_map = HashMap::new();
    for _ in 0..10u64 {
        let dev = net
            .create_endpoint::<E, _>("ep")
            .await
            .expect("create endpoint")
            .into_interface_in_realm(&realm)
            .await
            .expect("add endpoint to Netstack");

        let expect_enable = !(E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap
            && N::VERSION == NetstackVersion::Netstack3);
        assert_eq!(
            expect_enable,
            dev.control().enable().await.expect("send enable").expect("enable")
        );
        let () = dev.set_link_up(true).await.expect("bring device up");

        let address_state_provider = interfaces::add_subnet_address_and_route_wait_assigned(
            &dev,
            DEVICE_ADDRESS_IN_SUBNET,
            fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
        )
        .await
        .expect("add subnet address and route");
        let () = address_state_provider.detach().expect("detach address lifetime");

        // Create a socket and start sending data on it nonstop.
        let fidl_fuchsia_net_ext::IpAddress(bind_addr) = DEVICE_ADDRESS_IN_SUBNET.addr.into();
        let sock = fuchsia_async::net::UdpSocket::bind_in_realm(
            &realm,
            std::net::SocketAddr::new(bind_addr, 0),
        )
        .await
        .expect("create socket");

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
                    .expect("send frame on fake_ep");

                // Wait on a short timer to avoid too much log noise when
                // running the test.
                let () = fuchsia_async::Timer::new(fuchsia_async::Time::after(
                    fuchsia_zircon::Duration::from_micros(10),
                ))
                .await;
            }
        };

        let id = dev.id();
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
            |if_map| (!if_map.contains_key(&id)).then_some(()),
        );

        let (io_result, iface_dropped, ()) =
            futures::future::join3(io_fut, iface_dropped, drop_fut).await;
        let () = io_result.expect("unexpected error on io future");
        let () = iface_dropped.expect("observe interface removal");
    }
}

/// Tests that toggling interface enabled repeatedly results in every change
/// in the boolean value being observable.
#[variants_test]
async fn test_watcher_online_edges<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create netstack realm");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let event_stream = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
        .expect("event stream from state")
        .map(|r| r.expect("watcher error"))
        .fuse();
    futures::pin_mut!(event_stream);

    // Consume the watcher until we see the idle event.
    let existing = fidl_fuchsia_net_interfaces_ext::existing(
        event_stream.by_ref().map(std::result::Result::<_, fidl::Error>::Ok),
        HashMap::new(),
    )
    .await
    .expect("existing");
    // Only loopback should exist.
    assert_eq!(existing.len(), 1, "unexpected interfaces in existing: {:?}", existing);

    let ep = sandbox
        // We don't need to run variants for this test, all we care about is
        // the Netstack race. Use NetworkDevice because it's lighter weight.
        .create_endpoint::<netemul::NetworkDevice, _>("ep")
        .await
        .expect("create fixed ep")
        .into_interface_in_realm(&realm)
        .await
        .expect("install in realm");
    let iface_id = ep.id();
    assert_matches::assert_matches!(
        event_stream.select_next_some().await,
        fidl_fuchsia_net_interfaces::Event::Added(fidl_fuchsia_net_interfaces::Properties {
            id: Some(id),
            online: Some(false),
            ..
        }) => id == iface_id
    );
    // NB: Need to set link up and ensure that Netstack has observed link-up
    // (by also enabling the interface and observing that online changes
    // to true); otherwise enabling/disabling the interface may not change
    // interface online as we'd expect.
    ep.set_link_up(true).await.expect("bring link up");
    assert!(ep.control().enable().await.expect("send enable").expect("enable"));
    assert_matches::assert_matches!(
        event_stream.select_next_some().await,
        fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
            id: Some(id),
            online: Some(true),
            ..
        }) => id == iface_id
    );

    // Future which concurrently enables and disables the interface a set
    // number of iterations.  Note that the raciness is intentional: the
    // interface may be enabled/disabled less than the number of iterations.
    let toggle_online_fut = {
        // Both NS2 and NS3 have event queue sizes of 128. Because events are
        // not observed (aka watched) as soon as they occur in this test, it's
        // possible to fill the event queues in the Netstack, causing it to drop
        // events. Keeping the number of iterations <= 50 prevents this (each
        // iteration yields two events: enable & disable).
        const ITERATIONS: usize = 50;
        let enable_fut = futures::stream::iter(std::iter::repeat(()).take(ITERATIONS)).fold(
            (ep, 0),
            |(ep, change_count), ()| async move {
                if ep.control().enable().await.expect("send enable").expect("enable") {
                    (ep, change_count + 1)
                } else {
                    (ep, change_count)
                }
            },
        );
        let disable_fut = {
            let debug_interfaces = realm
                .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
                .expect("connect to protocol");
            let (control, server) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
                    .expect("create Control");
            debug_interfaces.get_admin(iface_id, server).expect("send get_admin");
            futures::stream::iter(std::iter::repeat(()).take(ITERATIONS)).fold(
                0,
                move |change_count, ()| {
                    control.disable().map(move |r| {
                        change_count
                            + if r.expect("send disable").expect("disable") { 1 } else { 0 }
                    })
                },
            )
        };
        futures::future::join(enable_fut, disable_fut).map(|((ep, enable_count), disable_count)| {
            // Removes the interface.
            std::mem::drop(ep);
            (enable_count, disable_count)
        })
    };

    // Future which consumes interface watcher events and tallies number of
    // offline->online edges (and vice versa).
    let watcher_fut = event_stream
        .take_while(|e| {
            futures::future::ready(match e {
                fidl_fuchsia_net_interfaces::Event::Removed(removed_id) => *removed_id != iface_id,
                fidl_fuchsia_net_interfaces::Event::Added(_)
                | fidl_fuchsia_net_interfaces::Event::Existing(_)
                | fidl_fuchsia_net_interfaces::Event::Changed(_)
                | fidl_fuchsia_net_interfaces::Event::Idle(fidl_fuchsia_net_interfaces::Empty) => {
                    true
                }
            })
        })
        .fold((0, 0, true), |(enable_count, disable_count, online_prev), event| {
            let online_next = assert_matches::assert_matches!(
                event,
                fidl_fuchsia_net_interfaces::Event::Changed(
                    fidl_fuchsia_net_interfaces::Properties {
                        id: Some(id),
                        online,
                        ..
                    },
                ) if id == iface_id => online
            );
            futures::future::ready(match online_next {
                None => (enable_count, disable_count, online_prev),
                Some(online_next) => match (online_prev, online_next) {
                    (false, true) => (enable_count + 1, disable_count, online_next),
                    (true, false) => (enable_count, disable_count + 1, online_next),
                    (prev, next) => {
                        panic!(
                            "online changed event with no change: prev = {}, next = {}",
                            prev, next
                        )
                    }
                },
            })
        });

    let ((want_enable_count, want_disable_count), (got_enable_count, got_disable_count, online)) =
        futures::future::join(toggle_online_fut, watcher_fut).await;
    assert_eq!((got_enable_count, got_disable_count), (want_enable_count, want_disable_count));
    // Since we started with the interface being online, if we end on offline
    // then there must have been one more disable than enable.
    assert_eq!(got_disable_count, if !online { got_enable_count + 1 } else { got_enable_count });
}

/// Tests that competing interface change events are reported by
/// fuchsia.net.interfaces/Watcher in the correct order.
#[variants_test]
async fn test_watcher_race<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create netstack realm");
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
    let debug_interfaces = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect("connect to protocol");
    for _ in 0..100 {
        let (watcher, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
                .expect("create watcher");
        let () = interface_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)
            .expect("initialize interface watcher");

        let ep = sandbox
            // We don't need to run variants for this test, all we care about is
            // the Netstack race. Use NetworkDevice because it's lighter weight.
            .create_endpoint::<netemul::NetworkDevice, _>("ep")
            .await
            .expect("create fixed ep")
            .into_interface_in_realm(&realm)
            .await
            .expect("install in realm");

        const ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.1/24");
        let control_add_address = {
            let (control, server_end) =
                fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                    .expect("create endpoints");
            debug_interfaces.get_admin(ep.id(), server_end).expect("send get_admin");
            control
        };

        // Bring the link up, enable the interface, add an IP address,
        // and a default route for the address "non-sequentially" (as much
        // as possible) to cause races in Netstack when reporting events.
        let ((), (), (), ()) = futures::future::join4(
            ep.set_link_up(true).map(|r| r.expect("bring link up")),
            async {
                let did_enable = ep.control().enable().await.expect("send enable").expect("enable");
                assert!(did_enable);
            },
            async {
                let address_state_provider = interfaces::add_address_wait_assigned(
                    &control_add_address,
                    ADDR,
                    fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                )
                .await
                .expect("add address");
                let () = address_state_provider.detach().expect("detach address lifetime");
            },
            ep.add_subnet_route(fidl_subnet!("0.0.0.0/0")).map(|r| r.expect("add default route")),
        )
        .await;

        let id = ep.id();
        let () =
            futures::stream::unfold(
                (watcher, false, false, false, false),
                |(watcher, present, up, has_addr, has_default_ipv4_route)| async move {
                    let event = watcher.watch().await.expect("watch");

                    let (
                        mut new_present,
                        mut new_up,
                        mut new_has_addr,
                        mut new_has_default_ipv4_route,
                    ) = (present, up, has_addr, has_default_ipv4_route);
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
                                    .any(
                                        |fidl_fuchsia_net_interfaces::Address {
                                             addr,
                                             valid_until: _,
                                             ..
                                         }| {
                                            addr == &Some(ADDR)
                                        },
                                    );
                                new_has_default_ipv4_route = properties
                                    .has_default_ipv4_route
                                    .expect(
                                    "added/existing event missing has_default_ipv4_route property",
                                );
                            }
                        }
                        fidl_fuchsia_net_interfaces::Event::Changed(
                            fidl_fuchsia_net_interfaces::Properties {
                                id: changed_id,
                                online,
                                addresses,
                                has_default_ipv4_route,
                                name: _,
                                device_class: _,
                                has_default_ipv6_route: _,
                                ..
                            },
                        ) => {
                            if changed_id == Some(id) {
                                assert!(
                                    present,
                                    "property change event before added or existing event"
                                );
                                if let Some(online) = online {
                                    new_up = online;
                                }
                                if let Some(addresses) = addresses {
                                    new_has_addr = addresses.iter().any(
                                        |fidl_fuchsia_net_interfaces::Address {
                                             addr,
                                             valid_until: _,
                                             ..
                                         }| {
                                            addr == &Some(ADDR)
                                        },
                                    );
                                }
                                if let Some(has_default_ipv4_route) = has_default_ipv4_route {
                                    new_has_default_ipv4_route = has_default_ipv4_route;
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
                        "Observed interfaces, previous = ({}, {}, {}, {}), new = ({}, {}, {}, {})",
                        present,
                        up,
                        has_addr,
                        has_default_ipv4_route,
                        new_present,
                        new_up,
                        new_has_addr,
                        new_has_default_ipv4_route
                    );

                    // Verify that none of the observed states can be seen as
                    // "undone" by bad event ordering in Netstack. We don't care
                    // about the order in which we see the events since we're
                    // intentionally racing some things, only that nothing tracks
                    // back.

                    // Device should not disappear.
                    assert!(!present || new_present, "out of order events, device disappeared");
                    // Device should not go offline.
                    assert!(!up || new_up, "out of order events, device went offline");
                    // Address should not disappear.
                    assert!(!has_addr || new_has_addr, "out of order events, address disappeared");
                    // Default route should not disappear.
                    assert!(
                        !has_default_ipv4_route || new_has_default_ipv4_route,
                        "out of order events, default IPv4 route disappeared"
                    );
                    if new_present && new_up && new_has_addr && new_has_default_ipv4_route {
                        // We got everything we wanted, end the stream.
                        None
                    } else {
                        // Continue folding with the new state.
                        Some((
                            (),
                            (
                                watcher,
                                new_present,
                                new_up,
                                new_has_addr,
                                new_has_default_ipv4_route,
                            ),
                        ))
                    }
                },
            )
            .collect()
            .await;
    }
}

/// Test interface changes are reported through the interface watcher.
#[variants_test]
async fn test_watcher<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let stack =
        realm.connect_to_protocol::<fnet_stack::StackMarker>().expect("connect to protocol");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    let initialize_watcher = || async {
        let (watcher, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
                .expect("create watcher");
        let () = interface_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)
            .expect("initialize interface watcher");

        let event = watcher.watch().await.expect("watch");
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
            watcher.watch().await.expect("watch"),
            fidl_fuchsia_net_interfaces::Event::Idle(fidl_fuchsia_net_interfaces::Empty {})
        );
        Result::Ok(watcher)
    };

    let blocking_stream = fidl_fuchsia_net_interfaces_ext::event_stream(
        initialize_watcher().await.expect("initialize blocking watcher"),
    );
    futures::pin_mut!(blocking_stream);
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(
        initialize_watcher().await.expect("initialize watcher"),
    );
    futures::pin_mut!(stream);

    async fn assert_blocked<S>(stream: &mut S)
    where
        S: futures::stream::TryStream<Error = fidl::Error> + std::marker::Unpin,
        <S as futures::TryStream>::Ok: std::fmt::Debug,
    {
        stream
            .try_next()
            .map(|event| {
                let event = event.expect("event stream error");
                let event = event.expect("watcher event stream ended");
                Some(event)
            })
            .on_timeout(
                fuchsia_async::Time::after(fuchsia_zircon::Duration::from_millis(50)),
                || None,
            )
            .map(|e| match e {
                Some(e) => panic!("did not block but yielded {:?}", e),
                None => (),
            })
            .await
    }
    // Add an interface.
    let () = assert_blocked(&mut blocking_stream).await;
    let dev = sandbox
        .create_endpoint::<netemul::NetworkDevice, _>("ep")
        .await
        .expect("create endpoint")
        .into_interface_in_realm(&realm)
        .await
        .expect("add endpoint to Netstack");
    let () = dev.set_link_up(true).await.expect("bring device up");
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
    async fn next<S>(stream: &mut S) -> fidl_fuchsia_net_interfaces::Event
    where
        S: futures::stream::TryStream<Ok = fidl_fuchsia_net_interfaces::Event, Error = fidl::Error>
            + Unpin,
    {
        stream.try_next().await.expect("stream error").expect("watcher event stream ended")
    }
    assert_eq!(next(&mut blocking_stream).await, want);
    assert_eq!(next(&mut stream).await, want);

    // Set the link to up.
    let () = assert_blocked(&mut blocking_stream).await;
    let did_enable = dev.control().enable().await.expect("send enable").expect("enable");
    assert!(did_enable);
    const LL_ADDR_COUNT: usize = 1;
    // NB The following fold function is necessary because IPv6 link-local
    // addresses are configured when the interface is brought up such that the
    // ordering or the number of events that reports the changes in the online
    // and addresses properties cannot be guaranteed. As such, we only assert
    // that:
    // 1. the online property changes exactly once to `true`,
    // 2. all addresses added are IPv6 and eventually reach the expected count, and
    // 3. no other properties change.
    //
    // It would be ideal to disable IPv6 LL address configuration for this
    // test, which would simplify this significantly.
    let fold_fn = |(online_changed, addresses): (bool, _), event| {
        let (online, got_addrs) = assert_matches::assert_matches!(
            event,
            fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
                id: Some(got_id),
                online,
                addresses,
                name: None,
                device_class: None,
                has_default_ipv4_route: None,
                has_default_ipv6_route: None,
                ..
            }) if got_id == id => (online, addresses)
        );
        let online_changed = online.map_or(online_changed, |got_online| {
            assert!(
                !online_changed,
                "duplicate online property change to new value of {}",
                got_online
            );
            assert!(got_online, "got offline, expected online");
            true
        });
        let addresses = got_addrs.map_or(addresses, |got_addrs| {
            got_addrs
                .iter()
                .map(|addr| {
                    let addr = fidl_fuchsia_net_interfaces_ext::Address::try_from(addr.clone())
                        .expect("failed to validate address");
                    assert_matches::assert_matches!(
                        addr,
                        fidl_fuchsia_net_interfaces_ext::Address {
                            addr: fidl_fuchsia_net::Subnet {
                                addr: fidl_fuchsia_net::IpAddress::Ipv6(
                                    fidl_fuchsia_net::Ipv6Address { .. }
                                ),
                                prefix_len: _,
                            },
                            valid_until: _,
                        }
                    );
                    addr
                })
                .collect::<HashSet<_>>()
        });
        futures::future::ready(if addresses.len() == LL_ADDR_COUNT && online_changed {
            async_utils::fold::FoldWhile::Done(addresses)
        } else {
            async_utils::fold::FoldWhile::Continue((online_changed, addresses))
        })
    };
    let ll_addrs = assert_matches::assert_matches!(
        async_utils::fold::fold_while(
            blocking_stream.by_ref().map(|r| r.expect("blocking event stream error")),
            (false, HashSet::new()),
            fold_fn,
        ).await,
        async_utils::fold::FoldResult::ShortCircuited(addresses) => addresses
    );
    {
        let addrs = assert_matches::assert_matches!(
            async_utils::fold::fold_while(
                stream.by_ref().map(|r| r.expect("non-blocking event stream error")),
                (false, HashSet::new()),
                fold_fn,
            ).await,
            async_utils::fold::FoldResult::ShortCircuited(addresses) => addresses
        );
        assert_eq!(ll_addrs, addrs);
    }

    // Add an address and subnet route.
    let () = assert_blocked(&mut blocking_stream).await;
    let subnet = fidl_subnet!("192.168.0.1/16");
    let _address_state_provider = interfaces::add_subnet_address_and_route_wait_assigned(
        &dev,
        subnet,
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
    )
    .await
    .expect("add subnet address and route");
    let addresses_changed = |event| {
        assert_matches::assert_matches!(
            event,
            fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
                id: Some(event_id),
                addresses: Some(addresses),
                name: None,
                device_class: None,
                online: None,
                has_default_ipv4_route: None,
                has_default_ipv6_route: None,
                ..
            }) if event_id == id => {
                addresses
                    .iter()
                    .map(|addr| {
                        fidl_fuchsia_net_interfaces_ext::Address::try_from(addr.clone())
                            .expect("failed to validate address")
                    })
                    .collect::<HashSet<_>>()
            }
        )
    };
    let want = ll_addrs
        .iter()
        .cloned()
        .chain(std::iter::once(fidl_fuchsia_net_interfaces_ext::Address {
            addr: subnet,
            valid_until: zx::sys::ZX_TIME_INFINITE,
        }))
        .collect();
    assert_eq!(addresses_changed(next(&mut blocking_stream).await), want);
    assert_eq!(addresses_changed(next(&mut stream).await), want);

    // Add a default route.
    let () = assert_blocked(&mut blocking_stream).await;
    let mut default_v4_entry = fnet_stack::ForwardingEntry {
        subnet: fidl_subnet!("0.0.0.0/0"),
        device_id: 0,
        next_hop: Some(Box::new(fidl_ip!("192.168.255.254"))),
        metric: 0,
    };
    let () = stack
        .add_forwarding_entry(&mut default_v4_entry)
        .await
        .squash_result()
        .expect("add default route");
    let want =
        fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
            id: Some(id),
            has_default_ipv4_route: Some(true),
            ..fidl_fuchsia_net_interfaces::Properties::EMPTY
        });
    assert_eq!(next(&mut blocking_stream).await, want);
    assert_eq!(next(&mut stream).await, want);

    // Remove the default route.
    let () = assert_blocked(&mut blocking_stream).await;
    let () = stack
        .del_forwarding_entry(&mut default_v4_entry)
        .await
        .squash_result()
        .expect("delete default route");
    let want =
        fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
            id: Some(id),
            has_default_ipv4_route: Some(false),
            ..fidl_fuchsia_net_interfaces::Properties::EMPTY
        });
    assert_eq!(next(&mut blocking_stream).await, want);
    assert_eq!(next(&mut stream).await, want);

    // Remove the added address.
    let () = assert_blocked(&mut blocking_stream).await;
    let was_removed = interfaces::remove_subnet_address_and_route(&dev, subnet)
        .await
        .expect("remove subnet address and route");
    assert!(was_removed);
    assert_eq!(addresses_changed(next(&mut blocking_stream).await), ll_addrs);
    assert_eq!(addresses_changed(next(&mut stream).await), ll_addrs);

    // Set the link to down.
    {
        let () = assert_blocked(&mut blocking_stream).await;
        let () = dev.set_link_up(false).await.expect("bring device up");
        let fold_fn = |(online_changed, addresses_empty): (bool, bool), event| {
            let (online, addresses) = assert_matches::assert_matches!(
                event,
                fidl_fuchsia_net_interfaces::Event::Changed(fidl_fuchsia_net_interfaces::Properties {
                    id: Some(got_id),
                    online,
                    addresses,
                    name: None,
                    device_class: None,
                    has_default_ipv4_route: None,
                    has_default_ipv6_route: None,
                    ..
                }) if got_id == id => (online, addresses)
            );
            let online_changed = online.map_or(online_changed, |online| {
                assert!(
                    !online_changed,
                    "duplicate online property change to new value of {}",
                    online
                );
                assert!(!online, "got online changed to true, want false");
                true
            });
            let addresses_empty = addresses.map_or(addresses_empty, |addresses| {
                assert!(
                    !addresses_empty,
                    "duplicate addresses property change to new value of {:?}",
                    addresses
                );
                assert_eq!(addresses[..], []);
                true
            });
            futures::future::ready(if addresses_empty && online_changed {
                async_utils::fold::FoldWhile::Done(())
            } else {
                async_utils::fold::FoldWhile::Continue((online_changed, addresses_empty))
            })
        };
        assert_eq!(
            async_utils::fold::fold_while(
                blocking_stream.by_ref().map(|r| r.expect("blocking event stream error")),
                (false, false),
                fold_fn,
            )
            .await,
            async_utils::fold::FoldResult::ShortCircuited(()),
        );
        assert_eq!(
            async_utils::fold::fold_while(
                stream.by_ref().map(|r| r.expect("non-blocking event stream error")),
                (false, false),
                fold_fn,
            )
            .await,
            async_utils::fold::FoldResult::ShortCircuited(()),
        );
    }

    // Remove the ethernet interface.
    let () = assert_blocked(&mut blocking_stream).await;
    std::mem::drop(dev);
    let want = fidl_fuchsia_net_interfaces::Event::Removed(id);
    assert_eq!(next(&mut blocking_stream).await, want);
    assert_eq!(next(&mut stream).await, want);
}

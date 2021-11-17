// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Context as _;
use fidl_fuchsia_net_stack as net_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fuchsia_async::TimeoutExt as _;
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryStreamExt as _};
use itertools::Itertools as _;
use net_declare::{fidl_ip, fidl_subnet, std_ip};
use netemul::RealmUdpSocket as _;
use netstack_testing_common::realms::{Netstack, Netstack2, NetstackVersion, TestSandboxExt as _};
use netstack_testing_common::Result;
use netstack_testing_macros::variants_test;
use std::collections::{HashMap, HashSet};
use std::convert::TryInto as _;

#[variants_test]
async fn watcher_existing<N: Netstack>(name: &str) {
    // We're limiting this test to mostly IPv4 because Netstack3 doesn't support
    // updates yet. We wanted the best test we could write just for the Existing
    // case since IPv6 LL addresses are subject to DAD and hard to test with
    // Existing events only.

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol");

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Expectation {
        Loopback(u64),
        Ethernet {
            id: u64,
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
                    addr,
                    has_default_ipv4_route,
                    has_default_ipv6_route,
                } => {
                    let fidl_fuchsia_net_interfaces_ext::Properties {
                        id: rhs_id,
                        // TODO(https://fxbug.dev/84516): Not comparing name
                        // because ns3 doesn't generate names yet.
                        name: _,
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
                        && id == rhs_id
                        && has_default_ipv4_route == rhs_ipv4_route
                        && has_default_ipv6_route == rhs_ipv6_route
                        && device_class
                            == &fidl_fuchsia_net_interfaces::DeviceClass::Device(
                                fidl_fuchsia_hardware_network::DeviceClass::Ethernet,
                            )
                }
            }
        }
    }

    let mut eps = Vec::new();
    let mut expectations = HashMap::new();
    for (idx, (has_default_ipv4_route, has_default_ipv6_route)) in
        IntoIterator::into_iter([true, false]).cartesian_product([true, false]).enumerate()
    {
        // TODO(https://fxbug.dev/75553): Use TestRealm::join_network_with
        // https://fuchsia-docs.firebaseapp.com/rust/netemul/struct.TestRealm.html#method.join_network_with
        // when `Changed` events are supported.
        let ep = sandbox
            .create_endpoint::<netemul::Ethernet, _>(format!("test-ep-{}", idx))
            .await
            .expect("create endpoint");

        let id = ep.add_to_stack(&realm).await.expect("add device to stack");

        // Netstack3 doesn't allow addresses to be added while link is down.
        let () = stack.enable_interface(id).await.squash_result().expect("enable interface");
        let () = ep.set_link_up(true).await.expect("bring device up");
        loop {
            // TODO(https://fxbug.dev/75553): Remove usage of get_interface_info.
            let info = stack.get_interface_info(id).await.squash_result().expect("get interface");
            if info.properties.physical_status == net_stack::PhysicalStatus::Up {
                break;
            }
        }
        eps.push(ep);

        let mut addr = fidl_fuchsia_net::Subnet {
            addr: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                addr: [192, 168, idx.try_into().unwrap(), 1],
            }),
            prefix_len: 24,
        };
        let expected =
            Expectation::Ethernet { id, addr, has_default_ipv4_route, has_default_ipv6_route };
        assert_eq!(expectations.insert(id, expected), None);

        let () = stack
            .add_interface_address(id, &mut addr)
            .await
            .squash_result()
            .expect("add interface address");

        if has_default_ipv4_route {
            stack
                .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                    subnet: fidl_subnet!("0.0.0.0/0"),
                    destination: fidl_fuchsia_net_stack::ForwardingDestination::DeviceId(id),
                })
                .await
                .squash_result()
                .expect("add default ipv4 route entry");
        }

        if has_default_ipv6_route {
            stack
                .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                    subnet: fidl_subnet!("::/0"),
                    destination: fidl_fuchsia_net_stack::ForwardingDestination::DeviceId(id),
                })
                .await
                .squash_result()
                .expect("add default ipv6 route entry");
        }
    }

    // Netstack2 reports the loopback interface as NIC 1.
    if N::VERSION == NetstackVersion::Netstack2 {
        assert_eq!(expectations.insert(1, Expectation::Loopback(1)), None);
    }

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");
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
    // TODO(https://fxbug.dev/72378): N3 doesn't support loopback devices yet.
    let expected = match N::VERSION {
        NetstackVersion::Netstack2 => std::iter::once((
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
        NetstackVersion::Netstack3 => HashMap::new(),
    };
    assert_eq!(interfaces, expected);
}

/// Tests that adding an interface causes an interface changed event.
#[variants_test]
async fn test_add_remove_interface<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");

    let id = device.add_to_stack(&realm).await.expect("add device");

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
        // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
        |if_map| if_map.contains_key(&id).then(|| ()),
    )
    .await
    .expect("observe interface addition");

    let () = stack.del_ethernet_interface(id).await.squash_result().expect("delete device");

    let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut if_map,
        // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
        |if_map| (!if_map.contains_key(&id)).then(|| ()),
    )
    .await
    .expect("observe interface addition");
}

/// Tests that if a device closes (is removed from the system), the
/// corresponding Netstack interface is deleted.
/// if `enabled` is `true`, enables the interface before closing the device.
async fn test_close_interface<E: netemul::Endpoint>(enabled: bool, name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");
    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol");
    let device = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");

    let id = device.add_to_stack(&realm).await.expect("add device");

    if enabled {
        let () = stack.enable_interface(id).await.squash_result().expect("enable interface");
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
        // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
        |if_map| if_map.contains_key(&id).then(|| ()),
    )
    .await
    .expect("observe interface addition");

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
    .expect("observe interface removal");
}

#[variants_test]
async fn test_close_disabled_interface<E: netemul::Endpoint>(name: &str) {
    test_close_interface::<E>(false, name).await
}

#[variants_test]
async fn test_close_enabled_interface<E: netemul::Endpoint>(name: &str) {
    test_close_interface::<E>(true, name).await
}

/// Tests races between device link down and close.
#[variants_test]
async fn test_down_close_race<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create netstack realm");
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

        let () = dev.enable_interface().await.expect("enable interface");
        let () = dev.start_dhcp().await.expect("start DHCP");
        let () = dev.set_link_up(true).await.expect("bring device up");

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
            |if_map| {
                // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
                (!if_map.contains_key(&id)).then(|| ())
            },
        )
        .await
        .expect("observe interface removal");
    }
}

/// Tests races between data traffic and closing a device.
#[variants_test]
async fn test_close_data_race<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let net = sandbox.create_network("net").await.expect("create network");
    let fake_ep = net.create_fake_endpoint().expect("create fake endpoint");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create netstack realm");

    // NOTE: We only run this test with IPv4 sockets since we only care about
    // exciting the tx path, the domain is irrelevant.
    const DEVICE_ADDRESS: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");
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

        let () = dev.enable_interface().await.expect("enable interface");
        let () = dev.set_link_up(true).await.expect("bring device up");
        let () = dev.add_ip_addr(DEVICE_ADDRESS).await.expect("add address");

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
        .expect("observe interface online");
        // Create a socket and start sending data on it nonstop.
        let fidl_fuchsia_net_ext::IpAddress(bind_addr) = DEVICE_ADDRESS.addr.into();
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
        let () = io_result.expect("unexpected error on io future");
        let () = iface_dropped.expect("observe interface removal");
    }
}

/// Tests that competing interface change events are reported by
/// fuchsia.net.interfaces/Watcher in the correct order.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_watcher_race() {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>("interfaces_watcher_race")
        .expect("create netstack realm");
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
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

        // Bring the link up, enable the interface, and add an IP address
        // "non-sequentially" (as much as possible) to cause races in Netstack
        // when reporting events.
        let ((), (), ()) = futures::future::join3(
            ep.set_link_up(true).map(|r| r.expect("bring link up")),
            ep.enable_interface().map(|r| r.expect("enable interface")),
            ep.add_ip_addr(ADDR).map(|r| r.expect("add address")),
        )
        .await;

        let id = ep.id();
        let () = futures::stream::unfold(
            (watcher, false, false, false),
            |(watcher, present, up, has_addr)| async move {
                let event = watcher.watch().await.expect("watch");

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
                if new_present && new_up && new_has_addr {
                    // We got everything we wanted, end the stream.
                    None
                } else {
                    // Continue folding with the new state.
                    Some(((), (watcher, new_present, new_up, new_has_addr)))
                }
            },
        )
        .collect()
        .await;
    }
}

/// Test interface changes are reported through the interface watcher.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_watcher() {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm =
        sandbox.create_netstack_realm::<Netstack2, _>("test_watcher").expect("create realm");
    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol");

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

    let blocking_watcher = initialize_watcher().await.expect("initialize blocking watcher");
    let blocking_stream = fidl_fuchsia_net_interfaces_ext::event_stream(blocking_watcher.clone());
    pin_utils::pin_mut!(blocking_stream);
    let watcher = initialize_watcher().await.expect("initialize watcher");
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone());
    pin_utils::pin_mut!(stream);

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
    let () = dev.enable_interface().await.expect("bring device up");
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
                        panic!("duplicate online property change to new value of {}", got_online,);
                    }
                    if got_online != want_online {
                        panic!("got online: {}, want {}", got_online, want_online);
                    }
                    online_changed = true;
                }
                if let Some(got_addrs) = got_addrs {
                    if !online_changed {
                        panic!(
                            "addresses changed before online property change, addresses: {:?}",
                            got_addrs
                        );
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
                        return futures::future::ready(async_utils::fold::FoldWhile::Done(
                            got_addrs,
                        ));
                    }
                    addresses = Some(got_addrs);
                }
                futures::future::ready(async_utils::fold::FoldWhile::Continue((
                    online_changed,
                    addresses,
                )))
            }
            event => {
                panic!("got: {:?}, want online and/or IPv6 link-local address change event", event)
            }
        }
    };
    const LL_ADDR_COUNT: usize = 1;
    let want_online = true;
    let ll_addrs = async_utils::fold::fold_while(
        blocking_stream.map(|r| r.expect("blocking event stream error")),
        (false, None),
        fold_fn(want_online, LL_ADDR_COUNT),
    )
        .await.short_circuited().unwrap_or_else(|(online_changed, addresses)| {
        panic!(
            "event stream ended unexpectedly while waiting for interface online = {} and LL addr count = {}, final state online_changed = {} addresses = {:?}",
            want_online, LL_ADDR_COUNT,
            online_changed,
            addresses
        )
    });

    let addrs = async_utils::fold::fold_while(
        stream.map(|r| r.expect("non-blocking event stream error")),
        (false, None),
        fold_fn(true, LL_ADDR_COUNT),
    )
        .await.short_circuited().unwrap_or_else(|(online_changed, addresses)| {
        panic!(
            "event stream ended unexpectedly while waiting for interface online = {} and LL addr count = {}, final state online_changed = {} addresses = {:?}",
            want_online, LL_ADDR_COUNT,
            online_changed,
            addresses
        )});
    assert_eq!(ll_addrs, addrs);
    let blocking_stream = fidl_fuchsia_net_interfaces_ext::event_stream(blocking_watcher.clone());
    pin_utils::pin_mut!(blocking_stream);
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone());
    pin_utils::pin_mut!(stream);

    // Add an address.
    let () = assert_blocked(&mut blocking_stream).await;
    let mut subnet = fidl_subnet!("192.168.0.1/16");
    let () =
        stack.add_interface_address(id, &mut subnet).await.squash_result().expect("add address");
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
        }) if event_id == id => addresses
            .iter()
            .filter_map(|&fidl_fuchsia_net_interfaces::Address { addr, valid_until, .. }| {
                assert_eq!(valid_until, Some(fuchsia_zircon::sys::ZX_TIME_INFINITE));
                addr
            })
            .collect::<HashSet<_>>(),
        event => panic!("got: {:?}, want changed event with added IPv4 address", event),
    };
    let want = ll_addrs.iter().cloned().chain(std::iter::once(subnet)).collect();
    assert_eq!(addresses_changed(next(&mut blocking_stream).await), want);
    assert_eq!(addresses_changed(next(&mut stream).await), want);

    // Add a default route.
    let () = assert_blocked(&mut blocking_stream).await;
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
        .del_forwarding_entry(&mut default_v4_subnet)
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
    let () =
        stack.del_interface_address(id, &mut subnet).await.squash_result().expect("add address");
    assert_eq!(addresses_changed(next(&mut blocking_stream).await), ll_addrs);
    assert_eq!(addresses_changed(next(&mut stream).await), ll_addrs);

    // Set the link to down.
    let () = assert_blocked(&mut blocking_stream).await;
    let () = dev.set_link_up(false).await.expect("bring device up");
    const LL_ADDR_COUNT_AFTER_LINK_DOWN: usize = 0;
    let want_online = false;
    let addresses = async_utils::fold::fold_while(
        blocking_stream.map(|r| r.expect("blocking event stream error")),
        (false, None),
        fold_fn(want_online, LL_ADDR_COUNT_AFTER_LINK_DOWN),
    )
        .await.short_circuited().unwrap_or_else(|(online_changed, addresses)| {
        panic!(
            "event stream ended unexpectedly while waiting for interface online = {} and LL addr count = {}, final state online_changed = {} addresses = {:?}",
            want_online, LL_ADDR_COUNT_AFTER_LINK_DOWN,
            online_changed,
            addresses
        )
    });
    assert!(addresses.is_subset(&ll_addrs), "got {:?}, want a subset of {:?}", addresses, ll_addrs);
    assert_eq!(
        async_utils::fold::fold_while(
            stream.map(|r| r.expect("non-blocking event stream error")),
            (false, None),
            fold_fn(false, LL_ADDR_COUNT_AFTER_LINK_DOWN),
        )
        .await,
        async_utils::fold::FoldResult::ShortCircuited(addresses),
    );
    let blocking_stream = fidl_fuchsia_net_interfaces_ext::event_stream(blocking_watcher);
    pin_utils::pin_mut!(blocking_stream);
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher);
    pin_utils::pin_mut!(stream);

    // Remove the ethernet interface.
    let () = assert_blocked(&mut blocking_stream).await;
    std::mem::drop(dev);
    let want = fidl_fuchsia_net_interfaces::Event::Removed(id);
    assert_eq!(next(&mut blocking_stream).await, want);
    assert_eq!(next(&mut stream).await, want);
}

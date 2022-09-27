// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

pub mod virtualization;

use std::collections::HashMap;

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcp as fnet_dhcp;
use fidl_fuchsia_net_ext::IntoExt as _;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_netemul_network as fnetemul_network;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::{
    future::{FutureExt as _, TryFutureExt as _},
    stream::{self, StreamExt as _},
};
use net_declare::fidl_ip_v4;
use net_types::ip as net_types_ip;
use netstack_testing_common::{
    interfaces,
    realms::{
        KnownServiceProvider, Manager, ManagerConfig, Netstack2, TestRealmExt as _, TestSandboxExt,
    },
    try_all, try_any, wait_for_component_stopped, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;

/// Test that NetCfg discovers a newly added device and it adds the device
/// to the Netstack.
#[variants_test]
async fn test_oir<E: netemul::Endpoint, M: Manager>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager {
                    agent: M::MANAGEMENT_AGENT,
                    use_dhcp_server: false,
                    config: ManagerConfig::Empty,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("create netstack realm");

    // Add a device to the realm.
    let endpoint = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let () = endpoint.set_link_up(true).await.expect("set link up");
    let endpoint_mount_path = E::dev_path("ep");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm.add_virtual_device(&endpoint, endpoint_mount_path).await.unwrap_or_else(|e| {
        panic!("add virtual device {}: {:?}", endpoint_mount_path.display(), e)
    });

    // Make sure the Netstack got the new device added.
    let interface_state = realm
        .connect_to_protocol::<fnet_interfaces::StateMarker>()
        .expect("connect to fuchsia.net.interfaces/State service");
    let wait_for_netmgr =
        wait_for_component_stopped(&realm, M::MANAGEMENT_AGENT.get_component_name(), None).fuse();
    futures::pin_mut!(wait_for_netmgr);
    let _: (u64, String) = interfaces::wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        None,
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .expect("wait for non loopback interface");

    // Wait for orderly shutdown of the test realm to complete before allowing
    // test interfaces to be cleaned up.
    //
    // This is necessary to prevent test interfaces from being removed while
    // NetCfg is still in the process of configuring them after adding them to
    // the Netstack, which causes spurious errors.
    realm.shutdown().await.expect("failed to shutdown realm");
}

/// Tests that stable interface name conflicts are handled gracefully.
#[variants_test]
async fn test_oir_interface_name_conflict<E: netemul::Endpoint, M: Manager>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager {
                    agent: M::MANAGEMENT_AGENT,
                    use_dhcp_server: false,
                    config: ManagerConfig::Empty,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("create netstack realm");

    let wait_for_netmgr =
        wait_for_component_stopped(&realm, M::MANAGEMENT_AGENT.get_component_name(), None);

    let netstack = realm
        .connect_to_protocol::<fnetstack::NetstackMarker>()
        .expect("connect to netstack service");

    let interface_state = realm
        .connect_to_protocol::<fnet_interfaces::StateMarker>()
        .expect("connect to fuchsia.net.interfaces/State service");
    let interfaces_stream =
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("interface event stream")
            .map(|r| r.expect("watcher error"))
            .filter_map(|event| {
                futures::future::ready(match event {
                    fidl_fuchsia_net_interfaces::Event::Added(
                        fidl_fuchsia_net_interfaces::Properties { id, name, .. },
                    )
                    | fidl_fuchsia_net_interfaces::Event::Existing(
                        fidl_fuchsia_net_interfaces::Properties { id, name, .. },
                    ) => Some((
                        id.expect("missing interface ID"),
                        name.expect("missing interface name"),
                    )),
                    fidl_fuchsia_net_interfaces::Event::Removed(id) => {
                        let _: u64 = id;
                        None
                    }
                    fidl_fuchsia_net_interfaces::Event::Idle(
                        fidl_fuchsia_net_interfaces::Empty {},
                    )
                    | fidl_fuchsia_net_interfaces::Event::Changed(
                        fidl_fuchsia_net_interfaces::Properties { .. },
                    ) => None,
                })
            });
    let interfaces_stream = futures::stream::select(
        interfaces_stream,
        futures::stream::once(wait_for_netmgr.map(|r| panic!("network manager exited {:?}", r))),
    )
    .fuse();
    futures::pin_mut!(interfaces_stream);
    // Observe the initially existing loopback interface.
    let _: (u64, String) = interfaces_stream.select_next_some().await;

    // Add a device to the realm and wait for it to be added to the netstack.
    //
    // Non PCI and USB devices get their interface names from their MAC addresses.
    // Using the same MAC address for different devices will result in the same
    // interface name.
    let mac = || Some(Box::new(fnet::MacAddress { octets: [2, 3, 4, 5, 6, 7] }));
    let ethx7 = sandbox
        .create_endpoint_with(
            "ep1",
            fnetemul_network::EndpointConfig { mtu: 1500, mac: mac(), backing: E::NETEMUL_BACKING },
        )
        .await
        .expect("create ethx7");
    let endpoint_mount_path = E::dev_path("ep1");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm.add_virtual_device(&ethx7, endpoint_mount_path).await.unwrap_or_else(|e| {
        panic!("add virtual device1 {}: {:?}", endpoint_mount_path.display(), e)
    });

    let (id_ethx7, name_ethx7) = interfaces_stream.select_next_some().await;
    assert_eq!(
        &name_ethx7, "ethx7",
        "first interface should use a stable name based on its MAC address"
    );

    // Create an interface that the network manager does not know about that will cause a
    // name conflict with the first temporary name.
    let etht0 =
        sandbox.create_endpoint::<netemul::Ethernet, _>("etht0").await.expect("create eth0");
    let name = "etht0";
    let netstack_id_etht0 = netstack
        .add_ethernet_device(
            name,
            &mut fnetstack::InterfaceConfig {
                name: name.to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
            },
            etht0
                .get_ethernet()
                .await
                .expect("netstack.add_ethernet_device requires an Ethernet endpoint"),
        )
        .await
        .expect("add_ethernet_device FIDL error")
        .map_err(zx::Status::from_raw)
        .expect("add_ethernet_device error");

    let (id_etht0, name_etht0) = interfaces_stream.select_next_some().await;
    assert_eq!(id_etht0, u64::from(netstack_id_etht0));
    assert_eq!(&name_etht0, "etht0");

    // Add another device from the network manager with the same MAC address and wait for it
    // to be added to the netstack. Its first two attempts at adding a name should conflict
    // with the above two devices.
    let etht1 = sandbox
        .create_endpoint_with(
            "ep2",
            fnetemul_network::EndpointConfig { mtu: 1500, mac: mac(), backing: E::NETEMUL_BACKING },
        )
        .await
        .expect("create etht1");
    let endpoint_mount_path = E::dev_path("ep2");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm.add_virtual_device(&etht1, endpoint_mount_path).await.unwrap_or_else(|e| {
        panic!("add virtual device2 {}: {:?}", endpoint_mount_path.display(), e)
    });
    let (id_etht1, name_etht1) = interfaces_stream.select_next_some().await;
    assert_ne!(id_ethx7, id_etht1, "interface IDs should be different");
    assert_ne!(id_etht0, id_etht1, "interface IDs should be different");
    assert_eq!(
        &name_etht1, "etht1",
        "second interface from network manager should use a temporary name"
    );

    // Wait for orderly shutdown of the test realm to complete before allowing
    // test interfaces to be cleaned up.
    //
    // This is necessary to prevent test interfaces from being removed while
    // NetCfg is still in the process of configuring them after adding them to
    // the Netstack, which causes spurious errors.
    let () = realm.shutdown().await.expect("failed to shutdown realm");
}

/// Make sure the DHCP server is configured to start serving requests when NetCfg discovers
/// a WLAN AP interface and stops serving when the interface is removed.
///
/// Also make sure that a new WLAN AP interface may be added after a previous interface has been
/// removed from the netstack.
#[variants_test]
async fn test_wlan_ap_dhcp_server<E: netemul::Endpoint, M: Manager>(name: &str) {
    // Use a large timeout to check for resolution.
    //
    // These values effectively result in a large timeout of 60s which should avoid
    // flakes. This test was run locally 100 times without flakes.
    /// Duration to sleep between polls.
    const POLL_WAIT: zx::Duration = zx::Duration::from_seconds(1);
    /// Maximum number of times we'll poll the DHCP server to check its parameters.
    const RETRY_COUNT: u64 = 120;

    /// Check if the DHCP server is started.
    async fn check_dhcp_status(dhcp_server: &fnet_dhcp::Server_Proxy, started: bool) {
        for _ in 0..RETRY_COUNT {
            let () = fuchsia_async::Timer::new(POLL_WAIT.after_now()).await;

            if started == dhcp_server.is_serving().await.expect("query server status request") {
                return;
            }
        }

        panic!("timed out checking DHCP server status");
    }

    /// Make sure the DHCP server is configured to start serving requests when NetCfg discovers
    /// a WLAN AP interface and stops serving when the interface is removed.
    ///
    /// When `wlan_ap_dhcp_server_inner` returns successfully, the interface that it creates will
    /// have been removed.
    async fn wlan_ap_dhcp_server_inner<'a, E: netemul::Endpoint>(
        sandbox: &'a netemul::TestSandbox,
        realm: &netemul::TestRealm<'a>,
        offset: u8,
    ) {
        // These constants are all hard coded in NetCfg for the WLAN AP interface and
        // the DHCP server.
        const DHCP_LEASE_TIME: u32 = 24 * 60 * 60; // 1 day in seconds.
        const NETWORK_ADDR: fnet::Ipv4Address = fidl_ip_v4!("192.168.255.248");
        const NETWORK_PREFIX_LEN: u8 = 29;
        const INTERFACE_ADDR: fnet::Ipv4Address = fidl_ip_v4!("192.168.255.249");
        const DHCP_POOL_START_ADDR: fnet::Ipv4Address = fidl_ip_v4!("192.168.255.250");
        const DHCP_POOL_END_ADDR: fnet::Ipv4Address = fidl_ip_v4!("192.168.255.254");
        const NETWORK_ADDR_SUBNET: net_types_ip::Subnet<net_types_ip::Ipv4Addr> = unsafe {
            net_types_ip::Subnet::new_unchecked(
                net_types_ip::Ipv4Addr::new(NETWORK_ADDR.addr),
                NETWORK_PREFIX_LEN,
            )
        };

        // Add a device to the realm that looks like a WLAN AP from the
        // perspective of NetCfg. The topological path for the interface must
        // include "wlanif-ap" as that is how NetCfg identifies a WLAN AP
        // interface.
        let network = sandbox
            .create_network(format!("dhcp-server-{}", offset))
            .await
            .expect("create network");
        let wlan_ap = network
            .create_endpoint::<E, _>(format!("wlanif-ap-dhcp-server-{}", offset))
            .await
            .expect("create wlan ap");
        let path = E::dev_path(&format!("dhcp-server-ep-{}", offset));
        let () = realm
            .add_virtual_device(&wlan_ap, path.as_path())
            .await
            .unwrap_or_else(|e| panic!("add WLAN AP virtual device {}: {:?}", path.display(), e));
        let () = wlan_ap.set_link_up(true).await.expect("set wlan ap link up");

        // Make sure the WLAN AP interface is added to the Netstack and is brought up with
        // the right IP address.
        let interface_state = realm
            .connect_to_protocol::<fnet_interfaces::StateMarker>()
            .expect("connect to fuchsia.net.interfaces/State service");
        let (watcher, watcher_server) =
            ::fidl::endpoints::create_proxy::<fnet_interfaces::WatcherMarker>()
                .expect("create proxy");
        let () = interface_state
            .get_watcher(fnet_interfaces::WatcherOptions::EMPTY, watcher_server)
            .expect("failed to initialize interface watcher");
        let mut if_map = HashMap::new();
        let (wlan_ap_id, wlan_ap_name) = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                if_map.iter().find_map(
                    |(
                        id,
                        fidl_fuchsia_net_interfaces_ext::Properties {
                            name, online, addresses, ..
                        },
                    )| {
                        (*online
                            && addresses.iter().any(
                                |&fidl_fuchsia_net_interfaces_ext::Address {
                                     addr: fnet::Subnet { addr, prefix_len: _ },
                                     valid_until: _,
                                 }| {
                                    addr == INTERFACE_ADDR.into_ext()
                                },
                            ))
                        .then(|| (*id, name.clone()))
                    },
                )
            },
        )
        .map_err(anyhow::Error::from)
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!("timed out"))
        })
        .await
        .expect("failed to wait for presence of a WLAN AP interface");

        // Check the DHCP server's configured parameters.
        let dhcp_server = realm
            .connect_to_protocol::<fnet_dhcp::Server_Marker>()
            .expect("connect to DHCP server service");
        let checks = [
            (
                fnet_dhcp::ParameterName::IpAddrs,
                fnet_dhcp::Parameter::IpAddrs(vec![INTERFACE_ADDR]),
            ),
            (
                fnet_dhcp::ParameterName::LeaseLength,
                fnet_dhcp::Parameter::Lease(fnet_dhcp::LeaseLength {
                    default: Some(DHCP_LEASE_TIME),
                    max: Some(DHCP_LEASE_TIME),
                    ..fnet_dhcp::LeaseLength::EMPTY
                }),
            ),
            (
                fnet_dhcp::ParameterName::BoundDeviceNames,
                fnet_dhcp::Parameter::BoundDeviceNames(vec![wlan_ap_name]),
            ),
            (
                fnet_dhcp::ParameterName::AddressPool,
                fnet_dhcp::Parameter::AddressPool(fnet_dhcp::AddressPool {
                    prefix_length: Some(NETWORK_PREFIX_LEN),
                    range_start: Some(DHCP_POOL_START_ADDR),
                    range_stop: Some(DHCP_POOL_END_ADDR),
                    ..fnet_dhcp::AddressPool::EMPTY
                }),
            ),
        ];

        let dhcp_server_ref = &dhcp_server;
        let checks_ref = &checks;
        if !try_any(stream::iter(0..RETRY_COUNT).then(|i| async move {
            let () = fuchsia_async::Timer::new(POLL_WAIT.after_now()).await;
            try_all(stream::iter(checks_ref.iter()).then(|(param_name, param_value)| async move {
                Ok(dhcp_server_ref
                    .get_parameter(*param_name)
                    .await
                    .unwrap_or_else(|e| panic!("get {:?} parameter request: {:?}", param_name, e))
                    .unwrap_or_else(|e| {
                        panic!(
                            "error getting {:?} parameter: {}",
                            param_name,
                            zx::Status::from_raw(e)
                        )
                    })
                    == *param_value)
            }))
            .await
            .with_context(|| format!("{}-th iteration checking DHCP parameters", i))
        }))
        .await
        .expect("checking DHCP parameters")
        {
            // Too many retries.
            panic!("timed out waiting for DHCP server configurations");
        }

        // The DHCP server should be started.
        let () = check_dhcp_status(&dhcp_server, true).await;

        // Add a host endpoint to the network. It should be configured by the DHCP server.
        let host = network
            .create_endpoint::<E, _>(format!("host-dhcp-client-{}", offset))
            .await
            .expect("create host");
        let path = E::dev_path(&format!("dhcp-client-ep-{}", offset));
        let () = realm
            .add_virtual_device(&host, path.as_path())
            .await
            .unwrap_or_else(|e| panic!("add host virtual device {}: {:?}", path.display(), e));
        let () = host.set_link_up(true).await.expect("set host link up");
        let host_id = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                if_map.iter().find_map(
                    |(
                        id,
                        fidl_fuchsia_net_interfaces_ext::Properties { online, addresses, .. },
                    )| {
                        (*id != wlan_ap_id
                            && *online
                            && addresses.iter().any(
                                |&fidl_fuchsia_net_interfaces_ext::Address {
                                     addr: fnet::Subnet { addr, prefix_len: _ },
                                     valid_until: _,
                                 }| match addr {
                                    fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => {
                                        NETWORK_ADDR_SUBNET
                                            .contains(&net_types_ip::Ipv4Addr::new(addr))
                                    }
                                    fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: _ }) => false,
                                },
                            ))
                        .then_some(*id)
                    },
                )
            },
        )
        .map_err(anyhow::Error::from)
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!("timed out"))
        })
        .await
        .expect("wait for host interface to be configured");

        // Take the interface down, the DHCP server should be stopped.
        let () = wlan_ap.set_link_up(false).await.expect("set wlan ap link down");
        let () = check_dhcp_status(&dhcp_server, false).await;

        // Bring the interface back up, the DHCP server should be started.
        let () = wlan_ap.set_link_up(true).await.expect("set wlan ap link up");
        let () = check_dhcp_status(&dhcp_server, true).await;
        // Remove the interface, the DHCP server should be stopped.
        drop(wlan_ap);
        let () = check_dhcp_status(&dhcp_server, false).await;

        // Remove the host endpoint from the network and wait for the interface
        // to be removed from the netstack.
        //
        // This is necessary to ensure this function can be called multiple
        // times and observe DHCP address acquisition on a new interface each
        // time.
        drop(host);
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| (!if_map.contains_key(&host_id)).then_some(()),
        )
        .await
        .expect("wait for host interface to be removed");
    }

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager {
                    agent: M::MANAGEMENT_AGENT,
                    use_dhcp_server: true,
                    config: ManagerConfig::Empty,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::FakeClock,
                KnownServiceProvider::SecureStash,
            ],
        )
        .expect("create netstack realm");
    let wait_for_netmgr =
        wait_for_component_stopped(&realm, M::MANAGEMENT_AGENT.get_component_name(), None).fuse();
    futures::pin_mut!(wait_for_netmgr);

    // Add a WLAN AP, make sure the DHCP server gets configured and starts or
    // stops when the interface is added and brought up or brought down/removed.
    // A loop is used to emulate interface flaps.
    for i in 0..=1 {
        let test_fut = wlan_ap_dhcp_server_inner::<E>(&sandbox, &realm, i).fuse();
        futures::pin_mut!(test_fut);
        let () = futures::select! {
            () = test_fut => {},
            stopped_event = wait_for_netmgr => {
                panic!(
                    "NetCfg unexpectedly exited with exit status = {:?}",
                    stopped_event
                );
            }
        };
    }
}

/// Tests that netcfg observes component stop events and exits cleanly.
#[variants_test]
async fn observes_stop_events<M: Manager>(name: &str) {
    use component_events::events::{self, Event as _};

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager {
                    agent: M::MANAGEMENT_AGENT,
                    use_dhcp_server: false,
                    config: ManagerConfig::Empty,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("create netstack realm");
    let event_source = events::EventSource::new().expect("create event source");
    let mut event_stream = event_source
        .subscribe(vec![events::EventSubscription::new(vec![
            events::Started::NAME,
            events::Stopped::NAME,
        ])])
        .await
        .expect("subscribe to events");

    let event_matcher = netstack_testing_common::get_child_component_event_matcher(
        &realm,
        M::MANAGEMENT_AGENT.get_component_name(),
    )
    .await
    .expect("get child moniker");

    // Wait for netcfg to start.
    let events::StartedPayload {} = event_matcher
        .clone()
        .wait::<events::Started>(&mut event_stream)
        .await
        .expect("got started event")
        .result()
        .expect("error event on started");

    let () = realm.shutdown().await.expect("shutdown");

    let event =
        event_matcher.wait::<events::Stopped>(&mut event_stream).await.expect("got stopped event");
    // NB: event::result below borrows from event, it needs to be in a different
    // statement.
    let events::StoppedPayload { status } = event.result().expect("error event on stopped");
    assert_matches::assert_matches!(status, events::ExitStatus::Clean);
}

/// Test that NetCfg enables forwarding on interfaces when the device class is configured to have
/// that enabled.
#[variants_test]
async fn test_forwarding<E: netemul::Endpoint, M: Manager>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager {
                    agent: M::MANAGEMENT_AGENT,
                    use_dhcp_server: false,
                    config: ManagerConfig::Forwarding,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("create netstack realm");

    // Add a device to the realm.
    let endpoint = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let () = endpoint.set_link_up(true).await.expect("set link up");
    let endpoint_mount_path = E::dev_path("ep");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm.add_virtual_device(&endpoint, endpoint_mount_path).await.unwrap_or_else(|e| {
        panic!("add virtual device {}: {:?}", endpoint_mount_path.display(), e)
    });

    // Make sure the Netstack got the new device added.
    let interface_state = realm
        .connect_to_protocol::<fnet_interfaces::StateMarker>()
        .expect("connect to fuchsia.net.interfaces/State service");
    let wait_for_netmgr =
        wait_for_component_stopped(&realm, M::MANAGEMENT_AGENT.get_component_name(), None).fuse();
    futures::pin_mut!(wait_for_netmgr);
    let (id, _name) = interfaces::wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        None,
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .expect("wait for non loopback interface");

    let control = realm
        .interface_control(id)
        .expect("connect to fuchsia.net.interfaces.admin/Control for new interface");
    let fnet_interfaces_admin::Configuration { ipv4: ipv4_config, ipv6: ipv6_config, .. } = control
        .get_configuration()
        .await
        .expect("get_configuration FIDL error")
        .expect("to get configuration");

    let fnet_interfaces_admin::Ipv4Configuration { forwarding: v4, .. } =
        ipv4_config.expect("to have a v4 config");
    let fnet_interfaces_admin::Ipv6Configuration { forwarding: v6, .. } =
        ipv6_config.expect("to have a v6 config");

    // The configuration installs forwarding on v4 on Virtual interfaces and v6 on Ethernet. We
    // should only observe the configuration to be installed on v4 because the device installed by
    // this test doesn't match the Ethernet device class.
    assert_eq!(v4, Some(true));
    assert_eq!(v6, Some(false));

    // Wait for orderly shutdown of the test realm to complete before allowing
    // test interfaces to be cleaned up.
    //
    // This is necessary to prevent test interfaces from being removed while
    // NetCfg is still in the process of configuring them after adding them to
    // the Netstack, which causes spurious errors.
    realm.shutdown().await.expect("failed to shutdown realm");
}

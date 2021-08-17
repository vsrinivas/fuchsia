// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::collections::HashMap;

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_dhcp as dhcp;
use fidl_fuchsia_net_ext::IntoExt as _;
use fidl_fuchsia_net_interfaces as net_interfaces;
use fidl_fuchsia_netemul_network as netemul_network;
use fidl_fuchsia_netstack as netstack;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{FutureExt as _, TryFutureExt as _};
use futures::stream::{self, StreamExt as _};
use net_declare::fidl_ip_v4;
use net_types::ip as net_types_ip;
use netstack_testing_common::realms::{
    constants, KnownServiceProvider, Manager, Netstack2, TestSandboxExt as _,
};
use netstack_testing_common::{
    try_all, try_any, wait_for_component_stopped, wait_for_non_loopback_interface_up, Result,
    ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;

/// Test that NetCfg discovers a newly added device and it adds the device
/// to the Netstack.
#[variants_test]
async fn test_oir<E: netemul::Endpoint, M: Manager>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("create sandbox")?;
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager(M::MANAGEMENT_AGENT),
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::Dhcpv6Client,
                KnownServiceProvider::SecureStash,
            ],
        )
        .context("create netstack realm")?;

    // Add a device to the realm.
    let endpoint = sandbox.create_endpoint::<E, _>(name).await.context("create endpoint")?;
    let () = endpoint.set_link_up(true).await.context("set link up")?;
    let endpoint_mount_path = E::dev_path("ep");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm
        .add_virtual_device(&endpoint, endpoint_mount_path)
        .await
        .with_context(|| format!("add virtual device {}", endpoint_mount_path.display()))?;

    // Make sure the Netstack got the new device added.
    let interface_state = realm
        .connect_to_protocol::<net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let wait_for_netmgr =
        wait_for_component_stopped(&realm, constants::netcfg::COMPONENT_NAME, None).fuse();
    futures::pin_mut!(wait_for_netmgr);
    let _: (u64, String) = wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        None,
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .context("wait for non loopback interface")?;

    realm
        .remove_virtual_device(endpoint_mount_path)
        .await
        .with_context(|| format!("remove virtual device {}", endpoint_mount_path.display()))
}

/// Tests that stable interface name conflicts are handled gracefully.
#[variants_test]
async fn test_oir_interface_name_conflict<E: netemul::Endpoint, M: Manager>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("create sandbox")?;
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager(M::MANAGEMENT_AGENT),
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::Dhcpv6Client,
                KnownServiceProvider::SecureStash,
            ],
        )
        .context("create netstack realm")?;

    let wait_for_netmgr =
        wait_for_component_stopped(&realm, constants::netcfg::COMPONENT_NAME, None).fuse();
    futures::pin_mut!(wait_for_netmgr);
    let netstack = realm
        .connect_to_protocol::<netstack::NetstackMarker>()
        .context("connect to netstack service")?;

    // Add a device to the realm and wait for it to be added to the netstack.
    //
    // Non PCI and USB devices get their interface names from their MAC addresses.
    // Using the same MAC address for different devices will result in the same
    // interface name.
    let mac = || Some(Box::new(net::MacAddress { octets: [2, 3, 4, 5, 6, 7] }));
    let ethx7 = sandbox
        .create_endpoint_with(
            "ep1",
            netemul_network::EndpointConfig { mtu: 1500, mac: mac(), backing: E::NETEMUL_BACKING },
        )
        .await
        .context("create ethx7")?;
    let () = ethx7.set_link_up(true).await.context("set link up")?;
    let endpoint_mount_path = E::dev_path("ep1");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm
        .add_virtual_device(&ethx7, endpoint_mount_path)
        .await
        .with_context(|| format!("add virtual device1 {}", endpoint_mount_path.display()))?;
    let interface_state = realm
        .connect_to_protocol::<net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let (id_ethx7, name_ethx7) = wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        None,
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .context("wait for first non loopback interface")?;
    assert_eq!(
        &name_ethx7, "ethx7",
        "first interface should use a stable name based on its MAC address"
    );

    // Create an interface that the network manager does not know about that will cause a
    // name conflict with the first temporary name.
    let etht0 =
        sandbox.create_endpoint::<netemul::Ethernet, _>("etht0").await.context("create eth0")?;
    let () = etht0.set_link_up(true).await.context("set link up")?;
    let name = "etht0";
    let netstack_id_etht0 = netstack
        .add_ethernet_device(
            name,
            &mut netstack::InterfaceConfig {
                name: name.to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
            },
            etht0
                .get_ethernet()
                .await
                .context("netstack.add_ethernet_device requires an Ethernet endpoint")?,
        )
        .await
        .context("add_ethernet_device FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_ethernet_device error")?;
    let () = netstack
        .set_interface_status(netstack_id_etht0, true /* enabled */)
        .context("set interface status FIDL error")?;
    let (id_etht0, name_etht0) = wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        Some(&std::iter::once(id_ethx7).collect()),
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .context("wait for second non loopback interface")?;
    assert_eq!(id_etht0, u64::from(netstack_id_etht0));
    assert_eq!(&name_etht0, "etht0");

    // Add another device from the network manager with the same MAC address and wait for it
    // to be added to the netstack. Its first two attempts at adding a name should conflict
    // with the above two devices.
    let etht1 = sandbox
        .create_endpoint_with(
            "ep2",
            netemul_network::EndpointConfig { mtu: 1500, mac: mac(), backing: E::NETEMUL_BACKING },
        )
        .await
        .context("create etht1")?;
    let () = etht1.set_link_up(true).await.context("set link up")?;
    let endpoint_mount_path = E::dev_path("ep2");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm
        .add_virtual_device(&etht1, endpoint_mount_path)
        .await
        .with_context(|| format!("add virtual device2 {}", endpoint_mount_path.display()))?;
    let (id_etht1, name_etht1) = wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        Some(&vec![id_ethx7, id_etht0].into_iter().collect()),
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .context("wait for third non loopback interface")?;
    assert_ne!(id_ethx7, id_etht1, "interface IDs should be different");
    assert_ne!(id_etht0, id_etht1, "interface IDs should be different");
    assert_eq!(
        &name_etht1, "etht1",
        "second interface from network manager should use a temporary name"
    );
    Ok(())
}

/// Make sure the DHCP server is configured to start serving requests when NetCfg discovers
/// a WLAN AP interface and stops serving when the interface is removed.
///
/// Also make sure that a new WLAN AP interface may be added after a previous interface has been
/// removed from the netstack.
#[variants_test]
async fn test_wlan_ap_dhcp_server<E: netemul::Endpoint, M: Manager>(name: &str) -> Result {
    // Use a large timeout to check for resolution.
    //
    // These values effectively result in a large timeout of 60s which should avoid
    // flakes. This test was run locally 100 times without flakes.
    /// Duration to sleep between polls.
    const POLL_WAIT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(1);
    /// Maximum number of times we'll poll the DHCP server to check its parameters.
    const RETRY_COUNT: u64 = 120;

    /// Check if the DHCP server is started.
    async fn check_dhcp_status(dhcp_server: &dhcp::Server_Proxy, started: bool) -> Result {
        for _ in 0..RETRY_COUNT {
            let () = fuchsia_async::Timer::new(POLL_WAIT.after_now()).await;

            if started == dhcp_server.is_serving().await.context("query server status request")? {
                return Ok(());
            }
        }

        Err(anyhow::anyhow!("timed out checking DHCP server status"))
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
    ) -> Result {
        // These constants are all hard coded in NetCfg for the WLAN AP interface and
        // the DHCP server.
        const DHCP_LEASE_TIME: u32 = 24 * 60 * 60; // 1 day in seconds.
        const NETWORK_ADDR: net::Ipv4Address = fidl_ip_v4!("192.168.255.248");
        const NETWORK_PREFIX_LEN: u8 = 29;
        const INTERFACE_ADDR: net::Ipv4Address = fidl_ip_v4!("192.168.255.249");
        const DHCP_POOL_START_ADDR: net::Ipv4Address = fidl_ip_v4!("192.168.255.250");
        const DHCP_POOL_END_ADDR: net::Ipv4Address = fidl_ip_v4!("192.168.255.254");
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
            .context("create network")?;
        let wlan_ap = network
            .create_endpoint::<E, _>(format!("wlanif-ap-dhcp-server-{}", offset))
            .await
            .context("create wlan ap")?;
        let path = E::dev_path(&format!("dhcp-server-ep-{}", offset));
        let () = realm
            .add_virtual_device(&wlan_ap, path.as_path())
            .await
            .with_context(|| format!("add WLAN AP virtual device {}", path.display()))?;
        let () = wlan_ap.set_link_up(true).await.context("set wlan ap link up")?;

        // Make sure the WLAN AP interface is added to the Netstack and is brought up with
        // the right IP address.
        let interface_state = realm
            .connect_to_protocol::<net_interfaces::StateMarker>()
            .context("connect to fuchsia.net.interfaces/State service")?;
        let (watcher, watcher_server) =
            ::fidl::endpoints::create_proxy::<net_interfaces::WatcherMarker>()?;
        let () = interface_state
            .get_watcher(net_interfaces::WatcherOptions::EMPTY, watcher_server)
            .context("failed to initialize interface watcher")?;
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
                                     addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
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
        .context("failed to wait for presence of a WLAN AP interface")?;

        // Check the DHCP server's configured parameters.
        let dhcp_server = realm
            .connect_to_protocol::<dhcp::Server_Marker>()
            .context("connect to DHCP server service")?;
        let checks = [
            (dhcp::ParameterName::IpAddrs, dhcp::Parameter::IpAddrs(vec![INTERFACE_ADDR])),
            (
                dhcp::ParameterName::LeaseLength,
                dhcp::Parameter::Lease(dhcp::LeaseLength {
                    default: Some(DHCP_LEASE_TIME),
                    max: Some(DHCP_LEASE_TIME),
                    ..dhcp::LeaseLength::EMPTY
                }),
            ),
            (
                dhcp::ParameterName::BoundDeviceNames,
                dhcp::Parameter::BoundDeviceNames(vec![wlan_ap_name]),
            ),
            (
                dhcp::ParameterName::AddressPool,
                dhcp::Parameter::AddressPool(dhcp::AddressPool {
                    prefix_length: Some(NETWORK_PREFIX_LEN),
                    range_start: Some(DHCP_POOL_START_ADDR),
                    range_stop: Some(DHCP_POOL_END_ADDR),
                    ..dhcp::AddressPool::EMPTY
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
                    .with_context(|| format!("get {:?} paramter request", param_name))?
                    .map_err(zx::Status::from_raw)
                    .with_context(|| format!("error getting {:?} paramter", param_name))?
                    == *param_value)
            }))
            .await
            .with_context(|| format!("{}-th iteration checking DHCP parameters", i))
        }))
        .await
        .context("checking DHCP parameters")?
        {
            // Too many retries.
            return Err(anyhow::anyhow!("timed out waiting for DHCP server configurations"));
        }

        // The DHCP server should be started.
        let () = check_dhcp_status(&dhcp_server, true)
            .await
            .context("check DHCP server started after interface added")?;

        // Add a host endpoint to the network. It should be configured by the DHCP server.
        let host = network
            .create_endpoint::<E, _>(format!("host-dhcp-client-{}", offset))
            .await
            .context("create host")?;
        let path = E::dev_path(&format!("dhcp-client-ep-{}", offset));
        let () = realm
            .add_virtual_device(&host, path.as_path())
            .await
            .with_context(|| format!("add host virtual device {}", path.display()))?;
        let () = host.set_link_up(true).await.context("set host link up")?;
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                if_map.iter().find_map(
                    |(
                        id,
                        fidl_fuchsia_net_interfaces_ext::Properties { online, addresses, .. },
                    )| {
                        // TODO(https://github.com/rust-lang/rust/issues/80967): use bool::then_some.
                        (*id != wlan_ap_id
                            && *online
                            && addresses.iter().any(
                                |&fidl_fuchsia_net_interfaces_ext::Address {
                                     addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                                     valid_until: _,
                                 }| match addr {
                                    net::IpAddress::Ipv4(net::Ipv4Address { addr }) => {
                                        NETWORK_ADDR_SUBNET
                                            .contains(&net_types_ip::Ipv4Addr::new(addr))
                                    }
                                    net::IpAddress::Ipv6(net::Ipv6Address { addr: _ }) => false,
                                },
                            ))
                        .then(|| ())
                    },
                )
            },
        )
        .map_err(anyhow::Error::from)
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!("timed out"))
        })
        .await
        .context("wait for host interface to be configured")?;

        // Take the interface down, the DHCP server should be stopped.
        let () = wlan_ap.set_link_up(false).await.context("set wlan ap link down")?;
        let () = check_dhcp_status(&dhcp_server, false)
            .await
            .context("check DHCP server stopped after interface down")?;

        // Bring the interface back up, the DHCP server should be started.
        let () = wlan_ap.set_link_up(true).await.context("set wlan ap link up")?;
        let () = check_dhcp_status(&dhcp_server, true)
            .await
            .context("check DHCP server started after interface up")?;

        // Remove the interface, the DHCP server should be stopped.
        std::mem::drop(wlan_ap);
        let () = check_dhcp_status(&dhcp_server, false)
            .await
            .context("check DHCP server stopped after interface removed")?;

        Ok(())
    }

    let sandbox = netemul::TestSandbox::new().context("create sandbox")?;
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            name,
            &[
                KnownServiceProvider::Manager(M::MANAGEMENT_AGENT),
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::Dhcpv6Client,
                KnownServiceProvider::SecureStash,
            ],
        )
        .context("create netstack realm")?;
    let wait_for_netmgr =
        wait_for_component_stopped(&realm, constants::netcfg::COMPONENT_NAME, None).fuse();
    futures::pin_mut!(wait_for_netmgr);

    // Add a WLAN AP, make sure the DHCP server gets configurd and starts or stops when the
    // interface is added and brought up or brought down/removed.
    for i in 0..=1 {
        let test_fut = wlan_ap_dhcp_server_inner::<E>(&sandbox, &realm, i).fuse();
        futures::pin_mut!(test_fut);
        let () = futures::select! {
            test_res = test_fut => test_res,
            stopped_event = wait_for_netmgr => {
                Err(anyhow::anyhow!(
                    "NetCfg unexpectedly exited with exit status = {:?}",
                    stopped_event
                ))
            }
        }
        .with_context(|| format!("test {}-th interface", i))?;
    }

    Ok(())
}

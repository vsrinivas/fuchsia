// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, HashSet};

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_dhcp as dhcp;
use fidl_fuchsia_net_ext::IntoExt as _;
use fidl_fuchsia_net_interfaces as net_interfaces;
use fidl_fuchsia_netemul_network as netemul_network;
use fidl_fuchsia_netstack as netstack;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{FusedFuture, Future, FutureExt as _};
use futures::stream::{self, StreamExt as _};
use net_declare::fidl_ip_v4;
use net_types::ip as net_types_ip;
use netstack_testing_macros::variants_test;

use crate::environments::{KnownServices, Manager, NetCfg, Netstack2, TestSandboxExt as _};
use crate::{
    try_all, try_any, Result, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT, DHCP_SERVER_DEFAULT_CONFIG_PATH,
};

/// Waits for a non-loopback interface to come up with an ID not in `exclude_ids`.
///
/// Useful when waiting for an interface to be discovered and brought up by a
/// network manager.
///
/// Returns the interface's ID and name.
pub(crate) async fn wait_for_non_loopback_interface_up<
    F: Unpin + FusedFuture + Future<Output = Result<fuchsia_component::client::ExitStatus>>,
>(
    interface_state: &net_interfaces::StateProxy,
    mut wait_for_netmgr: &mut F,
    exclude_ids: Option<&HashSet<u32>>,
    timeout: zx::Duration,
) -> Result<(u32, String)> {
    let mut if_map = HashMap::new();
    let wait_for_interface = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(interface_state)?,
        &mut if_map,
        |if_map| {
            if_map.iter().find_map(|(id, properties)| {
                let id = *id as u32;
                if properties.device_class
                    != Some(net_interfaces::DeviceClass::Loopback(net_interfaces::Empty {}))
                    && properties.online?
                    && exclude_ids.map_or(true, |ids| !ids.contains(&id))
                {
                    Some((
                        id,
                        properties.name.clone().expect("failed to find loopback interface name"),
                    ))
                } else {
                    None
                }
            })
        },
    )
    .on_timeout(timeout.after_now(), || {
        Err(anyhow::anyhow!(
            "timed out waiting for OnInterfaceseChanged event with a non-loopback interface"
        ))
    })
    .fuse();
    fuchsia_async::pin_mut!(wait_for_interface);
    futures::select! {
        wait_for_interface_res = wait_for_interface => {
            wait_for_interface_res
        }
        wait_for_netmgr_res = wait_for_netmgr => {
            Err(anyhow::anyhow!("the network manager unexpectedly exited with exit status = {:?}", wait_for_netmgr_res?))
        }
    }
}

/// Test that NetCfg discovers a newly added device and it adds the device
/// to the Netstack.
// TODO(fxbug.dev/54025): Enable this test for NetworkManager.
#[variants_test]
async fn test_oir<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("create sandbox")?;
    // Create an environment with the LookupAdmin service as NetCfg tries to configure
    // it. NetCfg will fail if it can't send the LookupAdmin a request.
    let environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(name, &[KnownServices::LookupAdmin])
        .context("create netstack environment")?;

    // Start the network manager.
    let launcher = environment.get_launcher().context("get launcher")?;
    let mut netmgr = fuchsia_component::client::launch(
        &launcher,
        NetCfg::PKG_URL.to_string(),
        NetCfg::testing_args(),
    )
    .context("launch the network manager")?;

    // Add a device to the environment.
    let endpoint = sandbox.create_endpoint::<E, _>(name).await.context("create endpoint")?;
    let () = endpoint.set_link_up(true).await.context("set link up")?;
    let endpoint_mount_path = E::dev_path("ep");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = environment
        .add_virtual_device(&endpoint, endpoint_mount_path)
        .with_context(|| format!("add virtual device {}", endpoint_mount_path.display()))?;

    // Make sure the Netstack got the new device added.
    let mut wait_for_netmgr = netmgr.wait().fuse();
    let interface_state = environment
        .connect_to_service::<net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let (_id, _name): (u32, String) = wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        None,
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .context("wait for non loopback interface")?;

    environment
        .remove_virtual_device(endpoint_mount_path)
        .with_context(|| format!("remove virtual device {}", endpoint_mount_path.display()))
}

/// Tests that stable interface name conflicts are handled gracefully.
// TODO(fxbug.dev/54025): Enable this test for NetworkManager.
#[variants_test]
async fn test_oir_interface_name_conflict<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("create sandbox")?;
    let environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(name, &[KnownServices::LookupAdmin])
        .context("create netstack environment")?;

    // Start the network manager.
    let launcher = environment.get_launcher().context("get launcher")?;
    let mut netmgr = fuchsia_component::client::launch(
        &launcher,
        NetCfg::PKG_URL.to_string(),
        NetCfg::testing_args(),
    )
    .context("launch the network manager")?;

    let mut wait_for_netmgr = netmgr.wait().fuse();
    let netstack = environment
        .connect_to_service::<netstack::NetstackMarker>()
        .context("connect to netstack service")?;

    // Add a device to the environment and wait for it to be added to the netstack.
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
    let () = environment
        .add_virtual_device(&ethx7, endpoint_mount_path)
        .with_context(|| format!("add virtual device1 {}", endpoint_mount_path.display()))?;
    let interface_state = environment
        .connect_to_service::<net_interfaces::StateMarker>()
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
    assert_eq!(id_etht0, netstack_id_etht0);
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
    let () = environment
        .add_virtual_device(&etht1, endpoint_mount_path)
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
async fn test_wlan_ap_dhcp_server<E: netemul::Endpoint>(name: &str) -> Result {
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
        environment: &netemul::TestEnvironment<'a>,
        offset: u8,
    ) -> Result {
        // These constants are all hard coded in NetCfg for the WLAN AP interface and
        // the DHCP server.
        const DHCP_LEASE_TIME: u32 = 24 * 60 * 60; // 1 day in seconds.
        const NETWORK_ADDR: net::Ipv4Address = fidl_ip_v4!(192.168.255.248);
        const NETWORK_PREFIX_LEN: u8 = 29;
        const INTERFACE_ADDR: net::Ipv4Address = fidl_ip_v4!(192.168.255.249);
        const DHCP_POOL_START_ADDR: net::Ipv4Address = fidl_ip_v4!(192.168.255.250);
        const DHCP_POOL_END_ADDR: net::Ipv4Address = fidl_ip_v4!(192.168.255.254);
        const BROADCAST_ADDR: net::Ipv4Address = fidl_ip_v4!(192.168.255.255);
        const NETWORK_MASK: net::Ipv4Address = fidl_ip_v4!(255.255.255.248);
        const NETWORK_ADDR_SUBNET: net_types_ip::Subnet<net_types_ip::Ipv4Addr> = unsafe {
            net_types_ip::Subnet::new_unchecked(
                net_types_ip::Ipv4Addr::new(NETWORK_ADDR.addr),
                NETWORK_PREFIX_LEN,
            )
        };

        // Add a device to the environment that looks like a WLAN AP from the perspective
        // of NetCfg. The topological path for the interface must include "wlanif-ap" as
        // that is how NetCfg identifies a WLAN AP interface.
        let network = sandbox
            .create_network(format!("dhcp-server-{}", offset))
            .await
            .context("create network")?;
        let wlan_ap = network
            .create_endpoint::<E, _>(format!("wlanif-ap-dhcp-server-{}", offset))
            .await
            .context("create wlan ap")?;
        let path = E::dev_path(&format!("dhcp-server-ep-{}", offset));
        let () = environment
            .add_virtual_device(&wlan_ap, path.as_path())
            .with_context(|| format!("add WLAN AP virtual device {}", path.display()))?;
        let () = wlan_ap.set_link_up(true).await.context("set wlan ap link up")?;

        // Make sure the WLAN AP interface is added to the Netstack and is brought up with
        // the right IP address.
        let interface_state = environment
            .connect_to_service::<net_interfaces::StateMarker>()
            .context("connect to fuchsia.net.interfaces/State service")?;
        let (watcher, watcher_server) =
            ::fidl::endpoints::create_proxy::<net_interfaces::WatcherMarker>()?;
        let () = interface_state
            .get_watcher(net_interfaces::WatcherOptions {}, watcher_server)
            .context("failed to initialize interface watcher")?;
        let mut if_map = HashMap::new();
        let (wlan_ap_id, wlan_ap_name) = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                if_map.iter().find_map(|(id, properties)| {
                    if properties.online?
                        && properties
                            .addresses
                            .as_ref()?
                            .iter()
                            .filter_map(|a| a.addr)
                            .any(|a| a.addr == INTERFACE_ADDR.into_ext())
                    {
                        Some((
                            *id,
                            properties.name.clone().expect("WLAN AP interface name missing"),
                        ))
                    } else {
                        None
                    }
                })
            },
        )
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!(
                "timed out waiting for OnInterfaceseChanged event with a WLAN AP interface"
            ))
        })
        .await?;

        // Check the DHCP server's configured parameters.
        let dhcp_server = environment
            .connect_to_service::<dhcp::Server_Marker>()
            .context("connect to DHCP server service")?;
        let checks = [
            (dhcp::ParameterName::IpAddrs, dhcp::Parameter::IpAddrs(vec![INTERFACE_ADDR])),
            (
                dhcp::ParameterName::LeaseLength,
                dhcp::Parameter::Lease(dhcp::LeaseLength {
                    default: Some(DHCP_LEASE_TIME),
                    max: Some(DHCP_LEASE_TIME),
                }),
            ),
            (
                dhcp::ParameterName::BoundDeviceNames,
                dhcp::Parameter::BoundDeviceNames(vec![wlan_ap_name]),
            ),
            (
                dhcp::ParameterName::AddressPool,
                dhcp::Parameter::AddressPool(dhcp::AddressPool {
                    network_id: Some(NETWORK_ADDR),
                    broadcast: Some(BROADCAST_ADDR),
                    mask: Some(NETWORK_MASK),
                    pool_range_start: Some(DHCP_POOL_START_ADDR),
                    pool_range_stop: Some(DHCP_POOL_END_ADDR),
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
        let () = environment
            .add_virtual_device(&host, path.as_path())
            .with_context(|| format!("add host virtual device {}", path.display()))?;
        let () = host.set_link_up(true).await.context("set host link up")?;
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
            &mut if_map,
            |if_map| {
                if_map.iter().find_map(|(id, properties)| {
                    if *id != wlan_ap_id
                        && properties.online?
                        && properties.addresses.as_ref()?.iter().filter_map(|a| a.addr).any(|a| {
                            match a.addr {
                                net::IpAddress::Ipv4(addr) => NETWORK_ADDR_SUBNET
                                    .contains(&net_types_ip::Ipv4Addr::new(addr.addr)),
                                net::IpAddress::Ipv6(_) => false,
                            }
                        })
                    {
                        Some(())
                    } else {
                        None
                    }
                })
            },
        )
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!(
                "timed out waiting for OnInterfaceseChanged event with a WLAN AP interface"
            ))
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
    let environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            name,
            vec![
                KnownServices::LookupAdmin.into_launch_service(),
                KnownServices::DhcpServer.into_launch_service_with_arguments(vec![
                    "--config",
                    DHCP_SERVER_DEFAULT_CONFIG_PATH,
                ]),
                KnownServices::SecureStash.into_launch_service(),
            ],
        )
        .context("create netstack environment")?;

    // Start NetCfg.
    let launcher = environment.get_launcher().context("get launcher")?;
    let mut netcfg = fuchsia_component::client::launch(
        &launcher,
        NetCfg::PKG_URL.to_string(),
        NetCfg::testing_args(),
    )
    .context("launch netcfg")?;
    let mut wait_for_netcfg_fut = netcfg.wait().fuse();

    // Add a WLAN AP, make sure the DHCP server gets configurd and starts or stops when the
    // interface is added and brought up or brought down/removed.
    for i in 0..=1 {
        let test_fut = wlan_ap_dhcp_server_inner::<E>(&sandbox, &environment, i).fuse();
        futures::pin_mut!(test_fut);
        let () = futures::select! {
            test_res = test_fut => test_res,
            wait_for_netcfg_res = wait_for_netcfg_fut => {
                Err(anyhow::anyhow!("NetCfg unexpectedly exited with exit status = {:?}", wait_for_netcfg_res?))
            }
        }
        .with_context(|| format!("test {}-th interface", i))?;
    }

    Ok(())
}

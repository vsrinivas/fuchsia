// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_ethernet as eth;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_dhcp as dhcp;
use fidl_fuchsia_net_ext::IntoExt as _;
use fidl_fuchsia_net_stack as net_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_netstack as netstack;
use fuchsia_async as fasync;

use anyhow::Context as _;
use futures::future::{self, FutureExt as _};
use futures::stream::{self, StreamExt as _};
use net_declare::fidl_ip_v4;
use net_types::ip as net_types_ip;

use crate::environments::*;
use crate::*;

/// Test that NetCfg discovers a newly added device and it adds the device
/// to the Netstack.
// TODO(54025): Enable this test for NetworkManager.
#[fasync::run_singlethreaded(test)]
async fn test_oir() -> Result {
    let name = "test_oir";
    let sandbox = TestSandbox::new().context("create sandbox")?;
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
    let endpoint = sandbox.create_endpoint::<Ethernet, _>(name).await.context("create endpoint")?;
    let endpoint_mount_path = "class/ethernet/ep";
    let () = environment.add_virtual_device(&endpoint, endpoint_mount_path.to_string())?;

    // Make sure the Netstack got the new device added.
    let netstack = environment
        .connect_to_service::<netstack::NetstackMarker>()
        .context("connect to netstack service")?;
    let mut wait_for_interface = netstack
        .take_event_stream()
        .try_filter_map(|netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            future::ok(interfaces.into_iter().find_map(
                |netstack::NetInterface { id, features, .. }| {
                    // Ignore the loopback device.
                    if features & eth::INFO_FEATURE_LOOPBACK == 0 {
                        Some(id)
                    } else {
                        None
                    }
                },
            ))
        })
        .map(|r| r.context("getting next OnInterfaceChanged event"));
    let mut wait_for_interface = wait_for_interface
        .try_next()
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!(
                "timed out waiting for OnInterfaceseChanged event with a non-loopback interface"
            ))
        })
        .fuse();
    let mut wait_for_netmgr = netmgr.wait().fuse();
    let _id = futures::select! {
        wait_for_interface_res = wait_for_interface => {
            wait_for_interface_res?.ok_or(anyhow::anyhow!("Netstack event stream unexpectedly ended"))
        }
        wait_for_netmgr_res = wait_for_netmgr => {
            Err(anyhow::anyhow!("the network manager unexpectedly exited with exit status = {:?}", wait_for_netmgr_res?))
        }
    }?;

    environment.remove_virtual_device(endpoint_mount_path)
}

/// Make sure the DHCP server is configured to start serving requests when NetCfg discovers
/// a WLAN AP interface and stops serving when the interface is removed.
///
/// Also make sure that a new WLAN AP interface may be added after a previous interface has been
/// removed from the netstack.
#[fasync::run_singlethreaded(test)]
async fn test_wlan_ap_dhcp_server() -> Result {
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
    async fn wlan_ap_dhcp_server_inner<'a>(
        sandbox: &'a TestSandbox,
        environment: &TestEnvironment<'a>,
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
            .create_endpoint::<Ethernet, _>(format!("wlanif-ap-dhcp-server-{}", offset))
            .await
            .context("create wlan ap")?;
        let () = environment
            .add_virtual_device(&wlan_ap, format!("class/ethernet/dhcp-server-ep-{}", offset))?;
        let () = wlan_ap.set_link_up(true).await.context("set wlan ap link up")?;

        // Make sure the WLAN AP interface is added to the Netstack and is brought up with
        // the right IP address.
        let netstack = environment
            .connect_to_service::<netstack::NetstackMarker>()
            .context("connect to netstack service")?;
        let (wlan_ap_id, wlan_ap_name) = netstack
            .take_event_stream()
            .try_filter_map(|netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                future::ok(interfaces.into_iter().find_map(
                    |netstack::NetInterface { id, name, addr, flags, .. }| {
                        if addr == INTERFACE_ADDR.into_ext()
                            && flags & netstack::NET_INTERFACE_FLAG_UP != 0
                        {
                            Some((id, name))
                        } else {
                            None
                        }
                    },
                ))
            })
            .map(|r| r.context("getting next OnInterfaceChanged event"))
            .try_next()
            .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                Err(anyhow::anyhow!(
                    "timed out waiting for OnInterfaceseChanged event with a WLAN AP interface"
                ))
            })
            .await?
            .ok_or(anyhow::anyhow!("Netstack event stream unexpectedly ended"))?;

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
            .create_endpoint::<Ethernet, _>(format!("host-dhcp-client-{}", offset))
            .await
            .context("create host")?;
        let () = environment
            .add_virtual_device(&host, format!("class/ethernet/dhcp-client-ep-{}", offset))?;
        let () = host.set_link_up(true).await.context("set host link up")?;
        let () = netstack
            .take_event_stream()
            .try_filter_map(|netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                if interfaces.iter().any(
                    |netstack::NetInterface { id, addr, netmask, broadaddr, flags, .. }| {
                        id != &wlan_ap_id
                            && flags & netstack::NET_INTERFACE_FLAG_UP != 0
                            && netmask == &NETWORK_MASK.into_ext()
                            && broadaddr == &BROADCAST_ADDR.into_ext()
                            && match addr {
                                net::IpAddress::Ipv4(addr) => NETWORK_ADDR_SUBNET
                                    .contains(&net_types_ip::Ipv4Addr::new(addr.addr)),
                                net::IpAddress::Ipv6(addr) => panic!(
                                "NetInterface.addr should only contain an IPv4 Address, got = {:?}",
                                addr
                            ),
                            }
                    },
                ) {
                    future::ok(Some(()))
                } else {
                    future::ok(None)
                }
            })
            .map(|r| r.context("getting next OnInterfaceChanged event"))
            .try_next()
            .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                Err(anyhow::anyhow!(
                    "timed out waiting for OnInterfaceseChanged event with a WLAN AP interface"
                ))
            })
            .await
            .context("wait for host interface to be configured")?
            .ok_or(anyhow::anyhow!("Netstack event stream unexpectedly ended"))?;

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
        let stack = environment
            .connect_to_service::<net_stack::StackMarker>()
            .context("connect to stack service")?;
        let () = stack
            .del_ethernet_interface(wlan_ap_id.into())
            .await
            .squash_result()
            .with_context(|| format!("failed to delete interface with id = {}", wlan_ap_id))?;
        let () = check_dhcp_status(&dhcp_server, false)
            .await
            .context("check DHCP server stopped after interface removed")?;

        Ok(())
    }

    let name = "wlan_ap_dhcp_server";
    let sandbox = TestSandbox::new().context("create sandbox")?;
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
        let test_fut = wlan_ap_dhcp_server_inner(&sandbox, &environment, i).fuse();
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

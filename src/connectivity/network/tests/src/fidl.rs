// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use fidl_fuchsia_net_stack_ext::{exec_fidl, FidlReturn};
use futures::{FutureExt as _, TryStreamExt as _};
use net_declare::{fidl_ip, std_ip};
use netemul::EnvironmentUdpSocket as _;
use netstack_testing_macros::variants_test;

use crate::environments::{Netstack, Netstack2, TestSandboxExt as _};
use crate::{EthertapName as _, Result};

/// Regression test: test that Netstack.SetInterfaceStatus does not kill the channel to the client
/// if given an invalid interface id.
#[fuchsia_async::run_singlethreaded(test)]
async fn set_interface_status_unknown_interface() -> Result {
    let name = "set_interface_status";
    let sandbox = netemul::TestSandbox::new()?;
    let (_env, netstack) =
        sandbox.new_netstack::<Netstack2, fidl_fuchsia_netstack::NetstackMarker, _>(name)?;

    let interfaces = netstack.get_interfaces2().await.context("failed to call get_interfaces2")?;
    let next_id =
        1 + interfaces.iter().map(|interface| interface.id).max().ok_or(anyhow::format_err!(
            "failed to find any network interfaces (at least localhost should be present)"
        ))?;

    let () = netstack
        .set_interface_status(next_id, false)
        .context("failed to call set_interface_status")?;
    let _interfaces = netstack
        .get_interfaces2()
        .await
        .context("failed to invoke netstack method after calling set_interface_status with an invalid argument")?;

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_device() -> Result {
    let name = "add_ethernet_device";
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (_env, netstack, device) = sandbox
        .new_netstack_and_device::<Netstack2, netemul::Ethernet, fidl_fuchsia_netstack::NetstackMarker, _>(
            name,
        )
        .await?;

    let id = netstack
        .add_ethernet_device(
            name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: name[..fidl_fuchsia_posix_socket::INTERFACE_NAME_LENGTH.into()].to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
            },
            // We're testing add_ethernet_device (netstack.fidl), which
            // does not have a network device entry point.
            device
                .get_ethernet()
                .await
                .context("add_ethernet_device requires an Ethernet endpoint")?,
        )
        .await
        .context("add_ethernet_device FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_ethernet_device failed")?;
    let interface = netstack
        .get_interfaces2()
        .await
        .context("failed to get interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet device"))?;
    let features = fidl_fuchsia_hardware_ethernet::Features::from_bits_truncate(interface.features);
    assert!(
        !features.contains(fidl_fuchsia_hardware_ethernet::Features::Loopback),
        "unexpected interface features: ({:b}).contains({:b})",
        features,
        fidl_fuchsia_hardware_ethernet::Features::Loopback
    );
    assert_eq!(interface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP, 0);
    Ok::<(), anyhow::Error>(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_no_duplicate_interface_names() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (env, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(
            "no_duplicate_interface_names",
        )
        .context("failed to create environment")?;
    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;
    // Create one endpoint of each type so we can use all the APIs that add an
    // interface. Note that fuchsia.net.stack/Stack.AddEthernetInterface does
    // not support setting the interface name.
    let eth_ep = sandbox
        .create_endpoint::<netemul::Ethernet, _>("eth-ep")
        .await
        .context("failed to create ethernet endpoint")?;
    let netdev_ep = sandbox
        .create_endpoint::<netemul::NetworkDevice, _>("netdev-ep")
        .await
        .context("failed to create netdevice endpoint")?;

    const IFNAME: &'static str = "testif";
    const TOPOPATH: &'static str = "/fake/topopath";
    const FILEPATH: &'static str = "/fake/filepath";
    // Add the first ep to the stack so it takes over the name.
    let _nicid = netstack
        .add_ethernet_device(
            TOPOPATH,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: IFNAME.to_string(),
                filepath: FILEPATH.to_string(),
                metric: 0,
            },
            eth_ep.get_ethernet().await.context("failed to connect to ethernet device")?,
        )
        .await
        .context("add_ethernet_device FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_ethernet_device error")?;

    // Now try to add again with the same parameters and expect an error.
    let result = netstack
        .add_ethernet_device(
            TOPOPATH,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: IFNAME.to_string(),
                filepath: FILEPATH.to_string(),
                metric: 0,
            },
            eth_ep.get_ethernet().await.context("failed to connect to ethernet device")?,
        )
        .await
        .context("add_ethernet_device FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw);
    assert_eq!(result, Err(fuchsia_zircon::Status::ALREADY_EXISTS));

    // Same for netdevice.
    let (network_device, mac) =
        netdev_ep.get_netdevice().await.context("failed to connect to netdevice protocols")?;
    let result = stack
        .add_interface(
            fidl_fuchsia_net_stack::InterfaceConfig {
                name: Some(IFNAME.to_string()),
                topopath: None,
                metric: None,
            },
            &mut fidl_fuchsia_net_stack::DeviceDefinition::Ethernet(
                fidl_fuchsia_net_stack::EthernetDeviceDefinition { network_device, mac },
            ),
        )
        .await
        .context("add_interface FIDL error")?;
    assert_eq!(result, Err(fidl_fuchsia_net_stack::Error::AlreadyExists));

    Ok(())
}

#[variants_test]
async fn add_ethernet_interface<N: Netstack>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new()?;
    let (_env, stack, device) = sandbox
        .new_netstack_and_device::<N, netemul::Ethernet, fidl_fuchsia_net_stack::StackMarker, _>(
            name,
        )
        .await?;
    let id = device.add_to_stack(&stack).await?;
    let interface = stack
        .list_interfaces()
        .await
        .context("failed to list interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet interface"))?;
    assert!(
        !interface.properties.features.contains(fidl_fuchsia_hardware_ethernet::Features::Loopback),
        "unexpected interface features: ({:b}).contains({:b})",
        interface.properties.features,
        fidl_fuchsia_hardware_ethernet::Features::Loopback
    );
    assert_eq!(interface.properties.physical_status, fidl_fuchsia_net_stack::PhysicalStatus::Down);
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_del_interface_address() -> Result {
    let name = "add_del_interface_address";

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (_env, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .context("failed to create environment")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let loopback = interfaces
        .iter()
        .find(|interface| {
            interface
                .properties
                .features
                .contains(fidl_fuchsia_hardware_ethernet::Features::Loopback)
        })
        .ok_or(anyhow::format_err!("failed to find loopback"))?;
    let mut interface_address =
        fidl_fuchsia_net::Subnet { addr: fidl_ip!(1.1.1.1), prefix_len: 32 };
    let res = stack
        .add_interface_address(loopback.id, &mut interface_address)
        .await
        .context("failed to call add interface address")?;
    assert_eq!(res, Ok(()));
    let loopback =
        exec_fidl!(stack.get_interface_info(loopback.id), "failed to get loopback interface")?;

    assert!(
        loopback.properties.addresses.iter().find(|addr| *addr == &interface_address).is_some(),
        "couldn't find {:#?} in {:#?}",
        interface_address,
        loopback.properties.addresses
    );

    let res = stack
        .del_interface_address(loopback.id, &mut interface_address)
        .await
        .context("failed to call del interface address")?;
    assert_eq!(res, Ok(()));
    let loopback =
        exec_fidl!(stack.get_interface_info(loopback.id), "failed to get loopback interface")?;

    assert!(
        loopback.properties.addresses.iter().find(|addr| *addr == &interface_address).is_none(),
        "did not expect to find {:#?} in {:#?}",
        interface_address,
        loopback.properties.addresses
    );

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_remove_interface_address_errors() -> Result {
    let name = "add_remove_interface_address_errors";

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (env, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .context("failed to create environment")?;
    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let mut interface_address = fidl_fuchsia_net::Subnet { addr: fidl_ip!(0.0.0.0), prefix_len: 0 };

    // Don't crash on interface not found.

    let error = stack
        .add_interface_address(max_id + 1, &mut interface_address)
        .await
        .context("failed to call add interface address")?
        .unwrap_err();
    assert_eq!(error, fidl_fuchsia_net_stack::Error::NotFound);

    let error = netstack
        .remove_interface_address(
            std::convert::TryInto::try_into(max_id + 1).expect("should fit"),
            &mut interface_address.addr,
            interface_address.prefix_len,
        )
        .await
        .context("failed to call add interface address")?;
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::UnknownInterface,
            message: "".to_string(),
        },
    );

    // Don't crash on invalid prefix length.
    interface_address.prefix_len = 43;
    let error = stack
        .add_interface_address(max_id, &mut interface_address)
        .await
        .context("failed to call add interface address")?
        .unwrap_err();
    assert_eq!(error, fidl_fuchsia_net_stack::Error::InvalidArgs);

    let error = netstack
        .remove_interface_address(
            std::convert::TryInto::try_into(max_id).expect("should fit"),
            &mut interface_address.addr,
            interface_address.prefix_len,
        )
        .await
        .context("failed to call add interface address")?;
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::ParseError,
            message: "prefix length exceeds address length".to_string(),
        },
    );

    Ok(())
}

#[variants_test]
async fn get_interface_info_not_found<N: Netstack>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (_env, stack) = sandbox
        .new_netstack::<N, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .context("failed to create environment")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let res =
        stack.get_interface_info(max_id + 1).await.context("failed to call get interface info")?;
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::NotFound));
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_interface_loopback() -> Result {
    let name = "disable_interface_loopback";

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (_env, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .context("failed to create environment")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let localhost = interfaces
        .iter()
        .find(|interface| {
            interface
                .properties
                .features
                .contains(fidl_fuchsia_hardware_ethernet::Features::Loopback)
        })
        .ok_or(anyhow::format_err!("failed to find loopback interface"))?;
    assert_eq!(
        localhost.properties.administrative_status,
        fidl_fuchsia_net_stack::AdministrativeStatus::Enabled
    );
    let () = exec_fidl!(stack.disable_interface(localhost.id), "failed to disable interface")?;
    let info = exec_fidl!(stack.get_interface_info(localhost.id), "failed to get interface info")?;
    assert_eq!(
        info.properties.administrative_status,
        fidl_fuchsia_net_stack::AdministrativeStatus::Disabled
    );
    Ok(())
}

/// Tests fuchsia.net.stack/Stack.del_ethernet_interface.
#[variants_test]
async fn test_remove_interface<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (_env, stack, device) = sandbox
        .new_netstack_and_device::<Netstack2, E, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .await
        .context("failed to create netstack environment")?;

    let id = device.add_to_stack(&stack).await.context("failed to add device")?;

    let () = stack
        .del_ethernet_interface(id)
        .await
        .squash_result()
        .context("failed to delete device")?;

    let ifs = stack.list_interfaces().await.context("failed to list interfaces")?;

    assert_eq!(ifs.into_iter().find(|info| info.id == id), None);

    Ok(())
}

/// Tests that adding an interface causes an interface changed event.
#[variants_test]
async fn test_add_interface_causes_interfaces_changed<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (env, stack, device) = sandbox
        .new_netstack_and_device::<Netstack2, E, fidl_fuchsia_net_stack::StackMarker, _>(
            name.ethertap_compatible_name(),
        )
        .await
        .context("failed to create netstack environment")?;

    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    // Ensure we're connected to Netstack so we don't miss any events.
    // Since we do it, assert that only loopback exists.
    let ifs = netstack.get_interfaces2().await.context("failed to get interfaces")?;
    assert_eq!(ifs.len(), 1);

    let id = device.add_to_stack(&stack).await.context("failed to add device")?;

    // Wait for interfaces changed event with the new ID.
    let _ifaces = netstack
        .take_event_stream()
        .try_filter(|fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            futures::future::ready(interfaces.into_iter().any(|iface| iface.id == id as u32))
        })
        .try_next()
        .await
        .context("failed to observe interface addition")?
        .ok_or_else(|| anyhow::anyhow!("netstack stream ended unexpectedly"))?;

    Ok(())
}

/// Tests that if a device closes (is removed from the system), the
/// corresponding Netstack interface is deleted.
/// if `enabled` is `true`, enables the interface before closing the device.
async fn test_close_interface<E: netemul::Endpoint>(enabled: bool, name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let (env, stack, device) = sandbox
        .new_netstack_and_device::<Netstack2, E, fidl_fuchsia_net_stack::StackMarker, _>(
            name.ethertap_compatible_name(),
        )
        .await
        .context("failed to create netstack environment")?;

    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    // Ensure we're connected to Netstack so we don't miss any events.
    // Since we do it, assert that only loopback exists.
    let ifs = netstack.get_interfaces2().await.context("failed to get interfaces")?;
    assert_eq!(ifs.len(), 1);

    let id = device.add_to_stack(&stack).await.context("failed to add device")?;
    if enabled {
        let () = stack
            .enable_interface(id)
            .await
            .squash_result()
            .context("failed to enable interface")?;
    }

    let _ifaces = netstack
        .take_event_stream()
        .try_filter(|fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            futures::future::ready(interfaces.into_iter().any(|iface| iface.id == id as u32))
        })
        .try_next()
        .await
        .context("failed to observe interface addition")?
        .ok_or_else(|| anyhow::anyhow!("netstack stream ended unexpectedly"))?;

    // Drop the device, that should cause the interface to be deleted.
    std::mem::drop(device);

    // Wait until we observe an event where the removed interface is missing.
    let _ifaces = netstack
        .take_event_stream()
        .try_filter(|fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            println!("interfaces changed: {:?}", interfaces);
            futures::future::ready(!interfaces.into_iter().any(|iface| iface.id == id as u32))
        })
        .try_next()
        .await
        .context("failed to observe interface addition")?
        .ok_or_else(|| anyhow::anyhow!("netstack stream ended unexpectedly"))?;

    Ok(())
}

#[variants_test]
async fn test_close_disabled_interface<E: netemul::Endpoint>(name: &str) -> Result {
    test_close_interface::<E>(false, name).await
}

#[variants_test]
async fn test_close_enabled_interface<E: netemul::Endpoint>(name: &str) -> Result {
    test_close_interface::<E>(true, name).await
}

/// Tests races between device link down and close.
#[variants_test]
async fn test_down_close_race<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>(name)
        .context("failed to create netstack environment")?;

    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    for _ in 0..10u64 {
        let dev = sandbox
            .create_endpoint::<E, _>("ep")
            .await
            .context("failed to create endpoint")?
            .into_interface_in_environment(&env)
            .await
            .context("failed to add endpoint to Netstack")?;

        let () = dev.enable_interface().await.context("failed to enable interface")?;
        let () = dev.start_dhcp().await.context("failed to start DHCP")?;
        let () = dev.set_link_up(true).await.context("failed to bring device up")?;

        let id = dev.id();
        // Wait until the interface is installed and the link state is up.
        let _ifaces = netstack
            .take_event_stream()
            .try_filter(
                |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                    futures::future::ready(interfaces.into_iter().any(|iface| {
                        iface.id == id as u32
                            && iface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP != 0
                    }))
                },
            )
            .try_next()
            .await
            .context("failed to observe event stream")?
            .ok_or_else(|| {
                anyhow::anyhow!("netstack stream ended unexpectedly while waiting for interface up")
            })?;

        // Here's where we cause the race. We bring the device's link down
        // and drop it right after; the two signals will race to reach
        // Netstack.
        let () = dev.set_link_up(false).await.context("failed to bring link down")?;
        std::mem::drop(dev);

        // Wait until the interface is removed from Netstack cleanly.
        let _ifaces = netstack
            .take_event_stream()
            .try_filter(
                |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                    futures::future::ready(
                        !interfaces.into_iter().any(|iface| iface.id == id as u32),
                    )
                },
            )
            .try_next()
            .await
            .context("failed to observe event stream")?
            .ok_or_else(|| {
                anyhow::anyhow!(
                    "netstack stream ended unexpectedly while waiting for interface disappear"
                )
            })?;
    }
    Ok(())
}

/// Tests races between data traffic and closing a device.
#[variants_test]
async fn test_close_data_race<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;
    let fake_ep = net.create_fake_endpoint().context("failed to create fake endpoint")?;
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>(name)
        .context("failed to create netstack environment")?;

    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    // NOTE: We only run this test with IPv4 sockets since we only care about
    // exciting the tx path, the domain is irrelevant.
    const DEVICE_ADDRESS: fidl_fuchsia_net::Subnet =
        fidl_fuchsia_net::Subnet { addr: fidl_ip!(192.168.0.2), prefix_len: 24 };
    // We're going to send data over a UDP socket to a multicast address so we
    // skip ARP resolution.
    const MCAST_ADDR: std::net::IpAddr = std_ip!(224.0.0.1);

    for _ in 0..10u64 {
        let dev = net
            .create_endpoint::<E, _>("ep")
            .await
            .context("failed to create endpoint")?
            .into_interface_in_environment(&env)
            .await
            .context("failed to add endpoint to Netstack")?;

        let () = dev.enable_interface().await.context("failed to enable interface")?;
        let () = dev.set_link_up(true).await.context("failed to bring device up")?;
        let () = dev.add_ip_addr(DEVICE_ADDRESS).await.context("failed to add address")?;

        let id = dev.id();
        // Wait until the interface is installed and the link state is up.
        let _ifaces = netstack
            .take_event_stream()
            .try_filter(
                |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                    futures::future::ready(interfaces.into_iter().any(|iface| {
                        iface.id == id as u32
                            && iface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP != 0
                    }))
                },
            )
            .try_next()
            .await
            .context("failed to observe event stream")?
            .ok_or_else(|| {
                anyhow::anyhow!("netstack stream ended unexpectedly while waiting for interface up")
            })?;
        // Create a socket and start sending data on it nonstop.
        let fidl_fuchsia_net_ext::IpAddress(bind_addr) = DEVICE_ADDRESS.addr.into();
        let sock = fuchsia_async::net::UdpSocket::bind_in_env(
            &env,
            std::net::SocketAddr::new(bind_addr, 0),
        )
        .await
        .context("failed to create socket")?;

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
                    .context("failed to send frame on fake_ep")?;

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

        let mut iface_dropped = netstack.take_event_stream().try_filter(
            |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                futures::future::ready(!interfaces.into_iter().any(|iface| iface.id == id as u32))
            },
        );

        let (io_result, iface_dropped, ()) =
            futures::future::join3(io_fut, iface_dropped.try_next(), drop_fut).await;
        let () = io_result.context("unexpected error on io future")?;
        let _ifaces =
            iface_dropped.context("failed to observe event stream")?.ok_or_else(|| {
                anyhow::anyhow!(
                    "netstack stream ended unexpectedly while waiting for interface to disappear"
                )
            })?;
    }
    Ok(())
}

/// Tests that competing InterfacesChanged events will be reported in the
/// correct order.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_interfaces_changed_race() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>("interfaces_changed_race")
        .context("failed to create netstack environment")?;
    // NB: This test takes more than 100 iterations to exercise the flake, but
    // we limit the load in CQ.
    for _ in 0..100 {
        let netstack = env
            .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
            .context("failed to connect to netstack")?;
        let ep = sandbox
            // We don't need to run variants for this test, all we care about is
            // the Netstack race. Use NetworkDevice because it's lighter weight.
            .create_endpoint::<netemul::NetworkDevice, _>("ep")
            .await
            .context("failed to create fixed ep")?
            .into_interface_in_environment(&env)
            .await
            .context("failed to install in environment")?;

        const ADDR: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.1);

        // Bring the link up, enable the interface, and add an IP address
        // "non-sequentially" (as much as possible) to cause races in Netstack
        // when reporting events.
        let ((), (), ()) = futures::future::try_join3(
            ep.set_link_up(true).map(|r| r.context("failed to bring link up")),
            ep.enable_interface().map(|r| r.context("failed to enable interface")),
            ep.add_ip_addr(fidl_fuchsia_net::Subnet { addr: ADDR, prefix_len: 24 })
                .map(|r| r.context("failed to add address")),
        )
        .await?;

        let id = ep.id();

        let () = futures::stream::try_unfold(
            (netstack.take_event_stream(), false, false, false),
            |(mut event_stream, present, up, has_addr)| async move {
                let fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces } =
                    event_stream
                        .try_next()
                        .await
                        .context("failed to fetch next event")?
                        .ok_or_else(|| {
                            anyhow::anyhow!("Netstack event stream ended unexpectedly")
                        })?;
                let (new_present, new_up, new_has_addr) =
                    if let Some(iface) = interfaces.iter().find(|i| u64::from(i.id) == id) {
                        (
                            true,
                            iface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP != 0,
                            iface.addr == ADDR,
                        )
                    } else {
                        (false, false, false)
                    };
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
                Result::Ok(if new_present && new_up && new_has_addr {
                    // We got everything we wanted, end the stream.
                    None
                } else {
                    // Continue folding with the new state.
                    Some(((), (event_stream, new_present, new_up, new_has_addr)))
                })
            },
        )
        .try_collect()
        .await?;
    }
    Ok(())
}

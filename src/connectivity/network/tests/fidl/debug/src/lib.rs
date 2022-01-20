// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use futures::{FutureExt as _, TryStreamExt as _};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use std::collections::HashMap;

async fn get_loopback_id(realm: &netemul::TestRealm<'_>) -> u64 {
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    let stream = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
        .expect("create watcher event stream");

    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(stream, HashMap::new())
        .await
        .expect("get starting state");

    let mut interfaces = interfaces.into_values();
    let fidl_fuchsia_net_interfaces_ext::Properties {
        id,
        name: _,
        device_class: _,
        online: _,
        addresses: _,
        has_default_ipv4_route: _,
        has_default_ipv6_route: _,
    } = interfaces.next().expect("interface properties map unexpectedly does not include loopback");
    assert_eq!(interfaces.next(), None);

    id
}

#[fuchsia::test]
async fn get_admin_unknown() {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm =
        sandbox.create_netstack_realm::<Netstack2, _>("get_admin_unknown").expect("create realm");

    let id = get_loopback_id(&realm).await;

    let debug_interfaces = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect("connect to protocol");

    // Request unknown NIC ID, expect request channel to be closed.
    let (admin_control, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create proxy");
    let () = debug_interfaces.get_admin(id + 1, server_end).expect("get admin failed");
    assert_matches::assert_matches!(
        admin_control.take_event_stream().try_collect::<Vec<_>>().await.as_ref().map(Vec::as_slice),
        // TODO(https://fxbug.dev/8018): Sending epitaphs not supported in Go.
        Ok([])
    );
}

#[fuchsia::test]
async fn get_mac() {
    // fuchsia.net.tun is not exposed by netemul realms.
    let tun_control =
        fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_net_tun::ControlMarker>()
            .expect("connect to protocol");

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>("get_mac").expect("create realm");

    let loopback_id = get_loopback_id(&realm).await;

    let (tun_device, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DeviceMarker>()
            .expect("create proxy");
    let () = tun_control
        .create_device(fidl_fuchsia_net_tun::DeviceConfig::EMPTY, server_end)
        .expect("create tun device");
    let (network_device, server_end) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::DeviceMarker>()
            .expect("create endpoints");
    let () = tun_device.get_device(server_end).expect("get device");

    let (admin_device_control, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let installer = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");
    let () = installer.install_device(network_device, server_end).expect("install device");

    let (tun_port, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::PortMarker>().expect("create proxy");
    // At the time of writing, netstack only supports dual-mode devices.
    const IP_FRAME_TYPES: [fidl_fuchsia_hardware_network::FrameType; 2] = [
        fidl_fuchsia_hardware_network::FrameType::Ipv4,
        fidl_fuchsia_hardware_network::FrameType::Ipv6,
    ];
    let () = tun_device
        .add_port(
            fidl_fuchsia_net_tun::DevicePortConfig {
                base: Some(fidl_fuchsia_net_tun::BasePortConfig {
                    id: Some(7), // Arbitrary nonzero to avoid masking default value assumptions.
                    rx_types: Some(IP_FRAME_TYPES.to_vec()),
                    tx_types: Some(
                        IP_FRAME_TYPES
                            .iter()
                            .copied()
                            .map(|type_| fidl_fuchsia_hardware_network::FrameTypeSupport {
                                type_,
                                features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                                supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
                            })
                            .collect(),
                    ),
                    mtu: Some(netemul::DEFAULT_MTU.into()),
                    ..fidl_fuchsia_net_tun::BasePortConfig::EMPTY
                }),
                ..fidl_fuchsia_net_tun::DevicePortConfig::EMPTY
            },
            server_end,
        )
        .expect("add port");

    let () = tun_port.set_online(false).await.expect("set online");

    let (network_port, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
            .expect("create endpoints");
    let () = tun_port.get_port(server_end).expect("get port");
    let fidl_fuchsia_hardware_network::PortInfo { id, .. } =
        network_port.get_info().await.expect("get info");
    let mut port_id = id.expect("port id");

    let (admin_control, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create proxy");

    let () = admin_device_control
        .create_interface(
            &mut port_id,
            server_end,
            fidl_fuchsia_net_interfaces_admin::Options {
                name: Some("ihazmac?".to_string()),
                ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
            },
        )
        .expect("create interface");

    let virtual_id = admin_control.get_id().await.expect("get id");

    let debug_interfaces = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect("connect to protocol");

    // Box<T> -> T.
    //
    // This helper works around the inability to match on `Box`. See
    // https://doc.rust-lang.org/beta/unstable-book/language-features/box-patterns.html.
    let get_mac = |id| {
        debug_interfaces
            .get_mac(id)
            .map(|result| result.map(|result| result.map(|option| option.map(|box_| *box_))))
    };

    // Loopback has the all-zero MAC address.
    assert_matches::assert_matches!(
        get_mac(loopback_id).await,
        Ok(Ok(Some(fidl_fuchsia_net::MacAddress { octets: [0, 0, 0, 0, 0, 0] })))
    );
    // Virtual interfaces do not have MAC addresses.
    assert_matches::assert_matches!(get_mac(virtual_id).await, Ok(Ok(None)));
    // Unknown NIC ID produces an error.
    assert_matches::assert_matches!(
        get_mac(virtual_id + 1).await,
        Ok(Err(fidl_fuchsia_net_debug::InterfacesGetMacError::NotFound))
    );
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for interacting with devices during integration tests.

use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_tun as fnet_tun;

/// Create a Tun device, returning handles to the created
/// `fuchsia.net.tun/Device` and the underlying network device.
pub fn create_tun_device(
) -> (fnet_tun::DeviceProxy, fidl::endpoints::ClientEnd<fhardware_network::DeviceMarker>) {
    let tun_ctl = fuchsia_component::client::connect_to_protocol::<fnet_tun::ControlMarker>()
        .expect("connect to protocol");
    let (tun_dev, tun_dev_server_end) =
        fidl::endpoints::create_proxy::<fnet_tun::DeviceMarker>().expect("create proxy");
    tun_ctl
        .create_device(fnet_tun::DeviceConfig::EMPTY, tun_dev_server_end)
        .expect("create tun device");
    let (netdevice_client_end, netdevice_server_end) =
        fidl::endpoints::create_endpoints::<fhardware_network::DeviceMarker>()
            .expect("create endpoints");
    tun_dev.get_device(netdevice_server_end).expect("get device");
    (tun_dev, netdevice_client_end)
}

/// Install the given network device into the test realm's networking stack,
/// returning the created `fuchsia.net.interfaces.admin/DeviceControl` handle.
pub fn install_device(
    realm: &netemul::TestRealm<'_>,
    device: fidl::endpoints::ClientEnd<fhardware_network::DeviceMarker>,
) -> fnet_interfaces_admin::DeviceControlProxy {
    let (admin_device_control, server_end) =
        fidl::endpoints::create_proxy::<fnet_interfaces_admin::DeviceControlMarker>()
            .expect("create proxy");
    let installer = realm
        .connect_to_protocol::<fnet_interfaces_admin::InstallerMarker>()
        .expect("connect to protocol");
    installer.install_device(device, server_end).expect("install device");
    admin_device_control
}

/// Create a port on the given Tun device, returning handles to the created
/// `fuchsia.net.tun/Port` and the underlying network port.
pub async fn create_tun_port(
    tun_device: &fnet_tun::DeviceProxy,
    id: Option<u8>,
) -> (fnet_tun::PortProxy, fhardware_network::PortProxy) {
    let (port, server_end) =
        fidl::endpoints::create_proxy::<fnet_tun::PortMarker>().expect("create proxy");
    // At the time of writing, netstack only supports dual-mode devices.
    const IP_FRAME_TYPES: [fhardware_network::FrameType; 2] =
        [fhardware_network::FrameType::Ipv4, fhardware_network::FrameType::Ipv6];
    tun_device
        .add_port(
            fnet_tun::DevicePortConfig {
                base: Some(fnet_tun::BasePortConfig {
                    id: id,
                    rx_types: Some(IP_FRAME_TYPES.to_vec()),
                    tx_types: Some(
                        IP_FRAME_TYPES
                            .iter()
                            .copied()
                            .map(|type_| fhardware_network::FrameTypeSupport {
                                type_,
                                features: fhardware_network::FRAME_FEATURES_RAW,
                                supported_flags: fhardware_network::TxFlags::empty(),
                            })
                            .collect(),
                    ),
                    mtu: Some(netemul::DEFAULT_MTU.into()),
                    ..fnet_tun::BasePortConfig::EMPTY
                }),
                ..fnet_tun::DevicePortConfig::EMPTY
            },
            server_end,
        )
        .expect("add port");

    let (network_port, server_end) =
        fidl::endpoints::create_proxy::<fhardware_network::PortMarker>().expect("create endpoints");
    port.get_port(server_end).expect("get port");

    (port, network_port)
}

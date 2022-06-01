// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for interacting with devices during integration tests.

/// Create a Tun device, returning handles to the created
/// `fuchsia.net.tun/Device` and the underlying network device.
pub fn create_tun_device() -> (
    fidl_fuchsia_net_tun::DeviceProxy,
    fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_network::DeviceMarker>,
) {
    let tun_ctl =
        fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_net_tun::ControlMarker>()
            .expect("connect to protocol");
    let (tun_dev, tun_dev_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DeviceMarker>()
            .expect("create proxy");
    let () = tun_ctl
        .create_device(fidl_fuchsia_net_tun::DeviceConfig::EMPTY, tun_dev_server_end)
        .expect("create tun device");
    let (netdevice_client_end, netdevice_server_end) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::DeviceMarker>()
            .expect("create endpoints");
    let () = tun_dev.get_device(netdevice_server_end).expect("get device");
    (tun_dev, netdevice_client_end)
}

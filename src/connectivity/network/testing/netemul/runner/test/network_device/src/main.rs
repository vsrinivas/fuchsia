// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_device::ControllerMarker;
use fidl_fuchsia_netemul_network::{
    DeviceProxy_Marker, EndpointBacking, EndpointConfig, EndpointManagerMarker,
    NetworkContextMarker,
};
use fuchsia_component::client;
use fuchsia_zircon as zx;

use anyhow::Context as _;

const NETDEV_FAKE_TOPO_PATH_ROOT: &str = "/netemul";

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let name = "test";

    // Create a network device backed endpoint.
    let netctx = client::connect_to_protocol::<NetworkContextMarker>()?;
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()
        .context("create endpoint manager proxy endpoints")?;
    let () = netctx.get_endpoint_manager(epm_server_end)?;
    let mut cfg = EndpointConfig { backing: EndpointBacking::NetworkDevice, mac: None, mtu: 1500 };
    let (status, ep) =
        epm.create_endpoint(name, &mut cfg).await.context("create endpoint request")?;
    let status = zx::Status::from_raw(status);
    if status != zx::Status::OK {
        return Err(anyhow::anyhow!("failed to create endpoint with status = {}", status));
    }
    let ep = match ep {
        Some(ep) => ep.into_proxy()?,
        None => panic!("expected an endpoint since create_endpoint returned with OK status"),
    };

    // Expect the device to implement `fuchsia.device/Controller.GetTopologicalPath`.
    let (dev_proxy, req) = fidl::endpoints::create_proxy::<DeviceProxy_Marker>()
        .context("create device proxy endpoints")?;
    let () = ep.get_proxy_(req).context("get device proxy")?;

    let (controller, server_end) =
        fidl::endpoints::create_proxy::<ControllerMarker>().context("proxy create")?;
    let () = dev_proxy.serve_device(server_end.into_channel()).context("serve device")?;
    let path = controller
        .get_topological_path()
        .await
        .context("get topological path request")?
        .map_err(zx::Status::from_raw)
        .context("get topological path")?;
    assert_eq!(path, format!("{}/{}", NETDEV_FAKE_TOPO_PATH_ROOT, name));
    Ok(())
}

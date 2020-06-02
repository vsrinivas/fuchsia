// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_netstack as netstack;
use fuchsia_async as fasync;

use anyhow::Context as _;
use futures::future::{self, FutureExt as _};
use futures::stream::StreamExt as _;
use net_declare::fidl_ip;

use crate::environments::*;
use crate::*;

/// The URL to NetCfg for use in a netemul environment.
///
/// Note, netcfg.cmx must never be used in a Netemul environment as it breaks hermeticity.
const NETCFG_PKG_URL: &str = "fuchsia-pkg://fuchsia.com/netcfg#meta/netcfg_netemul.cmx";

/// Test that NetCfg discovers a newly added device and it adds the device to the
/// Netstack.
#[fasync::run_singlethreaded(test)]
async fn test_oir() -> Result {
    let name = "test_oir";
    let sandbox = TestSandbox::new().context("create sandbox")?;
    // Create an environment with the LookupAdmin service as NetCfg tries to configure
    // it. NetCfg will fail if it can't send the LookupAdmin a request.
    let environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(name, &[KnownServices::LookupAdmin])
        .context("create netstack environment")?;

    // Start NetCfg.
    let launcher = environment.get_launcher().context("get launcher")?;
    let mut netcfg = fuchsia_component::client::launch(&launcher, NETCFG_PKG_URL.to_string(), None)
        .context("launch netcfg")?;

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
            if let Some(netstack::NetInterface { id, .. }) =
                interfaces.iter().find(|i| i.addr != fidl_ip!(127.0.0.1))
            {
                return future::ok(Some(*id));
            }

            future::ok(None)
        })
        .map(|r| r.context("getting next OnInterfaceChanged event"));
    let mut wait_for_interface = wait_for_interface
        .try_next()
        .on_timeout(DEFAULT_INTERFACE_UP_EVENT_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!(
                "timed out waiting for OnInterfaceseChanged event with a non-loopback interface"
            ))
        })
        .fuse();
    let mut wait_for_netcfg = netcfg.wait().fuse();
    let _id = futures::select! {
        wait_for_interface_res = wait_for_interface => {
            wait_for_interface_res?.ok_or(anyhow::anyhow!("Netstack event stream unexpectedly ended"))
        }
        wait_for_netcfg_res = wait_for_netcfg => {
            Err(anyhow::anyhow!("NetCfg unexpectedly exited with exit status = {:?}", wait_for_netcfg_res?))
        }
    }?;

    environment.remove_virtual_device(endpoint_mount_path)
}

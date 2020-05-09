// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;

use crate::environments::*;
use crate::Result;

/// Launches a new netstack with the endpoint and returns the IPv6 addresses
/// initially assigned to it.
///
/// If `run_netstack_and_get_ipv6_addrs_for_endpoint` returns successfully, it
/// is guaranteed that the launched netstack has been terminated. Note, if
/// `run_netstack_and_get_ipv6_addrs_for_endpoint` does not return successfully,
/// the launched netstack will still be terminated, but no guarantees are made
/// about when that will happen.
async fn run_netstack_and_get_ipv6_addrs_for_endpoint<N: Netstack>(
    endpoint: &TestEndpoint<'_>,
    launcher: &fidl_fuchsia_sys::LauncherProxy,
    name: String,
) -> Result<Vec<fidl_fuchsia_net::Subnet>> {
    // Launch the netstack service.

    let mut app = fuchsia_component::client::AppBuilder::new(N::VERSION.get_url())
        .spawn(launcher)
        .context("failed to spawn netstack")?;
    let netstack = app
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack service")?;

    // Add the device and get its interface state from netstack.
    // TODO(48907) Support Network Device. This helper fn should use stack.fidl
    // and be agnostic over interface type.
    let id = netstack
        .add_ethernet_device(
            &name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: name[..fidl_fuchsia_posix_socket::INTERFACE_NAME_LENGTH.into()].to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
                ip_address_config: fidl_fuchsia_netstack::IpAddressConfig::Dhcp(true),
            },
            endpoint
                .get_ethernet()
                .await
                .context("add_ethernet_device requires an Ethernet endpoint")?,
        )
        .await
        .context("failed to add ethernet device")?;
    let interface = netstack
        .get_interfaces2()
        .await
        .context("failed to get interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet device"))?;

    // Kill the netstack.
    //
    // Note, simply dropping `component_controller` would also kill the netstack
    // but we explicitly kill it and wait for the terminated event before
    // proceeding.
    let () = app.kill().context("failed to kill app")?;
    let _exit_status = app.wait().await.context("failed to observe netstack termination")?;

    Ok(interface.ipv6addrs)
}

/// Test that across netstack runs, a device will initially be assigned the same
/// IPv6 addresses.
#[fuchsia_async::run_singlethreaded(test)]
async fn consistent_initial_ipv6_addrs() -> Result {
    let name = "consistent_initial_ipv6_addrs";
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_environment(name, &[KnownServices::SecureStash])
        .context("failed to create environment")?;
    let launcher = env.get_launcher().context("failed to get launcher")?;
    let endpoint = sandbox.create_endpoint(name).await.context("failed to create endpoint")?;

    // Make sure netstack uses the same addresses across runs for a device.
    let first_run_addrs = run_netstack_and_get_ipv6_addrs_for_endpoint::<Netstack2>(
        &endpoint,
        &launcher,
        name.to_string(),
    )
    .await?;
    let second_run_addrs = run_netstack_and_get_ipv6_addrs_for_endpoint::<Netstack2>(
        &endpoint,
        &launcher,
        name.to_string(),
    )
    .await?;
    assert_eq!(first_run_addrs, second_run_addrs);

    Ok(())
}

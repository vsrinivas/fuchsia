// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_netemul_guest::{
        CommandListenerMarker, GuestDiscoveryMarker, GuestInteractionMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::io::AsyncReadExt as _,
    netemul_guest_lib::wait_for_command_completion,
};

async fn test_multihop_ping() -> Result<(), Error> {
    // Configure the Debian guest VM acting as a router.
    let guest_discovery_service = client::connect_to_protocol::<GuestDiscoveryMarker>()?;
    let (router_gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest_1", gis_ch)?;

    let (stdout_0, stdout_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
    let (stderr_0, stderr_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
        .context("Failed to create CommandListener ends")?;

    let () = router_gis.execute_command(
        "/bin/sh -c /root/input/setup_linux_router.sh",
        &mut [].iter_mut(),
        None,
        Some(stdout_1),
        Some(stderr_1),
        server_end,
    )?;

    let mut guest_stdout = Vec::new();
    let mut stdout_socket = fasync::Socket::from_socket(stdout_0)?;

    let mut guest_stderr = Vec::new();
    let mut stderr_socket = fasync::Socket::from_socket(stderr_0)?;

    let (exit, stdout_bytes, stderr_bytes) = futures::future::join3(
        wait_for_command_completion(client_proxy.take_event_stream(), None),
        stdout_socket.read_to_end(&mut guest_stdout),
        stderr_socket.read_to_end(&mut guest_stderr),
    )
    .await;

    let () = exit.with_context(|| {
        format!(
            "Failed to configure router\nStdout:\n{}\nStderr:\n{}",
            String::from_utf8_lossy(&guest_stdout),
            String::from_utf8_lossy(&guest_stderr)
        )
    })?;
    let _: usize = stdout_bytes.context("read stdout")?;
    let _: usize = stderr_bytes.context("read stderr")?;

    // Configure the Debian guest VM acting as a client endpoint.
    let (client_gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest_2", gis_ch)?;

    let (stdout_0, stdout_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
    let (stderr_0, stderr_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
        .context("Failed to create CommandListener ends")?;

    let () = client_gis.execute_command(
        "/bin/sh -c /root/input/setup_linux_client.sh",
        &mut [].iter_mut(),
        None,
        Some(stdout_1),
        Some(stderr_1),
        server_end,
    )?;

    let mut guest_stdout = Vec::new();
    let mut stdout_socket = fasync::Socket::from_socket(stdout_0)?;

    let mut guest_stderr = Vec::new();
    let mut stderr_socket = fasync::Socket::from_socket(stderr_0)?;

    let (exit, stdout_bytes, stderr_bytes) = futures::future::join3(
        wait_for_command_completion(client_proxy.take_event_stream(), None),
        stdout_socket.read_to_end(&mut guest_stdout),
        stderr_socket.read_to_end(&mut guest_stderr),
    )
    .await;

    let () = exit.with_context(|| {
        format!(
            "Failed to configure client\nStdout:\n{}\nStderr:\n{}",
            String::from_utf8_lossy(&guest_stdout),
            String::from_utf8_lossy(&guest_stderr)
        )
    })?;
    let _: usize = stdout_bytes.context("read stdout")?;
    let _: usize = stderr_bytes.context("read stderr")?;

    let (stdout_0, stdout_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
    let (stderr_0, stderr_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    // Ping from the Linux client through the Linux router to the Fuchsia endpoint.
    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
        .context("Failed to create CommandListener ends")?;

    let () = client_gis.execute_command(
        "/bin/ping -c 1 192.168.0.2",
        &mut [].iter_mut(),
        None,
        Some(stdout_1),
        Some(stderr_1),
        server_end,
    )?;

    let mut guest_stdout = Vec::new();
    let mut stdout_socket = fasync::Socket::from_socket(stdout_0)?;

    let mut guest_stderr = Vec::new();
    let mut stderr_socket = fasync::Socket::from_socket(stderr_0)?;

    let (exit, stdout_bytes, stderr_bytes) = futures::future::join3(
        wait_for_command_completion(client_proxy.take_event_stream(), None),
        stdout_socket.read_to_end(&mut guest_stdout),
        stderr_socket.read_to_end(&mut guest_stderr),
    )
    .await;

    let () = exit.with_context(|| {
        format!(
            "Failed to ping router from client\nStdout:\n{}\nStderr:\n{}",
            String::from_utf8_lossy(&guest_stdout),
            String::from_utf8_lossy(&guest_stderr)
        )
    })?;
    let _: usize = stdout_bytes.context("read stdout")?;
    let _: usize = stderr_bytes.context("read stderr")?;

    return Ok(());
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    test_multihop_ping().await
}

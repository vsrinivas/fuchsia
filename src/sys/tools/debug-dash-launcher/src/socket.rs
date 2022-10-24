// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, format_err, Error};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_dash::LauncherError;
use fidl_fuchsia_hardware_pty as pty;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures::future::{AbortHandle, Abortable};
use futures::io::{ReadHalf, WriteHalf};
use futures::prelude::*;

async fn dash_to_client_loop(
    server: pty::DeviceProxy,
    epair: zx::EventPair,
    mut write_to_client: WriteHalf<fasync::Socket>,
) -> Result<(), Error> {
    let readable =
        zx::Signals::from_bits(fidl_fuchsia_device::DeviceSignal::READABLE.bits()).unwrap();
    let hangup = zx::Signals::from_bits(fidl_fuchsia_device::DeviceSignal::HANGUP.bits()).unwrap();

    loop {
        let signals = fasync::OnSignals::new(&epair, readable | hangup).await?;
        if signals.contains(readable) {
            let bytes = server
                .read(fio::MAX_BUF)
                .await?
                .map_err(|e| format_err!("cannot read from PTY: {}", zx::Status::from_raw(e)))?;
            write_to_client.write_all(&bytes).await?;
            write_to_client.flush().await?;
        }
        if signals.contains(hangup) {
            return Ok(());
        }
    }
}

async fn client_to_dash_loop(
    server: pty::DeviceProxy,
    mut read_from_client: ReadHalf<fasync::Socket>,
) -> Result<(), Error> {
    let mut buf = [0u8; fio::MAX_BUF as usize];
    loop {
        let bytes_read = read_from_client.read(&mut buf).await?;
        if bytes_read > 0 {
            server
                .write(&buf[..bytes_read])
                .await?
                .map_err(|e| format_err!("cannot write to PTY: {}", zx::Status::from_raw(e)))?;
        } else {
            // The client has closed their side of the socket.
            break Ok(());
        }
    }
}

pub async fn spawn_pty_forwarder(
    socket: zx::Socket,
) -> Result<ClientEnd<pty::DeviceMarker>, LauncherError> {
    let server = connect_to_protocol::<pty::DeviceMarker>().map_err(|_| LauncherError::Pty)?;

    // Open a new controlling client and make it active.
    let (stdio, to_pty_stdio) =
        fidl::endpoints::create_endpoints::<pty::DeviceMarker>().map_err(|_| LauncherError::Pty)?;
    let status_client =
        server.open_client(0, to_pty_stdio).await.map_err(|_| LauncherError::Pty)?;
    zx::Status::ok(status_client).map_err(|_| LauncherError::Pty)?;

    // Assume that the terminal is 1024 x 768. When using a socket, we cannot find out the
    // terminal dimensions.
    let status_window_size = server
        .set_window_size(&mut pty::WindowSize { width: 1024, height: 768 })
        .await
        .map_err(|_| LauncherError::Pty)?;

    zx::Status::ok(status_window_size).map_err(|_| LauncherError::Pty)?;

    let pty::DeviceDescribeResponse { event, .. } =
        server.describe().await.map_err(|_| LauncherError::Pty)?;
    let epair = event.ok_or(LauncherError::Pty)?;

    let socket = fasync::Socket::from_socket(socket).unwrap();
    let (read_from_client, write_to_client) = socket.split();
    let server_for_dash_output = std::clone::Clone::clone(&server);

    let (dash_to_client_abort_handle, dash_to_client_abort_reg) = AbortHandle::new_pair();
    let (client_to_dash_abort_handle, client_to_dash_abort_reg) = AbortHandle::new_pair();

    // Set up two futures for bidirectional data transfer: dash process (PTY) <-> client (socket).
    // When either direction fails, both futures are aborted.
    let dash_to_client_fut = Abortable::new(
        async move {
            let _ = dash_to_client_loop(server_for_dash_output, epair, write_to_client).await;

            // Abort the other future.
            client_to_dash_abort_handle.abort();
        },
        dash_to_client_abort_reg,
    );

    let client_to_dash_fut = Abortable::new(
        async move {
            let _ = client_to_dash_loop(server, read_from_client).await;

            // Abort the other future.
            dash_to_client_abort_handle.abort();
        },
        client_to_dash_abort_reg,
    );

    fasync::Task::spawn(dash_to_client_fut.map(|_| ())).detach();
    fasync::Task::spawn(client_to_dash_fut.map(|_| ())).detach();

    Ok(stdio)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    async fn pty_forwarder() {
        let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

        let pty = spawn_pty_forwarder(stdio_server).await.unwrap();
        let pty = pty.into_proxy().unwrap();
        let mut stdio = fasync::Socket::from_socket(stdio).unwrap();
        let mut buf = [0u8, 0u8];

        pty.write("$ ".as_bytes()).await.unwrap().unwrap();
        stdio.read_exact(&mut buf).await.unwrap();
        assert_eq!(buf, "$ ".as_bytes());

        let pty::DeviceDescribeResponse { event, .. } = pty.describe().await.unwrap();
        let epair = event.unwrap();
        let readable =
            zx::Signals::from_bits(fidl_fuchsia_device::DeviceSignal::READABLE.bits()).unwrap();

        stdio.write_all("ls".as_bytes()).await.unwrap();
        fasync::OnSignals::new(&epair, readable).await.unwrap();
        let buf = pty.read(2).await.unwrap().map_err(|e| zx::Status::from_raw(e)).unwrap();
        assert_eq!(buf, "ls".as_bytes());
    }
}

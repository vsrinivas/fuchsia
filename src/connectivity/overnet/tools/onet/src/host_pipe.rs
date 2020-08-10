// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    argh::FromArgs,
    fidl_fuchsia_overnet::MeshControllerProxyInterface,
    futures::{future::try_join, prelude::*},
    parking_lot::Mutex,
    std::io::{Read, Write},
    std::sync::Arc,
};

#[derive(FromArgs)]
#[argh(subcommand, name = "host-pipe")]
/// Use stdin/stdout as a link to another overnet instance
pub struct HostPipe {}

async fn copy_stdin_to_socket(
    mut tx_socket: futures::io::WriteHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    let (mut tx_stdin, mut rx_stdin) = futures::channel::mpsc::channel::<Vec<u8>>(2);
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 1024];
            let mut stdin = std::io::stdin();
            loop {
                let n = stdin.read(&mut buf)?;
                if n == 0 {
                    return Ok(());
                }
                let buf = &buf[..n];
                futures::executor::block_on(tx_stdin.send(buf.to_vec()))?;
            }
        })
        .context("Spawning blocking thread")?;
    while let Some(buf) = rx_stdin.next().await {
        tx_socket.write(buf.as_slice()).await?;
    }
    Ok(())
}

async fn copy_socket_to_stdout(
    mut rx_socket: futures::io::ReadHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    let (mut tx_stdout, rx_stdout) = futures::channel::mpsc::channel::<Vec<u8>>(2);
    let rx_stdout = Arc::new(Mutex::new(rx_stdout));
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut stdout = std::io::stdout();
            let mut rx_stdout = rx_stdout.lock();
            while let Some(buf) = futures::executor::block_on(rx_stdout.next()) {
                let mut buf = buf.as_slice();
                loop {
                    let n = stdout.write(buf)?;
                    if n == buf.len() {
                        stdout.flush()?;
                        break;
                    }
                    buf = &buf[n..];
                }
            }
            Ok(())
        })
        .context("Spawning blocking thread")?;
    let mut buf = [0u8; 1024];
    loop {
        let n = rx_socket.read(&mut buf).await?;
        tx_stdout.send((&buf[..n]).to_vec()).await?;
    }
}

pub async fn host_pipe() -> Result<(), Error> {
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    let (rx_socket, tx_socket) = futures::AsyncReadExt::split(local_socket);
    hoist::connect_as_mesh_controller()?
        .attach_socket_link(remote_socket, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;
    try_join(copy_socket_to_stdout(rx_socket), copy_stdin_to_socket(tx_socket)).await?;

    Ok(())
}

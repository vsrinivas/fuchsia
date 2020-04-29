// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::SOCKET;
use crate::ssh::build_ssh_command;
use crate::target::{Target, TargetAddr};
use std::collections::HashSet;
use std::io::{Read, Write};
use std::process::{Child, Stdio};

use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_overnet::MeshControllerProxyInterface;
use futures::io::{AsyncReadExt, AsyncWriteExt};

pub async fn start_ascendd() {
    log::info!("Starting ascendd");
    hoist::spawn(async move {
        ascendd_lib::run_ascendd(ascendd_lib::Opt {
            sockpath: Some(SOCKET.to_string()),
            ..Default::default()
        })
        .await
        .unwrap();
    });
}

pub async fn connect_to_onet(target: &Target, addrs: HashSet<TargetAddr>) -> Result<Child, Error> {
    log::info!("Connecting to target: {}", target.nodename);
    let mut process = build_ssh_command(addrs, vec!["onet", "host-pipe"])
        .await?
        .stdout(Stdio::piped())
        .stdin(Stdio::piped())
        .spawn()
        .context("running target overnet pipe")?;
    let (pipe_rx, pipe_tx) =
        futures::AsyncReadExt::split(overnet_pipe().context("creating local overnet pipe")?);
    futures::future::try_join(
        copy_target_stdout_to_pipe(
            process.stdout.take().ok_or(anyhow!("unable to get stdout from target pipe"))?,
            pipe_tx,
        ),
        copy_pipe_to_target_stdin(
            pipe_rx,
            process.stdin.take().ok_or(anyhow!("unable to get stdin from target pipe"))?,
        ),
    )
    .await?;

    Ok(process)
}

pub fn overnet_pipe() -> Result<fidl::AsyncSocket, Error> {
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    hoist::connect_as_mesh_controller()?
        .attach_socket_link(remote_socket, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;

    Ok(local_socket)
}

async fn copy_target_stdout_to_pipe(
    mut stdout_pipe: std::process::ChildStdout,
    mut pipe_tx: futures::io::WriteHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 1024];
            loop {
                let n = stdout_pipe.read(&mut buf)?;
                if n == 0 {
                    break;
                }
                futures::executor::block_on(pipe_tx.write_all(&buf[..n]))?;
            }

            Ok(())
        })
        .context("spawning blocking thread")?;

    Ok(())
}

async fn copy_pipe_to_target_stdin(
    mut pipe_rx: futures::io::ReadHalf<fidl::AsyncSocket>,
    mut stdin_pipe: std::process::ChildStdin,
) -> Result<(), Error> {
    // Spawns new thread to avoid blocking executor on stdin_pipe and stdout_pipe.
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 1024];
            loop {
                let n = match futures::executor::block_on(pipe_rx.read(&mut buf))? {
                    0 => break,
                    n => n,
                };
                stdin_pipe.write_all(&buf[..n])?;
            }
            Ok(())
        })
        .context("spawning blocking thread")?;

    Ok(())
}

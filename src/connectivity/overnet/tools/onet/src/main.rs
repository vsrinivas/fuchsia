// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    clap::{App, SubCommand},
    failure::Error,
    futures::{future::try_join, prelude::*},
    hoist::OvernetProxyInterface,
    std::io::{Read, Write},
};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("onet")
        .version("0.1.0")
        .about("Overnet debug tool")
        .author("Fuchsia Team")
        .subcommands(vec![
            SubCommand::with_name("ls-peers").about("Lists known peer node ids"),
            SubCommand::with_name("host-pipe")
                .about("Use stdin/stdout as a link to another overnet instance"),
        ])
}

async fn ls_peers(svc: impl OvernetProxyInterface) -> Result<(), Error> {
    for peer in svc.list_peers().await? {
        println!("PEER: {:?}", peer);
    }
    Ok(())
}

async fn copy_stdin_to_socket(
    mut tx_socket: futures::io::WriteHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    let (mut tx_stdin, mut rx_stdin) = futures::channel::mpsc::channel::<Vec<u8>>(2);
    std::thread::spawn(move || -> Result<(), Error> {
        let mut buf = [0u8; 1024];
        let mut stdin = std::io::stdin();
        loop {
            let n = match stdin.read(&mut buf) {
                Ok(x) => x,
                Err(e) => {
                    log::warn!("Error reading: {}", e);
                    panic!();
                }
            };
            let buf = &buf[..n];
            if n == 0 {
                return Ok(());
            }
            futures::executor::block_on(tx_stdin.send(buf.to_vec()))?;
        }
    });
    while let Some(buf) = rx_stdin.next().await {
        tx_socket.write(buf.as_slice()).await?;
    }
    Ok(())
}

async fn copy_socket_to_stdout(
    mut rx_socket: futures::io::ReadHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    let (mut tx_stdout, mut rx_stdout) = futures::channel::mpsc::channel::<Vec<u8>>(2);
    std::thread::spawn(move || -> Result<(), Error> {
        let mut stdout = std::io::stdout();
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
    });
    let mut buf = [0u8; 1024];
    loop {
        let n = rx_socket.read(&mut buf).await?;
        tx_stdout.send((&buf[..n]).to_vec()).await?;
    }
}

async fn host_pipe(svc: impl OvernetProxyInterface) -> Result<(), Error> {
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    let (rx_socket, tx_socket) = futures::AsyncReadExt::split(local_socket);
    svc.attach_socket_link(remote_socket, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;
    try_join(copy_socket_to_stdout(rx_socket), copy_stdin_to_socket(tx_socket)).await?;

    Ok(())
}

async fn async_main() -> Result<(), Error> {
    let args = app().get_matches();

    let svc = hoist::connect()?;

    match args.subcommand_name() {
        Some("ls-peers") => ls_peers(svc).await,
        Some("host-pipe") => host_pipe(svc).await,
        _ => {
            let _ = app().write_help(&mut std::io::stderr());
            eprintln!("");
            Ok(())
        }
    }
}

fn main() {
    hoist::run(async move {
        if let Err(e) = async_main().await {
            log::warn!("Error: {}", e)
        }
    });
}

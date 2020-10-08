// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    anyhow::Result,
    futures_util::future::FutureExt,
    futures_util::io::{AsyncReadExt, AsyncWriteExt},
};

#[ffx_core::ffx_plugin(
    "zxdb_enabled",
    fidl_fuchsia_debugger::DebugAgentProxy = "core/appmgr:out:fuchsia.debugger.DebugAgent"
)]
pub async fn debug(
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    cmd: ffx_zxdb_plugin_args::DebugCommand,
) -> Result<(), Error> {
    let sdk = ffx_config::get_sdk().await?;
    let zxdb_path = sdk.get_host_tool("zxdb")?;

    // Connect to the debug_agent on the device.
    let (sock_server, sock_client) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).expect("Failed while creating socket");
    debugger_proxy.connect(sock_client).await?;

    let (rx, tx) = fidl::AsyncSocket::from_socket(sock_server)?.split();
    let rx = std::cell::RefCell::new(rx);
    let tx = std::cell::RefCell::new(tx);

    // Create our Unix socket.
    let listener = async_std::os::unix::net::UnixListener::bind(&cmd.socket_location).await?;

    // Start the local debugger.
    std::process::Command::new(zxdb_path)
        .args(&[
            "--unix-connect",
            &cmd.socket_location,
            "--symbol-server",
            "gs://fuchsia-artifacts-release/debug",
        ])
        .spawn()
        .expect("zxdb failed to start");

    loop {
        // Wait for a connection on the unix socket.
        let (socket, _) = listener.accept().await?;
        let (mut host_rx, mut host_tx) = socket.split();
        let mut fuchsia_rx = rx.borrow_mut();
        let mut fuchsia_tx = tx.borrow_mut();

        // Reading from the ZXDB socket and writing to the Fuchsia channel.
        let host_socket_read = async move {
            let mut buffer = [0; 4096];
            loop {
                let n = host_rx.read(&mut buffer[..]).await?;
                if n == 0 {
                    return Ok(()) as Result<(), Error>;
                }
                let mut ofs = 0;
                while ofs != n {
                    let wrote = fuchsia_tx.write(&buffer[ofs..n]).await?;
                    ofs += wrote;
                    if wrote == 0 {
                        return Ok(()) as Result<(), Error>;
                    }
                }
            }
        };
        let mut host_socket_read = Box::pin(host_socket_read.fuse());

        // Reading from the Fuchsia socket and writing to the ZXDB channel.
        let host_socket_write = async move {
            let mut buffer = [0; 4096];
            loop {
                let n = fuchsia_rx.read(&mut buffer).await?;
                let mut ofs = 0;
                while ofs != n {
                    let wrote = host_tx.write(&buffer[ofs..n]).await?;
                    ofs += wrote;
                    if wrote == 0 {
                        return Ok(()) as Result<(), Error>;
                    }
                }
            }
        };
        let mut host_socket_write = Box::pin(host_socket_write.fuse());

        futures::select! {
            read_res = host_socket_read => read_res?,
            write_res = host_socket_write => write_res?,
        };
    }
}

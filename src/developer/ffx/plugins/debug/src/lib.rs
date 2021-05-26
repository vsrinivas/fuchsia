// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    async_net::unix::UnixListener,
    futures_util::future::FutureExt,
    futures_util::io::{AsyncReadExt, AsyncWriteExt},
    std::fs,
};

#[ffx_core::ffx_plugin(
    "debug_enabled",
    fidl_fuchsia_debugger::DebugAgentProxy = "core/appmgr:out:fuchsia.debugger.DebugAgent"
)]
pub async fn debug(
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    cmd: ffx_debug_plugin_args::DebugCommand,
) -> Result<(), Error> {
    let result = execute_debug(debugger_proxy, &cmd).await;
    // Removes the Unix socket file to be able to connect again.
    let _ = fs::remove_file(&cmd.socket_location);
    result
}

pub async fn execute_debug(
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    cmd: &ffx_debug_plugin_args::DebugCommand,
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
    let listener = UnixListener::bind(&cmd.socket_location)?;

    // Start the local debugger. It will connect to the Unix socket.
    let child = std::process::Command::new(&zxdb_path)
        .args(&[
            "--unix-connect",
            &cmd.socket_location,
            "--symbol-server",
            "gs://fuchsia-artifacts-release/debug",
            "--quit-agent-on-exit",
        ])
        .spawn();
    if let Err(error) = child {
        return Err(anyhow!("Can't launch {:?}: {:?}", zxdb_path, error));
    }

    // Wait for a connection on the unix socket (connection from zxdb).
    let (socket, _) = listener.accept().await?;

    let (mut zxdb_rx, mut zxdb_tx) = socket.split();
    let mut debug_agent_rx = rx.borrow_mut();
    let mut debug_agent_tx = tx.borrow_mut();

    // Reading from zxdb (using the Unix socket) and writing to the debug agent.
    let zxdb_socket_read = async move {
        let mut buffer = [0; 4096];
        loop {
            let n = zxdb_rx.read(&mut buffer[..]).await?;
            if n == 0 {
                return Ok(()) as Result<(), Error>;
            }
            let mut ofs = 0;
            while ofs != n {
                let wrote = debug_agent_tx.write(&buffer[ofs..n]).await?;
                ofs += wrote;
                if wrote == 0 {
                    return Ok(()) as Result<(), Error>;
                }
            }
        }
    };
    let mut zxdb_socket_read = Box::pin(zxdb_socket_read.fuse());

    // Reading from the debug agent and writing to zxdb (using the Unix socket).
    let zxdb_socket_write = async move {
        let mut buffer = [0; 4096];
        loop {
            let n = debug_agent_rx.read(&mut buffer).await?;
            let mut ofs = 0;
            while ofs != n {
                let wrote = zxdb_tx.write(&buffer[ofs..n]).await?;
                ofs += wrote;
                if wrote == 0 {
                    return Ok(()) as Result<(), Error>;
                }
            }
        }
    };
    let mut zxdb_socket_write = Box::pin(zxdb_socket_write.fuse());

    // ffx zxdb exits when we have an error on the unix socket (either read or write).
    futures::select! {
        read_res = zxdb_socket_read => read_res?,
        write_res = zxdb_socket_write => write_res?,
    };
    return Ok(()) as Result<(), Error>;
}

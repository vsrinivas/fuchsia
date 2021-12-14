// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    async_net::unix::UnixListener,
    fuchsia_zircon_status::Status,
    futures_util::future::FutureExt,
    futures_util::io::{AsyncReadExt, AsyncWriteExt},
    std::path::{Path, PathBuf},
    std::{env, io},
};

/// Represents a connectable socket to the remote debug_agent. It's essentially a FIDL socket and a
/// UNIX socket proxied by us.
pub struct DebugAgentSocket {
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    unix_socket_path: PathBuf,
    unix_socket: UnixListener,
}

impl DebugAgentSocket {
    /// Create a UNIX socket on the host side for zxdb/fidlcat to connect.
    pub fn create(
        debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    ) -> Result<DebugAgentSocket> {
        let (unix_socket_path, unix_socket) = make_temp_unix_socket()?;
        return Ok(DebugAgentSocket { debugger_proxy, unix_socket_path, unix_socket });
    }

    /// The path to the UNIX socket.
    pub fn unix_socket_path(&self) -> &Path {
        &self.unix_socket_path
    }

    /// Create, accept and start forwarding one connection. The call is blocking until the
    /// connection closes, either by the remote debug_agent or by the local zxdb.
    pub async fn forward_one_connection(&self) -> Result<()> {
        // Wait for a connection on the UNIX socket (connection from zxdb).
        // Accept this first, otherwise zxdb will hang forever on connecting.
        let (unix_conn, _) = self.unix_socket.accept().await?;

        // Create a FIDL socket to the debug_agent on the device.
        let (fidl_left, fidl_right) = fidl::Socket::create(fidl::SocketOpts::STREAM)
            .map_err(|s| anyhow!("Failed while creating socket: {}", s))?;
        Status::ok(self.debugger_proxy.connect(fidl_right).await?)?;
        let fidl_conn = fidl::AsyncSocket::from_socket(fidl_left)?;

        let (mut unix_rx, mut unix_tx) = unix_conn.split();
        let (mut fidl_rx, mut fidl_tx) = fidl_conn.split();

        // Forward from UNIX socket to FIDL socket.
        let unix_to_fidl = async {
            let mut buffer = [0; 4096];
            loop {
                let n = unix_rx.read(&mut buffer).await?;
                if n == 0 {
                    return Ok(()) as Result<()>;
                }
                let mut ofs = 0;
                while ofs != n {
                    let wrote = fidl_tx.write(&buffer[ofs..n]).await?;
                    ofs += wrote;
                    if wrote == 0 {
                        return Ok(()) as Result<()>;
                    }
                }
            }
        };

        // Forward from FIDL socket to UNIX socket.
        let fidl_to_unix = async {
            let mut buffer = [0; 4096];
            loop {
                let n = fidl_rx.read(&mut buffer).await?;
                if n == 0 {
                    return Ok(()) as Result<()>;
                }
                let mut ofs = 0;
                while ofs != n {
                    let wrote = unix_tx.write(&buffer[ofs..n]).await?;
                    ofs += wrote;
                    if wrote == 0 {
                        return Ok(()) as Result<()>;
                    }
                }
            }
        };

        // Exit on close or any error.
        futures::select! {
            res = unix_to_fidl.fuse() => res?,
            res = fidl_to_unix.fuse() => res?,
        };

        Ok(())
    }
}

impl Drop for DebugAgentSocket {
    fn drop(&mut self) {
        std::fs::remove_file(&self.unix_socket_path).unwrap_or_default();
    }
}

/// This mimics tempfile::util::create_helper but unfortunately that function is private.
fn make_temp_unix_socket() -> std::io::Result<(PathBuf, UnixListener)> {
    use rand::distributions::{Alphanumeric, DistString};

    let retries = 10;
    let prefix = "debug_agent_";
    let rand_str_length = 6;
    let suffix = ".socket";

    for _ in 0..retries {
        let rand_str = Alphanumeric.sample_string(&mut rand::thread_rng(), rand_str_length);

        let mut path = env::temp_dir().into_os_string();
        path.extend(["/".as_ref(), prefix.as_ref(), rand_str.as_ref(), suffix.as_ref()]);

        match UnixListener::bind(&path) {
            Ok(socket) => return Ok((path.into(), socket)),
            Err(e) => {
                if e.kind() == io::ErrorKind::AlreadyExists {
                    continue;
                } else {
                    return Err(e);
                }
            }
        };
    }

    Err(io::Error::new(io::ErrorKind::AlreadyExists, "cannot create temp unix socket"))
}

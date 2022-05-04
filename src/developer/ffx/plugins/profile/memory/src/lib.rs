// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library that obtains and prints memory digests of a running fuchsia device.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_profile_memory_args::MemoryCommand,
    fidl_fuchsia_memory::MonitorProxy, futures::AsyncReadExt,
};

#[ffx_plugin("ffx_memory", MonitorProxy = "core/appmgr:out:fuchsia.memory.Monitor")]
/// Prints a JSON string containing a memory digest.
pub async fn print_memory_digest(monitor_proxy: MonitorProxy, _cmd: MemoryCommand) -> Result<()> {
    let s = get_digest(monitor_proxy).await?;
    // TODO(231425139): Pretty print the JSON, and print the raw JSON only when "--machine=json" is passed.
    println!("{}", s);
    Ok(())
}

/// Returns a JSON string containing a memory digest obtained via the MonitorProxyInterface.
async fn get_digest(
    monitor_proxy: impl fidl_fuchsia_memory::MonitorProxyInterface,
) -> anyhow::Result<String> {
    // Create a socket.
    let (rx, tx) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;

    // Send one end of the socket to the remote device.
    monitor_proxy.write_json_capture(tx)?;

    // Read all the bytes sent from the other end of the socket.
    let mut rx_async = fidl::AsyncSocket::from_socket(rx)?;
    let mut result = Vec::new();
    let _ = rx_async.read_to_end(&mut result).await?;

    Ok(String::from_utf8(result)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::AsyncWriteExt;

    const EXPECTED_STRING: &str = "xyz";

    use fidl_fuchsia_memory::MonitorRequest;

    /// Returns a fake monitor service that writes `EXPECTED_STRING` to the socket
    /// when `WriteJsonCapture` is called.
    fn setup_fake_monitor_svc() -> MonitorProxy {
        setup_fake_monitor_proxy(|request| match request {
            MonitorRequest::Watch { watcher: _, .. } => {}
            MonitorRequest::WriteJsonCapture { socket, .. } => {
                let mut s = fidl::AsyncSocket::from_socket(socket).unwrap();
                fuchsia_async::Task::local(async move {
                    s.write_all(EXPECTED_STRING.as_bytes()).await.unwrap();
                })
                .detach();
            }
        })
    }

    /// Tests that `get_digest` properly reads data from the memory monitor service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_digest_test() {
        let monitor_proxy = setup_fake_monitor_svc();
        let s = get_digest(monitor_proxy).await.expect("failed to get digest");
        assert_eq!(s, EXPECTED_STRING);
    }
}

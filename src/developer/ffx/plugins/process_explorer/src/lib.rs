// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library that obtains and prints information about all processes of a running fuchsia device.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_process_explorer_args::QueryCommand,
    fidl_fuchsia_process_explorer::QueryProxy, futures::AsyncReadExt,
};

// TODO(fxbug.dev/107973): The plugin must remain experimental until the FIDL API is strongly typed.
#[ffx_plugin("ffx_process_explorer", QueryProxy = "core/appmgr:out:fuchsia.process.explorer.Query")]
/// Prints a JSON string containing processes data.
pub async fn print_processes_data(query_proxy: QueryProxy, _cmd: QueryCommand) -> Result<()> {
    let s = get_processes_data(query_proxy).await?;
    // TODO(fxbug.dev/#107974): Pretty print the JSON, and print the raw JSON only when "--machine=json" is passed.
    println!("{}", s);
    Ok(())
}

/// Returns a JSON string containing all processes data obtained via the QueryProxyInterface.
async fn get_processes_data(
    query_proxy: impl fidl_fuchsia_process_explorer::QueryProxyInterface,
) -> Result<String> {
    // Create a socket.
    let (rx, tx) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;

    // Send one end of the socket to the remote device.
    query_proxy.write_json_processes_data(tx)?;

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

    use fidl_fuchsia_process_explorer::QueryRequest;

    /// Returns a fake query service that writes `EXPECTED_STRING` to the socket
    /// when `WriteJsonProcessesData` is called.
    fn setup_fake_query_svc() -> QueryProxy {
        setup_fake_query_proxy(|request| match request {
            QueryRequest::WriteJsonProcessesData { socket, .. } => {
                let mut s = fidl::AsyncSocket::from_socket(socket).unwrap();
                fuchsia_async::Task::local(async move {
                    s.write_all(EXPECTED_STRING.as_bytes()).await.unwrap();
                })
                .detach();
            }
        })
    }

    /// Tests that `get_processes_data` properly reads data from the query service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_processes_data_test() {
        let query_proxy = setup_fake_query_svc();
        let s = get_processes_data(query_proxy).await.expect("failed to get digest");
        assert_eq!(s, EXPECTED_STRING);
    }
}

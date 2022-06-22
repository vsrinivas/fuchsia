// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_error,
    ffx_component_explore_args::ExploreComponentCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_dash::{LauncherError, LauncherProxy},
    fidl_fuchsia_io as fio,
    futures::prelude::*,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    std::io::{Read, Write},
    termion::raw::IntoRawMode,
};

// TODO(https://fxbug.dev/102835): This plugin needs E2E tests.
#[ffx_plugin(
    "component.experimental",
    LauncherProxy = "core/dash-launcher:expose:fuchsia.dash.Launcher"
)]
pub async fn explore(launcher_proxy: LauncherProxy, cmd: ExploreComponentCommand) -> Result<()> {
    let moniker = AbsoluteMoniker::parse_str(&cmd.moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;

    // LifecycleController accepts RelativeMonikers only
    let relative_moniker = format!(".{}", moniker.to_string());

    // Launch dash with the given moniker and stdio handles
    let (pty, pty_server) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
    launcher_proxy
        .launch_with_socket(&relative_moniker, pty_server)
        .await
        .map_err(|e| ffx_error!("fidl error launching dash: {}", e))?
        .map_err(|e| match e {
            LauncherError::InstanceNotFound => ffx_error!("No instance was found matching the moniker '{}'. Use `ffx component list` to find the correct moniker to use here.", moniker),
            LauncherError::InstanceNotResolved => ffx_error!("The specified instance is not resolved. Use `ffx component resolve {}` and retry this command", moniker),
            e => ffx_error!("Unexpected error launching dash: {:?}", e),
        })?;

    // Put the host terminal into raw mode, so input characters are not echoed, streams
    // are not buffered and newlines are not changed.
    let mut term_out = std::io::stdout()
        .lock()
        .into_raw_mode()
        .map_err(|e| ffx_error!("could not set raw mode on terminal: {}", e))?;

    let pty = fuchsia_async::Socket::from_socket(pty).unwrap();
    let (mut read_from_pty, mut write_to_pty) = pty.split();

    // Setup a thread for forwarding stdin. Reading from stdin is a blocking operation which
    // will halt the executor if it were to run on the same thread.
    std::thread::spawn(move || {
        let mut executor = fuchsia_async::LocalExecutor::new().unwrap();
        executor.run_singlethreaded(async move {
            let mut term_in = std::io::stdin().lock();
            let mut buf = [0u8; fio::MAX_BUF as usize];
            loop {
                let bytes_read = term_in.read(&mut buf).unwrap();
                if bytes_read > 0 {
                    let _ = write_to_pty.write_all(&buf[..bytes_read]).await;
                    let _ = write_to_pty.flush().await;
                }
            }
        });
    });

    // In a loop, wait for the TTY to be readable and print out the bytes.
    loop {
        let mut buf = [0u8; fio::MAX_BUF as usize];
        let bytes_read = read_from_pty.read(&mut buf).await.unwrap();
        if bytes_read == 0 {
            // There are no more bytes to read. This means that the socket has been
            // closed. This is probably because the dash process has terminated.
            break;
        }

        term_out.write_all(&buf[..bytes_read]).unwrap();
        term_out.flush().unwrap();
    }

    drop(term_out);
    eprintln!("Connection to terminal closed");
    Ok(())
}

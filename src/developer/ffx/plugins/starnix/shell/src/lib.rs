// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    blocking::Unblock,
    errors::ffx_error,
    ffx_core::ffx_plugin,
    ffx_starnix_shell_args::ShellStarnixCommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_starnix_developer::{
        ManagerProxy, ShellControllerEvent, ShellControllerMarker, ShellParams,
    },
    futures::{FutureExt, StreamExt},
    signal_hook::{consts::signal::SIGINT, iterator::Signals},
};

#[ffx_plugin(
    "starnix_enabled",
    ManagerProxy = "core/starnix_manager:expose:fuchsia.starnix.developer.Manager"
)]
pub async fn shell_starnix(
    manager_proxy: ManagerProxy,
    command: ShellStarnixCommand,
) -> Result<()> {
    let (controller_proxy, controller_server_end) = create_proxy::<ShellControllerMarker>()?;
    let (sin, cin) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create stdin socket")?;
    let (sout, cout) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create stdout socket")?;
    let (serr, cerr) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create stderr socket")?;

    let mut stdin = fidl::AsyncSocket::from_socket(cin)?;
    let mut stdout = Unblock::new(std::io::stdout());
    let mut stderr = Unblock::new(std::io::stderr());
    let copy_futures = futures::future::join3(
        // We may need to swap out for a latency sensitive copy that calls "flush" regularly.
        // If you see what feels like stalling behavior at odd buffer biases, it may down to this
        // copy not pumping sufficient to flush.
        futures::io::copy(Unblock::new(std::io::stdin()), &mut stdin),
        // This approach does not support "ffx starnix shell cat largefile | head -n 1" because
        // closing stdout is not propagated back to cout.
        futures::io::copy(fidl::AsyncSocket::from_socket(cout)?, &mut stdout),
        futures::io::copy(fidl::AsyncSocket::from_socket(cerr)?, &mut stderr),
    );
    // Ensure `copy_futures` never terminates by mapping it to a pending() one.
    let copy_futures: Box<dyn futures::Future<Output = Result<()>> + Unpin> =
        Box::new(copy_futures.then(|_| futures::future::pending()));

    let mut event_stream = controller_proxy.take_event_stream();
    let term_event_future = async move {
        #[allow(clippy::never_loop)]
        while let Some(result) = event_stream.next().await {
            match result? {
                // Return code is not used. See fxbug.dev/98220
                ShellControllerEvent::OnTerminated { return_code: _ } => {
                    return Ok(());
                }
            }
        }
        Err(anyhow!(ffx_error!("Shell terminated abnormally")))
    };

    // Force an exit on interrupt.
    let mut signals = Signals::new(&[SIGINT]).unwrap();
    let handle = signals.handle();
    let thread = std::thread::spawn(move || {
        for signal in signals.forever() {
            match signal {
                SIGINT => {
                    eprintln!("Caught interrupt. Forcing exit...");
                    std::process::exit(0);
                }
                _ => unreachable!(),
            }
        }
    });

    let params = ShellParams {
        standard_in: Some(sin.into()),
        standard_out: Some(sout.into()),
        standard_err: Some(serr.into()),
        url: Some(command.url),
        ..ShellParams::EMPTY
    };

    manager_proxy
        .start_shell(params, controller_server_end)
        .with_context(|| format!("Error starting shell"))?;

    let (_ignore_copy_results, return_code) = futures::try_join!(copy_futures, term_event_future)?;

    // Shut down the signal thread.
    handle.close();
    thread.join().expect("thread to shutdown without panic");

    Ok(return_code)
}

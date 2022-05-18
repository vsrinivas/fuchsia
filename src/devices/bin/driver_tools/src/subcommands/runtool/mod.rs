// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{anyhow, Context, Result},
    args::RunToolCommand,
    blocking::Unblock,
    errors::ffx_error,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_driver_playground as fdp,
    futures::{FutureExt, StreamExt},
    std::io::Write,
};

pub async fn run_tool(
    cmd: RunToolCommand,
    writer: &mut impl Write,
    tool_runner_proxy: fdp::ToolRunnerProxy,
) -> Result<()> {
    let (controller_proxy, controller_server_end) = create_proxy::<fdp::CloseControllerMarker>()?;
    let (sin, cin) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create stdin socket")?;
    let (sout, cout) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create stdout socket")?;
    let (serr, cerr) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create stderr socket")?;

    let mut stdin = fidl::AsyncSocket::from_socket(cin)?;
    let mut stdout = Unblock::new(std::io::stdout());
    let mut stderr = Unblock::new(std::io::stderr());

    let in_copy = futures::io::copy(Unblock::new(std::io::stdin()), &mut stdin).fuse();
    let out_copy = futures::io::copy(fidl::AsyncSocket::from_socket(cout)?, &mut stdout).fuse();
    let err_copy = futures::io::copy(fidl::AsyncSocket::from_socket(cerr)?, &mut stderr).fuse();

    let mut event_stream = controller_proxy.take_event_stream();
    let term_event_future = async move {
        if let Some(result) = event_stream.next().await {
            match result? {
                fdp::CloseControllerEvent::OnTerminated { return_code: code } => {
                    return Ok(code);
                }
            }
        }
        Err(anyhow!(ffx_error!("Shell terminated abnormally")))
    }
    .fuse();

    futures::pin_mut!(in_copy, out_copy, err_copy, term_event_future);

    let params = fdp::StdioParams {
        standard_in: Some(sin.into()),
        standard_out: Some(sout.into()),
        standard_err: Some(serr.into()),
        ..fdp::StdioParams::EMPTY
    };

    let mut args = cmd.args.iter().map(|arg| arg.as_str());
    let run_result = tool_runner_proxy
        .run_tool(cmd.tool.as_str(), Some(&mut args), params, controller_server_end)
        .await
        .with_context(|| format!("Error calling RunTool"))?;

    match run_result {
        Ok(_) => {}
        Err(e) => {
            writeln!(writer, "Failed to run tool, error: {}.", e)?;
            return Ok(());
        }
    }

    let mut out_done = false;
    let mut err_done = false;
    let mut terminal_done = false;

    let mut exit_code: Option<i32> = None;

    while !(out_done && err_done && terminal_done) {
        futures::select! {
            in_result = in_copy => {
                if in_result.is_err() {
                    writeln!(writer, "Failed to copy from stdin stream.")?;
                }

                break;
            },
            out_result = out_copy => {
                out_result?;
                out_done = true;
            },
            err_result = err_copy => {
                err_result?;
                err_done = true;
            },
            terminal_result = term_event_future => {
                if terminal_result.is_err() {
                    writeln!(writer, "Failed to get exit code.")?;
                } else {
                    exit_code = terminal_result.ok();
                }

                terminal_done = true;
            }
        }
    }

    if let Some(code) = exit_code {
        if code != 0 {
            writeln!(writer, "Exited with {}", code)?;
        }
    }

    Ok(())
}

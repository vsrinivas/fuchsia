// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    fidl::endpoints::{ControlHandle, Responder},
    fidl_fuchsia_dash::{LauncherControlHandle, LauncherRequest, LauncherRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_zircon as zx,
    futures::prelude::*,
    launch::{launch_with_pty, launch_with_socket},
    std::convert::TryInto,
    tracing::*,
};

mod launch;
mod socket;

enum IncomingRequest {
    Launcher(LauncherRequestStream),
}

#[fuchsia::main]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // Initialize inspect.
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    service_fs.dir("svc").add_fidl_service(IncomingRequest::Launcher);

    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    component::health().set_ok();
    debug!("Initialized.");

    service_fs
        .for_each_concurrent(None, |IncomingRequest::Launcher(mut stream)| async move {
            while let Some(Ok(request)) = stream.next().await {
                match request {
                    LauncherRequest::LaunchWithPty {
                        moniker,
                        pty,
                        tools_url,
                        command,
                        responder,
                    } => {
                        let mut result =
                            launch_with_pty(&moniker, pty, tools_url, command).await.map(|p| {
                                info!("launched Dash for instance {}", moniker);
                                notify_on_process_exit(p, responder.control_handle().clone());
                            });
                        let _ = responder.send(&mut result);
                    }
                    LauncherRequest::LaunchWithSocket {
                        moniker,
                        socket,
                        tools_url,
                        command,
                        responder,
                    } => {
                        let mut result = launch_with_socket(&moniker, socket, tools_url, command)
                            .await
                            .map(|p| {
                                info!("launched Dash for instance {}", moniker);
                                notify_on_process_exit(p, responder.control_handle().clone());
                            });
                        let _ = responder.send(&mut result);
                    }
                }
            }
        })
        .await;

    Ok(())
}

fn notify_on_process_exit(process: zx::Process, control_handle: LauncherControlHandle) {
    fasync::Task::spawn(async move {
        let _ = fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED).await;
        match process.info() {
            Ok(info) => {
                let _ = control_handle
                    .send_on_terminated(info.return_code.try_into().unwrap())
                    .context("error sending OnTerminated event");
                info!("Dash process has terminated (exit code: {})", info.return_code);
            }
            Err(s) => {
                info!("Dash process has terminated (could not get exit code: {})", s);
                control_handle.shutdown();
            }
        }
    })
    .detach();
}

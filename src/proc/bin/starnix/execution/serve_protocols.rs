// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::execution::Galaxy;
use crate::types::errno;
use anyhow::{anyhow, Error};
use fidl::endpoints::ControlHandle;
use fidl_fuchsia_component_runner as fcrunner;
use fidl_fuchsia_starnix_binder as fbinder;
use fidl_fuchsia_starnix_galaxy as fstargalaxy;
use fuchsia_async::{self as fasync, DurationExt};
use futures::TryStreamExt;
use std::sync::Arc;
use tracing::error;

use crate::fs::fuchsia::create_fuchsia_pipe;
use crate::types::OpenFlags;

use super::*;

pub async fn serve_component_runner(
    mut request_stream: fcrunner::ComponentRunnerRequestStream,
    galaxy: Arc<Galaxy>,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                let galaxy = galaxy.clone();
                fasync::Task::local(async move {
                    if let Err(e) = start_component(start_info, controller, galaxy).await {
                        error!("failed to start component: {:?}", e);
                    }
                })
                .detach();
            }
        }
    }
    Ok(())
}

pub async fn serve_galaxy_controller(
    mut request_stream: fstargalaxy::ControllerRequestStream,
    galaxy: Arc<Galaxy>,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fstargalaxy::ControllerRequest::VsockConnect { port, bridge_socket, .. } => {
                connect_to_vsock(port, bridge_socket, &galaxy).await.unwrap_or_else(|e| {
                    tracing::error!("failed to connect to vsock {:?}", e);
                });
            }
        }
    }
    Ok(())
}

pub async fn serve_dev_binder(
    mut request_stream: fbinder::DevBinderRequestStream,
    galaxy: Arc<Galaxy>,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fbinder::DevBinderRequest::Open { path, process, binder, control_handle } => {
                let result: Result<(), Error> = (|| {
                    let node = galaxy.system_task.lookup_path_from_root(&path)?;
                    let device_type = node.entry.node.info().rdev;
                    let binder_driver = galaxy
                        .kernel
                        .binders
                        .read()
                        .get(&device_type)
                        .ok_or_else(|| errno!(ENOTSUP))?
                        .clone();
                    binder_driver.open_external(process, binder).detach();
                    Ok(())
                })();
                if result.is_err() {
                    control_handle.shutdown();
                }
            }
        }
    }
    Ok(())
}

async fn connect_to_vsock(
    port: u32,
    bridge_socket: fidl::Socket,
    galaxy: &Arc<Galaxy>,
) -> Result<(), Error> {
    static MAX_WAITS: u32 = 10;
    let mut waits = 0;
    let socket = loop {
        if let Ok(socket) = galaxy.kernel.default_abstract_vsock_namespace.lookup(&port) {
            break Ok(socket);
        };
        fasync::Timer::new(fasync::Duration::from_millis(100).after_now()).await;
        waits += 1;
        if waits >= MAX_WAITS {
            break Err(anyhow!("Failed to find socket."));
        }
    }?;

    let pipe = create_fuchsia_pipe(
        &galaxy.system_task,
        bridge_socket,
        OpenFlags::RDWR | OpenFlags::NONBLOCK,
    )?;
    socket.remote_connection(pipe)?;

    Ok(())
}

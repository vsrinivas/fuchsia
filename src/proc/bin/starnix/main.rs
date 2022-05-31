// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::execution::create_galaxy;
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use std::sync::Arc;
use fuchsia_runtime::{HandleInfo, HandleType};
use fuchsia_zircon as zx;
use fidl::endpoints::{RequestStream, ControlHandle};
use fidl_fuchsia_process_lifecycle as flifecycle;
use futures::{StreamExt, TryStreamExt};

mod auth;
mod collections;
mod device;
mod execution;
mod fs;
mod loader;
mod lock;
mod logging;
mod mm;
mod mutable_state;
mod selinux;
mod signals;
mod syscalls;
mod task;
mod types;
mod vmex_resource;

#[cfg(test)]
mod testing;

/// When the runner component is stopped, the default behavior is to kill its job, which kills the
/// processes in an unspecified order. If a starnix child is killed before the parent, the parent
/// will soon panic due to receiving an unexpected ZX_ERR_PEER_CLOSED from the child's exception
/// channel. This is mostly an issue because the test framework finds the panic in the logs and
/// fails the test. This panic is useful since there are other reasons a child process might
/// unexpectedly exit and there's no good way to handle it in general. For this specific case,
/// install a lifecycle hook to make sure that the parent will exit before the children in the case
/// of an orderly shutdown.
async fn serve_lifecycle_handler(lifecycle: zx::Handle) -> Result<(), Error> {
    let mut stream = flifecycle::LifecycleRequestStream::from_channel(fasync::Channel::from_channel(zx::Channel::from(lifecycle))?);
    while let Some(request) = stream.try_next().await? {
        match request {
            flifecycle::LifecycleRequest::Stop { control_handle } => {
                control_handle.shutdown_with_epitaph(zx::Status::OK);
                // TODO(tbodt): Kill the child processes.
                std::process::exit(0);
            }
        }
    }
    Ok(())
}

#[fuchsia::main(logging_tags = ["starnix"])]
async fn main() -> Result<(), Error> {
    if let Some(lifecycle) = fuchsia_runtime::take_startup_handle(HandleInfo::new(HandleType::Lifecycle, 0)) {
        fasync::Task::local(async move {
            serve_lifecycle_handler(lifecycle).await.expect("failed to serve lifecycle handler")
        }).detach();
    }

    let galaxy = Arc::new(create_galaxy().await?);

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        let galaxy = galaxy.clone();
        fasync::Task::local(async move {
            execution::serve_component_runner(stream, galaxy)
                .await
                .expect("failed to start runner.")
        })
        .detach();
    });
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            execution::serve_starnix_manager(stream).await.expect("failed to start manager.")
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;

    Ok(())
}

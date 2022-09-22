// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::too_many_arguments)]

use crate::execution::create_galaxy;
use anyhow::Error;
use fidl::endpoints::ControlHandle;
use fidl_fuchsia_process_lifecycle as flifecycle;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_runtime as fruntime;
use futures::{StreamExt, TryStreamExt};
use std::sync::Arc;

mod auth;
mod bpf;
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

#[fuchsia::main(logging_tags = ["starnix"])]
async fn main() -> Result<(), Error> {
    let galaxy = Arc::new(create_galaxy().await?);
    let serve_galaxy = galaxy.clone();

    if let Some(lifecycle) =
        fruntime::take_startup_handle(fruntime::HandleInfo::new(fruntime::HandleType::Lifecycle, 0))
    {
        fasync::Task::local(async move {
            if let Ok(mut stream) =
                fidl::endpoints::ServerEnd::<flifecycle::LifecycleMarker>::new(lifecycle.into())
                    .into_stream()
            {
                while let Ok(Some(request)) = stream.try_next().await {
                    match request {
                        flifecycle::LifecycleRequest::Stop { control_handle } => {
                            control_handle.shutdown();
                            std::process::exit(0);
                        }
                    }
                }
            }
        })
        .detach();
    }

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|stream| {
        let galaxy = galaxy.clone();
        fasync::Task::local(async move {
            execution::serve_component_runner(stream, galaxy)
                .await
                .expect("failed to start runner.")
        })
        .detach();
    });

    fs.dir("svc").add_fidl_service(|stream| {
        let galaxy = serve_galaxy.clone();
        fasync::Task::local(async move {
            execution::serve_galaxy_controller(stream, galaxy)
                .await
                .expect("failed to start manager.")
        })
        .detach();
    });

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;

    Ok(())
}

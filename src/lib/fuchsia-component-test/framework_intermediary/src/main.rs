// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_realm_builder as ftrb, fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    log::*,
    std::sync::Arc,
};

mod resolver;
mod runner;

#[fasync::run_singlethreaded()]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["fuchsia_component_test_framework_intermediary"])?;
    info!("started");

    let mut fs = fserver::ServiceFs::new_local();
    let registry = resolver::Registry::new();
    let runner = runner::MocksRunner::new();

    let registry_clone = registry.clone();
    fs.dir("svc").add_fidl_service(move |stream| registry_clone.run_resolver_service(stream));

    let runner_clone = runner.clone();
    fs.dir("svc").add_fidl_service(move |stream| runner_clone.run_runner_service(stream));

    fs.dir("svc").add_fidl_service(move |stream| {
        let registry = registry.clone();
        let runner = runner.clone();
        fasync::Task::local(async move {
            if let Err(e) = handle_framework_intermediary_stream(stream, registry, runner).await {
                error!("error encountered while running framework intermediary service: {:?}", e);
            }
        })
        .detach();
    });

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

async fn handle_framework_intermediary_stream(
    mut stream: ftrb::FrameworkIntermediaryRequestStream,
    registry: Arc<resolver::Registry>,
    runner: Arc<runner::MocksRunner>,
) -> Result<(), Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            ftrb::FrameworkIntermediaryRequest::RegisterDecl { decl, responder } => {
                match registry.validate_and_register(decl).await {
                    Ok(url) => responder.send(&mut Ok(url))?,
                    Err(e) => {
                        warn!("failed to validate and register decl from client: {:?}", e);
                        responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()))?;
                    }
                }
            }
            ftrb::FrameworkIntermediaryRequest::RegisterMock { responder } => {
                let mock_id = runner.register_mock(stream.control_handle()).await;
                responder.send(mock_id.as_str())?;
            }
        }
    }
    Ok(())
}

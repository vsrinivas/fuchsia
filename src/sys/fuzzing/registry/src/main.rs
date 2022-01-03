// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod registry;

use {
    crate::registry::Registry,
    anyhow::Context as _,
    fidl::endpoints::{ControlHandle, Responder},
    fidl_fuchsia_fuzzer as fuzz,
    fuchsia_async::{self as fasync, futures::StreamExt},
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_err,
    futures::TryStreamExt,
    std::sync::Arc,
};

fn main() -> anyhow::Result<()> {
    fuchsia_syslog::init_with_tags(&["fuzz-registry"]).context("failed to initialize logging")?;
    let mut executor = fasync::LocalExecutor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new();
    let registry = Arc::new(Registry::new());
    let registrar = registry.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        let registrar_for_connection = registrar.clone();
        fasync::Task::spawn(async move {
            serve_registrar(registrar_for_connection, stream)
                .await
                .expect("failed to serve registrar")
        })
        .detach()
    });
    fs.dir("svc").add_fidl_service(move |stream| {
        let registry_for_connection = registry.clone();
        fasync::Task::spawn(async move {
            serve_registry(registry_for_connection, stream).await.expect("failed to serve registry")
        })
        .detach()
    });
    fs.take_and_serve_directory_handle().context("failed to take and serve directory handle")?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

async fn serve_registrar(
    registry: Arc<Registry>,
    stream: fuzz::RegistrarRequestStream,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|request| async {
            let registry_for_request = registry.clone();
            match request {
                fuzz::RegistrarRequest::Register { provider, responder } => {
                    match registry_for_request.register(provider).await {
                        Err(status) => {
                            fx_log_err!("fuchsia.fuzzer.Registrar.Register failure: {}", status);
                            responder.control_handle().shutdown();
                            Ok(())
                        }
                        Ok(_) => responder.send(),
                    }
                }
            }
        })
        .await
}

async fn serve_registry(
    registry: Arc<Registry>,
    stream: fuzz::RegistryRequestStream,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|request| async {
            let registry_for_request = registry.clone();
            match request {
                fuzz::RegistryRequest::Connect { fuzzer_url, controller, timeout, responder } => {
                    let response =
                        registry_for_request.connect(&fuzzer_url, controller, timeout).await;
                    responder.send(response)
                }
                fuzz::RegistryRequest::Disconnect { fuzzer_url, responder } => {
                    let response = registry_for_request.disconnect(&fuzzer_url).await;
                    responder.send(response)
                }
            }
        })
        .await
}

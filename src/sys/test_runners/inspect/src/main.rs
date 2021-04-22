// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod error;
mod eval;
mod spec;
mod test_server;

use {
    fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    log::{error, info},
    test_server::TestServer,
    thiserror::Error,
};

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    info!("started");
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            async move { start_runner(stream).await.expect("failed to start runner.") },
        )
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

/// Error encountered while calling fdio operations.
#[derive(Debug, Error)]
pub enum RunnerError {
    #[error("Cannot read request: {:?}", _0)]
    RequestRead(fidl::Error),
}

async fn start_runner(
    mut request_stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), RunnerError> {
    while let Some(event) = request_stream.try_next().await.map_err(RunnerError::RequestRead)? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                info!(
                    "starting \"{}\"",
                    start_info.resolved_url.as_ref().unwrap_or(&"<unspecified>".to_string())
                );
                if let Ok(server) = TestServer::new(start_info, controller) {
                    fasync::Task::local(async move { server.execute().await }).detach();
                }
            }
        }
    }
    Ok(())
}

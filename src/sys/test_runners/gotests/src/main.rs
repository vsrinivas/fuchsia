// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_server;

use {
    fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    log::{error, warn},
    test_runners_lib::elf,
    test_server::TestServer,
    thiserror::Error,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["go_test_runner"])?;
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            async move { start_runner(stream).await.expect("failed to start runner.") },
        )
    });
    fs.take_and_serve_directory_handle()?;
    fs.for_each_concurrent(None, |t| async {
        t.await;
    })
    .await;
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
                let url = start_info.resolved_url.clone().unwrap_or("".to_owned());
                if let Err(e) = elf::start_component(
                    start_info,
                    controller,
                    TestServer::new,
                    TestServer::validate_args,
                ) {
                    warn!("Cannot start component '{}': {:?}", url, e);
                }
            }
        }
    }
    Ok(())
}

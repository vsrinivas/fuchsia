// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_server;

use {
    anyhow::Context as _, fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs, fuchsia_syslog::fx_log_info, futures::prelude::*,
    test_runners_lib::elf, test_server::TestServer, thiserror::Error,
};

fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["elf_test_runner"])?;
    fx_log_info!("started");
    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            async move { start_runner(stream).await.expect("failed to start runner.") },
        )
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

/// Error encountered while calling fdio operations.
#[derive(Debug, Error)]
pub enum RunnerError {
    #[error("Cannot read request: {:?}", _0)]
    RequestRead(fidl::Error),
}

async fn start_runner(
    mut stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), RunnerError> {
    while let Some(event) = stream.try_next().await.map_err(RunnerError::RequestRead)? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                let _ = elf::start_component(
                    start_info,
                    controller,
                    TestServer::new,
                    TestServer::validate_args,
                );
            }
        }
    }
    Ok(())
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    futures::prelude::*,
    test_runners_lib::elf,
    test_runners_lib::{elf::SuiteServer, errors::*},
    thiserror::Error,
};

pub fn add_runner_service<F, U, S>(
    get_test_server: F,
    validate_args: U,
) -> Result<(), anyhow::Error>
where
    F: 'static + Fn() -> S + Send + Copy,
    U: 'static + Fn(&Vec<String>) -> Result<(), ArgumentError> + Copy,
    S: SuiteServer,
{
    fx_log_info!("started");
    let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            start_runner(stream, get_test_server, validate_args)
                .await
                .expect("failed to start runner.")
        })
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

async fn start_runner<F, U, S>(
    mut stream: fcrunner::ComponentRunnerRequestStream,
    get_test_server: F,
    validate_args: U,
) -> Result<(), RunnerError>
where
    F: 'static + Fn() -> S + Send + Copy,
    U: 'static + Fn(&Vec<String>) -> Result<(), ArgumentError> + Copy,
    S: SuiteServer,
{
    while let Some(event) = stream.try_next().await.map_err(RunnerError::RequestRead)? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                let url = start_info.resolved_url.clone().unwrap_or("".to_owned());
                if let Err(e) =
                    elf::start_component(start_info, controller, get_test_server, validate_args)
                        .await
                {
                    fx_log_warn!("Cannot start component '{}': {:?}", url, e)
                };
            }
        }
    }
    Ok(())
}

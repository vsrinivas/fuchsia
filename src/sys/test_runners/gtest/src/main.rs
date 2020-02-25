// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod errors;
mod test_component;

use {
    anyhow::Context as _,
    fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_err,
    futures::prelude::*,
    std::{fs, path::Path},
    test_component::start_component,
    thiserror::Error,
};

fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["gtest_runner"])?;
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    // We will divide this directory up and pass to  tests as /test_result so that they can write
    // their json output
    let path = Path::new("/data/test_data");
    fs::create_dir(&path).expect("cannot create directory to store test results.");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(
            async move { start_runner(stream).await.expect("failed to start runner.") },
        );
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
                if let Err(e) = start_component(start_info, controller) {
                    fx_log_err!("cannot start test: {:?}", e);
                    continue;
                }
            }
        }
    }
    Ok(())
}

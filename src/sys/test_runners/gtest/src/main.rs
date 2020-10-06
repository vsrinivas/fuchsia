// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_server;

use {
    anyhow::Context as _,
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_info,
    futures::prelude::*,
    rand::Rng,
    std::{fs, path::Path},
    test_runners_lib::elf,
    test_server::TestServer,
    thiserror::Error,
};

fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["gtest_runner"])?;
    fx_log_info!("started");
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    // We will divide this directory up and pass to  tests as /test_result so that they can write
    // their json output
    let path = Path::new("/data/test_data");
    fs::create_dir(&path).expect("cannot create directory to store test results.");

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
                let _ =
                    elf::start_component(start_info, controller, get_new_test_server, |_| Ok(()));
            }
        }
    }
    Ok(())
}

fn get_new_test_server() -> TestServer {
    let mut rng = rand::thread_rng();
    let test_data_name = format!("{}", rng.gen::<u64>());
    let test_data_dir_parent = "/data/test_data".to_owned();
    let test_data_path = format!("{}/{}", test_data_dir_parent, test_data_name);

    // TODO(fxbug.dev/45856): use async lib.
    fs::create_dir(&test_data_path).expect("cannot create test output directory.");
    let test_data_dir = io_util::open_directory_in_namespace(
        &test_data_path,
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Cannot open data directory");

    TestServer::new(test_data_dir, test_data_name, test_data_dir_parent)
}

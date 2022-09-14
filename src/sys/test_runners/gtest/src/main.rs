// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_server;

use {
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    rand::Rng,
    std::{fs, path::Path},
    test_runners_lib::elf,
    test_server::TestServer,
    thiserror::Error,
    tracing::{info, warn},
};

#[cfg(feature = "gtest")]
#[fuchsia::main(logging_tags=["gtest_runner"])]
async fn main() -> Result<(), anyhow::Error> {
    main_impl().await
}

#[cfg(feature = "gunit")]
#[fuchsia::main(logging_tags=["gunit_runner"])]
async fn main() -> Result<(), anyhow::Error> {
    main_impl().await
}

async fn main_impl() -> Result<(), anyhow::Error> {
    info!("started");
    // We will divide this directory up and pass to  tests as /test_result so that they can write
    // their json output
    let path = Path::new("/data/test_data");
    // the directory might already be present so use create_dir_all.
    fs::create_dir_all(&path).expect("cannot create directory to store test results.");

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

/// Error encountered by runner.
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
                let url = start_info.resolved_url.clone().unwrap_or("".to_owned());
                if let Err(e) = elf::start_component(
                    start_info,
                    controller,
                    get_new_test_server,
                    TestServer::validate_args,
                )
                .await
                {
                    warn!("Cannot start component '{}': {:?}", url, e)
                };
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
    let test_data_dir = fuchsia_fs::directory::open_in_namespace(
        &test_data_path,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .expect("Cannot open data directory");

    TestServer::new(test_data_dir, test_data_name, test_data_dir_parent)
}

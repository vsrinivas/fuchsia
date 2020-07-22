// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_test_components as ftest, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    io_util::{self, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    std::path::PathBuf,
};

#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_trigger_service(stream).await.expect("failed to run trigger service");
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    fs.collect::<()>().await;
}

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        let ftest::TriggerRequest::Run { responder } = event;
        let data_proxy = io_util::open_directory_in_namespace(
            "/data",
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )?;
        let file = io_util::open_file(
            &data_proxy,
            &PathBuf::from("test"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
        )?;
        let msg = if let Err(_) = io_util::write_file_bytes(&file, b"test_data").await {
            "Failed to write to file"
        } else {
            "All tests passed"
        };
        responder.send(msg).expect("failed to send trigger response");
    }
    Ok(())
}

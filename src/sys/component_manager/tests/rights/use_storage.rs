// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_test_components as ftest, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_fs::{self, OpenFlags},
    futures::{StreamExt, TryStreamExt},
};

#[fuchsia::main]
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

async fn open_and_write_file(dir: &fio::DirectoryProxy) -> Result<(), Error> {
    // We are opening the file with the DESCRIBE flag and waiting for a response (no pipelining).
    // This should fail if the directory failed to route due to a rights issue.
    let file = fuchsia_fs::directory::open_file(
        &dir,
        "test",
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::CREATE,
    )
    .await?;
    fuchsia_fs::write_file_bytes(&file, b"test_data").await?;
    Ok(())
}

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        let ftest::TriggerRequest::Run { responder } = event;
        let data_proxy = fuchsia_fs::open_directory_in_namespace(
            "/data",
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
        )?;
        let msg = if open_and_write_file(&data_proxy).await.is_err() {
            "Failed to write to file"
        } else {
            "All tests passed"
        };
        responder.send(msg).expect("failed to send trigger response");
    }
    Ok(())
}

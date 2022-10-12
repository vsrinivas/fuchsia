// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::controller::serve_controller,
    crate::test::Test,
    crate::util::create_task,
    anyhow::{Context as _, Result},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_fuzzer as fuzz, fuchsia_zircon_status as zx,
    futures::StreamExt,
};

/// Serves `fuchsia.fuzzer.Manager` on the given `server_end` of a FIDL channel.
pub async fn serve_manager(server_end: ServerEnd<fuzz::ManagerMarker>, test: Test) -> Result<()> {
    let mut stream = server_end.into_stream()?;
    let mut task = None;
    let url = test.url();
    let fake = test.controller();
    let writer = test.writer().clone();
    while let Some(request) = stream.next().await {
        let request = request.context("fuchsia.fuzzer.Manager")?;
        match request {
            fuzz::ManagerRequest::Connect { fuzzer_url, controller, responder } => {
                let stream = controller.into_stream()?;
                {
                    let mut url_mut = url.borrow_mut();
                    *url_mut = Some(fuzzer_url);
                }
                responder.send(zx::Status::OK.into_raw())?;
                task = Some(create_task(serve_controller(stream, fake.clone()), &writer));
            }
            fuzz::ManagerRequest::GetOutput { fuzzer_url, output, socket, responder } => {
                let running = {
                    let url = url.borrow();
                    url.clone().unwrap_or(String::default())
                };
                if fuzzer_url == running {
                    responder.send(fake.set_output(output, socket).into_raw())?;
                } else {
                    responder.send(zx::Status::NOT_FOUND.into_raw())?;
                }
            }
            fuzz::ManagerRequest::Stop { fuzzer_url, responder } => {
                let running = {
                    let mut url_mut = url.borrow_mut();
                    let running = url_mut.as_ref().map_or(String::default(), |url| url.to_string());
                    *url_mut = Some(fuzzer_url.to_string());
                    running
                };
                if fuzzer_url == running {
                    task = None;
                    responder.send(zx::Status::OK.into_raw())?;
                } else {
                    responder.send(zx::Status::NOT_FOUND.into_raw())?;
                }
            }
        };
    }
    if let Some(task) = task.take() {
        task.await;
    }
    Ok(())
}

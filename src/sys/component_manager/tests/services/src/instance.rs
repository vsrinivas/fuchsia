// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl_fuchsia_test_services as ftest,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    log::*,
};

#[fuchsia::component]
async fn main() {
    let mut args = std::env::args().skip(1);
    let name = args.next().expect("name arg");
    info!("starting instance with name={}", &name);

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_unified_service(|req: ftest::ServiceRequest| req);
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing namespace");
    fs.for_each_concurrent(None, move |ftest::ServiceRequest::InstanceReporter(request)| {
        let name = name.clone();
        async move {
            match handle_request(name.clone(), request).await {
                Ok(()) => {}
                Err(err) => error!("failed to serve request: {}", err),
            }
        }
    })
    .await;
}

async fn handle_request(
    name: String,
    mut request_stream: ftest::InstanceReporterRequestStream,
) -> Result<()> {
    while let Some(request) =
        request_stream.try_next().await.context("failed to get next request")?
    {
        let ftest::InstanceReporterRequest::ReportInstance { responder } = request;
        responder.send(&name).context("failed to send response")?;
    }
    Ok(())
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl_test_inspect_validate::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::Inspector,
    fuchsia_syslog as syslog,
    futures::prelude::*,
    log::*,
};

async fn run_driver_service(mut stream: ValidateRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            ValidateRequest::Initialize { params, responder } => {
                let inspector = match params.vmo_size {
                    Some(size) => Inspector::new_with_size(size as usize),
                    None => Inspector::new(),
                };
                responder
                    .send(inspector.vmo_handle_for_test(), TestResult::Ok)
                    .context("responding to initialize")?
            }
            ValidateRequest::Act { action, responder } => {
                info!("Act was called: {:?}", action);
                // TODO(CF-911): Implement these actions.
                responder.send(TestResult::Ok)?;
            }
        }
    }
    Ok(())
}

enum IncomingService {
    Validate(ValidateRequestStream),
    // ... more services here
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&[]).expect("should not fail");
    info!("Puppet starting");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Validate);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 1;
    info!("Puppet about to wait for connection");
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Validate(stream)| {
        run_driver_service(stream).unwrap_or_else(|e| error!("ERROR in puppet's main: {:?}", e))
    });

    fut.await;
    Ok(())
}

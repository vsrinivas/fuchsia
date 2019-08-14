// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl_test_inspect_validate::{ValidateRequest, ValidateRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog as syslog,
    futures::prelude::*,
    log::*,
};

async fn run_echo_server(mut stream: ValidateRequestStream) -> Result<(), Error> {
    while let Some(ValidateRequest::EchoTheString { value, responder }) =
        stream.try_next().await.context("error running validator server")?
    {
        debug!("Received validator request for string {:?}", value);
        responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
        debug!("validator response sent successfully");
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
        run_echo_server(stream).unwrap_or_else(|e| println!("ERROR in puppet's main: {:?}", e))
    });

    fut.await;
    Ok(())
}

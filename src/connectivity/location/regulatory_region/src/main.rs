// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_location_namedplace::{
        RegulatoryRegionConfiguratorRequest, RegulatoryRegionConfiguratorRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{StreamExt, TryFutureExt, TryStreamExt},
};

const CONCURRENCY_LIMIT: Option<usize> = None;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init().context("Failed to initialize logging")?;
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|client| client);
    fs.take_and_serve_directory_handle().context("Failed to start serving")?;
    fs.for_each_concurrent(CONCURRENCY_LIMIT, |client| {
        process_request_stream(client)
            .unwrap_or_else(|e| fx_log_info!("Client terminated: {:?}", e))
    })
    .await;
    Ok(())
}

async fn process_request_stream(
    mut stream: RegulatoryRegionConfiguratorRequestStream,
) -> Result<(), Error> {
    while let Some(RegulatoryRegionConfiguratorRequest::SetRegion { region, control_handle: _ }) =
        stream.try_next().await.context("Failed to read client request")?
    {
        println!("Received request to set country to {}", region);
    }
    Ok(())
}

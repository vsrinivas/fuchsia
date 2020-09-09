// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_update_usb::CheckerRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_err,
    futures::prelude::*,
};

mod update_checker;
use update_checker::UsbUpdateChecker;

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    Checker(CheckerRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["usb-system-update-checker"]).context("Setting up logs")?;
    let mut service_fs = ServiceFs::new_local();

    service_fs.dir("svc").add_fidl_service(IncomingRequest::Checker);

    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::Checker(mut stream) => {
                    UsbUpdateChecker::new().handle_request_stream(&mut stream).await
                }
            }
            .unwrap_or_else(|e| fx_log_err!("Failed to handle check request: {:#}", e));
        })
        .await;

    Ok(())
}

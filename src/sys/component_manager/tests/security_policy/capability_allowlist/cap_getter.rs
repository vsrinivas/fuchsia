// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_io as fio, fidl_test_policy as ftest,
    fidl_test_policy::{AccessRequest, AccessRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

/// Trivial service host that just launches a restricted and unrestricted
/// protocol that both return a trivial string.
#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            run_access_service(stream).unwrap_or_else(|e| panic!("error running service: {:?}", e)),
        )
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing dir");
    fs.collect::<()>().await;
}

/// Attempts to access the restricted protocol
async fn check_restricted_protocol() -> bool {
    match connect_to_service::<ftest::RestrictedMarker>() {
        Ok(svc) => match svc.get_restricted().await {
            Ok(result) => result == "restricted",
            Err(_) => false,
        },
        Err(_) => false,
    }
}

/// Attempts to access the unrestricted protocol
async fn check_unrestricted_protocol() -> bool {
    match connect_to_service::<ftest::UnrestrictedMarker>() {
        Ok(svc) => match svc.get_unrestricted().await {
            Ok(result) => result == "unrestricted",
            Err(_) => false,
        },
        Err(_) => false,
    }
}

/// Attempts to access the restricted directory.
async fn check_restricted_directory() -> bool {
    fdio::open_fd("/restricted", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE).is_ok()
}

/// Attempts to access the unrestricted directory.
async fn check_unrestricted_directory() -> bool {
    fdio::open_fd("/unrestricted", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE).is_ok()
}

/// Trivial service that returns true if it can access the test.policy.Restricted cap.
async fn run_access_service(mut stream: AccessRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            AccessRequest::AccessRestrictedProtocol { responder } => {
                responder.send(check_restricted_protocol().await)?;
            }
            AccessRequest::AccessUnrestrictedProtocol { responder } => {
                responder.send(check_unrestricted_protocol().await)?;
            }
            AccessRequest::AccessRestrictedDirectory { responder } => {
                responder.send(check_restricted_directory().await)?;
            }
            AccessRequest::AccessUnrestrictedDirectory { responder } => {
                responder.send(check_unrestricted_directory().await)?;
            }
        }
    }
    Ok(())
}

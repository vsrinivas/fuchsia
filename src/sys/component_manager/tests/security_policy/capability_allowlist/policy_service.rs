// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_test_policy::{
        RestrictedRequest, RestrictedRequestStream, UnrestrictedRequest, UnrestrictedRequestStream,
    },
    fuchsia_async as fasync,
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
            run_restricted_service(stream)
                .unwrap_or_else(|e| panic!("error running service: {:?}", e)),
        )
        .detach();
    });
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            run_unrestricted_service(stream)
                .unwrap_or_else(|e| panic!("error running service: {:?}", e)),
        )
        .detach();
    });

    fs.take_and_serve_directory_handle().expect("failed to serve outgoing dir");
    fs.collect::<()>().await;
}

/// Trivial service that just returns the value "restricted"
async fn run_restricted_service(mut stream: RestrictedRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            RestrictedRequest::GetRestricted { responder } => {
                responder.send("restricted")?;
            }
        }
    }
    Ok(())
}

/// Trivial service that just returns the value "unrestricted"
async fn run_unrestricted_service(mut stream: UnrestrictedRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            UnrestrictedRequest::GetUnrestricted { responder } => {
                responder.send("unrestricted")?;
            }
        }
    }
    Ok(())
}

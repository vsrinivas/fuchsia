// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Context};
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::{component, health::Reporter};
use futures::prelude::*;
use tracing;

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    // Add a variant for each protocol being served. E.g:
    // ```
    // MyProtocol(MyProtocolRequestStream),
    // ```
}

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // Initialize inspect
    component::inspector().serve(&mut service_fs)?;
    component::health().set_starting_up();

    // Add services here. E.g:
    // ```
    // service_fs.dir("svc").add_fidl_service(IncomingRequest::MyProtocol);
    // ```

    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    component::health().set_ok();
    tracing::debug!("Initialized.");

    service_fs
        .for_each_concurrent(None, |_request: IncomingRequest| async move {
            // match on `request` and handle each protocol.
        })
        .await;

    Ok(())
}

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn smoke_test() {
        assert!(true);
    }
}

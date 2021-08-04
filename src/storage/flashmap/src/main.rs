// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod flashmap;
mod manager;
mod util;

use {
    crate::manager::Manager,
    anyhow::{self, Context},
    fidl_fuchsia_nand_flashmap::ManagerRequestStream,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    futures::prelude::*,
    tracing,
};

enum IncomingRequest {
    FlashmapManager(ManagerRequestStream),
}

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();
    let manager = Manager::new();

    // Initialize inspect
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    service_fs.dir("svc").add_fidl_service(IncomingRequest::FlashmapManager);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    component::health().set_ok();
    tracing::debug!("Initialized.");

    let manager_ref = &manager;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::FlashmapManager(stream) => manager_ref.serve(stream).await,
            };
        })
        .await;

    Ok(())
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_test_policy::{ProtectedOperationsRequest, ProtectedOperationsRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
};

#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            run_service(stream).unwrap_or_else(|e| panic!("error running service: {:?}", e)),
        )
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing dir");
    fs.collect::<()>().await;
}

async fn run_service(mut stream: ProtectedOperationsRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            ProtectedOperationsRequest::AmbientReplaceAsExecutable { vmo, responder } => {
                // TODO: zx::Vmo::replace_as_executable currently always uses an invalid resource,
                // which is what we want here because we're exercising ZX_POL_AMBIENT_MARK_VMO_EXEC.
                // When it's updated, this should continue using an invalid resource handle.
                let mut result = vmo
                    .replace_as_executable(&zx::Resource::from(zx::Handle::invalid()))
                    .map_err(zx::Status::into_raw);
                responder.send(&mut result)?;
            }
        }
    }
    Ok(())
}

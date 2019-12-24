// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_modular::*;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

mod local_resolver;

// The directory name where the module resolver FIDL services are exposed.
const SERVICE_DIRECTORY: &'static str = "svc";

/// Runs a module resolver server.
///
/// Arguments:
///     - `stream`: The stream which provides incoming requests.
/// Returns:
/// `Ok` once the request stream is closed.
async fn run_module_resolver(mut stream: ModuleResolverRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("Error running module resolver.")? {
        match request {
            ModuleResolverRequest::FindModules { query, responder } => {
                let index: local_resolver::ModuleActionIndex =
                    local_resolver::ModuleActionIndex::new();
                responder.send(&mut local_resolver::find_modules(&query, &index))?;
            }
        }
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir(SERVICE_DIRECTORY).add_fidl_service(|stream| stream);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |stream| {
        run_module_resolver(stream).unwrap_or_else(|e| println!("{:?}", e))
    });

    fut.await;
    Ok(())
}

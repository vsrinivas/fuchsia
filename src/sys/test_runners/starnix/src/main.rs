// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod runner;
mod test_suite;

use {anyhow::Error, fuchsia_component::server::ServiceFs, futures::StreamExt};

enum ExposedServices {
    Runner(fidl_fuchsia_component_runner::ComponentRunnerRequestStream),
}

#[fuchsia::main(logging_tags=["starnix_test_runner"])]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::Runner);
    fs.take_and_serve_directory_handle()?;

    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::Runner(stream) => match runner::handle_runner_requests(stream).await {
                Ok(_) => tracing::info!("Finished serving runner requests."),
                Err(e) => tracing::error!("Error serving runner requests: {:?}", e),
            },
        }
    }

    Ok(())
}

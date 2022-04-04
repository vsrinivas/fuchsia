// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use example_config::Config;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use tracing::info;

#[fuchsia::main]
async fn main() {
    // Retrieve config, recording it in the component's inspect
    let inspector = fuchsia_inspect::component::inspector();
    let Config { greeting } = Config::from_args().record_to_inspect(inspector.root());

    // Print our greeting to the log
    info!("Hello, {}!", greeting);

    // Serve the inspect output for clients to request
    let mut fs = ServiceFs::new_local();
    inspect_runtime::serve(inspector, &mut fs).unwrap();
    fs.take_and_serve_directory_handle().unwrap();
    while let Some(()) = fs.next().await {}
}

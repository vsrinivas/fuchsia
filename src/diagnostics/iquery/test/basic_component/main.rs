// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh,
    argh::FromArgs,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    futures::StreamExt,
    tracing::info,
};

/// Options for basic_component
#[derive(Debug, Default, FromArgs)]
pub struct Args {
    /// prints INFO is great to have!
    #[argh(switch)]
    with_logs: bool,
}

#[fuchsia::component(logging_tags = ["iquery_basic_component"])]
async fn main() -> Result<(), Error> {
    let opt: Args = argh::from_env();
    let inspector = component::inspector();
    inspector.root().record_string("iquery", "rocks");
    component::health().set_ok();

    let mut fs = ServiceFs::new();
    inspect_runtime::serve(&inspector, &mut fs)?;
    fs.take_and_serve_directory_handle()?;
    if opt.with_logs {
        info!("Is great to have!");
    }
    Ok(fs.collect().await)
}

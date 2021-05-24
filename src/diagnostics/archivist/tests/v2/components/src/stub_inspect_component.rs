// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::{component, health::Reporter};
use futures::StreamExt;
use tracing::info;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    component::health().set_ok();
    inspect_runtime::serve(component::inspector(), &mut fs)?;
    info!("This is a syslog message");
    info!("This is another syslog message");
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

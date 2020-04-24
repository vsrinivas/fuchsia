// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    futures::StreamExt,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    component::health().set_ok();
    component::inspector().serve(&mut fs)?;
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

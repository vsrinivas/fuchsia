// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod game;
mod tennis_service;

use anyhow::Error;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use tracing::info;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    info!("tennis service started");
    let tennis = tennis_service::TennisService::new();
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|stream| tennis.bind(stream));
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

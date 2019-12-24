// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod config;
mod fake_factory_items_server;

use {
    anyhow::Error,
    config::Config,
    fake_factory_items_server::{spawn_fake_factory_items_server, FakeFactoryItemsServer},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let config = Config::load().unwrap();
    let config_map = Arc::new(RwLock::new(config.into()));

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        let server = FakeFactoryItemsServer::new(Arc::clone(&config_map));
        spawn_fake_factory_items_server(server, stream);
    });
    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;
    Ok(())
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::fx_log_err;
use futures::{lock::Mutex, StreamExt};
use std::sync::Arc;

use crate::{
    registry::RegistryStore,
    router::Router,
    service::{ManagerService, RegistryService},
};

mod registry;
mod router;
mod service;
mod shortcut2;

/// Environment consists of FIDL services handlers and shortcut storage.
pub struct Environment {
    store: RegistryStore,
    registry_service: RegistryService,
    manager_service: Arc<Mutex<ManagerService>>,
    shortcut2: shortcut2::Shortcut2Impl,
}

impl Environment {
    fn new() -> Self {
        let store = RegistryStore::new();
        let shortcut2 = shortcut2::Shortcut2Impl::new();
        Environment {
            store: store.clone(),
            registry_service: RegistryService::new(),
            manager_service: Arc::new(Mutex::new(ManagerService::new(store))),
            shortcut2,
        }
    }
}

/// FIDL services multiplexer.
enum Services {
    RegistryServer(ui_shortcut::RegistryRequestStream),
    ManagerServer(ui_shortcut::ManagerRequestStream),
    Registry2Server(ui_shortcut2::RegistryRequestStream),
}

#[fuchsia::main(logging = true, logging_tags = ["shortcut"])]
async fn main() -> Result<()> {
    let environment = Environment::new();
    let router = Router::new(environment);
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(Services::RegistryServer)
        .add_fidl_service(Services::ManagerServer)
        .add_fidl_service(Services::Registry2Server);
    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(None, |incoming_service| async {
        match incoming_service {
            Services::RegistryServer(stream) => {
                router.registry_server(stream).await.context("registry server error")
            }
            Services::ManagerServer(stream) => {
                router.manager_server(stream).await.context("manager server error")
            }
            Services::Registry2Server(stream) => {
                router.handle_registry_stream(stream).await.context("shortcut2/Registry error")
            }
        }
        .unwrap_or_else(|e| fx_log_err!("request failed: {:?}", e))
    })
    .await;

    Ok(())
}

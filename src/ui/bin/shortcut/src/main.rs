// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_shortcut2 as fs2;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use tracing::error;

mod shortcut2;

/// FIDL services multiplexer.
enum Services {
    ManagerServer(ui_shortcut::ManagerRequestStream),
    Registry2Server(fs2::RegistryRequestStream),
}

#[fuchsia::main(logging = true, logging_tags = ["shortcut"])]
async fn main() -> Result<()> {
    let shortcut2 = shortcut2::Shortcut2Impl::new();
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(Services::ManagerServer)
        .add_fidl_service(Services::Registry2Server);
    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(None, |incoming_service| async {
        match incoming_service {
            Services::ManagerServer(stream) => {
                shortcut2.manager_server(stream).await.context("manager server error")
            }
            Services::Registry2Server(stream) => {
                shortcut2.handle_registry_stream(stream).await.context("shortcut2/Registry error")
            }
        }
        .unwrap_or_else(|e| error!("request failed: {:?}", e))
    })
    .await;

    Ok(())
}

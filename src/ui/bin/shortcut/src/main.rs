// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::lock::Mutex;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use std::sync::Arc;

mod registry;

const SERVER_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["shortcut"]).expect("shortcut syslog init should not fail");
    let mut executor =
        fasync::Executor::new().context("Creating fuchsia_async executor for Shortcut failed")?;

    let store = Arc::new(Mutex::new(registry::RegistryStore::default()));

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service_at(ui_shortcut::RegistryMarker::NAME, {
            // Capture a clone of store's Arc so the new client has a copy from
            // which to make new clones.
            let store = store.clone();
            move |stream| {
                fasync::spawn(
                    registry_server(stream, store.clone())
                        .unwrap_or_else(|e: failure::Error| fx_log_err!("couldn't run: {:?}", e)),
                );
            }
        })
        .add_fidl_service_at(ui_shortcut::ManagerMarker::NAME, {
            // Capture a clone of store's Arc so the new client has a copy from
            // which to make new clones.
            let store = Arc::clone(&store);
            move |stream| {
                fasync::spawn(
                    manager_server(stream, store.clone())
                        .unwrap_or_else(|e: failure::Error| fx_log_err!("couldn't run: {:?}", e)),
                );
            }
        });
    fs.take_and_serve_directory_handle()?;

    executor.run(fs.collect::<()>(), SERVER_THREADS);
    Ok(())
}

async fn registry_server(
    mut stream: ui_shortcut::RegistryRequestStream,
    store: Arc<Mutex<registry::RegistryStore>>,
) -> Result<(), Error> {
    fx_log_info!("new server connection");

    // The lifetime of the shortcuts is determined by the lifetime of the connection,
    // so once this registry goes out of scope, it's removed from RegistryStore.
    let registry = store.lock().await.add_new_registry();

    // TODO: clean up empty Weak refs for registries from the store

    while let Some(req) = stream.try_next().await.context("error running registry server")? {
        let mut registry = registry.lock().await;

        match req {
            ui_shortcut::RegistryRequest::SetView { view_ref, listener, .. } => {
                // New subscriber overrides the old one.
                registry.subscriber =
                    Some(registry::Subscriber { view_ref, listener: listener.into_proxy()? });
            }
            ui_shortcut::RegistryRequest::RegisterShortcut { shortcut, responder, .. } => {
                if let Some(mut modifiers) = shortcut.modifiers {
                    if modifiers.contains(ui_input::Modifiers::Shift) {
                        modifiers.remove(ui_input::Modifiers::LeftShift);
                        modifiers.remove(ui_input::Modifiers::RightShift);
                    }
                    if modifiers.contains(ui_input::Modifiers::Control) {
                        modifiers.remove(ui_input::Modifiers::LeftControl);
                        modifiers.remove(ui_input::Modifiers::RightControl);
                    }
                    if modifiers.contains(ui_input::Modifiers::Alt) {
                        modifiers.remove(ui_input::Modifiers::LeftAlt);
                        modifiers.remove(ui_input::Modifiers::RightAlt);
                    }
                    if modifiers.contains(ui_input::Modifiers::Meta) {
                        modifiers.remove(ui_input::Modifiers::LeftMeta);
                        modifiers.remove(ui_input::Modifiers::RightMeta);
                    }
                }
                // TODO: validation
                registry.shortcuts.push(shortcut);
                responder.send()?;
            }
        }
    }
    Ok(())
}

async fn manager_server(
    mut stream: ui_shortcut::ManagerRequestStream,
    store: Arc<Mutex<registry::RegistryStore>>,
) -> Result<(), Error> {
    fx_log_info!("new manager connection");

    while let Some(req) = stream.try_next().await.context("error running manager server")? {
        let mut store = store.lock().await;

        match req {
            ui_shortcut::ManagerRequest::HandleKeyEvent { event, responder } => {
                // TODO: error handling
                let was_handled = store.handle_key_event(event).await?;
                responder.send(was_handled).context("error sending response")?;
            }
        }
    }
    Ok(())
}

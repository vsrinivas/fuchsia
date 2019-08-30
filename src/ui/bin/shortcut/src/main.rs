// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
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

#[cfg(test)]
mod test {

    use super::*;
    use fidl_fuchsia_ui_input2 as ui_input;
    use fidl_fuchsia_ui_views as ui_views;
    use fuchsia_component::client::{launch, launcher};
    use fuchsia_zircon as zx;

    static COMPONENT_URL: &str = "fuchsia-pkg://fuchsia.com/shortcut#meta/shortcut_manager.cmx";
    static TEST_SHORTCUT_ID: u32 = 123;

    #[fasync::run_singlethreaded(test)]
    async fn test_as_client() -> Result<(), Error> {
        fuchsia_syslog::init_with_tags(&["shortcut"])
            .expect("shortcut syslog init should not fail");

        let launcher = launcher().context("Failed to open launcher service")?;

        let app = launch(&launcher, COMPONENT_URL.to_string(), None)
            .context("Failed to launch Shortcut manager")?;

        let registry = app
            .connect_to_service::<ui_shortcut::RegistryMarker>()
            .context("Failed to connect to Shortcut registry service")?;

        let manager = app
            .connect_to_service::<ui_shortcut::ManagerMarker>()
            .context("Failed to connect to Shortcut manager service")?;

        let (listener_client_end, mut listener_stream) =
            fidl::endpoints::create_request_stream::<ui_shortcut::ListenerMarker>()?;

        // Set listener and view ref.
        let (raw_event_pair, _) = zx::EventPair::create()?;
        let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };
        registry.set_view(view_ref, listener_client_end).expect("set_view");

        // Set the shortcut.
        let shortcut = ui_shortcut::Shortcut {
            id: Some(TEST_SHORTCUT_ID),
            modifiers: None,
            key: Some(ui_input::Key::A),
            use_priority: None,
        };
        registry.register_shortcut(shortcut).await.expect("register_shortcut");

        // Process key event that *does not* trigger a shortcut.
        let event = ui_input::KeyEvent {
            key: None,
            modifiers: None,
            phase: Some(ui_input::KeyEventPhase::Pressed),
        };

        let was_handled = manager.handle_key_event(event).await.expect("handle_key_event false");

        assert_eq!(false, was_handled);

        // Process key event that triggers a shortcut.
        let event = ui_input::KeyEvent {
            key: Some(ui_input::Key::A),
            modifiers: None,
            phase: Some(ui_input::KeyEventPhase::Pressed),
        };

        // The order is important here, as handle_key_event() dispatches the key
        // to be processed by the manager, which is expected to result in listener_stream
        // message in the next block.
        // At the same time, handle_key_event should return true, which is validated
        // later.
        let was_handled_fut = manager.handle_key_event(event);

        // React to one shortcut activation message from the listener stream.
        if let Some(Ok(req)) = listener_stream.next().await {
            match req {
                ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. } => {
                    assert_eq!(id, TEST_SHORTCUT_ID);
                    responder.send(true).expect("responding from shortcut listener")
                }
            };
        } else {
            panic!("Error from listener_stream.next()");
        }

        let was_handled = was_handled_fut.await.expect("handle_key_event true");
        // Expect key event to be handled.
        assert_eq!(true, was_handled);

        Ok(())
    }
}

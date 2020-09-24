// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_ui_input3 as ui_input3;
use fidl_fuchsia_ui_views as ui_views;
use futures::lock::Mutex;
use futures::TryStreamExt;
use std::collections::HashMap;
use std::sync::Arc;

/// Provides implementation for fuchsia.ui.input3.Keyboard FIDL.
pub struct KeyboardService {
    store: Arc<Mutex<KeyListenerStore>>,
}

/// Holder for fuchsia.ui.input2.KeyListener client requests.
#[derive(Default)]
struct KeyListenerStore {
    // Client subscribers for the Keyboard service, keyed
    // by self-generated monotonic id.
    subscribers: HashMap<usize, Arc<Mutex<Subscriber>>>,

    // Last monotonic id used as a subscribers hash key.
    last_id: usize,
}

/// A client of fuchsia.ui.input3.Keyboard.SetListener()
struct Subscriber {
    pub view_ref: ui_views::ViewRef,
    pub listener: ui_input3::KeyboardListenerProxy,
}

impl KeyboardService {
    /// Starts new instance of keyboard service.
    pub async fn new() -> Result<KeyboardService, Error> {
        Ok(KeyboardService { store: Arc::new(Mutex::new(KeyListenerStore::default())) })
    }

    /// Dispatches key event to clients.
    pub async fn handle_key_event(&self) -> Result<bool, Error> {
        // TODO(fxbug.dev/)47684: implement
        Ok(false)
    }

    /// Starts serving fuchsia.ui.input3.Keyboard protocol.
    pub async fn spawn_service(
        &self,
        mut stream: ui_input3::KeyboardRequestStream,
    ) -> Result<(), Error> {
        let store = self.store.clone();
        // KeyListenerStore subscriber ids to cleanup once client disconnects.
        let mut subscriber_ids: Vec<usize> = Vec::new();
        while let Some(ui_input3::KeyboardRequest::AddListener {
            view_ref,
            listener,
            responder,
            ..
        }) = stream.try_next().await.context("error running keyboard service")?
        {
            let id = store.lock().await.add_new_subscriber(view_ref, listener.into_proxy()?);
            subscriber_ids.push(id);
            responder.send()?;
        }
        // Remove subscribers from the store.
        let mut store = store.lock().await;
        subscriber_ids.iter().for_each(|i| {
            store.subscribers.remove(i);
        });
        Ok(())
    }
}

impl KeyListenerStore {
    fn add_new_subscriber(
        &mut self,
        view_ref: ui_views::ViewRef,
        listener: ui_input3::KeyboardListenerProxy,
    ) -> usize {
        let subscriber = Arc::new(Mutex::new(Subscriber { view_ref, listener }));
        self.last_id += 1;
        self.subscribers.insert(self.last_id, subscriber);
        self.last_id
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use fuchsia_syslog::fx_log_err;
    use fuchsia_zircon as zx;
    use futures::TryFutureExt;

    #[fasync::run_singlethreaded(test)]
    async fn test_as_client() -> Result<(), Error> {
        let (keyboard_proxy, keyboard_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ui_input3::KeyboardMarker>()
                .expect("Failed to create KeyboardProxy and stream.");
        let service = KeyboardService::new().await?;
        fuchsia_async::Task::spawn(
            async move { service.spawn_service(keyboard_request_stream).await }
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();

        let (listener_client_end, _listener) =
            fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
        let (raw_event_pair, _) = zx::EventPair::create()?;
        let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };

        keyboard_proxy.add_listener(view_ref, listener_client_end).await.expect("set_view");

        // TODO: dispatch a key, receive at client API
        Ok(())
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_ui_input2 as ui_input2;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_component::client;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use std::ops::Sub;
use std::sync::Arc;

static DEFAULT_IME_URL: &str =
    "fuchsia-pkg://fuchsia.com/ime_service#meta/default_hardware_ime.cmx";

/// Provides implementation for fuchsia.ui.input2.Keyboard FIDL.
/// Starts default_hardware_ime for keyboard layouts.
/// DEPRECATED: transition to input3 in progress.
pub struct Service {
    // Once ime leaves the scope, default_hardware_ime is unloaded.
    _ime: client::App,
    store: Arc<Mutex<Store>>,
    layout: Arc<Mutex<ui_input2::KeyboardLayout>>,
}

/// Store for fuchsia.ui.input2.KeyListener client requests.
#[derive(Default)]
struct Store {
    subscribers: Vec<(usize, Arc<Mutex<Subscriber>>)>,
    last_id: usize,
}

/// A client of fuchsia.ui.input2.Keyboard.SetListener()
struct Subscriber {
    pub view_ref: ui_views::ViewRef,
    pub listener: ui_input2::KeyListenerProxy,
}

impl Service {
    /// Create new instance of keyboard service.
    /// This will start default_hardware_ime as a keyboard layout provider.
    pub async fn new() -> Result<Service, Error> {
        let launcher = client::launcher().context("failed to connect to launcher")?;
        let ime = client::launch(&launcher, DEFAULT_IME_URL.to_string(), None)
            .context("Failed to launch default_hardware_ime")?;
        let layout_state = ime
            .connect_to_service::<ui_input2::KeyboardLayoutStateMarker>()
            .context("failed to connect to KeyboardLayoutState")?;

        let layout =
            layout_state.watch().await.context("failed to load initial keyboard layout")?;
        let layout = Arc::new(Mutex::new(layout));

        fuchsia_async::spawn(
            Service::spawn_layout_watcher(layout_state, layout.clone())
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        );

        Ok(Service { _ime: ime, layout, store: Arc::new(Mutex::new(Store::default())) })
    }

    /// Attempt to apply keyboard layout and dispatch key event to clients.
    pub async fn handle(&self, event: ui_input2::KeyEvent) -> Result<bool, Error> {
        let event = self.apply_layout(event, &self.layout).await;
        let was_handled = self.store.lock().await.dispatch_key(event).await?;
        Ok(was_handled)
    }

    /// Start serving fuchsia.ui.input2.Keyboard protocol.
    pub fn spawn_service(&self, mut stream: ui_input2::KeyboardRequestStream) {
        let store = self.store.clone();
        fuchsia_async::spawn(
            async move {
                // Store subscriber ids to cleanup once client disconnects.
                let mut subscriber_ids: Vec<usize> = Vec::new();
                while let Some(ui_input2::KeyboardRequest::SetListener {
                    view_ref,
                    listener,
                    responder,
                    ..
                }) = stream.try_next().await.context("error running keyboard service")?
                {
                    let id =
                        store.lock().await.add_new_subscriber(view_ref, listener.into_proxy()?);
                    subscriber_ids.push(id);
                    responder.send()?;
                }
                // Remove subscribers from the store.
                let mut store = store.lock().await;
                subscriber_ids.iter().for_each(|&i| store.remove_subscriber(i));
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        );
    }

    async fn spawn_layout_watcher(
        proxy: ui_input2::KeyboardLayoutStateProxy,
        layout: Arc<Mutex<ui_input2::KeyboardLayout>>,
    ) -> Result<(), Error> {
        while let Ok(new_layout) = proxy.watch().await {
            *layout.lock().await = new_layout;
        }
        Ok(())
    }

    async fn apply_layout(
        &self,
        mut event: ui_input2::KeyEvent,
        layout: &Arc<Mutex<ui_input2::KeyboardLayout>>,
    ) -> ui_input2::KeyEvent {
        let event_key = match event.key {
            None => return event,
            Some(key) => key,
        };
        let layout = layout.lock().await;
        let mut event_modifiers = event.modifiers.unwrap_or(ui_input2::Modifiers::empty());
        if event_modifiers.contains(ui_input2::Modifiers::Shift) {
            event_modifiers.remove(ui_input2::Modifiers::LeftShift);
            event_modifiers.remove(ui_input2::Modifiers::RightShift);
        }
        if event_modifiers.contains(ui_input2::Modifiers::Control) {
            event_modifiers.remove(ui_input2::Modifiers::LeftControl);
            event_modifiers.remove(ui_input2::Modifiers::RightControl);
        }
        if event_modifiers.contains(ui_input2::Modifiers::Alt) {
            event_modifiers.remove(ui_input2::Modifiers::LeftAlt);
            event_modifiers.remove(ui_input2::Modifiers::RightAlt);
        }
        if event_modifiers.contains(ui_input2::Modifiers::Meta) {
            event_modifiers.remove(ui_input2::Modifiers::LeftMeta);
            event_modifiers.remove(ui_input2::Modifiers::RightMeta);
        }

        if let Some(semantic_key_maps) = &layout.semantic_key_map {
            event.semantic_key = semantic_key_maps.iter().find_map(
                |ui_input2::SemanticKeyMap { modifiers, optional_modifiers, entries }| {
                    let entries = match entries {
                        None => return None,
                        Some(entries) => entries,
                    };
                    let modifiers = modifiers.unwrap_or(ui_input2::Modifiers::empty());
                    // Event modifiers must have keymap.modifiers set.
                    if !event_modifiers.contains(modifiers) {
                        return None;
                    }
                    let optional_modifiers =
                        optional_modifiers.unwrap_or(ui_input2::Modifiers::empty());
                    // Event modifiers may have keymap.modifiers or
                    // keymap.optional_modifiers set, but no other.
                    if !event_modifiers.sub(modifiers).sub(optional_modifiers).is_empty() {
                        return None;
                    }

                    let entry = entries
                        .iter()
                        .find(|ui_input2::SemanticKeyMapEntry { key, .. }| *key == event_key);
                    entry.map(|e| clone_semantic_key(&e.semantic_key))
                },
            );
        }
        event
    }
}

fn clone_semantic_key(semantic_key: &ui_input2::SemanticKey) -> ui_input2::SemanticKey {
    match semantic_key {
        ui_input2::SemanticKey::Symbol(symbol) => {
            ui_input2::SemanticKey::Symbol(symbol.to_string())
        }
        ui_input2::SemanticKey::Action(action) => ui_input2::SemanticKey::Action(*action),
        _ => panic!("UnknownVariant"),
    }
}

impl Store {
    /// Create and add new client subscriber to the store.
    fn add_new_subscriber(
        &mut self,
        view_ref: ui_views::ViewRef,
        listener: ui_input2::KeyListenerProxy,
    ) -> usize {
        let subscriber = Arc::new(Mutex::new(Subscriber { view_ref, listener }));
        self.last_id += 1;
        self.subscribers.push((self.last_id, subscriber));
        self.last_id
    }

    fn remove_subscriber(&mut self, id: usize) {
        let index = self.subscribers.iter().position(|t| t.0 == id);
        index.map(|i| self.subscribers.remove(i));
    }

    async fn dispatch_key(&self, event: ui_input2::KeyEvent) -> Result<bool, Error> {
        let subscribers = self.subscribers.iter().map(|t| t.1.clone()).into_iter();

        let was_handled = futures::stream::iter(subscribers)
            .fold(false, {
                // Capture by reference, since `async` non-`move` closures with
                // arguments are not currently supported
                let event = &event;
                move |was_handled, subscriber| async move {
                    if was_handled {
                        return was_handled;
                    }
                    let event = ui_input2::KeyEvent {
                        key: event.key,
                        modifiers: event.modifiers,
                        phase: event.phase,
                        physical_key: event.physical_key,
                        semantic_key: event.semantic_key.as_ref().map(clone_semantic_key),
                    };
                    let subscriber = subscriber.lock().await;
                    let handled = subscriber
                        .listener
                        .on_key_event(event)
                        .await
                        .map_err(Into::into)
                        .unwrap_or_else(|e: anyhow::Error| {
                            fx_log_err!("key listener handle error: {:?}", e);
                            ui_input2::Status::NotHandled
                        });
                    (handled == ui_input2::Status::Handled) || was_handled
                }
            })
            .await;

        Ok(was_handled)
    }
}

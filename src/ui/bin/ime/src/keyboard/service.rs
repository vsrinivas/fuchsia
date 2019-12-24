// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_component::client;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use std::ops::Sub;
use std::sync::Arc;

use crate::ime_service::ImeService;

static DEFAULT_IME_URL: &str =
    "fuchsia-pkg://fuchsia.com/ime_service#meta/default_hardware_ime.cmx";

struct Subscriber {
    pub view_ref: ui_views::ViewRef,
    pub listener: ui_input::KeyListenerProxy,
}

#[derive(Default)]
struct Store {
    subscribers: Vec<(usize, Arc<Mutex<Subscriber>>)>,
    last_id: usize,
}

pub struct Service {
    ime_service: ImeService,
    // Once ime leaves the scope, default_hardware_ime is unloaded.
    _ime: client::App,
    store: Arc<Mutex<Store>>,
    layout: Arc<Mutex<ui_input::KeyboardLayout>>,
}

fn clone_semantic_key(semantic_key: &ui_input::SemanticKey) -> ui_input::SemanticKey {
    match semantic_key {
        ui_input::SemanticKey::Symbol(symbol) => ui_input::SemanticKey::Symbol(symbol.to_string()),
        ui_input::SemanticKey::Action(action) => ui_input::SemanticKey::Action(*action),
        _ => panic!("UnknownVariant"),
    }
}

impl Store {
    /// Create and add new client subscriber to the store.
    pub fn add_new_subscriber(
        &mut self,
        view_ref: ui_views::ViewRef,
        listener: ui_input::KeyListenerProxy,
    ) -> usize {
        let subscriber = Arc::new(Mutex::new(Subscriber { view_ref, listener }));
        self.last_id = self.last_id + 1;
        self.subscribers.push((self.last_id, subscriber.clone()));
        self.last_id
    }

    pub fn remove_subscriber(&mut self, id: usize) {
        let index = self.subscribers.iter().position(|t| t.0 == id);
        match index {
            Some(index) => {
                self.subscribers.remove(index);
            }
            _ => (),
        }
    }

    pub async fn dispatch_key(&self, event: ui_input::KeyEvent) -> Result<bool, Error> {
        let subscribers = self.subscribers.iter().cloned().map(|t| t.1).into_iter();

        // TODO: sort according to ViewRef hierarchy
        // TODO: resolve accounting for use_priority

        let was_handled = futures::stream::iter(subscribers)
            .fold(false, {
                // Capture by reference, since `async` non-`move` closures with
                // arguments are not currently supported
                let event = &event;
                move |was_handled, subscriber| async move {
                    let event = ui_input::KeyEvent {
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
                            ui_input::Status::NotHandled
                        });
                    (handled == ui_input::Status::Handled) || was_handled
                }
            })
            .await;

        Ok(was_handled)
    }
}

impl Service {
    pub async fn new(ime_service: ImeService) -> Result<Service, Error> {
        let launcher = client::launcher().expect("failed to connect to launcher");
        let ime = client::launch(&launcher, DEFAULT_IME_URL.to_string(), None)
            .context("Failed to launch default_hardware_ime")?;
        let layout_state = ime
            .connect_to_service::<ui_input::KeyboardLayoutStateMarker>()
            .context("failed to connect to KeyboardLayoutState")?;

        let layout =
            layout_state.watch().await.context("failed to load initial keyboard layout")?;
        let layout = Arc::new(Mutex::new(layout));

        fuchsia_async::spawn(
            Service::spawn_layout_watcher(layout_state, layout.clone())
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        );

        Ok(Service {
            ime_service,
            _ime: ime,
            layout,
            store: Arc::new(Mutex::new(Store::default())),
        })
    }

    async fn apply_layout(
        mut event: ui_input::KeyEvent,
        layout: &Arc<Mutex<ui_input::KeyboardLayout>>,
    ) -> ui_input::KeyEvent {
        let layout = layout.lock().await;
        let event_key = match event.key {
            None => return event,
            Some(key) => key,
        };
        let mut event_modifiers = event.modifiers.unwrap_or(ui_input::Modifiers::empty());
        if event_modifiers.contains(ui_input::Modifiers::Shift) {
            event_modifiers.remove(ui_input::Modifiers::LeftShift);
            event_modifiers.remove(ui_input::Modifiers::RightShift);
        }
        if event_modifiers.contains(ui_input::Modifiers::Control) {
            event_modifiers.remove(ui_input::Modifiers::LeftControl);
            event_modifiers.remove(ui_input::Modifiers::RightControl);
        }
        if event_modifiers.contains(ui_input::Modifiers::Alt) {
            event_modifiers.remove(ui_input::Modifiers::LeftAlt);
            event_modifiers.remove(ui_input::Modifiers::RightAlt);
        }
        if event_modifiers.contains(ui_input::Modifiers::Meta) {
            event_modifiers.remove(ui_input::Modifiers::LeftMeta);
            event_modifiers.remove(ui_input::Modifiers::RightMeta);
        }
        // TODO: apply key_map for physical key mapping
        if let Some(semantic_key_maps) = &layout.semantic_key_map {
            event.semantic_key = semantic_key_maps.iter().find_map(
                |ui_input::SemanticKeyMap { modifiers, optional_modifiers, entries }| {
                    let entries = match entries {
                        None => return None,
                        Some(entries) => entries,
                    };
                    let modifiers = modifiers.unwrap_or(ui_input::Modifiers::empty());
                    // Event modifiers must have keymap.modifiers set.
                    if !event_modifiers.contains(modifiers) {
                        return None;
                    }
                    let optional_modifiers =
                        optional_modifiers.unwrap_or(ui_input::Modifiers::empty());
                    // Event modifiers may have keymap.modifiers or
                    // keymap.optional_modifiers set, but no other.
                    if !event_modifiers.sub(modifiers).sub(optional_modifiers).is_empty() {
                        return None;
                    }

                    let entry = entries
                        .iter()
                        .find(|ui_input::SemanticKeyMapEntry { key, .. }| *key == event_key);
                    entry.map(|e| clone_semantic_key(&e.semantic_key))
                },
            );
        }
        event
    }

    pub async fn spawn_layout_watcher(
        proxy: ui_input::KeyboardLayoutStateProxy,
        layout: Arc<Mutex<ui_input::KeyboardLayout>>,
    ) -> Result<(), Error> {
        while let Ok(new_layout) = proxy.watch().await {
            *layout.lock().await = new_layout;
        }
        Ok(())
    }

    pub fn spawn_ime_service(&self, mut stream: uii::ImeServiceRequestStream) {
        let layout = self.layout.clone();
        let mut ime_service = self.ime_service.clone();
        let store = self.store.clone();
        fuchsia_async::spawn(
            async move {
                while let Some(msg) =
                    stream.try_next().await.context("error running keyboard service")?
                {
                    match msg {
                        uii::ImeServiceRequest::DispatchKey { event, responder, .. } => {
                            let store = store.lock().await;
                            let event = Service::apply_layout(event, &layout).await;
                            let was_handled = store.dispatch_key(event).await?;
                            responder
                                .send(was_handled)
                                .context("error responding to DispatchKey")?;
                        }
                        _ => {
                            ime_service
                                .handle_ime_service_msg(msg)
                                .await
                                .context("Handle IME service messages")?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        );
    }

    pub fn spawn_keyboard_service(&self, mut stream: ui_input::KeyboardRequestStream) {
        let store = self.store.clone();
        fuchsia_async::spawn(
            async move {
                // Store subscriber ids to cleanup once client disconnects.
                let mut subscriber_ids: Vec<usize> = Vec::new();
                while let Some(msg) =
                    stream.try_next().await.context("error running keyboard service")?
                {
                    match msg {
                        ui_input::KeyboardRequest::SetListener {
                            view_ref,
                            listener,
                            responder,
                            ..
                        } => {
                            let id = store
                                .lock()
                                .await
                                .add_new_subscriber(view_ref, listener.into_proxy()?);
                            subscriber_ids.push(id);
                            responder.send()?;
                        }
                    }
                }
                // Remove subscribers from the store.
                let mut store = store.lock().await;
                subscriber_ids.iter().for_each(|&i| store.remove_subscriber(i));
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        );
    }
}

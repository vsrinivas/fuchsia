// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use std::sync::Arc;

use crate::ime_service::ImeService;

pub struct Subscriber {
    pub view_ref: ui_views::ViewRef,
    pub listener: ui_input::KeyListenerProxy,
}

#[derive(Default)]
pub struct Store {
    subscribers: Vec<(usize, Arc<Mutex<Subscriber>>)>,
    last_id: usize,
}

pub struct Service {
    ime_service: ImeService,
    store: Arc<Mutex<Store>>,
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

        let was_handled = futures::stream::iter(subscribers).fold(false, {
            // Capture variables by reference, since `async` non-`move` closures with
            // arguments are not currently supported
            let (key, modifiers, phase) = (event.key, event.modifiers, event.phase);
            move |was_handled, subscriber| {
                async move {
                    let event = ui_input::KeyEvent { key, modifiers, phase };
                    let subscriber = subscriber.lock().await;
                    let handled = subscriber
                        .listener
                        .on_key_event(event)
                        .await
                        .map_err(Into::into)
                        .unwrap_or_else(|e: failure::Error| {
                            fx_log_err!("key listener handle error: {:?}", e);
                            ui_input::Status::NotHandled
                        });
                    (handled == ui_input::Status::Handled) || was_handled
                }
            }
        });

        Ok(was_handled.await)
    }
}

impl Service {
    pub fn new(ime_service: ImeService) -> Service {
        Service { store: Arc::new(Mutex::new(Store::default())), ime_service }
    }

    pub fn spawn_ime_service(&self, mut stream: uii::ImeServiceRequestStream) {
        let store = self.store.clone();
        let mut ime_service = self.ime_service.clone();
        fuchsia_async::spawn(
            async move {
                while let Some(msg) =
                    stream.try_next().await.context("error running keyboard service")?
                {
                    match msg {
                        uii::ImeServiceRequest::DispatchKey { event, responder, .. } => {
                            let store = store.lock().await;
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
                .unwrap_or_else(|e: failure::Error| fx_log_err!("couldn't run: {:?}", e)),
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
                        ui_input::KeyboardRequest::SetListener { view_ref, listener, .. } => {
                            let id = store
                                .lock()
                                .await
                                .add_new_subscriber(view_ref, listener.into_proxy()?);
                            subscriber_ids.push(id);
                        }
                    }
                }
                // Remove subscribers from the store.
                let mut store = store.lock().await;
                subscriber_ids.iter().for_each(|&i| store.remove_subscriber(i));
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| fx_log_err!("couldn't run: {:?}", e)),
        );
    }
}

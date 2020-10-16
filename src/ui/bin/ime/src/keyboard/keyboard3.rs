// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::{Context as _, Error};
use fidl_fuchsia_ui_input3 as ui_input3;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_scenic as scenic;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon::AsHandleRef;
use futures::lock::Mutex;
use futures::TryStreamExt;
use std::collections::HashMap;
use std::sync::Arc;

/// Provides implementation for fuchsia.ui.input3.Keyboard FIDL.
#[derive(Clone)]
pub struct KeyboardService {
    store: Arc<Mutex<KeyListenerStore>>,
}

/// Holder for fuchsia.ui.input3.KeyboardListener client requests.
#[derive(Default)]
struct KeyListenerStore {
    // Client subscribers for the Keyboard service, keyed
    // by self-generated monotonic id.
    subscribers: HashMap<usize, Arc<Mutex<Subscriber>>>,

    // Last monotonic id used as a subscribers hash key.
    last_id: usize,

    // Currently focused View.
    focused_view: Option<ui_views::ViewRef>,
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
    pub async fn handle_key_event(&self, event: ui_input3::KeyEvent) -> Result<bool, Error> {
        Ok(self.store.lock().await.dispatch_key(event).await?)
    }

    /// Handle focus change.
    pub async fn handle_focus_change(&self, focused_view: ui_views::ViewRef) {
        self.store.lock().await.focused_view =
            Some(scenic::duplicate_view_ref(&focused_view).expect("valid view_ref"));
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

    fn is_focused(&self, view_ref: &ui_views::ViewRef) -> bool {
        if let Some(focused_view) = &self.focused_view {
            focused_view.reference.as_handle_ref().get_koid()
                == view_ref.reference.as_handle_ref().get_koid()
        } else {
            false
        }
    }

    async fn dispatch_key(&self, event: ui_input3::KeyEvent) -> Result<bool, Error> {
        let subscribers = self
            .subscribers
            .iter()
            .map(|(_view_ref, listener)| listener.clone())
            .into_iter()
            .map(|r| Ok(r));

        // Early exit for `try_for_each()` on error is used to propagate shortcut handle success.
        // When shortcut was handled, closure returns a `Err(KeyEventStatus::Handled)`.
        let result = futures::stream::iter(subscribers)
            .try_for_each({
                // Capture by reference, since `async` non-`move` closures with
                // arguments are not currently supported
                let event = &event;
                move |subscriber| async move {
                    let subscriber = subscriber.lock().await;
                    if !self.is_focused(&subscriber.view_ref) {
                        return Ok(());
                    }
                    let event = ui_input3::KeyEvent {
                        key: event.key,
                        modifiers: event.modifiers,
                        timestamp: event.timestamp,
                        type_: event.type_,
                    };
                    let handled =
                        subscriber.listener.on_key_event(event).await.unwrap_or_else(|e| {
                            fx_log_err!("key listener handle error: {:?}", e);
                            ui_input3::KeyEventStatus::NotHandled
                        });
                    if handled == ui_input3::KeyEventStatus::Handled {
                        return Err(handled);
                    } else {
                        return Ok(());
                    }
                }
            })
            .await;

        match result {
            Err(ui_input3::KeyEventStatus::Handled) => Ok(true),
            _ => Ok(false),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_input as input;
    use fuchsia_async as fasync;
    use futures::{future, StreamExt, TryFutureExt};

    fn create_key_event(key: input::Key, modifiers: ui_input3::Modifiers) -> ui_input3::KeyEvent {
        ui_input3::KeyEvent {
            timestamp: None,
            key: Some(key),
            modifiers: Some(modifiers),
            type_: Some(ui_input3::KeyEventType::Pressed),
        }
    }

    // Create and setup a fake keyboard client.
    // Returns fake client viewref and keyboard listener.
    async fn create_fake_client(
        keyboard_proxy: &ui_input3::KeyboardProxy,
    ) -> Result<(ui_views::ViewRef, ui_input3::KeyboardListenerRequestStream), Error> {
        let (listener_client_end, listener) =
            fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
        let view_ref = scenic::ViewRefPair::new()?.view_ref;

        keyboard_proxy
            .add_listener(&mut scenic::duplicate_view_ref(&view_ref)?, listener_client_end)
            .await
            .expect("add_listener");
        Ok((view_ref, listener))
    }

    async fn expect_key_and_modifiers(
        listener: &mut ui_input3::KeyboardListenerRequestStream,
        key: input::Key,
        modifiers: ui_input3::Modifiers,
    ) {
        if let Some(Ok(ui_input3::KeyboardListenerRequest::OnKeyEvent {
            event, responder, ..
        })) = listener.next().await
        {
            responder
                .send(ui_input3::KeyEventStatus::Handled)
                .expect("responding from key listener");
            assert_eq!(event.key, Some(key));
            assert_eq!(event.modifiers, Some(modifiers));
        } else {
            panic!("Expected key error: {:?}", (key, modifiers));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_client() -> Result<(), Error> {
        // Start the keyboard service.
        let (keyboard_proxy, keyboard_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ui_input3::KeyboardMarker>()
                .expect("Failed to create KeyboardProxy and stream.");

        let service = Arc::new(KeyboardService::new().await?);
        let service_clone = service.clone();
        fuchsia_async::Task::spawn(
            async move { service_clone.spawn_service(keyboard_request_stream).await }
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();

        let (view_ref, mut listener) = create_fake_client(&keyboard_proxy).await?;
        service.handle_focus_change(scenic::duplicate_view_ref(&view_ref)?).await;

        let (key, modifiers) = (input::Key::A, ui_input3::Modifiers::CapsLock);
        let dispatched_event = create_key_event(key, modifiers);

        let (was_handled, _) = future::join(
            service.handle_key_event(dispatched_event),
            expect_key_and_modifiers(&mut listener, key, modifiers),
        )
        .await;

        assert_eq!(was_handled?, true);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_not_focused() -> Result<(), Error> {
        // Start the keyboard service.
        let (keyboard_proxy, keyboard_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ui_input3::KeyboardMarker>()
                .expect("Failed to create KeyboardProxy and stream.");

        let service = Arc::new(KeyboardService::new().await?);
        let service_clone = service.clone();
        fuchsia_async::Task::spawn(
            async move { service_clone.spawn_service(keyboard_request_stream).await }
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();

        create_fake_client(&keyboard_proxy).await?;
        let (key, modifiers) = (input::Key::A, ui_input3::Modifiers::CapsLock);
        let was_handled = service.handle_key_event(create_key_event(key, modifiers)).await?;
        assert_eq!(was_handled, false);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_switching_focus() -> Result<(), Error> {
        // Start the keyboard service.
        let (keyboard_proxy, keyboard_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ui_input3::KeyboardMarker>()
                .expect("Failed to create KeyboardProxy and stream.");

        let service = Arc::new(KeyboardService::new().await?);
        let service_clone = service.clone();
        fuchsia_async::Task::spawn(
            async move { service_clone.spawn_service(keyboard_request_stream).await }
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();

        // Create fake clients.
        let (view_ref, mut listener) = create_fake_client(&keyboard_proxy).await?;
        let (other_view_ref, mut other_listener) = create_fake_client(&keyboard_proxy).await?;

        // Set focus to the first fake client.
        service.handle_focus_change(scenic::duplicate_view_ref(&view_ref)?).await;

        // Scope part of the test case to release listeners and borrows once done.
        {
            let (key, modifiers) = (input::Key::A, ui_input3::Modifiers::CapsLock);
            let dispatched_event = create_key_event(key, modifiers);

            // Setup key handing for both clients.
            let expect_key_fut = expect_key_and_modifiers(&mut listener, key, modifiers);
            let expect_other_key_fut =
                expect_key_and_modifiers(&mut other_listener, key, modifiers);
            futures::pin_mut!(expect_key_fut);
            futures::pin_mut!(expect_other_key_fut);

            let (was_handled, activated_listener) = future::join(
                service.handle_key_event(dispatched_event),
                // Correct listener is passed as first parameter to be resolved as Left.
                future::select(expect_key_fut, expect_other_key_fut),
            )
            .await;

            assert_eq!(was_handled?, true);
            assert!(matches!(activated_listener, future::Either::Left { .. }));
        }

        // Change focus to another client.
        service.handle_focus_change(scenic::duplicate_view_ref(&other_view_ref)?).await;

        // Scope part of the test case to release listeners and borrows once done.
        {
            let (key, modifiers) = (input::Key::B, ui_input3::Modifiers::NumLock);
            let dispatched_event = create_key_event(key, modifiers);

            // Setup key handing for both clients.
            let expect_key_fut = expect_key_and_modifiers(&mut listener, key, modifiers);
            let expect_other_key_fut =
                expect_key_and_modifiers(&mut other_listener, key, modifiers);
            futures::pin_mut!(expect_key_fut);
            futures::pin_mut!(expect_other_key_fut);

            let (was_handled, activated_listener) = future::join(
                service.handle_key_event(dispatched_event),
                // Correct listener is passed as first parameter to be resolved as Left.
                future::select(expect_other_key_fut, expect_key_fut),
            )
            .await;

            assert_eq!(was_handled?, true);
            assert!(matches!(activated_listener, future::Either::Left { .. }));
        }

        Ok(())
    }
}

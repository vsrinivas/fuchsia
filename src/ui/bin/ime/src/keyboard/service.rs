// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_ui_input as ui_input,
    fidl_fuchsia_ui_input3::{self as ui_input3, KeyMeaning, NonPrintableKey},
    fuchsia_syslog::{fx_log_debug, fx_log_err, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{TryFutureExt, TryStreamExt},
};

use fidl_fuchsia_ui_keyboard_focus as fidl_focus;

use crate::ime_service::ImeService;
use crate::keyboard::{events::KeyEvent, keyboard3};

/// Keyboard service router.
/// Starts providers of input3.Keyboard.
/// Handles keyboard events, routing them to one of the following:
/// - other fuchsia.ui.input.ImeService events to ime_service
pub struct Service {
    ime_service: ImeService,
    keyboard3: keyboard3::KeyboardService,
    clock: std::sync::Arc<zx::Clock>,
}

impl Service {
    pub async fn new(ime_service: ImeService) -> Result<Service, Error> {
        let keyboard3 = keyboard3::KeyboardService::new();
        let clock = std::sync::Arc::new(zx::Clock::create(zx::ClockOpts::MONOTONIC, None).unwrap());
        Ok(Service { ime_service, keyboard3, clock })
    }

    /// Starts a task that processes `fuchsia.ui.keyboard.focus.Controller`.
    /// This method returns immediately without blocking.
    pub fn spawn_focus_controller(&self, mut stream: fidl_focus::ControllerRequestStream) {
        let keyboard3 = self.keyboard3.clone();
        let clock = self.clock.clone();
        fuchsia_async::Task::spawn(
            async move {
                while let Some(msg) = stream.try_next().await.context(concat!(
                    "keyboard::Service::spawn_focus_controller: ",
                    "error while reading the request stream for ",
                    "fuchsia.ui.keyboard.focus.Controller"
                ))? {
                    match msg {
                        fidl_focus::ControllerRequest::Notify { view_ref, responder, .. } => {
                            let view_ref = keyboard3::ViewRef::new(view_ref);
                            let now = clock
                                .read()
                                .map_err(|e| {
                                    fx_log_err!("couldn't read clock: {:?}", e);
                                    e
                                })
                                .unwrap_or(zx::Time::ZERO);
                            keyboard3.handle_focus_change(view_ref, now).await;
                            responder.send()?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();
    }

    /// Starts a task that processes `fuchsia.ui.input3.KeyEventInjector` requests.
    /// This method returns immediately without blocking.
    pub fn spawn_key_event_injector(&self, mut stream: ui_input3::KeyEventInjectorRequestStream) {
        let mut keyboard3 = self.keyboard3.clone();
        let mut ime_service = self.ime_service.clone();
        fuchsia_async::Task::spawn(
            async move {
                while let Some(msg) = stream.try_next().await.context(concat!(
                    "keyboard::Service::spawn_key_event_injector: ",
                    "error while reading the request stream for ",
                    "fuchsia.ui.input3.KeyEventInjector"
                ))? {
                    match msg {
                        ui_input3::KeyEventInjectorRequest::Inject {
                            mut key_event,
                            responder,
                            ..
                        } => {
                            key_event.key = key_event.key.or_else(|| match key_event.key_meaning {
                                Some(KeyMeaning::NonPrintableKey(k)) => {
                                    key_from_non_printable_key(k)
                                }
                                Some(KeyMeaning::Codepoint(_)) => None,
                                None => None,
                            });
                            ime_service
                                .inject_input(KeyEvent::new(
                                    &key_event,
                                    keyboard3.get_keys_pressed().await,
                                )?)
                                .await
                                .unwrap_or_else(|e| {
                                    // Most of the time this is not a real error: what it actually
                                    // means is that we tried to offer text input to a text edit
                                    // field, but no such field is currently in focus.  Therefore
                                    // increasing the verbosity of the message, so that it's only
                                    // printed when you *really* need it for debugging.
                                    fx_log_debug!(
                                        concat!(
                                            "keyboard::Service::spawn_key_event_injector: ",
                                            "error injecting input into IME: {:?}"
                                        ),
                                        e
                                    )
                                });
                            let was_handled = if keyboard3
                                .handle_key_event(key_event)
                                .await
                                .context("error handling input3 keyboard event")?
                            {
                                ui_input3::KeyEventStatus::Handled
                            } else {
                                ui_input3::KeyEventStatus::NotHandled
                            };
                            responder.send(was_handled).context(concat!(
                                "keyboard::Service::spawn_key_event_injector: ",
                                "error while sending response"
                            ))?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();
    }

    pub fn spawn_ime_service(&self, mut stream: ui_input::ImeServiceRequestStream) {
        let mut ime_service = self.ime_service.clone();
        fuchsia_async::Task::spawn(
            async move {
                while let Some(msg) =
                    stream.try_next().await.context("error running keyboard service")?
                {
                    ime_service
                        .handle_ime_service_msg(msg)
                        .await
                        .context("Handle IME service messages")?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();
    }

    pub fn spawn_keyboard3_service(&self, stream: ui_input3::KeyboardRequestStream) {
        let keyboard3 = self.keyboard3.clone();
        fuchsia_async::Task::spawn(
            async move { keyboard3.spawn_service(stream).await }
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        )
        .detach();
    }
}

fn key_from_non_printable_key(
    non_printable_key: NonPrintableKey,
) -> Option<fidl_fuchsia_input::Key> {
    match non_printable_key {
        NonPrintableKey::Enter => Some(fidl_fuchsia_input::Key::Enter),
        NonPrintableKey::Tab => Some(fidl_fuchsia_input::Key::Tab),
        NonPrintableKey::Backspace => Some(fidl_fuchsia_input::Key::Backspace),
        unrecognized => {
            fx_log_warn!("received unrecognized NonPrintableKey {:?}", unrecognized);
            None
        }
    }
}

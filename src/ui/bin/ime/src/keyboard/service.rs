// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_ui_input as ui_input, fidl_fuchsia_ui_input3 as ui_input3,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::{TryFutureExt, TryStreamExt},
};

use crate::ime_service::ImeService;
use crate::keyboard::{events::KeyEvent, keyboard3};

/// Keyboard service router.
/// Starts providers of input3.Keyboard.
/// Handles keyboard events, routing them to one of the following:
/// - other fuchsia.ui.input.ImeService events to ime_service
pub struct Service {
    ime_service: ImeService,
    keyboard3: keyboard3::KeyboardService,
}

impl Service {
    pub async fn new(ime_service: ImeService) -> Result<Service, Error> {
        let keyboard3 = keyboard3::KeyboardService::new();
        Ok(Service { ime_service, keyboard3 })
    }

    pub fn spawn_ime_service(&self, mut stream: ui_input::ImeServiceRequestStream) {
        let mut keyboard3 = self.keyboard3.clone();
        let mut ime_service = self.ime_service.clone();
        fuchsia_async::Task::spawn(
            async move {
                while let Some(msg) =
                    stream.try_next().await.context("error running keyboard service")?
                {
                    match msg {
                        ui_input::ImeServiceRequest::ViewFocusChanged {
                            view_ref,
                            responder,
                            ..
                        } => {
                            let view_ref = keyboard3::ViewRef::new(view_ref);
                            keyboard3.handle_focus_change(view_ref).await;
                            responder.send()?;
                        }
                        ui_input::ImeServiceRequest::DispatchKey3 { event, responder, .. } => {
                            let key_event =
                                KeyEvent::new(&event, keyboard3.get_keys_pressed().await)?;
                            ime_service.inject_input(key_event.clone()).await.unwrap_or_else(|e| {
                                fx_log_warn!("error injecting input into IME: {:?}", e)
                            });
                            let was_handled = keyboard3
                                .handle_key_event(event)
                                .await
                                .context("error handling input3 keyboard event")?;
                            responder
                                .send(was_handled)
                                .context("error responding to DispatchKey3")?;
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

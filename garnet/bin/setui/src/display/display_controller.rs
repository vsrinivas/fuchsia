// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::registry::base::{Command, Notifier, State},
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::{brightness_info, SettingRequest, SettingResponse, SettingType},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
    std::sync::{Arc, RwLock},
};

/// Controller that handles commands for SettingType::Display.
/// TODO(ejia): refactor out common code
/// TODO(ejia): store persistently
pub fn spawn_display_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (display_handler_tx, mut display_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(async move {

        let brightness_service = service_context_handle
            .read()
            .unwrap()
            .connect::<fidl_fuchsia_ui_brightness::ControlMarker>()
            .unwrap();

        // TODO(fxb/35004): Listen to changes using hanging get as well
        // TODO: Better handling of service not working, coming in persistent storage CL
        let auto_brightness = match brightness_service.watch_auto_brightness().await {
            Ok(auto_brightness) => auto_brightness,
            Err(e) => {
                fx_log_err!("failed getting auto-brightness, {}", e);
                return;
            }
        };
        let brightness_value = match brightness_service.watch_current_brightness().await {
            Ok(brightness_value) => brightness_value,
            Err(e) => {
                fx_log_err!("failed getting brightness_value, {}", e);
                return;
            }
        };

        // last set brightness is needed to give a value when turning off auto brightness
        let last_set_brightness = Arc::new(RwLock::new(brightness_value));

        // TODO(ejia): replace with persistent state
        let brightness_state =
            Arc::new(RwLock::new(brightness_info(auto_brightness, Some(brightness_value))));

        while let Some(command) = display_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        *notifier_lock.write().unwrap() = Some(notifier);
                    }
                    State::EndListen => {
                        *notifier_lock.write().unwrap() = None;
                    }
                },
                Command::HandleRequest(request, responder) => {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::SetBrightness(brightness_value) => {

                            *last_set_brightness.write().unwrap() = brightness_value;
                            *brightness_state.write().unwrap() =
                                brightness_info(false, Some(brightness_value));

                            brightness_service.set_manual_brightness(brightness_value).unwrap();
                            responder.send(Ok(None)).unwrap();
                            if let Some(notifier) = (*notifier_lock.read().unwrap()).clone() {
                                notifier.unbounded_send(SettingType::Display).unwrap();
                            }
                        }
                        SettingRequest::SetAutoBrightness(auto_brightness_enabled) => {
                            if auto_brightness_enabled {
                                *brightness_state.write().unwrap() = brightness_info(true, None);
                                brightness_service.set_auto_brightness().unwrap();
                            } else {
                                let brightness_value = *last_set_brightness.read().unwrap();
                                *brightness_state.write().unwrap() =
                                    brightness_info(false, Some(brightness_value));
                                brightness_service.set_manual_brightness(brightness_value).unwrap();
                            }
                            responder.send(Ok(None)).unwrap();
                            // TODO: watch for changes on current brightness and notify changes
                            // that way instead.
                            if let Some(notifier) = (*notifier_lock.read().unwrap()).clone() {
                                notifier.unbounded_send(SettingType::Display).unwrap();
                            }
                        }
                        SettingRequest::Get => {
                            responder
                                .send(Ok(Some(SettingResponse::Brightness(
                                    *brightness_state.read().unwrap(),
                                ))))
                                .unwrap();
                        }
                        _ => panic!("Unexpected command to brightness"),
                    }
                }
            }
        }
    });
    display_handler_tx
}

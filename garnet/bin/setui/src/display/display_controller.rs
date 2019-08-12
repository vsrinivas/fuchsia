// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::registry::base::{Command, Notifier, State},
    crate::switchboard::base::{BrightnessInfo, SettingRequest, SettingResponse, SettingType},
    fuchsia_async as fasync,
    futures::StreamExt,
    std::sync::{Arc, RwLock},
};

/// Controller that handles commands for SettingType::Display.
/// TODO(ejia): refactor out common code
/// TODO(ejia): store persistently
/// TODO(ejia): replace with new service
pub fn spawn_display_controller(
    brightness_service: fidl_fuchsia_device_display::ManagerProxy,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (display_handler_tx, mut display_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(async move {
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
                            brightness_service.set_brightness(brightness_value.into()).await
                                .unwrap();
                            responder.send(Ok(None)).unwrap();
                            if let Some(notifier) = (*notifier_lock.read().unwrap()).clone() {
                                notifier.unbounded_send(SettingType::Display).unwrap();
                            }
                        }
                        SettingRequest::SetAutoBrightness(_brightness_enabled) => {
                            // TODO: implement when connecting to brightness service
                            responder.send(Err(failure::err_msg("unimplemented"))).unwrap();
                        }
                        SettingRequest::Get => {
                            let (_success, value) =
                                brightness_service.get_brightness().await.unwrap();
                            responder
                                .send(Ok(Some(SettingResponse::Brightness(
                                    BrightnessInfo::ManualBrightness(value as f32),
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

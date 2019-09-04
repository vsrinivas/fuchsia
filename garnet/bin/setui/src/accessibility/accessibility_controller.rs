// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::registry::base::{Command, Notifier, State},
    crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible},
    crate::switchboard::base::{
        AccessibilityInfo, ColorBlindnessType, SettingRequest, SettingRequestResponder,
        SettingResponse, SettingType,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::stream::StreamExt,
    futures::TryFutureExt,
    std::sync::{Arc, RwLock},
};

impl DeviceStorageCompatible for AccessibilityInfo {
    const DEFAULT_VALUE: Self =
        AccessibilityInfo { audio_description: false, color_correction: ColorBlindnessType::None };
    const KEY: &'static str = "accessibility_info";
}

/// Controller that handles commands for SettingType::Accessibility.
pub fn spawn_accessibility_controller(
    storage: Arc<Mutex<DeviceStorage<AccessibilityInfo>>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (accessibility_handler_tx, mut accessibility_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    // TODO(fxb/35532): switch to parking_lot
    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(
        async move {
            // Local copy of persisted audio description value.
            let mut stored_value: AccessibilityInfo;
            {
                let mut storage_lock = storage.lock().await;
                stored_value = storage_lock.get().await;
            }

            while let Some(command) = accessibility_handler_rx.next().await {
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
                            SettingRequest::SetAudioDescription(audio_description) => {
                                stored_value.audio_description = audio_description;
                                persist_accessibility_info(
                                    stored_value,
                                    storage.clone(),
                                    responder,
                                )
                                .await;

                                // Notify listeners of value change.
                                if let Some(notifier) = (*notifier_lock.read().unwrap()).clone() {
                                    notifier.unbounded_send(SettingType::Accessibility)?;
                                }
                            }
                            SettingRequest::SetColorCorrection(color_correction) => {
                                stored_value.color_correction = color_correction;
                                persist_accessibility_info(
                                    stored_value,
                                    storage.clone(),
                                    responder,
                                )
                                .await;

                                // Notify listeners of value change.
                                if let Some(notifier) = (*notifier_lock.read().unwrap()).clone() {
                                    notifier.unbounded_send(SettingType::Accessibility)?;
                                }
                            }
                            SettingRequest::Get => {
                                let _ = responder.send(Ok(Some(SettingResponse::Accessibility(
                                    stored_value.clone(),
                                ))));
                            }
                            _ => panic!("Unexpected command to accessibility"),
                        }
                    }
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: failure::Error| {
                fx_log_err!("Error processing accessibility command: {:?}", e)
            }),
    );
    accessibility_handler_tx
}

async fn persist_accessibility_info(
    info: AccessibilityInfo,
    storage: Arc<Mutex<DeviceStorage<AccessibilityInfo>>>,
    responder: SettingRequestResponder,
) {
    let write_request = storage.lock().await.write(info);
    let _ = match write_request {
        Ok(_) => responder.send(Ok(None)),
        Err(err) => responder
            .send(Err(failure::format_err!("failed to persist accessibility_info:{}", err))),
    };
}

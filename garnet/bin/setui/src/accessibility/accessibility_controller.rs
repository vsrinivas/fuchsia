// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::registry::base::{Command, Notifier, State},
    crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible},
    crate::switchboard::accessibility_types::AccessibilityInfo,
    crate::switchboard::base::{
        Merge, SettingRequest, SettingRequestResponder, SettingResponse, SettingType,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::stream::StreamExt,
    futures::TryFutureExt,
    parking_lot::RwLock,
    std::sync::Arc,
};

impl DeviceStorageCompatible for AccessibilityInfo {
    const KEY: &'static str = "accessibility_info";

    fn default_value() -> Self {
        AccessibilityInfo {
            audio_description: None,
            screen_reader: None,
            color_inversion: None,
            enable_magnification: None,
            color_correction: None,
            captions_settings: None,
        }
    }
}

/// Controller that handles commands for SettingType::Accessibility.
pub fn spawn_accessibility_controller(
    storage: Arc<Mutex<DeviceStorage<AccessibilityInfo>>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (accessibility_handler_tx, mut accessibility_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

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
                            *notifier_lock.write() = Some(notifier);
                        }
                        State::EndListen => {
                            *notifier_lock.write() = None;
                        }
                    },
                    Command::HandleRequest(request, responder) => {
                        #[allow(unreachable_patterns)]
                        match request {
                            SettingRequest::Get => {
                                let _ = responder.send(Ok(Some(SettingResponse::Accessibility(
                                    stored_value.clone(),
                                ))));
                                // Done handling request, no need to notify listeners or persist.
                                continue;
                            }
                            SettingRequest::SetAccessibilityInfo(info) => {
                                let old_value = stored_value.clone();

                                stored_value = info.merge(stored_value);

                                if old_value == stored_value {
                                    // No change in value, no need to notify listeners or persist.
                                    continue;
                                }
                            }
                            _ => panic!("Unexpected command to accessibility"),
                        }

                        // Notify listeners of value change.
                        if let Some(notifier) = (*notifier_lock.read()).clone() {
                            notifier.unbounded_send(SettingType::Accessibility)?;
                        }

                        // Persist the new value.
                        persist_accessibility_info(stored_value, storage.clone(), responder).await;
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| {
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
    let mut storage_lock = storage.lock().await;
    let write_request = storage_lock.write(&info, false).await;
    let _ = match write_request {
        Ok(_) => responder.send(Ok(None)),
        Err(err) => {
            responder.send(Err(anyhow::format_err!("failed to persist accessibility_info:{}", err)))
        }
    };
}

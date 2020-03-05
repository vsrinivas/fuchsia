// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::registry::base::{Command, Context, Notifier, SettingHandler, State},
    crate::registry::device_storage::{DeviceStorageCompatible, DeviceStorageFactory},
    crate::switchboard::base::*,
    fuchsia_async as fasync,
    futures::StreamExt,
    parking_lot::RwLock,
    std::sync::Arc,
};

impl DeviceStorageCompatible for SystemInfo {
    const KEY: &'static str = "system_info";

    fn default_value() -> Self {
        SystemInfo { login_override_mode: SystemLoginOverrideMode::None }
    }
}

pub fn spawn_system_controller<T: DeviceStorageFactory + Send + Sync + 'static>(
    context: &Context<T>,
) -> SettingHandler {
    let storage_handle = context.storage_factory_handle.clone();
    let (system_handler_tx, mut system_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(async move {
        let storage = storage_handle.lock().await.get_store::<SystemInfo>();

        let mut stored_value: SystemInfo;
        {
            let mut storage_lock = storage.lock().await;
            stored_value = storage_lock.get().await;
        }

        while let Some(command) = system_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        *notifier_lock.write() = Some(notifier);
                    }
                    State::EndListen => {
                        *notifier_lock.write() = None;
                    }
                },
                Command::HandleRequest(request, responder) =>
                {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::SetLoginOverrideMode(mode) => {
                            stored_value.login_override_mode = SystemLoginOverrideMode::from(mode);

                            let storage_clone = storage.clone();
                            let notifier_clone = notifier_lock.clone();
                            fasync::spawn(async move {
                                {
                                    let mut storage_lock = storage_clone.lock().await;
                                    storage_lock.write(&stored_value, true).await.unwrap();
                                }
                                responder.send(Ok(None)).ok();

                                if let Some(notifier) = &*notifier_clone.read() {
                                    notifier.unbounded_send(SettingType::System).unwrap();
                                }
                            });
                        }
                        SettingRequest::Get => {
                            responder
                                .send(Ok(Some(SettingResponse::System(stored_value))))
                                .unwrap();
                        }
                        _ => {
                            responder
                                .send(Err(SwitchboardError::UnimplementedRequest {
                                    setting_type: SettingType::System,
                                    request: request,
                                }))
                                .ok();
                        }
                    }
                }
            }
        }
    });
    system_handler_tx
}

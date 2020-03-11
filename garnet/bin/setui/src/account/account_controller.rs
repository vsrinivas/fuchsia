// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::registry::base::{Command, Context, SettingHandler},
    crate::registry::device_storage::DeviceStorageFactory,
    crate::service_context::ServiceContextHandle,
    crate::switchboard::base::*,
    fuchsia_async as fasync,
    futures::StreamExt,
};

const FACTORY_RESET_FLAG: &str = "FactoryReset";

async fn schedule_clear_accounts(
    service_context_handle: ServiceContextHandle,
) -> Result<(), SwitchboardError> {
    let connect_result = service_context_handle
        .lock()
        .await
        .connect::<fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker>()
        .await;

    if connect_result.is_err() {
        return Err(SwitchboardError::ExternalFailure {
            setting_type: SettingType::Account,
            dependency: "device_settings_manager".to_string(),
            request: "connect".to_string(),
        });
    }

    let device_settings_manager = connect_result.unwrap();

    if let Err(_) = device_settings_manager.set_integer(FACTORY_RESET_FLAG, 1).await {
        return Err(SwitchboardError::ExternalFailure {
            setting_type: SettingType::Account,
            dependency: "device_settings_manager".to_string(),
            request: "set factory reset integer".to_string(),
        });
    }

    return Ok(());
}

pub fn spawn_account_controller<T: DeviceStorageFactory + Send + Sync + 'static>(
    context: &Context<T>,
) -> SettingHandler {
    let service_context_handle = context.service_context_handle.clone();

    let (account_handler_tx, mut account_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    fasync::spawn(async move {
        while let Some(command) = account_handler_rx.next().await {
            match command {
                #[allow(unreachable_patterns)]
                Command::HandleRequest(request, responder) =>
                {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::ScheduleClearAccounts => {
                            match schedule_clear_accounts(service_context_handle.clone()).await {
                                Ok(()) => {
                                    responder.send(Ok(None)).ok();
                                }
                                Err(error) => {
                                    responder.send(Err(error)).ok();
                                }
                            }
                        }
                        _ => {
                            responder
                                .send(Err(SwitchboardError::UnimplementedRequest {
                                    setting_type: SettingType::Account,
                                    request: request,
                                }))
                                .ok();
                        }
                    }
                }
                // Ignore unsupported commands
                _ => {}
            }
        }
    });
    account_handler_tx
}

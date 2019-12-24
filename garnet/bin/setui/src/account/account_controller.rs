// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::registry::base::Command,
    crate::service_context::ServiceContext,
    crate::switchboard::base::*,
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    futures::StreamExt,
    parking_lot::RwLock,
    std::sync::Arc,
};

const FACTORY_RESET_FLAG: &str = "FactoryReset";

async fn schedule_clear_accounts(
    service_context_handle: Arc<RwLock<ServiceContext>>,
) -> Result<(), Error> {
    let device_settings_manager = service_context_handle
        .read()
        .connect::<fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker>(
    )?;

    if device_settings_manager.set_integer(FACTORY_RESET_FLAG, 1).await.is_ok() {
        return Ok(());
    } else {
        return Err(format_err!("could not set value in device settings"));
    }
}

pub fn spawn_account_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
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
                        _ => panic!("Unexpected request to account"),
                    }
                }
                // Ignore unsupported commands
                _ => {}
            }
        }
    });
    account_handler_tx
}

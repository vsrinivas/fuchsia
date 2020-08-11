// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::call_async;
use crate::handler::base::SettingHandlerResult;
use crate::handler::setting_handler::{controller, ClientProxy, ControllerError};
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{SettingRequest, SettingType};
use async_trait::async_trait;

const FACTORY_RESET_FLAG: &str = "FactoryReset";

pub struct AccountController {
    service_context: ServiceContextHandle,
}

#[async_trait]
impl controller::Create for AccountController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let service_context = client.get_service_context().await;
        Ok(Self { service_context: service_context })
    }
}

#[async_trait]
impl controller::Handle for AccountController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::ScheduleClearAccounts => {
                Some(match schedule_clear_accounts(&self.service_context).await {
                    Ok(()) => Ok(None),
                    Err(error) => Err(error),
                })
            }
            _ => None,
        }
    }
}

async fn schedule_clear_accounts(
    service_context_handle: &ServiceContextHandle,
) -> Result<(), ControllerError> {
    let connect_result = service_context_handle
        .lock()
        .await
        .connect::<fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker>()
        .await;

    if connect_result.is_err() {
        return Err(ControllerError::ExternalFailure(
            SettingType::Account,
            "device_settings_manager".into(),
            "connect".into(),
        ));
    }

    let device_settings_manager = connect_result.unwrap();

    if let Err(_) = call_async!(device_settings_manager => set_integer(FACTORY_RESET_FLAG, 1)).await
    {
        return Err(ControllerError::ExternalFailure(
            SettingType::Account,
            "device_settings_manager".into(),
            "set factory reset integer".into(),
        ));
    }

    return Ok(());
}

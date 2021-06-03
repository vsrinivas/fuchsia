// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::call_async;
use crate::handler::base::Request;
use crate::handler::device_storage::DeviceStorageAccess;
use crate::handler::setting_handler::{
    controller, ClientImpl, ControllerError, SettingHandlerResult,
};
use crate::service_context::ServiceContext;
use async_trait::async_trait;
use std::sync::Arc;

const FACTORY_RESET_FLAG: &str = "FactoryReset";

pub struct AccountController {
    service_context: Arc<ServiceContext>,
}

impl DeviceStorageAccess for AccountController {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

#[async_trait]
impl controller::Create for AccountController {
    async fn create(client: Arc<ClientImpl>) -> Result<Self, ControllerError> {
        let service_context = client.get_service_context();
        Ok(Self { service_context })
    }
}

#[async_trait]
impl controller::Handle for AccountController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        #[allow(unreachable_patterns)]
        match request {
            Request::ScheduleClearAccounts => {
                Some(match schedule_clear_accounts(&self.service_context).await {
                    Ok(()) => Ok(None),
                    Err(error) => Err(error),
                })
            }
            _ => None,
        }
    }
}

async fn schedule_clear_accounts(service_context: &ServiceContext) -> Result<(), ControllerError> {
    let connect_result =
        service_context.connect::<fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker>().await;

    let device_settings_manager = connect_result.map_err(|_| {
        ControllerError::ExternalFailure(
            SettingType::Account,
            "device_settings_manager".into(),
            "connect".into(),
        )
    })?;

    call_async!(device_settings_manager => set_integer(FACTORY_RESET_FLAG, 1))
        .await
        .map(|_| ())
        .map_err(|_| {
            ControllerError::ExternalFailure(
                SettingType::Account,
                "device_settings_manager".into(),
                "set factory reset integer".into(),
            )
        })
}

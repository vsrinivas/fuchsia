// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::base::SettingType,
    crate::call_async,
    crate::handler::base::Request,
    crate::handler::device_storage::DeviceStorageAccess,
    crate::handler::setting_handler::{
        controller, ClientImpl, ControllerError, SettingHandlerResult,
    },
    crate::service_context::ServiceContext,
    async_trait::async_trait,
    fidl_fuchsia_hardware_power_statecontrol::RebootReason,
    fuchsia_syslog::fx_log_err,
    std::sync::Arc,
};

async fn reboot(service_context_handle: &ServiceContext) -> Result<(), ControllerError> {
    let hardware_power_statecontrol_admin = service_context_handle
        .connect::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()
        .await
        .map_err(|_| {
            ControllerError::ExternalFailure(
                SettingType::Power,
                "hardware_power_statecontrol_manager".into(),
                "connect".into(),
            )
        })?;

    let build_err = || {
        ControllerError::ExternalFailure(
            SettingType::Power,
            "hardware_power_statecontrol_manager".into(),
            "reboot".into(),
        )
    };

    call_async!(hardware_power_statecontrol_admin => reboot(RebootReason::UserRequest))
        .await
        .map_err(|_| build_err())
        .and_then(|r| {
            r.map_err(|zx_status| {
                fx_log_err!("Failed to reboot device: {}", zx_status);
                build_err()
            })
        })
}

pub struct PowerController {
    service_context: Arc<ServiceContext>,
}

impl DeviceStorageAccess for PowerController {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

#[async_trait]
impl controller::Create for PowerController {
    async fn create(client: Arc<ClientImpl>) -> Result<Self, ControllerError> {
        let service_context = client.get_service_context();
        Ok(Self { service_context })
    }
}

#[async_trait]
impl controller::Handle for PowerController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::Reboot => Some(reboot(&self.service_context).await.map(|_| None)),
            _ => None,
        }
    }
}

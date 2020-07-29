// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::call_async,
    crate::registry::setting_handler::{controller, ClientProxy, ControllerError},
    crate::service_context::ServiceContextHandle,
    crate::switchboard::base::{
        SettingRequest, SettingResponseResult, SettingType, SwitchboardError,
    },
    async_trait::async_trait,
    fidl_fuchsia_hardware_power_statecontrol::RebootReason,
    fuchsia_syslog::fx_log_err,
};

async fn reboot(service_context_handle: &ServiceContextHandle) -> Result<(), SwitchboardError> {
    let hardware_power_statecontrol_admin = service_context_handle
        .lock()
        .await
        .connect::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()
        .await
        .or_else(|_| {
            Err(SwitchboardError::ExternalFailure {
                setting_type: SettingType::Power,
                dependency: "hardware_power_statecontrol_manager".to_owned(),
                request: "connect".to_owned(),
            })
        })?;

    let build_err = || SwitchboardError::ExternalFailure {
        setting_type: SettingType::Power,
        dependency: "hardware_power_statecontrol_manager".to_owned(),
        request: "reboot".to_owned(),
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
    service_context: ServiceContextHandle,
}

#[async_trait]
impl controller::Create for PowerController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let service_context = client.get_service_context().await;
        Ok(Self { service_context })
    }
}

#[async_trait]
impl controller::Handle for PowerController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        match request {
            SettingRequest::Reboot => Some(reboot(&self.service_context).await.map(|_| None)),
            _ => None,
        }
    }
}

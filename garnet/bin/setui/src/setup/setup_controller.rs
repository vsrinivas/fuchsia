// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::call_async;
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::service_context::ServiceContext;
use crate::setup::types::{ConfigurationInterfaceFlags, SetupInfo};
use async_trait::async_trait;
use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
use fuchsia_syslog::fx_log_err;

// TODO(fxb/79710): Separate the logic of communicating with external dependencies from internal
// logic for settings.
async fn reboot(service_context_handle: &ServiceContext) -> Result<(), ControllerError> {
    let hardware_power_statecontrol_admin = service_context_handle
        .connect::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()
        .await
        .map_err(|_| {
            ControllerError::ExternalFailure(
                SettingType::Setup,
                "hardware_power_statecontrol_manager".into(),
                "connect".into(),
            )
        })?;

    let reboot_err = || {
        ControllerError::ExternalFailure(
            SettingType::Setup,
            "hardware_power_statecontrol_manager".into(),
            "reboot".into(),
        )
    };

    call_async!(hardware_power_statecontrol_admin => reboot(RebootReason::UserRequest))
        .await
        .map_err(|_| reboot_err())
        .and_then(|r| {
            r.map_err(|zx_status| {
                fx_log_err!("Failed to reboot device: {}", zx_status);
                reboot_err()
            })
        })
}

impl DeviceStorageCompatible for SetupInfo {
    const KEY: &'static str = "setup_info";

    fn default_value() -> Self {
        SetupInfo { configuration_interfaces: ConfigurationInterfaceFlags::DEFAULT }
    }
}

impl From<SetupInfo> for SettingInfo {
    fn from(info: SetupInfo) -> SettingInfo {
        SettingInfo::Setup(info)
    }
}

pub struct SetupController {
    client: ClientProxy,
}

impl DeviceStorageAccess for SetupController {
    const STORAGE_KEYS: &'static [&'static str] = &[SetupInfo::KEY];
}

#[async_trait]
impl data_controller::Create for SetupController {
    /// Creates the controller
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        Ok(Self { client })
    }
}

#[async_trait]
impl controller::Handle for SetupController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::SetConfigurationInterfaces(params) => {
                let nonce = fuchsia_trace::generate_nonce();
                let mut info = self.client.read_setting::<SetupInfo>(nonce).await;
                info.configuration_interfaces = params.config_interfaces_flags;

                let write_setting_result =
                    self.client.write_setting(info.into(), true, nonce).await.into_handler_result();

                // If the write succeeded, reboot if necessary.
                if write_setting_result.is_ok() && params.should_reboot {
                    let reboot_result =
                        reboot(&self.client.get_service_context()).await.map(|_| None);
                    if reboot_result.is_err() {
                        // This will result in fidl_fuchsia_settings::Error::Failed in the caller.
                        return Some(reboot_result);
                    }
                }
                Some(write_setting_result)
            }
            Request::Get => Some(
                self.client
                    .read_setting_info::<SetupInfo>(fuchsia_trace::generate_nonce())
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

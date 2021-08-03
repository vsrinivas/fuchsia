// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::night_mode::types::NightModeInfo;
use async_trait::async_trait;

impl DeviceStorageCompatible for NightModeInfo {
    const KEY: &'static str = "night_mode_info";

    fn default_value() -> Self {
        NightModeInfo { night_mode_enabled: None }
    }
}

impl From<NightModeInfo> for SettingInfo {
    fn from(info: NightModeInfo) -> SettingInfo {
        SettingInfo::NightMode(info)
    }
}

pub struct NightModeController {
    client: ClientProxy,
}

impl DeviceStorageAccess for NightModeController {
    const STORAGE_KEYS: &'static [&'static str] = &[NightModeInfo::KEY];
}

#[async_trait]
impl data_controller::Create for NightModeController {
    /// Creates the controller
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        Ok(NightModeController { client })
    }
}

#[async_trait]
impl controller::Handle for NightModeController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::SetNightModeInfo(night_mode_info) => {
                let nonce = fuchsia_trace::generate_nonce();
                let mut current = self.client.read_setting::<NightModeInfo>(nonce).await;

                // Save the value locally.
                current.night_mode_enabled = night_mode_info.night_mode_enabled;
                Some(
                    self.client
                        .write_setting(current.into(), false, nonce)
                        .await
                        .into_handler_result(),
                )
            }
            Request::Get => Some(
                self.client
                    .read_setting_info::<NightModeInfo>(fuchsia_trace::generate_nonce())
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

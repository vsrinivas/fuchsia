// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::device_storage::DeviceStorageCompatible;
use crate::registry::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::registry::setting_handler::{controller, ControllerError};
use crate::switchboard::base::{
    SettingRequest, SettingResponse, SettingResponseResult, SystemInfo, SystemLoginOverrideMode,
};
use async_trait::async_trait;

impl DeviceStorageCompatible for SystemInfo {
    const KEY: &'static str = "system_info";

    fn default_value() -> Self {
        SystemInfo { login_override_mode: SystemLoginOverrideMode::None }
    }
}

pub struct SystemController {
    client: ClientProxy<SystemInfo>,
}

#[async_trait]
impl data_controller::Create<SystemInfo> for SystemController {
    /// Creates the controller
    async fn create(client: ClientProxy<SystemInfo>) -> Result<Self, ControllerError> {
        Ok(Self { client: client })
    }
}

#[async_trait]
impl controller::Handle for SystemController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::SetLoginOverrideMode(mode) => {
                let mut value = self.client.read().await;
                value.login_override_mode = SystemLoginOverrideMode::from(mode);

                Some(write(&self.client, value, false).await.into_response_result())
            }
            SettingRequest::Get => {
                Some(Ok(Some(SettingResponse::System(self.client.read().await))))
            }
            _ => None,
        }
    }
}

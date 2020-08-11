// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::SettingHandlerResult;
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::switchboard::base::{
    ConfigurationInterfaceFlags, SettingRequest, SettingResponse, SetupInfo,
};
use async_trait::async_trait;

impl DeviceStorageCompatible for SetupInfo {
    const KEY: &'static str = "setup_info";

    fn default_value() -> Self {
        SetupInfo { configuration_interfaces: ConfigurationInterfaceFlags::DEFAULT }
    }
}

pub struct SetupController {
    client: ClientProxy<SetupInfo>,
}

#[async_trait]
impl data_controller::Create<SetupInfo> for SetupController {
    /// Creates the controller
    async fn create(client: ClientProxy<SetupInfo>) -> Result<Self, ControllerError> {
        Ok(Self { client: client })
    }
}

#[async_trait]
impl controller::Handle for SetupController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::SetConfigurationInterfaces(interfaces) => {
                let mut info = self.client.read().await;
                info.configuration_interfaces = interfaces;

                return Some(write(&self.client, info, true).await.into_handler_result());
            }
            SettingRequest::Get => {
                return Some(Ok(Some(SettingResponse::Setup(self.client.read().await))));
            }
            _ => None,
        }
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::setup::types::{ConfigurationInterfaceFlags, SetupInfo};
use async_trait::async_trait;

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
            Request::SetConfigurationInterfaces(interfaces) => {
                let mut info = self.client.read_setting::<SetupInfo>().await;
                info.configuration_interfaces = interfaces;

                return Some(
                    self.client.write_setting(info.into(), true).await.into_handler_result(),
                );
            }
            Request::Get => {
                return Some(
                    self.client.read_setting_info::<SetupInfo>().await.into_handler_result(),
                );
            }
            _ => None,
        }
    }
}

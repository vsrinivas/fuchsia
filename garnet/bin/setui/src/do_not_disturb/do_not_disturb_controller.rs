// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use async_trait::async_trait;

impl DeviceStorageCompatible for DoNotDisturbInfo {
    const KEY: &'static str = "do_not_disturb_info";

    fn default_value() -> Self {
        DoNotDisturbInfo::new(false, false)
    }
}

impl From<DoNotDisturbInfo> for SettingInfo {
    fn from(info: DoNotDisturbInfo) -> SettingInfo {
        SettingInfo::DoNotDisturb(info)
    }
}

pub struct DoNotDisturbController {
    client: ClientProxy,
}

impl DeviceStorageAccess for DoNotDisturbController {
    const STORAGE_KEYS: &'static [&'static str] = &[DoNotDisturbInfo::KEY];
}

#[async_trait]
impl data_controller::Create for DoNotDisturbController {
    /// Creates the controller
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        Ok(DoNotDisturbController { client })
    }
}

#[async_trait]
impl controller::Handle for DoNotDisturbController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::SetDnD(dnd_info) => {
                let nonce = fuchsia_trace::generate_nonce();
                let mut stored_value = self.client.read_setting::<DoNotDisturbInfo>(nonce).await;
                if dnd_info.user_dnd.is_some() {
                    stored_value.user_dnd = dnd_info.user_dnd;
                }
                if dnd_info.night_mode_dnd.is_some() {
                    stored_value.night_mode_dnd = dnd_info.night_mode_dnd;
                }
                Some(
                    self.client
                        .write_setting(stored_value.into(), false, nonce)
                        .await
                        .into_handler_result(),
                )
            }
            Request::Get => Some(
                self.client
                    .read_setting_info::<DoNotDisturbInfo>(fuchsia_trace::generate_nonce())
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

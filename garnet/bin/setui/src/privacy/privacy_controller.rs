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
use crate::privacy::types::PrivacyInfo;
use async_trait::async_trait;

impl DeviceStorageCompatible for PrivacyInfo {
    const KEY: &'static str = "privacy_info";

    fn default_value() -> Self {
        PrivacyInfo { user_data_sharing_consent: None }
    }
}

impl From<PrivacyInfo> for SettingInfo {
    fn from(info: PrivacyInfo) -> SettingInfo {
        SettingInfo::Privacy(info)
    }
}

pub struct PrivacyController {
    client: ClientProxy,
}

impl DeviceStorageAccess for PrivacyController {
    const STORAGE_KEYS: &'static [&'static str] = &[PrivacyInfo::KEY];
}

#[async_trait]
impl data_controller::Create for PrivacyController {
    /// Creates the controller
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        Ok(PrivacyController { client })
    }
}

#[async_trait]
impl controller::Handle for PrivacyController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::SetUserDataSharingConsent(user_data_sharing_consent) => {
                let nonce = fuchsia_trace::generate_nonce();
                let mut current = self.client.read_setting::<PrivacyInfo>(nonce).await;

                // Save the value locally.
                current.user_data_sharing_consent = user_data_sharing_consent;
                Some(
                    self.client
                        .write_setting(current.into(), false, nonce)
                        .await
                        .into_handler_result(),
                )
            }
            Request::Get => Some(
                self.client
                    .read_setting_info::<PrivacyInfo>(fuchsia_trace::generate_nonce())
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::handler::base::Request;
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError, SettingHandlerResult};
use crate::privacy::types::PrivacyInfo;
use async_trait::async_trait;

impl DeviceStorageCompatible for PrivacyInfo {
    const KEY: &'static str = "privacy_info";

    fn default_value() -> Self {
        PrivacyInfo { user_data_sharing_consent: None }
    }
}

impl Into<SettingInfo> for PrivacyInfo {
    fn into(self) -> SettingInfo {
        SettingInfo::Privacy(self)
    }
}

pub struct PrivacyController {
    client: ClientProxy,
}

#[async_trait]
impl data_controller::Create<PrivacyInfo> for PrivacyController {
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
                let mut current = self.client.read::<PrivacyInfo>().await;

                // Save the value locally.
                current.user_data_sharing_consent = user_data_sharing_consent;
                Some(write(&self.client, current, false).await.into_handler_result())
            }
            Request::Get => Some(Ok(Some(SettingInfo::Privacy(self.client.read().await)))),
            _ => None,
        }
    }
}

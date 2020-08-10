// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::base::SettingHandlerResult;
use crate::registry::device_storage::DeviceStorageCompatible;
use crate::registry::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::registry::setting_handler::{controller, ControllerError};
use crate::switchboard::base::{PrivacyInfo, SettingRequest, SettingResponse};
use async_trait::async_trait;

impl DeviceStorageCompatible for PrivacyInfo {
    const KEY: &'static str = "privacy_info";

    fn default_value() -> Self {
        PrivacyInfo { user_data_sharing_consent: None }
    }
}

pub struct PrivacyController {
    client: ClientProxy<PrivacyInfo>,
}

#[async_trait]
impl data_controller::Create<PrivacyInfo> for PrivacyController {
    /// Creates the controller
    async fn create(client: ClientProxy<PrivacyInfo>) -> Result<Self, ControllerError> {
        Ok(PrivacyController { client: client })
    }
}

#[async_trait]
impl controller::Handle for PrivacyController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::SetUserDataSharingConsent(user_data_sharing_consent) => {
                let mut current = self.client.read().await;

                // Save the value locally.
                current.user_data_sharing_consent = user_data_sharing_consent;
                Some(write(&self.client, current, false).await.into_handler_result())
            }
            SettingRequest::Get => {
                Some(Ok(Some(SettingResponse::Privacy(self.client.read().await))))
            }
            _ => None,
        }
    }
}

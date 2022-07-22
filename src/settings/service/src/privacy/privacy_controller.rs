// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::handler::base::Request;
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::privacy::types::PrivacyInfo;
use async_trait::async_trait;
use settings_storage::device_storage::{DeviceStorage, DeviceStorageCompatible};
use settings_storage::storage_factory::StorageAccess;

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

impl StorageAccess for PrivacyController {
    type Storage = DeviceStorage;
    const STORAGE_KEYS: &'static [&'static str] = &[PrivacyInfo::KEY];
}

#[async_trait]
impl data_controller::Create for PrivacyController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        Ok(PrivacyController { client })
    }
}

#[async_trait]
impl controller::Handle for PrivacyController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::SetUserDataSharingConsent(user_data_sharing_consent) => {
                let id = fuchsia_trace::Id::new();
                let mut current = self.client.read_setting::<PrivacyInfo>(id).await;

                // Save the value locally.
                current.user_data_sharing_consent = user_data_sharing_consent;
                Some(self.client.write_setting(current.into(), id).await.into_handler_result())
            }
            Request::Get => Some(
                self.client
                    .read_setting_info::<PrivacyInfo>(fuchsia_trace::Id::new())
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::handler::base::SettingHandlerResult;
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::switchboard::accessibility_types::AccessibilityInfo;
use crate::switchboard::base::{Merge, SettingRequest, SettingResponse};

use async_trait::async_trait;

impl DeviceStorageCompatible for AccessibilityInfo {
    const KEY: &'static str = "accessibility_info";

    fn default_value() -> Self {
        AccessibilityInfo {
            audio_description: None,
            screen_reader: None,
            color_inversion: None,
            enable_magnification: None,
            color_correction: None,
            captions_settings: None,
        }
    }
}

pub struct AccessibilityController {
    client: ClientProxy<AccessibilityInfo>,
}

#[async_trait]
impl data_controller::Create<AccessibilityInfo> for AccessibilityController {
    /// Creates the controller.
    async fn create(client: ClientProxy<AccessibilityInfo>) -> Result<Self, ControllerError> {
        Ok(AccessibilityController { client })
    }
}

#[async_trait]
impl controller::Handle for AccessibilityController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::Get => {
                Some(Ok(Some(SettingResponse::Accessibility(self.client.read().await))))
            }
            SettingRequest::SetAccessibilityInfo(info) => Some(
                write(&self.client, info.merge(self.client.read().await), false)
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

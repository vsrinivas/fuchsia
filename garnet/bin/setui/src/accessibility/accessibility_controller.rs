// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::accessibility::types::AccessibilityInfo;
use crate::base::{Merge, SettingInfo};
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, write, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};

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

impl Into<SettingInfo> for AccessibilityInfo {
    fn into(self) -> SettingInfo {
        SettingInfo::Accessibility(self)
    }
}

pub struct AccessibilityController {
    client: ClientProxy,
}

impl DeviceStorageAccess for AccessibilityController {
    const STORAGE_KEYS: &'static [&'static str] = &[AccessibilityInfo::KEY];
}

#[async_trait]
impl data_controller::Create for AccessibilityController {
    /// Creates the controller.
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        Ok(AccessibilityController { client })
    }
}

#[async_trait]
impl controller::Handle for AccessibilityController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::Get => Some(Ok(Some(SettingInfo::Accessibility(self.client.read().await)))),
            Request::SetAccessibilityInfo(info) => Some(
                write(&self.client, info.merge(self.client.read().await), false)
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

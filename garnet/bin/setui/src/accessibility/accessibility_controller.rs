// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::accessibility::types::AccessibilityInfo;
use crate::base::{Merge, SettingInfo, SettingType};
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
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

impl From<AccessibilityInfo> for SettingInfo {
    fn from(info: AccessibilityInfo) -> Self {
        SettingInfo::Accessibility(info)
    }
}

pub(crate) struct AccessibilityController {
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
            Request::Get => Some(
                self.client
                    .read_setting_info::<AccessibilityInfo>(fuchsia_trace::generate_nonce())
                    .await
                    .into_handler_result(),
            ),
            Request::SetAccessibilityInfo(info) => {
                let nonce = fuchsia_trace::generate_nonce();
                let original_info = self.client.read_setting::<AccessibilityInfo>(nonce).await;
                assert!(original_info.is_finite());
                // Validate accessibility info contains valid float numbers.
                if !info.is_finite() {
                    return Some(Err(ControllerError::InvalidArgument(
                        SettingType::Accessibility,
                        "accessibility".into(),
                        format!("{:?}", info).into(),
                    )));
                }
                let result =
                    self.client.write_setting(original_info.merge(info).into(), false, nonce).await;
                Some(result.into_handler_result())
            }
            _ => None,
        }
    }
}

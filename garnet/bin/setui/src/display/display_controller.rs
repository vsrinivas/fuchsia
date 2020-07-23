// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::device_storage::DeviceStorageCompatible;
use crate::registry::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::registry::setting_handler::{controller, ControllerError};
use crate::switchboard::base::{
    DisplayInfo, LowLightMode, SettingRequest, SettingResponse, SettingResponseResult,
};
use async_trait::async_trait;

impl DeviceStorageCompatible for DisplayInfo {
    const KEY: &'static str = "display_info";

    fn default_value() -> Self {
        DisplayInfo::new(
            false,                 /*auto_brightness_enabled*/
            0.5,                   /*brightness_value*/
            LowLightMode::Disable, /*low_light_mode*/
        )
    }
}

pub struct DisplayController {
    client: ClientProxy<DisplayInfo>,
}

#[async_trait]
impl data_controller::Create<DisplayInfo> for DisplayController {
    /// Creates the controller
    async fn create(client: ClientProxy<DisplayInfo>) -> Result<Self, ControllerError> {
        Ok(Self { client })
    }
}

#[async_trait]
impl controller::Handle for DisplayController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::Restore => {
                // Load and set value.
                Some(store_brightness(self.client.read().await, &self.client).await)
            }
            SettingRequest::SetBrightness(brightness_value) => {
                let mut display_info = self.client.read().await.clone();
                display_info.auto_brightness = false;
                display_info.manual_brightness_value = brightness_value;
                Some(store_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetAutoBrightness(auto_brightness_enabled) => {
                let mut display_info = self.client.read().await.clone();
                display_info.auto_brightness = auto_brightness_enabled;
                Some(store_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetLowLightMode(low_light_mode) => {
                let mut display_info = self.client.read().await.clone();
                display_info.low_light_mode = low_light_mode;
                Some(store_brightness(display_info, &self.client).await)
            }
            SettingRequest::Get => {
                Some(Ok(Some(SettingResponse::Brightness(self.client.read().await))))
            }
            _ => None,
        }
    }
}

// This does not send the brightness value on anywhere, it simply stores it.
// Ambient EQ will pick up the value and set it on the brightness manager.
async fn store_brightness(
    info: DisplayInfo,
    client: &ClientProxy<DisplayInfo>,
) -> SettingResponseResult {
    write(&client, info, false).await.into_response_result()
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::base::State;
use crate::registry::device_storage::DeviceStorageCompatible;
use crate::registry::setting_handler::persist::{
    controller as data_controller, write, ClientProxy,
};
use crate::registry::setting_handler::{controller, ControllerError};
use crate::switchboard::base::{
    DisplayInfo, SettingRequest, SettingResponse, SettingResponseResult, SettingType,
    SwitchboardError,
};
use async_trait::async_trait;

impl DeviceStorageCompatible for DisplayInfo {
    const KEY: &'static str = "display_info";

    fn default_value() -> Self {
        DisplayInfo::new(false /*auto_brightness_enabled*/, 0.5 /*brightness_value*/)
    }
}

pub struct DisplayController {
    client: ClientProxy<DisplayInfo>,
    brightness_service: fidl_fuchsia_ui_brightness::ControlProxy,
}

#[async_trait]
impl data_controller::Create<DisplayInfo> for DisplayController {
    /// Creates the controller
    async fn create(client: ClientProxy<DisplayInfo>) -> Result<Self, ControllerError> {
        if let Ok(brightness_service) = client
            .get_service_context()
            .await
            .lock()
            .await
            .connect::<fidl_fuchsia_ui_brightness::ControlMarker>()
            .await
        {
            return Ok(Self { client: client, brightness_service: brightness_service });
        }

        Err(ControllerError::InitFailure {
            description: "could not connect to brightness service".to_string(),
        })
    }
}

#[async_trait]
impl controller::Handle for DisplayController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::Restore => {
                // Load and set value
                // TODO(fxb/35004): Listen to changes using hanging
                // get as well
                Some(
                    set_brightness(
                        self.client.read().await,
                        &self.brightness_service,
                        &self.client,
                    )
                    .await,
                )
            }
            SettingRequest::SetBrightness(brightness_value) => {
                Some(
                    set_brightness(
                        DisplayInfo::new(false /*auto_brightness_enabled*/, brightness_value),
                        &self.brightness_service,
                        &self.client,
                    )
                    .await,
                )
            }
            SettingRequest::SetAutoBrightness(auto_brightness_enabled) => {
                let brightness_value: f32;
                {
                    let stored_value = self.client.read().await;
                    brightness_value = stored_value.manual_brightness_value;
                }
                Some(
                    set_brightness(
                        DisplayInfo::new(auto_brightness_enabled, brightness_value),
                        &self.brightness_service,
                        &self.client,
                    )
                    .await,
                )
            }
            SettingRequest::Get => {
                Some(Ok(Some(SettingResponse::Brightness(self.client.read().await))))
            }
            _ => None,
        }
    }

    async fn change_state(&mut self, _state: State) {}
}

async fn set_brightness(
    info: DisplayInfo,
    brightness_service: &fidl_fuchsia_ui_brightness::ControlProxy,
    client: &ClientProxy<DisplayInfo>,
) -> SettingResponseResult {
    if let Err(e) = write(&client, info, false).await {
        return Err(e);
    }

    let result = if info.auto_brightness {
        brightness_service.set_auto_brightness()
    } else {
        brightness_service.set_manual_brightness(info.manual_brightness_value)
    };

    if result.is_ok() {
        Ok(None)
    } else {
        Err(SwitchboardError::ExternalFailure {
            setting_type: SettingType::Display,
            dependency: "brightness_service".to_string(),
            request: "set_brightness".to_string(),
        })
    }
}

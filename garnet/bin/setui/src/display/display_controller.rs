// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::call;
use crate::handler::base::SettingHandlerResult;
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::service_context::ExternalServiceProxy;
use crate::switchboard::base::{
    DisplayInfo, LowLightMode, SettingRequest, SettingResponse, SettingType,
};
use async_trait::async_trait;
use fidl_fuchsia_ui_brightness::{
    ControlMarker as BrightnessControlMarker, ControlProxy as BrightnessControlProxy,
};

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

#[async_trait]
pub trait BrightnessManager: Sized {
    async fn from_client(client: &ClientProxy<DisplayInfo>) -> Result<Self, ControllerError>;
    async fn update_brightness(
        &self,
        info: DisplayInfo,
        client: &ClientProxy<DisplayInfo>,
    ) -> SettingHandlerResult;
}

#[async_trait]
impl BrightnessManager for () {
    async fn from_client(_: &ClientProxy<DisplayInfo>) -> Result<Self, ControllerError> {
        Ok(())
    }

    // This does not send the brightness value on anywhere, it simply stores it.
    // External services will pick up the value and set it on the brightness manager.
    async fn update_brightness(
        &self,
        info: DisplayInfo,
        client: &ClientProxy<DisplayInfo>,
    ) -> SettingHandlerResult {
        write(&client, info, false).await.into_handler_result()
    }
}

pub struct ExternalBrightnessControl {
    brightness_service: ExternalServiceProxy<BrightnessControlProxy>,
}

#[async_trait]
impl BrightnessManager for ExternalBrightnessControl {
    async fn from_client(client: &ClientProxy<DisplayInfo>) -> Result<Self, ControllerError> {
        client
            .get_service_context()
            .await
            .lock()
            .await
            .connect::<BrightnessControlMarker>()
            .await
            .map(|brightness_service| Self { brightness_service })
            .map_err(|_| {
                ControllerError::InitFailure("could not connect to brightness service".into())
            })
    }

    async fn update_brightness(
        &self,
        info: DisplayInfo,
        client: &ClientProxy<DisplayInfo>,
    ) -> SettingHandlerResult {
        write(&client, info, false).await?;

        if info.auto_brightness {
            self.brightness_service.call(BrightnessControlProxy::set_auto_brightness)
        } else {
            call!(self.brightness_service => set_manual_brightness(info.manual_brightness_value))
        }
        .map(|_| None)
        .map_err(|_| {
            ControllerError::ExternalFailure(
                SettingType::Display,
                "brightness_service".into(),
                "set_brightness".into(),
            )
        })
    }
}

pub struct DisplayController<T = ()>
where
    T: BrightnessManager,
{
    client: ClientProxy<DisplayInfo>,
    brightness_manager: T,
}

#[async_trait]
impl<T> data_controller::Create<DisplayInfo> for DisplayController<T>
where
    T: BrightnessManager,
{
    /// Creates the controller
    async fn create(client: ClientProxy<DisplayInfo>) -> Result<Self, ControllerError> {
        let brightness_manager = <T as BrightnessManager>::from_client(&client).await?;
        Ok(Self { client, brightness_manager })
    }
}

#[async_trait]
impl<T> controller::Handle for DisplayController<T>
where
    T: BrightnessManager + Send + Sync,
{
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::Restore => {
                // Load and set value.
                Some(
                    self.brightness_manager
                        .update_brightness(self.client.read().await, &self.client)
                        .await,
                )
            }
            SettingRequest::SetBrightness(brightness_value) => {
                let mut display_info = self.client.read().await.clone();
                display_info.auto_brightness = false;
                display_info.manual_brightness_value = brightness_value;
                Some(self.brightness_manager.update_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetAutoBrightness(auto_brightness_enabled) => {
                let mut display_info = self.client.read().await.clone();
                display_info.auto_brightness = auto_brightness_enabled;
                Some(self.brightness_manager.update_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetLowLightMode(low_light_mode) => {
                let mut display_info = self.client.read().await.clone();
                display_info.low_light_mode = low_light_mode;
                Some(self.brightness_manager.update_brightness(display_info, &self.client).await)
            }
            SettingRequest::Get => {
                Some(Ok(Some(SettingResponse::Brightness(self.client.read().await))))
            }
            _ => None,
        }
    }
}

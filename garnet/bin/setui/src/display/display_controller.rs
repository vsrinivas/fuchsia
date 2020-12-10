// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::call;
use crate::handler::base::SettingHandlerResult;
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::service_context::ExternalServiceProxy;
use crate::switchboard::base::{
    DisplayInfo, LowLightMode, SettingRequest, SettingResponse, SettingType, ThemeType,
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
            true,                  /*screen_enabled*/
            LowLightMode::Disable, /*low_light_mode*/
            ThemeType::Unknown,    /*theme_type*/
        )
    }

    fn deserialize_from(value: &String) -> Self {
        Self::extract(&value)
            .unwrap_or_else(|_| Self::from(DisplayInfoV3::deserialize_from(&value)))
    }
}

impl From<DisplayInfoV3> for DisplayInfo {
    fn from(v3: DisplayInfoV3) -> Self {
        DisplayInfo {
            auto_brightness: v3.auto_brightness,
            manual_brightness_value: v3.manual_brightness_value,
            screen_enabled: v3.screen_enabled,
            low_light_mode: v3.low_light_mode,
            // In v4, the field formally known as theme_mode was renamed to
            // theme_type.
            theme_type: ThemeType::from(v3.theme_mode),
        }
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
                let mut display_info = self.client.read().await;
                display_info.auto_brightness = false;
                display_info.manual_brightness_value = brightness_value;
                display_info.screen_enabled = true;
                Some(self.brightness_manager.update_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetAutoBrightness(auto_brightness_enabled) => {
                let mut display_info = self.client.read().await;
                display_info.auto_brightness = auto_brightness_enabled;
                Some(self.brightness_manager.update_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetLowLightMode(low_light_mode) => {
                let mut display_info = self.client.read().await;
                display_info.low_light_mode = low_light_mode;
                Some(self.brightness_manager.update_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetScreenEnabled(enabled) => {
                let mut display_info = self.client.read().await;
                display_info.screen_enabled = enabled;

                // Set auto brightness to the opposite of the screen off state. If the screen is
                // turned off, auto brightness must be on so that the screen off component can
                // detect the changes. If the screen is turned on, the default behavior is to turn
                // it to full manual brightness.
                display_info.auto_brightness = !enabled;
                Some(self.brightness_manager.update_brightness(display_info, &self.client).await)
            }
            SettingRequest::SetThemeType(theme_type) => {
                let mut display_info = self.client.read().await;
                display_info.theme_type = theme_type;
                Some(write(&self.client, display_info, false).await.into_handler_result())
            }
            SettingRequest::Get => {
                Some(Ok(Some(SettingResponse::Brightness(self.client.read().await))))
            }
            _ => None,
        }
    }
}

/// The following struct should never be modified. It represents an old
/// version of the display settings.
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DisplayInfoV1 {
    /// The last brightness value that was manually set.
    pub manual_brightness_value: f32,
    pub auto_brightness: bool,
    pub low_light_mode: LowLightMode,
}

impl DisplayInfoV1 {
    pub const fn new(
        auto_brightness: bool,
        manual_brightness_value: f32,
        low_light_mode: LowLightMode,
    ) -> DisplayInfoV1 {
        DisplayInfoV1 { manual_brightness_value, auto_brightness, low_light_mode }
    }
}

impl DeviceStorageCompatible for DisplayInfoV1 {
    const KEY: &'static str = "display_infoV1";

    fn default_value() -> Self {
        DisplayInfoV1::new(
            false,                 /*auto_brightness_enabled*/
            0.5,                   /*brightness_value*/
            LowLightMode::Disable, /*low_light_mode*/
        )
    }
}

/// The following struct should never be modified.  It represents an old
/// version of the display settings.
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DisplayInfoV2 {
    pub manual_brightness_value: f32,
    pub auto_brightness: bool,
    pub low_light_mode: LowLightMode,
    pub theme_mode: ThemeModeV1,
}

impl DisplayInfoV2 {
    pub const fn new(
        auto_brightness: bool,
        manual_brightness_value: f32,
        low_light_mode: LowLightMode,
        theme_mode: ThemeModeV1,
    ) -> DisplayInfoV2 {
        DisplayInfoV2 { manual_brightness_value, auto_brightness, low_light_mode, theme_mode }
    }
}

impl DeviceStorageCompatible for DisplayInfoV2 {
    const KEY: &'static str = "display_infoV2";

    fn default_value() -> Self {
        DisplayInfoV2::new(
            false,                 /*auto_brightness_enabled*/
            0.5,                   /*brightness_value*/
            LowLightMode::Disable, /*low_light_mode*/
            ThemeModeV1::Unknown,  /*theme_mode*/
        )
    }

    fn deserialize_from(value: &String) -> Self {
        Self::extract(&value)
            .unwrap_or_else(|_| Self::from(DisplayInfoV1::deserialize_from(&value)))
    }
}

impl From<DisplayInfoV1> for DisplayInfoV2 {
    fn from(v1: DisplayInfoV1) -> Self {
        DisplayInfoV2 {
            auto_brightness: v1.auto_brightness,
            manual_brightness_value: v1.manual_brightness_value,
            low_light_mode: v1.low_light_mode,
            theme_mode: ThemeModeV1::Unknown,
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum ThemeModeV1 {
    Unknown,
    Default,
    Light,
    Dark,
    /// Product can choose a theme based on ambient cues.
    Auto,
}

impl From<ThemeModeV1> for ThemeType {
    fn from(theme_mode_v1: ThemeModeV1) -> Self {
        match theme_mode_v1 {
            ThemeModeV1::Unknown => ThemeType::Unknown,
            ThemeModeV1::Default => ThemeType::Default,
            ThemeModeV1::Light => ThemeType::Light,
            ThemeModeV1::Dark => ThemeType::Dark,
            ThemeModeV1::Auto => ThemeType::Auto,
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DisplayInfoV3 {
    /// The last brightness value that was manually set.
    pub manual_brightness_value: f32,
    pub auto_brightness: bool,
    pub screen_enabled: bool,
    pub low_light_mode: LowLightMode,
    pub theme_mode: ThemeModeV1,
}

impl DisplayInfoV3 {
    pub const fn new(
        auto_brightness: bool,
        manual_brightness_value: f32,
        screen_enabled: bool,
        low_light_mode: LowLightMode,
        theme_mode: ThemeModeV1,
    ) -> DisplayInfoV3 {
        DisplayInfoV3 {
            manual_brightness_value,
            auto_brightness,
            screen_enabled,
            low_light_mode,
            theme_mode,
        }
    }
}

impl DeviceStorageCompatible for DisplayInfoV3 {
    const KEY: &'static str = "display_info";

    fn default_value() -> Self {
        DisplayInfoV3::new(
            false,                 /*auto_brightness_enabled*/
            0.5,                   /*brightness_value*/
            true,                  /*screen_enabled*/
            LowLightMode::Disable, /*low_light_mode*/
            ThemeModeV1::Unknown,  /*theme_mode*/
        )
    }

    fn deserialize_from(value: &String) -> Self {
        Self::extract(&value)
            .unwrap_or_else(|_| Self::from(DisplayInfoV2::deserialize_from(&value)))
    }
}

impl From<DisplayInfoV2> for DisplayInfoV3 {
    fn from(v2: DisplayInfoV2) -> Self {
        DisplayInfoV3 {
            auto_brightness: v2.auto_brightness,
            manual_brightness_value: v2.manual_brightness_value,
            screen_enabled: true,
            low_light_mode: v2.low_light_mode,
            theme_mode: v2.theme_mode,
        }
    }
}

#[test]
fn test_display_migration_v1_to_v2() {
    const BRIGHTNESS_VALUE: f32 = 0.6;
    let mut v1 = DisplayInfoV1::default_value();
    v1.manual_brightness_value = BRIGHTNESS_VALUE;

    let serialized_v1 = v1.serialize_to();

    let v2 = DisplayInfoV2::deserialize_from(&serialized_v1);

    assert_eq!(v2.manual_brightness_value, BRIGHTNESS_VALUE);
    assert_eq!(v2.theme_mode, ThemeModeV1::Unknown);
}

#[test]
fn test_display_migration_v2_to_current() {
    const BRIGHTNESS_VALUE: f32 = 0.6;
    let mut v2 = DisplayInfoV2::default_value();
    v2.manual_brightness_value = BRIGHTNESS_VALUE;

    let serialized_v2 = v2.serialize_to();

    let current = DisplayInfo::deserialize_from(&serialized_v2);

    assert_eq!(current.manual_brightness_value, BRIGHTNESS_VALUE);
    assert_eq!(current.screen_enabled, true);
}

#[test]
fn test_display_migration_v1_to_current() {
    const BRIGHTNESS_VALUE: f32 = 0.6;
    let mut v1 = DisplayInfoV1::default_value();
    v1.manual_brightness_value = BRIGHTNESS_VALUE;

    let serialized_v1 = v1.serialize_to();

    let current = DisplayInfo::deserialize_from(&serialized_v1);

    assert_eq!(current.manual_brightness_value, BRIGHTNESS_VALUE);
    assert_eq!(current.theme_type, ThemeType::Unknown);
    assert_eq!(current.screen_enabled, true);
}

#[test]
fn test_display_migration_v3_to_current() {
    let mut v3 = DisplayInfoV3::default_value();
    // In v4 ThemeMode type was renamed to ThemeType, but the field in v3 is
    // still mode.
    v3.theme_mode = ThemeModeV1::Light;
    v3.screen_enabled = false;

    let serialized_v3 = v3.serialize_to();

    let current = DisplayInfo::deserialize_from(&serialized_v3);

    // In v4, the field formally known as theme_mode is theme_type.
    assert_eq!(current.theme_type, ThemeType::Light);
    assert_eq!(current.screen_enabled, false);
}

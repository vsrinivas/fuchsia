// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::base::{Merge, SettingInfo, SettingType};
use crate::call;
use crate::config::default_settings::DefaultSetting;
use crate::display::display_configuration::{
    ConfigurationThemeMode, ConfigurationThemeType, DisplayConfiguration,
};
use crate::display::types::{DisplayInfo, LowLightMode, Theme, ThemeBuilder, ThemeMode, ThemeType};
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{
    controller as data_controller, ClientProxy, UpdateState,
};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::service_context::ExternalServiceProxy;
use async_trait::async_trait;
use fidl_fuchsia_ui_brightness::{
    ControlMarker as BrightnessControlMarker, ControlProxy as BrightnessControlProxy,
};
use lazy_static::lazy_static;
use std::sync::Mutex;

pub(super) const DEFAULT_MANUAL_BRIGHTNESS_VALUE: f32 = 0.5;
pub(super) const DEFAULT_AUTO_BRIGHTNESS_VALUE: f32 = 0.5;

lazy_static! {
    /// Default display used if no configuration is available.
    pub(crate) static ref DEFAULT_DISPLAY_INFO: DisplayInfo = DisplayInfo::new(
        false,                           /*auto_brightness_enabled*/
        DEFAULT_MANUAL_BRIGHTNESS_VALUE, /*manual_brightness_value*/
        DEFAULT_AUTO_BRIGHTNESS_VALUE,   /*auto_brightness_value*/
        true,                            /*screen_enabled*/
        LowLightMode::Disable,           /*low_light_mode*/
        None,                            /*theme*/
    );
}

lazy_static! {
    /// Reference to a display configuration.
    pub(crate) static ref DISPLAY_CONFIGURATION: Mutex<DefaultSetting<DisplayConfiguration, &'static str>> =
        Mutex::new(DefaultSetting::new(
            None,
            "/config/data/display_configuration.json",
        ));
}

/// Returns a default display [`DisplayInfo`] that is derived from
/// [`DEFAULT_DISPLAY_INFO`] with any fields specified in the
/// display configuration set.
///
/// [`DEFAULT_DISPLAY_INFO`]: static@DEFAULT_DISPLAY_INFO
pub(crate) fn default_display_info() -> DisplayInfo {
    let mut default_display_info = *DEFAULT_DISPLAY_INFO;

    if let Ok(Some(display_configuration)) =
        DISPLAY_CONFIGURATION.lock().unwrap().get_cached_value()
    {
        default_display_info.theme = Some(Theme {
            theme_type: Some(match display_configuration.theme.theme_type {
                ConfigurationThemeType::Light => ThemeType::Light,
            }),
            theme_mode: if display_configuration
                .theme
                .theme_mode
                .contains(&ConfigurationThemeMode::Auto)
            {
                ThemeMode::AUTO
            } else {
                ThemeMode::empty()
            },
        });
    }

    default_display_info
}

impl DeviceStorageCompatible for DisplayInfo {
    const KEY: &'static str = "display_info";

    fn default_value() -> Self {
        default_display_info()
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(value).unwrap_or_else(|_| Self::from(DisplayInfoV5::deserialize_from(value)))
    }
}

impl From<DisplayInfo> for SettingInfo {
    fn from(info: DisplayInfo) -> SettingInfo {
        SettingInfo::Brightness(info)
    }
}

impl From<DisplayInfoV5> for DisplayInfo {
    fn from(v5: DisplayInfoV5) -> Self {
        DisplayInfo {
            auto_brightness: v5.auto_brightness,
            auto_brightness_value: DEFAULT_AUTO_BRIGHTNESS_VALUE,
            manual_brightness_value: v5.manual_brightness_value,
            screen_enabled: v5.screen_enabled,
            low_light_mode: v5.low_light_mode,
            theme: v5.theme,
        }
    }
}

#[async_trait]
pub(crate) trait BrightnessManager: Sized {
    async fn from_client(client: &ClientProxy) -> Result<Self, ControllerError>;
    async fn update_brightness(
        &self,
        info: DisplayInfo,
        client: &ClientProxy,
        // Allows overriding of the check for whether info has changed. This is necessary for
        // the initial restore call.
        always_send: bool,
    ) -> SettingHandlerResult;
}

#[async_trait]
impl BrightnessManager for () {
    async fn from_client(_: &ClientProxy) -> Result<Self, ControllerError> {
        Ok(())
    }

    // This does not send the brightness value on anywhere, it simply stores it.
    // External services will pick up the value and set it on the brightness manager.
    async fn update_brightness(
        &self,
        info: DisplayInfo,
        client: &ClientProxy,
        _: bool,
    ) -> SettingHandlerResult {
        let nonce = fuchsia_trace::generate_nonce();
        if !info.is_finite() {
            return Err(ControllerError::InvalidArgument(
                SettingType::Display,
                "display_info".into(),
                format!("{:?}", info).into(),
            ));
        }
        client.write_setting(info.into(), false, nonce).await.into_handler_result()
    }
}

pub(crate) struct ExternalBrightnessControl {
    brightness_service: ExternalServiceProxy<BrightnessControlProxy>,
}

#[async_trait]
impl BrightnessManager for ExternalBrightnessControl {
    async fn from_client(client: &ClientProxy) -> Result<Self, ControllerError> {
        client
            .get_service_context()
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
        client: &ClientProxy,
        always_send: bool,
    ) -> SettingHandlerResult {
        let nonce = fuchsia_trace::generate_nonce();
        if !info.is_finite() {
            return Err(ControllerError::InvalidArgument(
                SettingType::Display,
                "display_info".into(),
                format!("{:?}", info).into(),
            ));
        }
        let update_state = client.write_setting(info.into(), false, nonce).await?;
        if update_state == UpdateState::Unchanged && !always_send {
            return Ok(None);
        }

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

pub(crate) struct DisplayController<T = ()>
where
    T: BrightnessManager,
{
    client: ClientProxy,
    brightness_manager: T,
}

impl<T> DeviceStorageAccess for DisplayController<T>
where
    T: BrightnessManager,
{
    const STORAGE_KEYS: &'static [&'static str] = &[DisplayInfo::KEY];
}

#[async_trait]
impl<T> data_controller::Create for DisplayController<T>
where
    T: BrightnessManager,
{
    /// Creates the controller
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let brightness_manager = <T as BrightnessManager>::from_client(&client).await?;
        Ok(Self { client, brightness_manager })
    }
}

#[async_trait]
impl<T> controller::Handle for DisplayController<T>
where
    T: BrightnessManager + Send + Sync,
{
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::Restore => {
                let display_info =
                    self.client.read_setting::<DisplayInfo>(fuchsia_trace::generate_nonce()).await;
                assert!(display_info.is_finite());

                // Load and set value.
                Some(
                    self.brightness_manager
                        .update_brightness(display_info, &self.client, true)
                        .await,
                )
            }
            Request::SetDisplayInfo(mut set_display_info) => {
                let display_info =
                    self.client.read_setting::<DisplayInfo>(fuchsia_trace::generate_nonce()).await;
                assert!(display_info.is_finite());

                if let Some(theme) = set_display_info.theme {
                    set_display_info.theme = self.build_theme(theme, &display_info);
                }

                Some(
                    self.brightness_manager
                        .update_brightness(
                            display_info.merge(set_display_info),
                            &self.client,
                            false,
                        )
                        .await,
                )
            }
            Request::Get => Some(
                self.client
                    .read_setting_info::<DisplayInfo>(fuchsia_trace::generate_nonce())
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

impl<T> DisplayController<T>
where
    T: BrightnessManager,
{
    fn build_theme(&self, incoming_theme: Theme, display_info: &DisplayInfo) -> Option<Theme> {
        let existing_theme_type = display_info.theme.and_then(|theme| theme.theme_type);
        let new_theme_type = incoming_theme.theme_type.or(existing_theme_type);

        ThemeBuilder::new()
            .set_theme_type(new_theme_type)
            .set_theme_mode(incoming_theme.theme_mode)
            .build()
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
    const fn new(
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
            false,                           /*auto_brightness_enabled*/
            DEFAULT_MANUAL_BRIGHTNESS_VALUE, /*brightness_value*/
            LowLightMode::Disable,           /*low_light_mode*/
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
    const fn new(
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
            false,                           /*auto_brightness_enabled*/
            DEFAULT_MANUAL_BRIGHTNESS_VALUE, /*brightness_value*/
            LowLightMode::Disable,           /*low_light_mode*/
            ThemeModeV1::Unknown,            /*theme_mode*/
        )
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(value).unwrap_or_else(|_| Self::from(DisplayInfoV1::deserialize_from(value)))
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
            ThemeModeV1::Default => ThemeType::Default,
            ThemeModeV1::Light => ThemeType::Light,
            ThemeModeV1::Dark => ThemeType::Dark,
            // ThemeType has removed Auto field, see fxb/64775
            ThemeModeV1::Unknown | ThemeModeV1::Auto => ThemeType::Unknown,
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
    const fn new(
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
            false,                           /*auto_brightness_enabled*/
            DEFAULT_MANUAL_BRIGHTNESS_VALUE, /*brightness_value*/
            true,                            /*screen_enabled*/
            LowLightMode::Disable,           /*low_light_mode*/
            ThemeModeV1::Unknown,            /*theme_mode*/
        )
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(value).unwrap_or_else(|_| Self::from(DisplayInfoV2::deserialize_from(value)))
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

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DisplayInfoV4 {
    /// The last brightness value that was manually set.
    pub manual_brightness_value: f32,
    pub auto_brightness: bool,
    pub screen_enabled: bool,
    pub low_light_mode: LowLightMode,
    pub theme_type: ThemeType,
}

impl DisplayInfoV4 {
    const fn new(
        auto_brightness: bool,
        manual_brightness_value: f32,
        screen_enabled: bool,
        low_light_mode: LowLightMode,
        theme_type: ThemeType,
    ) -> DisplayInfoV4 {
        DisplayInfoV4 {
            manual_brightness_value,
            auto_brightness,
            screen_enabled,
            low_light_mode,
            theme_type,
        }
    }
}

impl From<DisplayInfoV3> for DisplayInfoV4 {
    fn from(v3: DisplayInfoV3) -> Self {
        DisplayInfoV4 {
            auto_brightness: v3.auto_brightness,
            manual_brightness_value: v3.manual_brightness_value,
            screen_enabled: v3.screen_enabled,
            low_light_mode: v3.low_light_mode,
            // In v4, the field formerly known as theme_mode was renamed to
            // theme_type.
            theme_type: ThemeType::from(v3.theme_mode),
        }
    }
}

impl DeviceStorageCompatible for DisplayInfoV4 {
    const KEY: &'static str = "display_info";

    fn default_value() -> Self {
        DisplayInfoV4::new(
            false,                           /*auto_brightness_enabled*/
            DEFAULT_MANUAL_BRIGHTNESS_VALUE, /*brightness_value*/
            true,                            /*screen_enabled*/
            LowLightMode::Disable,           /*low_light_mode*/
            ThemeType::Unknown,              /*theme_type*/
        )
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(value).unwrap_or_else(|_| Self::from(DisplayInfoV3::deserialize_from(value)))
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DisplayInfoV5 {
    /// The last brightness value that was manually set.
    pub manual_brightness_value: f32,
    pub auto_brightness: bool,
    pub screen_enabled: bool,
    pub low_light_mode: LowLightMode,
    pub theme: Option<Theme>,
}

impl DisplayInfoV5 {
    const fn new(
        auto_brightness: bool,
        manual_brightness_value: f32,
        screen_enabled: bool,
        low_light_mode: LowLightMode,
        theme: Option<Theme>,
    ) -> DisplayInfoV5 {
        DisplayInfoV5 {
            manual_brightness_value,
            auto_brightness,
            screen_enabled,
            low_light_mode,
            theme,
        }
    }
}

impl From<DisplayInfoV4> for DisplayInfoV5 {
    fn from(v4: DisplayInfoV4) -> Self {
        DisplayInfoV5 {
            auto_brightness: v4.auto_brightness,
            manual_brightness_value: v4.manual_brightness_value,
            screen_enabled: v4.screen_enabled,
            low_light_mode: v4.low_light_mode,
            // Clients has migrated off auto theme_type, we should not get theme_type as Auto
            theme: Some(Theme::new(Some(v4.theme_type), ThemeMode::empty())),
        }
    }
}

impl DeviceStorageCompatible for DisplayInfoV5 {
    const KEY: &'static str = "display_info";

    fn default_value() -> Self {
        DisplayInfoV5::new(
            false,                                                          /*auto_brightness_enabled*/
            DEFAULT_MANUAL_BRIGHTNESS_VALUE,                                /*brightness_value*/
            true,                                                           /*screen_enabled*/
            LowLightMode::Disable,                                          /*low_light_mode*/
            Some(Theme::new(Some(ThemeType::Unknown), ThemeMode::empty())), /*theme_type*/
        )
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(value).unwrap_or_else(|_| Self::from(DisplayInfoV4::deserialize_from(value)))
    }
}

#[test]
fn test_display_migration_v1_to_v2() {
    let v1 = DisplayInfoV1 {
        manual_brightness_value: 0.6,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
    };

    let serialized_v1 = v1.serialize_to();
    let v2 = DisplayInfoV2::deserialize_from(&serialized_v1);

    assert_eq!(
        v2,
        DisplayInfoV2 {
            manual_brightness_value: v1.manual_brightness_value,
            auto_brightness: v1.auto_brightness,
            low_light_mode: v1.low_light_mode,
            theme_mode: DisplayInfoV2::default_value().theme_mode,
        }
    );
}

#[test]
fn test_display_migration_v2_to_v3() {
    let v2 = DisplayInfoV2 {
        manual_brightness_value: 0.7,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
        theme_mode: ThemeModeV1::Default,
    };

    let serialized_v2 = v2.serialize_to();
    let v3 = DisplayInfoV3::deserialize_from(&serialized_v2);

    assert_eq!(
        v3,
        DisplayInfoV3 {
            manual_brightness_value: v2.manual_brightness_value,
            auto_brightness: v2.auto_brightness,
            screen_enabled: DisplayInfoV3::default_value().screen_enabled,
            low_light_mode: v2.low_light_mode,
            theme_mode: v2.theme_mode,
        }
    );
}

#[test]
fn test_display_migration_v3_to_v4() {
    let v3 = DisplayInfoV3 {
        manual_brightness_value: 0.7,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
        theme_mode: ThemeModeV1::Light,
        screen_enabled: false,
    };

    let serialized_v3 = v3.serialize_to();
    let v4 = DisplayInfoV4::deserialize_from(&serialized_v3);

    // In v4, the field formally known as theme_mode is theme_type.
    assert_eq!(
        v4,
        DisplayInfoV4 {
            manual_brightness_value: v3.manual_brightness_value,
            auto_brightness: v3.auto_brightness,
            low_light_mode: v3.low_light_mode,
            theme_type: ThemeType::Light,
            screen_enabled: v3.screen_enabled,
        }
    );
}

#[test]
fn test_display_migration_v4_to_v5() {
    let v4 = DisplayInfoV4 {
        manual_brightness_value: 0.7,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
        theme_type: ThemeType::Dark,
        screen_enabled: false,
    };

    let serialized_v4 = v4.serialize_to();
    let v5 = DisplayInfoV5::deserialize_from(&serialized_v4);

    assert_eq!(
        v5,
        DisplayInfoV5 {
            manual_brightness_value: v4.manual_brightness_value,
            auto_brightness: v4.auto_brightness,
            low_light_mode: v4.low_light_mode,
            theme: Some(Theme::new(Some(v4.theme_type), ThemeMode::empty())),
            screen_enabled: v4.screen_enabled,
        }
    );
}

#[test]
fn test_display_migration_v1_to_current() {
    let v1 = DisplayInfoV1 {
        manual_brightness_value: 0.6,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
    };

    let serialized_v1 = v1.serialize_to();
    let current = DisplayInfo::deserialize_from(&serialized_v1);

    assert_eq!(
        current,
        DisplayInfo {
            manual_brightness_value: v1.manual_brightness_value,
            auto_brightness: v1.auto_brightness,
            low_light_mode: v1.low_light_mode,
            theme: Some(Theme::new(Some(ThemeType::Unknown), ThemeMode::empty())),
            // screen_enabled was added in v3.
            screen_enabled: DisplayInfoV3::default_value().screen_enabled,
            auto_brightness_value: DEFAULT_DISPLAY_INFO.auto_brightness_value,
        }
    );
}

#[test]
fn test_display_migration_v2_to_current() {
    let v2 = DisplayInfoV2 {
        manual_brightness_value: 0.6,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
        theme_mode: ThemeModeV1::Light,
    };

    let serialized_v2 = v2.serialize_to();
    let current = DisplayInfo::deserialize_from(&serialized_v2);

    assert_eq!(
        current,
        DisplayInfo {
            manual_brightness_value: v2.manual_brightness_value,
            auto_brightness: v2.auto_brightness,
            low_light_mode: v2.low_light_mode,
            theme: Some(Theme::new(Some(ThemeType::Light), ThemeMode::empty())),
            // screen_enabled was added in v3.
            screen_enabled: DisplayInfoV3::default_value().screen_enabled,
            auto_brightness_value: DEFAULT_DISPLAY_INFO.auto_brightness_value,
        }
    );
}

#[test]
fn test_display_migration_v3_to_current() {
    let v3 = DisplayInfoV3 {
        manual_brightness_value: 0.6,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
        theme_mode: ThemeModeV1::Light,
        screen_enabled: false,
    };

    let serialized_v3 = v3.serialize_to();
    let current = DisplayInfo::deserialize_from(&serialized_v3);

    assert_eq!(
        current,
        DisplayInfo {
            manual_brightness_value: v3.manual_brightness_value,
            auto_brightness: v3.auto_brightness,
            low_light_mode: v3.low_light_mode,
            theme: Some(Theme::new(Some(ThemeType::Light), ThemeMode::empty())),
            // screen_enabled was added in v3.
            screen_enabled: v3.screen_enabled,
            auto_brightness_value: DEFAULT_DISPLAY_INFO.auto_brightness_value,
        }
    );
}

#[test]
fn test_display_migration_v4_to_current() {
    let v4 = DisplayInfoV4 {
        manual_brightness_value: 0.6,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
        theme_type: ThemeType::Light,
        screen_enabled: false,
    };

    let serialized_v4 = v4.serialize_to();
    let current = DisplayInfo::deserialize_from(&serialized_v4);

    assert_eq!(
        current,
        DisplayInfo {
            manual_brightness_value: v4.manual_brightness_value,
            auto_brightness: v4.auto_brightness,
            low_light_mode: v4.low_light_mode,
            theme: Some(Theme::new(Some(ThemeType::Light), ThemeMode::empty())),
            screen_enabled: v4.screen_enabled,
            auto_brightness_value: DEFAULT_DISPLAY_INFO.auto_brightness_value,
        }
    );
}

#[test]
fn test_display_migration_v5_to_current() {
    let v5 = DisplayInfoV5 {
        manual_brightness_value: 0.6,
        auto_brightness: true,
        low_light_mode: LowLightMode::Enable,
        theme: Some(Theme::new(Some(ThemeType::Light), ThemeMode::AUTO)),
        screen_enabled: false,
    };

    let serialized_v5 = v5.serialize_to();
    let current = DisplayInfo::deserialize_from(&serialized_v5);

    assert_eq!(
        current,
        DisplayInfo {
            manual_brightness_value: v5.manual_brightness_value,
            auto_brightness: v5.auto_brightness,
            low_light_mode: v5.low_light_mode,
            theme: Some(Theme::new(Some(ThemeType::Light), ThemeMode::AUTO)),
            screen_enabled: v5.screen_enabled,
            auto_brightness_value: DEFAULT_DISPLAY_INFO.auto_brightness_value,
        }
    );
}

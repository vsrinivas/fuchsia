// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use failure::Error;
use fuchsia_syslog::fx_log_warn;
use futures::channel::mpsc::UnboundedSender;
use futures::channel::oneshot::Sender;
use serde_derive::{Deserialize, Serialize};
use std::collections::HashSet;

pub type SettingResponseResult = Result<Option<SettingResponse>, Error>;
pub type SettingRequestResponder = Sender<SettingResponseResult>;

/// The setting types supported by the messaging system. This is used as a key
/// for listening to change notifications and sending requests.
/// The types are arranged alphabetically.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum SettingType {
    Unknown,
    Accessibility,
    Audio,
    Device,
    Display,
    DoNotDisturb,
    Intl,
    LightSensor,
    Privacy,
    Setup,
    System,
}

/// Returns all known setting types. New additions to SettingType should also
/// be inserted here.
pub fn get_all_setting_types() -> HashSet<SettingType> {
    let mut set = HashSet::new();
    set.insert(SettingType::Accessibility);
    set.insert(SettingType::Audio);
    set.insert(SettingType::Device);
    set.insert(SettingType::Display);
    set.insert(SettingType::DoNotDisturb);
    set.insert(SettingType::Intl);
    set.insert(SettingType::LightSensor);
    set.insert(SettingType::Privacy);
    set.insert(SettingType::Setup);
    set.insert(SettingType::System);

    set
}

/// The possible requests that can be made on a setting. The sink will expect a
/// subset of the values defined below based on the associated type.
/// The types are arranged alphabetically.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingRequest {
    Get,

    // Accessibility requests.
    SetAccessibilityInfo(AccessibilityInfo),

    // Audio requests.
    SetVolume(Vec<AudioStream>),

    // Display requests.
    SetBrightness(f32),
    SetAutoBrightness(bool),

    // Do not disturb requests.
    SetUserInitiatedDoNotDisturb(bool),
    SetNightModeInitiatedDoNotDisturb(bool),

    // Intl requests.
    SetTimeZone(String),

    // Privacy requests.
    SetUserDataSharingConsent(Option<bool>),

    // Setup info requests.
    SetConfigurationInterfaces(ConfigurationInterfaceFlags),

    // System login requests.
    SetLoginOverrideMode(SystemLoginOverrideMode),
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AccessibilityInfo {
    pub audio_description: Option<bool>,
    pub screen_reader: Option<bool>,
    pub color_inversion: Option<bool>,
    pub enable_magnification: Option<bool>,
    pub color_correction: Option<ColorBlindnessType>,
    pub captions_settings: Option<CaptionsSettings>,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum ColorBlindnessType {
    /// No color blindness.
    None,

    /// Red-green color blindness due to reduced sensitivity to red light.
    Protanomaly,

    /// Red-green color blindness due to reduced sensitivity to green light.
    Deuteranomaly,

    /// Blue-yellow color blindness. It is due to reduced sensitivity to blue
    /// light.
    Tritanomaly,
}

impl From<fidl_fuchsia_settings::ColorBlindnessType> for ColorBlindnessType {
    fn from(color_blindness_type: fidl_fuchsia_settings::ColorBlindnessType) -> Self {
        match color_blindness_type {
            fidl_fuchsia_settings::ColorBlindnessType::None => ColorBlindnessType::None,
            fidl_fuchsia_settings::ColorBlindnessType::Protanomaly => {
                ColorBlindnessType::Protanomaly
            }
            fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly => {
                ColorBlindnessType::Deuteranomaly
            }
            fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly => {
                ColorBlindnessType::Tritanomaly
            }
        }
    }
}

impl From<ColorBlindnessType> for fidl_fuchsia_settings::ColorBlindnessType {
    fn from(color_blindness_type: ColorBlindnessType) -> Self {
        match color_blindness_type {
            ColorBlindnessType::None => fidl_fuchsia_settings::ColorBlindnessType::None,
            ColorBlindnessType::Protanomaly => {
                fidl_fuchsia_settings::ColorBlindnessType::Protanomaly
            }
            ColorBlindnessType::Deuteranomaly => {
                fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly
            }
            ColorBlindnessType::Tritanomaly => {
                fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly
            }
        }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct DeviceInfo {
    pub build_tag: String,
}

impl DeviceInfo {
    pub const fn new(build_tag: String) -> DeviceInfo {
        DeviceInfo { build_tag }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct CaptionsSettings {
    pub for_media: Option<bool>,
    pub for_tts: Option<bool>,
    pub font_style: Option<CaptionFontStyle>,
    pub window_color: Option<ColorRgba>,
    pub background_color: Option<ColorRgba>,
}

impl From<fidl_fuchsia_settings::CaptionsSettings> for CaptionsSettings {
    fn from(src: fidl_fuchsia_settings::CaptionsSettings) -> Self {
        CaptionsSettings {
            for_media: src.for_media,
            for_tts: src.for_tts,
            font_style: src.font_style.map(fidl_fuchsia_settings::CaptionFontStyle::into),
            window_color: src.window_color.map(fidl_fuchsia_ui_types::ColorRgba::into),
            background_color: src.background_color.map(fidl_fuchsia_ui_types::ColorRgba::into),
        }
    }
}

impl From<CaptionsSettings> for fidl_fuchsia_settings::CaptionsSettings {
    fn from(src: CaptionsSettings) -> Self {
        let mut settings = fidl_fuchsia_settings::CaptionsSettings::empty();
        settings.for_media = src.for_media;
        settings.for_tts = src.for_tts;
        settings.font_style = src.font_style.map(CaptionFontStyle::into);
        settings.window_color = src.window_color.map(ColorRgba::into);
        settings.background_color = src.background_color.map(ColorRgba::into);
        settings
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct CaptionFontStyle {
    pub family: Option<CaptionFontFamily>,
    pub color: Option<ColorRgba>,
    pub relative_size: Option<f32>,
    pub char_edge_style: Option<EdgeStyle>,
}

impl From<fidl_fuchsia_settings::CaptionFontStyle> for CaptionFontStyle {
    fn from(src: fidl_fuchsia_settings::CaptionFontStyle) -> Self {
        CaptionFontStyle {
            family: src.family.map(fidl_fuchsia_settings::CaptionFontFamily::into),
            color: src.color.map(fidl_fuchsia_ui_types::ColorRgba::into),
            relative_size: src.relative_size,
            char_edge_style: src.char_edge_style.map(fidl_fuchsia_settings::EdgeStyle::into),
        }
    }
}

impl From<CaptionFontStyle> for fidl_fuchsia_settings::CaptionFontStyle {
    fn from(src: CaptionFontStyle) -> Self {
        let mut style = fidl_fuchsia_settings::CaptionFontStyle::empty();
        style.family = src.family.map(CaptionFontFamily::into);
        style.color = src.color.map(ColorRgba::into);
        style.relative_size = src.relative_size;
        style.char_edge_style = src.char_edge_style.map(EdgeStyle::into);
        style
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct ColorRgba {
    pub red: f32,
    pub green: f32,
    pub blue: f32,
    pub alpha: f32,
}

impl From<fidl_fuchsia_ui_types::ColorRgba> for ColorRgba {
    fn from(src: fidl_fuchsia_ui_types::ColorRgba) -> Self {
        ColorRgba {
            red: src.red.into(),
            green: src.green.into(),
            blue: src.blue.into(),
            alpha: src.alpha.into(),
        }
    }
}

impl From<ColorRgba> for fidl_fuchsia_ui_types::ColorRgba {
    fn from(src: ColorRgba) -> Self {
        fidl_fuchsia_ui_types::ColorRgba {
            red: src.red.into(),
            green: src.green.into(),
            blue: src.blue.into(),
            alpha: src.alpha.into(),
        }
    }
}

/// Font family groups for closed captions, specified by 47 CFR §79.102(k).
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum CaptionFontFamily {
    Unknown,
    MonospacedSerif,
    ProportionalSerif,
    MonospacedSansSerif,
    ProportionalSansSerif,
    Casual,
    Cursive,
    SmallCapitals,
}

impl From<fidl_fuchsia_settings::CaptionFontFamily> for CaptionFontFamily {
    fn from(src: fidl_fuchsia_settings::CaptionFontFamily) -> Self {
        match src {
            fidl_fuchsia_settings::CaptionFontFamily::Unknown => CaptionFontFamily::Unknown,
            fidl_fuchsia_settings::CaptionFontFamily::MonospacedSerif => {
                CaptionFontFamily::MonospacedSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::ProportionalSerif => {
                CaptionFontFamily::ProportionalSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::MonospacedSansSerif => {
                CaptionFontFamily::MonospacedSansSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::ProportionalSansSerif => {
                CaptionFontFamily::ProportionalSansSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::Casual => CaptionFontFamily::Casual,
            fidl_fuchsia_settings::CaptionFontFamily::Cursive => CaptionFontFamily::Cursive,
            fidl_fuchsia_settings::CaptionFontFamily::SmallCapitals => {
                CaptionFontFamily::SmallCapitals
            }
        }
    }
}

impl From<CaptionFontFamily> for fidl_fuchsia_settings::CaptionFontFamily {
    fn from(src: CaptionFontFamily) -> Self {
        match src {
            CaptionFontFamily::Unknown => fidl_fuchsia_settings::CaptionFontFamily::Unknown,
            CaptionFontFamily::MonospacedSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::MonospacedSerif
            }
            CaptionFontFamily::ProportionalSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::ProportionalSerif
            }
            CaptionFontFamily::MonospacedSansSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::MonospacedSansSerif
            }
            CaptionFontFamily::ProportionalSansSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::ProportionalSansSerif
            }
            CaptionFontFamily::Casual => fidl_fuchsia_settings::CaptionFontFamily::Casual,
            CaptionFontFamily::Cursive => fidl_fuchsia_settings::CaptionFontFamily::Cursive,
            CaptionFontFamily::SmallCapitals => {
                fidl_fuchsia_settings::CaptionFontFamily::SmallCapitals
            }
        }
    }
}

/// Edge style for fonts as specified in 47 CFR §79.103(c)(7)
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum EdgeStyle {
    /// No border around fonts.
    None,

    /// A shadow "behind" and slightly offset from each edge.
    DropShadow,

    /// A bevel that mimics a 3D raised effect.
    Raised,

    /// A bevel that mimics a 3D depressed effect.
    Depressed,

    /// A plain border around each shapes.
    Outline,
}

impl From<fidl_fuchsia_settings::EdgeStyle> for EdgeStyle {
    fn from(src: fidl_fuchsia_settings::EdgeStyle) -> Self {
        match src {
            fidl_fuchsia_settings::EdgeStyle::None => EdgeStyle::None,
            fidl_fuchsia_settings::EdgeStyle::DropShadow => EdgeStyle::DropShadow,
            fidl_fuchsia_settings::EdgeStyle::Raised => EdgeStyle::Raised,
            fidl_fuchsia_settings::EdgeStyle::Depressed => EdgeStyle::Depressed,
            fidl_fuchsia_settings::EdgeStyle::Outline => EdgeStyle::Outline,
        }
    }
}

impl From<EdgeStyle> for fidl_fuchsia_settings::EdgeStyle {
    fn from(src: EdgeStyle) -> Self {
        match src {
            EdgeStyle::None => fidl_fuchsia_settings::EdgeStyle::None,
            EdgeStyle::DropShadow => fidl_fuchsia_settings::EdgeStyle::DropShadow,
            EdgeStyle::Raised => fidl_fuchsia_settings::EdgeStyle::Raised,
            EdgeStyle::Depressed => fidl_fuchsia_settings::EdgeStyle::Depressed,
            EdgeStyle::Outline => fidl_fuchsia_settings::EdgeStyle::Outline,
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum AudioSettingSource {
    Default,
    User,
    System,
}

#[derive(PartialEq, Debug, Clone, Copy, Hash, Eq)]
pub enum AudioStreamType {
    Background,
    Media,
    Interruption,
    SystemAgent,
    Communication,
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub struct AudioStream {
    pub stream_type: AudioStreamType,
    pub source: AudioSettingSource,
    pub user_volume_level: f32,
    pub user_volume_muted: bool,
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub struct AudioInputInfo {
    pub mic_mute: bool,
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub struct AudioInfo {
    pub streams: [AudioStream; 5],
    pub input: AudioInputInfo,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DisplayInfo {
    /// The last brightness value that was manually set
    pub manual_brightness_value: f32,
    pub auto_brightness: bool,
}

impl DisplayInfo {
    pub const fn new(auto_brightness: bool, manual_brightness_value: f32) -> DisplayInfo {
        DisplayInfo {
            manual_brightness_value: manual_brightness_value,
            auto_brightness: auto_brightness,
        }
    }
}

bitflags! {
    #[derive(Serialize, Deserialize)]
    pub struct ConfigurationInterfaceFlags: u32 {
        const ETHERNET = 1 << 0;
        const WIFI = 1 << 1;
        const DEFAULT = Self::ETHERNET.bits | Self::WIFI.bits;
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DoNotDisturbInfo {
    pub user_dnd: bool,
    pub night_mode_dnd: bool,
}

impl DoNotDisturbInfo {
    pub const fn new(user_dnd: bool, night_mode_dnd: bool) -> DoNotDisturbInfo {
        DoNotDisturbInfo { user_dnd: user_dnd, night_mode_dnd: night_mode_dnd }
    }
}

#[derive(PartialEq, Debug, Clone)]
pub struct IntlInfo {
    pub time_zone_id: String,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct PrivacyInfo {
    pub user_data_sharing_consent: Option<bool>,
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum SystemLoginOverrideMode {
    None,
    AutologinGuest,
    AuthProvider,
}

#[derive(PartialEq, Debug, Clone)]
pub struct SystemInfo {
    pub login_override_mode: SystemLoginOverrideMode,
}

#[derive(PartialEq, Debug, Clone, Copy, Deserialize, Serialize)]
pub struct SetupInfo {
    pub configuration_interfaces: ConfigurationInterfaceFlags,
}

#[derive(PartialEq, Debug, Clone, Copy, Deserialize, Serialize)]
pub struct LightData {
    /// Overall illuminance as measured in lux
    pub illuminance: f32,
}

/// The possible responses to a SettingRequest.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingResponse {
    Unknown,
    Accessibility(AccessibilityInfo),
    Audio(AudioInfo),
    /// Response to a request to get current brightness state.AccessibilityEncoder
    Brightness(DisplayInfo),
    Device(DeviceInfo),
    LightSensor(LightData),
    DoNotDisturb(DoNotDisturbInfo),
    Intl(IntlInfo),
    Privacy(PrivacyInfo),
    Setup(SetupInfo),
    System(SystemInfo),
}

/// Description of an action request on a setting. This wraps a
/// SettingActionData, providing destination details (setting type) along with
/// callback information (action id).
pub struct SettingAction {
    pub id: u64,
    pub setting_type: SettingType,
    pub data: SettingActionData,
}

/// The types of actions. Note that specific request types should be enumerated
/// in the SettingRequest enum.
#[derive(PartialEq, Debug)]
pub enum SettingActionData {
    /// The listening state has changed for the particular setting. The provided
    /// value indicates the number of active listeners. 0 indicates there are
    /// no more listeners.
    Listen(u64),
    /// A request has been made on a particular setting. The specific setting
    /// and request data are encoded in SettingRequest.
    Request(SettingRequest),
}

/// The events generated in response to SettingAction.
pub enum SettingEvent {
    /// The backing data for the specified setting type has changed. Interested
    /// parties can query through request to get the updated values.
    Changed(SettingType),
    /// A response to a previous SettingActionData::Request is ready. The source
    /// SettingAction's id is provided alongside the result.
    Response(u64, SettingResponseResult),
}

/// A trait handed back from Switchboard's listen interface. Allows client to
/// signal they want to end the session.
pub trait ListenSession: Drop {
    /// Invoked to close the current listening session. No further updates will
    /// be provided to the listener provided at the initial listen call.
    fn close(&mut self);
}

/// A interface for send SettingActions.
pub trait Switchboard {
    /// Transmits a SettingRequest. Results are returned from the passed in
    /// oneshot sender.
    fn request(
        &mut self,
        setting_type: SettingType,
        request: SettingRequest,
        callback: Sender<Result<Option<SettingResponse>, Error>>,
    ) -> Result<(), Error>;

    /// Establishes a continuous callback for change notifications around a
    /// SettingType.
    fn listen(
        &mut self,
        setting_type: SettingType,
        listener: UnboundedSender<SettingType>,
    ) -> Result<Box<dyn ListenSession + Send + Sync>, Error>;
}

/// Custom trait used to handle results from responding to FIDL calls.
pub trait FidlResponseErrorLogger {
    fn log_fidl_response_error(&self, client_name: &str);
}

/// In order to not crash when a client dies, logs but doesn't crash for the specific case of
/// being unable to write to the client. Crashes if other types of errors occur.
impl FidlResponseErrorLogger for Result<(), fidl::Error> {
    fn log_fidl_response_error(&self, client_name: &str) {
        if let Some(error) = self.as_ref().err() {
            match error {
                fidl::Error::ServerResponseWrite(_) => {
                    fx_log_warn!("Failed to respond to client {:?} : {:?}", client_name, error);
                }
                _ => {
                    panic!(
                        "Unexpected client response error from client {:?} : {:?}",
                        client_name, error
                    );
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon as zx;

    /// Should succeed either when responding was successful or there was an error on the client side.
    #[test]
    fn test_error_logger_succeeds() {
        let result = Err(fidl::Error::ServerResponseWrite(zx::Status::PEER_CLOSED));
        result.log_fidl_response_error("");

        let result = Ok(());
        result.log_fidl_response_error("");
    }

    /// Should fail at all other times.
    #[should_panic]
    #[test]
    fn test_error_logger_fails() {
        let result = Err(fidl::Error::ServerRequestRead(zx::Status::PEER_CLOSED));
        result.log_fidl_response_error("");
    }
}

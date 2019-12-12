// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::accessibility_types::AccessibilityInfo;
use crate::switchboard::intl_types::IntlInfo;
use bitflags::bitflags;
use failure::Error;
use fuchsia_syslog::fx_log_warn;
use futures::channel::mpsc::UnboundedSender;
use futures::channel::oneshot::Sender;
use serde_derive::{Deserialize, Serialize};
use std::collections::HashSet;

pub type SettingResponseResult = Result<Option<SettingResponse>, Error>;
pub type SettingRequestResponder = Sender<SettingResponseResult>;

/// A trait for structs where all fields are options. Recursively performs
/// [Option::or](std::option::Option::or) on each field in the struct and substructs.
pub trait Merge {
    fn merge(&self, other: Self) -> Self;
}

/// The setting types supported by the messaging system. This is used as a key
/// for listening to change notifications and sending requests.
/// The types are arranged alphabetically.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum SettingType {
    Unknown,
    Accessibility,
    Account,
    Audio,
    Device,
    Display,
    DoNotDisturb,
    Intl,
    LightSensor,
    Power,
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

    // Account requests
    ScheduleClearAccounts,

    // Audio requests.
    SetVolume(Vec<AudioStream>),

    // Display requests.
    SetBrightness(f32),
    SetAutoBrightness(bool),

    // Do not disturb requests.
    SetDnD(DoNotDisturbInfo),

    // Intl requests.
    SetIntlInfo(IntlInfo),

    // Power requests.
    Reboot,

    // Privacy requests.
    SetUserDataSharingConsent(Option<bool>),

    // Setup info requests.
    SetConfigurationInterfaces(ConfigurationInterfaceFlags),

    // System login requests.
    SetLoginOverrideMode(SystemLoginOverrideMode),
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
pub enum AudioSettingSource {
    User,
    System,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize, Hash, Eq)]
pub enum AudioStreamType {
    Background,
    Media,
    Interruption,
    SystemAgent,
    Communication,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AudioStream {
    pub stream_type: AudioStreamType,
    pub source: AudioSettingSource,
    pub user_volume_level: f32,
    pub user_volume_muted: bool,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AudioInputInfo {
    pub mic_mute: bool,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
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
        DisplayInfo { manual_brightness_value, auto_brightness }
    }
}

bitflags! {
    #[derive(Serialize, Deserialize)]
    pub struct ConfigurationInterfaceFlags: u32 {
        const ETHERNET = 1 << 0;
        const WIFI = 1 << 1;
        const DEFAULT = Self::WIFI.bits;
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DoNotDisturbInfo {
    pub user_dnd: Option<bool>,
    pub night_mode_dnd: Option<bool>,
}

impl DoNotDisturbInfo {
    pub const fn empty() -> DoNotDisturbInfo {
        DoNotDisturbInfo { user_dnd: None, night_mode_dnd: None }
    }
    pub const fn new(user_dnd: bool, night_mode_dnd: bool) -> DoNotDisturbInfo {
        DoNotDisturbInfo { user_dnd: Some(user_dnd), night_mode_dnd: Some(night_mode_dnd) }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct PrivacyInfo {
    pub user_data_sharing_consent: Option<bool>,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum SystemLoginOverrideMode {
    None,
    AutologinGuest,
    AuthProvider,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct SystemInfo {
    pub login_override_mode: SystemLoginOverrideMode,
}

#[derive(PartialEq, Debug, Clone, Copy, Deserialize, Serialize)]
pub struct SetupInfo {
    pub configuration_interfaces: ConfigurationInterfaceFlags,
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub struct LightData {
    /// Overall illuminance as measured in lux.
    pub illuminance: f32,

    /// Light sensor color reading in rgb.
    pub color: fidl_fuchsia_ui_types::ColorRgb,
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

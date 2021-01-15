// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;

use fuchsia_syslog::fx_log_warn;
use serde::{Deserialize, Serialize};
use thiserror::Error;

use crate::accessibility::types::AccessibilityInfo;
use crate::audio::types::AudioStream;
use crate::base::{SettingInfo, SettingType};
use crate::display::types::{LowLightMode, Theme};
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::handler::base::SettingHandlerResult;
use crate::handler::setting_handler::ControllerError;
use crate::input::types::InputDevice;
use crate::input::{ButtonType, VolumeGain};
use crate::night_mode::types::NightModeInfo;
use crate::switchboard::intl_types::IntlInfo;
use crate::switchboard::light_types::LightState;
use bitflags::bitflags;
use std::borrow::Cow;

/// Return type from a controller after handling a state change.
pub type ControllerStateResult = Result<(), ControllerError>;
pub type SettingResponseResult = Result<Option<SettingInfo>, SwitchboardError>;

/// A trait for structs where all fields are options. Recursively performs
/// [Option::or](std::option::Option::or) on each field in the struct and substructs.
pub trait Merge {
    fn merge(&self, other: Self) -> Self;
}

#[derive(Error, Debug, Clone, PartialEq)]
pub enum SwitchboardError {
    #[error("Unimplemented Request:{0:?} for setting type: {1:?}")]
    UnimplementedRequest(SettingType, SettingRequest),

    #[error("Storage failure for setting type: {0:?}")]
    StorageFailure(SettingType),

    #[error("Initialization failure: cause {0:?}")]
    InitFailure(Cow<'static, str>),

    #[error("Restoration of setting on controller startup failed: cause {0:?}")]
    RestoreFailure(Cow<'static, str>),

    #[error("Invalid argument for setting type: {0:?} argument:{1:?} value:{2:?}")]
    InvalidArgument(SettingType, Cow<'static, str>, Cow<'static, str>),

    #[error("External failure for setting type:{0:?} dependency: {1:?} request:{2:?}")]
    ExternalFailure(SettingType, Cow<'static, str>, Cow<'static, str>),

    #[error("Unhandled type: {0:?}")]
    UnhandledType(SettingType),

    #[error("Delivery error for type: {0:?} received by: {1:?}")]
    DeliveryError(SettingType, SettingType),

    #[error("Unexpected error: {0}")]
    UnexpectedError(Cow<'static, str>),

    #[error("Undeliverable Request:{1:?} for setting type: {0:?}")]
    UndeliverableError(SettingType, SettingRequest),

    #[error("Unsupported request for setting type: {0:?}")]
    UnsupportedError(SettingType),

    #[error("Communication error")]
    CommunicationError,

    #[error("Irrecoverable error")]
    IrrecoverableError,

    #[error("Timeout error")]
    TimeoutError,
}

impl From<ControllerError> for SwitchboardError {
    fn from(error: ControllerError) -> Self {
        match error {
            ControllerError::UnimplementedRequest(setting_type, request) => {
                SwitchboardError::UnimplementedRequest(setting_type, request)
            }
            ControllerError::WriteFailure(setting_type) => {
                SwitchboardError::StorageFailure(setting_type)
            }
            ControllerError::InitFailure(description) => SwitchboardError::InitFailure(description),
            ControllerError::RestoreFailure(description) => {
                SwitchboardError::RestoreFailure(description)
            }
            ControllerError::ExternalFailure(setting_type, dependency, request) => {
                SwitchboardError::ExternalFailure(setting_type, dependency, request)
            }
            ControllerError::InvalidArgument(setting_type, argument, value) => {
                SwitchboardError::InvalidArgument(setting_type, argument, value)
            }
            ControllerError::UnhandledType(setting_type) => {
                SwitchboardError::UnhandledType(setting_type)
            }
            ControllerError::UnexpectedError(error) => SwitchboardError::UnexpectedError(error),
            ControllerError::UndeliverableError(setting_type, request) => {
                SwitchboardError::UndeliverableError(setting_type, request)
            }
            ControllerError::UnsupportedError(setting_type) => {
                SwitchboardError::UnsupportedError(setting_type)
            }
            ControllerError::DeliveryError(setting_type, setting_type_2) => {
                SwitchboardError::DeliveryError(setting_type, setting_type_2)
            }
            ControllerError::IrrecoverableError => SwitchboardError::IrrecoverableError,
            ControllerError::TimeoutError => SwitchboardError::TimeoutError,
            ControllerError::ExitError => SwitchboardError::IrrecoverableError,
        }
    }
}

/// Returns all known setting types. New additions to SettingType should also
/// be inserted here.
pub fn get_all_setting_types() -> HashSet<SettingType> {
    return vec![
        SettingType::Accessibility,
        SettingType::Audio,
        SettingType::Device,
        SettingType::Display,
        SettingType::DoNotDisturb,
        SettingType::FactoryReset,
        SettingType::Input,
        SettingType::Intl,
        SettingType::Light,
        SettingType::LightSensor,
        SettingType::NightMode,
        SettingType::Power,
        SettingType::Privacy,
        SettingType::Setup,
    ]
    .into_iter()
    .collect();
}

/// Returns default setting types. These types should be product-agnostic,
/// capable of operating with platform level support.
pub fn get_default_setting_types() -> HashSet<SettingType> {
    return vec![
        SettingType::Accessibility,
        SettingType::Device,
        SettingType::Intl,
        SettingType::Power,
        SettingType::Privacy,
        SettingType::Setup,
    ]
    .into_iter()
    .collect();
}

/// The possible requests that can be made on a setting. The sink will expect a
/// subset of the values defined below based on the associated type.
/// The types are arranged alphabetically.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingRequest {
    Get,

    // Input events.
    OnButton(ButtonType),
    OnVolume(VolumeGain),

    // Accessibility requests.
    SetAccessibilityInfo(AccessibilityInfo),

    // Account requests
    ScheduleClearAccounts,

    // Audio requests.
    SetVolume(Vec<AudioStream>),

    // Audio in requests.
    SetMicMute(bool),

    // Display requests.
    SetBrightness(f32),
    SetAutoBrightness(bool),
    SetLowLightMode(LowLightMode),
    SetScreenEnabled(bool),
    SetTheme(Theme),

    // Do not disturb requests.
    SetDnD(DoNotDisturbInfo),

    // Factory Reset requests.
    SetLocalResetAllowed(bool),

    // Input requests.
    SetInputStates(Vec<InputDevice>),

    // Intl requests.
    SetIntlInfo(IntlInfo),

    // Light requests.
    SetLightGroupValue(String, Vec<LightState>),

    // Night mode requests.
    SetNightModeInfo(NightModeInfo),

    // Power requests.
    Reboot,

    // Restores settings to outside dependencies.
    Restore,

    // Privacy requests.
    SetUserDataSharingConsent(Option<bool>),

    // Setup info requests.
    SetConfigurationInterfaces(ConfigurationInterfaceFlags),
}

impl SettingRequest {
    /// Returns the name of the enum, for writing to inspect.
    /// TODO(fxbug.dev/56718): write a macro to simplify this
    pub fn for_inspect(self) -> &'static str {
        match self {
            SettingRequest::Get => "Get",
            SettingRequest::OnButton(_) => "OnButton",
            SettingRequest::OnVolume(_) => "OnVolume",
            SettingRequest::SetAccessibilityInfo(_) => "SetAccessibilityInfo",
            SettingRequest::ScheduleClearAccounts => "ScheduleClearAccounts",
            SettingRequest::SetVolume(_) => "SetVolume",
            SettingRequest::SetMicMute(_) => "SetMicMute",
            SettingRequest::SetBrightness(_) => "SetBrightness",
            SettingRequest::SetAutoBrightness(_) => "SetAutoBrightness",
            SettingRequest::SetLocalResetAllowed(_) => "SetLocalResetAllowed",
            SettingRequest::SetLowLightMode(_) => "SetLowLightMode",
            SettingRequest::SetDnD(_) => "SetDnD",
            SettingRequest::SetInputStates(_) => "SetInputStates",
            SettingRequest::SetIntlInfo(_) => "SetIntlInfo",
            SettingRequest::SetLightGroupValue(_, _) => "SetLightGroupValue",
            SettingRequest::SetNightModeInfo(_) => "SetNightModeInfo",
            SettingRequest::Reboot => "Reboot",
            SettingRequest::Restore => "Restore",
            SettingRequest::SetUserDataSharingConsent(_) => "SetUserDataSharingConsent",
            SettingRequest::SetConfigurationInterfaces(_) => "SetConfigurationInterfaces",
            SettingRequest::SetScreenEnabled(_) => "SetScreenEnabled",
            SettingRequest::SetTheme(_) => "SetTheme",
        }
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

#[derive(Debug, Copy, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FactoryResetInfo {
    pub is_local_reset_allowed: bool,
}

impl FactoryResetInfo {
    pub const fn new(is_local_reset_allowed: bool) -> Self {
        Self { is_local_reset_allowed }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct PrivacyInfo {
    pub user_data_sharing_consent: Option<bool>,
}

#[derive(PartialEq, Debug, Clone, Copy, Deserialize, Serialize)]
pub struct SetupInfo {
    pub configuration_interfaces: ConfigurationInterfaceFlags,
}

/// Description of an action request on a setting. This wraps a
/// SettingActionData, providing destination details (setting type) along with
/// callback information (action id).
#[derive(PartialEq, Debug, Clone)]
pub struct SettingAction {
    pub id: u64,
    pub setting_type: SettingType,
    pub data: SettingActionData,
}

/// The types of actions. Note that specific request types should be enumerated
/// in the SettingRequest enum.
#[derive(PartialEq, Debug, Clone)]
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
#[derive(PartialEq, Clone, Debug)]
pub enum SettingEvent {
    /// The setting's data has changed. The setting type associated with this
    /// event is implied by the association of the signature of the sending
    /// proxy to the setting type. This mapping is maintained by the
    /// Switchboard. The [`SettingInfo`] provided represents the most up-to-date
    /// data at the time of this event.
    Changed(SettingInfo),
    /// A response to a previous SettingActionData::Request is ready. The source
    /// SettingAction's id is provided alongside the result.
    Response(u64, SettingHandlerResult),
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
    use fuchsia_zircon as zx;

    use super::*;

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

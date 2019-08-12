// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use futures::channel::mpsc::UnboundedSender;
use futures::channel::oneshot::Sender;

pub type SettingResponseResult = Result<Option<SettingResponse>, Error>;
pub type SettingRequestResponder = Sender<SettingResponseResult>;

/// The setting types supported by the messaging system. This is used as a key
/// for listening to change notifications and sending requests.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum SettingType {
    Unknown,
    Display,
    Intl,
}

/// The possible requests that can be made on a setting. The sink will expect a
/// subset of the values defined below based on the associated type.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingRequest {
    Get,
    SetBrightness(f32),
    SetAutoBrightness(bool),
    SetTimeZone(String),
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum BrightnessInfo {
    ManualBrightness(f32),
    AutoBrightness,
}

#[derive(PartialEq, Debug, Clone)]
pub struct IntlInfo {
    pub time_zone_id: String,
}

/// The possible responses to a SettingRequest.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingResponse {
    Unknown,
    /// Response to a request to get current brightness state.AccessibilityEncoder
    Brightness(BrightnessInfo),
    Intl(IntlInfo),
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
    ) -> Result<(), Error>;
}

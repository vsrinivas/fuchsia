// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Request;
use crate::handler::setting_handler::SettingHandlerResult;

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
/// in the Request enum.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingActionData {
    /// The listening state has changed for the particular setting. The provided
    /// value indicates the number of active listeners. 0 indicates there are
    /// no more listeners.
    Listen(u64),
    /// A request has been made on a particular setting. The specific setting
    /// and request data are encoded in Request.
    Request(Request),
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

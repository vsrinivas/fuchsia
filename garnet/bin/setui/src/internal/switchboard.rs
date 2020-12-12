// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::message_hub_definition;
use crate::switchboard::base::{SettingRequest, SettingResponseResult, SettingType};

#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Address {
    Switchboard,
}

// The types of data that can be sent.
#[derive(Clone, Debug)]
pub enum Payload {
    Action(Action),
    Listen(Listen),
}

#[derive(Clone, Debug)]
pub enum Listen {
    // Indicates the caller would like to listen to changes on the specified
    // `SettingType`.
    Request(SettingType),
    // Indicates an update is available for the specified `SettingType`.
    Update(SettingInfo),
}

#[derive(Clone, Debug)]
pub enum Action {
    // Defines a request to be acted upon the specified `SettingType`.
    Request(SettingType, SettingRequest),
    // Contains the response to the last request.
    Response(SettingResponseResult),
}

message_hub_definition!(Payload, Address);

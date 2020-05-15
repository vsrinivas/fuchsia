// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_light::{Capability, GroupInfo, Info, Rgb};
use serde::{Deserialize, Serialize};

pub enum LightMethod {
    GetNumLights,
    GetNumLightGroups,
    GetInfo,
    GetCurrentSimpleValue,
    SetSimpleValue,
    GetCurrentBrightnessValue,
    SetBrightnessValue,
    GetCurrentRgbValue,
    SetRgbValue,
    GetGroupInfo,
    GetGroupCurrentSimpleValue,
    SetGroupSimpleValue,
    GetGroupCurrentBrightnessValue,
    SetGroupBrightnessValue,
    GetGroupCurrentRgbValue,
    SetGroupRgbValue,
    UndefinedFunc,
}

impl LightMethod {
    pub fn from_str(method: &String) -> LightMethod {
        match method.as_ref() {
            "GetNumLights" => LightMethod::GetNumLights,
            "GetNumLightGroups" => LightMethod::GetNumLightGroups,
            "GetInfo" => LightMethod::GetInfo,
            "GetCurrentSimpleValue" => LightMethod::GetCurrentSimpleValue,
            "SetSimpleValue" => LightMethod::SetSimpleValue,
            "GetCurrentBrightnessValue" => LightMethod::GetCurrentBrightnessValue,
            "SetBrightnessValue" => LightMethod::SetBrightnessValue,
            "GetCurrentRgbValue" => LightMethod::GetCurrentRgbValue,
            "SetRgbValue" => LightMethod::SetRgbValue,
            "GetGroupInfo" => LightMethod::GetGroupInfo,
            "GetGroupCurrentSimpleValue" => LightMethod::GetGroupCurrentSimpleValue,
            "SetGroupSimpleValue" => LightMethod::SetGroupSimpleValue,
            "GetGroupCurrentBrightnessValue" => LightMethod::GetGroupCurrentBrightnessValue,
            "SetGroupBrightnessValue" => LightMethod::SetGroupBrightnessValue,
            "GetGroupCurrentRgbValue" => LightMethod::GetGroupCurrentRgbValue,
            "SetGroupRgbValue" => LightMethod::SetGroupRgbValue,
            _ => LightMethod::UndefinedFunc,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub enum SerializableCapability {
    Brightness,
    Rgb,
    Simple,
}

impl From<Capability> for SerializableCapability {
    fn from(x: Capability) -> Self {
        match x {
            Capability::Brightness => SerializableCapability::Brightness,
            Capability::Rgb => SerializableCapability::Rgb,
            Capability::Simple => SerializableCapability::Simple,
        }
    }
}

impl From<SerializableCapability> for Capability {
    fn from(x: SerializableCapability) -> Self {
        match x {
            SerializableCapability::Brightness => Capability::Brightness,
            SerializableCapability::Rgb => Capability::Rgb,
            SerializableCapability::Simple => Capability::Simple,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct SerializableInfo {
    pub name: String,
    pub capability: SerializableCapability,
}

/// Info object is not serializable so serialize the object
impl SerializableInfo {
    pub fn new(info: &Info) -> Self {
        SerializableInfo {
            name: info.name.clone(),
            capability: SerializableCapability::from(info.capability),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SerializableRgb {
    pub red: u8,
    pub green: u8,
    pub blue: u8,
}

/// Rgb object is not serializable so serialize the object
impl SerializableRgb {
    pub fn new(rgb: &Rgb) -> Self {
        SerializableRgb { red: rgb.red, green: rgb.green, blue: rgb.blue }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct SerializableGroupInfo {
    pub name: String,
    pub count: u32,
    pub capability: SerializableCapability,
}

/// GroupInfo object is not serializable so serialize the object
impl SerializableGroupInfo {
    pub fn new(group_info: &GroupInfo) -> Self {
        SerializableGroupInfo {
            name: group_info.name.clone(),
            count: group_info.count,
            capability: SerializableCapability::from(group_info.capability),
        }
    }
}

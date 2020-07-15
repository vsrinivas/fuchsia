// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct LightInfo {
    pub light_groups: HashMap<String, LightGroup>,
}

impl LightInfo {
    pub fn contains_light_group_name(self, name: String) -> bool {
        self.light_groups.contains_key(name.as_str())
    }
}

/// Internal representation of a light group.
#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct LightGroup {
    pub name: Option<String>,
    pub enabled: Option<bool>,
    pub light_type: Option<LightType>,
    pub lights: Option<Vec<LightState>>,
}

impl From<fidl_fuchsia_settings::LightGroup> for LightGroup {
    fn from(src: fidl_fuchsia_settings::LightGroup) -> Self {
        LightGroup {
            name: src.name,
            enabled: src.enabled,
            light_type: src.type_.map(LightType::from),
            lights: src.lights.map(|lights| lights.into_iter().map(LightState::from).collect()),
        }
    }
}

impl From<LightGroup> for fidl_fuchsia_settings::LightGroup {
    fn from(src: LightGroup) -> Self {
        fidl_fuchsia_settings::LightGroup {
            name: src.name,
            enabled: src.enabled,
            type_: src.light_type.map(LightType::into),
            lights: src.lights.map(|lights| lights.into_iter().map(LightState::into).collect()),
        }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub enum LightType {
    Brightness,
    Rgb,
    Simple,
}

impl From<fidl_fuchsia_settings::LightType> for LightType {
    fn from(src: fidl_fuchsia_settings::LightType) -> Self {
        match src {
            fidl_fuchsia_settings::LightType::Brightness => LightType::Brightness,
            fidl_fuchsia_settings::LightType::Rgb => LightType::Rgb,
            fidl_fuchsia_settings::LightType::Simple => LightType::Simple,
        }
    }
}

impl From<fidl_fuchsia_hardware_light::Capability> for LightType {
    fn from(src: fidl_fuchsia_hardware_light::Capability) -> Self {
        match src {
            fidl_fuchsia_hardware_light::Capability::Brightness => LightType::Brightness,
            fidl_fuchsia_hardware_light::Capability::Rgb => LightType::Rgb,
            fidl_fuchsia_hardware_light::Capability::Simple => LightType::Simple,
        }
    }
}

impl From<LightType> for fidl_fuchsia_settings::LightType {
    fn from(src: LightType) -> Self {
        match src {
            LightType::Brightness => fidl_fuchsia_settings::LightType::Brightness,
            LightType::Rgb => fidl_fuchsia_settings::LightType::Rgb,
            LightType::Simple => fidl_fuchsia_settings::LightType::Simple,
        }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct LightState {
    pub value: Option<LightValue>,
}

impl From<fidl_fuchsia_settings::LightState> for LightState {
    fn from(src: fidl_fuchsia_settings::LightState) -> Self {
        LightState { value: src.value.map(LightValue::from) }
    }
}

impl From<LightState> for fidl_fuchsia_settings::LightState {
    fn from(src: LightState) -> Self {
        fidl_fuchsia_settings::LightState {
            value: src.value.map(fidl_fuchsia_settings::LightValue::from),
        }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub enum LightValue {
    Brightness(u8),
    Rgb(ColorRgb),
    Simple(bool),
}

impl From<fidl_fuchsia_settings::LightValue> for LightValue {
    fn from(src: fidl_fuchsia_settings::LightValue) -> Self {
        match src {
            fidl_fuchsia_settings::LightValue::On(on) => LightValue::Simple(on),
            fidl_fuchsia_settings::LightValue::Brightness(brightness) => {
                LightValue::Brightness(brightness)
            }
            fidl_fuchsia_settings::LightValue::Color(color) => LightValue::Rgb(color.into()),
        }
    }
}

impl From<fidl_fuchsia_hardware_light::Rgb> for LightValue {
    fn from(src: fidl_fuchsia_hardware_light::Rgb) -> Self {
        LightValue::Rgb(ColorRgb {
            red: src.red as f32,
            green: src.green as f32,
            blue: src.blue as f32,
        })
    }
}

impl From<LightValue> for fidl_fuchsia_settings::LightValue {
    fn from(src: LightValue) -> Self {
        match src {
            LightValue::Simple(on) => fidl_fuchsia_settings::LightValue::On(on),
            LightValue::Brightness(brightness) => {
                fidl_fuchsia_settings::LightValue::Brightness(brightness)
            }
            LightValue::Rgb(color) => fidl_fuchsia_settings::LightValue::Color(color.into()),
        }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct ColorRgb {
    pub red: f32,
    pub green: f32,
    pub blue: f32,
}

impl From<fidl_fuchsia_ui_types::ColorRgb> for ColorRgb {
    fn from(src: fidl_fuchsia_ui_types::ColorRgb) -> Self {
        ColorRgb { red: src.red.into(), green: src.green.into(), blue: src.blue.into() }
    }
}

impl From<ColorRgb> for fidl_fuchsia_ui_types::ColorRgb {
    fn from(src: ColorRgb) -> Self {
        fidl_fuchsia_ui_types::ColorRgb {
            red: src.red.into(),
            green: src.green.into(),
            blue: src.blue.into(),
        }
    }
}

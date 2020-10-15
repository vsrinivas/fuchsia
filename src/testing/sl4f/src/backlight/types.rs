// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_backlight::State;
use serde::{Deserialize, Serialize};

pub enum BacklightMethod {
    GetStateNormalized,
    SetStateNormalized,
    GetNormalizedBrightnessScale,
    SetNormalizedBrightnessScale,
    UndefinedFunc,
}

impl std::str::FromStr for BacklightMethod {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(match s.as_ref() {
            "GetStateNormalized" => BacklightMethod::GetStateNormalized,
            "SetStateNormalized" => BacklightMethod::SetStateNormalized,
            "GetNormalizedBrightnessScale" => BacklightMethod::GetNormalizedBrightnessScale,
            "SetNormalizedBrightnessScale" => BacklightMethod::SetNormalizedBrightnessScale,
            _ => BacklightMethod::UndefinedFunc,
        })
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct SerializableState {
    pub backlight_on: bool,
    pub brightness: f64,
}

impl std::convert::From<State> for SerializableState {
    fn from(state: State) -> Self {
        SerializableState { backlight_on: state.backlight_on, brightness: state.brightness }
    }
}

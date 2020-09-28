// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Deserializer, Serialize};

use fidl_fuchsia_ui_input::Touch;

/// Enum for supported Input commands.
pub enum InputMethod {
    Tap,
    MultiFingerTap,
    Swipe,
    MultiFingerSwipe,
}

impl std::str::FromStr for InputMethod {
    type Err = anyhow::Error;
    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Tap" => Ok(InputMethod::Tap),
            "MultiFingerTap" => Ok(InputMethod::MultiFingerTap),
            "Swipe" => Ok(InputMethod::Swipe),
            "MultiFingerSwipe" => Ok(InputMethod::MultiFingerSwipe),
            _ => return Err(format_err!("Invalid Input Facade method: {}", method)),
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct MultiFingerTapRequest {
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub tap_event_count: Option<usize>,
    pub duration: Option<u64>,

    #[serde(default, deserialize_with = "TouchDef::vec")]
    pub fingers: Vec<Touch>,
}

#[derive(Deserialize, Debug)]
pub struct TapRequest {
    pub x: u32,
    pub y: u32,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub tap_event_count: Option<usize>,
    pub duration: Option<u64>,
}

#[derive(Deserialize, Debug)]
pub struct SwipeRequest {
    pub x0: u32,
    pub y0: u32,
    pub x1: u32,
    pub y1: u32,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub tap_event_count: Option<usize>,
    pub duration: Option<u64>,
}

#[derive(Deserialize, Debug)]
/// Describes a start and end position for a single finger.
pub struct FingerSwipe {
    pub x0: u32,
    pub y0: u32,
    pub x1: u32,
    pub y1: u32,
}

#[derive(Deserialize, Debug)]
pub struct MultiFingerSwipeRequest {
    pub fingers: Vec<FingerSwipe>,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub move_event_count: Option<usize>,
    pub duration: Option<u64>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ActionResult {
    Success,
    Fail,
}

/// This matches the FIDL struct `Touch` defined at
/// sdk/fidl/fuchsia.ui.input/input_reports.fidl
/// and is enforced by the build system.
#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "Touch")]
pub struct TouchDef {
    pub finger_id: u32,
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

/// serde does not work with Vec.
/// This requires us to write our own Deserializer for Vec<Touch>.
/// https://github.com/serde-rs/serde/issues/723#issuecomment-382501277
impl TouchDef {
    fn vec<'de, D>(deserializer: D) -> Result<Vec<Touch>, D::Error>
    where
        D: Deserializer<'de>,
    {
        #[derive(Deserialize)]
        struct Wrapper(#[serde(deserialize_with = "deserialize_element")] Touch);

        fn deserialize_element<'de, D>(deserializer: D) -> Result<Touch, D::Error>
        where
            D: Deserializer<'de>,
        {
            TouchDef::deserialize(deserializer)
        }

        let v = Vec::deserialize(deserializer)?;
        Ok(v.into_iter().map(|Wrapper(a)| a).collect())
    }
}

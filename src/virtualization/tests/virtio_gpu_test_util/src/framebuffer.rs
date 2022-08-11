// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {serde::Serialize, serde_json::json};

#[derive(Serialize, Debug, Default)]
pub struct DisplayInfo {
    pub id: String,
    pub width: u32,
    pub height: u32,
}

#[derive(Serialize, Debug, Default)]
pub struct DetectResult {
    /// A list of detected displays.
    pub displays: Vec<DisplayInfo>,
    /// If any errors were encountered, they're returned here.
    pub error: serde_json::Value,
    /// Platform-specific details about the detected display. This is for informational and
    /// debugging purposes only.
    pub details: serde_json::Value,
}

impl DetectResult {
    pub fn from_error(e: anyhow::Error) -> Self {
        DetectResult { error: json!(format!("{}", e)), ..Default::default() }
    }
}

pub trait Framebuffer {
    fn detect_displays(&self) -> DetectResult;
}

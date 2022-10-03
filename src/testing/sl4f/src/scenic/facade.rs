// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::scenic::types::ScreenshotDataDef;
use anyhow::{Context as _, Error};
use fidl_fuchsia_ui_composition::{self as ui_comp, ScreenshotMarker};
use fidl_fuchsia_ui_scenic::ScreenshotData;
use fuchsia_component::{self as app};
use serde_json::{to_value, Value};

/// Perform Scenic operations.
///
/// Note this object is shared among all threads created by server.
#[derive(Debug)]
pub struct ScenicFacade {}

impl ScenicFacade {
    pub fn new() -> ScenicFacade {
        ScenicFacade {}
    }
    pub async fn take_screenshot(&self) -> Result<Value, Error> {
        // Connect to the screenshot protocol.
        let screenshotter = app::client::connect_to_protocol::<ScreenshotMarker>()
            .context("Failed to connect to screenshot")?;

        let gnf_args = ui_comp::ScreenshotTakeRequest {
            format: Some(ui_comp::ScreenshotFormat::BgraRaw),
            ..ui_comp::ScreenshotTakeRequest::EMPTY
        };

        let screenshot_response: ui_comp::ScreenshotTakeResponse;
        let data = screenshotter.take(gnf_args).await;

        match data {
            Err(e) => {
                return Err(format_err!("Screenshot.Take() failed with FIDL err: {}", e));
            }
            Ok(val) => screenshot_response = val,
        }

        let image_vmo = screenshot_response
            .vmo
            .ok_or(format_err!("invalid vmo format in ScreenshotTakeResponse"))?;

        let image_size = screenshot_response
            .size
            .ok_or(format_err!("invalid size format in ScreenshotTakeResponse"))?;

        let image_info = fidl_fuchsia_images::ImageInfo {
            transform: fidl_fuchsia_images::Transform::Normal,
            width: image_size.width,
            height: image_size.height,
            stride: image_size.width * 4,
            pixel_format: fidl_fuchsia_images::PixelFormat::Bgra8,
            color_space: fidl_fuchsia_images::ColorSpace::Srgb,
            tiling: fidl_fuchsia_images::Tiling::Linear,
            alpha_format: fidl_fuchsia_images::AlphaFormat::Opaque,
        };

        let buffer = fidl_fuchsia_mem::Buffer {
            vmo: image_vmo,
            size: u64::from(image_size.height * image_size.width * 4),
        };

        let screenshot: ScreenshotData = ScreenshotData { info: image_info, data: buffer };

        return Ok(to_value(ScreenshotDataDef::new(screenshot))?);
    }
}

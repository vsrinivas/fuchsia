// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use base64;
use fidl_fuchsia_images::{AlphaFormat, ColorSpace, ImageInfo, PixelFormat, Tiling, Transform};
use fidl_fuchsia_mem::Buffer;
use fidl_fuchsia_ui_scenic::ScreenshotData;
use serde::Serializer;
use serde_derive::{Deserialize, Serialize};

/// Enum for supported FIDL commands.
pub enum ScenicMethod {
    TakeScreenshot,
}

impl std::str::FromStr for ScenicMethod {
    type Err = failure::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "TakeScreenshot" => Ok(ScenicMethod::TakeScreenshot),
            _ => bail!("invalid Scenic FIDL method: {}", method),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "Transform")]
pub enum TransformDef {
    Normal = 0,
    FlipHorizontal = 1,
    FlipVertical = 2,
    FlipVerticalAndHorizontal = 3,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "PixelFormat")]
pub enum PixelFormatDef {
    Bgra8 = 0,
    Yuy2 = 1,
    Nv12 = 2,
    Yv12 = 3,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "ColorSpace")]
pub enum ColorSpaceDef {
    Srgb = 0,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "Tiling")]
pub enum TilingDef {
    Linear = 0,
    GpuOptimal = 1,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "AlphaFormat")]
pub enum AlphaFormatDef {
    Opaque = 0,
    Premultiplied = 1,
    NonPremultiplied = 2,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "ImageInfo")]
pub struct ImageInfoDef {
    #[serde(with = "TransformDef")]
    pub transform: Transform,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    #[serde(with = "PixelFormatDef")]
    pub pixel_format: PixelFormat,
    #[serde(with = "ColorSpaceDef")]
    pub color_space: ColorSpace,
    #[serde(with = "TilingDef")]
    pub tiling: Tiling,
    #[serde(with = "AlphaFormatDef")]
    pub alpha_format: AlphaFormat,
}

fn serialize_buffer<S>(buffer: &Buffer, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    let mut data = vec![0; buffer.size as usize];
    use serde::ser::Error;
    buffer.vmo.read(&mut data, 0).map_err(Error::custom)?;
    serializer.serialize_str(&base64::encode(&data))
}

#[derive(Serialize, Debug)]
pub struct ScreenshotDataDef {
    #[serde(with = "ImageInfoDef")]
    pub info: ImageInfo,
    #[serde(serialize_with = "serialize_buffer")]
    pub data: Buffer,
}

impl ScreenshotDataDef {
    pub fn new(screenshot_data: ScreenshotData) -> ScreenshotDataDef {
        ScreenshotDataDef { info: screenshot_data.info, data: screenshot_data.data }
    }
}

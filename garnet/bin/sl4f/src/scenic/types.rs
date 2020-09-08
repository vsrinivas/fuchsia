// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use base64;
use fidl_fuchsia_images::{AlphaFormat, ColorSpace, ImageInfo, PixelFormat, Tiling, Transform};
use fidl_fuchsia_intl::{CalendarId, LocaleId, Profile, TemperatureUnit, TimeZoneId};
use fidl_fuchsia_mem::Buffer;
use fidl_fuchsia_ui_app::ViewConfig;
use fidl_fuchsia_ui_scenic::ScreenshotData;
use serde::{Deserialize, Deserializer, Serialize, Serializer};

/// Enum for supported FIDL commands.
pub enum ScenicMethod {
    TakeScreenshot,
    PresentView,
}

impl std::str::FromStr for ScenicMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "TakeScreenshot" => Ok(ScenicMethod::TakeScreenshot),
            "PresentView" => Ok(ScenicMethod::PresentView),
            _ => return Err(format_err!("invalid Scenic FIDL method: {}", method)),
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
    R8G8B8A8 = 4,
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

// https://github.com/serde-rs/serde/issues/723
macro_rules! remote_vec {
    ($remote:ident with=$def:ident) => {
        impl $def {
            fn opt_vec<'de, D>(deserializer: D) -> Result<Option<Vec<$remote>>, D::Error>
            where
                D: Deserializer<'de>,
            {
                #[derive(Deserialize)]
                struct Wrapper(#[serde(deserialize_with = "deserialize_element")] $remote);

                fn deserialize_element<'de, D>(deserializer: D) -> Result<$remote, D::Error>
                where
                    D: Deserializer<'de>,
                {
                    $def::deserialize(deserializer)
                }

                let o: Option<Vec<Wrapper>> = Deserialize::deserialize(deserializer)?;
                Ok(o.map(|v| v.into_iter().map(|Wrapper(a)| a).collect()))
            }
        }
    };
}

// https://github.com/serde-rs/serde/issues/723
macro_rules! remote_optional {
    ($remote:ident with=$def:ident) => {
        impl $def {
            fn deserialize_option<'de, D>(deserializer: D) -> Result<Option<$remote>, D::Error>
            where
                D: Deserializer<'de>,
            {
                use $def as Def;
                #[derive(Deserialize)]
                struct Wrapper(#[serde(with = "Def")] $remote);

                let v: Option<Wrapper> = Option::deserialize(deserializer)?;
                Ok(v.map(|Wrapper(a)| a))
            }
        }
    };
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "LocaleId")]
pub struct LocaleIdDef {
    pub id: String,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "CalendarId")]
pub struct CalendarIdDef {
    pub id: String,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "TimeZoneId")]
pub struct TimeZoneIdDef {
    pub id: String,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(remote = "TemperatureUnit")]
pub enum TemperatureUnitDef {
    Celsius = 0,
    Fahrenheit = 1,
}

remote_vec!(LocaleId with=LocaleIdDef);
remote_vec!(CalendarId with=CalendarIdDef);
remote_vec!(TimeZoneId with=TimeZoneIdDef);

remote_optional!(TemperatureUnit with=TemperatureUnitDef);
remote_optional!(ViewConfig with=ViewConfigDef);

// TODO(fxb/59274): Do not use Serde remote for FIDL tables.
#[derive(Deserialize, Debug)]
#[serde(remote = "Profile")]
pub struct ProfileDef {
    #[serde(default, deserialize_with = "LocaleIdDef::opt_vec")]
    pub locales: Option<Vec<LocaleId>>,
    #[serde(default, deserialize_with = "CalendarIdDef::opt_vec")]
    pub calendars: Option<Vec<CalendarId>>,
    #[serde(default, deserialize_with = "TimeZoneIdDef::opt_vec")]
    pub time_zones: Option<Vec<TimeZoneId>>,
    #[serde(default, deserialize_with = "TemperatureUnitDef::deserialize_option")]
    pub temperature_unit: Option<TemperatureUnit>,
}

#[derive(Deserialize, Debug)]
#[serde(remote = "ViewConfig")]
pub struct ViewConfigDef {
    #[serde(with = "ProfileDef")]
    pub intl_profile: Profile,
}

#[derive(Deserialize, Debug)]
pub struct PresentViewRequest {
    pub url: String,
    #[serde(default, deserialize_with = "ViewConfigDef::deserialize_option")]
    pub config: Option<ViewConfig>,
}

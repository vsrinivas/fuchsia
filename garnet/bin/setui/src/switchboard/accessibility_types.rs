// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::base::Merge;
use serde_derive::{Deserialize, Serialize};

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AccessibilityInfo {
    pub audio_description: Option<bool>,
    pub screen_reader: Option<bool>,
    pub color_inversion: Option<bool>,
    pub enable_magnification: Option<bool>,
    pub color_correction: Option<ColorBlindnessType>,
    pub captions_settings: Option<CaptionsSettings>,
}

impl Merge for AccessibilityInfo {
    fn merge(&self, other: Self) -> Self {
        AccessibilityInfo {
            audio_description: self.audio_description.or(other.audio_description),
            screen_reader: self.screen_reader.or(other.screen_reader),
            color_inversion: self.color_inversion.or(other.color_inversion),
            enable_magnification: self.enable_magnification.or(other.enable_magnification),
            color_correction: self
                .color_correction
                .map(ColorBlindnessType::into)
                .or(other.color_correction),
            captions_settings: match (self.captions_settings, other.captions_settings) {
                (Some(caption_settings), Some(other_caption_settings)) => {
                    Some(caption_settings.merge(other_caption_settings))
                }
                _ => self.captions_settings.or(other.captions_settings),
            },
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum ColorBlindnessType {
    /// No color blindness.
    None,

    /// Red-green color blindness due to reduced sensitivity to red light.
    Protanomaly,

    /// Red-green color blindness due to reduced sensitivity to green light.
    Deuteranomaly,

    /// Blue-yellow color blindness. It is due to reduced sensitivity to blue
    /// light.
    Tritanomaly,
}

impl From<fidl_fuchsia_settings::ColorBlindnessType> for ColorBlindnessType {
    fn from(color_blindness_type: fidl_fuchsia_settings::ColorBlindnessType) -> Self {
        match color_blindness_type {
            fidl_fuchsia_settings::ColorBlindnessType::None => ColorBlindnessType::None,
            fidl_fuchsia_settings::ColorBlindnessType::Protanomaly => {
                ColorBlindnessType::Protanomaly
            }
            fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly => {
                ColorBlindnessType::Deuteranomaly
            }
            fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly => {
                ColorBlindnessType::Tritanomaly
            }
        }
    }
}

impl From<ColorBlindnessType> for fidl_fuchsia_settings::ColorBlindnessType {
    fn from(color_blindness_type: ColorBlindnessType) -> Self {
        match color_blindness_type {
            ColorBlindnessType::None => fidl_fuchsia_settings::ColorBlindnessType::None,
            ColorBlindnessType::Protanomaly => {
                fidl_fuchsia_settings::ColorBlindnessType::Protanomaly
            }
            ColorBlindnessType::Deuteranomaly => {
                fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly
            }
            ColorBlindnessType::Tritanomaly => {
                fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly
            }
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct CaptionsSettings {
    pub for_media: Option<bool>,
    pub for_tts: Option<bool>,
    pub font_style: Option<CaptionFontStyle>,
    pub window_color: Option<ColorRgba>,
    pub background_color: Option<ColorRgba>,
}

impl Merge for CaptionsSettings {
    fn merge(&self, other: Self) -> Self {
        CaptionsSettings {
            for_media: self.for_media.or(other.for_media),
            for_tts: self.for_tts.or(other.for_tts),
            window_color: self.window_color.or(other.window_color),
            background_color: self.background_color.or(other.background_color),
            font_style: match (self.font_style, other.font_style) {
                (Some(style), Some(other_style)) => Some(style.merge(other_style)),
                _ => self.font_style.or(other.font_style),
            },
        }
    }
}

impl From<fidl_fuchsia_settings::CaptionsSettings> for CaptionsSettings {
    fn from(src: fidl_fuchsia_settings::CaptionsSettings) -> Self {
        CaptionsSettings {
            for_media: src.for_media,
            for_tts: src.for_tts,
            font_style: src.font_style.map(fidl_fuchsia_settings::CaptionFontStyle::into),
            window_color: src.window_color.map(fidl_fuchsia_ui_types::ColorRgba::into),
            background_color: src.background_color.map(fidl_fuchsia_ui_types::ColorRgba::into),
        }
    }
}

impl From<CaptionsSettings> for fidl_fuchsia_settings::CaptionsSettings {
    fn from(src: CaptionsSettings) -> Self {
        let mut settings = fidl_fuchsia_settings::CaptionsSettings::empty();
        settings.for_media = src.for_media;
        settings.for_tts = src.for_tts;
        settings.font_style = src.font_style.map(CaptionFontStyle::into);
        settings.window_color = src.window_color.map(ColorRgba::into);
        settings.background_color = src.background_color.map(ColorRgba::into);
        settings
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct CaptionFontStyle {
    pub family: Option<CaptionFontFamily>,
    pub color: Option<ColorRgba>,
    pub relative_size: Option<f32>,
    pub char_edge_style: Option<EdgeStyle>,
}

impl Merge for CaptionFontStyle {
    fn merge(&self, other: Self) -> Self {
        CaptionFontStyle {
            family: self.family.or(other.family),
            color: self.color.or(other.color),
            relative_size: self.relative_size.or(other.relative_size),
            char_edge_style: self.char_edge_style.or(other.char_edge_style),
        }
    }
}

impl From<fidl_fuchsia_settings::CaptionFontStyle> for CaptionFontStyle {
    fn from(src: fidl_fuchsia_settings::CaptionFontStyle) -> Self {
        CaptionFontStyle {
            family: src.family.map(fidl_fuchsia_settings::CaptionFontFamily::into),
            color: src.color.map(fidl_fuchsia_ui_types::ColorRgba::into),
            relative_size: src.relative_size,
            char_edge_style: src.char_edge_style.map(fidl_fuchsia_settings::EdgeStyle::into),
        }
    }
}

impl From<CaptionFontStyle> for fidl_fuchsia_settings::CaptionFontStyle {
    fn from(src: CaptionFontStyle) -> Self {
        let mut style = fidl_fuchsia_settings::CaptionFontStyle::empty();
        style.family = src.family.map(CaptionFontFamily::into);
        style.color = src.color.map(ColorRgba::into);
        style.relative_size = src.relative_size;
        style.char_edge_style = src.char_edge_style.map(EdgeStyle::into);
        style
    }
}

/// Font family groups for closed captions, specified by 47 CFR §79.102(k).
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum CaptionFontFamily {
    Unknown,
    MonospacedSerif,
    ProportionalSerif,
    MonospacedSansSerif,
    ProportionalSansSerif,
    Casual,
    Cursive,
    SmallCapitals,
}

impl From<fidl_fuchsia_settings::CaptionFontFamily> for CaptionFontFamily {
    fn from(src: fidl_fuchsia_settings::CaptionFontFamily) -> Self {
        match src {
            fidl_fuchsia_settings::CaptionFontFamily::Unknown => CaptionFontFamily::Unknown,
            fidl_fuchsia_settings::CaptionFontFamily::MonospacedSerif => {
                CaptionFontFamily::MonospacedSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::ProportionalSerif => {
                CaptionFontFamily::ProportionalSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::MonospacedSansSerif => {
                CaptionFontFamily::MonospacedSansSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::ProportionalSansSerif => {
                CaptionFontFamily::ProportionalSansSerif
            }
            fidl_fuchsia_settings::CaptionFontFamily::Casual => CaptionFontFamily::Casual,
            fidl_fuchsia_settings::CaptionFontFamily::Cursive => CaptionFontFamily::Cursive,
            fidl_fuchsia_settings::CaptionFontFamily::SmallCapitals => {
                CaptionFontFamily::SmallCapitals
            }
        }
    }
}

impl From<CaptionFontFamily> for fidl_fuchsia_settings::CaptionFontFamily {
    fn from(src: CaptionFontFamily) -> Self {
        match src {
            CaptionFontFamily::Unknown => fidl_fuchsia_settings::CaptionFontFamily::Unknown,
            CaptionFontFamily::MonospacedSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::MonospacedSerif
            }
            CaptionFontFamily::ProportionalSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::ProportionalSerif
            }
            CaptionFontFamily::MonospacedSansSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::MonospacedSansSerif
            }
            CaptionFontFamily::ProportionalSansSerif => {
                fidl_fuchsia_settings::CaptionFontFamily::ProportionalSansSerif
            }
            CaptionFontFamily::Casual => fidl_fuchsia_settings::CaptionFontFamily::Casual,
            CaptionFontFamily::Cursive => fidl_fuchsia_settings::CaptionFontFamily::Cursive,
            CaptionFontFamily::SmallCapitals => {
                fidl_fuchsia_settings::CaptionFontFamily::SmallCapitals
            }
        }
    }
}

/// Edge style for fonts as specified in 47 CFR §79.103(c)(7)
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum EdgeStyle {
    /// No border around fonts.
    None,

    /// A shadow "behind" and slightly offset from each edge.
    DropShadow,

    /// A bevel that mimics a 3D raised effect.
    Raised,

    /// A bevel that mimics a 3D depressed effect.
    Depressed,

    /// A plain border around each shapes.
    Outline,
}

impl From<fidl_fuchsia_settings::EdgeStyle> for EdgeStyle {
    fn from(src: fidl_fuchsia_settings::EdgeStyle) -> Self {
        match src {
            fidl_fuchsia_settings::EdgeStyle::None => EdgeStyle::None,
            fidl_fuchsia_settings::EdgeStyle::DropShadow => EdgeStyle::DropShadow,
            fidl_fuchsia_settings::EdgeStyle::Raised => EdgeStyle::Raised,
            fidl_fuchsia_settings::EdgeStyle::Depressed => EdgeStyle::Depressed,
            fidl_fuchsia_settings::EdgeStyle::Outline => EdgeStyle::Outline,
        }
    }
}

impl From<EdgeStyle> for fidl_fuchsia_settings::EdgeStyle {
    fn from(src: EdgeStyle) -> Self {
        match src {
            EdgeStyle::None => fidl_fuchsia_settings::EdgeStyle::None,
            EdgeStyle::DropShadow => fidl_fuchsia_settings::EdgeStyle::DropShadow,
            EdgeStyle::Raised => fidl_fuchsia_settings::EdgeStyle::Raised,
            EdgeStyle::Depressed => fidl_fuchsia_settings::EdgeStyle::Depressed,
            EdgeStyle::Outline => fidl_fuchsia_settings::EdgeStyle::Outline,
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct ColorRgba {
    pub red: f32,
    pub green: f32,
    pub blue: f32,
    pub alpha: f32,
}

impl From<fidl_fuchsia_ui_types::ColorRgba> for ColorRgba {
    fn from(src: fidl_fuchsia_ui_types::ColorRgba) -> Self {
        ColorRgba {
            red: src.red.into(),
            green: src.green.into(),
            blue: src.blue.into(),
            alpha: src.alpha.into(),
        }
    }
}

impl From<ColorRgba> for fidl_fuchsia_ui_types::ColorRgba {
    fn from(src: ColorRgba) -> Self {
        fidl_fuchsia_ui_types::ColorRgba {
            red: src.red.into(),
            green: src.green.into(),
            blue: src.blue.into(),
            alpha: src.alpha.into(),
        }
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::Merge;
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AccessibilityInfo {
    pub audio_description: Option<bool>,
    pub screen_reader: Option<bool>,
    pub color_inversion: Option<bool>,
    pub enable_magnification: Option<bool>,
    pub color_correction: Option<ColorBlindnessType>,
    pub captions_settings: Option<CaptionsSettings>,
}

impl AccessibilityInfo {
    pub(crate) fn is_finite(&self) -> bool {
        self.captions_settings.map_or(true, |captions| captions.is_finite())
    }
}

impl Merge for AccessibilityInfo {
    fn merge(&self, other: Self) -> Self {
        AccessibilityInfo {
            audio_description: other.audio_description.or(self.audio_description),
            screen_reader: other.screen_reader.or(self.screen_reader),
            color_inversion: other.color_inversion.or(self.color_inversion),
            enable_magnification: other.enable_magnification.or(self.enable_magnification),
            color_correction: other
                .color_correction
                .map(ColorBlindnessType::into)
                .or(self.color_correction),
            captions_settings: match (self.captions_settings, other.captions_settings) {
                (Some(caption_settings), Some(other_caption_settings)) => {
                    Some(caption_settings.merge(other_caption_settings))
                }
                _ => other.captions_settings.or(self.captions_settings),
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

impl CaptionsSettings {
    pub(crate) fn is_finite(&self) -> bool {
        self.font_style.map_or(true, |font_style| font_style.is_finite())
            && self.window_color.map_or(true, |window_color| window_color.is_finite())
            && self.background_color.map_or(true, |bkg_color| bkg_color.is_finite())
    }
}

impl Merge for CaptionsSettings {
    fn merge(&self, other: Self) -> Self {
        CaptionsSettings {
            for_media: other.for_media.or(self.for_media),
            for_tts: other.for_tts.or(self.for_tts),
            window_color: other.window_color.or(self.window_color),
            background_color: other.background_color.or(self.background_color),
            font_style: match (self.font_style, other.font_style) {
                (Some(style), Some(other_style)) => Some(style.merge(other_style)),
                _ => other.font_style.or(self.font_style),
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
        let mut settings = fidl_fuchsia_settings::CaptionsSettings::EMPTY;
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

impl CaptionFontStyle {
    pub(crate) fn is_finite(&self) -> bool {
        self.color.map_or(true, |color| color.is_finite())
            && self.relative_size.map_or(true, |size| size.is_finite())
    }
}

impl Merge for CaptionFontStyle {
    fn merge(&self, other: Self) -> Self {
        CaptionFontStyle {
            family: other.family.or(self.family),
            color: other.color.or(self.color),
            relative_size: other.relative_size.or(self.relative_size),
            char_edge_style: other.char_edge_style.or(self.char_edge_style),
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
        let mut style = fidl_fuchsia_settings::CaptionFontStyle::EMPTY;
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

impl ColorRgba {
    pub(crate) fn is_finite(&self) -> bool {
        self.red.is_finite()
            && self.green.is_finite()
            && self.blue.is_finite()
            && self.alpha.is_finite()
    }
}

impl From<fidl_fuchsia_ui_types::ColorRgba> for ColorRgba {
    fn from(src: fidl_fuchsia_ui_types::ColorRgba) -> Self {
        ColorRgba { red: src.red, green: src.green, blue: src.blue, alpha: src.alpha }
    }
}

impl From<ColorRgba> for fidl_fuchsia_ui_types::ColorRgba {
    fn from(src: ColorRgba) -> Self {
        fidl_fuchsia_ui_types::ColorRgba {
            red: src.red,
            green: src.green,
            blue: src.blue,
            alpha: src.alpha,
        }
    }
}

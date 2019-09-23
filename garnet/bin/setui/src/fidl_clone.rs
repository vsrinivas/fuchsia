// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_settings::*, fidl_fuchsia_setui::*};

/// A placeholder for real cloning support in FIDL generated Rust code.
/// TODO(QA-715): Remove
pub trait FIDLClone {
    fn clone(&self) -> Self;
}

impl FIDLClone for SettingData {
    fn clone(&self) -> SettingData {
        match self {
            SettingData::StringValue(val) => {
                return SettingData::StringValue(val.to_string());
            }
            SettingData::TimeZoneValue(val) => {
                return SettingData::TimeZoneValue(val.clone());
            }
            SettingData::Connectivity(val) => {
                return SettingData::Connectivity(val.clone());
            }
            SettingData::Intl(val) => {
                return SettingData::Intl(val.clone());
            }
            SettingData::Wireless(val) => {
                return SettingData::Wireless(val.clone());
            }
            SettingData::Account(val) => {
                return SettingData::Account(val.clone());
            }
        }
    }
}

impl FIDLClone for AccountSettings {
    fn clone(&self) -> Self {
        return AccountSettings { mode: self.mode };
    }
}

impl<T: FIDLClone> FIDLClone for Vec<T> {
    fn clone(&self) -> Self {
        return self.into_iter().map(FIDLClone::clone).collect();
    }
}

impl<T: FIDLClone> FIDLClone for Option<Box<T>> {
    fn clone(&self) -> Self {
        match self {
            None => None,
            Some(val) => Some(Box::new(val.as_ref().clone())),
        }
    }
}

impl FIDLClone for TimeZone {
    fn clone(&self) -> Self {
        return TimeZone {
            id: self.id.clone(),
            name: self.name.clone(),
            region: self.region.clone(),
        };
    }
}

impl FIDLClone for AccessibilitySettings {
    fn clone(&self) -> Self {
        let mut settings = AccessibilitySettings::empty();
        settings.audio_description = self.audio_description;
        settings.screen_reader = self.screen_reader;
        settings.color_inversion = self.color_inversion;
        settings.enable_magnification = self.enable_magnification;
        settings.color_correction = self.color_correction;
        settings.captions_settings = match &self.captions_settings {
            Some(setting) => Some(setting.clone()),
            None => None,
        };
        settings
    }
}

impl FIDLClone for CaptionsSettings {
    fn clone(&self) -> Self {
        let mut settings = CaptionsSettings::empty();
        settings.for_media = self.for_media;
        settings.for_tts = self.for_tts;
        settings.font_style = match &self.font_style {
            Some(style) => Some(style.clone()),
            None => None,
        };
        settings.window_color = self.window_color;
        settings.background_color = self.background_color;
        settings
    }
}

impl FIDLClone for CaptionFontStyle {
    fn clone(&self) -> Self {
        let mut style = CaptionFontStyle::empty();
        style.family = self.family;
        style.color = self.color;
        style.relative_size = self.relative_size;
        style.char_edge_style = self.char_edge_style;
        style
    }
}

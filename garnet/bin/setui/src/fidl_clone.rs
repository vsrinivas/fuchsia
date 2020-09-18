// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_sys::Peer, fidl_fuchsia_settings::*,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
};

/// A placeholder for real cloning support in FIDL generated Rust code.
/// TODO(fxb/37456): Remove
pub trait FIDLClone {
    fn clone(&self) -> Self;
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

impl FIDLClone for AudioSettings {
    fn clone(&self) -> Self {
        return AudioSettings {
            streams: Some(self.streams.as_ref().unwrap().clone()),
            input: Some(AudioInput { muted: self.input.as_ref().unwrap().muted }),
        };
    }
}

impl FIDLClone for AudioStreamSettings {
    fn clone(&self) -> Self {
        return AudioStreamSettings {
            stream: self.stream,
            source: self.source,
            user_volume: Some(Volume {
                level: self.user_volume.as_ref().unwrap().level,
                muted: self.user_volume.as_ref().unwrap().muted,
            }),
        };
    }
}

impl FIDLClone for InputDeviceSettings {
    fn clone(&self) -> Self {
        InputDeviceSettings {
            microphone: Some(Microphone { muted: self.microphone.as_ref().unwrap().muted }),
        }
    }
}

impl FIDLClone for Microphone {
    fn clone(&self) -> Self {
        Microphone { muted: self.muted }
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

impl FIDLClone for fidl_fuchsia_settings::IntlSettings {
    fn clone(&self) -> Self {
        let mut settings = fidl_fuchsia_settings::IntlSettings::empty();
        settings.locales = match &self.locales {
            Some(locales) => Some(locales.clone()),
            None => None,
        };
        settings.temperature_unit = self.temperature_unit;
        settings.time_zone_id = match &self.time_zone_id {
            Some(time_zone_id) => Some(time_zone_id.clone()),
            None => None,
        };
        settings.hour_cycle = self.hour_cycle;
        settings
    }
}

impl FIDLClone for LightGroup {
    fn clone(&self) -> Self {
        LightGroup {
            name: self.name.clone(),
            enabled: self.enabled,
            type_: self.type_,
            lights: self
                .lights
                .as_ref()
                .map(|l| l.iter().map(LightState::clone).collect::<Vec<_>>()),
        }
    }
}

impl FIDLClone for LightState {
    fn clone(&self) -> Self {
        LightState { value: self.value.as_ref().map(LightValue::clone) }
    }
}

impl FIDLClone for LightValue {
    fn clone(&self) -> Self {
        match self {
            LightValue::On(value) => LightValue::On(value.clone()),
            LightValue::Brightness(value) => LightValue::Brightness(value.clone()),
            LightValue::Color(value) => LightValue::Color(value.clone()),
        }
    }
}

impl FIDLClone for MediaButtonsEvent {
    fn clone(&self) -> Self {
        MediaButtonsEvent { volume: self.volume, mic_mute: self.mic_mute, pause: self.pause }
    }
}

impl FIDLClone for Peer {
    fn clone(&self) -> Self {
        return Peer {
            id: self.id.clone(),
            address: self.address.clone(),
            technology: self.technology,
            connected: self.connected,
            bonded: self.bonded,
            name: self.name.clone(),
            appearance: self.appearance,
            device_class: self.device_class.clone(),
            rssi: self.rssi,
            tx_power: self.tx_power,
            services: self.services.clone(),
            le_services: self.le_services.clone(),
            bredr_services: self.bredr_services.clone(),
        };
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use {
    bt_avctp::pub_decodable_enum, fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_avrcp as fidl_avrcp, std::convert::TryFrom,
};

pub mod get_attribute_text;
pub mod get_current_settings;
pub mod get_value_text;
pub mod list_settings;
pub mod set_current_settings;

pub use self::{
    get_attribute_text::*, get_current_settings::*, get_value_text::*, list_settings::*,
    set_current_settings::*,
};
use crate::packets::Error;

pub_decodable_enum!(
    PlayerApplicationSettingAttributeId <u8, Error, InvalidParameter> {
        Equalizer => 0x01,
        RepeatStatusMode => 0x02,
        ShuffleMode => 0x03,
        ScanMode => 0x04,
    }
);

impl From<fidl_avrcp::PlayerApplicationSettingAttributeId> for PlayerApplicationSettingAttributeId {
    fn from(
        src: fidl_avrcp::PlayerApplicationSettingAttributeId,
    ) -> PlayerApplicationSettingAttributeId {
        match src {
            fidl_avrcp::PlayerApplicationSettingAttributeId::Equalizer => {
                PlayerApplicationSettingAttributeId::Equalizer
            }
            fidl_avrcp::PlayerApplicationSettingAttributeId::RepeatStatusMode => {
                PlayerApplicationSettingAttributeId::RepeatStatusMode
            }
            fidl_avrcp::PlayerApplicationSettingAttributeId::ShuffleMode => {
                PlayerApplicationSettingAttributeId::ShuffleMode
            }
            fidl_avrcp::PlayerApplicationSettingAttributeId::ScanMode => {
                PlayerApplicationSettingAttributeId::ScanMode
            }
        }
    }
}

impl From<PlayerApplicationSettingAttributeId> for fidl_avrcp::PlayerApplicationSettingAttributeId {
    fn from(
        src: PlayerApplicationSettingAttributeId,
    ) -> fidl_avrcp::PlayerApplicationSettingAttributeId {
        match src {
            PlayerApplicationSettingAttributeId::Equalizer => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::Equalizer
            }
            PlayerApplicationSettingAttributeId::RepeatStatusMode => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::RepeatStatusMode
            }
            PlayerApplicationSettingAttributeId::ShuffleMode => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::ShuffleMode
            }
            PlayerApplicationSettingAttributeId::ScanMode => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::ScanMode
            }
        }
    }
}

pub_decodable_enum!(
    RepeatStatusMode <u8, Error, OutOfRange> {
        Off => 0x01,
        SingleTrackRepeat => 0x02,
        AllTrackRepeat => 0x03,
        GroupRepeat => 0x04,
    }
);

impl From<fidl_avrcp::RepeatStatusMode> for RepeatStatusMode {
    fn from(src: fidl_avrcp::RepeatStatusMode) -> RepeatStatusMode {
        match src {
            fidl_avrcp::RepeatStatusMode::Off => RepeatStatusMode::Off,
            fidl_avrcp::RepeatStatusMode::SingleTrackRepeat => RepeatStatusMode::SingleTrackRepeat,
            fidl_avrcp::RepeatStatusMode::AllTrackRepeat => RepeatStatusMode::AllTrackRepeat,
            fidl_avrcp::RepeatStatusMode::GroupRepeat => RepeatStatusMode::GroupRepeat,
        }
    }
}

impl From<RepeatStatusMode> for fidl_avrcp::RepeatStatusMode {
    fn from(src: RepeatStatusMode) -> fidl_avrcp::RepeatStatusMode {
        match src {
            RepeatStatusMode::Off => fidl_avrcp::RepeatStatusMode::Off,
            RepeatStatusMode::SingleTrackRepeat => fidl_avrcp::RepeatStatusMode::SingleTrackRepeat,
            RepeatStatusMode::AllTrackRepeat => fidl_avrcp::RepeatStatusMode::AllTrackRepeat,
            RepeatStatusMode::GroupRepeat => fidl_avrcp::RepeatStatusMode::GroupRepeat,
        }
    }
}

pub_decodable_enum!(
    ShuffleMode <u8, Error, OutOfRange> {
        Off => 0x01,
        AllTrackShuffle => 0x02,
        GroupShuffle => 0x03,
    }
);

impl From<fidl_avrcp::ShuffleMode> for ShuffleMode {
    fn from(src: fidl_avrcp::ShuffleMode) -> ShuffleMode {
        match src {
            fidl_avrcp::ShuffleMode::Off => ShuffleMode::Off,
            fidl_avrcp::ShuffleMode::AllTrackShuffle => ShuffleMode::AllTrackShuffle,
            fidl_avrcp::ShuffleMode::GroupShuffle => ShuffleMode::GroupShuffle,
        }
    }
}

impl From<ShuffleMode> for fidl_avrcp::ShuffleMode {
    fn from(src: ShuffleMode) -> fidl_avrcp::ShuffleMode {
        match src {
            ShuffleMode::Off => fidl_avrcp::ShuffleMode::Off,
            ShuffleMode::AllTrackShuffle => fidl_avrcp::ShuffleMode::AllTrackShuffle,
            ShuffleMode::GroupShuffle => fidl_avrcp::ShuffleMode::GroupShuffle,
        }
    }
}

pub_decodable_enum!(
    ScanMode <u8, Error, OutOfRange> {
        Off => 0x01,
        AllTrackScan => 0x02,
        GroupScan => 0x03,
    }
);

impl From<fidl_avrcp::ScanMode> for ScanMode {
    fn from(src: fidl_avrcp::ScanMode) -> ScanMode {
        match src {
            fidl_avrcp::ScanMode::Off => ScanMode::Off,
            fidl_avrcp::ScanMode::AllTrackScan => ScanMode::AllTrackScan,
            fidl_avrcp::ScanMode::GroupScan => ScanMode::GroupScan,
        }
    }
}

impl From<ScanMode> for fidl_avrcp::ScanMode {
    fn from(src: ScanMode) -> fidl_avrcp::ScanMode {
        match src {
            ScanMode::Off => fidl_avrcp::ScanMode::Off,
            ScanMode::AllTrackScan => fidl_avrcp::ScanMode::AllTrackScan,
            ScanMode::GroupScan => fidl_avrcp::ScanMode::GroupScan,
        }
    }
}

pub_decodable_enum!(
    Equalizer <u8, Error, OutOfRange> {
        Off => 0x01,
        On => 0x02,
    }
);

impl From<fidl_avrcp::Equalizer> for Equalizer {
    fn from(src: fidl_avrcp::Equalizer) -> Equalizer {
        match src {
            fidl_avrcp::Equalizer::Off => Equalizer::Off,
            fidl_avrcp::Equalizer::On => Equalizer::On,
        }
    }
}

impl From<Equalizer> for fidl_avrcp::Equalizer {
    fn from(src: Equalizer) -> fidl_avrcp::Equalizer {
        match src {
            Equalizer::Off => fidl_avrcp::Equalizer::Off,
            Equalizer::On => fidl_avrcp::Equalizer::On,
        }
    }
}

// TODO(fxbug.dev/41253): Add support to handle custom attributes.
#[derive(Clone, Debug)]
pub struct PlayerApplicationSettings {
    pub equalizer: Option<Equalizer>,
    pub repeat_status_mode: Option<RepeatStatusMode>,
    pub shuffle_mode: Option<ShuffleMode>,
    pub scan_mode: Option<ScanMode>,
}

impl PlayerApplicationSettings {
    pub fn new(
        equalizer: Option<Equalizer>,
        repeat_status_mode: Option<RepeatStatusMode>,
        shuffle_mode: Option<ShuffleMode>,
        scan_mode: Option<ScanMode>,
    ) -> Self {
        Self { equalizer, repeat_status_mode, shuffle_mode, scan_mode }
    }

    // Given an attribute specified by `attribute_id`, sets the field to None.
    pub fn clear_attribute(&mut self, attribute_id: PlayerApplicationSettingAttributeId) {
        match attribute_id {
            PlayerApplicationSettingAttributeId::Equalizer => self.equalizer = None,
            PlayerApplicationSettingAttributeId::ScanMode => self.scan_mode = None,
            PlayerApplicationSettingAttributeId::RepeatStatusMode => self.repeat_status_mode = None,
            PlayerApplicationSettingAttributeId::ShuffleMode => self.shuffle_mode = None,
        };
    }
}

impl From<&fidl_avrcp::PlayerApplicationSettings> for PlayerApplicationSettings {
    fn from(src: &fidl_avrcp::PlayerApplicationSettings) -> PlayerApplicationSettings {
        let mut equalizer: Option<Equalizer> = None;
        let mut repeat_status_mode: Option<RepeatStatusMode> = None;
        let mut shuffle_mode: Option<ShuffleMode> = None;
        let mut scan_mode: Option<ScanMode> = None;

        if let Some(eq) = src.equalizer {
            equalizer = Some(eq.into());
        }
        if let Some(rsm) = src.repeat_status_mode {
            repeat_status_mode = Some(rsm.into());
        }
        if let Some(shm) = src.shuffle_mode {
            shuffle_mode = Some(shm.into());
        }
        if let Some(scm) = src.scan_mode {
            scan_mode = Some(scm.into());
        }

        PlayerApplicationSettings::new(equalizer, repeat_status_mode, shuffle_mode, scan_mode)
    }
}

impl From<PlayerApplicationSettings> for fidl_avrcp::PlayerApplicationSettings {
    fn from(src: PlayerApplicationSettings) -> fidl_avrcp::PlayerApplicationSettings {
        let mut setting = fidl_avrcp::PlayerApplicationSettings::new_empty();
        if let Some(eq) = src.equalizer {
            setting.equalizer = Some(eq.into());
        }
        if let Some(rsm) = src.repeat_status_mode {
            setting.repeat_status_mode = Some(rsm.into());
        }
        if let Some(shm) = src.shuffle_mode {
            setting.shuffle_mode = Some(shm.into());
        }
        if let Some(scm) = src.scan_mode {
            setting.scan_mode = Some(scm.into());
        }

        setting
    }
}

/// Takes a PlayerApplicationSettings and returns a vector of (attribute_id, value).
// TODO(fxbug.dev/41253): Add support to handle custom attributes.
pub fn settings_to_vec(
    settings: &PlayerApplicationSettings,
) -> Vec<(PlayerApplicationSettingAttributeId, u8)> {
    let mut attribute_id_vals = vec![];
    if let Some(equalizer) = settings.equalizer {
        let value: Equalizer = equalizer.into();
        attribute_id_vals.push((PlayerApplicationSettingAttributeId::Equalizer, u8::from(&value)));
    }
    if let Some(repeat_status_mode) = settings.repeat_status_mode {
        let value: RepeatStatusMode = repeat_status_mode.into();
        attribute_id_vals
            .push((PlayerApplicationSettingAttributeId::RepeatStatusMode, u8::from(&value)));
    }
    if let Some(shuffle_mode) = settings.shuffle_mode {
        let value: ShuffleMode = shuffle_mode.into();
        attribute_id_vals
            .push((PlayerApplicationSettingAttributeId::ShuffleMode, u8::from(&value)));
    }
    if let Some(scan_mode) = settings.scan_mode {
        let value: ScanMode = scan_mode.into();
        attribute_id_vals.push((PlayerApplicationSettingAttributeId::ScanMode, u8::from(&value)));
    }

    attribute_id_vals
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_avrcp::{self as fidl_avrcp, MediaAttributes, PlayStatus},
    fidl_fuchsia_media::{self as fidl_media_types, Metadata, TimelineFunction},
    fidl_fuchsia_media_sessions2::{self as fidl_media},
    fidl_table_validation::ValidFidlTable,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    std::convert::TryInto,
};

/// Converts time (i64, in nanoseconds) to milliseconds (u32).
fn time_nanos_to_millis(t: i64) -> u32 {
    zx::Duration::from_nanos(t).into_millis() as u32
}

/// Returns the current position of playing media in milliseconds.
/// Using formula defined in: fuchsia.media.TimelineFunction.
/// s = (r - reference_time) * (subject_delta / reference_delta) + subject_time
/// where `s` = song_position, `r` = current time.
/// If the `reference_delta` in `t` is 0, this violates the `fuchsia.media.TimelineFunction`
/// contract.
fn media_timeline_fn_to_position(t: TimelineFunction, current_time: i64) -> Option<u32> {
    let diff = current_time - t.reference_time;
    if diff.is_negative() {
        fx_log_warn!("Current time is before reference time. This is not possible.");
        return None;
    }

    let ratio = if t.reference_delta == 0 {
        fx_log_warn!("Reference delta is zero. Violation of TimelineFunction API.");
        return None;
    } else {
        (t.subject_delta / t.reference_delta) as i64
    };

    let position_nanos = diff * ratio + t.subject_time;
    Some(time_nanos_to_millis(position_nanos))
}

pub fn media_repeat_mode_to_avrcp(
    src: fidl_media::RepeatMode,
) -> Option<fidl_avrcp::RepeatStatusMode> {
    match src {
        fidl_media::RepeatMode::Off => Some(fidl_avrcp::RepeatStatusMode::Off),
        fidl_media::RepeatMode::Group => Some(fidl_avrcp::RepeatStatusMode::GroupRepeat),
        fidl_media::RepeatMode::Single => Some(fidl_avrcp::RepeatStatusMode::SingleTrackRepeat),
    }
}

pub fn avrcp_repeat_mode_to_media(src: fidl_avrcp::RepeatStatusMode) -> fidl_media::RepeatMode {
    match src {
        fidl_avrcp::RepeatStatusMode::SingleTrackRepeat => fidl_media::RepeatMode::Single,
        fidl_avrcp::RepeatStatusMode::GroupRepeat
        | fidl_avrcp::RepeatStatusMode::AllTrackRepeat => fidl_media::RepeatMode::Group,
        _ => fidl_media::RepeatMode::Off,
    }
}

pub fn media_shuffle_mode_to_avrcp(src: bool) -> Option<fidl_avrcp::ShuffleMode> {
    if src {
        Some(fidl_avrcp::ShuffleMode::AllTrackShuffle)
    } else {
        Some(fidl_avrcp::ShuffleMode::Off)
    }
}

pub fn avrcp_shuffle_mode_to_media(src: fidl_avrcp::ShuffleMode) -> bool {
    match src {
        fidl_avrcp::ShuffleMode::AllTrackShuffle | fidl_avrcp::ShuffleMode::GroupShuffle => true,
        _ => false,
    }
}

pub fn media_player_state_to_playback_status(
    src: fidl_media::PlayerState,
) -> Option<fidl_avrcp::PlaybackStatus> {
    match src {
        fidl_media::PlayerState::Idle => Some(fidl_avrcp::PlaybackStatus::Stopped),
        fidl_media::PlayerState::Playing => Some(fidl_avrcp::PlaybackStatus::Playing),
        fidl_media::PlayerState::Paused | fidl_media::PlayerState::Buffering => {
            Some(fidl_avrcp::PlaybackStatus::Paused)
        }
        fidl_media::PlayerState::Error => Some(fidl_avrcp::PlaybackStatus::Error),
    }
}

#[derive(Clone, Debug, Default, PartialEq, ValidFidlTable)]
#[fidl_table_src(PlayStatus)]
pub(crate) struct ValidPlayStatus {
    #[fidl_field_type(optional)]
    /// The length of media in millis.
    song_length: Option<u32>,
    #[fidl_field_type(optional)]
    /// The current position of media in millis.
    song_position: Option<u32>,
    #[fidl_field_type(optional)]
    /// The current playback status of media.
    playback_status: Option<fidl_avrcp::PlaybackStatus>,
}

impl ValidPlayStatus {
    #[allow(unused)]
    pub(crate) fn new(
        song_length: Option<u32>,
        song_position: Option<u32>,
        playback_status: Option<fidl_avrcp::PlaybackStatus>,
    ) -> Self {
        Self { song_length, song_position, playback_status }
    }

    /// Return the current playback position of currently playing media.
    /// If there is no currently playing media (i.e `song_position` is None),
    /// return u32 MAX, as defined in AVRCP 1.6.2 Section 6.7.2, Table 6.35.
    pub fn get_playback_position(&self) -> u32 {
        self.song_position.map_or(std::u32::MAX, |p| p)
    }

    /// Return the current playback status of currently playing media.
    /// If there is no currently playing media (i.e `playback_status` is None),
    /// return PlaybackStatus::Stopped, as this is mandatory.
    /// See AVRCP 1.6.2 Section 6.7.1.
    pub fn get_playback_status(&self) -> fidl_avrcp::PlaybackStatus {
        self.playback_status.map_or(fidl_avrcp::PlaybackStatus::Stopped, |s| s)
    }

    /// Update the play status from updates from Media.
    /// `duration` is in nanoseconds -> convert to millis.
    pub(crate) fn update_play_status(
        &mut self,
        duration: Option<i64>,
        timeline_fn: Option<TimelineFunction>,
        player_state: Option<fidl_media::PlayerState>,
    ) {
        self.song_length = duration.map(|d| time_nanos_to_millis(d));
        self.song_position = timeline_fn
            .and_then(|f| media_timeline_fn_to_position(f, fasync::Time::now().into_nanos()));
        self.playback_status =
            player_state.and_then(|state| media_player_state_to_playback_status(state));
    }
}

/// The PlayerApplicationSettings for the MediaSession.
/// Currently, only `repeat_status_mode` and `shuffle_mode` are supported by
/// Players in Media.
/// `equalizer` and `scan_mode` are present for correctness in mapping to AVRCP,
/// but are not used or set.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ValidPlayerApplicationSettings {
    equalizer: Option<fidl_avrcp::Equalizer>,
    repeat_status_mode: Option<fidl_avrcp::RepeatStatusMode>,
    shuffle_mode: Option<fidl_avrcp::ShuffleMode>,
    scan_mode: Option<fidl_avrcp::ScanMode>,
    // TODO(41253): Add support to handle custom attributes.
}

impl ValidPlayerApplicationSettings {
    pub fn new(
        equalizer: Option<fidl_avrcp::Equalizer>,
        repeat_status_mode: Option<fidl_avrcp::RepeatStatusMode>,
        shuffle_mode: Option<fidl_avrcp::ShuffleMode>,
        scan_mode: Option<fidl_avrcp::ScanMode>,
    ) -> Self {
        Self { equalizer, repeat_status_mode, shuffle_mode, scan_mode }
    }

    pub fn repeat_status_mode(&self) -> Option<fidl_avrcp::RepeatStatusMode> {
        self.repeat_status_mode
    }

    pub fn shuffle_mode(&self) -> Option<fidl_avrcp::ShuffleMode> {
        self.shuffle_mode
    }

    /// Return the current RepeatStatusMode.
    /// If it is not set, default to OFF.
    pub fn get_repeat_status_mode(&self) -> fidl_avrcp::RepeatStatusMode {
        self.repeat_status_mode.map_or(fidl_avrcp::RepeatStatusMode::Off, |s| s)
    }

    /// Return the current ShuffleMode.
    /// If it is not set, default to OFF.
    pub fn get_shuffle_mode(&self) -> fidl_avrcp::ShuffleMode {
        self.shuffle_mode.map_or(fidl_avrcp::ShuffleMode::Off, |s| s)
    }

    /// Given an attribute specified by `attribute_id`, sets the field to None.
    pub fn clear_attribute(
        &mut self,
        attribute_id: fidl_avrcp::PlayerApplicationSettingAttributeId,
    ) {
        use fidl_avrcp::PlayerApplicationSettingAttributeId as avrcp_id;
        match attribute_id {
            avrcp_id::Equalizer => self.equalizer = None,
            avrcp_id::ScanMode => self.scan_mode = None,
            avrcp_id::RepeatStatusMode => self.repeat_status_mode = None,
            avrcp_id::ShuffleMode => self.shuffle_mode = None,
        };
    }

    /// Returns true if the player application settings contains unsupported settings.
    pub fn unsupported_settings_set(&self) -> bool {
        self.equalizer.is_some() || self.scan_mode.is_some()
    }

    /// Update the settings, if the settings are present. Otherwise, ignore the update.
    pub fn update_player_application_settings(
        &mut self,
        repeat_mode: Option<fidl_media::RepeatMode>,
        shuffle_on: Option<bool>,
    ) {
        if let Some(repeat) = repeat_mode {
            self.update_repeat_status_mode(repeat);
        }
        if let Some(shuffle) = shuffle_on {
            self.update_shuffle_mode(shuffle);
        }
    }

    /// Update the `repeat_status_mode` from a change in MediaPlayer.
    pub fn update_repeat_status_mode(&mut self, repeat_mode: fidl_media::RepeatMode) {
        self.repeat_status_mode = media_repeat_mode_to_avrcp(repeat_mode);
    }

    /// Update the `shuffle_mode` from a change in MediaPlayer.
    pub fn update_shuffle_mode(&mut self, shuffle_mode: bool) {
        self.shuffle_mode = media_shuffle_mode_to_avrcp(shuffle_mode);
    }

    /// Sets the `repeat_status_mode`.
    pub fn set_repeat_status_mode(&mut self, status: Option<fidl_avrcp::RepeatStatusMode>) {
        self.repeat_status_mode = status;
    }

    /// Sets the `shuffle_mode`.
    pub fn set_shuffle_mode(&mut self, status: Option<fidl_avrcp::ShuffleMode>) {
        self.shuffle_mode = status;
    }
}

impl From<fidl_avrcp::PlayerApplicationSettings> for ValidPlayerApplicationSettings {
    fn from(src: fidl_avrcp::PlayerApplicationSettings) -> ValidPlayerApplicationSettings {
        ValidPlayerApplicationSettings::new(
            src.equalizer.map(|v| v.into()),
            src.repeat_status_mode.map(|v| v.into()),
            src.shuffle_mode.map(|v| v.into()),
            src.scan_mode.map(|v| v.into()),
        )
    }
}

impl From<ValidPlayerApplicationSettings> for fidl_avrcp::PlayerApplicationSettings {
    fn from(src: ValidPlayerApplicationSettings) -> fidl_avrcp::PlayerApplicationSettings {
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

#[derive(Clone, Debug, Default, PartialEq, ValidFidlTable)]
#[fidl_table_src(MediaAttributes)]
pub struct MediaInfo {
    #[fidl_field_type(optional)]
    title: Option<String>,
    #[fidl_field_type(optional)]
    artist_name: Option<String>,
    #[fidl_field_type(optional)]
    album_name: Option<String>,
    #[fidl_field_type(optional)]
    track_number: Option<String>,
    #[fidl_field_type(optional)]
    genre: Option<String>,
    #[fidl_field_type(optional)]
    total_number_of_tracks: Option<String>,
    #[fidl_field_type(optional)]
    playing_time: Option<String>,
}

impl MediaInfo {
    pub fn new(
        title: Option<String>,
        artist_name: Option<String>,
        album_name: Option<String>,
        track_number: Option<String>,
        genre: Option<String>,
        total_number_of_tracks: Option<String>,
        playing_time: Option<String>,
    ) -> Self {
        Self {
            title,
            artist_name,
            album_name,
            track_number,
            genre,
            total_number_of_tracks,
            playing_time,
        }
    }

    /// Returns 0x0 if there is metadata present, implying there is currently set media.
    /// Otherwise returns u64 MAX.
    /// The track ID value is defined in AVRCP 1.6.2 Section 6.7.2, Table 6.32.
    pub fn get_track_id(&self) -> u64 {
        if self.title.is_some()
            || self.artist_name.is_some()
            || self.album_name.is_some()
            || self.track_number.is_some()
            || self.genre.is_some()
        {
            0
        } else {
            std::u64::MAX
        }
    }

    /// Updates information about currently playing media.
    /// If a metadata update is present, this implies the current media has changed.
    /// In the event that metadata is present, but duration is not, `playing_time`
    /// will default to None. See `update_playing_time()`.
    ///
    /// If no metadata is present, but a duration is present, update only `playing_time`.
    /// The rationale here is a use-case such as a live stream, where the playing time
    /// may be constantly changing, but stream metadata remains constant.
    pub fn update_media_info(
        &mut self,
        media_duration: Option<i64>,
        media_metadata: Option<Metadata>,
    ) {
        if let Some(metadata) = media_metadata {
            self.update_metadata(metadata);
        }
        self.update_playing_time(media_duration);
    }

    /// Take the duration of media, in nanos, and store as duration in millis as a string.
    /// If no duration is present, `playing_time` will be set to None.
    fn update_playing_time(&mut self, duration: Option<i64>) {
        self.playing_time = duration.map(|d| time_nanos_to_millis(d).to_string());
    }

    fn update_metadata(&mut self, metadata: Metadata) {
        for property in metadata.properties {
            match property.label.as_str() {
                fidl_media_types::METADATA_LABEL_TITLE => {
                    self.title = Some(property.value);
                }
                fidl_media_types::METADATA_LABEL_ARTIST => {
                    self.artist_name = Some(property.value);
                }
                fidl_media_types::METADATA_LABEL_ALBUM => {
                    self.album_name = Some(property.value);
                }
                fidl_media_types::METADATA_LABEL_TRACK_NUMBER => {
                    self.track_number = Some(property.value);
                }
                fidl_media_types::METADATA_LABEL_GENRE => {
                    self.genre = Some(property.value);
                }
                _ => {
                    fx_vlog!(tag: "avrcp-tg", 1, "Media metadata {:?} variant not supported.", property.label);
                }
            }
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub(crate) struct Notification {
    pub status: Option<fidl_avrcp::PlaybackStatus>,
    pub track_id: Option<u64>,
    pub pos: Option<u32>,
    // BatteryStatus
    // SystemStatus
    pub application_settings: Option<ValidPlayerApplicationSettings>,
    pub player_id: Option<u16>,
    pub volume: Option<u8>,
    pub device_connected: Option<bool>,
}

impl Notification {
    pub fn new(
        status: Option<fidl_avrcp::PlaybackStatus>,
        track_id: Option<u64>,
        pos: Option<u32>,
        application_settings: Option<ValidPlayerApplicationSettings>,
        player_id: Option<u16>,
        volume: Option<u8>,
        device_connected: Option<bool>,
    ) -> Self {
        Self { status, track_id, pos, application_settings, player_id, volume, device_connected }
    }
}

impl From<fidl_avrcp::Notification> for Notification {
    fn from(src: fidl_avrcp::Notification) -> Notification {
        Notification::new(
            src.status.map(|s| s.into()),
            src.track_id,
            src.pos,
            src.application_settings.map(|s| s.try_into().expect("Couldn't convert PAS")),
            src.player_id,
            src.volume,
            src.device_connected,
        )
    }
}

impl From<Notification> for fidl_avrcp::Notification {
    fn from(src: Notification) -> fidl_avrcp::Notification {
        let mut res = fidl_avrcp::Notification::new_empty();

        res.status = src.status.map(|s| s.into());
        res.track_id = src.track_id;
        res.pos = src.pos;
        res.application_settings = src.application_settings.map(|s| s.into());
        res.player_id = src.player_id;
        res.volume = src.volume;
        res.device_connected = src.device_connected;
        res
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::encoding::Decodable as FidlDecodable,
        fidl_fuchsia_bluetooth_avrcp::{self as fidl_avrcp},
        fidl_fuchsia_media::{self as fidl_media_types},
        fidl_fuchsia_media_sessions2::{self as fidl_media},
    };

    #[test]
    /// Tests correctness of updating the `playing_time` field in MediaInfo.
    fn test_media_info_update_playing_time() {
        let mut info: MediaInfo = Default::default();
        assert_eq!(info.get_track_id(), std::u64::MAX);

        // Duration (in nanos), roughly 12 milliseconds.
        let duration = Some(12345678);
        let expected_duration = 12;
        info.update_playing_time(duration);
        assert_eq!(Some(expected_duration.to_string()), info.playing_time);
        assert_eq!(std::u64::MAX, info.get_track_id());
    }

    #[test]
    /// Tests correctness of updating media-related metadata.
    fn test_media_info_update_metadata() {
        let mut info: MediaInfo = Default::default();

        let mut metadata = fidl_media_types::Metadata::new_empty();
        let mut property1 = fidl_media_types::Property::new_empty();
        property1.label = fidl_media_types::METADATA_LABEL_TITLE.to_string();
        let sample_title = "This is a sample title".to_string();
        property1.value = sample_title.clone();
        let mut property2 = fidl_media_types::Property::new_empty();
        property2.label = fidl_media_types::METADATA_LABEL_GENRE.to_string();
        let sample_genre = "Pop".to_string();
        property2.value = sample_genre.clone();
        // Unsupported piece of metadata, should be ignored.
        let mut property3 = fidl_media_types::Property::new_empty();
        property3.label = fidl_media_types::METADATA_LABEL_COMPOSER.to_string();
        property3.value = "Bach".to_string();

        metadata.properties = vec![property1, property2, property3];
        info.update_metadata(metadata);
        assert_eq!(Some(sample_title), info.title);
        assert_eq!(Some(sample_genre), info.genre);
        assert_eq!(0, info.get_track_id());
    }

    #[test]
    /// Tests updating media_info with `metadata` but no `duration` defaults `duration` = None.
    fn test_media_info_update_metadata_no_duration() {
        let mut info: MediaInfo = Default::default();

        let mut metadata = fidl_media_types::Metadata::new_empty();
        let mut property1 = fidl_media_types::Property::new_empty();
        property1.label = fidl_media_types::METADATA_LABEL_TITLE.to_string();
        let sample_title = "Foobar".to_string();
        property1.value = sample_title.clone();
        metadata.properties = vec![property1];

        info.update_media_info(None, Some(metadata));
        assert_eq!(None, info.playing_time);
        assert_eq!(Some(sample_title), info.title);
        assert_eq!(0, info.get_track_id());
    }

    #[test]
    /// Tests updating media_info with no metadata updates preserves original values, but
    /// overwrites `playing_time` since duration is not a top-level SessionInfoDelta update.
    /// Tests correctness of conversion to `fidl_avrcp::MediaAttributes` type.
    fn test_media_info_update_no_metadata() {
        let mut info: MediaInfo = Default::default();
        let duration = Some(22345678);

        // Create original state (metadata and random duration).
        let mut metadata = fidl_media_types::Metadata::new_empty();
        let mut property1 = fidl_media_types::Property::new_empty();
        property1.label = fidl_media_types::METADATA_LABEL_TITLE.to_string();
        let sample_title = "Foobar".to_string();
        property1.value = sample_title.clone();
        metadata.properties = vec![property1];
        info.update_media_info(None, Some(metadata));
        info.update_playing_time(duration);

        // Metadata should be preserved, except `playing_time`.
        info.update_media_info(None, None);
        assert_eq!(None, info.playing_time);
        assert_eq!(Some(sample_title.clone()), info.title);

        let info_fidl: fidl_avrcp::MediaAttributes = info.into();
        assert_eq!(Some(sample_title), info_fidl.title);
        assert_eq!(None, info_fidl.artist_name);
        assert_eq!(None, info_fidl.album_name);
        assert_eq!(None, info_fidl.track_number);
        assert_eq!(None, info_fidl.total_number_of_tracks);
        assert_eq!(None, info_fidl.playing_time);
    }

    #[test]
    /// Creates ValidPlayerApplicationSettings.
    /// Tests correctness of updating `repeat_status_mode` and `shuffle_mode`.
    /// Tests updating the settings with no updates preserves original values.
    /// Tests correctness of conversion to `fidl_avrcp::PlayerApplicationSettings` type.
    /// Tests correctness of clearing a field.
    fn test_player_application_settings() {
        let mut settings = ValidPlayerApplicationSettings::new(None, None, None, None);
        assert_eq!(settings.unsupported_settings_set(), false);

        let repeat_mode = Some(fidl_media::RepeatMode::Group);
        let shuffle_mode = Some(true);
        settings.update_player_application_settings(repeat_mode, shuffle_mode);

        let expected_repeat_mode = Some(fidl_avrcp::RepeatStatusMode::GroupRepeat);
        let expected_shuffle_mode = Some(fidl_avrcp::ShuffleMode::AllTrackShuffle);
        assert_eq!(expected_repeat_mode, settings.repeat_status_mode);
        assert_eq!(expected_shuffle_mode, settings.shuffle_mode);
        assert_eq!(settings.unsupported_settings_set(), false);

        settings.update_player_application_settings(None, None);
        assert_eq!(expected_repeat_mode, settings.repeat_status_mode);
        assert_eq!(expected_shuffle_mode, settings.shuffle_mode);

        let settings_fidl: fidl_avrcp::PlayerApplicationSettings = settings.clone().into();
        assert_eq!(
            Some(fidl_avrcp::RepeatStatusMode::GroupRepeat),
            settings_fidl.repeat_status_mode
        );
        assert_eq!(Some(fidl_avrcp::ShuffleMode::AllTrackShuffle), settings_fidl.shuffle_mode);
        assert_eq!(None, settings_fidl.equalizer);
        assert_eq!(None, settings_fidl.scan_mode);

        settings.clear_attribute(fidl_avrcp::PlayerApplicationSettingAttributeId::ShuffleMode);
        assert_eq!(None, settings.shuffle_mode);
    }

    #[test]
    /// Creates PlayStatus.
    /// Tests correctness of updating with duration, timeline_fn, and player_state from Media.
    /// Tests correctness of conversion to fidl_avrcp::PlayStatus.
    fn test_play_status() {
        let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(900000000));

        let mut play_status: ValidPlayStatus = Default::default();
        assert_eq!(play_status.get_playback_position(), std::u32::MAX);
        assert_eq!(play_status.get_playback_status(), fidl_avrcp::PlaybackStatus::Stopped);

        let player_state = Some(fidl_media::PlayerState::Buffering);
        play_status.update_play_status(None, None, player_state);

        let expected_player_state = Some(fidl_avrcp::PlaybackStatus::Paused);
        assert_eq!(None, play_status.song_length);
        assert_eq!(None, play_status.song_position);
        assert_eq!(expected_player_state, play_status.playback_status);
        assert_eq!(play_status.get_playback_position(), std::u32::MAX);
        assert_eq!(play_status.get_playback_status(), fidl_avrcp::PlaybackStatus::Paused);

        let duration = Some(9876543210); // nanos
        let mut timeline_fn = fidl_media_types::TimelineFunction::new_empty();
        timeline_fn.subject_time = 1000; // nanos
        timeline_fn.subject_delta = 1;
        timeline_fn.reference_delta = 1;
        timeline_fn.reference_time = 800000000; // nanos
        let player_state = Some(fidl_media::PlayerState::Idle);
        play_status.update_play_status(duration, Some(timeline_fn), player_state);

        let expected_song_length = Some(9876); // millis
        let expected_song_position = Some(100); // (100000000 + 1000) / 10^6
        let expected_player_state = Some(fidl_avrcp::PlaybackStatus::Stopped);
        assert_eq!(expected_song_length, play_status.song_length);
        assert_eq!(expected_song_position, play_status.song_position);
        assert_eq!(expected_player_state, play_status.playback_status);
        assert_eq!(play_status.get_playback_position(), 100);
        assert_eq!(play_status.get_playback_status(), fidl_avrcp::PlaybackStatus::Stopped);

        let play_status_fidl: fidl_avrcp::PlayStatus = play_status.clone().into();
        assert_eq!(expected_song_length, play_status_fidl.song_length);
        assert_eq!(Some(fidl_avrcp::PlaybackStatus::Stopped), play_status_fidl.playback_status);
    }

    #[test]
    /// Tests the timeline function to song_position conversion.
    /// 1. Normal case with media playing.
    /// 2. Normal case with media paused.
    /// 3. Error case with invalid timeline function -> should return None.
    /// 4. Normal case with media in fast forwarding.
    /// 5. Error case where current time is less than reference time. This means we
    /// went back in time!
    fn test_timeline_fn_to_song_position() {
        // 1. Normal case, media is playing.
        let mut timeline_fn = fidl_media_types::TimelineFunction::new_empty();
        // Playback started at beginning of media.
        timeline_fn.subject_time = 0;
        // Monotonic clock time at beginning of media (nanos).
        timeline_fn.reference_time = 500000000;
        // Playback rate = 1, normal playback.
        timeline_fn.subject_delta = 1;
        timeline_fn.reference_delta = 1;
        // Current time of the system (nanos).
        let curr_time = 520060095;
        let expected_position = 20; // 20060095 / 1000000 = 520millis

        let song_position = media_timeline_fn_to_position(timeline_fn, curr_time);
        assert!(song_position.is_some());
        assert_eq!(song_position.unwrap(), expected_position);

        // 2. Normal case, media is paused.
        timeline_fn = fidl_media_types::TimelineFunction::new_empty();
        // Playback started at some random time.
        timeline_fn.subject_time = 534912992;
        // Monotonic clock time at beginning of media.
        timeline_fn.reference_time = 500000000;
        // Playback rate = 0, it's paused.
        timeline_fn.subject_delta = 0;
        timeline_fn.reference_delta = 1;
        // Current time of the system.
        let curr_time = 500060095;
        // The expected position of media should be when it was started (subject_time), since
        // it's paused.
        let expected_position = 534; // 534973087 / 1000000 = 534 millis.

        let song_position = media_timeline_fn_to_position(timeline_fn, curr_time);
        assert!(song_position.is_some());
        assert_eq!(song_position.unwrap(), expected_position);

        // 3. Invalid case, `reference_delta` = 0, which violates the MediaSession contract.
        timeline_fn = fidl_media_types::TimelineFunction::new_empty();
        // Playback started at some random time.
        timeline_fn.subject_time = 534912992;
        // Monotonic clock time at beginning of media.
        timeline_fn.reference_time = 500000000;
        // Playback rate = 0, it's paused.
        timeline_fn.subject_delta = 0;
        timeline_fn.reference_delta = 0;
        // Current time of the system.
        let curr_time = 500060095;

        let song_position = media_timeline_fn_to_position(timeline_fn, curr_time);
        assert!(song_position.is_none());

        // 4. Fast-forward case, the ratio is > 1, so the media is in fast-forward mode.
        timeline_fn = fidl_media_types::TimelineFunction::new_empty();
        // Playback started at some random time.
        timeline_fn.subject_time = 500;
        // Monotonic clock time at beginning of media.
        timeline_fn.reference_time = 500000000;
        // Playback rate = 2, fast-forward.
        timeline_fn.subject_delta = 2;
        timeline_fn.reference_delta = 1;
        // Current time of the system.
        let curr_time = 500760095;
        let expected_position = 1; // 1520690 / 1000000 = 1 millis

        let song_position = media_timeline_fn_to_position(timeline_fn, curr_time);
        assert!(song_position.is_some());
        assert_eq!(song_position.unwrap(), expected_position);

        // 5. Error case, went back in time.
        timeline_fn = fidl_media_types::TimelineFunction::new_empty();
        // Playback started at some random time.
        timeline_fn.subject_time = 534912992;
        // Monotonic clock time at beginning of media.
        timeline_fn.reference_time = 500000000;
        // Playback rate = 0, it's paused.
        timeline_fn.subject_delta = 0;
        timeline_fn.reference_delta = 0;
        // Current time of the system.
        let curr_time = 400060095;

        let song_position = media_timeline_fn_to_position(timeline_fn, curr_time);
        assert!(song_position.is_none());
    }

    #[test]
    /// Tests conversion from Media RepeatMode to RepeatStatusMode
    fn test_media_repeat_mode_conversion() {
        let mode = fidl_media::RepeatMode::Off;
        assert_eq!(media_repeat_mode_to_avrcp(mode), Some(fidl_avrcp::RepeatStatusMode::Off));
        let mode = fidl_media::RepeatMode::Group;
        assert_eq!(
            media_repeat_mode_to_avrcp(mode),
            Some(fidl_avrcp::RepeatStatusMode::GroupRepeat)
        );
        let mode = fidl_media::RepeatMode::Single;
        assert_eq!(
            media_repeat_mode_to_avrcp(mode),
            Some(fidl_avrcp::RepeatStatusMode::SingleTrackRepeat)
        );
    }

    #[test]
    /// Tests conversion from Media Shuffle flag to  ShuffleMode
    fn test_media_shuffle_mode_conversion() {
        let mode = true;
        assert_eq!(
            media_shuffle_mode_to_avrcp(mode),
            Some(fidl_avrcp::ShuffleMode::AllTrackShuffle)
        );
        let mode = false;
        assert_eq!(media_shuffle_mode_to_avrcp(mode), Some(fidl_avrcp::ShuffleMode::Off));
    }

    #[test]
    /// Tests conversion from Media PlayerState to fidl_avrcp::PlaybackStatus
    fn test_media_player_state_conversion() {
        let state = fidl_media::PlayerState::Idle;
        assert_eq!(
            media_player_state_to_playback_status(state),
            Some(fidl_avrcp::PlaybackStatus::Stopped)
        );
        let state = fidl_media::PlayerState::Playing;
        assert_eq!(
            media_player_state_to_playback_status(state),
            Some(fidl_avrcp::PlaybackStatus::Playing)
        );
        let state = fidl_media::PlayerState::Paused;
        assert_eq!(
            media_player_state_to_playback_status(state),
            Some(fidl_avrcp::PlaybackStatus::Paused)
        );
        let state = fidl_media::PlayerState::Buffering;
        assert_eq!(
            media_player_state_to_playback_status(state),
            Some(fidl_avrcp::PlaybackStatus::Paused)
        );
        let state = fidl_media::PlayerState::Error;
        assert_eq!(
            media_player_state_to_playback_status(state),
            Some(fidl_avrcp::PlaybackStatus::Error)
        );
    }
}

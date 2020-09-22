// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{u32, u64};

use super::*;
use fidl_fuchsia_bluetooth_avrcp::NotificationEvent;

pub_decodable_enum! {
    /// AVRCP 1.6.1 section 28 "Appendix H: list of defined notification events"
    NotificationEventId<u8, Error, InvalidParameter> {
        /// Change in playback status of the current track.
        EventPlaybackStatusChanged => 0x01,
        /// Change of current track
        EventTrackChanged => 0x02,
        /// Reached end of a track
        EventTrackReachedEnd => 0x03,
        /// Reached start of a track
        EventTrackReachedStart => 0x04,
        /// Change in playback position. Returned after
        /// the specified playback notification change
        /// notification interval
        EventPlaybackPosChanged => 0x05,
        /// Change in battery status
        EventBattStatusChanged => 0x06,
        /// Change in system status
        EventSystemStatusChanged => 0x07,
        /// Change in player application setting
        EventPlayerApplicationSettingChanged => 0x08,
        /// The content of the Now Playing list has
        /// changed, see Section 6.9.5.
        EventNowPlayingContentChanged => 0x09,
        /// The available players have changed, see
        /// Section 6.9.4.
        EventAvailablePlayersChanged => 0x0a,
        /// The Addressed Player has been changed,
        /// see Section 6.9.2.
        EventAddressedPlayerChanged => 0x0b,
        /// The UIDs have changed, see Section
        /// 6.10.3.3.
        EventUidsChanged => 0x0c,
        ///The volume has been changed locally on the TG, see Section 6.13.3.
        EventVolumeChanged => 0x0d,
        // 0x0e-0xff reserved
    }
}

impl From<&fidl_avrcp::NotificationEvent> for NotificationEventId {
    fn from(event: &NotificationEvent) -> Self {
        match event {
            &NotificationEvent::PlaybackStatusChanged => {
                NotificationEventId::EventPlaybackStatusChanged
            }
            &NotificationEvent::TrackChanged => NotificationEventId::EventTrackChanged,
            &NotificationEvent::TrackReachedEnd => NotificationEventId::EventTrackReachedEnd,
            &NotificationEvent::TrackReachedStart => NotificationEventId::EventTrackReachedStart,
            &NotificationEvent::TrackPosChanged => NotificationEventId::EventPlaybackPosChanged,
            &NotificationEvent::BattStatusChanged => NotificationEventId::EventBattStatusChanged,
            &NotificationEvent::SystemStatusChanged => {
                NotificationEventId::EventSystemStatusChanged
            }
            &NotificationEvent::PlayerApplicationSettingChanged => {
                NotificationEventId::EventPlayerApplicationSettingChanged
            }
            &NotificationEvent::NowPlayingContentChanged => {
                NotificationEventId::EventNowPlayingContentChanged
            }
            &NotificationEvent::AvailablePlayersChanged => {
                NotificationEventId::EventAvailablePlayersChanged
            }
            &NotificationEvent::AddressedPlayerChanged => {
                NotificationEventId::EventAddressedPlayerChanged
            }
            &NotificationEvent::UidsChanged => NotificationEventId::EventUidsChanged,
            &NotificationEvent::VolumeChanged => NotificationEventId::EventVolumeChanged,
        }
    }
}

impl From<&NotificationEventId> for fidl_avrcp::NotificationEvent {
    fn from(event: &NotificationEventId) -> Self {
        match event {
            &NotificationEventId::EventPlaybackStatusChanged => {
                NotificationEvent::PlaybackStatusChanged
            }
            &NotificationEventId::EventTrackChanged => NotificationEvent::TrackChanged,
            &NotificationEventId::EventTrackReachedEnd => NotificationEvent::TrackReachedEnd,
            &NotificationEventId::EventTrackReachedStart => NotificationEvent::TrackReachedStart,
            &NotificationEventId::EventPlaybackPosChanged => NotificationEvent::TrackPosChanged,
            &NotificationEventId::EventBattStatusChanged => NotificationEvent::BattStatusChanged,
            &NotificationEventId::EventSystemStatusChanged => {
                NotificationEvent::SystemStatusChanged
            }
            &NotificationEventId::EventPlayerApplicationSettingChanged => {
                NotificationEvent::PlayerApplicationSettingChanged
            }
            &NotificationEventId::EventNowPlayingContentChanged => {
                NotificationEvent::NowPlayingContentChanged
            }
            &NotificationEventId::EventAvailablePlayersChanged => {
                NotificationEvent::AvailablePlayersChanged
            }
            &NotificationEventId::EventAddressedPlayerChanged => {
                NotificationEvent::AddressedPlayerChanged
            }
            &NotificationEventId::EventUidsChanged => NotificationEvent::UidsChanged,
            &NotificationEventId::EventVolumeChanged => NotificationEvent::VolumeChanged,
        }
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.2 RegisterNotification
pub struct RegisterNotificationCommand {
    event_id: NotificationEventId,
    playback_interval: u32,
}

impl RegisterNotificationCommand {
    const EVENT_ID_LEN: usize = 1;
    const PLAYBACK_INTERVAL_LEN: usize = 4;

    pub fn new(event_id: NotificationEventId) -> Self {
        Self { event_id, playback_interval: 0 }
    }

    pub fn new_position_changed(playback_interval: u32) -> Self {
        Self { event_id: NotificationEventId::EventPlaybackPosChanged, playback_interval }
    }

    pub fn event_id(&self) -> &NotificationEventId {
        &self.event_id
    }

    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn playback_interval(&self) -> u32 {
        self.playback_interval
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for RegisterNotificationCommand {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for RegisterNotificationCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Notify
    }
}

impl Decodable for RegisterNotificationCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < Self::EVENT_ID_LEN + Self::PLAYBACK_INTERVAL_LEN {
            return Err(Error::InvalidMessage);
        }
        let event_id = NotificationEventId::try_from(buf[0])?;

        let mut temp = [0; Self::PLAYBACK_INTERVAL_LEN];
        temp.copy_from_slice(
            &buf[Self::EVENT_ID_LEN..Self::EVENT_ID_LEN + Self::PLAYBACK_INTERVAL_LEN],
        );
        let playback_interval = u32::from_be_bytes(temp);

        Ok(Self { event_id, playback_interval })
    }
}

impl Encodable for RegisterNotificationCommand {
    fn encoded_len(&self) -> usize {
        Self::EVENT_ID_LEN + Self::PLAYBACK_INTERVAL_LEN
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < Self::EVENT_ID_LEN + Self::PLAYBACK_INTERVAL_LEN {
            return Err(Error::InvalidMessageLength);
        }

        buf[0] = u8::from(&self.event_id);
        buf[Self::EVENT_ID_LEN..Self::EVENT_ID_LEN + Self::PLAYBACK_INTERVAL_LEN]
            .copy_from_slice(&self.playback_interval.to_be_bytes());
        Ok(())
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.2 RegisterNotification
pub struct VolumeChangedNotificationResponse {
    volume: u8,
}

impl VolumeChangedNotificationResponse {
    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn new(volume: u8) -> Self {
        Self { volume }
    }

    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn volume(&self) -> u8 {
        self.volume
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for VolumeChangedNotificationResponse {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

impl Decodable for VolumeChangedNotificationResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessage);
        }

        if buf[0] != u8::from(&NotificationEventId::EventVolumeChanged) {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { volume: buf[1] & 0x7F }) // high bit is reserved
    }
}

impl Encodable for VolumeChangedNotificationResponse {
    fn encoded_len(&self) -> usize {
        2
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }

        buf[0] = u8::from(&NotificationEventId::EventVolumeChanged);
        buf[1] = self.volume & 0x7F; // high bit is reserved
        Ok(())
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.2 RegisterNotification
pub struct PlaybackStatusChangedNotificationResponse {
    playback_status: PlaybackStatus,
}

impl PlaybackStatusChangedNotificationResponse {
    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn new(playback_status: PlaybackStatus) -> Self {
        Self { playback_status }
    }

    pub fn playback_status(&self) -> PlaybackStatus {
        self.playback_status
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for PlaybackStatusChangedNotificationResponse {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

impl Decodable for PlaybackStatusChangedNotificationResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessageLength);
        }

        if buf[0] != u8::from(&NotificationEventId::EventPlaybackStatusChanged) {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { playback_status: PlaybackStatus::try_from(buf[1])? })
    }
}

impl Encodable for PlaybackStatusChangedNotificationResponse {
    fn encoded_len(&self) -> usize {
        2
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }

        buf[0] = u8::from(&NotificationEventId::EventPlaybackStatusChanged);
        buf[1] = u8::from(&self.playback_status);
        Ok(())
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.2 RegisterNotification
pub struct TrackChangedNotificationResponse {
    identifier: u64,
}

impl TrackChangedNotificationResponse {
    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn new(identifier: u64) -> Self {
        Self { identifier }
    }

    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn unknown() -> Self {
        Self::new(0x0)
    }

    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn none() -> Self {
        Self::new(u64::MAX)
    }

    pub fn identifier(&self) -> u64 {
        self.identifier
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for TrackChangedNotificationResponse {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

impl Decodable for TrackChangedNotificationResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 9 {
            return Err(Error::InvalidMessage);
        }

        if buf[0] != u8::from(&NotificationEventId::EventTrackChanged) {
            return Err(Error::InvalidMessage);
        }

        let mut temp = [0; 8];
        temp.copy_from_slice(&buf[1..9]);
        let identifier = u64::from_be_bytes(temp);

        Ok(Self { identifier })
    }
}

impl Encodable for TrackChangedNotificationResponse {
    fn encoded_len(&self) -> usize {
        9
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }

        buf[0] = u8::from(&NotificationEventId::EventTrackChanged);
        buf[1..9].copy_from_slice(&self.identifier.to_be_bytes());
        Ok(())
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.2 RegisterNotification
pub struct PlaybackPosChangedNotificationResponse {
    position: u32,
}

impl PlaybackPosChangedNotificationResponse {
    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn new(position: u32) -> Self {
        Self { position }
    }

    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn no_track() -> Self {
        Self::new(0xFFFFFFFF)
    }

    pub fn position(&self) -> u32 {
        self.position
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for PlaybackPosChangedNotificationResponse {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

impl Decodable for PlaybackPosChangedNotificationResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 5 {
            return Err(Error::InvalidMessage);
        }

        if buf[0] != u8::from(&NotificationEventId::EventPlaybackPosChanged) {
            return Err(Error::InvalidMessage);
        }

        let mut temp = [0; 4];
        temp.copy_from_slice(&buf[1..5]);
        let position = u32::from_be_bytes(temp);

        Ok(Self { position })
    }
}

impl Encodable for PlaybackPosChangedNotificationResponse {
    fn encoded_len(&self) -> usize {
        5
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }

        buf[0] = u8::from(&NotificationEventId::EventPlaybackPosChanged);
        buf[1..5].copy_from_slice(&self.position.to_be_bytes());
        Ok(())
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.2 RegisterNotification
pub struct AddressedPlayerChangedNotificationResponse {
    player_id: u16,
    uid_counter: u16,
}

impl AddressedPlayerChangedNotificationResponse {
    pub fn new(player_id: u16, uid_counter: u16) -> Self {
        Self { player_id, uid_counter }
    }

    #[cfg(test)] // only used by tests today
    pub fn player_id(&self) -> u16 {
        self.player_id
    }

    #[cfg(test)] // only used by tests today
    pub fn uid_counter(&self) -> u16 {
        self.uid_counter
    }
}

impl VendorDependentPdu for AddressedPlayerChangedNotificationResponse {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

impl Decodable for AddressedPlayerChangedNotificationResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 5 {
            return Err(Error::InvalidMessage);
        }

        if buf[0] != u8::from(&NotificationEventId::EventAddressedPlayerChanged) {
            return Err(Error::InvalidMessage);
        }

        let player_id = {
            let mut temp = [0; 2];
            temp.copy_from_slice(&buf[1..3]);
            u16::from_be_bytes(temp)
        };

        let uid_counter = {
            let mut temp = [0; 2];
            temp.copy_from_slice(&buf[3..5]);
            u16::from_be_bytes(temp)
        };

        Ok(Self { player_id, uid_counter })
    }
}

impl Encodable for AddressedPlayerChangedNotificationResponse {
    fn encoded_len(&self) -> usize {
        5
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }

        buf[0] = u8::from(&NotificationEventId::EventAddressedPlayerChanged);
        buf[1..3].copy_from_slice(&self.player_id.to_be_bytes());
        buf[3..5].copy_from_slice(&self.uid_counter.to_be_bytes());
        Ok(())
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.2 RegisterNotification
pub struct PlayerApplicationSettingChangedResponse {
    attribute_values: Vec<(PlayerApplicationSettingAttributeId, u8)>,
}

impl PlayerApplicationSettingChangedResponse {
    pub fn new(
        attribute_values: Vec<(PlayerApplicationSettingAttributeId, u8)>,
    ) -> PlayerApplicationSettingChangedResponse {
        Self { attribute_values }
    }
}

impl VendorDependentPdu for PlayerApplicationSettingChangedResponse {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

impl Decodable for PlayerApplicationSettingChangedResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessage);
        }

        if buf[0] != u8::from(&NotificationEventId::EventPlayerApplicationSettingChanged) {
            return Err(Error::InvalidMessage);
        }

        let num_values = buf[1];
        // There must be at least 1 attribute (ID,Value) pair provided.
        // See AVRCP Sec 6.5.3
        if num_values < 1 {
            return Err(Error::InvalidMessageLength);
        }

        let mut idx = 2;
        let mut attribute_values = vec![];

        while idx + 1 < buf.len() {
            let attribute_id: u8 = buf[idx];
            let value: u8 = buf[idx + 1];
            attribute_values
                .push((PlayerApplicationSettingAttributeId::try_from(attribute_id)?, value));
            idx += 2;
        }

        if attribute_values.len() != num_values as usize {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { attribute_values })
    }
}

impl Encodable for PlayerApplicationSettingChangedResponse {
    fn encoded_len(&self) -> usize {
        2 + (2 * self.attribute_values.len()) as usize
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }

        buf[0] = u8::from(&NotificationEventId::EventPlayerApplicationSettingChanged);

        let num_values =
            u8::try_from(self.attribute_values.len()).map_err(|_| Error::ParameterEncodingError)?;

        buf[1] = num_values;
        let mut idx: usize = 0;
        let mut buf_idx: usize = 2;

        while idx < num_values as usize {
            let (id, val) = self.attribute_values[idx];
            buf[buf_idx] = u8::from(&id);
            buf[buf_idx + 1] = val;
            idx += 1;
            buf_idx += 2;
        }

        Ok(())
    }
}

impl From<PlayerApplicationSettings> for PlayerApplicationSettingChangedResponse {
    fn from(src: PlayerApplicationSettings) -> PlayerApplicationSettingChangedResponse {
        let mut values = vec![];

        if let Some(eq) = src.equalizer {
            values.push((PlayerApplicationSettingAttributeId::Equalizer, u8::from(&eq)))
        }

        if let Some(repeat_mode) = src.repeat_status_mode {
            values.push((
                PlayerApplicationSettingAttributeId::RepeatStatusMode,
                u8::from(&repeat_mode),
            ))
        }

        if let Some(shuffle_mode) = src.shuffle_mode {
            values.push((PlayerApplicationSettingAttributeId::ShuffleMode, u8::from(&shuffle_mode)))
        }

        if let Some(scan_mode) = src.scan_mode {
            values.push((PlayerApplicationSettingAttributeId::ScanMode, u8::from(&scan_mode)))
        }

        PlayerApplicationSettingChangedResponse::new(values)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_register_notification_encode() {
        let cmd = RegisterNotificationCommand::new(NotificationEventId::EventVolumeChanged);
        assert_eq!(cmd.playback_interval(), 0);
        assert_eq!(cmd.encoded_len(), 5);
        assert_eq!(cmd.command_type(), AvcCommandType::Notify);
        let mut buf = vec![0; cmd.encoded_len()];
        assert!(cmd.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x0d, 0x00, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn test_register_notification_encode_pos_changed() {
        let cmd = RegisterNotificationCommand::new_position_changed(102030405);
        assert_eq!(cmd.playback_interval(), 102030405);
        assert_eq!(cmd.encoded_len(), 5);
        assert_eq!(cmd.event_id(), &NotificationEventId::EventPlaybackPosChanged);
        assert_eq!(cmd.command_type(), AvcCommandType::Notify);
        let mut buf = vec![0; cmd.encoded_len()];
        assert!(cmd.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x05, 0x06, 0x14, 0xDC, 0x45]);
    }

    #[test]
    fn test_register_notification_decode() {
        let cmd = RegisterNotificationCommand::decode(&[0x0d, 0x00, 0x00, 0x00, 0x00])
            .expect("unable to decode packet");
        assert_eq!(cmd.encoded_len(), 5);
        assert_eq!(cmd.event_id(), &NotificationEventId::EventVolumeChanged);
        assert_eq!(cmd.playback_interval(), 0);
    }

    #[test]
    fn test_register_notification_decode_pos_changed() {
        let cmd = RegisterNotificationCommand::decode(&[0x05, 0x01, 0x00, 0xff, 0x00])
            .expect("unable to decode packet");
        assert_eq!(cmd.encoded_len(), 5);
        assert_eq!(cmd.event_id(), &NotificationEventId::EventPlaybackPosChanged);
        assert_eq!(cmd.playback_interval(), 16842496);
    }

    #[test]
    fn test_playbackpos_changed_notification_decode() {
        let cmd = PlaybackPosChangedNotificationResponse::decode(&[0x05, 0x00, 0x00, 0xff, 0x00])
            .expect("unable to decode packet");
        assert_eq!(65280, cmd.position());
    }

    #[test]
    fn test_playbackpos_changed_notification_encode() {
        let cmd = PlaybackPosChangedNotificationResponse::new(20);
        assert_eq!(20, cmd.position());
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("unable to encode packet");
        assert_eq!(buf, &[0x05, 0x00, 0x00, 0x00, 0x14]);
    }

    #[test]
    fn test_playbackpos_changed_notification_encode_decode() {
        let cmd_a = PlaybackPosChangedNotificationResponse::new(30);
        let mut buf = vec![0; cmd_a.encoded_len()];
        cmd_a.encode(&mut buf[..]).expect("unable to encode packet");
        let cmd_b = PlaybackPosChangedNotificationResponse::decode(&buf[..])
            .expect("unable to decode packet");

        assert_eq!(cmd_a.position(), cmd_b.position());
    }

    #[test]
    fn test_volume_changed_notification_decode() {
        let a = VolumeChangedNotificationResponse::decode(&[0x0d, 0x0f])
            .expect("unable to decode packet");
        assert_eq!(15, a.volume());
    }

    #[test]
    fn test_volume_changed_notification_encode() {
        let cmd = VolumeChangedNotificationResponse::new(20);
        assert_eq!(20, cmd.volume());
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("unable to encode packet");
        assert_eq!(buf, &[0x0d, 0x14]);
    }

    #[test]
    fn test_volume_changed_notification_encode_decode() {
        let cmd = VolumeChangedNotificationResponse::new(30);
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("unable to encode packet");
        let b =
            VolumeChangedNotificationResponse::decode(&buf[..]).expect("unable to decode packet");

        assert_eq!(cmd.volume(), b.volume());
    }

    #[test]
    fn test_track_changed_notification_decode() {
        let cmd = TrackChangedNotificationResponse::decode(&[
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f,
        ])
        .expect("unable to decode packet");
        assert_eq!(15, cmd.identifier());
    }

    #[test]
    fn test_track_changed_notification_encode_id() {
        let cmd = TrackChangedNotificationResponse::new(20);
        assert_eq!(20, cmd.identifier());
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("unable to encode packet");
        assert_eq!(buf, &[0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14]);
    }

    #[test]
    fn test_track_changed_notification_encode_unknown() {
        let cmd = TrackChangedNotificationResponse::unknown();
        assert_eq!(0, cmd.identifier());
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("unable to encode packet");
        assert_eq!(buf, &[0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn test_track_changed_notification_encode_none() {
        let cmd = TrackChangedNotificationResponse::none();
        assert_eq!(u64::MAX, cmd.identifier());
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("unable to encode packet");
        assert_eq!(buf, &[0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]);
    }

    #[test]
    fn test_track_changed_notification_encode_decode() {
        let cmd_a = TrackChangedNotificationResponse::new(30);
        let mut buf = vec![0; cmd_a.encoded_len()];
        cmd_a.encode(&mut buf[..]).expect("unable to encode packet");
        let cmd_b =
            TrackChangedNotificationResponse::decode(&buf[..]).expect("unable to decode packet");

        assert_eq!(cmd_a.identifier(), cmd_b.identifier());
    }
    #[test]
    fn test_playback_status_changed_encode() {
        let cmd = PlaybackStatusChangedNotificationResponse::new(PlaybackStatus::Playing);
        assert_eq!(PlaybackStatus::Playing, cmd.playback_status());
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("unable to encode packet");
        assert_eq!(buf, &[0x01, 0x01]);
    }

    #[test]
    fn test_playback_status_changed_decode() {
        let cmd = PlaybackStatusChangedNotificationResponse::decode(&[0x01, 0x02])
            .expect("unable to decode packet");
        assert_eq!(PlaybackStatus::Paused, cmd.playback_status());
    }

    #[test]
    fn test_playback_status_changed_encode_decode() {
        let cmd_a = PlaybackStatusChangedNotificationResponse::new(PlaybackStatus::FwdSeek);
        let mut buf = vec![0; cmd_a.encoded_len()];
        cmd_a.encode(&mut buf[..]).expect("unable to encode packet");
        let cmd_b = PlaybackStatusChangedNotificationResponse::decode(&buf[..])
            .expect("unable to decode packet");

        assert_eq!(cmd_a.playback_status(), cmd_b.playback_status());
    }

    #[test]
    fn test_playback_status_changed_invalid() {
        let cmd = PlaybackStatusChangedNotificationResponse::decode(&[0x01, 0xF1]);
        assert_eq!(cmd.unwrap_err(), Error::OutOfRange);
    }

    #[test]
    fn test_addressed_player_changed_encode_decode() {
        let cmd_a = AddressedPlayerChangedNotificationResponse::new(15535, 5535);
        let mut buf = vec![0; cmd_a.encoded_len()];
        cmd_a.encode(&mut buf[..]).expect("unable to encode packet");
        let cmd_b = AddressedPlayerChangedNotificationResponse::decode(&buf[..])
            .expect("unable to decode packet");

        assert_eq!(cmd_a.uid_counter(), cmd_b.uid_counter());
        assert_eq!(cmd_a.player_id(), cmd_b.player_id());
    }

    #[test]
    fn test_addressed_player_changed_invalid() {
        let cmd = AddressedPlayerChangedNotificationResponse::decode(&[0x13, 0xF1]);
        assert_eq!(cmd.unwrap_err(), Error::InvalidMessage);
    }

    #[test]
    fn test_player_application_settings_changed_response_encode() {
        let response_vals = vec![
            (PlayerApplicationSettingAttributeId::Equalizer, 0x02),
            (PlayerApplicationSettingAttributeId::RepeatStatusMode, 0x01),
            (PlayerApplicationSettingAttributeId::ShuffleMode, 0x01),
        ];
        let response = PlayerApplicationSettingChangedResponse::new(response_vals);
        assert_eq!(response.raw_pdu_id(), u8::from(&PduId::RegisterNotification));
        assert_eq!(response.encoded_len(), 8);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x08, 0x03, 0x01, 0x02, 0x02, 0x01, 0x03, 0x01]);
    }

    #[test]
    fn test_player_application_settings_changed_response_response_decode() {
        let result = PlayerApplicationSettingChangedResponse::decode(&[0x08, 0x01, 0x04, 0x02]);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.attribute_values.len(), 1);
        assert_eq!(
            result.attribute_values,
            vec![(PlayerApplicationSettingAttributeId::ScanMode, 0x02)]
        );
    }

    #[test]
    fn test_player_application_settings_changed_response_decode_invalid() {
        // Malformed result, contains two attributes, but missing second value.
        let result =
            PlayerApplicationSettingChangedResponse::decode(&[0x08, 0x02, 0x04, 0x02, 0x03]);
        assert_eq!(result.unwrap_err(), Error::InvalidMessage);
    }
}

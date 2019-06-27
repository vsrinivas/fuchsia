// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{u32, u64};

use super::*;

pub_decodable_enum! {
    /// AVRCP 1.6.1 section 28 "Appendix H: list of defined notification events"
    NotificationEventId<u8, Error> {
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

    pub fn playback_interval(&self) -> u32 {
        self.playback_interval
    }
}

impl VendorDependent for RegisterNotificationCommand {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
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
            return Err(Error::OutOfRange);
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
    pub fn new(volume: u8) -> Self {
        Self { volume }
    }

    pub fn volume(&self) -> u8 {
        self.volume
    }
}

impl VendorDependent for VolumeChangedNotificationResponse {
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
            return Err(Error::OutOfRange);
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
    #[allow(dead_code)] // TODO(BT-2218): WIP. Remove once used.
    pub fn new(playback_status: PlaybackStatus) -> Self {
        Self { playback_status }
    }

    pub fn playback_status(&self) -> PlaybackStatus {
        self.playback_status
    }
}

impl VendorDependent for PlaybackStatusChangedNotificationResponse {
    fn pdu_id(&self) -> PduId {
        PduId::RegisterNotification
    }
}

impl Decodable for PlaybackStatusChangedNotificationResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessage);
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
            return Err(Error::OutOfRange);
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
    pub fn new(identifier: u64) -> Self {
        Self { identifier }
    }

    pub fn unknown() -> Self {
        Self::new(0x0)
    }

    pub fn none() -> Self {
        Self::new(u64::MAX)
    }

    pub fn identifier(&self) -> u64 {
        self.identifier
    }
}

impl VendorDependent for TrackChangedNotificationResponse {
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
            return Err(Error::OutOfRange);
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
    pub fn new(position: u32) -> Self {
        Self { position }
    }

    pub fn no_track() -> Self {
        Self::new(u32::MAX)
    }

    pub fn position(&self) -> u32 {
        self.position
    }
}

impl VendorDependent for PlaybackPosChangedNotificationResponse {
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
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(&NotificationEventId::EventTrackChanged);
        buf[1..5].copy_from_slice(&self.position.to_be_bytes());
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_register_notification_encode() {
        let b = RegisterNotificationCommand::new(NotificationEventId::EventVolumeChanged);
        assert_eq!(b.encoded_len(), 5);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x0d, 0x00, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn test_register_notification_encode_pos_changed() {
        let b = RegisterNotificationCommand::new_position_changed(102030405);
        assert_eq!(b.encoded_len(), 5);
        assert_eq!(b.event_id(), &NotificationEventId::EventPlaybackPosChanged);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x05, 0x06, 0x14, 0xDC, 0x45]);
    }

    #[test]
    fn test_register_notification_decode() {
        let b = RegisterNotificationCommand::decode(&[0x0d, 0x00, 0x00, 0x00, 0x00])
            .expect("unable to decode packet");
        assert_eq!(b.encoded_len(), 5);
        assert_eq!(b.event_id(), &NotificationEventId::EventVolumeChanged);
    }

    #[test]
    fn test_register_notification_decode_pos_changed() {
        let b = RegisterNotificationCommand::decode(&[0x05, 0x01, 0x00, 0xff, 0x00])
            .expect("unable to decode packet");
        assert_eq!(b.encoded_len(), 5);
        assert_eq!(b.event_id(), &NotificationEventId::EventPlaybackPosChanged);
        assert_eq!(b.playback_interval(), 16842496);
    }
}

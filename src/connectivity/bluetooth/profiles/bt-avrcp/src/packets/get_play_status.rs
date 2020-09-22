// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::u32;

use super::*;

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.1 GetPlayStatus
pub struct GetPlayStatusCommand {}

impl GetPlayStatusCommand {
    pub fn new() -> GetPlayStatusCommand {
        Self {}
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for GetPlayStatusCommand {
    fn pdu_id(&self) -> PduId {
        PduId::GetPlayStatus
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for GetPlayStatusCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for GetPlayStatusCommand {
    fn decode(_buf: &[u8]) -> PacketResult<Self> {
        Ok(Self {})
    }
}

impl Encodable for GetPlayStatusCommand {
    fn encoded_len(&self) -> usize {
        0
    }

    fn encode(&self, _buf: &mut [u8]) -> PacketResult<()> {
        Ok(())
    }
}

// The length of the current song, u32 represented as a 4 byte payload.
const SONG_LENGTH_LEN: usize = 4;
// The position of the current song, u32 represented as a 4 byte payload.
const SONG_POSITION_LEN: usize = 4;
// The current status of playing media, 1 byte payload.
const PLAY_STATUS_LEN: usize = 1;
// The total length of the response payload.
const RESPONSE_LEN: usize = SONG_LENGTH_LEN + SONG_POSITION_LEN + PLAY_STATUS_LEN;
// If the TG doesn't support song_length, it shall respond with 0xFFFFFFFF.
pub const SONG_LENGTH_NOT_SUPPORTED: u32 = 0xFFFFFFFF;
// If the TG doesn't support song_position, it shall respond with 0xFFFFFFFF.
pub const SONG_POSITION_NOT_SUPPORTED: u32 = 0xFFFFFFFF;

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.7.1 GetPlayStatus
pub struct GetPlayStatusResponse {
    pub song_length: u32,
    pub song_position: u32,
    pub playback_status: PlaybackStatus,
}

impl GetPlayStatusResponse {
    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    /// Time is encoded as milliseconds. Max value is (2^32 – 1)
    pub fn new(
        song_length: u32,
        song_position: u32,
        playback_status: PlaybackStatus,
    ) -> GetPlayStatusResponse {
        Self { song_length, song_position, playback_status }
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for GetPlayStatusResponse {
    fn pdu_id(&self) -> PduId {
        PduId::GetPlayStatus
    }
}

impl Decodable for GetPlayStatusResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < RESPONSE_LEN {
            return Err(Error::InvalidMessageLength);
        }

        let mut temp = [0; SONG_LENGTH_LEN];
        temp.copy_from_slice(&buf[0..SONG_LENGTH_LEN]);
        let song_length = u32::from_be_bytes(temp);

        temp = [0; SONG_POSITION_LEN];
        temp.copy_from_slice(&buf[SONG_LENGTH_LEN..SONG_LENGTH_LEN + SONG_POSITION_LEN]);
        let song_position = u32::from_be_bytes(temp);

        let playback_status = PlaybackStatus::try_from(buf[SONG_LENGTH_LEN + SONG_POSITION_LEN])?;

        Ok(Self { song_length, song_position, playback_status })
    }
}

impl Encodable for GetPlayStatusResponse {
    fn encoded_len(&self) -> usize {
        RESPONSE_LEN
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        let sl_bytes = u32::to_be_bytes(self.song_length);
        let sp_bytes = u32::to_be_bytes(self.song_position);

        buf[0..SONG_LENGTH_LEN].copy_from_slice(&sl_bytes);
        buf[SONG_LENGTH_LEN..SONG_LENGTH_LEN + SONG_POSITION_LEN].copy_from_slice(&sp_bytes);
        buf[SONG_LENGTH_LEN + SONG_POSITION_LEN] = u8::from(&self.playback_status);

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_play_status_command_encode() {
        let b = GetPlayStatusCommand::new();
        assert_eq!(b.encoded_len(), 0);
        assert_eq!(b.command_type(), AvcCommandType::Status);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf.len(), 0);
    }

    #[test]
    fn test_get_play_status_command_decode() {
        let b = GetPlayStatusCommand::decode(&[]).expect("unable to decode");
        assert_eq!(b.encoded_len(), 0);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf.len(), 0);
    }

    #[test]
    fn test_get_play_status_response_encode() {
        let b = GetPlayStatusResponse::new(0x64, 0x102095, PlaybackStatus::Playing);
        assert_eq!(b.encoded_len(), 9);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(
            buf,
            &[
                0x00, 0x00, 0x00, 0x64, // length
                0x00, 0x10, 0x20, 0x95, // position
                0x01, // Playing
            ]
        );
    }

    #[test]
    fn test_get_play_status_response_decode() {
        let b = GetPlayStatusResponse::decode(&[
            0x00, 0x00, 0x00, 0x64, // length
            0x00, 0x10, 0x20, 0x90, // position
            0x03, // FwdSeek
        ])
        .expect("unable to decode packet");
        assert_eq!(b.encoded_len(), 9);
        assert_eq!(b.playback_status, PlaybackStatus::FwdSeek);
        assert_eq!(b.song_length, 0x64);
        assert_eq!(b.song_position, 0x102090);
    }
}

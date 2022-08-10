// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    packet_encoding::{Decodable, Encodable},
    std::convert::{TryFrom, TryInto},
};

use crate::packets::{
    AvcCommandType, Error, PacketResult, PduId, Scope, StatusCode, VendorCommand,
    VendorDependentPdu,
};

/// AVRCP 1.6.2 section 6.12.1.1 PlayItem
#[derive(Debug)]
pub struct PlayItemCommand {
    scope: Scope,
    uid: u64,
    uid_counter: u16,
}

impl PlayItemCommand {
    /// The packet size of a PlayItemCommand.
    /// 1 byte for scope, 8 for uid, 2 for uid_counter.
    pub const PACKET_SIZE: usize = 11;

    pub fn new(scope: Scope, uid: u64, uid_counter: u16) -> Result<PlayItemCommand, Error> {
        if scope == Scope::MediaPlayerList {
            return Err(Error::InvalidMessage);
        }
        Ok(Self { scope, uid, uid_counter })
    }

    #[cfg(test)]
    pub(crate) fn scope(&self) -> Scope {
        self.scope
    }

    #[cfg(test)]
    pub(crate) fn uid(&self) -> u64 {
        self.uid
    }

    #[cfg(test)]
    pub fn uid_counter(&self) -> u16 {
        self.uid_counter
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for PlayItemCommand {
    fn pdu_id(&self) -> PduId {
        PduId::PlayItem
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for PlayItemCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

impl Decodable for PlayItemCommand {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < PlayItemCommand::PACKET_SIZE {
            return Err(Error::InvalidMessageLength);
        }
        let scope = Scope::try_from(buf[0])?;
        // The scope in which the UID of the media element item or folder item,
        // if supported, is valid. This cannot be MediaPlayerList.
        if scope == Scope::MediaPlayerList {
            return Err(Error::InvalidMessage);
        }
        let uid = u64::from_be_bytes(buf[1..9].try_into().unwrap());
        let uid_counter = u16::from_be_bytes(buf[9..11].try_into().unwrap());

        Ok(Self::new(scope, uid, uid_counter)?)
    }
}

impl Encodable for PlayItemCommand {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        PlayItemCommand::PACKET_SIZE
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < PlayItemCommand::PACKET_SIZE {
            return Err(Error::BufferLengthOutOfRange);
        }
        // The scope in which the UID of the media element item or folder item,
        // if supported, is valid. This cannot be MediaPlayerList.
        if self.scope == Scope::MediaPlayerList {
            return Err(Error::InvalidMessage);
        }

        buf[0] = u8::from(&self.scope);
        buf[1..9].copy_from_slice(&self.uid.to_be_bytes());
        buf[9..11].copy_from_slice(&self.uid_counter.to_be_bytes());

        Ok(())
    }
}

/// AVRCP 1.6.2 section 6.12.1.2 PlayItem
#[derive(Debug)]
pub struct PlayItemResponse {
    status: StatusCode,
}

impl PlayItemResponse {
    /// The packet size of a PlayItemResponse.
    /// 1 byte for status.
    const PACKET_SIZE: usize = 1;

    #[allow(unused)]
    pub fn new(status: StatusCode) -> PlayItemResponse {
        Self { status }
    }

    pub fn status(&self) -> StatusCode {
        self.status
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for PlayItemResponse {
    fn pdu_id(&self) -> PduId {
        PduId::PlayItem
    }
}

impl Decodable for PlayItemResponse {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < PlayItemResponse::PACKET_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let status = StatusCode::try_from(buf[0])?;
        Ok(Self { status })
    }
}

impl Encodable for PlayItemResponse {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        PlayItemResponse::PACKET_SIZE
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }
        buf[0] = u8::from(&self.status);

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn test_play_item_command_encode() {
        let cmd = PlayItemCommand::new(Scope::MediaPlayerVirtualFilesystem, 257, 1)
            .expect("should have initialized");
        assert_eq!(cmd.encoded_len(), PlayItemCommand::PACKET_SIZE);
        assert_eq!(cmd.pdu_id(), PduId::PlayItem);
        assert_eq!(cmd.command_type(), AvcCommandType::Control);
        let mut buf = vec![0; cmd.encoded_len()];
        let _ = cmd.encode(&mut buf[..]).expect("should have encoded successfully");
        assert_eq!(buf, &[0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x1, 0x0, 0x1]);
    }

    #[fuchsia::test]
    fn test_play_item_command_encode_invalid() {
        let _ = PlayItemCommand::new(Scope::MediaPlayerList, 1, 1)
            .expect_err("should not have initialized");

        let invalid_cmd = PlayItemCommand { scope: Scope::MediaPlayerList, uid: 1, uid_counter: 1 };
        let mut buf = vec![0; invalid_cmd.encoded_len()];
        let _ = invalid_cmd.encode(&mut buf[..]).expect_err("should have failed encoding");

        let cmd = PlayItemCommand { scope: Scope::Search, uid: 1, uid_counter: 1 };
        let mut invalid_buf = vec![0; 5];
        let _ = cmd.encode(&mut invalid_buf[..]).expect_err("should have failed encoding");
    }

    #[fuchsia::test]
    fn test_play_item_command_decode() {
        let buf = [0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x1, 0x0, 0x1];
        let cmd = PlayItemCommand::decode(&buf).expect("should have decoded");
        assert_eq!(cmd.scope, Scope::MediaPlayerVirtualFilesystem);
        assert_eq!(cmd.uid, 257);
        assert_eq!(cmd.uid_counter, 1);
    }

    #[fuchsia::test]
    fn test_play_item_command_decode_invalid() {
        let invalid_scope_buf = [0x00, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x1, 0x0, 0x1];
        let _ = PlayItemCommand::decode(&invalid_scope_buf).expect_err("should not have decoded");

        let invalid_buf = [0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x1];
        let _ = PlayItemCommand::decode(&invalid_buf).expect_err("should not have decoded");
    }

    #[fuchsia::test]
    fn test_play_item_response_encode() {
        let resp = PlayItemResponse::new(StatusCode::Success);
        assert_eq!(resp.encoded_len(), PlayItemResponse::PACKET_SIZE);
        assert_eq!(resp.pdu_id(), PduId::PlayItem);
        let mut buf = vec![0; resp.encoded_len()];
        let _ = resp.encode(&mut buf[..]).expect("should have encoded successfully");
        assert_eq!(buf, &[0x04]);
    }

    #[fuchsia::test]
    fn test_play_item_response_encode_invalid() {
        let resp = PlayItemResponse::new(StatusCode::Success);
        let mut invalid_buf = vec![];
        let _ = resp.encode(&mut invalid_buf[..]).expect_err("should have failed encoding");
    }

    #[fuchsia::test]
    fn test_play_item_response_decode() {
        let buf = [0x04];
        let resp = PlayItemResponse::decode(&buf).expect("should have decoded");
        assert_eq!(resp.status, StatusCode::Success);
    }

    #[fuchsia::test]
    fn test_play_item_response_decode_invalid() {
        let invalid_buf = [];
        let _ = PlayItemResponse::decode(&invalid_buf).expect_err("should not have decoded");
    }
}

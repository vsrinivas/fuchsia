// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    packet_encoding::{Decodable, Encodable},
    std::convert::{TryFrom, TryInto},
    std::num::NonZeroU64,
};

use crate::packets::{Direction, Error, PacketResult, StatusCode};

/// AVRCP 1.6.2 section 6.10.4.1.1 ChangePath .
#[derive(Debug, PartialEq)]
pub struct ChangePathCommand {
    uid_counter: u16,
    // None if direction is FolderUp. Some folder uid if direction is
    // FolderDown.
    folder_uid: Option<NonZeroU64>,
}

impl ChangePathCommand {
    /// The packet size of a ChangePathCommand.
    /// 2 bytes for uid counter, 1 byte for direction, 8 for folder uid.
    const PACKET_SIZE: usize = 11;

    pub fn new(uid_counter: u16, uid: Option<u64>) -> Result<Self, Error> {
        let folder_uid = match uid {
            None => None,
            Some(id) => Some(NonZeroU64::new(id).ok_or(Error::InvalidParameter)?),
        };
        Ok(Self { uid_counter, folder_uid })
    }

    fn direction(&self) -> Direction {
        match self.folder_uid {
            Some(_) => Direction::FolderDown,
            None => Direction::FolderUp,
        }
    }

    pub fn uid_counter(&self) -> u16 {
        self.uid_counter
    }

    #[cfg(test)]
    pub fn folder_uid(&self) -> Option<&NonZeroU64> {
        self.folder_uid.as_ref()
    }
}

impl Decodable for ChangePathCommand {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < ChangePathCommand::PACKET_SIZE {
            return Err(Error::InvalidMessage);
        }

        let uid_counter = u16::from_be_bytes(buf[0..2].try_into().unwrap());
        let direction = Direction::try_from(buf[2])?;
        match direction {
            Direction::FolderUp => Ok(Self { uid_counter, folder_uid: None }),
            Direction::FolderDown => {
                let folder_uid =
                    NonZeroU64::new(u64::from_be_bytes(buf[3..11].try_into().unwrap()))
                        .ok_or(Error::InvalidMessage)?;
                Ok(Self { uid_counter, folder_uid: Some(folder_uid) })
            }
        }
    }
}

impl Encodable for ChangePathCommand {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        ChangePathCommand::PACKET_SIZE
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0..2].copy_from_slice(&self.uid_counter().to_be_bytes());
        buf[2] = u8::from(&self.direction());
        buf[3..11].copy_from_slice(&self.folder_uid.map_or(0, |id| id.into()).to_be_bytes());

        Ok(())
    }
}

/// AVRCP 1.6.2 section 6.10.4.1.2 ChangePath.
#[derive(Debug)]
pub enum ChangePathResponse {
    Success { num_of_items: u32 },
    Failure(StatusCode),
}

impl ChangePathResponse {
    /// The packet size of a ChangePathResponse for failure case.
    /// 1 byte for Status.
    const FAILURE_PACKET_SIZE: usize = 1;

    /// The packet size of a ChangePathResponse for success case.
    /// 1 byte for Status, 4 bytes for Number of Items.
    const SUCCESS_PACKET_SIZE: usize = 5;

    #[allow(unused)]
    pub fn new_success(num_of_items: u32) -> Self {
        ChangePathResponse::Success { num_of_items }
    }

    #[allow(unused)]
    pub fn new_failure(status: StatusCode) -> Result<Self, Error> {
        if status == StatusCode::Success {
            return Err(Error::InvalidMessage);
        }
        Ok(ChangePathResponse::Failure(status))
    }
}

impl Decodable for ChangePathResponse {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < ChangePathResponse::FAILURE_PACKET_SIZE {
            return Err(Error::InvalidMessage);
        }

        let status = StatusCode::try_from(buf[0])?;
        if status != StatusCode::Success {
            return Ok(Self::Failure(status));
        }

        if buf.len() < ChangePathResponse::SUCCESS_PACKET_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let num_of_items = u32::from_be_bytes(buf[1..5].try_into().unwrap());
        Ok(Self::Success { num_of_items })
    }
}

impl Encodable for ChangePathResponse {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        match &self {
            Self::Success { .. } => ChangePathResponse::SUCCESS_PACKET_SIZE,
            Self::Failure(_) => ChangePathResponse::FAILURE_PACKET_SIZE,
        }
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }
        match &self {
            ChangePathResponse::Failure(status) => {
                buf[0] = u8::from(status);
            }
            ChangePathResponse::Success { num_of_items } => {
                buf[0] = u8::from(&StatusCode::Success);
                buf[1..5].copy_from_slice(&num_of_items.to_be_bytes());
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[fuchsia::test]
    /// Encoding a GetFolderItemsCommand successfully produces a byte buffer.
    fn test_change_path_command_encode() {
        let cmd = ChangePathCommand::new(1, Some(300)).expect("should be valid");

        assert_eq!(cmd.encoded_len(), ChangePathCommand::PACKET_SIZE);
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("should have encoded successfully");
        assert_eq!(buf, &[0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2C]);

        let cmd = ChangePathCommand::new(1, None).expect("should be valid");

        assert_eq!(cmd.encoded_len(), ChangePathCommand::PACKET_SIZE);
        let mut buf = vec![0; cmd.encoded_len()];
        cmd.encode(&mut buf[..]).expect("should have encoded successfully");
        assert_eq!(buf, &[0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    }

    #[fuchsia::test]
    /// Sending expected buffer decodes successfully.
    fn test_change_path_command_decode_success() {
        // Folder down with folder UID.
        let buf = [0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2C];
        let cmd = ChangePathCommand::decode(&buf[..]).expect("should have decoded successfully");
        assert_eq!(cmd.uid_counter(), 1);
        assert_eq!(cmd.direction(), Direction::FolderDown);
        assert_eq!(
            cmd,
            ChangePathCommand { uid_counter: 1, folder_uid: Some(NonZeroU64::new(300).unwrap()) }
        );

        // Folder up with zero folder UID.
        let buf = [0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let cmd = ChangePathCommand::decode(&buf[..]).expect("should have decoded successfully");
        assert_eq!(cmd.uid_counter(), 1);
        assert_eq!(cmd.direction(), Direction::FolderUp);
        assert_eq!(cmd, ChangePathCommand { uid_counter: 1, folder_uid: None });

        // Folder up with non-zero folder UID.
        let buf = [0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2C];
        let cmd = ChangePathCommand::decode(&buf[..]).expect("should have decoded successfully");
        assert_eq!(cmd.uid_counter(), 1);
        assert_eq!(cmd.direction(), Direction::FolderUp);
        assert_eq!(cmd, ChangePathCommand { uid_counter: 1, folder_uid: None });
    }

    #[fuchsia::test]
    /// Sending payloads that are malformed and/or contain invalid parameters should be
    /// gracefully handled.
    fn test_change_path_command_decode_invalid_buf() {
        // Unsupported Direction provided in buffer.
        let invalid_direction_buf = [0x00, 0x01, 0x02, 0x00, 0x00, 0x01, 0x2C];
        let _ = ChangePathCommand::decode(&invalid_direction_buf[..])
            .expect_err("decode should have failed");

        // Incomplete buffer.
        let invalid_format_buf = [0x00, 0x01, 0x01, 0x00, 0x00, 0x01];
        let _ = ChangePathCommand::decode(&invalid_format_buf[..])
            .expect_err("decode should have failed");
    }

    #[fuchsia::test]
    fn test_change_path_response_encode() {
        let resp = ChangePathResponse::new_success(16);
        assert_eq!(resp.encoded_len(), ChangePathResponse::SUCCESS_PACKET_SIZE);
        let mut buf = vec![0; resp.encoded_len()];
        let _ = resp.encode(&mut buf[..]).expect("should encode successfully");
        assert_eq!(buf, &[0x04, 0x0, 0x0, 0x0, 0x10]);

        let resp = ChangePathResponse::new_failure(StatusCode::InternalError)
            .expect("should have initialized");
        assert_eq!(resp.encoded_len(), ChangePathResponse::FAILURE_PACKET_SIZE);
        let mut buf = vec![0; resp.encoded_len()];
        let _ = resp.encode(&mut buf[..]).expect("should encode successfully");
        assert_eq!(buf, &[0x03]);
    }

    #[fuchsia::test]
    fn test_change_path_response_encode_invalid() {
        // Invalid status code for failure.
        let _ = ChangePathResponse::new_failure(StatusCode::Success)
            .expect_err("should not have initialized");

        // Invalid buf.
        let resp = ChangePathResponse::new_success(2);
        let mut invalid_buf = vec![0; 1]; // invalid buf
        let _ = resp.encode(&mut invalid_buf[..]).expect_err("encode should have failed");

        let resp = ChangePathResponse::new_failure(StatusCode::InternalError)
            .expect("should have initialized");
        let mut invalid_buf = vec![0; 0];
        let _ = resp.encode(&mut invalid_buf[..]).expect_err("encode should have failed");
    }

    #[fuchsia::test]
    fn test_change_path_response_decode_success() {
        // With status code success.
        let buf = [0x04, 0x0, 0x0, 0x0, 0x10];
        let resp = ChangePathResponse::decode(&buf[..]).expect("should have decoded successfully");
        assert_matches!(resp, ChangePathResponse::Success { num_of_items: 16 });

        // With non-success status code.
        let buf = [0x03];
        let resp = ChangePathResponse::decode(&buf[..]).expect("should have decoded successfully");
        assert_matches!(resp, ChangePathResponse::Failure(StatusCode::InternalError));
    }

    #[fuchsia::test]
    /// Decoding a packet that is malformed.
    fn test_change_path_response_decode_invalid_buf() {
        // Empty buffer.
        let invalid_format_buf = [];
        let _ = ChangePathResponse::decode(&invalid_format_buf[..])
            .expect_err("decode should have failed");

        // Incomplete buffer with status code success.
        let buf = [0x04, 0x0];
        let _ = ChangePathResponse::decode(&buf[..]).expect_err("decode should have failed");
    }
}

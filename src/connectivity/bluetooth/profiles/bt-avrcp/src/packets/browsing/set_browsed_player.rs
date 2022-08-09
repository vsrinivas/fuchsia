// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    packet_encoding::{Decodable, Encodable},
    std::convert::{TryFrom, TryInto},
    tracing::warn,
};

use crate::packets::{CharsetId, Error, PacketResult, StatusCode};

/// AVRCP 1.6.2 section 6.9.3.1 SetBrowsedPlayer.
#[derive(Debug)]
pub struct SetBrowsedPlayerCommand {
    player_id: u16,
}

impl SetBrowsedPlayerCommand {
    pub fn new(player_id: u16) -> Self {
        Self { player_id }
    }

    #[cfg(test)]
    pub fn player_id(&self) -> u16 {
        self.player_id
    }
}

impl Decodable for SetBrowsedPlayerCommand {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessage);
        }

        let player_id = u16::from_be_bytes(buf[0..2].try_into().unwrap());

        Ok(Self { player_id })
    }
}

impl Encodable for SetBrowsedPlayerCommand {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        2
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0..2].copy_from_slice(&self.player_id.to_be_bytes());
        Ok(())
    }
}

/// AVRCP 1.6.2 section 6.9.3.2 SetBrowsedPlayer.
#[derive(Debug)]
pub enum SetBrowsedPlayerResponse {
    Success(SetBrowsedPlayerResponseParams),
    Failure(StatusCode),
}

#[derive(Debug)]
pub struct SetBrowsedPlayerResponseParams {
    uid_counter: u16,
    num_items: u32,
    folder_names: Vec<String>,
}

impl SetBrowsedPlayerResponseParams {
    pub fn uid_counter(&self) -> u16 {
        self.uid_counter
    }

    pub fn num_items(&self) -> u32 {
        self.num_items
    }

    pub fn folder_names(self) -> Vec<String> {
        self.folder_names
    }
}

impl SetBrowsedPlayerResponse {
    /// Minimum encoded length that includes length of all required parameters
    /// for a failure response message.
    /// The fields are: Status (1 byte).
    /// Defined in AVRCP 1.6.2, Section 6.9.3.2.
    const MIN_FAILURE_PACKET_SIZE: usize = 1;

    /// Minimum encoded length that includes length of all required parameters
    /// for a success response message. Excludes folder name length/folder name pair.
    /// The fields are: Status (1 byte), UID Counter (2 bytes), Number
    /// of Items (4 bytes), Char Set ID (2 bytes), Folder Depth (1 byte).
    /// Defined in AVRCP 1.6.2, Section 6.9.3.2.
    const MIN_SUCCESS_PACKET_SIZE: usize = 10;

    #[cfg(test)]
    pub fn new_success(
        uid_counter: u16,
        num_items: u32,
        folder_names: Vec<String>,
    ) -> Result<Self, Error> {
        if folder_names.len() > std::u8::MAX.into() {
            return Err(Error::InvalidMessageLength);
        }
        Ok(Self::Success(SetBrowsedPlayerResponseParams { uid_counter, num_items, folder_names }))
    }

    #[cfg(test)]
    pub fn new_failure(status: StatusCode) -> Result<Self, Error> {
        if status == StatusCode::Success {
            return Err(Error::InvalidMessage);
        }
        Ok(Self::Failure(status))
    }
}

impl Decodable for SetBrowsedPlayerResponse {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < SetBrowsedPlayerResponse::MIN_FAILURE_PACKET_SIZE {
            return Err(Error::InvalidMessage);
        }

        let status = StatusCode::try_from(buf[0])?;
        match status {
            StatusCode::Success => {
                let success_resp = SetBrowsedPlayerResponseParams::decode(buf)?;
                Ok(Self::Success(success_resp))
            }
            code => Ok(Self::Failure(code)),
        }
    }
}

impl Decodable for SetBrowsedPlayerResponseParams {
    type Error = Error;

    // Given a SetBrowsedPlayerResponse message buf with supposed Success status,
    // it will try to decode the remaining response parameters.
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < SetBrowsedPlayerResponse::MIN_SUCCESS_PACKET_SIZE {
            return Err(Error::InvalidMessage);
        }

        // No need to process status since it would have processed as part
        // of SetBrowsedPlayerResponse.

        let uid_counter = u16::from_be_bytes(buf[1..3].try_into().unwrap());
        let num_items = u32::from_be_bytes(buf[3..7].try_into().unwrap());
        let is_utf8 = match CharsetId::try_from(u16::from_be_bytes(buf[7..9].try_into().unwrap())) {
            Ok(CharsetId::Utf8) => true,
            res => {
                warn!("Unsupported charset ID {:?}", res);
                false
            }
        };
        let folder_depth = buf[9];

        let mut next_idx = 10;
        let mut folder_names = Vec::with_capacity(folder_depth.into());

        for processed in 0..folder_depth {
            if buf.len() < next_idx + 2 {
                return Err(Error::InvalidMessage);
            }
            let name_len: usize =
                u16::from_be_bytes(buf[next_idx..next_idx + 2].try_into().unwrap()).into();
            if buf.len() < next_idx + 2 + name_len {
                return Err(Error::InvalidMessage);
            }
            // TODO(fxdev.bug/100467): add support to appropriately convert non-utf8
            // charset ID folder names to utf8 names.
            let name = if is_utf8 {
                let mut name_arr = vec![0; name_len];
                name_arr.copy_from_slice(&buf[next_idx + 2..next_idx + 2 + name_len]);
                String::from_utf8(name_arr).map_err(|_| Error::ParameterEncodingError)?
            } else {
                format!("Folder {:?}", processed + 1)
            };
            folder_names.push(name);
            next_idx += 2 + name_len;
        }

        if next_idx != buf.len() {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { uid_counter, num_items, folder_names })
    }
}

impl Encodable for SetBrowsedPlayerResponse {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        match &self {
            SetBrowsedPlayerResponse::Failure(_) => {
                SetBrowsedPlayerResponse::MIN_FAILURE_PACKET_SIZE
            }
            SetBrowsedPlayerResponse::Success(resp) => resp.encoded_len(),
        }
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        match &self {
            SetBrowsedPlayerResponse::Failure(r) => {
                buf[0] = u8::from(r);
                Ok(())
            }
            SetBrowsedPlayerResponse::Success(r) => r.encode(&mut buf[..]),
        }
    }
}

impl Encodable for SetBrowsedPlayerResponseParams {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        SetBrowsedPlayerResponse::MIN_SUCCESS_PACKET_SIZE
            + self.folder_names.iter().map(|name| 2 + name.len()).sum::<usize>()
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0] = u8::from(&StatusCode::Success);
        buf[1..3].copy_from_slice(&self.uid_counter.to_be_bytes());
        buf[3..7].copy_from_slice(&self.num_items.to_be_bytes());
        buf[7..9].copy_from_slice(&u16::from(&CharsetId::Utf8).to_be_bytes());
        buf[9] = u8::try_from(self.folder_names.len()).map_err(|_| Error::OutOfRange)?;
        let mut next_idx = 10;
        for name in &self.folder_names {
            buf[next_idx..next_idx + 2]
                .copy_from_slice(&(u16::try_from(name.len()).unwrap().to_be_bytes()));
            buf[next_idx + 2..next_idx + 2 + name.len()].copy_from_slice(&name.as_bytes());
            next_idx += 2 + name.len();
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[fuchsia::test]
    fn test_set_browsed_player_command_encode() {
        let cmd = SetBrowsedPlayerCommand::new(5);

        assert_eq!(cmd.player_id(), 5);
        assert_eq!(cmd.encoded_len(), 2);
        let mut buf = vec![0; cmd.encoded_len()];
        assert_eq!(cmd.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(buf, &[0x00, 0x05]);
    }

    #[fuchsia::test]
    fn test_set_browsed_player_command_decode_success() {
        let buf = [0x00, 0x02];
        let cmd = SetBrowsedPlayerCommand::decode(&buf[..]);
        let cmd = cmd.expect("Just checked");
        assert_eq!(cmd.player_id(), 2);
    }

    #[fuchsia::test]
    fn test_set_browsed_player_response_encode_success() {
        let uid_counter = 1;
        let num_items = 10;
        let folder_names = vec!["hi".to_string(), "bye".to_string()];
        let response = SetBrowsedPlayerResponse::new_success(uid_counter, num_items, folder_names)
            .expect("should create response");

        // 10 bytes for required parameters, 4 bytes for encoding "hi" folder
        // name length/name, 5 bytes for encoding "bye" folder name length/name.
        assert_eq!(response.encoded_len(), 19);

        let mut got = vec![0; response.encoded_len()];
        let _ = response.encode(&mut got[..]).expect("should have succeeded");

        let expected = [4, 0, 1, 0, 0, 0, 10, 0, 106, 2, 0, 2, 0x68, 0x69, 0, 3, 0x62, 0x79, 0x65];
        assert_eq!(got[..response.encoded_len()], expected);
    }

    #[fuchsia::test]
    fn test_set_browsed_player_response_bad_status_encode_success() {
        let status = StatusCode::InvalidParameter;
        let response =
            SetBrowsedPlayerResponse::new_failure(status).expect("should create response");

        // 1 byte for required parameters.
        assert_eq!(response.encoded_len(), 1);

        let mut got = vec![0; response.encoded_len()];
        let _ = response.encode(&mut got[..]).expect("should have succeeded");

        let expected = [1];
        assert_eq!(got[..response.encoded_len()], expected);
    }

    #[fuchsia::test]
    fn test_set_browsed_player_response_encode_fail() {
        // Improper folder name length.
        let _ = SetBrowsedPlayerResponse::new_success(1, 1, vec!["test".to_string(); 300])
            .expect_err("should have failed");

        // Improper status.
        let _ = SetBrowsedPlayerResponse::new_failure(StatusCode::Success)
            .expect_err("should have failed");

        // Insufficient buf.
        let resp =
            SetBrowsedPlayerResponse::new_success(1, 10, vec!["hi".to_string(), "bye".to_string()])
                .expect("should create response");
        let mut invalid_buf = vec![0; 5]; // insufficient buffer.
        assert!(resp.encode(&mut invalid_buf[..]).is_err());

        let resp = SetBrowsedPlayerResponse::new_failure(StatusCode::InvalidParameter)
            .expect("should create response");
        let mut invalid_buf = vec![0; 0]; // insufficient buffer.
        assert!(resp.encode(&mut invalid_buf[..]).is_err());
    }

    #[fuchsia::test]
    fn test_set_browsed_player_response_decode_success() {
        // Success response.
        let success_buf = [4, 0, 1, 0, 0, 0, 10, 0, 106, 0];
        let resp = SetBrowsedPlayerResponse::decode(&success_buf[..]);

        let resp = resp.expect("Just checked");
        assert_matches!(
            resp,
            SetBrowsedPlayerResponse::Success(r) => {
                assert_eq!(r.uid_counter, 1);
                assert_eq!(r.num_items, 10);
                assert!(r.folder_names.is_empty());
            }
        );

        // Failure response.
        let failure_buf = [1];
        let resp = SetBrowsedPlayerResponse::decode(&failure_buf[..]);

        let resp = resp.expect("Just checked");
        assert_matches!(resp, SetBrowsedPlayerResponse::Failure(StatusCode::InvalidParameter));
    }

    #[fuchsia::test]
    fn test_set_browsed_player_response_decode_with_folders_success() {
        // With Utf8 folder names.
        let buf = [4, 0, 1, 0, 0, 0, 10, 0, 106, 2, 0, 2, 0x41, 0x42, 0, 2, 0x43, 0x44];
        let resp = SetBrowsedPlayerResponse::decode(&buf[..]).expect("should have decoded");
        assert_matches!(
            resp,
            SetBrowsedPlayerResponse::Success(r) => {
                assert_eq!(r.uid_counter, 1);
                assert_eq!(r.num_items, 10);
                assert_eq!(r.folder_names, vec!["AB".to_string(), "CD".to_string()]);
            }
        );

        // With non-Utf8 folder names.
        let buf = [4, 0, 1, 0, 0, 0, 10, 0, 55, 1, 0, 2, 0x41, 0x42];
        let resp = SetBrowsedPlayerResponse::decode(&buf[..]).expect("should have decoded");
        assert_matches!(
            resp,
            SetBrowsedPlayerResponse::Success(r) => {
                assert_eq!(r.uid_counter, 1);
                assert_eq!(r.num_items, 10);
                assert_eq!(r.folder_names, vec!["Folder 1".to_string()]);
            }
        );
    }

    #[fuchsia::test]
    fn test_set_browsed_player_response_decode_invalid_buf() {
        // None-zero folder depth with no folder names.
        let buf = [4, 0, 1, 0, 0, 0, 10, 0, 106, 1];
        let cmd = SetBrowsedPlayerResponse::decode(&buf[..]);
        let _ = cmd.expect_err("should not have decoded successfully");
    }

    #[fuchsia::test]
    fn test_set_browsed_player_response_decode_invalid_folder_names() {
        // None-zero folder depth with folder name that is too long.
        let buf = [4, 0, 1, 0, 0, 0, 10, 0, 106, 1, 0, 1, 0x41, 0x42];
        let cmd = SetBrowsedPlayerResponse::decode(&buf[..]);
        let _ = cmd.expect_err("should not have decoded successfully");

        // None-zero folder depth with folder name that is too long.
        let buf = [4, 0, 1, 0, 0, 0, 10, 0, 106, 1, 0, 2, 0x41];
        let cmd = SetBrowsedPlayerResponse::decode(&buf[..]);
        let _ = cmd.expect_err("should not have decoded successfully");

        // Zero folder depth with folder names.
        let buf = [4, 0, 1, 0, 0, 0, 10, 0, 106, 0, 0, 1, 0x41];
        let cmd = SetBrowsedPlayerResponse::decode(&buf[..]);
        let _ = cmd.expect_err("should not have decoded successfully");
    }
}

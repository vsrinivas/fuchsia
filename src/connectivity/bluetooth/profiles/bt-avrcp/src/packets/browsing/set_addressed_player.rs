// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    packet_encoding::{Decodable, Encodable},
    std::convert::TryFrom,
};

use crate::packets::{
    AvcCommandType, Error, PacketResult, PduId, StatusCode, VendorCommand, VendorDependentPdu,
};

/// AVRCP 1.6.1 section 6.9.1 SetAddressedPlayer.
#[derive(Debug)]
pub struct SetAddressedPlayerCommand {
    player_id: u16,
}

impl SetAddressedPlayerCommand {
    #[cfg(test)]
    pub fn new(player_id: u16) -> Self {
        Self { player_id }
    }

    pub fn player_id(&self) -> u16 {
        self.player_id
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for SetAddressedPlayerCommand {
    fn pdu_id(&self) -> PduId {
        PduId::SetAddressedPlayer
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for SetAddressedPlayerCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

impl Decodable for SetAddressedPlayerCommand {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessage);
        }

        let mut temp = [0; 2];
        temp.copy_from_slice(&buf[0..2]);
        let player_id = u16::from_be_bytes(temp);

        Ok(Self { player_id })
    }
}

impl Encodable for SetAddressedPlayerCommand {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        2
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0..2].copy_from_slice(&self.player_id.to_be_bytes());
        Ok(())
    }
}

/// AVRCP 1.6.1 section 6.9.1 SetAddressedPlayer.
#[derive(Debug)]
pub struct SetAddressedPlayerResponse {
    status: StatusCode,
}

impl SetAddressedPlayerResponse {
    pub fn new(status: StatusCode) -> Self {
        Self { status }
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for SetAddressedPlayerResponse {
    fn pdu_id(&self) -> PduId {
        PduId::SetAddressedPlayer
    }
}

impl Decodable for SetAddressedPlayerResponse {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        let status = StatusCode::try_from(buf[0])?;

        Ok(Self { status })
    }
}

impl Encodable for SetAddressedPlayerResponse {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(&self.status);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_set_addressed_player_encode() {
        let cmd = SetAddressedPlayerCommand::new(5);

        assert_eq!(cmd.encoded_len(), 2);
        let mut buf = vec![0; cmd.encoded_len()];
        assert_eq!(cmd.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(buf, &[0x00, 0x05]);
    }

    #[test]
    fn test_set_addressed_player_decode_success() {
        let buf = [0x00, 0x02];
        let cmd = SetAddressedPlayerCommand::decode(&buf[..]);
        assert!(cmd.is_ok());
        let cmd = cmd.expect("Just checked");
        assert_eq!(cmd.player_id, 2);
    }

    #[test]
    /// Decoding a packet that is malformed.
    fn test_set_addressed_player_decode_invalid_buf() {
        let buf = [0x00];
        let cmd = SetAddressedPlayerCommand::decode(&buf[..]);
        assert!(cmd.is_err());
    }
}

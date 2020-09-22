// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

/// AVRCP 1.6.1 SetAbsoluteVolume. Command encodes a requested volume. Response returns the volume
/// actually set.
#[derive(Debug)]
pub struct SetAbsoluteVolumeCommand {
    requested_volume: u8,
}

impl SetAbsoluteVolumeCommand {
    pub fn new(requested_volume: u8) -> Result<SetAbsoluteVolumeCommand, Error> {
        if requested_volume >= 0x80 {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { requested_volume })
    }

    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn volume(self) -> u8 {
        self.requested_volume
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for SetAbsoluteVolumeCommand {
    fn pdu_id(&self) -> PduId {
        PduId::SetAbsoluteVolume
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for SetAbsoluteVolumeCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

impl Decodable for SetAbsoluteVolumeCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { requested_volume: buf[0] & 0x7f }) // Ignore the reserved bit
    }
}

impl Encodable for SetAbsoluteVolumeCommand {
    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessageLength);
        }

        // volume can only be set to between and 0-127.
        if self.requested_volume >= 0x80 {
            return Err(Error::InvalidMessage);
        }

        buf[0] = self.requested_volume;
        Ok(())
    }
}

/// AVRCP 1.6.1 SetAbsoluteVolume. Command encodes a requested volume. Response returns the volume
/// actually set.
#[derive(Debug)]
pub struct SetAbsoluteVolumeResponse {
    set_volume: u8,
}

impl SetAbsoluteVolumeResponse {
    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn new(volume: u8) -> Result<SetAbsoluteVolumeResponse, Error> {
        if volume >= 0x80 {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { set_volume: volume })
    }

    pub fn volume(self) -> u8 {
        self.set_volume
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for SetAbsoluteVolumeResponse {
    fn pdu_id(&self) -> PduId {
        PduId::SetAbsoluteVolume
    }
}

impl Decodable for SetAbsoluteVolumeResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessageLength);
        }

        Ok(Self { set_volume: buf[0] & 0x7f }) // Ignore the reserved bit
    }
}

impl Encodable for SetAbsoluteVolumeResponse {
    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < 1 {
            return Err(Error::BufferLengthOutOfRange);
        }

        // volume can only be set to between and 0-127.
        if self.set_volume >= 0x80 {
            return Err(Error::InvalidMessage);
        }

        buf[0] = self.set_volume;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn set_absolute_volume_command_encode() {
        let b = SetAbsoluteVolumeCommand::new(0x60).expect("unable to encode packet");
        assert_eq!(b.encoded_len(), 1);
        assert_eq!(b.command_type(), AvcCommandType::Control);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x60]);
    }

    #[test]
    fn set_absolute_volume_response_encode() {
        let b = SetAbsoluteVolumeResponse::new(0x60).expect("unable to encode packet");
        assert_eq!(b.encoded_len(), 1);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x60]);
    }

    #[test]
    fn set_absolute_volume_command_error_invalid_volume() {
        let b = SetAbsoluteVolumeResponse::new(0x81);
        assert_eq!(b.expect_err("expected error"), Error::InvalidMessage);
    }

    #[test]
    fn set_absolute_volume_response_error_invalid_volume() {
        let b = SetAbsoluteVolumeCommand::new(0x81);
        assert_eq!(b.expect_err("expected error"), Error::InvalidMessage);
    }

    #[test]
    fn set_absolute_volume_command_decode() {
        let b = SetAbsoluteVolumeCommand::decode(&[0x60]).expect("unable to decode packet");
        assert_eq!(b.encoded_len(), 1);
        assert_eq!(b.volume(), 0x60);
    }

    #[test]
    fn set_absolute_volume_response_decode() {
        let b = SetAbsoluteVolumeResponse::decode(&[0x60]).expect("unable to decode packet");
        assert_eq!(b.encoded_len(), 1);
        assert_eq!(b.volume(), 0x60);
    }

    #[test]
    fn set_absolute_volume_command_decode_ignore_reserved_bit() {
        // reserved bit set.
        let b = SetAbsoluteVolumeCommand::decode(&[0x89]).expect("unable to decode packet");
        assert_eq!(b.volume(), 9);
    }

    #[test]
    fn set_absolute_volume_command_response_ignore_reserved_bit() {
        // reserved bit set.
        let b = SetAbsoluteVolumeResponse::decode(&[0x89]).expect("unable to decode packet");
        assert_eq!(b.volume(), 9);
    }
}

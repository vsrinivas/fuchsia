// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    packet_encoding::{decodable_enum, Decodable, Encodable},
    std::convert::TryInto,
};

mod change_path;
mod get_folder_items;
mod get_total_items;
mod set_addressed_player;
mod set_browsed_player;

pub use self::{
    change_path::*, get_folder_items::*, get_total_items::*, set_addressed_player::*,
    set_browsed_player::*,
};
use crate::packets::{Error, PacketResult, PduId, StatusCode};

decodable_enum! {
    pub enum Scope <u8, Error, OutOfRange> {
        MediaPlayerList = 0x00,
        MediaPlayerVirtualFilesystem = 0x01,
        Search = 0x02,
        NowPlaying = 0x03,
    }
}

/// The packet structure of a Browse channel command.
pub struct BrowsePreamble {
    pub pdu_id: u8,
    pub parameter_length: u16,
    pub body: Vec<u8>,
}

impl BrowsePreamble {
    pub fn new(pdu_id: u8, body: Vec<u8>) -> Self {
        Self { pdu_id, parameter_length: body.len() as u16, body }
    }

    /// General reject packet. AVRCP 1.6, Table 4.5 and Section 6.15.2.1.1.
    pub fn general_reject(status_code: StatusCode) -> Self {
        let general_reject_pdu_id = u8::from(&PduId::GeneralReject);
        let parameter_length = 1;
        let body = [u8::from(&status_code)].to_vec();

        Self { pdu_id: general_reject_pdu_id, parameter_length, body }
    }
}

impl Decodable for BrowsePreamble {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 3 {
            return Err(Error::InvalidMessage);
        }

        let pdu_id = buf[0];
        let parameter_length = u16::from_be_bytes(buf[1..3].try_into().unwrap());
        let body = buf[3..].to_vec();

        if parameter_length as usize != body.len() {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { pdu_id, parameter_length, body })
    }
}

impl Encodable for BrowsePreamble {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        3 + self.parameter_length as usize
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessage);
        }

        buf[0] = self.pdu_id;
        buf[1..3].copy_from_slice(&self.parameter_length.to_be_bytes());

        if self.body.len() < self.parameter_length as usize {
            return Err(Error::InvalidMessage);
        }

        buf[3..].copy_from_slice(&self.body);

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    /// Tests encoding an AVCTP packet works as expected.
    fn test_avctp_packet_encode_success() {
        let packet = BrowsePreamble::new(10, vec![0x1, 0x2]);

        // 3 bytes for header, 2 bytes for payload.
        assert_eq!(packet.encoded_len(), 5);
        let mut buf = vec![0; packet.encoded_len()];
        assert_eq!(packet.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(buf, &[10, 0x00, 0x02, 0x01, 0x02]);
    }

    #[test]
    fn test_avctp_packet_decode_success() {
        let packet = [0x74, 0x00, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05];
        let avctp = BrowsePreamble::decode(&packet[..]);
        assert!(avctp.is_ok());
        let avctp = avctp.expect("Just checked");

        assert_eq!(avctp.pdu_id, 0x74);
        assert_eq!(avctp.parameter_length, 5);
        assert_eq!(avctp.body, vec![0x01, 0x02, 0x03, 0x04, 0x05]);
    }
}

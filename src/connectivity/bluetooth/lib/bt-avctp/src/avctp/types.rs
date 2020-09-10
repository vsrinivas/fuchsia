// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;

use crate::{pub_decodable_enum, Decodable, Encodable, Error, Result};

/// An AVCTP Transaction Label
/// Not used outside the library. Public as part of some internal Error variants.
/// See Section 6.1.1
#[derive(Debug, Clone, PartialEq)]
pub struct TxLabel(u8);

// Transaction labels are only 4 bits.
const MAX_TX_LABEL: u8 = 0xF;

impl TryFrom<u8> for TxLabel {
    type Error = Error;
    fn try_from(value: u8) -> Result<Self> {
        if value > MAX_TX_LABEL {
            Err(Error::OutOfRange)
        } else {
            Ok(TxLabel(value))
        }
    }
}

impl From<&TxLabel> for u8 {
    fn from(v: &TxLabel) -> u8 {
        v.0
    }
}

impl From<&TxLabel> for usize {
    fn from(v: &TxLabel) -> usize {
        v.0 as usize
    }
}

/// An AVCTP Profile Identifier
/// The type indicates the how the command/request frame is encoded. It should be identical to the
/// 16bit UUID of the service class for this profile.
/// See Section 6.1.1
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct ProfileId([u8; 2]);

/// 16bit UUID for "A/V Remote Control" assigned by the Bluetooth assigned numbers document
pub(crate) const AV_REMOTE_PROFILE: &ProfileId = &ProfileId([0x11, 0x0e]);

impl From<[u8; 2]> for ProfileId {
    fn from(value: [u8; 2]) -> Self {
        Self(value)
    }
}

pub_decodable_enum! {
    /// Indicates whether this packet is part of a fragmented packet set.
    /// See Section 6.1
    PacketType<u8, Error, OutOfRange> {
        Single => 0x00,
        Start => 0x01,
        Continue => 0x02,
        End => 0x03,
    }
}

pub_decodable_enum! {
    /// Specifies the type of the packet as being either Command or Response
    /// See Section 6.1.1
    MessageType<u8, Error, OutOfRange> {
        Command => 0x00,
        Response => 0x01,
    }
}

#[derive(Debug)]
pub struct Header {
    label: TxLabel,            // byte 0, bit 7..4
    packet_type: PacketType,   // byte 0, bit 3..2
    message_type: MessageType, // byte 0, bit 1
    invalid_profile_id: bool,  // byte 0, bit 0
    num_packets: u8,           // byte 1 if packet type == start
    profile_id: ProfileId,     // byte 1..2 (byte 2..3 if packet type is start)
}

impl Header {
    pub(crate) fn new(
        label: TxLabel,
        profile_id: ProfileId,
        message_type: MessageType,
        invalid_profile_id: bool,
    ) -> Header {
        Header {
            label,
            profile_id,
            message_type,
            packet_type: PacketType::Single,
            invalid_profile_id,
            num_packets: 1,
        }
    }

    /// Creates a new header from this header with it's message type set to response.
    pub(crate) fn create_response(&self, packet_type: PacketType) -> Header {
        Header {
            label: self.label.clone(),
            profile_id: self.profile_id.clone(),
            message_type: MessageType::Response,
            packet_type,
            invalid_profile_id: false,
            num_packets: 1,
        }
    }

    /// Creates a new header from this header with it's message type set to response
    /// and with the ipid (invalid profile id) bit set to true.
    pub(crate) fn create_invalid_profile_id_response(&self) -> Header {
        Header {
            label: self.label.clone(),
            profile_id: self.profile_id.clone(),
            message_type: MessageType::Response,
            packet_type: PacketType::Single,
            invalid_profile_id: true,
            num_packets: 1,
        }
    }

    pub(crate) fn label(&self) -> &TxLabel {
        &self.label
    }

    pub(crate) fn profile_id(&self) -> &ProfileId {
        &self.profile_id
    }

    pub fn message_type(&self) -> &MessageType {
        &self.message_type
    }

    pub fn packet_type(&self) -> &PacketType {
        &self.packet_type
    }

    pub fn is_invalid_profile_id(&self) -> bool {
        self.invalid_profile_id
    }

    // convenience helpers
    pub fn is_type(&self, other: &MessageType) -> bool {
        &self.message_type == other
    }

    pub fn is_single(&self) -> bool {
        self.packet_type == PacketType::Single
    }
}

impl Decodable for Header {
    fn decode(bytes: &[u8]) -> Result<Header> {
        if bytes.len() < 3 {
            return Err(Error::OutOfRange);
        }
        let label = TxLabel::try_from(bytes[0] >> 4)?;
        let packet_type = PacketType::try_from((bytes[0] >> 2) & 0x3)?;
        let (id_offset, num_packets) = match packet_type {
            PacketType::Start => {
                if bytes.len() < 4 {
                    return Err(Error::OutOfRange);
                }
                (2, bytes[1])
            }
            _ => (1, 1),
        };

        let profile_id = ProfileId::from([bytes[id_offset], bytes[id_offset + 1]]);
        let invalid_profile_id = bytes[0] & 0x1 == 1;
        let header = Header {
            label,
            profile_id,
            message_type: MessageType::try_from(bytes[0] >> 1 & 0x1)?,
            packet_type,
            invalid_profile_id,
            num_packets,
        };
        Ok(header)
    }
}

impl Encodable for Header {
    fn encoded_len(&self) -> usize {
        match self.packet_type {
            PacketType::Start => 4,
            _ => 3,
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::Encoding);
        }
        let invalid_profile_id: u8 = if self.invalid_profile_id { 1 } else { 0 };
        buf[0] = u8::from(&self.label) << 4
            | u8::from(&self.packet_type) << 2
            | u8::from(&self.message_type) << 1
            | invalid_profile_id;
        let mut buf_idx = 1;
        if self.packet_type == PacketType::Start {
            buf[buf_idx] = self.num_packets;
            buf_idx = 2;
        }
        let profile_id = self.profile_id.0;
        buf[buf_idx] = profile_id[0];
        buf[buf_idx + 1] = profile_id[1];
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    /// Test Header encoding
    fn test_header_encode() {
        let header =
            Header::new(TxLabel(0), AV_REMOTE_PROFILE.clone(), MessageType::Command, false);
        assert!(!header.is_invalid_profile_id());
        assert!(header.is_single());
        assert!(header.is_type(&MessageType::Command));
        assert_eq!(TxLabel(0), *header.label());
        let len = header.encoded_len();
        assert_eq!(3, len);
        let mut buf = vec![0; len];
        assert!(header.encode(&mut buf[..]).is_ok());

        assert_eq!(
            &[
                0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
                0x11, // AV PROFILE
                0x0e, // AV PROFILE
            ],
            &buf[..]
        );
    }

    #[test]
    /// Test Header encoding
    fn test_header_encode_response() {
        let header =
            Header::new(TxLabel(15), AV_REMOTE_PROFILE.clone(), MessageType::Command, false);
        let header = header.create_response(PacketType::Single);
        assert!(!header.is_invalid_profile_id());
        assert!(header.is_single());
        assert!(header.is_type(&MessageType::Response));
        assert_eq!(TxLabel(15), *header.label());
        let len = header.encoded_len();
        assert_eq!(3, len);
        let mut buf = vec![0; len];
        assert!(header.encode(&mut buf[..]).is_ok());

        assert_eq!(
            &[
                0xf2, // TxLabel 15, Single 0, Response 1, Ipid 0
                0x11, // AV PROFILE
                0x0e, // AV PROFILE
            ],
            &buf[..]
        );
    }

    #[test]
    /// Test Header encoding
    fn test_header_encode_invalid_profile_response() {
        let header =
            Header::new(TxLabel(0), AV_REMOTE_PROFILE.clone(), MessageType::Command, false);
        let header = header.create_invalid_profile_id_response();
        assert!(header.is_invalid_profile_id());
        assert!(header.is_single());
        assert!(header.is_type(&MessageType::Response));
        assert_eq!(TxLabel(0), *header.label());
        let len = header.encoded_len();
        assert_eq!(3, len);
        let mut buf = vec![0; len];
        assert!(header.encode(&mut buf[..]).is_ok());

        assert_eq!(
            &[
                0x03, // TxLabel 0, Single 0, Response 1, Ipid 1
                0x11, // AV PROFILE
                0x0e, // AV PROFILE
            ],
            &buf[..]
        );
    }

    #[test]
    /// Test Header decoding
    fn test_header_decode_invalid_packet_response() {
        let header = Header::decode(&[
            0xf3, // TxLabel 15, Single 0, Response 1, Ipid 1
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            0x12, // extra ignored
            0x34, // extra ignored
            0x45, // extra ignored
        ])
        .expect("unable to decode header");
        assert!(header.is_invalid_profile_id());
        assert!(header.is_single());
        assert!(header.is_type(&MessageType::Response));
        assert_eq!(TxLabel(15), *header.label());
    }

    #[test]
    /// Test Header decoding
    fn test_header_decode_command() {
        let header = Header::decode(&[
            0x80, // TxLabel 8, Single 0, Command 0, Ipid 0
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            0x34, // extra ignored
            0x45, // extra ignored
        ])
        .expect("unable to decode header");
        assert!(!header.is_invalid_profile_id());
        assert!(header.is_single());
        assert!(header.is_type(&MessageType::Command));
        assert_eq!(TxLabel(8), *header.label());
    }

    #[test]
    /// Test Header decoding
    fn test_header_decode_invalid() {
        assert_eq!(
            Error::OutOfRange,
            Header::decode(&[
                0x80, // TxLabel 8, Single 0, Command 0, Ipid 0
                0x11, // AV PROFILE
                      // missing fields
            ])
            .unwrap_err()
        );
    }

    #[test]
    fn txlabel_tofrom_u8() {
        let mut label: Result<TxLabel> = TxLabel::try_from(15);
        assert!(label.is_ok());
        assert_eq!(15, u8::from(&label.unwrap()));
        label = TxLabel::try_from(16);
        assert_eq!(Err(Error::OutOfRange), label);
    }

    #[test]
    fn txlabel_to_usize() {
        let label = TxLabel::try_from(1).unwrap();
        assert_eq!(1, usize::from(&label));
    }
}

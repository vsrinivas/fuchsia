// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::convert::TryFrom;

use crate::{pub_decodable_enum, Decodable, Encodable, Error, Result};

pub_decodable_enum! {
    /// AV/C Command and Response types.
    /// See AV/C General Specification Section 5.3.1 and 5.3.2
    CommandType<u8, Error, OutOfRange> {
        Control => 0x00,
        Status => 0x01,
        SpecificInquiry => 0x02,
        Notify => 0x03,
        GeneralInquiry => 0x04, // Unused with bt?
    }
}

pub_decodable_enum! {
    /// AV/C Command and Response types.
    /// See AV/C General Specification Section 5.3.1 and 5.3.2
    ResponseType<u8, Error, OutOfRange> {
        NotImplemented => 0x08,
        Accepted => 0x09,
        Rejected => 0x0a,
        InTransition => 0x0b, // Unused with bt?
        ImplementedStable => 0x0c,
        Changed => 0x0d,
        Interim => 0x0f,
    }
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum PacketType {
    Command(CommandType),
    Response(ResponseType),
}

impl PacketType {
    pub fn try_from(val: u8) -> Result<Self> {
        if val < 0x08 {
            Ok(PacketType::Command(CommandType::try_from(val)?))
        } else {
            Ok(PacketType::Response(ResponseType::try_from(val)?))
        }
    }

    pub fn raw_value(&self) -> u8 {
        match self {
            PacketType::Command(x) => u8::from(x),
            PacketType::Response(x) => u8::from(x),
        }
    }
}

pub_decodable_enum! {
    /// AV/C Op Codes
    /// See AV/C General Specification Section 5.3.1
    OpCode<u8, Error, OutOfRange> {
        VendorDependent => 0x00,
        UnitInfo => 0x30,
        SubUnitInfo => 0x31,
        Passthrough => 0x7c,
    }
}

pub_decodable_enum! {
    /// Most common subunits from the AV/C General Specification in AVRCP
    /// All AVRCP commands are transacted on the panel subunit according to the Panel Specification
    SubunitType<u8, Error, OutOfRange> {
        Panel => 0x09,
        Unit => 0x1F,
    }
}

/// An AVC Vendor Company Identifier
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct CompanyId([u8; 3]);

pub(crate) const BT_SIG_COMPANY_ID: CompanyId = CompanyId([0x00, 0x19, 0x58]);

impl TryFrom<&[u8]> for CompanyId {
    type Error = Error;

    fn try_from(value: &[u8]) -> Result<Self> {
        if value.len() < 3 {
            return Err(Error::OutOfRange);
        }
        let mut buf: [u8; 3] = [0; 3];
        buf.copy_from_slice(&value[0..3]);
        Ok(Self(buf))
    }
}

/// AVC Command and Response frames use the same layout with different command values
#[derive(Debug)]
pub struct Header {
    packet_type: PacketType,       // byte 0, bit 3..0
    subunit_type: u8,              // byte 1, bit 7..3
    subunit_id: u8,                // byte 1, bit 2..0
    op_code: OpCode,               // byte 2
    company_id: Option<CompanyId>, // byte 3-5 (only vendor dependent packets)
}

impl Header {
    pub(crate) fn new(
        command_type: CommandType,
        subunit_type: u8,
        subunit_id: u8,
        op_code: OpCode,
        company_id: Option<CompanyId>,
    ) -> Header {
        Header {
            packet_type: PacketType::Command(command_type),
            subunit_type,
            subunit_id,
            op_code,
            company_id,
        }
    }

    /// Creates a new Header with all the same fields but with a new response command type
    pub(crate) fn create_response(&self, response_type: ResponseType) -> Result<Header> {
        Ok(Header {
            packet_type: PacketType::Response(response_type),
            subunit_type: self.subunit_type,
            subunit_id: self.subunit_id,
            op_code: self.op_code,
            company_id: self.company_id.clone(),
        })
    }

    pub fn packet_type(&self) -> PacketType {
        self.packet_type
    }

    pub fn op_code(&self) -> &OpCode {
        &self.op_code
    }

    pub fn subunit_type(&self) -> Option<SubunitType> {
        match SubunitType::try_from(self.subunit_type) {
            Ok(x) => Some(x),
            Err(_) => None,
        }
    }
}

impl Encodable for Header {
    fn encoded_len(&self) -> usize {
        if self.op_code == OpCode::VendorDependent {
            6
        } else {
            3
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::Encoding);
        }
        buf[0] = self.packet_type.raw_value();
        buf[1] = (self.subunit_type << 3) | (self.subunit_id & 0x7);
        buf[2] = u8::from(&self.op_code);
        if self.op_code == OpCode::VendorDependent {
            let company_id = match self.company_id {
                Some(ref x) => <[u8; 3]>::from(x.0),
                None => return Err(Error::InvalidHeader),
            };
            buf[3..6].copy_from_slice(&company_id);
        }
        Ok(())
    }
}

impl Decodable for Header {
    fn decode(bytes: &[u8]) -> Result<Header> {
        if bytes.len() < 3 {
            return Err(Error::InvalidHeader);
        }
        if bytes[0] >> 4 != 0 {
            // Upper 4 bits should be zero.
            return Err(Error::InvalidHeader);
        }

        let packet_type = PacketType::try_from(bytes[0]).map_err(|_| Error::InvalidHeader)?;
        let subunit_type = bytes[1] >> 3;
        let subunit_id = bytes[1] & 0x7;
        let op_code = OpCode::try_from(bytes[2]).map_err(|_| Error::InvalidHeader)?;
        let mut company_id = None;
        if op_code == OpCode::VendorDependent {
            if bytes.len() < 6 {
                return Err(Error::InvalidHeader);
            }
            company_id = Some(CompanyId::try_from(&bytes[3..6]).map_err(|_| Error::InvalidHeader)?);
        }
        Ok(Header { packet_type, subunit_type, subunit_id, op_code, company_id })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    /// Test Header vendor dependent encoding
    fn test_header_encode_vendor_dependent() {
        let header = Header::new(
            CommandType::Notify,
            9,
            0,
            OpCode::VendorDependent,
            Some(BT_SIG_COMPANY_ID),
        );
        assert_eq!(Some(SubunitType::Panel), header.subunit_type());
        assert_eq!(PacketType::Command(CommandType::Notify), header.packet_type());
        assert_eq!(&OpCode::VendorDependent, header.op_code());
        let len = header.encoded_len();
        assert_eq!(6, len);
        let mut buf = vec![0; len];
        assert!(header.encode(buf.as_mut_slice()).is_ok());

        assert_eq!(
            &[
                0x03, // notify
                0x48, // panel subunit_type 9 (<< 3), subunit_id 0
                0x00, // op code vendor dependent
                0x00, 0x19, 0x58 // bit sig company id
            ],
            &buf[..]
        )
    }

    #[test]
    /// Test Header passthrough encoding
    fn test_header_encode_passthrough() {
        let header = Header::new(CommandType::Control, 9, 0, OpCode::Passthrough, None);
        assert_eq!(header.subunit_type().unwrap(), SubunitType::Panel);
        assert_eq!(&OpCode::Passthrough, header.op_code());
        let len = header.encoded_len();
        assert_eq!(3, len);
        let mut buf = vec![0; len];
        assert!(header.encode(&mut buf[..]).is_ok());

        assert_eq!(
            &[
                0x00, // control
                0x48, // panel subunit_type 9 (<< 3), subunit_id 0
                0x7c, // op code passthrough
            ],
            &buf[..]
        )
    }

    #[test]
    /// Test Header decoding
    fn test_header_decode_passthrough_command() {
        let header = Header::decode(&[
            0x00, // control
            0x48, // panel subunit_type 9 (<< 3), subunit_id 0
            0x7c, // op code passthrough
            0x12, // body should be ignored
            0x34, // body should be ignored
            0x56, // body should be ignored
            0x67, // body should be ignored
        ])
        .expect("Error decoding packet");

        assert_eq!(PacketType::Command(CommandType::Control), header.packet_type());
        assert_eq!(Some(SubunitType::Panel), header.subunit_type());
        assert_eq!(&OpCode::Passthrough, header.op_code());
    }

    #[test]
    /// Test Header decoding
    fn test_header_decode_passthrough_response() {
        let header = Header::decode(&[
            0x09, // accepted
            0x48, // panel subunit_type 9 (<< 3), subunit_id 0
            0x7c, // op code passthrough
            0x12, // body should be ignored
            0x34, // body should be ignored
            0x56, // body should be ignored
            0x67, // body should be ignored
        ])
        .expect("Error decoding packet");

        assert_eq!(PacketType::Response(ResponseType::Accepted), header.packet_type());
        assert_eq!(header.subunit_type().unwrap(), SubunitType::Panel);
        assert_eq!(&OpCode::Passthrough, header.op_code());
    }

    #[test]
    /// Test Header decoding
    fn test_header_decode_invalid_ctype() {
        assert_eq!(
            Header::decode(&[
                0x05, // invalid CType
                0x48, // panel subunit_type 9 (<< 3), subunit_id 0
                0x7c, // op code passthrough
                0x12, // body should be ignored
                0x34, // body should be ignored
                0x56, // body should be ignored
                0x67, // body should be ignored
            ])
            .unwrap_err(),
            Error::InvalidHeader
        );
    }

    #[test]
    /// Test Header decoding
    fn test_header_decode_partial() {
        assert_eq!(
            Header::decode(&[
                0x0c, // stable
                0x48, // panel subunit_type 9 (<< 3), subunit_id 0
                0x00, // op vendor dependent
                      // missing company id
            ])
            .unwrap_err(),
            Error::InvalidHeader
        );
    }
}

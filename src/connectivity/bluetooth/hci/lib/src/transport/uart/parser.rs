// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Primitive HCI Packet parsing tailored specifically to the fuchsia's UART transport use-case.
//! Because the UART `Transport` implementation uses a VecDeque rather than a data structure that
//! guarantees all data is in a single contiguous region of memory, an Iterator input is used.
//!
//! This implementation is not suitable to more general packet parsing for two reasons:
//!   1) It does not have zero-copy semantics. Most zero-copy strategies expect a single contiguous
//!      buffer as the underlying backing buffer.
//!   2) It does not parse or otherwise validate the contents of any HCI header field except for the
//!      packet length.

use super::parse_util::{copy_until_full, slices_from_range};
use crate::transport::{IncomingPacket, OutgoingPacket};
use std::collections::VecDeque;

// Indicator field values. See Bluetooth Core Spec v5.1, Vol 4, Part A, Table 2.1
const CMD_INDICATOR: u8 = 1;
const ACL_INDICATOR: u8 = 2;
#[allow(unused)] // Unused until SCO is implemented
const SCO_INDICATOR: u8 = 3;
const EVENT_INDICATOR: u8 = 4;

/// Error encountered while parsing an HCI packet from raw bytes read from the UART.
#[derive(thiserror::Error, Debug, PartialEq, Eq)]
pub enum ParseError {
    #[error("Invalid packet indicator byte {0}")]
    InvalidPacketIndicator(u8),
    #[error("HCI packet payload too short")]
    PayloadTooShort,
}

/// Single u8 value is a hci packet type indicator.
#[derive(Clone, Copy)]
pub(super) struct UartHeader(IncomingPacketIndicator);

#[repr(u8)]
#[derive(Clone, Copy)]
pub(super) enum IncomingPacketIndicator {
    Event = EVENT_INDICATOR,
    Acl = ACL_INDICATOR,
}

impl UartHeader {
    /// The UART transport frames each HCI packet with a single byte indicating packet type
    pub(super) const SIZE: usize = 1;

    /// Try to copy a complete IncomingHciHeader from the passed in iterator, interpreting the
    /// contained packet type from the packet indicator in this UartHeader.
    pub fn try_hdr_from_iter(
        self,
        iter: impl ExactSizeIterator<Item = u8>,
    ) -> Result<IncomingHciHeader, ParseError> {
        match self.0 {
            IncomingPacketIndicator::Acl => {
                AclHeader::try_from_iter(iter).map(IncomingHciHeader::Acl)
            }
            IncomingPacketIndicator::Event => {
                EventHeader::try_from_iter(iter).map(IncomingHciHeader::Event)
            }
        }
    }

    pub fn try_from_iter(mut iter: impl ExactSizeIterator<Item = u8>) -> Result<Self, ParseError> {
        match iter.next() {
            Some(EVENT_INDICATOR) => Ok(Self(IncomingPacketIndicator::Event)),
            Some(ACL_INDICATOR) => Ok(Self(IncomingPacketIndicator::Acl)),
            Some(b) => Err(ParseError::InvalidPacketIndicator(b)),
            None => Err(ParseError::PayloadTooShort),
        }
    }
}

/// Enum covering the two types of incoming packets that are supported by the fuchsia Bluetooth
/// stack.
#[derive(Clone, Copy)]
pub(super) enum IncomingHciHeader {
    Event(EventHeader),
    Acl(AclHeader),
}

impl IncomingHciHeader {
    /// Return a view to the underlying bytes
    pub fn bytes(&self) -> &[u8] {
        match self {
            IncomingHciHeader::Acl(hdr) => hdr.bytes(),
            IncomingHciHeader::Event(hdr) => hdr.bytes(),
        }
    }

    /// The length of the Hci packet body parsed from the header.
    pub fn body_len(self) -> usize {
        match self {
            IncomingHciHeader::Acl(hdr) => hdr.body_len(),
            IncomingHciHeader::Event(hdr) => hdr.body_len(),
        }
    }

    /// Return a builder function for creating an `IncomingPacket` based on the type of the
    /// packet specified by this header. The returned builder will take a buffer containing
    /// the complete header + body of the hci packet as an argument.
    pub fn incoming_packet_builder(&self) -> impl Fn(Vec<u8>) -> IncomingPacket {
        match self {
            IncomingHciHeader::Acl(_) => IncomingPacket::Acl,
            IncomingHciHeader::Event(_) => IncomingPacket::Event,
        }
    }
}

#[cfg(test)]
impl IncomingHciHeader {
    pub fn to_event(self) -> EventHeader {
        match self {
            IncomingHciHeader::Event(hdr) => hdr,
            _ => panic!("Not event packet"),
        }
    }
    pub fn to_acl(self) -> AclHeader {
        match self {
            IncomingHciHeader::Acl(hdr) => hdr,
            _ => panic!("Not acl packet"),
        }
    }
}

#[derive(Clone, Copy)]
pub(super) struct EventHeader {
    bytes: [u8; Self::SIZE],
}

impl EventHeader {
    /// Event packets have a 2 byte header. See Bluetooth Core Spec v5.1, Vol 2, Part E, Section
    /// 5.4.4
    pub const SIZE: usize = 2;
    // Location of the packet length field in the header
    const BODY_LEN_OFFSET: usize = 1;

    /// Try to copy a complete EventHeader from the passed in iterator.
    pub fn try_from_iter(iter: impl ExactSizeIterator<Item = u8>) -> Result<Self, ParseError> {
        let mut bytes = [0u8; Self::SIZE];
        match copy_until_full(iter, bytes.iter_mut()) {
            Ok(()) => Ok(Self { bytes }),
            Err(()) => Err(ParseError::PayloadTooShort),
        }
    }

    /// Return a view to the underlying bytes
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    /// The length of the Event packet body parsed from the header.
    pub fn body_len(&self) -> usize {
        self.bytes[Self::BODY_LEN_OFFSET].into()
    }
}

#[derive(Clone, Copy)]
pub(super) struct AclHeader {
    bytes: [u8; Self::SIZE],
}

impl AclHeader {
    /// ACL Data packets have a 4 byte header. See Bluetooth Core Spec v5.1, Vol 2, Part E, Section
    /// 5.4.2
    pub const SIZE: usize = 4;
    // Location of the low bits of the body length field within the header
    const BODY_LEN_LO: usize = 2;
    // Location of the high bits of the body length field within the header
    const BODY_LEN_HI: usize = 3;

    /// Try to copy a complete AclHeader from the passed in iterator.
    pub fn try_from_iter(iter: impl ExactSizeIterator<Item = u8>) -> Result<Self, ParseError> {
        let mut bytes = [0u8; Self::SIZE];
        match copy_until_full(iter, bytes.iter_mut()) {
            Ok(()) => Ok(Self { bytes }),
            Err(()) => Err(ParseError::PayloadTooShort),
        }
    }

    /// Return a view to the underlying bytes.
    fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    /// The length of the ACL packet body parsed from the header.
    fn body_len(&self) -> usize {
        u16::from_le_bytes([self.bytes[Self::BODY_LEN_LO], self.bytes[Self::BODY_LEN_HI]]).into()
    }
}

/// Return true if the provided `Iterator` contains a complete packet starting at the
/// beginning of the `iter`.
pub(crate) fn has_complete_packet(buffer: &VecDeque<u8>) -> Result<(), ParseError> {
    let mut iter = buffer.iter().cloned();
    UartHeader::try_from_iter(&mut iter).and_then(|hdr| hdr.try_hdr_from_iter(&mut iter)).and_then(
        |hdr| {
            if iter.len() >= hdr.body_len() {
                Ok(())
            } else {
                Err(ParseError::PayloadTooShort)
            }
        },
    )
}

/// Write a packet from the provided `Iterator`. `out_buffer` is always consumed and
/// is used as the backing allocation for the returned `IncomingPacket` if a packet
/// is available.
///
/// If a complete packet is returned, the bytes in `buffer` containing the packet will be consumed.
/// If there is not a complete packet at the head of `buffer`, `buffer` will not be modified
/// by this function.
pub(super) fn consume_next_packet(
    buffer: &mut VecDeque<u8>,
    mut out_buffer: Vec<u8>,
) -> Result<IncomingPacket, ParseError> {
    let mut iter = buffer.iter().cloned();
    UartHeader::try_from_iter(&mut iter)
        .and_then(|uart_hdr| uart_hdr.try_hdr_from_iter(&mut iter))
        .and_then(|hci_hdr| {
            if iter.len() >= hci_hdr.body_len() {
                let hci_start = UartHeader::SIZE;
                let hci_end = hci_start + hci_hdr.bytes().len() + hci_hdr.body_len();
                let (a, b) = slices_from_range(&buffer, hci_start..hci_end);
                out_buffer.extend_from_slice(a);
                out_buffer.extend_from_slice(b);
                // return an IncomingPacket containing the next hci packet.
                Ok(hci_hdr.incoming_packet_builder()(out_buffer))
            } else {
                Err(ParseError::PayloadTooShort)
            }
        })
        .map(|pkt| {
            // drop bytes used to create the packet
            let consumed = UartHeader::SIZE + pkt.inner().len();
            buffer.drain(..consumed);
            pkt
        })
}

/// Frame an outgoing hci packet with a UART transport frame.
/// See Bluetooth Core Spec v5.1, Vol 4, Part A, Section 2
pub(super) fn encode_outgoing_packet<'a>(packet: OutgoingPacket<'a>, outgoing: &mut Vec<u8>) {
    match packet {
        OutgoingPacket::Cmd(data) => {
            outgoing.push(CMD_INDICATOR);
            outgoing.extend_from_slice(data);
        }
        OutgoingPacket::Acl(data) => {
            outgoing.push(ACL_INDICATOR);
            outgoing.extend_from_slice(data);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::iter::FromIterator;

    #[test]
    fn encode_outgoing_packets() {
        let mut expected = b"deadbeef".to_vec();
        expected.insert(0, CMD_INDICATOR);
        let p = OutgoingPacket::Cmd(b"deadbeef");
        let mut actual = vec![];
        encode_outgoing_packet(p, &mut actual);
        assert_eq!(expected, actual);

        let mut expected = b"deadbeef".to_vec();
        expected.insert(0, ACL_INDICATOR);
        let p = OutgoingPacket::Acl(b"deadbeef");
        let mut actual = vec![];
        encode_outgoing_packet(p, &mut actual);
        assert_eq!(expected, actual);
    }

    #[test]
    fn get_event_packet_header() {
        let mut iter = vec![0x04, 0x0e, 0x00, 0x01, 0x4c, 0xfc, 0x00, 0x04, 0x0e].into_iter();
        let hdr = UartHeader::try_from_iter(&mut iter).unwrap();
        let hdr = hdr.try_hdr_from_iter(&mut iter).unwrap().to_event();
        assert_eq!(0, hdr.body_len());

        let mut iter =
            vec![0x04, 0x0e, 0x04, 0x01, 0x4c, 0xfc, 0x00, 0x04, 0x0e, 0xaa, 0xbb].into_iter();
        let hdr = UartHeader::try_from_iter(&mut iter).unwrap();
        let hdr = hdr.try_hdr_from_iter(&mut iter).unwrap().to_event();
        assert_eq!(4, hdr.body_len());

        let mut iter = vec![0x04, 0x00, 0x04].into_iter();
        let hdr = UartHeader::try_from_iter(&mut iter).unwrap();
        let hdr = hdr.try_hdr_from_iter(&mut iter).unwrap().to_event();
        assert_eq!(4, hdr.body_len());
    }

    #[test]
    fn get_acl_packet_lengths() {
        let mut iter = vec![0x02, 0x0e, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x04, 0x0e].into_iter();
        let hdr = UartHeader::try_from_iter(&mut iter).unwrap();
        let hdr = hdr.try_hdr_from_iter(&mut iter).unwrap().to_acl();
        assert_eq!(0, hdr.body_len());

        let mut iter = vec![0x02, 0x0e, 0x04, 0x01, 0xff, 0xfc, 0x00, 0x04, 0x0e].into_iter();
        let hdr = UartHeader::try_from_iter(&mut iter).unwrap();
        let hdr = hdr.try_hdr_from_iter(&mut iter).unwrap().to_acl();
        assert_eq!(65281, hdr.body_len());

        let mut iter = vec![0x02, 0x00, 0x00, 0x01, 0x02].into_iter();
        let hdr = UartHeader::try_from_iter(&mut iter).unwrap();
        let hdr = hdr.try_hdr_from_iter(&mut iter).unwrap().to_acl();
        assert_eq!(513, hdr.body_len());
    }

    #[test]
    fn has_packet_data_too_short() {
        // empty packet
        let buffer = vec![];
        let mut buffer = VecDeque::from_iter(buffer);
        assert_eq!(has_complete_packet(&mut buffer), Err(ParseError::PayloadTooShort));
        assert_eq!(Vec::from(buffer), vec![]);

        // shorter than header size
        let buffer = vec![0x04, 0x02];
        let mut buffer = VecDeque::from_iter(buffer);
        assert_eq!(has_complete_packet(&mut buffer), Err(ParseError::PayloadTooShort));
        assert_eq!(Vec::from(buffer), vec![0x04, 0x02]);

        // shorter than packet size
        let buffer = vec![0x04, 0x02, 0x03, 0x04, 0x05];
        let mut buffer = VecDeque::from_iter(buffer);
        assert_eq!(has_complete_packet(&mut buffer), Err(ParseError::PayloadTooShort));
        assert_eq!(Vec::from(buffer), vec![0x04, 0x02, 0x03, 0x04, 0x05]);
    }

    #[test]
    fn has_packet_invalid_data() {
        // single byte buffer with invalid indicator
        let buffer = vec![0xff];
        let mut buffer = VecDeque::from_iter(buffer);
        assert_eq!(has_complete_packet(&mut buffer), Err(ParseError::InvalidPacketIndicator(0xff)));
    }

    #[test]
    fn consume_next_packet_data_too_short() {
        // empty packet
        let buffer_ = vec![];
        let mut buffer = VecDeque::from_iter(buffer_.clone());
        let out = vec![];
        assert_eq!(consume_next_packet(&mut buffer, out), Err(ParseError::PayloadTooShort));
        assert!(Vec::from(buffer).is_empty());

        // shorter than header size
        let buffer_ = vec![0x04, 0x02];
        let mut buffer = VecDeque::from_iter(buffer_.clone());
        let out = vec![];
        assert_eq!(consume_next_packet(&mut buffer, out), Err(ParseError::PayloadTooShort));
        assert_eq!(Vec::from(buffer), buffer_);

        // shorter than packet size
        let buffer_ = vec![0x04, 0x02, 0x03, 0x04, 0x05];
        let mut buffer = VecDeque::from_iter(buffer_.clone());
        let out = vec![];
        assert_eq!(consume_next_packet(&mut buffer, out), Err(ParseError::PayloadTooShort));
        assert_eq!(Vec::from(buffer), buffer_);
    }

    #[test]
    fn consume_next_packet_data_full_packet() {
        // exactly enough data
        let buffer_ = vec![0x04, 0x02, 0x03, 0x04, 0x05, 0x06];
        let mut buffer = VecDeque::from_iter(buffer_);
        let out = vec![];
        assert_eq!(
            consume_next_packet(&mut buffer, out),
            Ok(IncomingPacket::Event(vec![0x02, 0x03, 0x04, 0x05, 0x06]))
        );
        // accumulation buffer is empty after packet is consumed
        assert!(Vec::from(buffer).is_empty());

        // more than enough data
        let buffer_ = vec![0x04, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
        let mut buffer = VecDeque::from_iter(buffer_);
        let out = vec![];
        assert_eq!(
            consume_next_packet(&mut buffer, out),
            Ok(IncomingPacket::Event(vec![0x02, 0x03, 0x04, 0x05, 0x06]))
        );
        // accumulation buffer has remaining data
        assert_eq!(Vec::from(buffer), vec![0x07, 0x08]);
    }
}

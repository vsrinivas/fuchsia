// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::{buf::Iter, Buf, BufMut, Bytes, BytesMut, IntoBuf};
use failure::{format_err, Error};
use std::convert::TryFrom;
use std::fmt;
use std::io::Cursor;

// TODO(jsankey): Add structs for a message (i.e. a collection of packets) and a message builder.

/// The header length for an initialization packet: 4 byte channel ID, 1 byte command, 2 byte len.
const INIT_HEADER_LENGTH: usize = 7;
/// The minimum length of the payload in an initialization packet.
const INIT_MIN_PAYLOAD_LENGTH: usize = 0;
/// The length of the header for a continuation packet: 4 byte channel ID, 1 byte sequence.
const CONT_HEADER_LENGTH: usize = 5;
/// The minimum length of the payload in a continuation packet.
const CONT_MIN_PAYLOAD_LENGTH: usize = 1;
/// The maximum theoretical HID packet size, as documented in `fuchsia.hardware.input`. Note that
/// actual max packet size varies between CTAP devices and is usually much smaller, i.e. 64 bytes.
const MAX_PACKET_LENGTH: usize = 8192;

/// CTAPHID commands as defined in https://fidoalliance.org/specs/fido-v2.0-ps-20190130/
/// Note that the logical rather than numeric ordering here matches that in the specification.
#[repr(u8)]
#[derive(Debug, PartialEq, Clone)]
pub enum Command {
    Msg = 0x03,
    Cbor = 0x10,
    Init = 0x06,
    Cancel = 0x11,
    Error = 0x3f,
    Keepalive = 0x3b,
    Wink = 0x08,
    Lock = 0x04,
}

impl TryFrom<u8> for Command {
    type Error = failure::Error;

    fn try_from(value: u8) -> Result<Self, Error> {
        match value {
            0x03 => Ok(Self::Msg),
            0x10 => Ok(Self::Cbor),
            0x06 => Ok(Self::Init),
            0x11 => Ok(Self::Cancel),
            0x3f => Ok(Self::Error),
            0x3b => Ok(Self::Keepalive),
            0x08 => Ok(Self::Wink),
            0x04 => Ok(Self::Lock),
            value => Err(format_err!("Invalid command: {:?}", value)),
        }
    }
}

/// A single CTAPHID packet either received over or to be sent over a `Connection`.
/// The CTAPHID protocol is defined in https://fidoalliance.org/specs/fido-v2.0-ps-20190130/
#[allow(dead_code)]
#[derive(PartialEq, Clone)]
pub enum Packet {
    /// An initialization packet sent as the first in a sequence.
    Initialization {
        /// The unique channel identifier for the client.
        channel: u32,
        /// The meaning of the message.
        command: Command,
        /// The total payload length of the message this packet initiates.
        message_length: u16,
        /// The data carried within the packet.
        payload: Bytes,
    },
    /// A continuation packet sent after the first in a sequence.
    Continuation {
        /// The unique channel identifier for the client.
        channel: u32,
        /// The order of this packet within the message.
        sequence: u8,
        /// The data carried within the packet.
        payload: Bytes,
    },
}

#[allow(dead_code)]
impl Packet {
    /// Creates a new initialization packet.
    pub fn initialization(
        channel: u32,
        command: Command,
        message_length: u16,
        payload: Bytes,
    ) -> Result<Self, Error> {
        if payload.as_ref().len() > MAX_PACKET_LENGTH - INIT_HEADER_LENGTH {
            return Err(format_err!("Initialization packet payload exceeded max length"));
        } else if payload.as_ref().len() > message_length as usize {
            return Err(format_err!("Initialization packet data larger than message length"));
        }
        Ok(Self::Initialization { channel, command, message_length, payload })
    }

    /// Creates a new continuation packet.
    pub fn continuation(channel: u32, sequence: u8, payload: Bytes) -> Result<Self, Error> {
        if payload.as_ref().len() > MAX_PACKET_LENGTH - CONT_HEADER_LENGTH {
            return Err(format_err!("Continuation packet payload exceeded max length"));
        } else if payload.as_ref().len() == 0 {
            return Err(format_err!("Continuation packet did not contain any data"));
        } else if sequence & 0x80 != 0 {
            return Err(format_err!("Continuation packet has sequence number with MSB set"));
        }
        Ok(Self::Continuation { channel, sequence, payload })
    }

    /// Create a new packet using the supplied data if it is valid, or return an informative
    /// error otherwise.
    pub fn new(data: Vec<u8>) -> Result<Self, Error> {
        // Determine the packet type. The MSB of the fifth byte determines packet type.
        let len = data.len();
        if (len > 4) && (data[4] & 0x80 != 0) {
            // Initialization packet.
            if len < INIT_HEADER_LENGTH + INIT_MIN_PAYLOAD_LENGTH {
                return Err(format_err!("Data too short for initialization packet"));
            };
            let mut buf = data.into_buf();
            Packet::initialization(
                /*channel*/ buf.get_u32_be(),
                /*command*/ Command::try_from(buf.get_u8() & 0x7F)?,
                /*message length*/ buf.get_u16_be(),
                /*payload*/ Bytes::from(buf.bytes()),
            )
        } else {
            // Continuation packet.
            if len < CONT_HEADER_LENGTH + CONT_MIN_PAYLOAD_LENGTH {
                return Err(format_err!("Data too short for continuation packet"));
            }
            let mut buf = data.into_buf();
            Packet::continuation(
                /*channel*/ buf.get_u32_be(),
                /*sequence*/ buf.get_u8(),
                /*payload*/ Bytes::from(buf.bytes()),
            )
        }
    }
}

impl fmt::Debug for Packet {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Packet::Initialization { channel, command, message_length, payload } => write!(
                f,
                "InitPacket/{:?} ch={:08x?} msg_len={:?} payload={:02x?}",
                command,
                channel,
                message_length,
                &payload[..]
            ),
            Packet::Continuation { channel, sequence, payload } => write!(
                f,
                "ContPacket ch={:08x?} seq={:?} payload={:02x?}",
                channel,
                sequence,
                &payload[..]
            ),
        }
    }
}

impl Into<Bytes> for Packet {
    fn into(self) -> Bytes {
        match self {
            Packet::Initialization { channel, command, message_length, payload } => {
                let mut data = BytesMut::with_capacity(INIT_HEADER_LENGTH + payload.len());
                data.put_u32_be(channel);
                // Setting the MSB on a command byte identifies an init packet.
                data.put_u8(0x80 | command as u8);
                data.put_u16_be(message_length);
                data.put(payload);
                Bytes::from(data)
            }
            Packet::Continuation { channel, sequence, payload } => {
                let mut data = BytesMut::with_capacity(CONT_HEADER_LENGTH + payload.len());
                data.put_u32_be(channel);
                data.put_u8(sequence);
                data.put(payload);
                Bytes::from(data)
            }
        }
    }
}

impl IntoIterator for Packet {
    type Item = u8;
    type IntoIter = Iter<Cursor<Bytes>>;

    fn into_iter(self) -> Self::IntoIter {
        // TODO(jsankey): It would be more efficient to iterate over our internal data rather than
        // creating a copy to iterate over `Bytes`.
        let bytes: Bytes = self.into();
        bytes.into_buf().iter()
    }
}

impl TryFrom<Vec<u8>> for Packet {
    type Error = failure::Error;

    fn try_from(value: Vec<u8>) -> Result<Self, Error> {
        Packet::new(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_CHANNEL: u32 = 0x89abcdef;
    const TEST_COMMAND: Command = Command::Wink;
    const TEST_SEQUENCE_NUM: u8 = 99;

    #[test]
    fn test_command_conversion() {
        // Verify that every integer that maps to a valid command is also the value of that
        // command. Note we don't have runtime enum information to test all commands are accessible
        // from an integer.
        for i in 0..=255 {
            if let Ok(command) = Command::try_from(i) {
                assert_eq!(command as u8, i);
            }
        }
    }

    /// Verifies that a valid packet can be converted into bytes and back, and that the bytes and
    /// debug strings match expectations.
    fn do_conversion_test(
        packet: Packet,
        debug_string: &str,
        bytes_string: &str,
    ) -> Result<(), Error> {
        // Debug format for a packet is very valuable during debugging, so worth testing.
        assert_eq!(format!("{:?}", packet), debug_string);
        let cloned_original = packet.clone();
        let converted: Vec<u8> = packet.into_iter().collect();
        assert_eq!(format!("{:02x?}", converted), bytes_string);
        let double_converted = Packet::try_from(converted)?;
        assert_eq!(cloned_original, double_converted);
        Ok(())
    }

    #[test]
    fn test_empty_initialization_packet() -> Result<(), Error> {
        // Lock = 0x04
        do_conversion_test(
            Packet::initialization(TEST_CHANNEL, Command::Lock, 0, Bytes::from(vec![]))?,
            "InitPacket/Lock ch=89abcdef msg_len=0 payload=[]",
            "[89, ab, cd, ef, 84, 00, 00]",
        )
    }

    #[test]
    fn test_non_empty_initialization_packet() -> Result<(), Error> {
        // Wink = 0x08
        // Message len = 1122 hex = 4386 dec
        do_conversion_test(
            Packet::initialization(
                TEST_CHANNEL,
                Command::Wink,
                0x1122,
                Bytes::from(vec![0x44, 0x55, 0x66, 0x77]),
            )?,
            "InitPacket/Wink ch=89abcdef msg_len=4386 payload=[44, 55, 66, 77]",
            "[89, ab, cd, ef, 88, 11, 22, 44, 55, 66, 77]",
        )
    }

    #[test]
    fn test_non_empty_continuation_packet() -> Result<(), Error> {
        // Sequence = 99 decimal = 63 hex
        do_conversion_test(
            Packet::continuation(TEST_CHANNEL, 99, Bytes::from(vec![0xfe, 0xed, 0xcd]))?,
            "ContPacket ch=89abcdef seq=99 payload=[fe, ed, cd]",
            "[89, ab, cd, ef, 63, fe, ed, cd]",
        )
    }

    #[test]
    fn test_invalid_initialization_packet() -> Result<(), Error> {
        // Payload longer than the max packet size should fail.
        assert!(Packet::initialization(
            TEST_CHANNEL,
            TEST_COMMAND,
            20000,
            Bytes::from(vec![0; 10000])
        )
        .is_err());
        // Message length smaller than the payload of its first packet should fail.
        assert!(Packet::initialization(TEST_CHANNEL, TEST_COMMAND, 28, Bytes::from(vec![0; 30]))
            .is_err());
        Ok(())
    }

    #[test]
    fn test_invalid_continuation_packet() -> Result<(), Error> {
        // Sequence number with the MSB set should fail.
        assert!(Packet::continuation(TEST_CHANNEL, 0x88, Bytes::from(vec![1])).is_err());
        // Continuation packet with no data should fail.
        assert!(Packet::continuation(TEST_CHANNEL, TEST_SEQUENCE_NUM, Bytes::from(vec![])).is_err());
        Ok(())
    }

    #[test]
    fn test_short_input_tryfrom() -> Result<(), Error> {
        // Initiation packet of 6 bytes should fail.
        assert!(Packet::try_from(vec![0x89, 0xab, 0xcd, 0xef, 0x84, 0x00]).is_err());
        // Continuation packet of 6 bytes should pass.
        assert!(Packet::try_from(vec![0x89, 0xab, 0xcd, 0xef, 0x04, 0x00]).is_ok());
        // Continuation packet of 5 bytes should pass.
        assert!(Packet::try_from(vec![0x89, 0xab, 0xcd, 0xef, 0x04]).is_err());
        Ok(())
    }
}

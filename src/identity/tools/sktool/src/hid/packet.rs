// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hid::command::Command;
use crate::hid::util::pad;
use anyhow::{format_err, Error};
use bytes::{buf::IntoIter, Buf, BufMut, Bytes, BytesMut};
use std::convert::{TryFrom, TryInto};
use std::fmt;

/// The header length for an initialization packet: 4 byte channel ID, 1 byte command, 2 byte len.
pub const INIT_HEADER_LENGTH: u16 = 7;
/// The minimum length of the payload in an initialization packet.
pub const INIT_MIN_PAYLOAD_LENGTH: u16 = 0;
/// The length of the header for a continuation packet: 4 byte channel ID, 1 byte sequence.
pub const CONT_HEADER_LENGTH: u16 = 5;
/// The minimum length of the payload in a continuation packet.
pub const CONT_MIN_PAYLOAD_LENGTH: u16 = 1;
/// The maximum theoretical HID packet size, as documented in `fuchsia.hardware.input`. Note that
/// actual max packet size varies between CTAP devices and is usually much smaller, i.e. 64 bytes.
pub const MAX_PACKET_LENGTH: u16 = 8192;

/// A single CTAPHID packet either received over or to be sent over a `Connection`.
/// The CTAPHID protocol is defined in https://fidoalliance.org/specs/fido-v2.0-ps-20190130/
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

impl Packet {
    /// Creates a new initialization packet.
    pub fn initialization<T: Into<Bytes>>(
        channel: u32,
        command: Command,
        message_length: u16,
        payload: T,
    ) -> Result<Self, Error> {
        let payload_bytes = payload.into();
        if payload_bytes.len() > (MAX_PACKET_LENGTH - INIT_HEADER_LENGTH) as usize {
            return Err(format_err!("Initialization packet payload exceeded max length"));
        }
        Ok(Self::Initialization { channel, command, message_length, payload: payload_bytes })
    }

    /// Creates a new initialization packet using data padded out to the supplied packet length.
    /// This will require a copy, hence the payload is supplied by reference.
    pub fn padded_initialization(
        channel: u32,
        command: Command,
        payload: &[u8],
        packet_length: u16,
    ) -> Result<Self, Error> {
        let payload_length: u16 = payload.len().try_into()?;
        if payload_length > packet_length - INIT_HEADER_LENGTH {
            return Err(format_err!("Padded initialization packet length shorter than content"));
        }
        Self::initialization(
            channel,
            command,
            payload_length,
            pad(payload, packet_length - INIT_HEADER_LENGTH)?,
        )
    }

    /// Creates a new continuation packet.
    pub fn continuation<T: Into<Bytes>>(
        channel: u32,
        sequence: u8,
        payload: T,
    ) -> Result<Self, Error> {
        let payload_bytes = payload.into();
        if payload_bytes.len() > (MAX_PACKET_LENGTH - CONT_HEADER_LENGTH) as usize {
            return Err(format_err!("Continuation packet payload exceeded max length"));
        } else if payload_bytes.len() == 0 {
            return Err(format_err!("Continuation packet did not contain any data"));
        } else if sequence & 0x80 != 0 {
            return Err(format_err!("Continuation packet has sequence number with MSB set"));
        }
        Ok(Self::Continuation { channel, sequence, payload: payload_bytes })
    }

    /// Create a new packet using the supplied data if it is valid, or return an informative
    /// error otherwise.
    fn from_bytes(data: Vec<u8>) -> Result<Self, Error> {
        // Determine the packet type. The MSB of the fifth byte determines packet type.
        let len = data.len();
        if (len > 4) && (data[4] & 0x80 != 0) {
            // Initialization packet.
            if len < (INIT_HEADER_LENGTH + INIT_MIN_PAYLOAD_LENGTH) as usize {
                return Err(format_err!("Data too short for initialization packet"));
            };
            let mut buf: Bytes = data.into();
            Packet::initialization(
                /*channel*/ buf.get_u32(),
                /*command*/ Command::try_from(buf.get_u8() & 0x7F)?,
                /*message length*/ buf.get_u16(),
                /*payload*/ buf.to_bytes(),
            )
        } else {
            // Continuation packet.
            if len < (CONT_HEADER_LENGTH + CONT_MIN_PAYLOAD_LENGTH) as usize {
                return Err(format_err!("Data too short for continuation packet"));
            }
            let mut buf: Bytes = data.into();
            Packet::continuation(
                /*channel*/ buf.get_u32(),
                /*sequence*/ buf.get_u8(),
                /*payload*/ buf.to_bytes(),
            )
        }
    }

    /// Returns the channel of this packet.
    pub fn channel(&self) -> u32 {
        match self {
            Packet::Initialization { channel, .. } => *channel,
            Packet::Continuation { channel, .. } => *channel,
        }
    }

    /// Returns the command of this packet, or an error if called on a continuation packet.
    pub fn command(&self) -> Result<Command, Error> {
        match self {
            Packet::Initialization { command, .. } => Ok(*command),
            Packet::Continuation { .. } => {
                Err(format_err!("Cannot get command for continuation packet"))
            }
        }
    }

    /// Returns true iff this is an intitializaion packet with the specified command.
    pub fn is_command(&self, command: Command) -> bool {
        match self {
            Packet::Initialization { command: actual, .. } => (actual == &command),
            Packet::Continuation { .. } => false,
        }
    }

    /// Returns the payload of this packet.
    pub fn payload(&self) -> &Bytes {
        match self {
            Packet::Initialization { payload, .. } => &payload,
            Packet::Continuation { payload, .. } => &payload,
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
                let mut data = BytesMut::with_capacity(INIT_HEADER_LENGTH as usize + payload.len());
                data.put_u32(channel);
                // Setting the MSB on a command byte identifies an init packet.
                data.put_u8(0x80 | command as u8);
                data.put_u16(message_length);
                data.put(payload);
                Bytes::from(data)
            }
            Packet::Continuation { channel, sequence, payload } => {
                let mut data = BytesMut::with_capacity(CONT_HEADER_LENGTH as usize + payload.len());
                data.put_u32(channel);
                data.put_u8(sequence);
                data.put(payload);
                Bytes::from(data)
            }
        }
    }
}

impl IntoIterator for Packet {
    type Item = u8;
    type IntoIter = IntoIter<Bytes>;

    fn into_iter(self) -> Self::IntoIter {
        // TODO(jsankey): It would be more efficient to iterate over our internal data rather than
        // creating a copy to iterate over `Bytes`.
        let bytes: Bytes = self.into();
        bytes.into_iter()
    }
}

impl TryFrom<Vec<u8>> for Packet {
    type Error = anyhow::Error;

    fn try_from(value: Vec<u8>) -> Result<Self, Error> {
        Packet::from_bytes(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_CHANNEL: u32 = 0x89abcdef;
    const TEST_COMMAND: Command = Command::Wink;
    const TEST_SEQUENCE_NUM: u8 = 99;
    const TEST_LENGTH: u16 = 0x4455;

    #[test]
    fn initialization_packet_getters() -> Result<(), Error> {
        let packet =
            Packet::initialization(TEST_CHANNEL, TEST_COMMAND, TEST_LENGTH, vec![0xff, 0xee])?;
        assert_eq!(packet.channel(), TEST_CHANNEL);
        assert_eq!(packet.command()?, TEST_COMMAND);
        assert_eq!(packet.is_command(TEST_COMMAND), true);
        assert_eq!(packet.is_command(Command::Cbor), false);
        assert_eq!(packet.payload(), &vec![0xff, 0xee]);
        Ok(())
    }

    #[test]
    fn continuation_packet_getters() -> Result<(), Error> {
        let packet = Packet::continuation(TEST_CHANNEL, TEST_SEQUENCE_NUM, vec![0xff, 0xee])?;
        assert_eq!(packet.channel(), TEST_CHANNEL);
        assert!(packet.command().is_err());
        assert_eq!(packet.is_command(TEST_COMMAND), false);
        assert_eq!(packet.payload(), &vec![0xff, 0xee]);
        Ok(())
    }

    /// Verifies that a valid packet can be converted into bytes and back, and that the bytes and
    /// debug strings match expectations.
    fn do_packet_conversion_test(
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
    fn empty_initialization_packet() -> Result<(), Error> {
        // Lock = 0x04
        do_packet_conversion_test(
            Packet::initialization(TEST_CHANNEL, Command::Lock, 0, vec![])?,
            "InitPacket/Lock ch=89abcdef msg_len=0 payload=[]",
            "[89, ab, cd, ef, 84, 00, 00]",
        )
    }

    #[test]
    fn non_empty_initialization_packet() -> Result<(), Error> {
        // Wink = 0x08
        // Message len = 1122 hex = 4386 dec
        // Note that in real life packets are padded.
        do_packet_conversion_test(
            Packet::initialization(
                TEST_CHANNEL,
                Command::Wink,
                0x1122,
                vec![0x44, 0x55, 0x66, 0x77],
            )?,
            "InitPacket/Wink ch=89abcdef msg_len=4386 payload=[44, 55, 66, 77]",
            "[89, ab, cd, ef, 88, 11, 22, 44, 55, 66, 77]",
        )
    }

    #[test]
    fn padded_initialization_packet() -> Result<(), Error> {
        // Wink = 0x08
        // Note that in real life packets are larger than the 16 bytes here.
        do_packet_conversion_test(
            Packet::padded_initialization(
                TEST_CHANNEL,
                Command::Wink,
                &vec![0x44, 0x55, 0x66],
                16,
            )?,
            "InitPacket/Wink ch=89abcdef msg_len=3 payload=[44, 55, 66, 00, 00, 00, 00, 00, 00]",
            "[89, ab, cd, ef, 88, 00, 03, 44, 55, 66, 00, 00, 00, 00, 00, 00]",
        )
    }

    #[test]
    fn non_empty_continuation_packet() -> Result<(), Error> {
        // Sequence = 99 decimal = 63 hex
        do_packet_conversion_test(
            Packet::continuation(TEST_CHANNEL, 99, vec![0xfe, 0xed, 0xcd])?,
            "ContPacket ch=89abcdef seq=99 payload=[fe, ed, cd]",
            "[89, ab, cd, ef, 63, fe, ed, cd]",
        )
    }

    #[test]
    fn invalid_initialization_packet() -> Result<(), Error> {
        // Payload longer than the max packet size should fail.
        assert!(Packet::initialization(TEST_CHANNEL, TEST_COMMAND, 20000, vec![0; 10000]).is_err());
        Ok(())
    }

    #[test]
    fn invalid_continuation_packet() -> Result<(), Error> {
        // Sequence number with the MSB set should fail.
        assert!(Packet::continuation(TEST_CHANNEL, 0x88, vec![1]).is_err());
        // Continuation packet with no data should fail.
        assert!(Packet::continuation(TEST_CHANNEL, TEST_SEQUENCE_NUM, vec![]).is_err());
        Ok(())
    }

    #[test]
    fn packet_try_from_byte_vector() -> Result<(), Error> {
        // Initiation packet of 6 bytes should fail.
        assert!(Packet::try_from(vec![0x89, 0xab, 0xcd, 0xef, 0x84, 0x00]).is_err());
        // Continuation packet of 6 bytes should pass.
        assert!(Packet::try_from(vec![0x89, 0xab, 0xcd, 0xef, 0x04, 0x00]).is_ok());
        // Continuation packet of 5 bytes should pass.
        assert!(Packet::try_from(vec![0x89, 0xab, 0xcd, 0xef, 0x04]).is_err());
        Ok(())
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use bytes::{buf::Iter, Buf, BufMut, Bytes, BytesMut, IntoBuf};
use std::convert::{TryFrom, TryInto};
use std::fmt;
use std::io::Cursor;

// TODO(jsankey): Add structs for a message (i.e. a collection of packets) and a message builder.

/// The header length for an initialization packet: 4 byte channel ID, 1 byte command, 2 byte len.
const INIT_HEADER_LENGTH: u16 = 7;
/// The minimum length of the payload in an initialization packet.
const INIT_MIN_PAYLOAD_LENGTH: u16 = 0;
/// The length of the header for a continuation packet: 4 byte channel ID, 1 byte sequence.
const CONT_HEADER_LENGTH: u16 = 5;
/// The minimum length of the payload in a continuation packet.
const CONT_MIN_PAYLOAD_LENGTH: u16 = 1;
/// The maximum nuber of continuation packets in a message, as defined by the CTAPHID spec.
const MAX_CONT_PACKET_COUNT: u8 = 128;
/// The maximum theoretical HID packet size, as documented in `fuchsia.hardware.input`. Note that
/// actual max packet size varies between CTAP devices and is usually much smaller, i.e. 64 bytes.
const MAX_PACKET_LENGTH: u16 = 8192;

/// CTAPHID commands as defined in https://fidoalliance.org/specs/fido-v2.0-ps-20190130/
/// Note that the logical rather than numeric ordering here matches that in the specification.
#[repr(u8)]
#[derive(Debug, PartialEq, Copy, Clone)]
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
    type Error = anyhow::Error;

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
            let mut buf = data.into_buf();
            Packet::initialization(
                /*channel*/ buf.get_u32_be(),
                /*command*/ Command::try_from(buf.get_u8() & 0x7F)?,
                /*message length*/ buf.get_u16_be(),
                /*payload*/ buf.bytes(),
            )
        } else {
            // Continuation packet.
            if len < (CONT_HEADER_LENGTH + CONT_MIN_PAYLOAD_LENGTH) as usize {
                return Err(format_err!("Data too short for continuation packet"));
            }
            let mut buf = data.into_buf();
            Packet::continuation(
                /*channel*/ buf.get_u32_be(),
                /*sequence*/ buf.get_u8(),
                /*payload*/ buf.bytes(),
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
                data.put_u32_be(channel);
                // Setting the MSB on a command byte identifies an init packet.
                data.put_u8(0x80 | command as u8);
                data.put_u16_be(message_length);
                data.put(payload);
                Bytes::from(data)
            }
            Packet::Continuation { channel, sequence, payload } => {
                let mut data = BytesMut::with_capacity(CONT_HEADER_LENGTH as usize + payload.len());
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
    type Error = anyhow::Error;

    fn try_from(value: Vec<u8>) -> Result<Self, Error> {
        Packet::from_bytes(value)
    }
}

/// A CTAPHID message either received over or to be sent over a `Connection`.
/// The CTAPHID protocol is defined in https://fidoalliance.org/specs/fido-v2.0-ps-20190130/
///
/// A `Message` may be iterated over as a sequence of `Packet` objects or assembled from a sequence
/// of `Packet` objects using a `MessageBuilder`. Each message owns its own payload as a `Bytes`
/// object. This message payload object is used to back the payload for each packet during
/// iteration so we pad the message payload with zeros to the next complete packet length.
#[derive(PartialEq, Clone)]
pub struct Message {
    /// The unique channel identifier for the client.
    channel: u32,
    /// The meaning of the message.
    command: Command,
    /// The data carried within the message, padded to the next packet boundary.
    payload: Bytes,
    /// The length of the data within the message before any paddding.
    payload_length: u16,
    /// The length of packet this message was assembled from or will be broken into.
    packet_length: u16,
}

#[allow(dead_code)]
impl Message {
    /// Creates a new message containing the supplied payload.
    // Note: The supplied payload is padded to a complete final packet internally, hence the
    // packet size must be supplied and the payload is supplied by reference.
    pub fn new(
        channel: u32,
        command: Command,
        payload: &[u8],
        packet_length: u16,
    ) -> Result<Self, Error> {
        let payload_length = u16::try_from(payload.len()).map_err(|_| {
            format_err!("Payload length {} exceeds max theoretical size", payload.len())
        })?;
        let cont_packet_count = cont_packet_count(payload_length, packet_length);
        if cont_packet_count > MAX_CONT_PACKET_COUNT {
            return Err(format_err!("Payload length {} exceeds max packets", payload_length));
        }
        let padded_length = padded_length(payload_length, packet_length);
        Ok(Self {
            channel,
            command,
            payload: pad(payload, padded_length)?,
            payload_length,
            packet_length,
        })
    }

    /// Returns the channel of this message.
    pub fn channel(&self) -> u32 {
        self.channel
    }

    /// Returns the command of this message.
    pub fn command(&self) -> Command {
        self.command
    }

    /// Returns the payload of this message, without any padding.
    pub fn payload(&self) -> Bytes {
        self.payload.slice_to(self.payload_length as usize)
    }
}

impl fmt::Debug for Message {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Msg/{:?} ch={:08x?} payload={:02x?}",
            self.command,
            self.channel,
            &self.payload()[..]
        )
    }
}

/// An iterator over the `Packet` objects that can be generated from a `Message`.
#[derive(PartialEq, Clone)]
pub struct MessageIterator {
    /// The message we are iterating over.
    message: Message,
    /// The next packet index to send, where zero indicates the initialization packet and
    /// one indicates the first continuation packet.
    next_index: u8,
}

impl Iterator for MessageIterator {
    type Item = Packet;

    fn next(&mut self) -> Option<Self::Item> {
        let message = &self.message;
        let cont_packet_count = cont_packet_count(message.payload_length, message.packet_length);
        let init_payload_length = (message.packet_length - INIT_HEADER_LENGTH) as usize;
        // Note: We ensured that a message payload was padded out to allow a complete last packet
        // when the message was constructed, hence it is legal to slice complete packets from that
        // payload here, even when they extend past the message length.
        match self.next_index {
            0 => {
                self.next_index += 1;
                Some(Packet::Initialization {
                    channel: message.channel,
                    command: message.command,
                    message_length: message.payload_length,
                    payload: message.payload.slice_to(init_payload_length),
                })
            }
            next_index if next_index <= cont_packet_count => {
                // Note: The first continuation packet is sequence number=0, which is our index=1.
                let sequence = self.next_index - 1;
                let cont_payload_length = (message.packet_length - CONT_HEADER_LENGTH) as usize;
                let start_offset = init_payload_length + (sequence as usize * cont_payload_length);
                self.next_index += 1;
                Some(Packet::Continuation {
                    channel: message.channel,
                    sequence,
                    payload: message
                        .payload
                        .slice(start_offset, start_offset + cont_payload_length),
                })
            }
            _ => None,
        }
    }
}

impl IntoIterator for Message {
    type Item = Packet;
    type IntoIter = MessageIterator;

    fn into_iter(self) -> Self::IntoIter {
        MessageIterator { message: self, next_index: 0 }
    }
}

/// The current state of a `MessageBuilder`, indicating whether it contains enough data to be
/// turned into a message.
#[derive(Debug, PartialEq, Copy, Clone)]
pub enum BuilderStatus {
    /// The `MessageBuilder` contains all packets in the message.
    Complete,
    /// The `MessageBuilder` does not yet contain all the packets of the message.
    Incomplete,
}

/// A builder to assemble a CTAPHID `Message` from CTAPHID `Packet` objects received over a
/// `Connection`.
pub struct MessageBuilder {
    /// The unique channel identifier for the client.
    channel: u32,
    /// The meaning of the message.
    command: Command,
    /// The length of the data within the message before any paddding.
    payload_length: u16,
    /// A `BytesMut` object sized to contain the entire message, populated with the data received
    /// to date.
    payload: BytesMut,
    /// The sequence number expected in the next continuation packet.
    next_sequence: u8,
    /// The length of packets being supplied.
    packet_length: u16,
}

#[allow(dead_code)]
impl MessageBuilder {
    /// Creates a new `MessageBuilder` starting with the supplied initialization packet.
    pub fn new(packet: Packet) -> Result<MessageBuilder, Error> {
        match packet {
            Packet::Initialization {
                channel,
                command,
                message_length,
                payload: packet_payload,
            } => {
                let packet_length = INIT_HEADER_LENGTH + packet_payload.len() as u16;
                if cont_packet_count(message_length, packet_length) > MAX_CONT_PACKET_COUNT {
                    return Err(format_err!(
                        "Payload length {} exceeds max packets",
                        message_length
                    ));
                }
                let padded_length = padded_length(message_length, packet_length);
                let mut message_payload = BytesMut::with_capacity(padded_length as usize);
                message_payload.put(packet_payload);
                Ok(Self {
                    channel,
                    command,
                    payload_length: message_length,
                    payload: message_payload,
                    next_sequence: 0,
                    packet_length,
                })
            }
            Packet::Continuation { .. } => {
                Err(format_err!("First packet in builder was not an initialization packet"))
            }
        }
    }

    /// Add a continuation packet to this builder, returning a status indicating whether the
    /// message is ready to be built on success.
    pub fn append(&mut self, packet: Packet) -> Result<BuilderStatus, Error> {
        if self.status() == BuilderStatus::Complete {
            return Err(format_err!("Cannot append packets to a builder that is already complete"));
        }
        match packet {
            Packet::Initialization { .. } => {
                Err(format_err!("Appended packet was not a continuation packet"))
            }
            Packet::Continuation { channel, sequence, payload: packet_payload } => {
                if channel != self.channel {
                    return Err(format_err!(
                        "Appended packet channel ({:?}) does not match \
                        initialization channel ({:?})",
                        channel,
                        self.channel
                    ));
                }
                let packet_length = CONT_HEADER_LENGTH + packet_payload.len() as u16;
                if packet_length != self.packet_length {
                    return Err(format_err!(
                        "Appended packet length ({:?}) does not match \
                        initialization length ({:?})",
                        packet_length,
                        self.packet_length
                    ));
                }
                if sequence != self.next_sequence {
                    return Err(format_err!(
                        "Appended packet sequence ({:?}) does not match \
                        expectation ({:?})",
                        sequence,
                        self.next_sequence
                    ));
                }
                self.payload.put(packet_payload);
                self.next_sequence += 1;
                Ok(self.status())
            }
        }
    }

    /// Returns a status indicating whether the `MessageBuilder` is ready to be converted into a
    /// `Message`.
    pub fn status(&self) -> BuilderStatus {
        if self.next_sequence >= cont_packet_count(self.payload_length, self.packet_length) {
            BuilderStatus::Complete
        } else {
            BuilderStatus::Incomplete
        }
    }
}

impl TryFrom<MessageBuilder> for Message {
    type Error = anyhow::Error;

    fn try_from(builder: MessageBuilder) -> Result<Message, Error> {
        match builder.status() {
            BuilderStatus::Complete => Ok(Message {
                channel: builder.channel,
                command: builder.command,
                payload: Bytes::from(builder.payload),
                payload_length: builder.payload_length,
                packet_length: builder.packet_length,
            }),
            BuilderStatus::Incomplete => {
                Err(format_err!("Cannot create a message from an incomplete builder"))
            }
        }
    }
}

/// Returns the number of continuation packets required to hold a particular payload, given a
/// packet size. Note that an initialization packet is always required in addition to these
/// continuation packets.
fn cont_packet_count(payload_length: u16, packet_length: u16) -> u8 {
    if payload_length < (packet_length - INIT_HEADER_LENGTH) {
        0
    } else {
        let cont_payload = payload_length - (packet_length - INIT_HEADER_LENGTH);
        let payload_per_cont_packet = packet_length - CONT_HEADER_LENGTH;
        let packet_count = (cont_payload + (payload_per_cont_packet - 1)) / payload_per_cont_packet;
        // Note: The maximum sequence number in the protocol is 127, anything higher than this will
        // be rejected, so its OK for us to report u8:max in cases where the message would require
        // even more than this
        u8::try_from(packet_count).unwrap_or(std::u8::MAX)
    }
}

/// Return the total buffer size required to store the supplied `payload_length` padded out to a
/// whole final packet.
fn padded_length(payload_length: u16, packet_length: u16) -> u16 {
    let cont_packet_count = cont_packet_count(payload_length, packet_length) as u16;
    (packet_length - INIT_HEADER_LENGTH) + cont_packet_count * (packet_length - CONT_HEADER_LENGTH)
}

/// Convenience method to create a new `Bytes` with the supplied input padded to the specified
/// `length` using with zeros.
fn pad(data: &[u8], length: u16) -> Result<Bytes, Error> {
    // The fact that FIDL bindings require mutable data when sending a packet means we need to
    // define packets as the full length instead of only defining a packet as the meaningful
    // payload and iterating over additional fixed zero bytes to form the full length.
    //
    // Defining padded packets requires a copy operatation (unlike the other zero-copy
    // initializers) and so we define padding methods that use byte array references.
    if data.len() > length as usize {
        return Err(format_err!("Data to pad exceeded requested length"));
    }
    let mut bytes = Bytes::with_capacity(length as usize);
    bytes.extend(data);
    bytes.extend(&vec![0; length as usize - data.len()]);
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_CHANNEL: u32 = 0x89abcdef;
    const WRONG_CHANNEL: u32 = 0x98badcfe;
    const TEST_COMMAND: Command = Command::Wink;
    const TEST_SEQUENCE_NUM: u8 = 99;
    const TEST_LENGTH: u16 = 0x4455;
    const TEST_PACKET_LENGTH: u16 = 10;

    #[test]
    fn command_conversion() {
        // Verify that every integer that maps to a valid command is also the value of that
        // command. Note we don't have runtime enum information to test all commands are accessible
        // from an integer.
        for i in 0..=255 {
            if let Ok(command) = Command::try_from(i) {
                assert_eq!(command as u8, i);
            }
        }
    }

    #[test]
    fn initialization_packet_getters() -> Result<(), Error> {
        let packet =
            Packet::initialization(TEST_CHANNEL, TEST_COMMAND, TEST_LENGTH, vec![0xff, 0xee])?;
        assert_eq!(packet.channel(), TEST_CHANNEL);
        assert_eq!(packet.command()?, TEST_COMMAND);
        assert_eq!(packet.payload(), &vec![0xff, 0xee]);
        Ok(())
    }

    #[test]
    fn continuation_packet_getters() -> Result<(), Error> {
        let packet = Packet::continuation(TEST_CHANNEL, TEST_SEQUENCE_NUM, vec![0xff, 0xee])?;
        assert_eq!(packet.channel(), TEST_CHANNEL);
        assert!(packet.command().is_err());
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

    #[test]
    fn message_getters() -> Result<(), Error> {
        let message =
            Message::new(TEST_CHANNEL, TEST_COMMAND, &vec![0xff, 0xee], TEST_PACKET_LENGTH)?;
        assert_eq!(message.channel(), TEST_CHANNEL);
        assert_eq!(message.command(), TEST_COMMAND);
        assert_eq!(message.payload(), &vec![0xff, 0xee]);
        Ok(())
    }

    /// Verifies that a valid message can be converted into packets and back, and that debug
    /// strings for the message itself and these packets match expectations.
    fn do_message_conversion_test(
        message: Message,
        debug_string: &str,
        expected_packets: Vec<Packet>,
    ) -> Result<(), Error> {
        // Debug format for a message is very valuable during debugging, so worth testing.
        assert_eq!(format!("{:?}", message), debug_string);
        let cloned_original = message.clone();
        let mut packets: Vec<Packet> = message.into_iter().collect();
        assert_eq!(packets, expected_packets);

        let mut builder = MessageBuilder::new(packets.remove(0))?;
        let expected_status = match packets.is_empty() {
            true => BuilderStatus::Complete,
            false => BuilderStatus::Incomplete,
        };
        assert_eq!(builder.status(), expected_status);
        for packet in packets {
            builder.append(packet)?;
        }
        assert_eq!(builder.status(), BuilderStatus::Complete);
        let double_converted = Message::try_from(builder)?;
        assert_eq!(cloned_original, double_converted);
        Ok(())
    }

    #[test]
    fn empty_message() -> Result<(), Error> {
        do_message_conversion_test(
            Message::new(TEST_CHANNEL, Command::Lock, &vec![], TEST_PACKET_LENGTH)?,
            "Msg/Lock ch=89abcdef payload=[]",
            vec![Packet::padded_initialization(
                TEST_CHANNEL,
                Command::Lock,
                &vec![],
                TEST_PACKET_LENGTH,
            )?],
        )
    }

    #[test]
    fn single_packet_message() -> Result<(), Error> {
        do_message_conversion_test(
            Message::new(TEST_CHANNEL, Command::Lock, &vec![0x12, 0x23], TEST_PACKET_LENGTH)?,
            "Msg/Lock ch=89abcdef payload=[12, 23]",
            vec![Packet::padded_initialization(
                TEST_CHANNEL,
                Command::Lock,
                &vec![0x12, 0x23],
                TEST_PACKET_LENGTH,
            )?],
        )
    }

    #[test]
    fn exactly_two_packet_message() -> Result<(), Error> {
        // Note, our 10 byte test packet size allows 3 bytes payload per init, 5 per cont.
        do_message_conversion_test(
            Message::new(
                TEST_CHANNEL,
                Command::Cbor,
                &vec![0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27],
                TEST_PACKET_LENGTH,
            )?,
            "Msg/Cbor ch=89abcdef payload=[20, 21, 22, 23, 24, 25, 26, 27]",
            vec![
                Packet::initialization(TEST_CHANNEL, Command::Cbor, 8, vec![0x20, 0x21, 0x22])?,
                Packet::continuation(TEST_CHANNEL, 0, vec![0x23, 0x24, 0x25, 0x26, 0x27])?,
            ],
        )
    }

    #[test]
    fn three_packet_message() -> Result<(), Error> {
        // Note, our 10 byte test packet size allows 3 bytes payload per init, 5 per cont.
        do_message_conversion_test(
            Message::new(
                TEST_CHANNEL,
                Command::Cbor,
                &vec![0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29],
                TEST_PACKET_LENGTH,
            )?,
            "Msg/Cbor ch=89abcdef payload=[20, 21, 22, 23, 24, 25, 26, 27, 28, 29]",
            vec![
                Packet::initialization(TEST_CHANNEL, Command::Cbor, 10, vec![0x20, 0x21, 0x22])?,
                Packet::continuation(TEST_CHANNEL, 0, vec![0x23, 0x24, 0x25, 0x26, 0x27])?,
                Packet::continuation(TEST_CHANNEL, 1, vec![0x28, 0x29, 0x00, 0x00, 0x00])?,
            ],
        )
    }

    #[test]
    fn new_message_too_large() -> Result<(), Error> {
        // This message should fail because the payload is over 16 bit length, even though it would
        // not require more than 127 packets.
        assert!(Message::new(TEST_CHANNEL, TEST_COMMAND, &vec![7; 100000], 1000).is_err());
        // This message should fail because the payload would require over 127 of the specified
        // packet size, even though the length is under 16 bits.
        assert!(Message::new(TEST_CHANNEL, TEST_COMMAND, &vec![7; 1000], 10).is_err());
        Ok(())
    }

    #[test]
    fn new_messagebuilder_with_invalid_packet() -> Result<(), Error> {
        // Message builder has to start with an init packet.
        assert!(MessageBuilder::new(Packet::continuation(TEST_CHANNEL, 0, vec![0x00])?).is_err());
        // Init packet has to infer less than 128 continue packets.
        assert!(MessageBuilder::new(Packet::initialization(
            TEST_CHANNEL,
            TEST_COMMAND,
            10000,
            vec![7; 10]
        )?)
        .is_err());
        Ok(())
    }

    #[test]
    fn append_messagebuilder_with_invalid_packet() -> Result<(), Error> {
        // Appended message has to have the same channel.
        let init_payload_len = (TEST_PACKET_LENGTH - INIT_HEADER_LENGTH) as usize;
        let cont_payload_len = (TEST_PACKET_LENGTH - CONT_HEADER_LENGTH) as usize;
        let init_packet =
            Packet::initialization(TEST_CHANNEL, TEST_COMMAND, 100, vec![7; init_payload_len])?;

        // First test a succesful append to help avoid a degenerate test.
        let mut builder = MessageBuilder::new(init_packet.clone())?;
        assert_eq!(
            builder.append(Packet::continuation(TEST_CHANNEL, 0, vec![8; cont_payload_len])?)?,
            BuilderStatus::Incomplete
        );

        // Channel of continuation packet has to match.
        let mut builder = MessageBuilder::new(init_packet.clone())?;
        assert!(builder
            .append(Packet::continuation(WRONG_CHANNEL, 0, vec![8; cont_payload_len])?)
            .is_err());

        // Length of continuation packet has to match.
        let mut builder = MessageBuilder::new(init_packet.clone())?;
        assert!(builder
            .append(Packet::continuation(TEST_CHANNEL, 0, vec![8; cont_payload_len - 1])?)
            .is_err());

        // Sequence number has to be sequential.
        let mut builder = MessageBuilder::new(init_packet.clone())?;
        assert!(builder
            .append(Packet::continuation(TEST_CHANNEL, 1, vec![8; cont_payload_len])?)
            .is_err());
        Ok(())
    }

    #[test]
    fn build_incomplete_messagebuilder() -> Result<(), Error> {
        let builder = MessageBuilder::new(Packet::initialization(
            TEST_CHANNEL,
            TEST_COMMAND,
            100,
            vec![7; 10],
        )?)?;
        assert!(Message::try_from(builder).is_err());
        Ok(())
    }

    #[test]
    fn append_to_complete_messagebuilder() -> Result<(), Error> {
        let mut builder = MessageBuilder::new(Packet::initialization(
            TEST_CHANNEL,
            TEST_COMMAND,
            22,
            vec![7; 10],
        )?)?;
        assert_eq!(
            builder.append(Packet::continuation(TEST_CHANNEL, 0, vec![8; 12])?)?,
            BuilderStatus::Complete
        );
        assert!(builder.append(Packet::continuation(TEST_CHANNEL, 1, vec![8; 12])?).is_err());
        Ok(())
    }
}

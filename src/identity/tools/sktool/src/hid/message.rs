// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hid::command::Command;
use crate::hid::packet::{Packet, CONT_HEADER_LENGTH, INIT_HEADER_LENGTH};
use crate::hid::util::pad;
use anyhow::{format_err, Error};
use bytes::{BufMut, Bytes, BytesMut};
use std::convert::TryFrom;
use std::fmt;

/// The maximum nuber of continuation packets in a message, as defined by the CTAPHID spec.
const MAX_CONT_PACKET_COUNT: u8 = 128;

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
    #[allow(dead_code)]
    pub fn channel(&self) -> u32 {
        self.channel
    }

    /// Returns the command of this message.
    #[allow(dead_code)]
    pub fn command(&self) -> Command {
        self.command
    }

    /// Returns the payload of this message, without any padding.
    pub fn payload(&self) -> Bytes {
        self.payload.slice_to(self.payload_length as usize)
    }

    /// Returns the length of packets this message iterates over.
    #[allow(dead_code)]
    pub fn packet_length(&self) -> u16 {
        self.packet_length
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

/// The internal state of a `MessageBuilder` that has received one or more packets
struct MessageBuilderState {
    /// The unique channel identifier for the client.
    channel: u32,
    /// The meaning of the message.
    command: Command,
    /// The length of the data within the message before any padding.
    payload_length: u16,
    /// A `BytesMut` object sized to contain the entire message, populated with the data received
    /// to date.
    payload: BytesMut,
    /// The sequence number expected in the next continuation packet.
    next_sequence: u8,
    /// The length of packets being supplied.
    packet_length: u16,
}

impl MessageBuilderState {
    /// Returns a status indicating whether the `MessageBuilderState` is ready to be converted into
    /// a `Message`.
    pub fn status(&self) -> BuilderStatus {
        if self.next_sequence >= cont_packet_count(self.payload_length, self.packet_length) {
            BuilderStatus::Complete
        } else {
            BuilderStatus::Incomplete
        }
    }
}

/// A builder to assemble a CTAPHID `Message` from CTAPHID `Packet` objects received over a
/// `Connection`.
pub struct MessageBuilder {
    /// The internal state of the message builder, populated on append of the first packet.
    state: Option<MessageBuilderState>,
}

impl MessageBuilder {
    /// Creates a new empty `MessageBuilder`.
    pub fn new() -> MessageBuilder {
        MessageBuilder { state: None }
    }

    /// Add a packet to this `MessageBuilder`. If successful, returns a status indicating whether
    /// the `MessageBuilder` is ready to be converted to a `Message`.
    pub fn append(&mut self, packet: Packet) -> Result<BuilderStatus, Error> {
        match packet {
            Packet::Initialization {
                channel,
                command,
                message_length,
                payload: packet_payload,
            } => {
                if self.state.is_some() {
                    return Err(format_err!("Subsequent packet was not a continuation packet"));
                }
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
                self.state.replace(MessageBuilderState {
                    channel,
                    command,
                    payload_length: message_length,
                    payload: message_payload,
                    next_sequence: 0,
                    packet_length,
                });
                Ok(self.status())
            }
            Packet::Continuation { channel, sequence, payload: packet_payload } => {
                match self.state.as_mut() {
                    None => Err(format_err!("First packet was not an initialization packet")),
                    Some(state) => {
                        if state.status() == BuilderStatus::Complete {
                            return Err(format_err!(
                                "Cannot append to a builder that is already complete"
                            ));
                        }
                        if channel != state.channel {
                            return Err(format_err!(
                                "Appended packet channel ({:?}) does not match \
                                initialization channel ({:?})",
                                channel,
                                state.channel
                            ));
                        }
                        let packet_length = CONT_HEADER_LENGTH + packet_payload.len() as u16;
                        if packet_length != state.packet_length {
                            return Err(format_err!(
                                "Appended packet length ({:?}) does not match \
                                initialization length ({:?})",
                                packet_length,
                                state.packet_length
                            ));
                        }
                        if sequence != state.next_sequence {
                            return Err(format_err!(
                                "Appended packet sequence ({:?}) does not match \
                                expectation ({:?})",
                                sequence,
                                state.next_sequence
                            ));
                        }
                        state.payload.put(packet_payload);
                        state.next_sequence += 1;
                        Ok(state.status())
                    }
                }
            }
        }
    }

    /// Returns a status indicating whether the `MessageBuilder` is ready to be converted into a
    /// `Message`.
    pub fn status(&self) -> BuilderStatus {
        match &self.state {
            None => BuilderStatus::Incomplete,
            Some(state) => state.status(),
        }
    }
}

impl TryFrom<MessageBuilder> for Message {
    type Error = anyhow::Error;

    fn try_from(builder: MessageBuilder) -> Result<Message, Error> {
        match builder.state {
            None => Err(format_err!("Cannot create a message from an empty builder")),
            Some(state) => match state.status() {
                BuilderStatus::Complete => Ok(Message {
                    channel: state.channel,
                    command: state.command,
                    payload: Bytes::from(state.payload),
                    payload_length: state.payload_length,
                    packet_length: state.packet_length,
                }),
                BuilderStatus::Incomplete => {
                    Err(format_err!("Cannot create a message from an incomplete builder"))
                }
            },
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

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_CHANNEL: u32 = 0x89abcdef;
    const WRONG_CHANNEL: u32 = 0x98badcfe;
    const TEST_COMMAND: Command = Command::Wink;
    const TEST_PACKET_LENGTH: u16 = 10;

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
        let packets: Vec<Packet> = message.into_iter().collect();
        assert_eq!(packets, expected_packets);

        let mut builder = MessageBuilder::new();
        for packet in packets {
            let received_status = builder.append(packet)?;
            assert_eq!(received_status, builder.status());
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
        let mut builder = MessageBuilder::new();
        assert!(builder.append(Packet::continuation(TEST_CHANNEL, 0, vec![0x00])?).is_err());
        // Init packet has to infer less than 128 continue packets.
        let mut builder = MessageBuilder::new();
        assert!(builder
            .append(Packet::initialization(TEST_CHANNEL, TEST_COMMAND, 10000, vec![7; 10])?)
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
        let mut builder = MessageBuilder::new();
        builder.append(init_packet.clone())?;
        assert_eq!(
            builder.append(Packet::continuation(TEST_CHANNEL, 0, vec![8; cont_payload_len])?)?,
            BuilderStatus::Incomplete
        );

        // Channel of continuation packet has to match.
        let mut builder = MessageBuilder::new();
        builder.append(init_packet.clone())?;
        assert!(builder
            .append(Packet::continuation(WRONG_CHANNEL, 0, vec![8; cont_payload_len])?)
            .is_err());

        // Length of continuation packet has to match.
        let mut builder = MessageBuilder::new();
        builder.append(init_packet.clone())?;
        assert!(builder
            .append(Packet::continuation(TEST_CHANNEL, 0, vec![8; cont_payload_len - 1])?)
            .is_err());

        // Sequence number has to be sequential.
        let mut builder = MessageBuilder::new();
        builder.append(init_packet.clone())?;
        assert!(builder
            .append(Packet::continuation(TEST_CHANNEL, 1, vec![8; cont_payload_len])?)
            .is_err());
        Ok(())
    }

    #[test]
    fn build_empty_messagebuilder() -> Result<(), Error> {
        let builder = MessageBuilder::new();
        assert!(Message::try_from(builder).is_err());
        Ok(())
    }

    #[test]
    fn build_incomplete_messagebuilder() -> Result<(), Error> {
        let mut builder = MessageBuilder::new();
        builder.append(Packet::initialization(TEST_CHANNEL, TEST_COMMAND, 100, vec![7; 10])?)?;
        assert!(Message::try_from(builder).is_err());
        Ok(())
    }

    #[test]
    fn append_to_complete_messagebuilder() -> Result<(), Error> {
        let mut builder = MessageBuilder::new();
        builder.append(Packet::initialization(TEST_CHANNEL, TEST_COMMAND, 22, vec![7; 10])?)?;
        assert_eq!(
            builder.append(Packet::continuation(TEST_CHANNEL, 0, vec![8; 12])?)?,
            BuilderStatus::Complete
        );
        assert!(builder.append(Packet::continuation(TEST_CHANNEL, 1, vec![8; 12])?).is_err());
        Ok(())
    }
}

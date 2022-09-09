// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bytes::Bytes,
    fidl_fuchsia_fido_report::CtapHidCommand,
    std::convert::{TryFrom, TryInto},
    std::fmt,
};

/// A CTAPHID message to be either received or sent over a `Connection`.
#[derive(PartialEq, Clone)]
pub struct Message {
    /// The unique channel identifier for the client.
    channel: u32,
    /// The meaning of the message.
    command: CtapHidCommand,
    /// The data carried within the message, padded to the next packet boundary.
    payload: Bytes,
    /// The length of the data within the message before any paddding.
    payload_length: u16,
}

impl Message {
    /// Creates a new message containing the supplied payload.
    pub fn new(channel: u32, command: CtapHidCommand, payload: &[u8]) -> Result<Self, Error> {
        let payload_length = u16::try_from(payload.len()).map_err(|_| {
            format_err!("Payload length {} exceeds max theoretical size", payload.len())
        })?;
        Ok(Self { channel, command, payload: Vec::from(payload).into(), payload_length })
    }

    /// Returns the channel of this message. Used in tests.
    #[allow(dead_code)]
    pub fn channel(&self) -> u32 {
        self.channel
    }

    /// Returns the command of this message.
    pub fn command(&self) -> CtapHidCommand {
        self.command
    }

    /// Returns the payload of this message, without any padding.
    pub fn payload(&self) -> Bytes {
        self.payload.slice(..self.payload_length as usize)
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

impl From<fidl_fuchsia_fido_report::Message> for Message {
    fn from(value: fidl_fuchsia_fido_report::Message) -> Self {
        let channel = value.channel_id.unwrap();
        let command = value.command_id.unwrap();
        let payload: Bytes = value.data.unwrap().into();
        let payload_length = value.payload_len.unwrap();
        return Message { channel, command, payload, payload_length };
    }
}

impl TryInto<fidl_fuchsia_fido_report::Message> for Message {
    type Error = anyhow::Error;

    fn try_into(self) -> Result<fidl_fuchsia_fido_report::Message, Error> {
        return Ok(fidl_fuchsia_fido_report::Message {
            channel_id: Some(self.channel),
            command_id: Some(self.command),
            data: Some(self.payload.to_vec()),
            payload_len: Some(self.payload_length),
            ..fidl_fuchsia_fido_report::Message::EMPTY
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_CHANNEL: u32 = 0x89abcdef;
    const TEST_COMMAND: CtapHidCommand = CtapHidCommand::Wink;

    #[test]
    fn message_getters() -> Result<(), Error> {
        let message = Message::new(TEST_CHANNEL, TEST_COMMAND, &vec![0xff, 0xee])?;
        assert_eq!(message.channel(), TEST_CHANNEL);
        assert_eq!(message.command(), TEST_COMMAND);
        assert_eq!(message.payload(), &vec![0xff, 0xee]);
        Ok(())
    }

    #[test]
    fn new_message_too_large() -> Result<(), Error> {
        // This message should fail because the payload is over 16 bit length, even though it would
        // not require more than 127 packets.
        assert!(Message::new(TEST_CHANNEL, TEST_COMMAND, &vec![7; 100000]).is_err());
        Ok(())
    }
}

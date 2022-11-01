// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Netboot messaging types.
//!
//! The Netboot protocol is used to issue commands and discover netsvc nodes.
//!
//! Note: There's not an RFC or standard for this protocol, as it has evolved
//! over time with the netsvc code. The closest to an authoritative source is
//! the netsvc source code in `//src/bringup/bin/netsvc`.

use const_unwrap::const_unwrap_option;
use packet::{
    BufferView, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use std::{
    convert::{TryFrom, TryInto},
    num::NonZeroU16,
};
use zerocopy::{
    byteorder::little_endian::U32, AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned,
};

// Re-export witness type.
pub use witness::ErrorValue;

/// The UDP port a netboot server listens on.
pub const SERVER_PORT: NonZeroU16 = const_unwrap_option(NonZeroU16::new(33330));
/// The UDP port multicast advertisements are sent to.
pub const ADVERT_PORT: NonZeroU16 = const_unwrap_option(NonZeroU16::new(33331));

const MAGIC: u32 = 0xAA774217;

mod witness {
    /// A witness type for error values observed from the netboot protocol.
    ///
    /// An instance of [`ErrorValue`] *always* represents a `u32` with the MSB
    /// set.
    #[derive(Debug, Copy, Clone, Eq, PartialEq)]
    pub struct ErrorValue(u32);

    impl ErrorValue {
        const ERROR_MASK: u32 = 0x80000000;

        /// Creates a new [`ErrorValue`] from `v`, returning `Some` if `v` has
        /// the MSB set, matching the protocol definition.
        pub const fn new(v: u32) -> Option<Self> {
            if v & Self::ERROR_MASK != 0 {
                Some(Self(v))
            } else {
                None
            }
        }
    }

    impl From<ErrorValue> for u32 {
        fn from(v: ErrorValue) -> Self {
            let ErrorValue(v) = v;
            v
        }
    }
}

/// Operations accepted by netboot servers.
///
/// `payload` and `arg` semantics vary depending on the opcode.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum Opcode {
    Command,
    SendFile,
    Data,
    Boot,
    Query,
    ShellCmd,
    Open,
    Read,
    Write,
    Close,
    LastData,
    Reboot,
    GetAdvert,
    Ack,
    FileReceived,
    Advertise,
}

impl From<Opcode> for u32 {
    fn from(value: Opcode) -> u32 {
        match value {
            Opcode::Command => 1,
            Opcode::SendFile => 2,
            Opcode::Data => 3,
            Opcode::Boot => 4,
            Opcode::Query => 5,
            Opcode::ShellCmd => 6,
            Opcode::Open => 7,
            Opcode::Read => 8,
            Opcode::Write => 9,
            Opcode::Close => 10,
            Opcode::LastData => 11,
            Opcode::Reboot => 12,
            Opcode::GetAdvert => 13,
            Opcode::Ack => 0,
            Opcode::FileReceived => 0x70000001,
            Opcode::Advertise => 0x77777777,
        }
    }
}

/// Protocol errors.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum ErrorCode {
    BadCommand,
    BadParam,
    TooLarge,
    BadFile,
    Unknown(ErrorValue),
}

impl From<ErrorCode> for u32 {
    fn from(value: ErrorCode) -> u32 {
        match value {
            ErrorCode::BadCommand => 0x80000001,
            ErrorCode::BadParam => 0x80000002,
            ErrorCode::TooLarge => 0x80000003,
            ErrorCode::BadFile => 0x80000004,
            ErrorCode::Unknown(v) => v.into(),
        }
    }
}

/// Part of a netboot message's preamble.
///
/// Every message is marked with either an opcode or an error.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum OpcodeOrErr {
    Op(Opcode),
    Err(ErrorCode),
}

impl From<Opcode> for OpcodeOrErr {
    fn from(op: Opcode) -> Self {
        OpcodeOrErr::Op(op)
    }
}

impl From<ErrorCode> for OpcodeOrErr {
    fn from(err: ErrorCode) -> Self {
        OpcodeOrErr::Err(err)
    }
}

impl TryFrom<u32> for OpcodeOrErr {
    type Error = u32;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Opcode::Command.into()),
            2 => Ok(Opcode::SendFile.into()),
            3 => Ok(Opcode::Data.into()),
            4 => Ok(Opcode::Boot.into()),
            5 => Ok(Opcode::Query.into()),
            6 => Ok(Opcode::ShellCmd.into()),
            7 => Ok(Opcode::Open.into()),
            8 => Ok(Opcode::Read.into()),
            9 => Ok(Opcode::Write.into()),
            10 => Ok(Opcode::Close.into()),
            11 => Ok(Opcode::LastData.into()),
            12 => Ok(Opcode::Reboot.into()),
            13 => Ok(Opcode::GetAdvert.into()),
            0 => Ok(Opcode::Ack.into()),
            0x70000001 => Ok(Opcode::FileReceived.into()),
            0x77777777 => Ok(Opcode::Advertise.into()),
            0x80000001 => Ok(ErrorCode::BadCommand.into()),
            0x80000002 => Ok(ErrorCode::BadParam.into()),
            0x80000003 => Ok(ErrorCode::TooLarge.into()),
            0x80000004 => Ok(ErrorCode::BadFile.into()),
            v => match ErrorValue::new(v) {
                Some(e) => Ok(ErrorCode::Unknown(e).into()),
                None => Err(v),
            },
        }
    }
}

impl From<OpcodeOrErr> for u32 {
    fn from(cmd: OpcodeOrErr) -> Self {
        match cmd {
            OpcodeOrErr::Op(op) => op.into(),
            OpcodeOrErr::Err(e) => e.into(),
        }
    }
}

/// Error parsing a netboot message.
#[derive(Debug)]
pub enum ParseError {
    Malformed,
    UnknownOpcode(u32),
    BadMagic,
}

#[repr(C)]
#[derive(FromBytes, AsBytes, Unaligned, Debug)]
struct MessageHead {
    magic: U32,
    cookie: U32,
    cmd: U32,
    arg: U32,
}

/// A netboot packet.
#[derive(Debug)]
pub struct NetbootPacket<B: ByteSlice> {
    command: OpcodeOrErr,
    message: LayoutVerified<B, MessageHead>,
    payload: B,
}

impl<B: ByteSlice> NetbootPacket<B> {
    pub fn command(&self) -> OpcodeOrErr {
        self.command
    }

    pub fn cookie(&self) -> u32 {
        self.message.cookie.get()
    }

    pub fn arg(&self) -> u32 {
        self.message.arg.get()
    }

    pub fn payload(&self) -> &[u8] {
        self.payload.as_ref()
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for NetbootPacket<B> {
    type Error = ParseError;

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        let message = buffer.take_obj_front::<MessageHead>().ok_or(ParseError::Malformed)?;
        if message.magic.get() != MAGIC {
            return Err(ParseError::BadMagic);
        }
        let opcode = message.cmd.get().try_into().map_err(ParseError::UnknownOpcode)?;
        let payload = buffer.into_rest();
        Ok(Self { command: opcode, message, payload })
    }

    fn parse_metadata(&self) -> ParseMetadata {
        // ParseMetadata is only needed if we do undo parse.
        // See GrowBuffer::undo_parse for info.
        unimplemented!()
    }
}

/// A [`PacketBuilder`] for the netboot protocol.
pub struct NetbootPacketBuilder {
    cmd: OpcodeOrErr,
    cookie: u32,
    arg: u32,
}

impl NetbootPacketBuilder {
    pub fn new(cmd: OpcodeOrErr, cookie: u32, arg: u32) -> Self {
        Self { cmd, cookie, arg }
    }
}

impl PacketBuilder for NetbootPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(std::mem::size_of::<MessageHead>(), 0, 0, std::usize::MAX)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let mut buffer = buffer.header();
        let mut bv = crate::as_buffer_view_mut(&mut buffer);
        let mut message = bv.take_obj_front::<MessageHead>().expect("not enough space in buffer");
        let MessageHead { magic, cookie, cmd, arg } = &mut *message;
        magic.set(MAGIC);
        cookie.set(self.cookie);
        arg.set(self.arg);
        cmd.set(self.cmd.into());
    }
}

#[cfg(test)]
mod tests {

    use super::*;

    use assert_matches::assert_matches;
    use packet::{InnerPacketBuilder as _, ParseBuffer as _, Serializer as _};

    #[test]
    fn test_parse_serialize() {
        const PAYLOAD: [u8; 10] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
        let mut pkt = (&PAYLOAD[..])
            .into_serializer()
            .serialize_vec(NetbootPacketBuilder::new(Opcode::Ack.into(), 3, 4))
            .expect("failed to serialize");
        let parsed = pkt.parse::<NetbootPacket<_>>().expect("failed to parse");
        assert_eq!(parsed.command(), OpcodeOrErr::Op(Opcode::Ack));
        assert_eq!(parsed.cookie(), 3);
        assert_eq!(parsed.arg(), 4);
        assert_eq!(parsed.payload(), &PAYLOAD[..]);
    }

    #[test]
    fn test_parse_serialize_opcodes() {
        const TEST_OPCODES: [Opcode; 16] = [
            Opcode::Command,
            Opcode::SendFile,
            Opcode::Data,
            Opcode::Boot,
            Opcode::Query,
            Opcode::ShellCmd,
            Opcode::Open,
            Opcode::Read,
            Opcode::Write,
            Opcode::Close,
            Opcode::LastData,
            Opcode::Reboot,
            Opcode::GetAdvert,
            Opcode::Ack,
            Opcode::FileReceived,
            Opcode::Advertise,
        ];

        for opcode in TEST_OPCODES.iter() {
            match opcode {
                Opcode::Command
                | Opcode::SendFile
                | Opcode::Data
                | Opcode::Boot
                | Opcode::Query
                | Opcode::ShellCmd
                | Opcode::Open
                | Opcode::Read
                | Opcode::Write
                | Opcode::Close
                | Opcode::LastData
                | Opcode::Reboot
                | Opcode::GetAdvert
                | Opcode::Ack
                | Opcode::FileReceived
                | Opcode::Advertise => {
                    // Change detector so new op codes are added to the test
                    // array above.
                }
            }
            let opcode_or_err = OpcodeOrErr::try_from(u32::from(*opcode)).expect("failed to parse");
            assert_matches!(opcode_or_err, OpcodeOrErr::Op(op) if op == *opcode);
        }
    }

    #[test]
    fn test_parse_serialize_error_codes() {
        let test_error_codes = [
            ErrorCode::BadCommand,
            ErrorCode::BadParam,
            ErrorCode::TooLarge,
            ErrorCode::BadFile,
            ErrorCode::Unknown(ErrorValue::new(0x80001234).unwrap()),
        ];
        for error in test_error_codes.iter() {
            match error {
                ErrorCode::BadCommand
                | ErrorCode::BadParam
                | ErrorCode::TooLarge
                | ErrorCode::BadFile
                | ErrorCode::Unknown(_) => {
                    // Change detector so new error codes are added to the test
                    // array above.
                }
            }
            let opcode_or_err = OpcodeOrErr::try_from(u32::from(*error)).expect("failed to parse");
            assert_matches!(opcode_or_err, OpcodeOrErr::Err(e) if e == *error);
        }
    }
}

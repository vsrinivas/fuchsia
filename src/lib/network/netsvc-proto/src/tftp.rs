// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TFTP library.
//!
//! Netsvc uses TFTP for file transfers. TFTP is defined in [RFC 1350]. Netsvc
//! also incorporates the following extensions:
//!  * Option extension [RFC 2347].
//!  * Block size option [RFC 2348].
//!  * Timeout interval, and transfer size options [RFC 2349].
//!  * Windows size option [RFC 7440].
//!
//! Over the years netsvc also introduces some fuchsia-specific extensions,
//! which are called out in documentation.
//!
//! [RFC 1350]: https://datatracker.ietf.org/doc/html/rfc1350
//! [RFC 2347]: https://datatracker.ietf.org/doc/html/rfc2347
//! [RFC 2348]: https://datatracker.ietf.org/doc/html/rfc2348
//! [RFC 2349]: https://datatracker.ietf.org/doc/html/rfc2349
//! [RFC 7440]: https://datatracker.ietf.org/doc/html/rfc7440

use crate::ValidStr;
use const_unwrap::const_unwrap_option;
use packet::{
    BufferView, InnerPacketBuilder, PacketBuilder, PacketConstraints, ParsablePacket,
    ParseMetadata, SerializeBuffer,
};
use std::{convert::TryFrom, io::Write as _, num::NonZeroU16, str::FromStr};
use thiserror::Error;
use witness::NonEmptyValidStr;
use zerocopy::{
    byteorder::network_endian::U16, AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified,
    Unaligned,
};

/// The port netsvc uses to send TFTP traffic from.
pub const OUTGOING_PORT: NonZeroU16 = const_unwrap_option(NonZeroU16::new(33340));

/// The port netsvc uses to listen to TFTP traffic.
pub const INCOMING_PORT: NonZeroU16 = const_unwrap_option(NonZeroU16::new(33341));

/// The default block size option value, according to [RFC 1350].
///
/// [RFC 1350]: https://datatracker.ietf.org/doc/html/rfc1350
pub const DEFAULT_BLOCK_SIZE_OPTION: u16 = 512;

/// The default window size option value, according to [RFC 1350].
///
/// [RFC 1350]: https://datatracker.ietf.org/doc/html/rfc1350
pub const DEFAULT_WINDOW_SIZE_OPTION: u16 = 512;

/// The default timeout option value used in netsvc.
pub const DEFAULT_TIMEOUT_SECS_OPTION: u16 = 1;

mod witness {
    use crate::ValidStr;

    /// A witness type for non empty [`ValidStr`]s.
    #[derive(Debug)]
    pub(super) struct NonEmptyValidStr<B: zerocopy::ByteSlice>(ValidStr<B>);

    impl<B: zerocopy::ByteSlice> NonEmptyValidStr<B> {
        /// Creates a new [`NonEmptyValidStr`] iff `str` is not empty.
        pub(super) fn new(str: ValidStr<B>) -> Option<Self> {
            if str.as_str().is_empty() {
                None
            } else {
                Some(Self(str))
            }
        }
    }

    impl<B: zerocopy::ByteSlice> std::ops::Deref for NonEmptyValidStr<B> {
        type Target = ValidStr<B>;
        fn deref(&self) -> &Self::Target {
            let Self(b) = self;
            b
        }
    }
}

/// Error codes defined in [RFC 1350 appendix I].
///
/// [RFC 1350 appendix I]: https://datatracker.ietf.org/doc/html/rfc1350#appendix-I
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum TftpError {
    Undefined,
    FileNotFound,
    AccessViolation,
    DiskFull,
    IllegalOperation,
    UnknownTransferId,
    FileAlreadyExists,
    NoSuchUser,
    /// Introduced in [RFC 2347].
    ///
    /// [RFC 2347]: https://datatracker.ietf.org/doc/html/rfc2347.
    BadOptions,
    /// Fuchsia-specific error code.
    ///
    /// BUSY is sent by a server as a response to a RRQ or WRQ, and indicates
    /// that the server is unavailable to process the request at the moment
    /// (but expects to be able to handle it at some time in the future).
    Busy,
    Unknown(u16),
}

impl Into<u16> for TftpError {
    fn into(self) -> u16 {
        match self {
            TftpError::Undefined => 0,
            TftpError::FileNotFound => 1,
            TftpError::AccessViolation => 2,
            TftpError::DiskFull => 3,
            TftpError::IllegalOperation => 4,
            TftpError::UnknownTransferId => 5,
            TftpError::FileAlreadyExists => 6,
            TftpError::NoSuchUser => 7,
            TftpError::BadOptions => 8,
            TftpError::Busy => 0x143,
            TftpError::Unknown(v) => v,
        }
    }
}

/// Fields that are parsed as strings.
#[derive(Debug, Eq, PartialEq, Clone)]
pub enum StringField {
    OptionName,
    OptionValue,
    Filename,
    TransferMode,
}

/// Kinds of observable [`ParseError`]s.
#[derive(Debug, Eq, PartialEq, Clone, Error)]
pub enum ParseError {
    #[error("invalid message length")]
    InvalidLength,
    #[error("invalid empty string for {0:?}")]
    EmptyStringValue(StringField),
    #[error("failed to parse string: {0}")]
    BadString(crate::ValidStrError),
    #[error("bad option `{name} = {value}`")]
    BadOption { name: String, value: String },
    #[error("bad value for option `{name}`: {error}")]
    BadOptionValue { name: String, error: std::num::ParseIntError },
    #[error("unrecognized transfer mode `{0}`")]
    UnrecognizedTransferMode(String),
    #[error("invalid opcode: {0}")]
    InvalidOpcode(u16),
    #[error("too many options, dropped {0:?}")]
    TooManyOptions(Forceable<TftpOption>),
}

impl From<u16> for TftpError {
    fn from(value: u16) -> Self {
        match value {
            0 => TftpError::Undefined,
            1 => TftpError::FileNotFound,
            2 => TftpError::AccessViolation,
            3 => TftpError::DiskFull,
            4 => TftpError::IllegalOperation,
            5 => TftpError::UnknownTransferId,
            6 => TftpError::FileAlreadyExists,
            7 => TftpError::NoSuchUser,
            8 => TftpError::BadOptions,
            0x143 => TftpError::Busy,
            unknown => TftpError::Unknown(unknown),
        }
    }
}

/// TFTP Opcodes as defined in [RFC 1350 section 5].
///
/// [RFC 1350 section 5]: https://datatracker.ietf.org/doc/html/rfc1350#section-5
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum Opcode {
    ReadRequest,
    WriteRequest,
    Data,
    Ack,
    Error,
    /// Introduced in [RFC 2347].
    ///
    /// [RFC 2347]: https://datatracker.ietf.org/doc/html/rfc2347.
    OptionAck,
}

impl Into<u16> for Opcode {
    fn into(self) -> u16 {
        match self {
            Opcode::ReadRequest => 1,
            Opcode::WriteRequest => 2,
            Opcode::Data => 3,
            Opcode::Ack => 4,
            Opcode::Error => 5,
            Opcode::OptionAck => 6,
        }
    }
}

impl TryFrom<u16> for Opcode {
    type Error = ParseError;

    fn try_from(value: u16) -> Result<Self, ParseError> {
        match value {
            1 => Ok(Opcode::ReadRequest),
            2 => Ok(Opcode::WriteRequest),
            3 => Ok(Opcode::Data),
            4 => Ok(Opcode::Ack),
            5 => Ok(Opcode::Error),
            6 => Ok(Opcode::OptionAck),
            opcode => Err(ParseError::InvalidOpcode(opcode)),
        }
    }
}

/// Helper structure to encode forced values, a Fuchsia-specific extension to
/// options.
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct Forceable<T> {
    pub value: T,
    pub forced: bool,
}

/// A collection of options in a TFTP message.
#[derive(Debug, Default)]
pub struct OptionCollection(arrayvec::ArrayVec<Forceable<TftpOption>, MAX_OPTIONS>);

impl OptionCollection {
    /// Returns an iterator over the contained options.
    pub fn iter(&self) -> impl Iterator<Item = &Forceable<TftpOption>> {
        let Self(this) = self;
        this.iter()
    }

    /// Pushes a new option.
    ///
    /// Returns an error if if this [`OptionCollection`] already contains
    /// [`MAX_OPTIONS`].
    pub fn try_push(&mut self, option: Forceable<TftpOption>) -> Result<(), Forceable<TftpOption>> {
        let Self(this) = self;
        this.try_push(option).map_err(|e| e.element())
    }

    /// Gets the total serialized length of the contained options.
    pub fn serialized_len(&self) -> usize {
        self.iter().map(|o| o.serialized_len()).sum()
    }

    fn parse<B: ByteSlice, BV: BufferView<B>>(buffer: &mut BV) -> Result<Self, ParseError> {
        // options always come at the end, we'll try to gather options until the
        // buffer is exhausted or we've already gathered as many options as we
        // can fit
        let Self(mut vec) = Self::default();
        while !buffer.is_empty() {
            let name = NonEmptyValidStr::new(
                ValidStr::new_null_terminated_from_buffer(buffer).map_err(ParseError::BadString)?,
            )
            .ok_or(ParseError::EmptyStringValue(StringField::OptionName))?;
            let value = NonEmptyValidStr::new(
                ValidStr::new_null_terminated_from_buffer(buffer).map_err(ParseError::BadString)?,
            )
            .ok_or(ParseError::EmptyStringValue(StringField::OptionValue))?;

            let option = TftpOption::parse(name.as_ref(), value.as_ref())?;
            let () = vec.try_push(option).map_err(|e| ParseError::TooManyOptions(e.element()))?;
        }
        Ok(Self(vec))
    }

    fn serialize<B: ByteSliceMut, BV: BufferView<B>>(&self, buffer: &mut BV) {
        self.iter().for_each(|v| v.serialize(buffer))
    }

    /// Collects the containing options into [`AllOptions`].
    pub fn collect(&self) -> AllOptions {
        self.iter().cloned().collect()
    }
}

impl std::iter::FromIterator<Forceable<TftpOption>> for OptionCollection {
    fn from_iter<T: IntoIterator<Item = Forceable<TftpOption>>>(iter: T) -> Self {
        Self(iter.into_iter().collect())
    }
}

/// A container with all possible [`TftpOption`] values in a message.
#[derive(Default, Eq, PartialEq, Debug)]
pub struct AllOptions {
    pub transfer_size: Option<Forceable<u64>>,
    pub window_size: Option<Forceable<u16>>,
    pub timeout: Option<Forceable<u8>>,
    pub block_size: Option<Forceable<u16>>,
}

/// Constructs an [`AllOptions`] from an iterator of [`Forceable<TftpOption>`].
///
/// If the same option appears more than once in the iterator, the later value
/// is kept.
impl std::iter::FromIterator<Forceable<TftpOption>> for AllOptions {
    fn from_iter<T: IntoIterator<Item = Forceable<TftpOption>>>(iter: T) -> Self {
        iter.into_iter().fold(Self::default(), |mut all_options, Forceable { value, forced }| {
            match value {
                TftpOption::TransferSize(value) => {
                    all_options.transfer_size = Some(Forceable { value, forced })
                }
                TftpOption::BlockSize(value) => {
                    all_options.block_size = Some(Forceable { value, forced })
                }
                TftpOption::Timeout(value) => {
                    all_options.timeout = Some(Forceable { value, forced })
                }
                TftpOption::WindowSize(value) => {
                    all_options.window_size = Some(Forceable { value, forced })
                }
            }
            all_options
        })
    }
}

/// The body of a Read or Write request.
#[derive(Debug)]
pub struct RequestBody<B: ByteSlice> {
    filename: NonEmptyValidStr<B>,
    mode: TftpMode,
    options: OptionCollection,
}

impl<B> RequestBody<B>
where
    B: ByteSlice,
{
    fn parse<BV: BufferView<B>>(buffer: &mut BV) -> Result<Self, ParseError> {
        let filename = ValidStr::new_null_terminated_from_buffer(buffer)
            .map_err(ParseError::BadString)
            .and_then(|s| {
                NonEmptyValidStr::new(s).ok_or(ParseError::EmptyStringValue(StringField::Filename))
            })?;

        let mode = TftpMode::try_from(
            ValidStr::new_null_terminated_from_buffer(buffer)
                .map_err(ParseError::BadString)
                .and_then(|s| {
                    NonEmptyValidStr::new(s)
                        .ok_or(ParseError::EmptyStringValue(StringField::TransferMode))
                })?
                .as_ref(),
        )?;
        let options = OptionCollection::parse(buffer)?;
        Ok(Self { filename, mode, options })
    }

    pub fn filename(&self) -> &str {
        self.filename.as_ref()
    }

    pub fn mode(&self) -> TftpMode {
        self.mode
    }

    pub fn options(&self) -> &OptionCollection {
        &self.options
    }
}

/// The body of a data message.
#[derive(Debug)]
pub struct DataBody<B: ByteSlice> {
    block: LayoutVerified<B, U16>,
    payload: B,
}

impl<B> DataBody<B>
where
    B: ByteSlice,
{
    fn parse<BV: BufferView<B>>(buffer: &mut BV) -> Result<Self, ParseError> {
        let block = buffer.take_obj_front::<U16>().ok_or(ParseError::InvalidLength)?;
        let payload = buffer.take_rest_front();
        Ok(Self { block, payload })
    }

    pub fn block(&self) -> u16 {
        self.block.get()
    }

    pub fn payload(&self) -> &B {
        &self.payload
    }
}

/// The body of an Ack message.
#[derive(Debug)]
pub struct AckBody<B: ByteSlice> {
    block: LayoutVerified<B, U16>,
}

impl<B> AckBody<B>
where
    B: ByteSlice,
{
    fn parse<BV: BufferView<B>>(buffer: &mut BV) -> Result<Self, ParseError> {
        let block = buffer.take_obj_front::<U16>().ok_or(ParseError::InvalidLength)?;
        Ok(Self { block })
    }

    pub fn block(&self) -> u16 {
        self.block.get()
    }
}

/// The body of an error message.
#[derive(Debug)]
pub struct ErrorBody<B: ByteSlice> {
    error: TftpError,
    msg: ValidStr<B>,
}

impl<B> ErrorBody<B>
where
    B: ByteSlice,
{
    fn parse<BV: BufferView<B>>(buffer: &mut BV) -> Result<Self, ParseError> {
        let error =
            TftpError::from(buffer.take_obj_front::<U16>().ok_or(ParseError::InvalidLength)?.get());
        let msg =
            ValidStr::new_null_terminated_from_buffer(buffer).map_err(ParseError::BadString)?;
        Ok(Self { error, msg })
    }

    pub fn error(&self) -> TftpError {
        self.error
    }

    pub fn message(&self) -> &str {
        self.msg.as_ref()
    }
}

/// The body of an option ack (OACK) message.
#[derive(Debug)]
pub struct OptionAckBody {
    options: OptionCollection,
}

impl OptionAckBody {
    fn parse<B: ByteSlice, BV: BufferView<B>>(buffer: &mut BV) -> Result<Self, ParseError> {
        let options = OptionCollection::parse(buffer)?;
        Ok(Self { options })
    }

    pub fn options(&self) -> &OptionCollection {
        &self.options
    }
}

/// A TFTP packet.
///
/// Implements [`ParsablePacket`] to parse from wire representation.
#[derive(Debug)]
pub enum TftpPacket<B: ByteSlice> {
    ReadRequest(RequestBody<B>),
    WriteRequest(RequestBody<B>),
    Data(DataBody<B>),
    Ack(AckBody<B>),
    Error(ErrorBody<B>),
    OptionAck(OptionAckBody),
}

impl<B: ByteSlice> TftpPacket<B> {
    /// Gets the opcode for the packet.
    pub fn opcode(&self) -> Opcode {
        match self {
            TftpPacket::ReadRequest(_) => Opcode::ReadRequest,
            TftpPacket::WriteRequest(_) => Opcode::WriteRequest,
            TftpPacket::Data(_) => Opcode::Data,
            TftpPacket::Ack(_) => Opcode::Ack,
            TftpPacket::Error(_) => Opcode::Error,
            TftpPacket::OptionAck(_) => Opcode::OptionAck,
        }
    }

    pub fn into_read_request(self) -> Result<RequestBody<B>, Self> {
        match self {
            Self::ReadRequest(r) => Ok(r),
            o => Err(o),
        }
    }

    pub fn into_write_request(self) -> Result<RequestBody<B>, Self> {
        match self {
            Self::WriteRequest(r) => Ok(r),
            o => Err(o),
        }
    }

    pub fn into_data(self) -> Result<DataBody<B>, Self> {
        match self {
            Self::Data(r) => Ok(r),
            o => Err(o),
        }
    }

    pub fn into_ack(self) -> Result<AckBody<B>, Self> {
        match self {
            Self::Ack(r) => Ok(r),
            o => Err(o),
        }
    }

    pub fn into_error(self) -> Result<ErrorBody<B>, Self> {
        match self {
            Self::Error(r) => Ok(r),
            o => Err(o),
        }
    }

    pub fn into_oack(self) -> Result<OptionAckBody, Self> {
        match self {
            Self::OptionAck(r) => Ok(r),
            o => Err(o),
        }
    }
}

impl<B> ParsablePacket<B, ()> for TftpPacket<B>
where
    B: ByteSlice,
{
    type Error = ParseError;

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, ParseError> {
        let opcode: Opcode =
            buffer.take_obj_front::<MessageHead>().ok_or(ParseError::InvalidLength)?.opcode()?;
        Ok(match opcode {
            Opcode::ReadRequest => TftpPacket::ReadRequest(RequestBody::parse(&mut buffer)?),
            Opcode::WriteRequest => TftpPacket::WriteRequest(RequestBody::parse(&mut buffer)?),
            Opcode::Data => TftpPacket::Data(DataBody::parse(&mut buffer)?),
            Opcode::Ack => TftpPacket::Ack(AckBody::parse(&mut buffer)?),
            Opcode::Error => TftpPacket::Error(ErrorBody::parse(&mut buffer)?),
            Opcode::OptionAck => TftpPacket::OptionAck(OptionAckBody::parse(&mut buffer)?),
        })
    }

    fn parse_metadata(&self) -> ParseMetadata {
        // ParseMetadata is only needed if we need to undo parsing.
        unimplemented!()
    }
}

const OPT_NETASCII: unicase::Ascii<&'static str> = unicase::Ascii::new("NETASCII");
const OPT_OCTET: unicase::Ascii<&'static str> = unicase::Ascii::new("OCTET");
const OPT_MAIL: unicase::Ascii<&'static str> = unicase::Ascii::new("MAIL");

/// TFTP transfer modes defined in [RFC 1350].
///
/// [RFC 1350]: https://datatracker.ietf.org/doc/html/rfc1350.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum TftpMode {
    NETASCII,
    OCTET,
    MAIL,
}

impl TftpMode {
    pub fn as_str(&self) -> &'static str {
        match self {
            TftpMode::NETASCII => OPT_NETASCII,
            TftpMode::OCTET => OPT_OCTET,
            TftpMode::MAIL => OPT_MAIL,
        }
        .into_inner()
    }
}

impl Into<&'static str> for TftpMode {
    fn into(self) -> &'static str {
        self.as_str()
    }
}

impl<'a> TryFrom<&'a str> for TftpMode {
    type Error = ParseError;

    fn try_from(value: &'a str) -> Result<Self, Self::Error> {
        let value = unicase::Ascii::new(value);
        // NB: unicase::Ascii can't be used in match patterns
        if value == OPT_NETASCII {
            Ok(TftpMode::NETASCII)
        } else if value == OPT_OCTET {
            Ok(TftpMode::OCTET)
        } else if value == OPT_MAIL {
            Ok(TftpMode::MAIL)
        } else {
            Err(ParseError::UnrecognizedTransferMode(value.to_string()))
        }
    }
}

/// The maximum number of options that a request may carry.
pub const MAX_OPTIONS: usize = 4;

const OPT_TRANSFER_SIZE: unicase::Ascii<&'static str> = unicase::Ascii::new("TSIZE");
const OPT_BLOCK_SIZE: unicase::Ascii<&'static str> = unicase::Ascii::new("BLKSIZE");
const OPT_TIMEOUT: unicase::Ascii<&'static str> = unicase::Ascii::new("TIMEOUT");
const OPT_WINDOWSIZE: unicase::Ascii<&'static str> = unicase::Ascii::new("WINDOWSIZE");

/// TFTP Options.
///
/// TFTP options are introduced in [RFC 2347].
///
/// [RFC 2347]: https://datatracker.ietf.org/doc/html/rfc2347
#[derive(Debug, Eq, PartialEq, Clone)]
pub enum TftpOption {
    /// Transfer size option, as defined in [RFC 2349].
    ///
    /// Encodes the size of the transfer, in bytes.
    /// [RFC 2349]: https://datatracker.ietf.org/doc/html/rfc2349.
    TransferSize(u64),
    /// Block size option, as defined in [RFC 2348].
    ///
    /// The block size is the maximum  number of file bytes that can be
    /// transferred in a single message.
    ///
    /// [RFC 2348]: https://datatracker.ietf.org/doc/html/rfc2348.
    BlockSize(u16),
    /// Timeout configuration option, as defined in [RFC 2349].
    ///
    /// Carries the negotiated timeout, in seconds.
    ///
    /// [RFC 2349]:https://datatracker.ietf.org/doc/html/rfc2349.
    Timeout(u8),
    /// Window size configuration option, as defined in [RFC 7440].
    ///
    /// The window size is the number of data blocks that can be transferred
    /// between acknowledgements.
    ///
    /// [RFC 7440]: https://datatracker.ietf.org/doc/html/rfc7440.
    WindowSize(u16),
}

impl TftpOption {
    pub fn parse(option: &str, value: &str) -> Result<Forceable<Self>, ParseError> {
        let (option, forced) = match option.chars().last() {
            Some('!') => (&option[..option.len() - 1], true),
            Some(_) | None => (option, false),
        };
        let option = unicase::Ascii::new(option);
        let value = if option == OPT_TRANSFER_SIZE {
            u64::from_str(value).map(|v| TftpOption::TransferSize(v))
        } else if option == OPT_BLOCK_SIZE {
            u16::from_str(value).map(|v| TftpOption::BlockSize(v))
        } else if option == OPT_TIMEOUT {
            u8::from_str(value).map(|v| TftpOption::Timeout(v))
        } else if option == OPT_WINDOWSIZE {
            u16::from_str(value).map(|v| TftpOption::WindowSize(v))
        } else {
            return Err(ParseError::BadOption {
                name: option.to_string(),
                value: value.to_string(),
            });
        }
        .map_err(|error| ParseError::BadOptionValue { name: option.to_string(), error })?;

        Ok(Forceable { value, forced })
    }

    fn get_option_and_value(&self) -> (unicase::Ascii<&'static str>, u64) {
        match self {
            TftpOption::TransferSize(v) => (OPT_TRANSFER_SIZE, *v),
            TftpOption::BlockSize(v) => (OPT_BLOCK_SIZE, (*v).into()),
            TftpOption::Timeout(v) => (OPT_TIMEOUT, (*v).into()),
            TftpOption::WindowSize(v) => (OPT_WINDOWSIZE, (*v).into()),
        }
    }

    pub const fn not_forced(self) -> Forceable<TftpOption> {
        Forceable { value: self, forced: false }
    }

    pub const fn forced(self) -> Forceable<TftpOption> {
        Forceable { value: self, forced: true }
    }
}

impl Forceable<TftpOption> {
    /// Gets this options's serialized length.
    ///
    /// `forced` controls whether the option will be forced. Forceable options have
    /// an appended `!` character which increases their length when serialized.
    pub fn serialized_len(&self) -> usize {
        let Forceable { value, forced } = self;
        let (option, value) = value.get_option_and_value();
        let forced = if *forced { 1 } else { 0 };

        #[derive(Default)]
        struct FormattedLen(usize);

        impl std::io::Write for FormattedLen {
            fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
                let Self(counter) = self;
                *counter += buf.len();
                Ok(buf.len())
            }

            fn flush(&mut self) -> std::io::Result<()> {
                Ok(())
            }
        }

        let mut value_len = FormattedLen::default();
        let () =
            std::write!(&mut value_len, "{}", value).expect("failed to serialize value to string");
        let FormattedLen(value_len) = value_len;
        // forced + both string lengths + 2 null termination characters
        forced + option.len() + value_len + 2
    }

    /// Serializes the option into a buffer view.
    pub fn serialize<B: ByteSliceMut, BV: BufferView<B>>(&self, bv: &mut BV) {
        let Forceable { value, forced } = self;
        let (option, value) = value.get_option_and_value();
        write_option_and_value(bv, option.as_ref(), *forced, value);
    }
}

fn write_str<B, BV>(buff: &mut BV, v: &str)
where
    B: ByteSliceMut,
    BV: BufferView<B>,
{
    write_str_forced(buff, v, false)
}

fn write_str_forced<B, BV>(buff: &mut BV, v: &str, forced: bool)
where
    B: ByteSliceMut,
    BV: BufferView<B>,
{
    let extra = if forced { 2 } else { 1 };
    let mut d = buff.take_front(v.len() + extra).unwrap();
    let (data, end) = d.split_at_mut(v.len());
    data.copy_from_slice(v.as_bytes());
    if forced {
        end[0] = '!' as u8;
        end[1] = 0;
    } else {
        end[0] = 0;
    }
}

fn write_option_and_value<B, BV, V>(buff: &mut BV, option: &str, forced: bool, value: V)
where
    B: ByteSliceMut,
    BV: BufferView<B>,
    V: std::fmt::Display,
{
    write_str_forced(buff, option, forced);

    struct BVIoWriter<'a, B, BV>(&'a mut BV, std::marker::PhantomData<B>);

    impl<'a, B, BV> std::io::Write for BVIoWriter<'a, B, BV>
    where
        BV: BufferView<B>,
        B: ByteSliceMut,
    {
        fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
            let Self(bv, std::marker::PhantomData) = self;
            let mut b = bv
                .take_front(buf.len())
                .ok_or(std::io::Error::from(std::io::ErrorKind::OutOfMemory))?;
            b.as_mut().copy_from_slice(buf);
            Ok(b.len())
        }

        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }

    std::write!(&mut BVIoWriter(buff, std::marker::PhantomData), "{}\0", value)
        .unwrap_or_else(|e| panic!("failed to serialize {}: {:?}", value, e));
}

#[repr(C)]
#[derive(FromBytes, AsBytes, Unaligned)]
struct MessageHead {
    opcode: U16,
}

impl MessageHead {
    fn opcode(&self) -> Result<Opcode, ParseError> {
        // NB: We mask the opcode here because Fuchsia extensions use the rest
        // of the opcode.
        Opcode::try_from(self.opcode.get() & 0xff)
    }

    fn set_opcode(&mut self, opcode: Opcode) {
        self.opcode.set(opcode.into());
    }
}

/// The direction of a file transfer.
#[derive(Debug, Copy, Clone)]
pub enum TransferDirection {
    Read,
    Write,
}

#[derive(Debug)]
/// Implements [`InnerPacketBuilder`] to build Read and Write requests.
pub struct TransferRequestBuilder<'a> {
    direction: TransferDirection,
    filename: &'a str,
    mode: TftpMode,
    options: OptionCollection,
}

impl<'a> TransferRequestBuilder<'a> {
    /// Creates a new builder with no options.
    pub fn new(direction: TransferDirection, filename: &'a str, mode: TftpMode) -> Self {
        Self { direction, filename, mode, options: OptionCollection::default() }
    }

    /// Creates a new builder with a set of options.
    pub fn new_with_options(
        direction: TransferDirection,
        filename: &'a str,
        mode: TftpMode,
        options: impl IntoIterator<Item = Forceable<TftpOption>>,
    ) -> Self {
        Self { direction, filename, mode, options: options.into_iter().collect() }
    }

    pub fn options_mut(&mut self) -> &mut OptionCollection {
        &mut self.options
    }
}

impl<'a> InnerPacketBuilder for TransferRequestBuilder<'a> {
    fn bytes_len(&self) -> usize {
        std::mem::size_of::<MessageHead>()
            + self.filename.as_bytes().len()
            + 1
            + self.mode.as_str().as_bytes().len()
            + 1
            + self.options.serialized_len()
    }

    fn serialize(&self, mut buffer: &mut [u8]) {
        let mut bv = crate::as_buffer_view_mut(&mut buffer);
        bv.take_obj_front::<MessageHead>().unwrap().set_opcode(match self.direction {
            TransferDirection::Read => Opcode::ReadRequest,
            TransferDirection::Write => Opcode::WriteRequest,
        });
        write_str(&mut bv, self.filename.as_ref());
        write_str(&mut bv, self.mode.as_str());
        self.options.serialize(&mut bv);
    }
}

/// Implements [`PacketBuilder`] for a data request.
#[derive(Debug)]
pub struct DataPacketBuilder {
    block: u16,
}

impl DataPacketBuilder {
    /// Creates a new builder.
    pub fn new(block: u16) -> Self {
        Self { block }
    }
}

impl PacketBuilder for DataPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(
            std::mem::size_of::<MessageHead>() + std::mem::size_of::<U16>(),
            0,
            0,
            std::u16::MAX.into(),
        )
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let mut buffer = buffer.header();
        let mut bv = crate::as_buffer_view_mut(&mut buffer);
        bv.take_obj_front::<MessageHead>().unwrap().set_opcode(Opcode::Data);
        bv.take_obj_front::<U16>().unwrap().set(self.block);
    }
}

/// Implements [`InnerPacketBuilder`] for ack messages.
#[derive(Debug)]
pub struct AckPacketBuilder {
    block: u16,
}

impl AckPacketBuilder {
    /// Creates a new builder.
    pub fn new(block: u16) -> Self {
        Self { block }
    }
}

impl InnerPacketBuilder for AckPacketBuilder {
    fn bytes_len(&self) -> usize {
        std::mem::size_of::<MessageHead>() + std::mem::size_of::<U16>()
    }

    fn serialize(&self, mut buffer: &mut [u8]) {
        let mut bv = crate::as_buffer_view_mut(&mut buffer);
        bv.take_obj_front::<MessageHead>().unwrap().set_opcode(Opcode::Ack);
        bv.take_obj_front::<U16>().unwrap().set(self.block);
    }
}

/// Implements [`InnerPacketBuilder`] for error messages.
#[derive(Debug)]
pub struct ErrorPacketBuilder<'a> {
    error: TftpError,
    msg: &'a str,
}

impl<'a> ErrorPacketBuilder<'a> {
    /// Creates a new builder.
    pub fn new(error: TftpError, msg: &'a str) -> Self {
        Self { error, msg }
    }
}

impl<'a> InnerPacketBuilder for ErrorPacketBuilder<'a> {
    fn bytes_len(&self) -> usize {
        std::mem::size_of::<MessageHead>()
            + std::mem::size_of::<U16>()
            + self.msg.as_bytes().len()
            + 1
    }

    fn serialize(&self, mut buffer: &mut [u8]) {
        let mut bv = crate::as_buffer_view_mut(&mut buffer);
        bv.take_obj_front::<MessageHead>().unwrap().set_opcode(Opcode::Error);
        bv.take_obj_front::<U16>().unwrap().set(self.error.into());
        write_str(&mut bv, self.msg);
    }
}

/// Implements [`InnerPacketBuilder`] for option ack (OACK) messages.
#[derive(Debug, Default)]
pub struct OptionAckPacketBuilder {
    options: OptionCollection,
}

impl OptionAckPacketBuilder {
    /// Creates a new builder with the options in `options`.
    pub fn new_with(options: impl IntoIterator<Item = Forceable<TftpOption>>) -> Self {
        Self { options: options.into_iter().collect() }
    }

    pub fn options_mut(&mut self) -> &mut OptionCollection {
        &mut self.options
    }
}

impl InnerPacketBuilder for OptionAckPacketBuilder {
    fn bytes_len(&self) -> usize {
        std::mem::size_of::<MessageHead>() + self.options.serialized_len()
    }

    fn serialize(&self, mut buffer: &mut [u8]) {
        let mut bv = crate::as_buffer_view_mut(&mut buffer);
        bv.take_obj_front::<MessageHead>().unwrap().set_opcode(Opcode::OptionAck);
        self.options.serialize(&mut bv);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use packet::{ParseBuffer as _, Serializer as _};

    const FILENAME: &'static str = "filename";

    #[test]
    fn test_read_request() {
        let mut req =
            TransferRequestBuilder::new(TransferDirection::Read, FILENAME, TftpMode::OCTET)
                .into_serializer()
                .serialize_vec_outer()
                .unwrap_or_else(|_| panic!("failed to serialize"));
        let body = match req.parse::<TftpPacket<_>>().expect("failed to parse") {
            TftpPacket::ReadRequest(b) => b,
            p => panic!("unexpected packet {:?}", p),
        };
        assert_eq!(body.filename(), FILENAME);
        assert_eq!(body.mode(), TftpMode::OCTET);
        assert!(body.options().iter().next().is_none());
    }

    #[test]
    fn test_write_request() {
        let mut req =
            TransferRequestBuilder::new(TransferDirection::Write, FILENAME, TftpMode::OCTET)
                .into_serializer()
                .serialize_vec_outer()
                .unwrap_or_else(|_| panic!("failed to serialize"));
        let body = match req.parse::<TftpPacket<_>>().expect("failed to parse") {
            TftpPacket::WriteRequest(b) => b,
            p => panic!("unexpected packet {:?}", p),
        };
        assert_eq!(body.filename(), FILENAME);
        assert_eq!(body.mode(), TftpMode::OCTET);
        assert!(body.options().iter().next().is_none());
    }

    #[test]
    fn test_data() {
        let data: Vec<_> = std::iter::successors(Some(0u8), |v| Some(*v + 1)).take(128).collect();
        let mut ser = (&data[..])
            .into_serializer()
            .serialize_vec(DataPacketBuilder::new(123))
            .unwrap_or_else(|_| panic!("failed to serialize"));
        let body = match ser.parse::<TftpPacket<_>>().expect("failed to parse") {
            TftpPacket::Data(b) => b,
            p => panic!("unexpected packet {:?}", p),
        };
        assert_eq!(body.block(), 123);
        assert_eq!(body.payload().as_ref(), &data[..]);
    }

    #[test]
    fn test_error() {
        const ERR_STR: &str = "ERROR";
        let mut err = ErrorPacketBuilder::new(TftpError::FileNotFound, ERR_STR)
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"));
        let body = match err.parse::<TftpPacket<_>>().expect("failed to parse") {
            TftpPacket::Error(b) => b,
            p => panic!("unexpected packet {:?}", p),
        };
        assert_eq!(body.error(), TftpError::FileNotFound);
        assert_eq!(body.message(), ERR_STR);
    }

    #[test]
    fn test_option_ack() {
        let builder = OptionAckPacketBuilder::new_with([
            TftpOption::WindowSize(10).not_forced(),
            TftpOption::BlockSize(35).not_forced(),
            TftpOption::Timeout(1).forced(),
            TftpOption::TransferSize(400).forced(),
        ]);
        let mut oack = builder
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"));
        let body = match oack.parse::<TftpPacket<_>>().expect("failed to parse") {
            TftpPacket::OptionAck(b) => b,
            p => panic!("unexpected packet {:?}", p),
        };
        let AllOptions { window_size, block_size, timeout, transfer_size } =
            body.options().collect();
        assert_eq!(window_size, Some(Forceable { value: 10, forced: false }));
        assert_eq!(block_size, Some(Forceable { value: 35, forced: false }));
        assert_eq!(timeout, Some(Forceable { value: 1, forced: true }));
        assert_eq!(transfer_size, Some(Forceable { value: 400, forced: true }));
    }

    #[test]
    fn test_ack() {
        let mut ack = AckPacketBuilder::new(123)
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"));
        let body = match ack.parse::<TftpPacket<_>>().expect("failed to parse") {
            TftpPacket::Ack(b) => b,
            p => panic!("unexpected packet {:?}", p),
        };
        assert_eq!(body.block(), 123);
    }

    #[test]
    fn test_transfer_request_options() {
        let builder = TransferRequestBuilder::new_with_options(
            TransferDirection::Read,
            FILENAME,
            TftpMode::OCTET,
            [
                TftpOption::WindowSize(10).not_forced(),
                TftpOption::BlockSize(35).not_forced(),
                TftpOption::Timeout(1).forced(),
                TftpOption::TransferSize(400).forced(),
            ],
        );
        let mut req = builder
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"));
        let body = match req.parse::<TftpPacket<_>>().expect("failed to parse") {
            TftpPacket::ReadRequest(b) => b,
            p => panic!("unexpected packet {:?}", p),
        };
        let AllOptions { window_size, block_size, timeout, transfer_size } =
            body.options().collect();
        assert_eq!(window_size, Some(Forceable { value: 10, forced: false }));
        assert_eq!(block_size, Some(Forceable { value: 35, forced: false }));
        assert_eq!(timeout, Some(Forceable { value: 1, forced: true }));
        assert_eq!(transfer_size, Some(Forceable { value: 400, forced: true }));
    }
}

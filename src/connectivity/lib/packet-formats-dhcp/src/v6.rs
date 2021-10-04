// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization for DHCPv6 messages.

use {
    byteorder::NetworkEndian,
    mdns::protocol::{Domain, ParseError as MdnsParseError},
    num_derive::FromPrimitive,
    packet::{
        records::{
            ParsedRecord, RecordParseResult, Records, RecordsImpl, RecordsImplLayout,
            RecordsSerializer, RecordsSerializerImpl,
        },
        BufferView, BufferViewMut, InnerPacketBuilder, ParsablePacket, ParseMetadata,
    },
    std::{
        convert::{TryFrom, TryInto},
        mem,
        net::Ipv6Addr,
        slice::Iter,
        str,
    },
    thiserror::Error,
    uuid::Uuid,
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

type U16 = zerocopy::U16<NetworkEndian>;
type U32 = zerocopy::U32<NetworkEndian>;

/// A DHCPv6 packet parsing error.
#[allow(missing_docs)]
#[derive(Debug, Error, PartialEq)]
pub enum ParseError {
    #[error("invalid message type: {}", _0)]
    InvalidMessageType(u8),
    #[error("invalid option code: {}", _0)]
    InvalidOpCode(u16),
    #[error("invalid option length {} for option code {:?}", _1, _0)]
    InvalidOpLen(OptionCode, usize),
    #[error("buffer exhausted while more bytes are expected")]
    BufferExhausted,
    #[error("failed to parse domain {:?}", _0)]
    DomainParseError(MdnsParseError),
    #[error("failed to parse UTF8 string: {:?}", _0)]
    Utf8Error(#[from] str::Utf8Error),
}

/// A DHCPv6 message type as defined in [RFC 8415, Section 7.3].
///
/// [RFC 8415, Section 7.3]: https://tools.ietf.org/html/rfc8415#section-7.3
#[allow(missing_docs)]
#[derive(Debug, PartialEq, FromPrimitive, AsBytes, Copy, Clone)]
#[repr(u8)]
pub enum MessageType {
    Solicit = 1,
    Advertise = 2,
    Request = 3,
    Confirm = 4,
    Renew = 5,
    Rebind = 6,
    Reply = 7,
    Release = 8,
    Decline = 9,
    Reconfigure = 10,
    InformationRequest = 11,
    RelayForw = 12,
    RelayRepl = 13,
}

impl From<MessageType> for u8 {
    fn from(t: MessageType) -> u8 {
        t as u8
    }
}

impl TryFrom<u8> for MessageType {
    type Error = ParseError;

    fn try_from(b: u8) -> Result<MessageType, ParseError> {
        <Self as num_traits::FromPrimitive>::from_u8(b).ok_or(ParseError::InvalidMessageType(b))
    }
}

/// A DHCPv6 option code that identifies a corresponding option.
///
/// Options that are not found in this type are currently not supported. An exhaustive list of
/// option codes can be found [here][option-codes].
///
/// [option-codes]: https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
#[allow(missing_docs)]
#[derive(Debug, PartialEq, FromPrimitive, Clone, Copy)]
#[repr(u8)]
pub enum OptionCode {
    ClientId = 1,
    ServerId = 2,
    Iana = 3,
    IaAddr = 5,
    Oro = 6,
    Preference = 7,
    ElapsedTime = 8,
    StatusCode = 13,
    DnsServers = 23,
    DomainList = 24,
    InformationRefreshTime = 32,
    SolMaxRt = 82,
}

impl From<OptionCode> for u16 {
    fn from(code: OptionCode) -> u16 {
        code as u16
    }
}

impl TryFrom<u16> for OptionCode {
    type Error = ParseError;

    fn try_from(n: u16) -> Result<OptionCode, ParseError> {
        <Self as num_traits::FromPrimitive>::from_u16(n).ok_or(ParseError::InvalidOpCode(n))
    }
}

/// A parsed DHCPv6 options.
///
/// Options that are not found in this type are currently not supported. An exhaustive list of
/// options can be found [here][options].
///
/// [options]: https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
// TODO(https://fxbug.dev/75612): replace `ParsedDhcpOption` and `DhcpOption` with a single type.
#[allow(missing_docs)]
#[derive(Debug, PartialEq)]
pub enum ParsedDhcpOption<'a> {
    // https://tools.ietf.org/html/rfc8415#section-21.2
    ClientId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.3
    ServerId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.4
    // TODO(https://fxbug.dev/74340): add validation; not all option codes can
    // be present in an IA_NA option.
    Iana(IanaData<&'a [u8]>),
    // https://tools.ietf.org/html/rfc8415#section-21.6
    // TODO(https://fxbug.dev/74340): add validation; not all option codes can
    // be present in an IA Address option.
    IaAddr(IaAddrData<&'a [u8]>),
    // https://tools.ietf.org/html/rfc8415#section-21.7
    // TODO(https://fxbug.dev/74340): add validation; not all option codes can
    // be present in an ORO option.
    // See https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
    Oro(Vec<OptionCode>),
    // https://tools.ietf.org/html/rfc8415#section-21.8
    Preference(u8),
    // https://tools.ietf.org/html/rfc8415#section-21.9
    ElapsedTime(u16),
    // https://tools.ietf.org/html/rfc8415#section-21.13
    StatusCode(U16, &'a str),
    // https://tools.ietf.org/html/rfc8415#section-21.23
    InformationRefreshTime(u32),
    // https://tools.ietf.org/html/rfc8415#section-21.24
    SolMaxRt(U32),
    // https://tools.ietf.org/html/rfc3646#section-3
    DnsServers(Vec<Ipv6Addr>),
    // https://tools.ietf.org/html/rfc3646#section-4
    DomainList(Vec<checked::Domain>),
}

/// An overlay representation of an IA_NA option.
#[derive(Debug, PartialEq)]
pub struct IanaData<B: ByteSlice> {
    header: LayoutVerified<B, IanaHeader>,
    options: Records<B, ParsedDhcpOptionImpl>,
}

impl<'a, B: ByteSlice> IanaData<B> {
    /// Constructs a new `IanaData` from a `ByteSlice`.
    pub fn new(buf: B) -> Result<Self, ParseError> {
        let buf_len = buf.len();
        let (header, options) = LayoutVerified::new_unaligned_from_prefix(buf)
            .ok_or(ParseError::InvalidOpLen(OptionCode::Iana, buf_len))?;
        let options = Records::<B, ParsedDhcpOptionImpl>::parse_with_context(options, ())?;
        Ok(IanaData { header, options })
    }

    /// Returns the IAID.
    pub fn iaid(&self) -> u32 {
        self.header.iaid.get()
    }

    /// Returns the T1.
    pub fn t1(&self) -> u32 {
        self.header.t1.get()
    }

    /// Returns the T2.
    pub fn t2(&self) -> u32 {
        self.header.t2.get()
    }

    /// Returns an iterator over the options.
    pub fn iter_options(&'a self) -> impl 'a + Iterator<Item = ParsedDhcpOption<'a>> {
        self.options.iter()
    }
}

/// An overlay for the fixed fields of an IA_NA option.
#[derive(FromBytes, AsBytes, Unaligned, Debug, PartialEq, Copy, Clone)]
#[repr(C)]
pub struct IanaHeader {
    iaid: U32,
    t1: U32,
    t2: U32,
}

const IANA_HEADER_LEN: usize = 12;

/// An overlay representation of an IA Address option.
#[derive(Debug, PartialEq)]
pub struct IaAddrData<B: ByteSlice> {
    header: LayoutVerified<B, IaAddrHeader>,
    options: Records<B, ParsedDhcpOptionImpl>,
}

impl<'a, B: ByteSlice> IaAddrData<B> {
    /// Constructs a new `IaAddrData` from a `ByteSlice`.
    pub fn new(buf: B) -> Result<Self, ParseError> {
        let buf_len = buf.len();
        let (header, options) = LayoutVerified::new_unaligned_from_prefix(buf)
            .ok_or(ParseError::InvalidOpLen(OptionCode::IaAddr, buf_len))?;
        let options = Records::<B, ParsedDhcpOptionImpl>::parse_with_context(options, ())?;
        Ok(IaAddrData { header, options })
    }

    /// Returns the address.
    pub fn addr(&self) -> Ipv6Addr {
        Ipv6Addr::from(self.header.addr.ipv6_bytes())
    }

    /// Returns the preferred lifetime.
    pub fn preferred_lifetime(&self) -> u32 {
        self.header.preferred_lifetime.get()
    }

    /// Returns the valid lifetime.
    pub fn valid_lifetime(&self) -> u32 {
        self.header.valid_lifetime.get()
    }

    /// Returns an iterator over the options.
    pub fn iter_options(&'a self) -> impl 'a + Iterator<Item = ParsedDhcpOption<'a>> {
        self.options.iter()
    }
}

/// An overlay for the fixed fields of an IA Address option.
#[derive(FromBytes, AsBytes, Unaligned, Debug, PartialEq, Copy, Clone)]
#[repr(C)]
pub struct IaAddrHeader {
    addr: net_types::ip::Ipv6Addr,
    preferred_lifetime: U32,
    valid_lifetime: U32,
}

const IAADDR_HEADER_LEN: usize = 24;

mod checked {
    use std::convert::TryFrom;
    use std::str::FromStr;

    use mdns::protocol::{DomainBuilder, EmbeddedPacketBuilder};
    use packet::BufferViewMut;
    use zerocopy::ByteSliceMut;

    use super::ParseError;

    /// A checked domain that can only be created through the provided constructor.
    #[derive(Debug, PartialEq)]
    pub struct Domain {
        domain: String,
        builder: DomainBuilder,
    }

    impl FromStr for Domain {
        type Err = ParseError;

        /// Constructs a `Domain` from a string.
        ///
        /// See `<Domain as TryFrom<String>>::try_from`.
        fn from_str(s: &str) -> Result<Self, ParseError> {
            Self::try_from(s.to_string())
        }
    }

    impl TryFrom<String> for Domain {
        type Error = ParseError;

        /// Constructs a `Domain` from a string.
        ///
        /// # Errors
        ///
        /// If the string is not a valid domain following the definition in [RFC 1035].
        ///
        /// [RFC 1035]: https://tools.ietf.org/html/rfc1035
        fn try_from(domain: String) -> Result<Self, ParseError> {
            let builder = DomainBuilder::from_str(&domain).map_err(ParseError::DomainParseError)?;
            Ok(Domain { domain, builder })
        }
    }

    impl Domain {
        pub(crate) fn bytes_len(&self) -> usize {
            self.builder.bytes_len()
        }

        pub(crate) fn serialize<B: ByteSliceMut, BV: BufferViewMut<B>>(&self, bv: &mut BV) {
            let () = self.builder.serialize(bv);
        }
    }
}

macro_rules! option_to_code {
    ($option:ident, $($option_name:ident::$variant:tt($($v:tt)*)),*) => {
        match $option {
            $($option_name::$variant($($v)*)=>OptionCode::$variant,)*
        }
    }
}

impl ParsedDhcpOption<'_> {
    /// Returns the corresponding option code for the calling option.
    pub fn code(&self) -> OptionCode {
        option_to_code!(
            self,
            ParsedDhcpOption::ClientId(_),
            ParsedDhcpOption::ServerId(_),
            ParsedDhcpOption::Iana(_),
            ParsedDhcpOption::IaAddr(_),
            ParsedDhcpOption::Oro(_),
            ParsedDhcpOption::Preference(_),
            ParsedDhcpOption::ElapsedTime(_),
            ParsedDhcpOption::StatusCode(_, _),
            ParsedDhcpOption::InformationRefreshTime(_),
            ParsedDhcpOption::SolMaxRt(_),
            ParsedDhcpOption::DnsServers(_),
            ParsedDhcpOption::DomainList(_)
        )
    }
}

/// An ID that uniquely identifies a DHCPv6 client or server, defined in [RFC8415, Section 11].
///
/// [RFC8415, Section 11]: https://tools.ietf.org/html/rfc8415#section-11
type Duid = [u8];

/// An implementation of `RecordsImpl` for `ParsedDhcpOption`.
///
/// Options in DHCPv6 messages are sequential, so they are parsed through the
/// APIs provided in [packet::records].
///
/// [packet::records]: https://fuchsia-docs.firebaseapp.com/rust/packet/records/index.html
#[derive(Debug, PartialEq)]
enum ParsedDhcpOptionImpl {}

impl RecordsImplLayout for ParsedDhcpOptionImpl {
    type Context = ();

    type Error = ParseError;
}

impl<'a> RecordsImpl<'a> for ParsedDhcpOptionImpl {
    type Record = ParsedDhcpOption<'a>;

    /// Tries to parse an option from the beginning of the input buffer. Returns the parsed
    /// `ParsedDhcpOption` and the remaining buffer. If the buffer is malformed, returns a
    /// `ParseError`. Option format as defined in [RFC 8415, Section 21.1]:
    ///
    /// [RFC 8415, Section 21.1]: https://tools.ietf.org/html/rfc8415#section-21.1
    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        _context: &mut Self::Context,
    ) -> RecordParseResult<Self::Record, Self::Error> {
        if data.len() == 0 {
            return Ok(ParsedRecord::Done);
        }

        let opt_code = data.take_obj_front::<U16>().ok_or(ParseError::BufferExhausted)?;
        let opt_len = data.take_obj_front::<U16>().ok_or(ParseError::BufferExhausted)?;
        let opt_len = usize::from(opt_len.get());
        let mut opt_val = data.take_front(opt_len).ok_or(ParseError::BufferExhausted)?;

        let opt_code = match OptionCode::try_from(opt_code.get()) {
            Ok(opt_code) => opt_code,
            // TODO(https://fxbug.dev/57177): surface skipped codes so we know which ones are not
            // supported.
            Err(ParseError::InvalidOpCode(_)) => {
                // Skip unknown option codes to keep useful information.
                //
                // https://tools.ietf.org/html/rfc8415#section-16
                return Ok(ParsedRecord::Skipped);
            }
            Err(e) => unreachable!("unexpected error from op code conversion: {}", e),
        };

        let opt = match opt_code {
            OptionCode::ClientId => Ok(ParsedDhcpOption::ClientId(opt_val)),
            OptionCode::ServerId => Ok(ParsedDhcpOption::ServerId(opt_val)),
            OptionCode::Iana => IanaData::new(opt_val).map(ParsedDhcpOption::Iana),
            OptionCode::IaAddr => IaAddrData::new(opt_val).map(ParsedDhcpOption::IaAddr),
            OptionCode::Oro => {
                let options = opt_val
                    // TODO(https://github.com/rust-lang/rust/issues/74985): use slice::as_chunks.
                    .chunks(2)
                    .map(|opt| {
                        let opt: [u8; 2] = opt.try_into().map_err(
                            |std::array::TryFromSliceError { .. }| {
                                ParseError::InvalidOpLen(OptionCode::Oro, opt_val.len())
                            },
                        )?;
                        OptionCode::try_from(u16::from_be_bytes(opt))
                    })
                    .collect::<Result<_, ParseError>>()?;
                Ok(ParsedDhcpOption::Oro(options))
            }
            OptionCode::Preference => match opt_val {
                &[b] => Ok(ParsedDhcpOption::Preference(b)),
                opt_val => Err(ParseError::InvalidOpLen(OptionCode::Preference, opt_val.len())),
            },
            OptionCode::ElapsedTime => match opt_val {
                &[b0, b1] => Ok(ParsedDhcpOption::ElapsedTime(u16::from_be_bytes([b0, b1]))),
                opt_val => Err(ParseError::InvalidOpLen(OptionCode::ElapsedTime, opt_val.len())),
            },
            OptionCode::StatusCode => {
                let mut opt_val = &mut opt_val;
                let code = (&mut opt_val)
                    .take_obj_front::<U16>()
                    .ok_or(ParseError::InvalidOpLen(OptionCode::StatusCode, opt_val.len()))?;
                let message = str::from_utf8(opt_val)?;
                Ok(ParsedDhcpOption::StatusCode(*code, message))
            }
            OptionCode::InformationRefreshTime => match opt_val {
                &[b0, b1, b2, b3] => {
                    Ok(ParsedDhcpOption::InformationRefreshTime(u32::from_be_bytes([
                        b0, b1, b2, b3,
                    ])))
                }
                opt_val => {
                    Err(ParseError::InvalidOpLen(OptionCode::InformationRefreshTime, opt_val.len()))
                }
            },
            OptionCode::SolMaxRt => {
                let mut opt_val = &mut opt_val;
                let sol_max_rt = (&mut opt_val)
                    .take_obj_front::<U32>()
                    .ok_or(ParseError::InvalidOpLen(OptionCode::SolMaxRt, opt_val.len()))?;
                Ok(ParsedDhcpOption::SolMaxRt(*sol_max_rt))
            }
            OptionCode::DnsServers => {
                let addresses = opt_val
                    // TODO(https://github.com/rust-lang/rust/issues/74985): use slice::as_chunks.
                    .chunks(16)
                    .map(|opt| {
                        let opt: [u8; 16] = opt.try_into().map_err(
                            |std::array::TryFromSliceError { .. }| {
                                ParseError::InvalidOpLen(OptionCode::DnsServers, opt_val.len())
                            },
                        )?;
                        Ok(Ipv6Addr::from(opt))
                    })
                    .collect::<Result<_, ParseError>>()?;
                Ok(ParsedDhcpOption::DnsServers(addresses))
            }
            OptionCode::DomainList => {
                let mut opt_val = &mut opt_val;
                let mut domains = Vec::new();
                while opt_val.len() > 0 {
                    domains.push(checked::Domain::try_from(
                        Domain::parse(&mut opt_val, None)
                            .map_err(ParseError::DomainParseError)?
                            .to_string(),
                    )?);
                }
                Ok(ParsedDhcpOption::DomainList(domains))
            }
        }?;

        Ok(ParsedRecord::Parsed(opt))
    }
}

/// Generates a random DUID UUID based on the format defined in [RFC 8415, Section 11.5].
///
/// [RFC 8415, Section 11.5]: https://tools.ietf.org/html/rfc8415#section-11.5
pub fn duid_uuid() -> [u8; 18] {
    let mut duid = [0u8; 18];
    duid[1] = 4;
    let uuid = Uuid::new_v4();
    let uuid = uuid.as_bytes();
    for i in 0..16 {
        duid[2 + i] = uuid[i];
    }
    duid
}

/// A serializable DHCPv6 option.
///
/// Options that are not found in this type are currently not supported. An exhaustive list of
/// options can be found [here][options].
///
/// [options]: https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
// TODO(https://fxbug.dev/75612): replace `DhcpOption` and `ParsedDhcpOption` with a single type.
#[allow(missing_docs)]
#[derive(Debug)]
pub enum DhcpOption<'a> {
    // https://tools.ietf.org/html/rfc8415#section-21.2
    ClientId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.3
    ServerId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.4
    // TODO(https://fxbug.dev/74340): add validation; not all option codes can
    // be present in an IA_NA option.
    Iana(IanaSerializer<'a>),
    // https://tools.ietf.org/html/rfc8415#section-21.6
    // TODO(https://fxbug.dev/74340): add validation; not all option codes can
    // be present in an IA Address option.
    IaAddr(IaAddrSerializer<'a>),
    // https://tools.ietf.org/html/rfc8415#section-21.7
    // TODO(https://fxbug.dev/74340): add validation; not all option codes can
    // be present in an ORO option.
    // See https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
    Oro(&'a [OptionCode]),
    // https://tools.ietf.org/html/rfc8415#section-21.8
    Preference(u8),
    // https://tools.ietf.org/html/rfc8415#section-21.9
    ElapsedTime(u16),
    // https://tools.ietf.org/html/rfc8415#section-21.13
    StatusCode(u16, &'a str),
    // https://tools.ietf.org/html/rfc8415#section-21.23
    InformationRefreshTime(u32),
    // https://tools.ietf.org/html/rfc8415#section-21.24
    SolMaxRt(u32),
    // https://tools.ietf.org/html/rfc3646#section-3
    DnsServers(&'a [Ipv6Addr]),
    // https://tools.ietf.org/html/rfc3646#section-4
    DomainList(&'a [checked::Domain]),
}

/// A serializer for the IA_NA DHCPv6 option.
#[derive(Debug)]
pub struct IanaSerializer<'a> {
    iaid: u32,
    t1: u32,
    t2: u32,
    options: RecordsSerializer<'a, DhcpOptionImpl, DhcpOption<'a>, Iter<'a, DhcpOption<'a>>>,
}

impl<'a> IanaSerializer<'a> {
    /// Constructs a new `IanaSerializer`.
    pub fn new(iaid: u32, t1: u32, t2: u32, options: &'a [DhcpOption<'a>]) -> IanaSerializer<'a> {
        IanaSerializer { iaid, t1, t2, options: RecordsSerializer::new(options.iter()) }
    }
}

/// A serializer for the IA Address DHCPv6 option.
#[derive(Debug)]
pub struct IaAddrSerializer<'a> {
    addr: Ipv6Addr,
    preferred_lifetime: u32,
    valid_lifetime: u32,
    options: RecordsSerializer<'a, DhcpOptionImpl, DhcpOption<'a>, Iter<'a, DhcpOption<'a>>>,
}

impl<'a> IaAddrSerializer<'a> {
    /// Constructs a new `IaAddrSerializer`.
    pub fn new(
        addr: Ipv6Addr,
        preferred_lifetime: u32,
        valid_lifetime: u32,
        options: &'a [DhcpOption<'a>],
    ) -> IaAddrSerializer<'a> {
        IaAddrSerializer {
            addr,
            preferred_lifetime,
            valid_lifetime,
            options: RecordsSerializer::new(options.iter()),
        }
    }
}

impl DhcpOption<'_> {
    /// Returns the corresponding option code for the calling option.
    pub fn code(&self) -> OptionCode {
        option_to_code!(
            self,
            DhcpOption::ClientId(_),
            DhcpOption::ServerId(_),
            DhcpOption::Iana(_),
            DhcpOption::IaAddr(_),
            DhcpOption::Oro(_),
            DhcpOption::Preference(_),
            DhcpOption::ElapsedTime(_),
            DhcpOption::StatusCode(_, _),
            DhcpOption::InformationRefreshTime(_),
            DhcpOption::SolMaxRt(_),
            DhcpOption::DnsServers(_),
            DhcpOption::DomainList(_)
        )
    }
}

/// An implementation of `RecordsSerializerImpl` for `DhcpOption`.
///
/// Options in DHCPv6 messages are sequential, so they are serialized through
/// the APIs provided in [packet::records].
///
/// [packet::records]: https://fuchsia-docs.firebaseapp.com/rust/packet/records/index.html
#[derive(Debug)]
enum DhcpOptionImpl {}

impl<'a> RecordsSerializerImpl<'a> for DhcpOptionImpl {
    type Record = DhcpOption<'a>;

    /// Calculates the serialized length of the option based on option format defined in
    /// [RFC 8415, Section 21.1].
    ///
    /// For variable length options that exceeds the size limit (`u16::MAX`), fallback to a
    /// default value.
    ///
    /// [RFC 8415, Section 21.1]: https://tools.ietf.org/html/rfc8415#section-21.1
    fn record_length(opt: &Self::Record) -> usize {
        4 + match opt {
            DhcpOption::ClientId(duid) | DhcpOption::ServerId(duid) => {
                u16::try_from(duid.len()).unwrap_or(18).into()
            }
            DhcpOption::Iana(iana_data) => {
                u16::try_from(IANA_HEADER_LEN + iana_data.options.records_bytes_len())
                    .expect("overflows")
                    .into()
            }
            DhcpOption::IaAddr(iaaddr_data) => {
                u16::try_from(IAADDR_HEADER_LEN + iaaddr_data.options.records_bytes_len())
                    .expect("overflows")
                    .into()
            }
            DhcpOption::Oro(opts) => u16::try_from(2 * opts.len()).unwrap_or(0).into(),
            DhcpOption::Preference(v) => std::mem::size_of_val(v),
            DhcpOption::ElapsedTime(v) => std::mem::size_of_val(v),
            DhcpOption::StatusCode(v, message) => std::mem::size_of_val(v) + message.len(),
            DhcpOption::InformationRefreshTime(v) => std::mem::size_of_val(v),
            DhcpOption::SolMaxRt(v) => std::mem::size_of_val(v),
            DhcpOption::DnsServers(recursive_name_servers) => {
                u16::try_from(16 * recursive_name_servers.len()).unwrap_or(0).into()
            }
            DhcpOption::DomainList(domains) => {
                u16::try_from(domains.iter().fold(0, |tot, domain| tot + domain.bytes_len()))
                    .unwrap_or(0)
                    .into()
            }
        }
    }

    /// Serializes an option and appends to input buffer based on option format defined in
    /// [RFC 8415, Section 21.1].
    ///
    /// For variable length options that exceeds the size limit (`u16::MAX`), fallback to
    /// use a default value instead, so it is impossible for future changes to introduce
    /// DoS vulnerabilities even if they accidentally allow such options to be injected.
    ///
    /// # Panics
    ///
    /// If buffer is too small. This means `record_length` is not correctly implemented.
    ///
    /// [RFC 8415, Section 21.1]: https://tools.ietf.org/html/rfc8415#section-21.1
    fn serialize(mut buf: &mut [u8], opt: &Self::Record) {
        // Implements BufferViewMut, giving us write_obj_front.
        let mut buf = &mut buf;
        let () = buf.write_obj_front(&U16::new(opt.code().into())).expect("buffer is too small");

        match opt {
            DhcpOption::ClientId(duid) | DhcpOption::ServerId(duid) => {
                match u16::try_from(duid.len()) {
                    Ok(len) => {
                        let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                        let () = buf.write_obj_front(*duid).expect("buffer is too small");
                    }
                    Err(std::num::TryFromIntError { .. }) => {
                        // Do not panic, so DUIDs with length exceeding u16 won't introduce DoS
                        // vulnerability.
                        let duid = duid_uuid();
                        let len = u16::try_from(duid.len()).expect("uuid length is too long");
                        let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                        let () = buf.write_obj_front(&duid).expect("buffer is too small");
                    }
                }
            }
            DhcpOption::Iana(IanaSerializer { iaid, t1, t2, options }) => {
                let len = u16::try_from(IANA_HEADER_LEN + options.records_bytes_len())
                    .expect("overflows");
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                let () = buf.write_obj_front(&U32::new(*iaid)).expect("buffer is too small");
                let () = buf.write_obj_front(&U32::new(*t1)).expect("buffer is too small");
                let () = buf.write_obj_front(&U32::new(*t2)).expect("buffer is too small");
                let () = options.serialize_records(buf);
            }
            DhcpOption::IaAddr(IaAddrSerializer {
                addr,
                preferred_lifetime,
                valid_lifetime,
                options,
            }) => {
                let len = u16::try_from(IAADDR_HEADER_LEN + options.records_bytes_len())
                    .expect("overflows");
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                let () = buf.write_obj_front(&addr.octets()).expect("buffer is too small");
                let () = buf
                    .write_obj_front(&U32::new(*preferred_lifetime))
                    .expect("buffer is too small");
                let () =
                    buf.write_obj_front(&U32::new(*valid_lifetime)).expect("buffer is too small");
                let () = options.serialize_records(buf);
            }
            DhcpOption::Oro(requested_opts) => {
                let (requested_opts, len) = u16::try_from(2 * requested_opts.len()).map_or_else(
                    |std::num::TryFromIntError { .. }| {
                        // Do not panic, so OROs with size exceeding u16 won't introduce DoS
                        // vulnerability.
                        (&[][..], 0)
                    },
                    |len| (*requested_opts, len),
                );
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                for opt_code in requested_opts.into_iter() {
                    let () = buf
                        .write_obj_front(&u16::from(*opt_code).to_be_bytes())
                        .expect("buffer is too small");
                }
            }
            DhcpOption::Preference(pref_val) => {
                let () = buf.write_obj_front(&U16::new(1)).expect("buffer is too small");
                let () = buf.write_obj_front(pref_val).expect("buffer is too small");
            }
            DhcpOption::ElapsedTime(elapsed_time) => {
                let () = buf
                    .write_obj_front(&U16::new(
                        mem::size_of_val(elapsed_time).try_into().expect("overflows"),
                    ))
                    .expect("buffer is too small");
                let () =
                    buf.write_obj_front(&U16::new(*elapsed_time)).expect("buffer is too small");
            }
            DhcpOption::StatusCode(code, message) => {
                let opt_len = u16::try_from(2 + message.len()).expect("overflows");
                let () = buf.write_obj_front(&U16::new(opt_len)).expect("buffer is too small");
                let () = buf.write_obj_front(&U16::new(*code)).expect("buffer is too small");
                let () = buf.write_obj_front(message.as_bytes()).expect("buffer is too small");
            }
            DhcpOption::InformationRefreshTime(information_refresh_time) => {
                let () = buf
                    .write_obj_front(&U16::new(
                        mem::size_of_val(information_refresh_time).try_into().expect("overflows"),
                    ))
                    .expect("buffer is too small");
                let () = buf
                    .write_obj_front(&U32::new(*information_refresh_time))
                    .expect("buffer is too small");
            }
            DhcpOption::SolMaxRt(sol_max_rt) => {
                let () = buf
                    .write_obj_front(&U16::new(
                        mem::size_of_val(sol_max_rt).try_into().expect("overflows"),
                    ))
                    .expect("buffer is too small");
                let () = buf.write_obj_front(&U32::new(*sol_max_rt)).expect("buffer is too small");
            }
            DhcpOption::DnsServers(recursive_name_servers) => {
                let (recursive_name_servers, len) =
                    u16::try_from(16 * recursive_name_servers.len()).map_or_else(
                        |std::num::TryFromIntError { .. }| {
                            // Do not panic, so DnsServers with size exceeding `u16` won't introduce
                            // DoS vulnerability.
                            (&[][..], 0)
                        },
                        |len| (*recursive_name_servers, len),
                    );
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                recursive_name_servers.iter().for_each(|server_addr| {
                    let () =
                        buf.write_obj_front(&server_addr.octets()).expect("buffer is too small");
                })
            }
            DhcpOption::DomainList(domains) => {
                let (domains, len) =
                    u16::try_from(domains.iter().map(|domain| domain.bytes_len()).sum::<usize>())
                        .map_or_else(
                            |std::num::TryFromIntError { .. }| {
                                // Do not panic, so DomainList with size exceeding `u16` won't
                                // introduce DoS vulnerability.
                                (&[][..], 0)
                            },
                            |len| (*domains, len),
                        );
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                domains.iter().for_each(|domain| {
                    domain.serialize(&mut buf);
                })
            }
        }
    }
}

/// A transaction ID defined in [RFC 8415, Section 8].
///
/// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
type TransactionId = [u8; 3];

/// A DHCPv6 message as defined in [RFC 8415, Section 8].
///
/// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
#[derive(Debug)]
pub struct Message<'a, B> {
    msg_type: MessageType,
    transaction_id: &'a TransactionId,
    options: Records<B, ParsedDhcpOptionImpl>,
}

impl<'a, B: ByteSlice> Message<'a, B> {
    /// Returns the message type.
    pub fn msg_type(&self) -> MessageType {
        self.msg_type
    }

    /// Returns the transaction ID.
    pub fn transaction_id(&self) -> &TransactionId {
        &self.transaction_id
    }

    /// Returns an iterator over the options.
    pub fn options<'b: 'a>(&'b self) -> impl 'b + Iterator<Item = ParsedDhcpOption<'a>> {
        self.options.iter()
    }
}

impl<'a, B: 'a + ByteSlice> ParsablePacket<B, ()> for Message<'a, B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let Self { msg_type, transaction_id, options } = self;
        ParseMetadata::from_packet(
            0,
            mem::size_of_val(msg_type) + mem::size_of_val(transaction_id) + options.bytes().len(),
            0,
        )
    }

    fn parse<BV: BufferView<B>>(mut buf: BV, _args: ()) -> Result<Self, ParseError> {
        let msg_type =
            MessageType::try_from(buf.take_byte_front().ok_or(ParseError::BufferExhausted)?)?;
        let transaction_id =
            buf.take_obj_front::<TransactionId>().ok_or(ParseError::BufferExhausted)?.into_ref();
        let options = Records::<_, ParsedDhcpOptionImpl>::parse(buf.take_rest_front())?;
        Ok(Message { msg_type, transaction_id, options })
    }
}

/// A `DHCPv6Message` builder.
///
/// DHCPv6 messages are serialized through [packet::serialize::InnerPacketBuilder] because it won't
/// encapsulate any other packets.
///
/// [packet::serialize::InnerPacketBuilder]: https://fuchsia-docs.firebaseapp.com/rust/packet/serialize/trait.InnerPacketBuilder.html
pub struct MessageBuilder<'a> {
    msg_type: MessageType,
    transaction_id: TransactionId,
    options: RecordsSerializer<'a, DhcpOptionImpl, DhcpOption<'a>, Iter<'a, DhcpOption<'a>>>,
}

impl<'a> MessageBuilder<'a> {
    /// Constructs a new `MessageBuilder`.
    pub fn new(
        msg_type: MessageType,
        transaction_id: TransactionId,
        options: &'a [DhcpOption<'a>],
    ) -> MessageBuilder<'a> {
        MessageBuilder { msg_type, transaction_id, options: RecordsSerializer::new(options.iter()) }
    }
}

impl InnerPacketBuilder for MessageBuilder<'_> {
    /// Calculates the serialized length of the DHCPv6 message based on format defined in
    /// [RFC 8415, Section 8].
    ///
    /// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
    fn bytes_len(&self) -> usize {
        let Self { msg_type, transaction_id, options } = self;
        mem::size_of_val(msg_type) + mem::size_of_val(transaction_id) + options.records_bytes_len()
    }

    /// Serializes DHCPv6 message based on format defined in [RFC 8415, Section 8].
    ///
    /// # Panics
    ///
    /// If buffer is too small. This means `record_length` is not correctly implemented.
    ///
    /// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
    fn serialize(&self, mut buffer: &mut [u8]) {
        let Self { msg_type, transaction_id, options } = self;
        // Implements BufferViewMut, giving us write_obj_front.
        let mut buffer = &mut buffer;
        let () = buffer.write_obj_front(msg_type).expect("buffer is too small");
        let () = buffer.write_obj_front(transaction_id).expect("buffer is too small");
        let () = options.serialize_records(buffer);
    }
}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches, std::str::FromStr};

    fn test_buf_with_no_options() -> Vec<u8> {
        let builder = MessageBuilder::new(MessageType::Solicit, [1, 2, 3], &[]);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
        buf
    }

    #[test]
    fn test_message_serialization() {
        let iaaddr_options = [DhcpOption::StatusCode(0, "Success.")];
        let iana_options = [
            DhcpOption::Preference(42),
            DhcpOption::IaAddr(IaAddrSerializer::new(
                Ipv6Addr::from([0, 1, 2, 3, 4, 5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215]),
                3600,
                7200,
                &iaaddr_options,
            )),
        ];
        let dns_servers = [
            Ipv6Addr::from([0, 1, 2, 3, 4, 5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215]),
            Ipv6Addr::from([10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]),
        ];
        let domains = [
            checked::Domain::from_str("fuchsia.dev").expect("failed to construct test domain"),
            checked::Domain::from_str("www.google.com").expect("failed to construct test domain"),
        ];
        let options = [
            DhcpOption::ClientId(&[4, 5, 6]),
            DhcpOption::ServerId(&[8]),
            DhcpOption::Iana(IanaSerializer::new(42, 3000, 6500, &iana_options)),
            DhcpOption::Oro(&[OptionCode::ClientId, OptionCode::ServerId]),
            DhcpOption::Preference(42),
            DhcpOption::ElapsedTime(3600),
            DhcpOption::StatusCode(0, "Success."),
            DhcpOption::InformationRefreshTime(86400),
            DhcpOption::SolMaxRt(86400),
            DhcpOption::DnsServers(&dns_servers),
            DhcpOption::DomainList(&domains),
        ];
        let builder = MessageBuilder::new(MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];

        let () = builder.serialize(&mut buf);

        assert_eq!(buf.len(), 197);
        #[rustfmt::skip]
        assert_eq!(
            buf[..],
            [
                1, // message type
                1, 2, 3, // transaction id
                0, 1, 0, 3, 4, 5, 6, // option - client ID
                0, 2, 0, 1, 8, // option - server ID
                // option - IA_NA
                0, 3, 0, 59, 0, 0, 0, 42, 0, 0, 11, 184, 0, 0, 25, 100, 0, 7, 0, 1, 42, 0, 5, 0, 38, 0, 1, 2, 3, 4, 5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215, 0, 0, 14, 16, 0, 0, 28, 32, 0, 13, 0, 10, 0, 0, 83, 117, 99, 99, 101, 115, 115, 46,
                0, 6, 0, 4, 0, 1, 0, 2, // option - ORO
                0, 7, 0, 1, 42, // option - preference
                0, 8, 0, 2, 14, 16, // option - elapsed time
                // option - status code
                0, 13, 0, 10, 0, 0, 83, 117, 99, 99, 101, 115, 115, 46,
                0, 32, 0, 4, 0, 1, 81, 128, // option - information refresh time
                0, 82, 0, 4, 0, 1, 81, 128, // option - SOL_MAX_RT
                // option - Dns servers
                0, 23, 0, 32,
                0, 1, 2, 3, 4, 5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215,
                10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                // option - Dns domains
                0, 24, 0, 29,
                7, 102, 117, 99, 104, 115, 105, 97, 3, 100, 101, 118, 0,
                3, 119, 119, 119, 6, 103, 111, 111, 103, 108, 101, 3, 99, 111, 109, 0
            ],
        );
    }

    #[test]
    fn test_message_serialization_parsing_roundtrip() {
        let iaaddr_suboptions = [DhcpOption::StatusCode(0, "Success.")];
        let iana_suboptions = [
            DhcpOption::Preference(42),
            DhcpOption::IaAddr(IaAddrSerializer::new(
                Ipv6Addr::from([0, 1, 2, 3, 4, 5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215]),
                7200,
                9000,
                &iaaddr_suboptions,
            )),
        ];
        let dns_servers = [Ipv6Addr::from(0)];
        let domains = [
            checked::Domain::from_str("fuchsia.dev").expect("failed to construct test domain"),
            checked::Domain::from_str("www.google.com").expect("failed to construct test domain"),
        ];
        let options = [
            DhcpOption::ClientId(&[4, 5, 6]),
            DhcpOption::ServerId(&[8]),
            DhcpOption::Iana(IanaSerializer::new(1234, 7000, 8800, &iana_suboptions)),
            DhcpOption::Oro(&[OptionCode::ClientId, OptionCode::ServerId]),
            DhcpOption::Preference(42),
            DhcpOption::ElapsedTime(3600),
            DhcpOption::StatusCode(0, "Success."),
            DhcpOption::InformationRefreshTime(86400),
            DhcpOption::SolMaxRt(86400),
            DhcpOption::DnsServers(&dns_servers),
            DhcpOption::DomainList(&domains),
        ];
        let builder = MessageBuilder::new(MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);

        let mut buf = &buf[..];
        let msg = Message::parse(&mut buf, ()).expect("parse should succeed");
        assert_eq!(msg.msg_type, MessageType::Solicit);
        assert_eq!(msg.transaction_id, &[1, 2, 3]);
        let got_options: Vec<_> = msg.options.iter().collect();

        let iana_buf = [
            0, 0, 4, 210, 0, 0, 27, 88, 0, 0, 34, 96, 0, 7, 0, 1, 42, 0, 5, 0, 38, 0, 1, 2, 3, 4,
            5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215, 0, 0, 28, 32, 0, 0, 35, 40, 0, 13,
            0, 10, 0, 0, 83, 117, 99, 99, 101, 115, 115, 46,
        ];
        let options = [
            ParsedDhcpOption::ClientId(&[4, 5, 6]),
            ParsedDhcpOption::ServerId(&[8]),
            ParsedDhcpOption::Iana(IanaData::new(&iana_buf[..]).expect("construction failed")),
            ParsedDhcpOption::Oro(vec![OptionCode::ClientId, OptionCode::ServerId]),
            ParsedDhcpOption::Preference(42),
            ParsedDhcpOption::ElapsedTime(3600),
            ParsedDhcpOption::StatusCode(U16::new(0), "Success."),
            ParsedDhcpOption::InformationRefreshTime(86400),
            ParsedDhcpOption::SolMaxRt(U32::new(86400)),
            ParsedDhcpOption::DnsServers(vec![Ipv6Addr::from([0; 16])]),
            ParsedDhcpOption::DomainList(vec![
                checked::Domain::from_str("fuchsia.dev").expect("failed to construct test domain"),
                checked::Domain::from_str("www.google.com")
                    .expect("failed to construct test domain"),
            ]),
        ];
        assert_eq!(got_options, options);
    }

    // We're forced into an `as` cast because From::from is not a const fn.
    const OVERFLOW_LENGTH: usize = u16::MAX as usize + 1;

    #[test]
    fn test_message_serialization_duid_too_long() {
        let options = [DhcpOption::ClientId(&[0u8; OVERFLOW_LENGTH])];
        let builder = MessageBuilder::new(MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);

        assert_eq!(buf.len(), 26);
        assert_eq!(
            buf[..8],
            [
                1, // message type
                1, 2, 3, // transaction id
                0, 1, 0, 18, // option - client ID (code and len only)
            ],
        );

        // Make sure the buffer is still parsable.
        let mut buf = &buf[..];
        let _: Message<'_, _> = Message::parse(&mut buf, ()).expect("parse should succeed");
    }

    #[test]
    fn test_message_serialization_oro_too_long() {
        let options = [DhcpOption::Oro(&[OptionCode::Preference; OVERFLOW_LENGTH][..])];
        let builder = MessageBuilder::new(MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);

        assert_eq!(
            buf[..],
            [
                1, // message type
                1, 2, 3, // transaction id
                0, 6, 0, 0, // option - ORO
            ],
        );

        // Make sure the buffer is still parsable.
        let mut buf = &buf[..];
        let _: Message<'_, _> = Message::parse(&mut buf, ()).expect("parse should succeed");
    }

    #[test]
    fn test_option_serialization_parsing_roundtrip() {
        let mut buf = [0u8; 6];
        let option = DhcpOption::ElapsedTime(42);

        let () = <DhcpOptionImpl as RecordsSerializerImpl>::serialize(&mut buf, &option);
        assert_eq!(buf, [0, 8, 0, 2, 0, 42]);

        let options = Records::<_, ParsedDhcpOptionImpl>::parse_with_context(&buf[..], ())
            .expect("parse should succeed");
        let options: Vec<ParsedDhcpOption<'_>> = options.iter().collect();
        assert_eq!(options[..], [ParsedDhcpOption::ElapsedTime(42)]);
    }

    #[test]
    fn test_buffer_too_short() {
        let buf = [];
        assert_matches!(Message::parse(&mut &buf[..], ()), Err(ParseError::BufferExhausted));

        let buf = [
            1, // valid message type
            0, // transaction id is too short
        ];
        assert_matches!(Message::parse(&mut &buf[..], ()), Err(ParseError::BufferExhausted));

        let buf = [
            1, // valid message type
            1, 2, 3, // valid transaction id
            0, // option code is too short
        ];
        assert_matches!(Message::parse(&mut &buf[..], ()), Err(ParseError::BufferExhausted));

        let buf = [
            1, // valid message type
            1, 2, 3, // valida transaction id
            0, 1, // valid option code
            0, // option length is too short
        ];
        assert_matches!(Message::parse(&mut &buf[..], ()), Err(ParseError::BufferExhausted));

        // option value too short
        let buf = [
            1, // valid message type
            1, 2, 3, // valid transaction id
            0, 1, // valid option code
            0, 100, // valid option length
            1, 2, // option value is too short
        ];
        assert_matches!(Message::parse(&mut &buf[..], ()), Err(ParseError::BufferExhausted));
    }

    #[test]
    fn test_invalid_message_type() {
        let mut buf = test_buf_with_no_options();
        // 0 is an invalid message type.
        buf[0] = 0;
        assert_matches!(Message::parse(&mut &buf[..], ()), Err(ParseError::InvalidMessageType(0)));
    }

    #[test]
    fn test_skip_invalid_op_code() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 0, // opt code = 0, invalid op code
            0, 1, // valid opt length
            0, // valid opt value
            0, 1, 0, 3, 4, 5, 6, // option - client ID
        ]);
        let mut buf = &buf[..];
        let msg = Message::parse(&mut buf, ()).expect("parse should succeed");
        let got_options: Vec<_> = msg.options.iter().collect();
        assert_eq!(got_options, [ParsedDhcpOption::ClientId(&[4, 5, 6])]);
    }

    #[test]
    fn test_iana_no_suboptions_serialization_parsing_roundtrip() {
        let mut buf = [0u8; 16];
        let option = DhcpOption::Iana(IanaSerializer::new(3456, 1024, 54321, &[]));

        let () = <DhcpOptionImpl as RecordsSerializerImpl>::serialize(&mut buf, &option);
        assert_eq!(buf, [0, 3, 0, 12, 0, 0, 13, 128, 0, 0, 4, 0, 0, 0, 212, 49]);

        let options = Records::<_, ParsedDhcpOptionImpl>::parse_with_context(&buf[..], ())
            .expect("parse should succeed");
        let options: Vec<ParsedDhcpOption<'_>> = options.iter().collect();
        let iana_buf = [0, 0, 13, 128, 0, 0, 4, 0, 0, 0, 212, 49];
        assert_eq!(
            options[..],
            [ParsedDhcpOption::Iana(IanaData::new(&iana_buf[..]).expect("construction failed"))]
        );
    }

    // IA_NA must have option length >= 12, according to [RFC 8145, Section 21.4].
    //
    // [RFC 8145, Section 21.4]: https://tools.ietf.org/html/rfc8415#section-21.4
    #[test]
    fn test_iana_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 3, // opt code = 3, IA_NA
            0, 8, // invalid opt length, must be >= 12
            0, 0, 0, 0, 0, 0, 0, 0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::Iana, 8))
        );
    }

    #[test]
    fn test_iaaddr_no_suboptions_serialization_parsing_roundtrip() {
        let mut buf = [0u8; 28];
        let option = DhcpOption::IaAddr(IaAddrSerializer::new(
            Ipv6Addr::from([10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]),
            0,
            0,
            &[],
        ));

        let () = <DhcpOptionImpl as RecordsSerializerImpl>::serialize(&mut buf, &option);
        assert_eq!(
            buf,
            [
                0, 5, 0, 24, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0,
                0, 0, 0, 0, 0, 0
            ]
        );

        let options = Records::<_, ParsedDhcpOptionImpl>::parse_with_context(&buf[..], ())
            .expect("parse should succeed");
        let options: Vec<ParsedDhcpOption<'_>> = options.iter().collect();
        let iaaddr_buf = [
            10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0, 0, 0, 0, 0, 0, 0,
        ];
        assert_eq!(
            options[..],
            [ParsedDhcpOption::IaAddr(
                IaAddrData::new(&iaaddr_buf[..]).expect("construction failed")
            )]
        );
    }

    // IA Address must have option length >= 24, according to [RFC 8145, Section 21.6].
    //
    // [RFC 8145, Section 21.6]: https://tools.ietf.org/html/rfc8415#section-21.6
    #[test]
    fn test_iaaddr_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 5, // opt code = 5, IA Address
            0, 8, // invalid opt length, must be >= 24
            0, 0, 0, 0, 0, 0, 0, 0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::IaAddr, 8))
        );
    }

    // Oro must have a even option length, according to [RFC 8415, Section 21.7].
    //
    // [RFC 8415, Section 21.7]: https://tools.ietf.org/html/rfc8415#section-21.7
    #[test]
    fn test_invalid_oro_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 6, // opt code = 6, ORO
            0, 1, // invalid opt length, must be even
            0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::Oro, 1))
        );
    }

    // Preference must have option length 1, according to [RFC8415, Section 21.8].
    //
    // [RFC8415, Section 21.8]: https://tools.ietf.org/html/rfc8415#section-21.8
    #[test]
    fn test_invalid_preference_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 7, // opt code = 7, preference
            0, 2, // invalid opt length, must be even
            0, 0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::Preference, 2))
        );
    }

    // Elapsed time must have option length 2, according to [RFC 8145, Section 21.9].
    //
    // [RFC 8145, Section 21.9]: https://tools.ietf.org/html/rfc8415#section-21.9
    #[test]
    fn test_elapsed_time_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 8, // opt code = 8, elapsed time
            0, 3, // invalid opt length, must be even
            0, 0, 0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::ElapsedTime, 3))
        );
    }

    // Status code must have option length >= 2, according to [RFC 8145, Section 21.13].
    //
    // [RFC 8145, Section 21.13]: https://tools.ietf.org/html/rfc8415#section-21.13
    #[test]
    fn test_status_code_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 13, // opt code = 13, status code
            0, 1, // invalid opt length, must be >= 2
            0, 0, 0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::StatusCode, 1))
        );
    }
    // Information refresh time must have option length 4, according to [RFC 8145, Section 21.23].
    //
    // [RFC 8145, Section 21.23]: https://tools.ietf.org/html/rfc8415#section-21.23
    #[test]
    fn test_information_refresh_time_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 32, // opt code = 32, information refresh time
            0, 3, // invalid opt length, must be 4
            0, 0, 0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::InformationRefreshTime, 3))
        );
    }

    // SOL_MAX_RT must have option length 4, according to [RFC 8145, Section 21.24].
    //
    // [RFC 8145, Section 21.24]: https://tools.ietf.org/html/rfc8415#section-21.24
    #[test]
    fn test_sol_max_rt_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 82, // opt code = 82, SOL_MAX_RT
            0, 3, // invalid opt length, must be 4
            0, 0, 0,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::SolMaxRt, 3))
        );
    }
    // Option length of Dns servers must be multiples of 16, according to [RFC 3646, Section 3].
    //
    // [RFC 3646, Section 3]: https://tools.ietf.org/html/rfc3646#section-3
    #[test]
    fn test_dns_servers_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.extend_from_slice(&[
            0, 23, // opt code = 23, dns servers
            0, 17, // invalid opt length, must be multiple of 16
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        ]);
        assert_matches!(
            Message::parse(&mut &buf[..], ()),
            Err(ParseError::InvalidOpLen(OptionCode::DnsServers, 17))
        );
    }
}

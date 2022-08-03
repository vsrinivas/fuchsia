// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use net_types::ethernet::Mac as MacAddr;
use num_derive::FromPrimitive;
use serde::{Deserialize, Serialize};
use std::convert::{Infallible as Never, TryFrom, TryInto};
use std::fmt;
use std::iter::Iterator;
use std::net::Ipv4Addr;
use std::num::NonZeroU8;
use thiserror::Error;
use tracing::warn;

pub const SERVER_PORT: u16 = 67;
pub const CLIENT_PORT: u16 = 68;

const OP_IDX: usize = 0;
// currently unused
//const HTYPE_IDX: usize = 1;
//const HLEN_IDX: usize = 2;
//const HOPS_IDX: usize = 3;
const XID_IDX: usize = 4;
const SECS_IDX: usize = 8;
const FLAGS_IDX: usize = 10;
const CIADDR_IDX: usize = 12;
const YIADDR_IDX: usize = 16;
const SIADDR_IDX: usize = 20;
const GIADDR_IDX: usize = 24;
const CHADDR_IDX: usize = 28;
const SNAME_IDX: usize = 44;
const FILE_IDX: usize = 108;
const OPTIONS_START_IDX: usize = 236;

const ETHERNET_HTYPE: u8 = 1;
const ETHERNET_HLEN: u8 = 6;
const HOPS_DEFAULT: u8 = 0;
const MAGIC_COOKIE: [u8; 4] = [99, 130, 83, 99];

const UNUSED_CHADDR_BYTES: usize = 10;

const CHADDR_LEN: usize = 6;
const SNAME_LEN: usize = 64;
const FILE_LEN: usize = 128;
const IPV4_ADDR_LEN: usize = 4;

// Datagrams and DHCP Messages must both be at least 576 bytes.
// - Datagram: https://datatracker.ietf.org/doc/html/rfc2132#section-4.4
// - DHCP Message: https://datatracker.ietf.org/doc/html/rfc2132#section-9.10
const MIN_MESSAGE_SIZE: u16 = 576;

// Minimum legal value for mtu is 68.
// https://datatracker.ietf.org/doc/html/rfc2132#section-5.1
const MIN_MTU_VAL: u16 = 68;

const ASCII_NULL: char = '\x00';

#[derive(Debug, Error, PartialEq)]
pub enum ProtocolError {
    #[error("invalid buffer length: {}", _0)]
    InvalidBufferLength(usize),
    #[error("option not supported in fuchsia.net.dhcp: {:?}", _0)]
    InvalidFidlOption(DhcpOption),
    #[error("invalid message type: {}", _0)]
    InvalidMessageType(u8),
    #[error("invalid bootp op code: {}", _0)]
    InvalidOpCode(u8),
    #[error("invalid option code: {}", _0)]
    InvalidOptionCode(u8),
    #[error("invalid option value. code: {}, value: {:?}", _0, _1)]
    InvalidOptionValue(OptionCode, Vec<u8>),
    #[error("missing opcode")]
    MissingOpCode,
    #[error("missing expected option: {}", _0)]
    MissingOption(OptionCode),
    #[error(
        "malformed option {code} needs at least {want} bytes, but buffer has {remaining} remaining"
    )]
    MalformedOption { code: u8, remaining: usize, want: usize },
    #[error("received unknown fidl option variant")]
    UnknownFidlOption,
    #[error("invalid utf-8 after buffer index: {}", _0)]
    Utf8(usize),
    #[error("invalid protocol field {} = {} for message type {}", field, value, msg_type)]
    InvalidField { field: String, value: String, msg_type: MessageType },
}

#[derive(Debug, Error, PartialEq)]
#[error("Buffer is of invalid length: {0}")]
struct InvalidBufferLengthError(usize);

impl From<InvalidBufferLengthError> for ProtocolError {
    fn from(err: InvalidBufferLengthError) -> ProtocolError {
        let InvalidBufferLengthError(len) = err;
        ProtocolError::InvalidBufferLength(len)
    }
}

/// A DHCP protocol message as defined in RFC 2131.
///
/// All fields in `Message` follow the naming conventions outlined in the RFC.
/// Note that `Message` does not expose `htype`, `hlen`, or `hops` fields, as
/// these fields are effectively constants.
#[derive(Debug, PartialEq)]
pub struct Message {
    pub op: OpCode,
    pub xid: u32,
    pub secs: u16,
    pub bdcast_flag: bool,
    /// `ciaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub ciaddr: Ipv4Addr,
    /// `yiaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub yiaddr: Ipv4Addr,
    /// `siaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub siaddr: Ipv4Addr,
    /// `giaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub giaddr: Ipv4Addr,
    /// `chaddr` should be stored in Big-Endian order,
    /// e.g `[0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]`.
    pub chaddr: MacAddr,
    /// `sname` should not exceed 64 characters.
    pub sname: String,
    /// `file` should not exceed 128 characters.
    pub file: String,
    pub options: Vec<DhcpOption>,
}

impl Message {
    /// Instantiates a new `Message` from a byte buffer conforming to the DHCP
    /// protocol as defined RFC 2131. Returns `None` if the buffer is malformed.
    /// Any malformed configuration options will be skipped over, leaving only
    /// well formed `DhcpOption`s in the final `Message`.
    pub fn from_buffer(buf: &[u8]) -> Result<Self, ProtocolError> {
        let options =
            buf.get(OPTIONS_START_IDX..).ok_or(ProtocolError::InvalidBufferLength(buf.len()))?;
        let options = {
            let magic_cookie = options
                .get(..MAGIC_COOKIE.len())
                .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?;
            let options = options
                .get(MAGIC_COOKIE.len()..)
                .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?;
            if magic_cookie == MAGIC_COOKIE {
                parse_options(options, Vec::new())?
            } else {
                Vec::new()
            }
        };

        // Ordinarily, DHCP Options are stored in the variable length option field.
        // However, a client can, at its discretion, store Options in the typically unused
        // sname and file fields. If it wants to do this, it puts an OptionOverload option
        // in the options field to indicate that additional options are either in the sname
        // field, or the file field, or both. Consequently, we must:
        //
        // 1. Parse the options field.
        // 2. Check if the parsed options include an OptionOverload.
        // 3. If it does, grab the bytes from the field(s) indicated by the OptionOverload
        //    option.
        // 4. Parse those bytes into options.
        // 5. Combine those parsed options with whatever was in the variable length
        //    option field.
        //
        // From RFC 2131 pp23-24:
        //
        //     If the options in a DHCP message extend into the 'sname' and 'file'
        //     fields, the 'option overload' option MUST appear in the 'options' field,
        //     with value 1, 2 or 3, as specified in RFC 1533.
        //
        //     The options in the 'options' field MUST be interpreted first, so
        //     that any 'option overload' options may be interpreted.
        let overload = options.iter().find_map(|v| match v {
            &DhcpOption::OptionOverload(overload) => Some(overload),
            _ => None,
        });
        let sname =
            buf.get(SNAME_IDX..FILE_IDX).ok_or(ProtocolError::InvalidBufferLength(buf.len()))?;
        let file = buf
            .get(FILE_IDX..OPTIONS_START_IDX)
            .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?;
        let options = match overload {
            Some(overload) => {
                let extra_opts = match overload {
                    Overload::SName => sname,
                    Overload::File => file,
                    Overload::Both => buf
                        .get(SNAME_IDX..OPTIONS_START_IDX)
                        .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?,
                };
                parse_options(extra_opts, options)?
            }
            None => options,
        };
        Ok(Self {
            op: OpCode::try_from(*buf.get(OP_IDX).ok_or(ProtocolError::MissingOpCode)?)?,
            xid: u32::from_be_bytes(
                <[u8; 4]>::try_from(
                    buf.get(XID_IDX..SECS_IDX)
                        .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?,
                )
                .map_err(|std::array::TryFromSliceError { .. }| {
                    ProtocolError::InvalidBufferLength(buf.len())
                })?,
            ),
            secs: u16::from_be_bytes(
                <[u8; 2]>::try_from(
                    buf.get(SECS_IDX..FLAGS_IDX)
                        .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?,
                )
                .map_err(|std::array::TryFromSliceError { .. }| {
                    ProtocolError::InvalidBufferLength(buf.len())
                })?,
            ),
            bdcast_flag: *buf
                .get(FLAGS_IDX)
                .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?
                != 0,
            ciaddr: ip_addr_from_buf_at(buf, CIADDR_IDX)?,
            yiaddr: ip_addr_from_buf_at(buf, YIADDR_IDX)?,
            siaddr: ip_addr_from_buf_at(buf, SIADDR_IDX)?,
            giaddr: ip_addr_from_buf_at(buf, GIADDR_IDX)?,
            chaddr: MacAddr::new(
                buf.get(CHADDR_IDX..CHADDR_IDX + CHADDR_LEN)
                    .ok_or(ProtocolError::InvalidBufferLength(buf.len()))?
                    .try_into()
                    .map_err(|std::array::TryFromSliceError { .. }| {
                        ProtocolError::InvalidBufferLength(buf.len())
                    })?,
            ),
            sname: match overload {
                Some(Overload::SName) | Some(Overload::Both) => String::from(""),
                Some(Overload::File) | None => buf_to_msg_string(sname)?,
            },
            file: match overload {
                Some(Overload::File) | Some(Overload::Both) => String::from(""),
                Some(Overload::SName) | None => buf_to_msg_string(file)?,
            },
            options,
        })
    }

    /// Consumes the calling `Message` to serialize it into a buffer of bytes.
    pub fn serialize(self) -> Vec<u8> {
        let Self {
            op,
            xid,
            secs,
            bdcast_flag,
            ciaddr,
            yiaddr,
            siaddr,
            giaddr,
            chaddr,
            sname,
            file,
            options,
        } = self;
        let mut buffer = Vec::with_capacity(OPTIONS_START_IDX);
        buffer.push(op.into());
        buffer.push(ETHERNET_HTYPE);
        buffer.push(ETHERNET_HLEN);
        buffer.push(HOPS_DEFAULT);
        buffer.extend_from_slice(&xid.to_be_bytes());
        buffer.extend_from_slice(&secs.to_be_bytes());
        if bdcast_flag {
            // Set most significant bit.
            buffer.push(128u8);
        } else {
            buffer.push(0u8);
        }
        buffer.push(0u8);
        buffer.extend_from_slice(&ciaddr.octets());
        buffer.extend_from_slice(&yiaddr.octets());
        buffer.extend_from_slice(&siaddr.octets());
        buffer.extend_from_slice(&giaddr.octets());
        buffer.extend_from_slice(&chaddr.bytes().as_ref());
        buffer.extend_from_slice(&[0u8; UNUSED_CHADDR_BYTES]);
        trunc_string_to_n_and_push(&sname, SNAME_LEN, &mut buffer);
        trunc_string_to_n_and_push(&file, FILE_LEN, &mut buffer);

        buffer.extend_from_slice(&MAGIC_COOKIE);
        for option in options.into_iter() {
            option.serialize_to(&mut buffer);
        }
        buffer.push(OptionCode::End.into());

        buffer
    }

    /// Returns the value's DHCP `MessageType` or appropriate `MessageTypeError` in case of failure.
    pub fn get_dhcp_type(&self) -> Result<MessageType, ProtocolError> {
        self.options
            .iter()
            .filter_map(|opt| match opt {
                DhcpOption::DhcpMessageType(v) => Some(*v),
                _ => None,
            })
            .next()
            .ok_or(ProtocolError::MissingOption(OptionCode::DhcpMessageType))
    }
}

pub mod identifier {
    use super::{DhcpOption, Message, CHADDR_LEN};
    use net_types::ethernet::Mac as MacAddr;
    use std::convert::TryInto as _;

    const CLIENT_IDENTIFIER_ID: &'static str = "id";
    const CLIENT_IDENTIFIER_CHADDR: &'static str = "chaddr";

    /// An opaque identifier which uniquely identifies a DHCP client to a DHCP server.
    #[derive(Clone, Debug, Eq, Hash, PartialEq)]
    pub struct ClientIdentifier {
        inner: ClientIdentifierInner,
    }

    #[derive(Clone, Debug, Eq, Hash, PartialEq)]
    enum ClientIdentifierInner {
        /// An identifier derived from a Client-identifier DHCP Option, as defined in
        /// https://tools.ietf.org/html/rfc2132#section-9.14.
        Id(Vec<u8>),
        /// An identifier derived from the chaddr field of a DHCP message, typically only used in the
        /// absense of the Client-identifier DHCP Option.
        Chaddr(MacAddr),
    }

    impl From<MacAddr> for ClientIdentifier {
        fn from(v: MacAddr) -> Self {
            Self { inner: ClientIdentifierInner::Chaddr(v) }
        }
    }

    impl From<&Message> for ClientIdentifier {
        /// Returns the opaque client identifier associated with the argument message.
        ///
        /// Typically, a message will contain a `DhcpOption::ClientIdentifier` which stores the
        /// associated opaque client identifier. In the absence of this option, an identifier
        /// will be constructed from the `chaddr` field of the message.
        fn from(msg: &Message) -> ClientIdentifier {
            msg.options
                .iter()
                .find_map(|opt| match opt {
                    DhcpOption::ClientIdentifier(v) => {
                        Some(ClientIdentifier { inner: ClientIdentifierInner::Id(v.clone()) })
                    }
                    _ => None,
                })
                .unwrap_or_else(|| ClientIdentifier::from(msg.chaddr.clone()))
        }
    }

    impl std::str::FromStr for ClientIdentifier {
        type Err = anyhow::Error;

        fn from_str(s: &str) -> Result<Self, Self::Err> {
            let mut id_parts = s.splitn(2, ":");
            let id_type = id_parts
                .next()
                .ok_or(anyhow::anyhow!("no client id type found in string: {}", s))?;
            let id =
                id_parts.next().ok_or(anyhow::anyhow!("no client id found in string: {}", s))?;
            let () = match id_parts.next() {
                None => (),
                Some(v) => {
                    return Err(anyhow::anyhow!(
                        "client id string contained unexpected fields: {}",
                        v
                    ))
                }
            };
            let id = hex::decode(id)?;
            match id_type {
                CLIENT_IDENTIFIER_ID => Ok(Self { inner: ClientIdentifierInner::Id(id) }),
                CLIENT_IDENTIFIER_CHADDR => Ok(Self {
                    inner: ClientIdentifierInner::Chaddr(MacAddr::new(
                        id.get(..CHADDR_LEN)
                            .ok_or(anyhow::anyhow!("client id had insufficient length: {:?}", id))?
                            .try_into()?,
                    )),
                }),
                id_type => Err(anyhow::anyhow!("unrecognized client id type: {}", id_type)),
            }
        }
    }

    impl std::fmt::Display for ClientIdentifier {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            use zerocopy::AsBytes as _;
            let (id_type, id) = match self {
                Self { inner: ClientIdentifierInner::Id(v) } => (CLIENT_IDENTIFIER_ID, &v[..]),
                Self { inner: ClientIdentifierInner::Chaddr(v) } => {
                    (CLIENT_IDENTIFIER_CHADDR, v.as_bytes())
                }
            };
            write!(f, "{}:{}", id_type, hex::encode(id))
        }
    }
}

/// A DHCP protocol op-code as defined in RFC 2131.
///
/// Note that this type corresponds to the first field of a DHCP message,
/// opcode, and is distinct from the OptionCode type. In this case, "Op"
/// is an abbreviation for Operator, not Option.
///
/// `OpCode::BOOTREQUEST` should only appear in protocol messages from the
/// client, and conversely `OpCode::BOOTREPLY` should only appear in messages
/// from the server.
#[derive(FromPrimitive, Copy, Clone, Debug, PartialEq)]
#[repr(u8)]
pub enum OpCode {
    BOOTREQUEST = 1,
    BOOTREPLY = 2,
}

impl fmt::Display for OpCode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            OpCode::BOOTREQUEST => write!(f, "BOOTREQUEST"),
            OpCode::BOOTREPLY => write!(f, "BOOTREPLY"),
        }
    }
}

impl Into<u8> for OpCode {
    fn into(self) -> u8 {
        self as u8
    }
}

impl TryFrom<u8> for OpCode {
    type Error = ProtocolError;

    fn try_from(n: u8) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u8(n).ok_or(ProtocolError::InvalidOpCode(n))
    }
}

/// A DHCP option code.
///
/// This enum corresponds to the codes for DHCP options as defined in
/// RFC 1533. Note that not all options defined in the RFC are represented
/// here; options which are not in this type are not currently supported. Supported
/// options appear in this type in the order in which they are defined in the RFC.
#[derive(Copy, Clone, Debug, Deserialize, Eq, FromPrimitive, Hash, PartialEq, Serialize)]
#[repr(u8)]
pub enum OptionCode {
    Pad = 0,
    SubnetMask = 1,
    TimeOffset = 2,
    Router = 3,
    TimeServer = 4,
    NameServer = 5,
    DomainNameServer = 6,
    LogServer = 7,
    CookieServer = 8,
    LprServer = 9,
    ImpressServer = 10,
    ResourceLocationServer = 11,
    HostName = 12,
    BootFileSize = 13,
    MeritDumpFile = 14,
    DomainName = 15,
    SwapServer = 16,
    RootPath = 17,
    ExtensionsPath = 18,
    IpForwarding = 19,
    NonLocalSourceRouting = 20,
    PolicyFilter = 21,
    MaxDatagramReassemblySize = 22,
    DefaultIpTtl = 23,
    PathMtuAgingTimeout = 24,
    PathMtuPlateauTable = 25,
    InterfaceMtu = 26,
    AllSubnetsLocal = 27,
    BroadcastAddress = 28,
    PerformMaskDiscovery = 29,
    MaskSupplier = 30,
    PerformRouterDiscovery = 31,
    RouterSolicitationAddress = 32,
    StaticRoute = 33,
    TrailerEncapsulation = 34,
    ArpCacheTimeout = 35,
    EthernetEncapsulation = 36,
    TcpDefaultTtl = 37,
    TcpKeepaliveInterval = 38,
    TcpKeepaliveGarbage = 39,
    NetworkInformationServiceDomain = 40,
    NetworkInformationServers = 41,
    NetworkTimeProtocolServers = 42,
    VendorSpecificInformation = 43,
    NetBiosOverTcpipNameServer = 44,
    NetBiosOverTcpipDatagramDistributionServer = 45,
    NetBiosOverTcpipNodeType = 46,
    NetBiosOverTcpipScope = 47,
    XWindowSystemFontServer = 48,
    XWindowSystemDisplayManager = 49,
    RequestedIpAddress = 50,
    IpAddressLeaseTime = 51,
    OptionOverload = 52,
    DhcpMessageType = 53,
    ServerIdentifier = 54,
    ParameterRequestList = 55,
    Message = 56,
    MaxDhcpMessageSize = 57,
    RenewalTimeValue = 58,
    RebindingTimeValue = 59,
    VendorClassIdentifier = 60,
    ClientIdentifier = 61,
    NetworkInformationServicePlusDomain = 64,
    NetworkInformationServicePlusServers = 65,
    TftpServerName = 66,
    BootfileName = 67,
    MobileIpHomeAgent = 68,
    SmtpServer = 69,
    Pop3Server = 70,
    NntpServer = 71,
    DefaultWwwServer = 72,
    DefaultFingerServer = 73,
    DefaultIrcServer = 74,
    StreetTalkServer = 75,
    StreetTalkDirectoryAssistanceServer = 76,
    End = 255,
}

impl Into<u8> for OptionCode {
    fn into(self) -> u8 {
        self as u8
    }
}

impl TryFrom<u8> for OptionCode {
    type Error = ProtocolError;

    fn try_from(n: u8) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u8(n).ok_or(ProtocolError::InvalidOptionCode(n))
    }
}

impl fmt::Display for OptionCode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&self, f)
    }
}

/// A DHCP Option as defined in RFC 2132.
/// DHCP Options provide a mechanism for transmitting configuration parameters
/// between the Server and Client and vice-versa. DHCP Options also include
/// some control and meta information needed for the operation of the DHCP
/// protocol but which could not be included in the DHCP header because of
/// the backwards compatibility requirement with the older BOOTP protocol.
#[derive(Clone, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub enum DhcpOption {
    Pad(),
    End(),
    SubnetMask(Ipv4Addr),
    TimeOffset(i32),
    Router(Vec<Ipv4Addr>),
    TimeServer(Vec<Ipv4Addr>),
    NameServer(Vec<Ipv4Addr>),
    DomainNameServer(Vec<Ipv4Addr>),
    LogServer(Vec<Ipv4Addr>),
    CookieServer(Vec<Ipv4Addr>),
    LprServer(Vec<Ipv4Addr>),
    ImpressServer(Vec<Ipv4Addr>),
    ResourceLocationServer(Vec<Ipv4Addr>),
    HostName(String),
    BootFileSize(u16),
    MeritDumpFile(String),
    DomainName(String),
    SwapServer(Ipv4Addr),
    RootPath(String),
    ExtensionsPath(String),
    IpForwarding(bool),
    NonLocalSourceRouting(bool),
    PolicyFilter(Vec<Ipv4Addr>),
    MaxDatagramReassemblySize(u16),
    // DefaultIpTtl cannot be zero: https://datatracker.ietf.org/doc/html/rfc2132#section-4.5
    DefaultIpTtl(NonZeroU8),
    PathMtuAgingTimeout(u32),
    PathMtuPlateauTable(Vec<u16>),
    InterfaceMtu(u16),
    AllSubnetsLocal(bool),
    BroadcastAddress(Ipv4Addr),
    PerformMaskDiscovery(bool),
    MaskSupplier(bool),
    PerformRouterDiscovery(bool),
    RouterSolicitationAddress(Ipv4Addr),
    StaticRoute(Vec<Ipv4Addr>),
    TrailerEncapsulation(bool),
    ArpCacheTimeout(u32),
    EthernetEncapsulation(bool),
    // TcpDefaultTtl cannot be zero: https://datatracker.ietf.org/doc/html/rfc2132#section-7.1
    TcpDefaultTtl(NonZeroU8),
    TcpKeepaliveInterval(u32),
    TcpKeepaliveGarbage(bool),
    NetworkInformationServiceDomain(String),
    NetworkInformationServers(Vec<Ipv4Addr>),
    NetworkTimeProtocolServers(Vec<Ipv4Addr>),
    VendorSpecificInformation(Vec<u8>),
    NetBiosOverTcpipNameServer(Vec<Ipv4Addr>),
    NetBiosOverTcpipDatagramDistributionServer(Vec<Ipv4Addr>),
    NetBiosOverTcpipNodeType(NodeType),
    NetBiosOverTcpipScope(String),
    XWindowSystemFontServer(Vec<Ipv4Addr>),
    XWindowSystemDisplayManager(Vec<Ipv4Addr>),
    NetworkInformationServicePlusDomain(String),
    NetworkInformationServicePlusServers(Vec<Ipv4Addr>),
    MobileIpHomeAgent(Vec<Ipv4Addr>),
    SmtpServer(Vec<Ipv4Addr>),
    Pop3Server(Vec<Ipv4Addr>),
    NntpServer(Vec<Ipv4Addr>),
    DefaultWwwServer(Vec<Ipv4Addr>),
    DefaultFingerServer(Vec<Ipv4Addr>),
    DefaultIrcServer(Vec<Ipv4Addr>),
    StreetTalkServer(Vec<Ipv4Addr>),
    StreetTalkDirectoryAssistanceServer(Vec<Ipv4Addr>),
    RequestedIpAddress(Ipv4Addr),
    IpAddressLeaseTime(u32),
    OptionOverload(Overload),
    TftpServerName(String),
    BootfileName(String),
    DhcpMessageType(MessageType),
    ServerIdentifier(Ipv4Addr),
    ParameterRequestList(Vec<OptionCode>),
    Message(String),
    MaxDhcpMessageSize(u16),
    RenewalTimeValue(u32),
    RebindingTimeValue(u32),
    VendorClassIdentifier(Vec<u8>),
    ClientIdentifier(Vec<u8>),
}

/// Generates a match expression on `$option` which maps each of the supplied `DhcpOption` variants
/// to their `OptionCode` equivalent.
macro_rules! option_to_code {
    ($option:ident, $(DhcpOption::$variant:tt($($v:tt)*)),*) => {
        match $option {
            $(DhcpOption::$variant($($v)*) => OptionCode::$variant,)*
        }
    };
}

impl DhcpOption {
    fn from_raw_parts(code: OptionCode, val: &[u8]) -> Result<Self, ProtocolError> {
        match code {
            OptionCode::Pad => Ok(DhcpOption::Pad()),
            OptionCode::End => Ok(DhcpOption::End()),
            OptionCode::SubnetMask => Ok(DhcpOption::SubnetMask(bytes_to_addr(val)?)),
            OptionCode::TimeOffset => {
                let offset = get_byte_array::<4>(val).map(i32::from_be_bytes)?;
                Ok(DhcpOption::TimeOffset(offset))
            }
            OptionCode::Router => Ok(DhcpOption::Router(bytes_to_addrs(val)?)),
            OptionCode::TimeServer => Ok(DhcpOption::TimeServer(bytes_to_addrs(val)?)),
            OptionCode::NameServer => Ok(DhcpOption::NameServer(bytes_to_addrs(val)?)),
            OptionCode::DomainNameServer => Ok(DhcpOption::DomainNameServer(bytes_to_addrs(val)?)),
            OptionCode::LogServer => Ok(DhcpOption::LogServer(bytes_to_addrs(val)?)),
            OptionCode::CookieServer => Ok(DhcpOption::CookieServer(bytes_to_addrs(val)?)),
            OptionCode::LprServer => Ok(DhcpOption::LprServer(bytes_to_addrs(val)?)),
            OptionCode::ImpressServer => Ok(DhcpOption::ImpressServer(bytes_to_addrs(val)?)),
            OptionCode::ResourceLocationServer => {
                Ok(DhcpOption::ResourceLocationServer(bytes_to_addrs(val)?))
            }
            OptionCode::HostName => Ok(DhcpOption::HostName(bytes_to_nonempty_str(val)?)),
            OptionCode::BootFileSize => {
                let size = get_byte_array::<2>(val).map(u16::from_be_bytes)?;
                Ok(DhcpOption::BootFileSize(size))
            }
            OptionCode::MeritDumpFile => Ok(DhcpOption::MeritDumpFile(bytes_to_nonempty_str(val)?)),
            OptionCode::DomainName => Ok(DhcpOption::DomainName(bytes_to_nonempty_str(val)?)),
            OptionCode::SwapServer => Ok(DhcpOption::SwapServer(bytes_to_addr(val)?)),
            OptionCode::RootPath => Ok(DhcpOption::RootPath(bytes_to_nonempty_str(val)?)),
            OptionCode::ExtensionsPath => {
                Ok(DhcpOption::ExtensionsPath(bytes_to_nonempty_str(val)?))
            }
            OptionCode::IpForwarding => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::IpForwarding(flag))
            }
            OptionCode::NonLocalSourceRouting => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::NonLocalSourceRouting(flag))
            }
            OptionCode::PolicyFilter => {
                let addrs = bytes_to_addrs(val)?;
                if addrs.len() < 2 || addrs.len() % 2 != 0 {
                    return Err(ProtocolError::InvalidBufferLength(val.len()));
                }
                Ok(DhcpOption::PolicyFilter(addrs))
            }
            OptionCode::MaxDatagramReassemblySize => {
                let max_datagram = get_byte_array::<2>(val).map(u16::from_be_bytes)?;
                if max_datagram < MIN_MESSAGE_SIZE {
                    return Err(ProtocolError::InvalidOptionValue(code, val.to_vec()));
                }
                Ok(DhcpOption::MaxDatagramReassemblySize(max_datagram))
            }
            OptionCode::DefaultIpTtl => {
                let ttl = get_byte(val)?;
                let ttl = NonZeroU8::new(ttl)
                    .ok_or(ProtocolError::InvalidOptionValue(code, val.to_vec()))?;
                Ok(DhcpOption::DefaultIpTtl(ttl))
            }
            OptionCode::PathMtuAgingTimeout => {
                let timeout = get_byte_array::<4>(val).map(u32::from_be_bytes)?;
                Ok(DhcpOption::PathMtuAgingTimeout(timeout))
            }
            OptionCode::PathMtuPlateauTable => {
                let val = nonempty(val)?;
                let mtus = val
                    .chunks(2)
                    .map(|chunk| get_byte_array::<2>(chunk).map(u16::from_be_bytes))
                    .collect::<Result<Vec<u16>, InvalidBufferLengthError>>()
                    .map_err(|InvalidBufferLengthError(_)| {
                        ProtocolError::InvalidBufferLength(val.len())
                    })?;
                Ok(DhcpOption::PathMtuPlateauTable(mtus))
            }
            OptionCode::InterfaceMtu => {
                let mtu = get_byte_array::<2>(val).map(u16::from_be_bytes)?;
                if mtu < MIN_MTU_VAL {
                    return Err(ProtocolError::InvalidOptionValue(code, val.to_vec()));
                }
                Ok(DhcpOption::InterfaceMtu(mtu))
            }
            OptionCode::AllSubnetsLocal => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::AllSubnetsLocal(flag))
            }
            OptionCode::BroadcastAddress => Ok(DhcpOption::BroadcastAddress(bytes_to_addr(val)?)),
            OptionCode::PerformMaskDiscovery => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::PerformMaskDiscovery(flag))
            }
            OptionCode::MaskSupplier => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::MaskSupplier(flag))
            }
            OptionCode::PerformRouterDiscovery => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::PerformRouterDiscovery(flag))
            }
            OptionCode::RouterSolicitationAddress => {
                Ok(DhcpOption::RouterSolicitationAddress(bytes_to_addr(val)?))
            }
            OptionCode::StaticRoute => {
                let addrs = bytes_to_addrs(val)?;
                if addrs.len() < 2 || addrs.len() % 2 != 0 {
                    return Err(ProtocolError::InvalidBufferLength(val.len()));
                }
                Ok(DhcpOption::StaticRoute(addrs))
            }
            OptionCode::TrailerEncapsulation => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::TrailerEncapsulation(flag))
            }
            OptionCode::ArpCacheTimeout => {
                let timeout = get_byte_array::<4>(val).map(u32::from_be_bytes)?;
                Ok(DhcpOption::ArpCacheTimeout(timeout))
            }
            OptionCode::EthernetEncapsulation => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::EthernetEncapsulation(flag))
            }
            OptionCode::TcpDefaultTtl => {
                let ttl = get_byte(val)?;
                let ttl = NonZeroU8::new(ttl)
                    .ok_or(ProtocolError::InvalidOptionValue(code, val.to_vec()))?;
                Ok(DhcpOption::TcpDefaultTtl(ttl))
            }
            OptionCode::TcpKeepaliveInterval => {
                let interval = get_byte_array::<4>(val).map(u32::from_be_bytes)?;
                Ok(DhcpOption::TcpKeepaliveInterval(interval))
            }
            OptionCode::TcpKeepaliveGarbage => {
                let flag = bytes_to_bool(val).map_err(|e| e.to_protocol(code))?;
                Ok(DhcpOption::TcpKeepaliveGarbage(flag))
            }
            OptionCode::NetworkInformationServiceDomain => {
                let name = bytes_to_nonempty_str(val)?;
                Ok(DhcpOption::NetworkInformationServiceDomain(name))
            }
            OptionCode::NetworkInformationServers => {
                Ok(DhcpOption::NetworkInformationServers(bytes_to_addrs(val)?))
            }
            OptionCode::NetworkTimeProtocolServers => {
                Ok(DhcpOption::NetworkTimeProtocolServers(bytes_to_addrs(val)?))
            }
            OptionCode::VendorSpecificInformation => {
                let val = nonempty(val)?;
                Ok(DhcpOption::VendorSpecificInformation(val.to_owned()))
            }
            OptionCode::NetBiosOverTcpipNameServer => {
                Ok(DhcpOption::NetBiosOverTcpipNameServer(bytes_to_addrs(val)?))
            }
            OptionCode::NetBiosOverTcpipDatagramDistributionServer => {
                Ok(DhcpOption::NetBiosOverTcpipDatagramDistributionServer(bytes_to_addrs(val)?))
            }
            OptionCode::NetBiosOverTcpipNodeType => {
                let byte = get_byte(val)?;
                Ok(DhcpOption::NetBiosOverTcpipNodeType(NodeType::try_from(byte)?))
            }
            OptionCode::NetBiosOverTcpipScope => {
                Ok(DhcpOption::NetBiosOverTcpipScope(bytes_to_nonempty_str(val)?))
            }
            OptionCode::XWindowSystemFontServer => {
                Ok(DhcpOption::XWindowSystemFontServer(bytes_to_addrs(val)?))
            }
            OptionCode::XWindowSystemDisplayManager => {
                Ok(DhcpOption::XWindowSystemDisplayManager(bytes_to_addrs(val)?))
            }
            OptionCode::NetworkInformationServicePlusDomain => {
                Ok(DhcpOption::NetworkInformationServicePlusDomain(bytes_to_nonempty_str(val)?))
            }
            OptionCode::NetworkInformationServicePlusServers => {
                Ok(DhcpOption::NetworkInformationServicePlusServers(bytes_to_addrs(val)?))
            }
            OptionCode::MobileIpHomeAgent => {
                // Mobile IP Home Agent is allowed to be empty indicating no home agents are
                // available: https://datatracker.ietf.org/doc/html/rfc2132#section-8.13
                if val.len() == 0 {
                    return Ok(DhcpOption::MobileIpHomeAgent(Vec::new()));
                }
                Ok(DhcpOption::MobileIpHomeAgent(bytes_to_addrs(val)?))
            }
            OptionCode::SmtpServer => Ok(DhcpOption::SmtpServer(bytes_to_addrs(val)?)),
            OptionCode::Pop3Server => Ok(DhcpOption::Pop3Server(bytes_to_addrs(val)?)),
            OptionCode::NntpServer => Ok(DhcpOption::NntpServer(bytes_to_addrs(val)?)),
            OptionCode::DefaultWwwServer => Ok(DhcpOption::DefaultWwwServer(bytes_to_addrs(val)?)),
            OptionCode::DefaultFingerServer => {
                Ok(DhcpOption::DefaultFingerServer(bytes_to_addrs(val)?))
            }
            OptionCode::DefaultIrcServer => Ok(DhcpOption::DefaultIrcServer(bytes_to_addrs(val)?)),
            OptionCode::StreetTalkServer => Ok(DhcpOption::StreetTalkServer(bytes_to_addrs(val)?)),
            OptionCode::StreetTalkDirectoryAssistanceServer => {
                Ok(DhcpOption::StreetTalkDirectoryAssistanceServer(bytes_to_addrs(val)?))
            }
            OptionCode::RequestedIpAddress => {
                Ok(DhcpOption::RequestedIpAddress(bytes_to_addr(val)?))
            }
            OptionCode::IpAddressLeaseTime => {
                let lease_time = get_byte_array::<4>(val).map(u32::from_be_bytes)?;
                Ok(DhcpOption::IpAddressLeaseTime(lease_time))
            }
            OptionCode::OptionOverload => {
                let overload = Overload::try_from(
                    *val.first().ok_or(ProtocolError::InvalidBufferLength(val.len()))?,
                )?;
                Ok(DhcpOption::OptionOverload(overload))
            }
            OptionCode::TftpServerName => {
                let name = bytes_to_nonempty_str(val)?;
                Ok(DhcpOption::TftpServerName(name))
            }
            OptionCode::BootfileName => {
                let name = bytes_to_nonempty_str(val)?;
                Ok(DhcpOption::BootfileName(name))
            }
            OptionCode::DhcpMessageType => {
                let message_type = MessageType::try_from(
                    *val.first().ok_or(ProtocolError::InvalidBufferLength(val.len()))?,
                )?;
                Ok(DhcpOption::DhcpMessageType(message_type))
            }
            OptionCode::ServerIdentifier => Ok(DhcpOption::ServerIdentifier(bytes_to_addr(val)?)),
            OptionCode::ParameterRequestList => {
                let val = nonempty(val)?;
                let opcodes =
                    val.into_iter().filter_map(|code| OptionCode::try_from(*code).ok()).collect();
                Ok(DhcpOption::ParameterRequestList(opcodes))
            }
            OptionCode::Message => Ok(DhcpOption::Message(bytes_to_nonempty_str(val)?)),
            OptionCode::MaxDhcpMessageSize => {
                let max_size = get_byte_array::<2>(val).map(u16::from_be_bytes)?;
                if max_size < MIN_MESSAGE_SIZE {
                    return Err(ProtocolError::InvalidOptionValue(code, val.to_vec()));
                }
                Ok(DhcpOption::MaxDhcpMessageSize(max_size))
            }
            OptionCode::RenewalTimeValue => {
                let renewal_time = get_byte_array::<4>(val).map(u32::from_be_bytes)?;
                Ok(DhcpOption::RenewalTimeValue(renewal_time))
            }
            OptionCode::RebindingTimeValue => {
                let rebinding_time = get_byte_array::<4>(val).map(u32::from_be_bytes)?;
                Ok(DhcpOption::RebindingTimeValue(rebinding_time))
            }
            OptionCode::VendorClassIdentifier => {
                let val = nonempty(val)?;
                Ok(DhcpOption::VendorClassIdentifier(val.to_owned()))
            }
            OptionCode::ClientIdentifier => {
                // Client Identifier must be at least two bytes.
                // https://datatracker.ietf.org/doc/html/rfc2132#section-9.14
                if val.len() < 2 {
                    return Err(ProtocolError::InvalidBufferLength(val.len()));
                }
                Ok(DhcpOption::ClientIdentifier(val.to_owned()))
            }
        }
    }

    fn serialize_to(self, buf: &mut Vec<u8>) {
        match self {
            DhcpOption::Pad() => {
                buf.push(OptionCode::Pad.into());
            }
            DhcpOption::End() => {
                buf.push(OptionCode::End.into());
            }
            DhcpOption::SubnetMask(v) => serialize_address(OptionCode::SubnetMask, v, buf),
            DhcpOption::TimeOffset(v) => {
                let size = std::mem::size_of::<i32>();
                buf.push(OptionCode::TimeOffset.into());
                buf.push(size as u8);
                buf.extend_from_slice(&v.to_be_bytes());
            }
            DhcpOption::Router(v) => serialize_addresses(OptionCode::Router, &v, buf),
            DhcpOption::TimeServer(v) => serialize_addresses(OptionCode::TimeServer, &v, buf),
            DhcpOption::NameServer(v) => serialize_addresses(OptionCode::NameServer, &v, buf),
            DhcpOption::DomainNameServer(v) => {
                serialize_addresses(OptionCode::DomainNameServer, &v, buf)
            }
            DhcpOption::LogServer(v) => serialize_addresses(OptionCode::LogServer, &v, buf),
            DhcpOption::CookieServer(v) => serialize_addresses(OptionCode::CookieServer, &v, buf),
            DhcpOption::LprServer(v) => serialize_addresses(OptionCode::LprServer, &v, buf),
            DhcpOption::ImpressServer(v) => serialize_addresses(OptionCode::ImpressServer, &v, buf),
            DhcpOption::ResourceLocationServer(v) => {
                serialize_addresses(OptionCode::ResourceLocationServer, &v, buf)
            }
            DhcpOption::HostName(v) => serialize_string(OptionCode::HostName, &v, buf),
            DhcpOption::BootFileSize(v) => serialize_u16(OptionCode::BootFileSize, v, buf),
            DhcpOption::MeritDumpFile(v) => serialize_string(OptionCode::MeritDumpFile, &v, buf),
            DhcpOption::DomainName(v) => serialize_string(OptionCode::DomainName, &v, buf),
            DhcpOption::SwapServer(v) => serialize_address(OptionCode::SwapServer, v, buf),
            DhcpOption::RootPath(v) => serialize_string(OptionCode::RootPath, &v, buf),
            DhcpOption::ExtensionsPath(v) => serialize_string(OptionCode::ExtensionsPath, &v, buf),
            DhcpOption::IpForwarding(v) => serialize_flag(OptionCode::IpForwarding, v, buf),
            DhcpOption::NonLocalSourceRouting(v) => {
                serialize_flag(OptionCode::NonLocalSourceRouting, v, buf)
            }
            DhcpOption::PolicyFilter(v) => serialize_addresses(OptionCode::PolicyFilter, &v, buf),
            DhcpOption::MaxDatagramReassemblySize(v) => {
                serialize_u16(OptionCode::MaxDatagramReassemblySize, v, buf)
            }
            DhcpOption::DefaultIpTtl(v) => serialize_u8(OptionCode::DefaultIpTtl, v.into(), buf),
            DhcpOption::PathMtuAgingTimeout(v) => {
                serialize_u32(OptionCode::PathMtuAgingTimeout, v, buf)
            }
            DhcpOption::PathMtuPlateauTable(v) => {
                let size = std::mem::size_of_val(&v);
                buf.push(OptionCode::PathMtuPlateauTable.into());
                buf.push(size as u8);
                for mtu in v {
                    buf.extend_from_slice(&mtu.to_be_bytes())
                }
            }
            DhcpOption::InterfaceMtu(v) => serialize_u16(OptionCode::InterfaceMtu, v, buf),
            DhcpOption::AllSubnetsLocal(v) => serialize_flag(OptionCode::AllSubnetsLocal, v, buf),
            DhcpOption::BroadcastAddress(v) => {
                serialize_address(OptionCode::BroadcastAddress, v, buf)
            }
            DhcpOption::PerformMaskDiscovery(v) => {
                serialize_flag(OptionCode::PerformMaskDiscovery, v, buf)
            }
            DhcpOption::MaskSupplier(v) => serialize_flag(OptionCode::MaskSupplier, v, buf),
            DhcpOption::PerformRouterDiscovery(v) => {
                serialize_flag(OptionCode::PerformRouterDiscovery, v, buf)
            }
            DhcpOption::RouterSolicitationAddress(v) => {
                serialize_address(OptionCode::RouterSolicitationAddress, v, buf)
            }
            DhcpOption::StaticRoute(v) => serialize_addresses(OptionCode::StaticRoute, &v, buf),
            DhcpOption::TrailerEncapsulation(v) => {
                serialize_flag(OptionCode::TrailerEncapsulation, v, buf)
            }
            DhcpOption::ArpCacheTimeout(v) => serialize_u32(OptionCode::ArpCacheTimeout, v, buf),
            DhcpOption::EthernetEncapsulation(v) => {
                serialize_flag(OptionCode::EthernetEncapsulation, v, buf)
            }
            DhcpOption::TcpDefaultTtl(v) => serialize_u8(OptionCode::TcpDefaultTtl, v.into(), buf),
            DhcpOption::TcpKeepaliveInterval(v) => {
                serialize_u32(OptionCode::TcpKeepaliveInterval, v, buf)
            }
            DhcpOption::TcpKeepaliveGarbage(v) => {
                serialize_flag(OptionCode::TcpKeepaliveGarbage, v, buf)
            }
            DhcpOption::NetworkInformationServiceDomain(v) => {
                serialize_string(OptionCode::NetworkInformationServiceDomain, &v, buf)
            }
            DhcpOption::NetworkInformationServers(v) => {
                serialize_addresses(OptionCode::NetworkInformationServers, &v, buf)
            }
            DhcpOption::NetworkTimeProtocolServers(v) => {
                serialize_addresses(OptionCode::NetworkTimeProtocolServers, &v, buf)
            }
            DhcpOption::VendorSpecificInformation(v) => {
                serialize_bytes(OptionCode::VendorSpecificInformation, &v, buf)
            }
            DhcpOption::NetBiosOverTcpipNameServer(v) => {
                serialize_addresses(OptionCode::NetBiosOverTcpipNameServer, &v, buf)
            }
            DhcpOption::NetBiosOverTcpipDatagramDistributionServer(v) => {
                serialize_addresses(OptionCode::NetBiosOverTcpipDatagramDistributionServer, &v, buf)
            }
            DhcpOption::NetBiosOverTcpipNodeType(v) => {
                serialize_enum(OptionCode::NetBiosOverTcpipNodeType, v, buf)
            }
            DhcpOption::NetBiosOverTcpipScope(v) => {
                serialize_string(OptionCode::NetBiosOverTcpipScope, &v, buf)
            }
            DhcpOption::XWindowSystemFontServer(v) => {
                serialize_addresses(OptionCode::XWindowSystemFontServer, &v, buf)
            }
            DhcpOption::XWindowSystemDisplayManager(v) => {
                serialize_addresses(OptionCode::XWindowSystemDisplayManager, &v, buf)
            }
            DhcpOption::NetworkInformationServicePlusDomain(v) => {
                serialize_string(OptionCode::NetworkInformationServicePlusDomain, &v, buf)
            }
            DhcpOption::NetworkInformationServicePlusServers(v) => {
                serialize_addresses(OptionCode::NetworkInformationServicePlusServers, &v, buf)
            }
            DhcpOption::MobileIpHomeAgent(v) => {
                serialize_addresses(OptionCode::MobileIpHomeAgent, &v, buf)
            }
            DhcpOption::SmtpServer(v) => serialize_addresses(OptionCode::SmtpServer, &v, buf),
            DhcpOption::Pop3Server(v) => serialize_addresses(OptionCode::Pop3Server, &v, buf),
            DhcpOption::NntpServer(v) => serialize_addresses(OptionCode::NntpServer, &v, buf),
            DhcpOption::DefaultWwwServer(v) => {
                serialize_addresses(OptionCode::DefaultWwwServer, &v, buf)
            }
            DhcpOption::DefaultFingerServer(v) => {
                serialize_addresses(OptionCode::DefaultFingerServer, &v, buf)
            }
            DhcpOption::DefaultIrcServer(v) => {
                serialize_addresses(OptionCode::DefaultIrcServer, &v, buf)
            }
            DhcpOption::StreetTalkServer(v) => {
                serialize_addresses(OptionCode::StreetTalkServer, &v, buf)
            }
            DhcpOption::StreetTalkDirectoryAssistanceServer(v) => {
                serialize_addresses(OptionCode::StreetTalkDirectoryAssistanceServer, &v, buf)
            }
            DhcpOption::RequestedIpAddress(v) => {
                serialize_address(OptionCode::RequestedIpAddress, v, buf)
            }
            DhcpOption::IpAddressLeaseTime(v) => {
                serialize_u32(OptionCode::IpAddressLeaseTime, v, buf)
            }
            DhcpOption::OptionOverload(v) => serialize_enum(OptionCode::OptionOverload, v, buf),
            DhcpOption::TftpServerName(v) => serialize_string(OptionCode::TftpServerName, &v, buf),
            DhcpOption::BootfileName(v) => serialize_string(OptionCode::BootfileName, &v, buf),
            DhcpOption::DhcpMessageType(v) => serialize_enum(OptionCode::DhcpMessageType, v, buf),
            DhcpOption::ServerIdentifier(v) => {
                serialize_address(OptionCode::ServerIdentifier, v, buf)
            }
            DhcpOption::ParameterRequestList(v) => {
                let size = std::mem::size_of_val(&v);
                buf.push(OptionCode::ParameterRequestList.into());
                buf.push(size as u8);
                buf.extend(v.into_iter().map(|code| code as u8));
            }
            DhcpOption::Message(v) => serialize_string(OptionCode::Message, &v, buf),
            DhcpOption::MaxDhcpMessageSize(v) => {
                serialize_u16(OptionCode::MaxDhcpMessageSize, v, buf)
            }
            DhcpOption::RenewalTimeValue(v) => serialize_u32(OptionCode::RenewalTimeValue, v, buf),
            DhcpOption::RebindingTimeValue(v) => {
                serialize_u32(OptionCode::RebindingTimeValue, v, buf)
            }
            DhcpOption::VendorClassIdentifier(v) => {
                serialize_bytes(OptionCode::VendorClassIdentifier, &v, buf)
            }
            DhcpOption::ClientIdentifier(v) => {
                serialize_bytes(OptionCode::ClientIdentifier, &v, buf)
            }
        }
    }

    /// Returns the `OptionCode` variant corresponding to `self`.
    pub fn code(&self) -> OptionCode {
        option_to_code!(
            self,
            DhcpOption::Pad(),
            DhcpOption::End(),
            DhcpOption::SubnetMask(_),
            DhcpOption::TimeOffset(_),
            DhcpOption::Router(_),
            DhcpOption::TimeServer(_),
            DhcpOption::NameServer(_),
            DhcpOption::DomainNameServer(_),
            DhcpOption::LogServer(_),
            DhcpOption::CookieServer(_),
            DhcpOption::LprServer(_),
            DhcpOption::ImpressServer(_),
            DhcpOption::ResourceLocationServer(_),
            DhcpOption::HostName(_),
            DhcpOption::BootFileSize(_),
            DhcpOption::MeritDumpFile(_),
            DhcpOption::DomainName(_),
            DhcpOption::SwapServer(_),
            DhcpOption::RootPath(_),
            DhcpOption::ExtensionsPath(_),
            DhcpOption::IpForwarding(_),
            DhcpOption::NonLocalSourceRouting(_),
            DhcpOption::PolicyFilter(_),
            DhcpOption::MaxDatagramReassemblySize(_),
            DhcpOption::DefaultIpTtl(_),
            DhcpOption::PathMtuAgingTimeout(_),
            DhcpOption::PathMtuPlateauTable(_),
            DhcpOption::InterfaceMtu(_),
            DhcpOption::AllSubnetsLocal(_),
            DhcpOption::BroadcastAddress(_),
            DhcpOption::PerformMaskDiscovery(_),
            DhcpOption::MaskSupplier(_),
            DhcpOption::PerformRouterDiscovery(_),
            DhcpOption::RouterSolicitationAddress(_),
            DhcpOption::StaticRoute(_),
            DhcpOption::TrailerEncapsulation(_),
            DhcpOption::ArpCacheTimeout(_),
            DhcpOption::EthernetEncapsulation(_),
            DhcpOption::TcpDefaultTtl(_),
            DhcpOption::TcpKeepaliveInterval(_),
            DhcpOption::TcpKeepaliveGarbage(_),
            DhcpOption::NetworkInformationServiceDomain(_),
            DhcpOption::NetworkInformationServers(_),
            DhcpOption::NetworkTimeProtocolServers(_),
            DhcpOption::VendorSpecificInformation(_),
            DhcpOption::NetBiosOverTcpipNameServer(_),
            DhcpOption::NetBiosOverTcpipDatagramDistributionServer(_),
            DhcpOption::NetBiosOverTcpipNodeType(_),
            DhcpOption::NetBiosOverTcpipScope(_),
            DhcpOption::XWindowSystemFontServer(_),
            DhcpOption::XWindowSystemDisplayManager(_),
            DhcpOption::NetworkInformationServicePlusDomain(_),
            DhcpOption::NetworkInformationServicePlusServers(_),
            DhcpOption::MobileIpHomeAgent(_),
            DhcpOption::SmtpServer(_),
            DhcpOption::Pop3Server(_),
            DhcpOption::NntpServer(_),
            DhcpOption::DefaultWwwServer(_),
            DhcpOption::DefaultFingerServer(_),
            DhcpOption::DefaultIrcServer(_),
            DhcpOption::StreetTalkServer(_),
            DhcpOption::StreetTalkDirectoryAssistanceServer(_),
            DhcpOption::RequestedIpAddress(_),
            DhcpOption::IpAddressLeaseTime(_),
            DhcpOption::OptionOverload(_),
            DhcpOption::TftpServerName(_),
            DhcpOption::BootfileName(_),
            DhcpOption::DhcpMessageType(_),
            DhcpOption::ServerIdentifier(_),
            DhcpOption::ParameterRequestList(_),
            DhcpOption::Message(_),
            DhcpOption::MaxDhcpMessageSize(_),
            DhcpOption::RenewalTimeValue(_),
            DhcpOption::RebindingTimeValue(_),
            DhcpOption::VendorClassIdentifier(_),
            DhcpOption::ClientIdentifier(_)
        )
    }
}

fn serialize_address(code: OptionCode, addr: Ipv4Addr, buf: &mut Vec<u8>) {
    serialize_addresses(code, &[addr], buf);
}

fn serialize_addresses(code: OptionCode, addrs: &[Ipv4Addr], buf: &mut Vec<u8>) {
    let size = std::mem::size_of_val(addrs);
    buf.push(code.into());
    buf.push(size as u8);
    for addr in addrs {
        buf.extend_from_slice(&addr.octets());
    }
}

fn serialize_string(code: OptionCode, string: &str, buf: &mut Vec<u8>) {
    let size = string.len();
    buf.push(code.into());
    buf.push(size as u8);
    buf.extend_from_slice(string.as_bytes());
}

fn serialize_flag(code: OptionCode, flag: bool, buf: &mut Vec<u8>) {
    let size = std::mem::size_of::<bool>();
    buf.push(code.into());
    buf.push(size as u8);
    buf.push(flag as u8);
}

fn serialize_u16(code: OptionCode, v: u16, buf: &mut Vec<u8>) {
    let size = std::mem::size_of::<u16>();
    buf.push(code.into());
    buf.push(size as u8);
    buf.extend_from_slice(&v.to_be_bytes());
}

fn serialize_u8(code: OptionCode, v: u8, buf: &mut Vec<u8>) {
    let size = std::mem::size_of::<u8>();
    buf.push(code.into());
    buf.push(size as u8);
    buf.push(v);
}

fn serialize_u32(code: OptionCode, v: u32, buf: &mut Vec<u8>) {
    let size = std::mem::size_of::<u32>();
    buf.push(code.into());
    buf.push(size as u8);
    buf.extend_from_slice(&v.to_be_bytes());
}

fn serialize_bytes(code: OptionCode, v: &[u8], buf: &mut Vec<u8>) {
    let size = std::mem::size_of_val(v);
    buf.push(code.into());
    buf.push(size as u8);
    buf.extend_from_slice(v);
}

fn serialize_enum<T: Into<u8>>(code: OptionCode, v: T, buf: &mut Vec<u8>) {
    let size = std::mem::size_of::<T>();
    buf.push(code.into());
    buf.push(size as u8);
    buf.push(v.into());
}

/// A type which can be converted to and from a FIDL type `F`.
// TODO(https://fxbug.dev/42819): Impl FidlCompatible for Iterator<Item: FidlCompatible>
pub trait FidlCompatible<F>: Sized {
    type FromError;
    type IntoError;

    fn try_from_fidl(fidl: F) -> Result<Self, Self::FromError>;
    fn try_into_fidl(self) -> Result<F, Self::IntoError>;
}

/// Utility trait for infallible FIDL conversion.
pub trait FromFidlExt<F>: FidlCompatible<F, FromError = Never> {
    fn from_fidl(fidl: F) -> Self {
        match Self::try_from_fidl(fidl) {
            Ok(slf) => slf,
            Err(err) => match err {},
        }
    }
}
/// Utility trait for infallible FIDL conversion.
pub trait IntoFidlExt<F>: FidlCompatible<F, IntoError = Never> {
    fn into_fidl(self) -> F {
        match self.try_into_fidl() {
            Ok(fidl) => fidl,
            Err(err) => match err {},
        }
    }
}

impl<F, C: FidlCompatible<F, IntoError = Never>> IntoFidlExt<F> for C {}
impl<F, C: FidlCompatible<F, FromError = Never>> FromFidlExt<F> for C {}

impl FidlCompatible<fidl_fuchsia_net::Ipv4Address> for Ipv4Addr {
    type FromError = Never;
    type IntoError = Never;

    fn try_from_fidl(fidl: fidl_fuchsia_net::Ipv4Address) -> Result<Self, Self::FromError> {
        Ok(Ipv4Addr::from(fidl.addr))
    }

    fn try_into_fidl(self) -> Result<fidl_fuchsia_net::Ipv4Address, Self::IntoError> {
        Ok(fidl_fuchsia_net::Ipv4Address { addr: self.octets() })
    }
}

impl FidlCompatible<Vec<fidl_fuchsia_net::Ipv4Address>> for Vec<Ipv4Addr> {
    type FromError = Never;
    type IntoError = Never;

    fn try_from_fidl(fidl: Vec<fidl_fuchsia_net::Ipv4Address>) -> Result<Self, Self::FromError> {
        Ok(fidl
            .into_iter()
            .filter_map(|addr| Ipv4Addr::try_from_fidl(addr).ok())
            .collect::<Vec<Ipv4Addr>>())
    }

    fn try_into_fidl(self) -> Result<Vec<fidl_fuchsia_net::Ipv4Address>, Self::IntoError> {
        Ok(self
            .into_iter()
            .filter_map(|addr| addr.try_into_fidl().ok())
            .collect::<Vec<fidl_fuchsia_net::Ipv4Address>>())
    }
}

// TODO(atait): Consider using a macro to reduce/eliminate the boilerplate in these implementations.
impl FidlCompatible<fidl_fuchsia_net_dhcp::Option_> for DhcpOption {
    type FromError = ProtocolError;
    type IntoError = ProtocolError;

    fn try_into_fidl(self) -> Result<fidl_fuchsia_net_dhcp::Option_, Self::IntoError> {
        match self {
            DhcpOption::Pad() => Err(Self::IntoError::InvalidFidlOption(self)),
            DhcpOption::End() => Err(Self::IntoError::InvalidFidlOption(self)),
            DhcpOption::SubnetMask(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::SubnetMask(v.into_fidl()))
            }
            DhcpOption::TimeOffset(v) => Ok(fidl_fuchsia_net_dhcp::Option_::TimeOffset(v)),
            DhcpOption::Router(v) => Ok(fidl_fuchsia_net_dhcp::Option_::Router(v.into_fidl())),
            DhcpOption::TimeServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::TimeServer(v.into_fidl()))
            }
            DhcpOption::NameServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NameServer(v.into_fidl()))
            }
            DhcpOption::DomainNameServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::DomainNameServer(v.into_fidl()))
            }
            DhcpOption::LogServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::LogServer(v.into_fidl()))
            }
            DhcpOption::CookieServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::CookieServer(v.into_fidl()))
            }
            DhcpOption::LprServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::LprServer(v.into_fidl()))
            }
            DhcpOption::ImpressServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::ImpressServer(v.into_fidl()))
            }
            DhcpOption::ResourceLocationServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::ResourceLocationServer(v.into_fidl()))
            }
            DhcpOption::HostName(v) => Ok(fidl_fuchsia_net_dhcp::Option_::HostName(v)),
            DhcpOption::BootFileSize(v) => Ok(fidl_fuchsia_net_dhcp::Option_::BootFileSize(v)),
            DhcpOption::MeritDumpFile(v) => Ok(fidl_fuchsia_net_dhcp::Option_::MeritDumpFile(v)),
            DhcpOption::DomainName(v) => Ok(fidl_fuchsia_net_dhcp::Option_::DomainName(v)),
            DhcpOption::SwapServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::SwapServer(v.into_fidl()))
            }
            DhcpOption::RootPath(v) => Ok(fidl_fuchsia_net_dhcp::Option_::RootPath(v)),
            DhcpOption::ExtensionsPath(v) => Ok(fidl_fuchsia_net_dhcp::Option_::ExtensionsPath(v)),
            DhcpOption::IpForwarding(v) => Ok(fidl_fuchsia_net_dhcp::Option_::IpForwarding(v)),
            DhcpOption::NonLocalSourceRouting(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NonLocalSourceRouting(v))
            }
            DhcpOption::PolicyFilter(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::PolicyFilter(v.into_fidl()))
            }
            DhcpOption::MaxDatagramReassemblySize(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::MaxDatagramReassemblySize(v))
            }
            DhcpOption::DefaultIpTtl(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::DefaultIpTtl(v.into()))
            }
            DhcpOption::PathMtuAgingTimeout(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::PathMtuAgingTimeout(v))
            }
            DhcpOption::PathMtuPlateauTable(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::PathMtuPlateauTable(v))
            }
            DhcpOption::InterfaceMtu(v) => Ok(fidl_fuchsia_net_dhcp::Option_::InterfaceMtu(v)),
            DhcpOption::AllSubnetsLocal(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::AllSubnetsLocal(v))
            }
            DhcpOption::BroadcastAddress(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::BroadcastAddress(v.into_fidl()))
            }
            DhcpOption::PerformMaskDiscovery(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::PerformMaskDiscovery(v))
            }
            DhcpOption::MaskSupplier(v) => Ok(fidl_fuchsia_net_dhcp::Option_::MaskSupplier(v)),
            DhcpOption::PerformRouterDiscovery(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::PerformRouterDiscovery(v))
            }
            DhcpOption::RouterSolicitationAddress(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::RouterSolicitationAddress(v.into_fidl()))
            }
            DhcpOption::StaticRoute(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::StaticRoute(v.into_fidl()))
            }
            DhcpOption::TrailerEncapsulation(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::TrailerEncapsulation(v))
            }
            DhcpOption::ArpCacheTimeout(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::ArpCacheTimeout(v))
            }
            DhcpOption::EthernetEncapsulation(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::EthernetEncapsulation(v))
            }
            DhcpOption::TcpDefaultTtl(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::TcpDefaultTtl(v.into()))
            }
            DhcpOption::TcpKeepaliveInterval(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::TcpKeepaliveInterval(v))
            }
            DhcpOption::TcpKeepaliveGarbage(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::TcpKeepaliveGarbage(v))
            }
            DhcpOption::NetworkInformationServiceDomain(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetworkInformationServiceDomain(v))
            }
            DhcpOption::NetworkInformationServers(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetworkInformationServers(v.into_fidl()))
            }
            DhcpOption::NetworkTimeProtocolServers(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetworkTimeProtocolServers(v.into_fidl()))
            }
            DhcpOption::VendorSpecificInformation(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::VendorSpecificInformation(v))
            }
            DhcpOption::NetBiosOverTcpipNameServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipNameServer(v.into_fidl()))
            }
            DhcpOption::NetBiosOverTcpipDatagramDistributionServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipDatagramDistributionServer(
                    v.into_fidl(),
                ))
            }
            DhcpOption::NetBiosOverTcpipNodeType(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipNodeType(v.into_fidl()))
            }
            DhcpOption::NetBiosOverTcpipScope(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipScope(v))
            }
            DhcpOption::XWindowSystemFontServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::XWindowSystemFontServer(v.into_fidl()))
            }
            DhcpOption::XWindowSystemDisplayManager(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::XWindowSystemDisplayManager(v.into_fidl()))
            }
            DhcpOption::NetworkInformationServicePlusDomain(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NetworkInformationServicePlusDomain(v))
            }
            DhcpOption::NetworkInformationServicePlusServers(v) => Ok(
                fidl_fuchsia_net_dhcp::Option_::NetworkInformationServicePlusServers(v.into_fidl()),
            ),
            DhcpOption::MobileIpHomeAgent(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::MobileIpHomeAgent(v.into_fidl()))
            }
            DhcpOption::SmtpServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::SmtpServer(v.into_fidl()))
            }
            DhcpOption::Pop3Server(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::Pop3Server(v.into_fidl()))
            }
            DhcpOption::NntpServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::NntpServer(v.into_fidl()))
            }
            DhcpOption::DefaultWwwServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::DefaultWwwServer(v.into_fidl()))
            }
            DhcpOption::DefaultFingerServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::DefaultFingerServer(v.into_fidl()))
            }
            DhcpOption::DefaultIrcServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::DefaultIrcServer(v.into_fidl()))
            }
            DhcpOption::StreetTalkServer(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::StreettalkServer(v.into_fidl()))
            }
            DhcpOption::StreetTalkDirectoryAssistanceServer(v) => Ok(
                fidl_fuchsia_net_dhcp::Option_::StreettalkDirectoryAssistanceServer(v.into_fidl()),
            ),
            DhcpOption::RequestedIpAddress(_) => Err(ProtocolError::InvalidFidlOption(self)),
            DhcpOption::IpAddressLeaseTime(_) => Err(ProtocolError::InvalidFidlOption(self)),
            DhcpOption::OptionOverload(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::OptionOverload(v.into_fidl()))
            }
            DhcpOption::TftpServerName(v) => Ok(fidl_fuchsia_net_dhcp::Option_::TftpServerName(v)),
            DhcpOption::BootfileName(v) => Ok(fidl_fuchsia_net_dhcp::Option_::BootfileName(v)),
            DhcpOption::DhcpMessageType(_) => Err(ProtocolError::InvalidFidlOption(self)),
            DhcpOption::ServerIdentifier(_) => Err(ProtocolError::InvalidFidlOption(self)),
            DhcpOption::ParameterRequestList(_) => Err(ProtocolError::InvalidFidlOption(self)),
            DhcpOption::Message(_) => Err(ProtocolError::InvalidFidlOption(self)),
            DhcpOption::MaxDhcpMessageSize(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::MaxDhcpMessageSize(v))
            }
            DhcpOption::RenewalTimeValue(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::RenewalTimeValue(v))
            }
            DhcpOption::RebindingTimeValue(v) => {
                Ok(fidl_fuchsia_net_dhcp::Option_::RebindingTimeValue(v))
            }
            DhcpOption::VendorClassIdentifier(_) => Err(ProtocolError::InvalidFidlOption(self)),
            DhcpOption::ClientIdentifier(_) => Err(ProtocolError::InvalidFidlOption(self)),
        }
    }

    fn try_from_fidl(v: fidl_fuchsia_net_dhcp::Option_) -> Result<Self, Self::FromError> {
        match v {
            fidl_fuchsia_net_dhcp::Option_::SubnetMask(v) => {
                Ok(DhcpOption::SubnetMask(Ipv4Addr::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::TimeOffset(v) => Ok(DhcpOption::TimeOffset(v)),
            fidl_fuchsia_net_dhcp::Option_::Router(v) => {
                Ok(DhcpOption::Router(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::TimeServer(v) => {
                Ok(DhcpOption::TimeServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::NameServer(v) => {
                Ok(DhcpOption::NameServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::DomainNameServer(v) => {
                Ok(DhcpOption::DomainNameServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::LogServer(v) => {
                Ok(DhcpOption::LogServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::CookieServer(v) => {
                Ok(DhcpOption::CookieServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::LprServer(v) => {
                Ok(DhcpOption::LprServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::ImpressServer(v) => {
                Ok(DhcpOption::ImpressServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::ResourceLocationServer(v) => {
                Ok(DhcpOption::ResourceLocationServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::HostName(v) => Ok(DhcpOption::HostName(v)),
            fidl_fuchsia_net_dhcp::Option_::BootFileSize(v) => Ok(DhcpOption::BootFileSize(v)),
            fidl_fuchsia_net_dhcp::Option_::MeritDumpFile(v) => Ok(DhcpOption::MeritDumpFile(v)),
            fidl_fuchsia_net_dhcp::Option_::DomainName(v) => Ok(DhcpOption::DomainName(v)),
            fidl_fuchsia_net_dhcp::Option_::SwapServer(v) => {
                Ok(DhcpOption::SwapServer(Ipv4Addr::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::RootPath(v) => Ok(DhcpOption::RootPath(v)),
            fidl_fuchsia_net_dhcp::Option_::ExtensionsPath(v) => Ok(DhcpOption::ExtensionsPath(v)),
            fidl_fuchsia_net_dhcp::Option_::IpForwarding(v) => Ok(DhcpOption::IpForwarding(v)),
            fidl_fuchsia_net_dhcp::Option_::NonLocalSourceRouting(v) => {
                Ok(DhcpOption::NonLocalSourceRouting(v))
            }
            fidl_fuchsia_net_dhcp::Option_::PolicyFilter(v) => {
                Ok(DhcpOption::PolicyFilter(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::MaxDatagramReassemblySize(v) => {
                Ok(DhcpOption::MaxDatagramReassemblySize(v))
            }
            fidl_fuchsia_net_dhcp::Option_::DefaultIpTtl(v) => {
                let ttl = NonZeroU8::new(v)
                    .ok_or(ProtocolError::InvalidOptionValue(OptionCode::DefaultIpTtl, vec![v]))?;
                Ok(DhcpOption::DefaultIpTtl(ttl))
            }
            fidl_fuchsia_net_dhcp::Option_::PathMtuAgingTimeout(v) => {
                Ok(DhcpOption::PathMtuAgingTimeout(v))
            }
            fidl_fuchsia_net_dhcp::Option_::PathMtuPlateauTable(v) => {
                Ok(DhcpOption::PathMtuPlateauTable(v))
            }
            fidl_fuchsia_net_dhcp::Option_::InterfaceMtu(v) => Ok(DhcpOption::InterfaceMtu(v)),
            fidl_fuchsia_net_dhcp::Option_::AllSubnetsLocal(v) => {
                Ok(DhcpOption::AllSubnetsLocal(v))
            }
            fidl_fuchsia_net_dhcp::Option_::BroadcastAddress(v) => {
                Ok(DhcpOption::BroadcastAddress(Ipv4Addr::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::PerformMaskDiscovery(v) => {
                Ok(DhcpOption::PerformMaskDiscovery(v))
            }
            fidl_fuchsia_net_dhcp::Option_::MaskSupplier(v) => Ok(DhcpOption::MaskSupplier(v)),
            fidl_fuchsia_net_dhcp::Option_::PerformRouterDiscovery(v) => {
                Ok(DhcpOption::PerformRouterDiscovery(v))
            }
            fidl_fuchsia_net_dhcp::Option_::RouterSolicitationAddress(v) => {
                Ok(DhcpOption::RouterSolicitationAddress(Ipv4Addr::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::StaticRoute(v) => {
                Ok(DhcpOption::StaticRoute(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::TrailerEncapsulation(v) => {
                Ok(DhcpOption::TrailerEncapsulation(v))
            }
            fidl_fuchsia_net_dhcp::Option_::ArpCacheTimeout(v) => {
                Ok(DhcpOption::ArpCacheTimeout(v))
            }
            fidl_fuchsia_net_dhcp::Option_::EthernetEncapsulation(v) => {
                Ok(DhcpOption::EthernetEncapsulation(v))
            }
            fidl_fuchsia_net_dhcp::Option_::TcpDefaultTtl(v) => {
                let ttl = NonZeroU8::new(v)
                    .ok_or(ProtocolError::InvalidOptionValue(OptionCode::TcpDefaultTtl, vec![v]))?;
                Ok(DhcpOption::TcpDefaultTtl(ttl))
            }
            fidl_fuchsia_net_dhcp::Option_::TcpKeepaliveInterval(v) => {
                Ok(DhcpOption::TcpKeepaliveInterval(v))
            }
            fidl_fuchsia_net_dhcp::Option_::TcpKeepaliveGarbage(v) => {
                Ok(DhcpOption::TcpKeepaliveGarbage(v))
            }
            fidl_fuchsia_net_dhcp::Option_::NetworkInformationServiceDomain(v) => {
                Ok(DhcpOption::NetworkInformationServiceDomain(v))
            }
            fidl_fuchsia_net_dhcp::Option_::NetworkInformationServers(v) => {
                Ok(DhcpOption::NetworkInformationServers(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::NetworkTimeProtocolServers(v) => {
                Ok(DhcpOption::NetworkTimeProtocolServers(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::VendorSpecificInformation(v) => {
                Ok(DhcpOption::VendorSpecificInformation(v))
            }
            fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipNameServer(v) => {
                Ok(DhcpOption::NetBiosOverTcpipNameServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipDatagramDistributionServer(v) => {
                Ok(DhcpOption::NetBiosOverTcpipDatagramDistributionServer(
                    Vec::<Ipv4Addr>::from_fidl(v),
                ))
            }
            fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipNodeType(v) => {
                Ok(DhcpOption::NetBiosOverTcpipNodeType(NodeType::try_from_fidl(v)?))
            }
            fidl_fuchsia_net_dhcp::Option_::NetbiosOverTcpipScope(v) => {
                Ok(DhcpOption::NetBiosOverTcpipScope(v))
            }
            fidl_fuchsia_net_dhcp::Option_::XWindowSystemFontServer(v) => {
                Ok(DhcpOption::XWindowSystemFontServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::XWindowSystemDisplayManager(v) => {
                Ok(DhcpOption::XWindowSystemDisplayManager(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::NetworkInformationServicePlusDomain(v) => {
                Ok(DhcpOption::NetworkInformationServicePlusDomain(v))
            }
            fidl_fuchsia_net_dhcp::Option_::NetworkInformationServicePlusServers(v) => {
                Ok(DhcpOption::NetworkInformationServicePlusServers(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::MobileIpHomeAgent(v) => {
                Ok(DhcpOption::MobileIpHomeAgent(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::SmtpServer(v) => {
                Ok(DhcpOption::SmtpServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::Pop3Server(v) => {
                Ok(DhcpOption::Pop3Server(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::NntpServer(v) => {
                Ok(DhcpOption::NntpServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::DefaultWwwServer(v) => {
                Ok(DhcpOption::DefaultWwwServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::DefaultFingerServer(v) => {
                Ok(DhcpOption::DefaultFingerServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::DefaultIrcServer(v) => {
                Ok(DhcpOption::DefaultIrcServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::StreettalkServer(v) => {
                Ok(DhcpOption::StreetTalkServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::StreettalkDirectoryAssistanceServer(v) => {
                Ok(DhcpOption::StreetTalkDirectoryAssistanceServer(Vec::<Ipv4Addr>::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::OptionOverload(v) => {
                Ok(DhcpOption::OptionOverload(Overload::from_fidl(v)))
            }
            fidl_fuchsia_net_dhcp::Option_::TftpServerName(v) => Ok(DhcpOption::TftpServerName(v)),
            fidl_fuchsia_net_dhcp::Option_::BootfileName(v) => Ok(DhcpOption::BootfileName(v)),
            fidl_fuchsia_net_dhcp::Option_::MaxDhcpMessageSize(v) => {
                Ok(DhcpOption::MaxDhcpMessageSize(v))
            }
            fidl_fuchsia_net_dhcp::Option_::RenewalTimeValue(v) => {
                Ok(DhcpOption::RenewalTimeValue(v))
            }
            fidl_fuchsia_net_dhcp::Option_::RebindingTimeValue(v) => {
                Ok(DhcpOption::RebindingTimeValue(v))
            }
            fidl_fuchsia_net_dhcp::Option_Unknown!() => Err(ProtocolError::UnknownFidlOption),
        }
    }
}

/// A NetBIOS over TCP/IP Node Type.
///
/// This enum and the values of its variants corresponds to the DHCP option defined
/// in: https://tools.ietf.org/html/rfc2132#section-8.7
#[derive(Clone, Copy, Debug, Deserialize, Eq, FromPrimitive, Hash, PartialEq, Serialize)]
#[repr(u8)]
pub enum NodeType {
    BNode = 0x1,
    PNode = 0x2,
    MNode = 0x4,
    HNode = 0x8,
}

impl TryFrom<u8> for NodeType {
    type Error = ProtocolError;

    fn try_from(n: u8) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u8(n)
            .ok_or(ProtocolError::InvalidOptionValue(OptionCode::NetBiosOverTcpipNodeType, vec![n]))
    }
}

impl Into<u8> for NodeType {
    fn into(self) -> u8 {
        self as u8
    }
}

impl FidlCompatible<fidl_fuchsia_net_dhcp::NodeTypes> for NodeType {
    type FromError = ProtocolError;
    type IntoError = Never;

    fn try_from_fidl(fidl: fidl_fuchsia_net_dhcp::NodeTypes) -> Result<NodeType, Self::FromError> {
        match fidl {
            fidl_fuchsia_net_dhcp::NodeTypes::B_NODE => Ok(NodeType::BNode),
            fidl_fuchsia_net_dhcp::NodeTypes::P_NODE => Ok(NodeType::PNode),
            fidl_fuchsia_net_dhcp::NodeTypes::M_NODE => Ok(NodeType::MNode),
            fidl_fuchsia_net_dhcp::NodeTypes::H_NODE => Ok(NodeType::HNode),
            other => Err(ProtocolError::InvalidOptionValue(
                OptionCode::NetBiosOverTcpipNodeType,
                vec![other.bits()],
            )),
        }
    }

    fn try_into_fidl(self) -> Result<fidl_fuchsia_net_dhcp::NodeTypes, Self::IntoError> {
        match self {
            NodeType::BNode => Ok(fidl_fuchsia_net_dhcp::NodeTypes::B_NODE),
            NodeType::PNode => Ok(fidl_fuchsia_net_dhcp::NodeTypes::P_NODE),
            NodeType::MNode => Ok(fidl_fuchsia_net_dhcp::NodeTypes::M_NODE),
            NodeType::HNode => Ok(fidl_fuchsia_net_dhcp::NodeTypes::H_NODE),
        }
    }
}

/// The DHCP message fields to use for storing additional options.
///
/// A DHCP client can indicate that it wants to use the File or SName fields of
/// the DHCP header to store DHCP options. This enum and its variant values correspond
/// to the DHCP option defined in: https://tools.ietf.org/html/rfc2132#section-9.3
#[derive(Clone, Copy, Debug, Deserialize, Eq, FromPrimitive, Hash, PartialEq, Serialize)]
#[repr(u8)]
pub enum Overload {
    File = 1,
    SName = 2,
    Both = 3,
}

impl Into<u8> for Overload {
    fn into(self) -> u8 {
        self as u8
    }
}

impl TryFrom<u8> for Overload {
    type Error = ProtocolError;

    fn try_from(n: u8) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u8(n)
            .ok_or(ProtocolError::InvalidOptionValue(OptionCode::OptionOverload, vec![n]))
    }
}

impl FidlCompatible<fidl_fuchsia_net_dhcp::OptionOverloadValue> for Overload {
    type FromError = Never;
    type IntoError = Never;

    fn try_from_fidl(
        fidl: fidl_fuchsia_net_dhcp::OptionOverloadValue,
    ) -> Result<Self, Self::FromError> {
        match fidl {
            fidl_fuchsia_net_dhcp::OptionOverloadValue::File => Ok(Overload::File),
            fidl_fuchsia_net_dhcp::OptionOverloadValue::Sname => Ok(Overload::SName),
            fidl_fuchsia_net_dhcp::OptionOverloadValue::Both => Ok(Overload::Both),
        }
    }

    fn try_into_fidl(self) -> Result<fidl_fuchsia_net_dhcp::OptionOverloadValue, Self::IntoError> {
        match self {
            Overload::File => Ok(fidl_fuchsia_net_dhcp::OptionOverloadValue::File),
            Overload::SName => Ok(fidl_fuchsia_net_dhcp::OptionOverloadValue::Sname),
            Overload::Both => Ok(fidl_fuchsia_net_dhcp::OptionOverloadValue::Both),
        }
    }
}

/// A DHCP Message Type.
///
/// This enum corresponds to the DHCP Message Type option values
/// defined in section 9.4 of RFC 1533.
#[derive(FromPrimitive, Copy, Clone, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
#[repr(u8)]
pub enum MessageType {
    DHCPDISCOVER = 1,
    DHCPOFFER = 2,
    DHCPREQUEST = 3,
    DHCPDECLINE = 4,
    DHCPACK = 5,
    DHCPNAK = 6,
    DHCPRELEASE = 7,
    DHCPINFORM = 8,
}

impl Into<u8> for MessageType {
    fn into(self) -> u8 {
        self as u8
    }
}

/// Instead of reusing the implementation of `Debug::fmt` here, a cleaner way
/// is to derive the 'Display' trait for enums using `enum-display-derive` crate
///
/// https://docs.rs/enum-display-derive/0.1.0/enum_display_derive/
///
/// Since addition of this in third_party/rust_crates needs OSRB approval
/// it should be done if there is a stronger need for more complex enums.
impl fmt::Display for MessageType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&self, f)
    }
}

impl TryFrom<u8> for MessageType {
    type Error = ProtocolError;

    fn try_from(n: u8) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u8(n).ok_or(ProtocolError::InvalidMessageType(n))
    }
}

/// Parses DHCP options from `buf` into `options`.
fn parse_options<T: Extend<DhcpOption>>(
    mut buf: &[u8],
    mut options: T,
) -> Result<T, ProtocolError> {
    loop {
        let (raw_opt_code, rest) = buf.split_first().ok_or_else(|| {
            // From RFC 2131 Section 4.1:
            //   The last option must always be the 'end' option.
            ProtocolError::MissingOption(OptionCode::End)
        })?;
        buf = rest;
        match OptionCode::try_from(*raw_opt_code) {
            Ok(OptionCode::End) => {
                // End of options reached.
                return Ok(options);
            }
            Ok(OptionCode::Pad) => {}
            code => {
                let (&opt_len, rest) =
                    buf.split_first().ok_or_else(|| ProtocolError::MalformedOption {
                        code: *raw_opt_code,
                        remaining: buf.len(),
                        want: 1,
                    })?;
                buf = rest;
                let opt_len = opt_len as usize;

                // Reaching the end of the buffer means we never encountered an End code.
                if buf.len() < opt_len {
                    return Err(ProtocolError::MalformedOption {
                        code: *raw_opt_code,
                        remaining: buf.len(),
                        want: opt_len,
                    });
                };
                let (val, rest) = buf.split_at(opt_len);
                buf = rest;

                // Ignore unknown option codes, hinted at in RFC 2131 section 3.5:
                //   ... Other options representing "hints" at configuration parameters are allowed
                //   in a DHCPDISCOVER or DHCPREQUEST message.  However, additional options may be
                //   ignored by servers...
                let code = match code {
                    Ok(c) => c,
                    Err(ProtocolError::InvalidOptionCode(_)) => continue,
                    Err(e) => return Err(e),
                };

                match DhcpOption::from_raw_parts(code, val) {
                    Ok(option) => options.extend(std::iter::once(option)),
                    Err(e) => {
                        // RFC 2131 does not define how to handle invalid options.
                        // In order to match prior art, like ISC and dnsmasq, we strive to
                        // be lenient. We throw out as little as necessary and will allow a packet
                        // even if a subset of the options are invalid.
                        //
                        // For example, here is a case where ISC will use the first 4 bytes of an
                        // IPv4 Address even if the option had > 4 length.
                        // https://github.com/isc-projects/dhcp/blob/31e68e5/server/dhcp.c#L947-L959
                        warn!("Error while parsing Option: {}", e);
                        continue;
                    }
                }
            }
        }
    }
}

// Ensures slice is non empty. InvalidBufferLength is returned for empty slices.
fn nonempty<T>(slice: &[T]) -> Result<&[T], InvalidBufferLengthError> {
    if slice.len() == 0 {
        return Err(InvalidBufferLengthError(slice.len()));
    }
    Ok(slice)
}

fn get_byte_array<const T: usize>(bytes: &[u8]) -> Result<[u8; T], InvalidBufferLengthError> {
    bytes
        .try_into()
        .map_err(|std::array::TryFromSliceError { .. }| InvalidBufferLengthError(bytes.len()))
}

// Converts input byte slice into a single byte.
fn get_byte(bytes: &[u8]) -> Result<u8, InvalidBufferLengthError> {
    match bytes {
        [b] => Ok(*b),
        bytes => Err(InvalidBufferLengthError(bytes.len())),
    }
}

// Converts input byte slice into a nonempty utf8 string.
fn bytes_to_nonempty_str(bytes: &[u8]) -> Result<String, ProtocolError> {
    // Spec states that strings should not be null terminated, yet receivers
    // should be prepared to trim trailing nulls.
    // See https://datatracker.ietf.org/doc/html/rfc2132#section-2
    std::str::from_utf8(nonempty(bytes)?.into())
        .map_err(|e| ProtocolError::Utf8(e.valid_up_to()))
        .map(|string| string.trim_end_matches(ASCII_NULL).to_owned())
}

// Converts input byte slice into an Ipv4Addr.
fn bytes_to_addr(bytes: &[u8]) -> Result<Ipv4Addr, InvalidBufferLengthError> {
    Ok(Ipv4Addr::from(get_byte_array::<4>(bytes)?))
}

// Converts a nonempty input byte slice into a list of Ipv4Addr.
fn bytes_to_addrs(bytes: &[u8]) -> Result<Vec<Ipv4Addr>, InvalidBufferLengthError> {
    nonempty(bytes)?
        .chunks(IPV4_ADDR_LEN)
        .map(bytes_to_addr)
        .collect::<Result<Vec<Ipv4Addr>, InvalidBufferLengthError>>()
        .map_err(|InvalidBufferLengthError(_)| InvalidBufferLengthError(bytes.len()))
}

#[derive(Debug, Error, PartialEq)]
enum BooleanConversionError {
    #[error("invalid buffer length: {}", _0)]
    InvalidBufferLength(usize),
    #[error("invalid value: {}", _0)]
    InvalidValue(u8),
}

impl BooleanConversionError {
    fn to_protocol(&self, code: OptionCode) -> ProtocolError {
        match self {
            Self::InvalidBufferLength(len) => ProtocolError::InvalidBufferLength(*len),
            Self::InvalidValue(val) => ProtocolError::InvalidOptionValue(code, vec![*val]),
        }
    }
}

// Returns a bool from a nonempty byte slice.
fn bytes_to_bool(bytes: &[u8]) -> Result<bool, BooleanConversionError> {
    let byte = get_byte(bytes)
        .map_err(|InvalidBufferLengthError(n)| BooleanConversionError::InvalidBufferLength(n))?;
    match byte {
        0 | 1 => Ok(byte == 1),
        b => Err(BooleanConversionError::InvalidValue(b)),
    }
}

// Returns an Ipv4Addr when given a byte buffer in network order whose len >= start + 4.
pub fn ip_addr_from_buf_at(buf: &[u8], start: usize) -> Result<Ipv4Addr, ProtocolError> {
    let buf = buf.get(start..start + 4).ok_or(ProtocolError::InvalidBufferLength(buf.len()))?;
    let buf: [u8; 4] = buf.try_into().map_err(|std::array::TryFromSliceError { .. }| {
        ProtocolError::InvalidBufferLength(buf.len())
    })?;
    Ok(buf.into())
}

fn buf_to_msg_string(buf: &[u8]) -> Result<String, ProtocolError> {
    Ok(std::str::from_utf8(buf)
        .map_err(|e| ProtocolError::Utf8(e.valid_up_to()))?
        .trim_end_matches(ASCII_NULL)
        .to_string())
}

fn trunc_string_to_n_and_push(s: &str, n: usize, buffer: &mut Vec<u8>) {
    if s.len() > n {
        let truncated = s.split_at(n);
        buffer.extend(truncated.0.as_bytes());
        return;
    }
    buffer.extend(s.as_bytes());
    let unused_bytes = n - s.len();
    let old_len = buffer.len();
    buffer.resize(old_len + unused_bytes, 0);
}

#[cfg(test)]
mod tests {

    use super::identifier::ClientIdentifier;
    use super::*;
    use net_declare::std::ip_v4;
    use std::net::Ipv4Addr;
    use std::str::FromStr;
    use test_case::test_case;

    const DEFAULT_SUBNET_MASK: Ipv4Addr = ip_v4!("255.255.255.0");

    fn new_test_msg() -> Message {
        Message {
            op: OpCode::BOOTREQUEST,
            xid: 42,
            secs: 1024,
            bdcast_flag: false,
            ciaddr: Ipv4Addr::UNSPECIFIED,
            yiaddr: ip_v4!("192.168.1.1"),
            siaddr: Ipv4Addr::UNSPECIFIED,
            giaddr: Ipv4Addr::UNSPECIFIED,
            chaddr: MacAddr::new([0; 6]),
            sname: String::from("relay.example.com"),
            file: String::from("boot.img"),
            options: Vec::new(),
        }
    }

    #[test]
    fn serialize_returns_correct_bytes() {
        let mut msg = new_test_msg();
        msg.options.push(DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK));

        let bytes = msg.serialize();

        assert_eq!(bytes.len(), 247);
        assert_eq!(bytes[0], 1u8);
        assert_eq!(bytes[1], 1u8);
        assert_eq!(bytes[2], 6u8);
        assert_eq!(bytes[3], 0u8);
        assert_eq!(bytes[7], 42u8);
        assert_eq!(bytes[8], 4u8);
        assert_eq!(bytes[16], 192u8);
        assert_eq!(bytes[17], 168u8);
        assert_eq!(bytes[18], 1u8);
        assert_eq!(bytes[19], 1u8);
        assert_eq!(bytes[44], 'r' as u8);
        assert_eq!(bytes[60], 'm' as u8);
        assert_eq!(bytes[61], 0u8);
        assert_eq!(bytes[108], 'b' as u8);
        assert_eq!(bytes[115], 'g' as u8);
        assert_eq!(bytes[116], 0u8);
        assert_eq!(bytes[OPTIONS_START_IDX..OPTIONS_START_IDX + MAGIC_COOKIE.len()], MAGIC_COOKIE);
        assert_eq!(bytes[bytes.len() - 1], 255u8);
    }

    #[test]
    fn message_from_buffer_returns_correct_message() {
        use std::string::ToString;

        let mut buf = Vec::new();
        buf.push(1u8);
        buf.push(1u8);
        buf.push(6u8);
        buf.push(0u8);
        buf.extend_from_slice(b"\x00\x00\x00\x2A");
        buf.extend_from_slice(b"\x04\x00");
        buf.extend_from_slice(b"\x00\x00");
        buf.extend_from_slice(b"\x00\x00\x00\x00");
        buf.extend_from_slice(b"\xC0\xA8\x01\x01");
        buf.extend_from_slice(b"\x00\x00\x00\x00");
        buf.extend_from_slice(b"\x00\x00\x00\x00");
        buf.extend_from_slice(b"\x00\x00\x00\x00\x00\x00");
        buf.extend_from_slice(b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00");
        buf.extend_from_slice(b"relay.example.com");
        let mut old_len = buf.len();
        let mut unused_bytes = SNAME_LEN - b"relay.example.com".len();
        buf.resize(old_len + unused_bytes, 0u8);
        buf.extend_from_slice(b"boot.img");
        old_len = buf.len();
        unused_bytes = FILE_LEN - b"boot.img".len();
        buf.resize(old_len + unused_bytes, 0u8);
        buf.extend_from_slice(&MAGIC_COOKIE);
        buf.extend_from_slice(b"\x01\x04");
        buf.extend_from_slice(&DEFAULT_SUBNET_MASK.octets()[..]);
        buf.extend_from_slice(b"\x00");
        buf.extend_from_slice(b"\x00");
        buf.extend_from_slice(b"\x36\x04");
        let server_id = ip_v4!("1.2.3.4");
        buf.extend_from_slice(&server_id.octets()[..]);
        buf.extend_from_slice(b"\xFF");

        assert_eq!(
            Message::from_buffer(&buf),
            Ok(Message {
                op: OpCode::BOOTREQUEST,
                xid: 42,
                secs: 1024,
                bdcast_flag: false,
                ciaddr: Ipv4Addr::UNSPECIFIED,
                yiaddr: ip_v4!("192.168.1.1"),
                siaddr: Ipv4Addr::UNSPECIFIED,
                giaddr: Ipv4Addr::UNSPECIFIED,
                chaddr: MacAddr::new([0; 6]),
                sname: "relay.example.com".to_string(),
                file: "boot.img".to_string(),
                options: vec![
                    DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK),
                    DhcpOption::ServerIdentifier(server_id),
                ],
            })
        );
    }

    #[test]
    fn serialize_then_deserialize_with_single_option_is_equal_to_starting_value() {
        let msg = || {
            let mut msg = new_test_msg();
            msg.options.push(DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK));
            msg
        };

        assert_eq!(Message::from_buffer(&msg().serialize()), Ok(msg()));
    }

    #[test]
    fn serialize_then_deserialize_with_no_options_is_equal_to_starting_value() {
        let msg = new_test_msg();

        assert_eq!(Message::from_buffer(&msg.serialize()), Ok(new_test_msg()));
    }

    #[test]
    fn serialize_then_deserialize_with_many_options_is_equal_to_starting_value() {
        let msg = || {
            let mut msg = new_test_msg();
            msg.options.push(DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK));
            msg.options.push(DhcpOption::NameServer(vec![ip_v4!("1.2.3.4")]));
            msg.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPDISCOVER));
            msg
        };

        assert_eq!(Message::from_buffer(&msg().serialize()), Ok(msg()));
    }

    #[test]
    fn strips_null_from_option_strings() {
        let ascii_str = String::from("Hello World");
        let null_terminated_str = format!("{}{}", ascii_str, ASCII_NULL);
        let msg = Message {
            options: vec![DhcpOption::MeritDumpFile(null_terminated_str)],
            ..new_test_msg()
        };
        let Message { options: parsed_options, .. } = Message::from_buffer(&msg.serialize())
            .expect("Parsing serialized message should succeed.");
        assert_eq!(parsed_options, vec![DhcpOption::MeritDumpFile(ascii_str)]);
    }

    #[test]
    fn message_from_too_short_buffer_returns_error() {
        let buf = vec![0u8, 0u8, 0u8];

        assert_eq!(
            Message::from_buffer(&buf),
            Err(ProtocolError::InvalidBufferLength(buf.len()).into())
        );
    }

    #[test]
    fn serialize_with_valid_option_returns_correct_bytes() {
        let opt = DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK);
        let mut bytes = Vec::with_capacity(6);
        let () = opt.serialize_to(&mut bytes);
        assert_eq!(bytes.len(), 6);
        assert_eq!(bytes[0], 1);
        assert_eq!(bytes[1], 4);
        assert_eq!(bytes[2], 255);
        assert_eq!(bytes[3], 255);
        assert_eq!(bytes[4], 255);
        assert_eq!(bytes[5], 0);
    }

    #[test]
    fn serialize_with_fixed_len_option_returns_correct_bytes() {
        let opt = DhcpOption::End();
        let mut bytes = Vec::with_capacity(1);
        let () = opt.serialize_to(&mut bytes);
        assert_eq!(bytes.len(), 1);
        assert_eq!(bytes[0], 255);
    }

    #[test]
    fn option_from_valid_buffer_has_correct_value() {
        let buf = vec![1, 4, 255, 255, 255, 0, 255];
        assert_eq!(
            parse_options(&buf[..], Vec::new()),
            Ok(vec![DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK)])
        );
    }

    #[test]
    fn option_from_valid_buffer_with_fixed_length_returns_empty_options() {
        let buf = vec![255];
        assert_eq!(parse_options(&buf[..], Vec::new()), Ok(Vec::new()));
    }

    #[test]
    fn option_from_valid_buffer_ignores_unknown_opcodes() {
        let buf = vec![254, 2, 1, 2, 255];
        assert_eq!(parse_options(&buf[..], Vec::new()), Ok(Vec::new()));
    }

    #[test]
    fn option_stops_at_end_of_options() {
        let buf = vec![26, 2, 4, 0, 255, 26, 2, 4, 0];
        assert_eq!(parse_options(&buf[..], Vec::new()), Ok(vec![DhcpOption::InterfaceMtu(1024)]));
    }

    #[test]
    fn option_from_buffer_with_invalid_length_returns_err() {
        let buf = vec![1, 6, 255, 255, 255, 0];
        assert_eq!(
            parse_options(&buf[..], Vec::new()),
            Err(ProtocolError::MalformedOption { code: 1, remaining: 4, want: 6 })
        );
    }

    #[test]
    fn option_from_buffer_missing_length_returns_err() {
        let buf = vec![1];
        assert_eq!(
            parse_options(&buf[..], Vec::new()),
            Err(ProtocolError::MalformedOption { code: 1, remaining: 0, want: 1 })
        );
    }

    #[test]
    fn option_from_buffer_missing_end_option_returns_err() {
        assert_eq!(
            parse_options(&[], Vec::new()),
            Err(ProtocolError::MissingOption(OptionCode::End))
        );
    }

    #[test]
    fn get_dhcp_type_with_dhcp_type_option_returns_value() {
        let mut msg = new_test_msg();
        msg.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPDISCOVER));

        assert_eq!(msg.get_dhcp_type(), Ok(MessageType::DHCPDISCOVER));
    }

    #[test]
    fn get_dhcp_type_without_dhcp_type_option_returns_err() {
        let msg = new_test_msg();

        assert_eq!(
            msg.get_dhcp_type(),
            Err(ProtocolError::MissingOption(OptionCode::DhcpMessageType).into())
        );
    }

    #[test]
    fn buf_into_options_with_invalid_option_parses_other_valid_options() {
        let msg = || {
            let mut msg = new_test_msg();
            msg.options.push(DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK));
            msg.options.push(DhcpOption::Router(vec![ip_v4!("192.168.1.1")]));
            msg.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPDISCOVER));
            msg
        };

        let mut buf = msg().serialize();
        // introduce invalid option code in first option
        buf[OPTIONS_START_IDX + 4] = 99;

        // Expect that everything but the invalid option deserializes.
        let mut expected_msg = msg();
        assert_eq!(expected_msg.options.remove(0), DhcpOption::SubnetMask(DEFAULT_SUBNET_MASK));
        assert_eq!(Message::from_buffer(&buf), Ok(expected_msg));
    }

    #[test_case(OptionCode::HostName, 0; "Min length 1")]
    #[test_case(OptionCode::ClientIdentifier, 1; "Min length 2_1")]
    #[test_case(OptionCode::PathMtuPlateauTable, 1; "Min length 2_2")]
    #[test_case(OptionCode::Router, 0; "Min length 4")]
    #[test_case(OptionCode::PolicyFilter, 4; "Min length 8_1")]
    #[test_case(OptionCode::StaticRoute, 4; "Min length 8_2")]
    fn parse_options_with_invalid_min_lengths(code: OptionCode, len: usize) {
        let option = DhcpOption::from_raw_parts(code, &vec![0; len]);
        assert_eq!(Err(ProtocolError::InvalidBufferLength(len)), option)
    }

    #[test_case(OptionCode::IpForwarding, 0; "Length = 1")]
    #[test_case(OptionCode::BootfileName, 0; "Length = 2")]
    #[test_case(OptionCode::TimeOffset, 0; "Length = 4")]
    fn parse_options_with_invalid_static_length(code: OptionCode, len: usize) {
        let option = DhcpOption::from_raw_parts(code, &vec![0; len]);
        assert_eq!(Err(ProtocolError::InvalidBufferLength(len)), option)
    }

    #[test_case(OptionCode::PathMtuPlateauTable, 3; "Min length 2, multiple of 2")]
    #[test_case(OptionCode::MobileIpHomeAgent, 5; "Min length 0, multiple of 4")]
    #[test_case(OptionCode::PolicyFilter, 4; "PolicyFilter_4: Min length 8, multiple of 8")]
    #[test_case(OptionCode::PolicyFilter, 12; "PolicyFilter_12: Min length 8, multiple of 8 - 2")]
    #[test_case(OptionCode::StaticRoute, 4; "StaticRoute_4: Min length 8, multiple of 8 - 1")]
    #[test_case(OptionCode::StaticRoute, 12; "StaticRoute_12: Min length 8, multiple of 8 - 2")]
    fn parse_options_with_invalid_length_multiples(code: OptionCode, len: usize) {
        let option = DhcpOption::from_raw_parts(code, &vec![0; len]);
        assert_eq!(Err(ProtocolError::InvalidBufferLength(len)), option)
    }

    #[test_case(OptionCode::IpForwarding)]
    #[test_case(OptionCode::NonLocalSourceRouting)]
    #[test_case(OptionCode::AllSubnetsLocal)]
    #[test_case(OptionCode::PerformMaskDiscovery)]
    #[test_case(OptionCode::MaskSupplier)]
    #[test_case(OptionCode::PerformRouterDiscovery)]
    #[test_case(OptionCode::TrailerEncapsulation)]
    #[test_case(OptionCode::EthernetEncapsulation)]
    #[test_case(OptionCode::TcpKeepaliveGarbage)]
    fn parse_options_with_invalid_flag_value(code: OptionCode) {
        let val = vec![2];
        let option = DhcpOption::from_raw_parts(code, &val);
        assert_eq!(Err(ProtocolError::InvalidOptionValue(code, val)), option)
    }

    #[test_case(0)]
    #[test_case(4)]
    fn parse_options_with_invalid_overload_value(overload: u8) {
        let code = OptionCode::OptionOverload;
        let val = vec![overload];
        let option = DhcpOption::from_raw_parts(code, &val);

        // Valid values are 1, 2, 3.
        assert_eq!(Err(ProtocolError::InvalidOptionValue(code, val)), option);
    }

    #[test]
    fn parse_options_with_invalid_netbios_node_value() {
        // Valid values are 1, 2, 4, 8
        let code = OptionCode::NetBiosOverTcpipNodeType;
        let val = vec![3];
        let option = DhcpOption::from_raw_parts(code, &val);
        assert_eq!(Err(ProtocolError::InvalidOptionValue(code, val)), option);
    }

    #[test_case(OptionCode::DefaultIpTtl)]
    #[test_case(OptionCode::TcpDefaultTtl)]
    fn parse_options_with_invalid_ttl_value(code: OptionCode) {
        let val = vec![0];
        let option = DhcpOption::from_raw_parts(code, &val);
        assert_eq!(Err(ProtocolError::InvalidOptionValue(code, val)), option);
    }

    #[test_case(OptionCode::MaxDatagramReassemblySize, MIN_MESSAGE_SIZE)]
    #[test_case(OptionCode::MaxDhcpMessageSize, MIN_MESSAGE_SIZE)]
    #[test_case(OptionCode::InterfaceMtu, MIN_MTU_VAL)]
    fn parse_options_with_too_low_value(code: OptionCode, min_size: u16) {
        let val = (min_size - 1).to_be_bytes().to_vec();
        let option = DhcpOption::from_raw_parts(code, &val);
        assert_eq!(Err(ProtocolError::InvalidOptionValue(code, val)), option);
    }

    #[test]
    fn parameter_request_list_with_known_and_unknown_options_returns_known_options() {
        assert_eq!(
            DhcpOption::from_raw_parts(
                OptionCode::ParameterRequestList,
                &[
                    121, /* unrecognized */
                    1, 3, 6, 15, 31, 33, 249, /* unrecognized */
                    43, 44, 46, 47, 119, /* unrecognized */
                    252, /* unrecognized */
                ]
            ),
            Ok(DhcpOption::ParameterRequestList(vec![
                OptionCode::SubnetMask,
                OptionCode::Router,
                OptionCode::DomainNameServer,
                OptionCode::DomainName,
                OptionCode::PerformRouterDiscovery,
                OptionCode::StaticRoute,
                OptionCode::VendorSpecificInformation,
                OptionCode::NetBiosOverTcpipNameServer,
                OptionCode::NetBiosOverTcpipNodeType,
                OptionCode::NetBiosOverTcpipScope,
            ]))
        );
    }

    fn test_option_overload(overload: Overload) {
        let mut msg = Message {
            op: OpCode::BOOTREQUEST,
            xid: 0,
            secs: 0,
            bdcast_flag: false,
            ciaddr: Ipv4Addr::UNSPECIFIED,
            yiaddr: Ipv4Addr::UNSPECIFIED,
            siaddr: Ipv4Addr::UNSPECIFIED,
            giaddr: Ipv4Addr::UNSPECIFIED,
            chaddr: MacAddr::new([0; 6]),
            sname: String::from(""),
            file: String::from(""),
            options: vec![DhcpOption::OptionOverload(overload)],
        }
        .serialize();
        let ip = crate::server::tests::random_ipv4_generator();
        let first_extra_opt = {
            let mut acc = Vec::new();
            let () = DhcpOption::RequestedIpAddress(ip).serialize_to(&mut acc);
            acc
        };
        let last_extra_opt = {
            let mut acc = Vec::new();
            let () = DhcpOption::End().serialize_to(&mut acc);
            acc
        };
        let (extra_opts, start_idx) = match overload {
            Overload::SName => ([&first_extra_opt[..], &last_extra_opt[..]].concat(), SNAME_IDX),
            Overload::File => ([&first_extra_opt[..], &last_extra_opt[..]].concat(), FILE_IDX),
            Overload::Both => {
                // Insert enough padding bytes such that extra_opts will straddle both file and
                // sname fields.
                ([&first_extra_opt[..], &[0u8; SNAME_LEN], &last_extra_opt[..]].concat(), SNAME_IDX)
            }
        };
        let _: std::vec::Splice<'_, _> =
            msg.splice(start_idx..start_idx + extra_opts.len(), extra_opts);
        assert_eq!(
            Message::from_buffer(&msg),
            Ok(Message {
                op: OpCode::BOOTREQUEST,
                xid: 0,
                secs: 0,
                bdcast_flag: false,
                ciaddr: Ipv4Addr::UNSPECIFIED,
                yiaddr: Ipv4Addr::UNSPECIFIED,
                siaddr: Ipv4Addr::UNSPECIFIED,
                giaddr: Ipv4Addr::UNSPECIFIED,
                chaddr: MacAddr::new([0; 6]),
                sname: String::from(""),
                file: String::from(""),
                options: vec![
                    DhcpOption::OptionOverload(overload),
                    DhcpOption::RequestedIpAddress(ip)
                ],
            })
        );
    }

    #[test]
    fn message_with_option_overload_parses_extra_options() {
        test_option_overload(Overload::SName);
        test_option_overload(Overload::File);
        test_option_overload(Overload::Both);
    }

    #[test]
    fn client_identifier_from_str() {
        assert_matches::assert_matches!(
            ClientIdentifier::from_str("id:1234567890abcd"),
            Ok(ClientIdentifier { .. })
        );
        assert_matches::assert_matches!(
            ClientIdentifier::from_str("chaddr:1234567890ab"),
            Ok(ClientIdentifier { .. })
        );
        // incorrect type prefix
        assert_matches::assert_matches!(ClientIdentifier::from_str("option:1234567890"), Err(..));
        // extra field
        assert_matches::assert_matches!(ClientIdentifier::from_str("id:1234567890:extra"), Err(..));
        // no type prefix
        assert_matches::assert_matches!(ClientIdentifier::from_str("1234567890"), Err(..));
        // no delimiter
        assert_matches::assert_matches!(ClientIdentifier::from_str("id1234567890"), Err(..));
        // incorrect delimiter
        assert_matches::assert_matches!(ClientIdentifier::from_str("id-1234567890"), Err(..));
        // invalid hex digits
        assert_matches::assert_matches!(
            ClientIdentifier::from_str("id:1234567890abcdefg"),
            Err(..)
        );
        // odd number of hex digits
        assert_matches::assert_matches!(ClientIdentifier::from_str("id:123456789"), Err(..));
        // insufficient digits for chaddr
        assert_matches::assert_matches!(ClientIdentifier::from_str("chaddr:1234567890"), Err(..));
    }
}

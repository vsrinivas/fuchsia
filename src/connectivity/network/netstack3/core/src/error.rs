// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the netstack.

use failure::Fail;

use crate::ip::Ip;
use crate::wire::icmp::IcmpIpTypes;

/// Results returned from many functions in the netstack.
pub(crate) type Result<T> = std::result::Result<T, NetstackError>;

/// Results returned from parsing functions in the netstack.
pub(crate) type ParseResult<T> = std::result::Result<T, ParseError>;

/// Results returned from IP packet parsing functions in the netstack.
pub(crate) type IpParseResult<I, T> = std::result::Result<T, IpParseError<I>>;

/// Top-level error type the netstack.
#[derive(Fail, Debug)]
pub enum NetstackError {
    #[fail(display = "{}", _0)]
    /// Errors related to packet parsing.
    Parse(#[cause] ParseError),

    /// Error when item already exists.
    #[fail(display = "Item already exists")]
    Exists,

    /// Error when item is not found.
    #[fail(display = "Item not found")]
    NotFound,
    // Add error types here as we add more to the stack
}

/// Error type for packet parsing.
#[derive(Fail, Debug, PartialEq)]
pub enum ParseError {
    #[fail(display = "Operation is not supported")]
    NotSupported,
    #[fail(display = "Operation was not expected in this context")]
    NotExpected,
    #[fail(display = "Invalid checksum")]
    Checksum,
    #[fail(display = "Packet is not formatted properly")]
    Format,
}

/// Error type for IP packet parsing.
#[derive(Fail, Debug, PartialEq)]
pub(crate) enum IpParseError<I: Ip> {
    #[fail(display = "Parsing Error")]
    Parse { error: ParseError },
    /// For errors where an ICMP Parameter Problem error needs to be
    /// sent to the source of a packet.
    ///
    /// `src_ip` and `dst_ip` are the source and destination IP addresses of the
    /// original packet. If `must_send_icmp` is `true`, the RFC for either IPv4 or
    /// IPv6 specifies that an ICMP Parameter Problem MUST be sent back to the
    /// source; otherwise, we can choose to not send the response. `header_len`
    /// is the length of the header that we know about up to the point of the
    /// parameter problem error. `code` is the ICMP (ICMPv4 or ICMPv6) specific
    /// parameter problem code that provides more granular information about the
    /// parameter problem encountered. `pointer` is the offset of the erroneous
    /// value within an IP packet, calculated from the beginning of the IP packet.
    #[fail(display = "Parameter Problem")]
    ParameterProblem {
        src_ip: I::Addr,
        dst_ip: I::Addr,
        code: <I as IcmpIpTypes>::ParameterProblemCode,
        pointer: <I as IcmpIpTypes>::ParameterProblemPointer,
        must_send_icmp: bool,
        header_len: usize,
    },
}

impl<I: Ip> From<ParseError> for IpParseError<I> {
    fn from(error: ParseError) -> Self {
        IpParseError::Parse { error }
    }
}

/// Error when something exists unexpectedly, such as trying to add an
/// element when the element is already present.
pub(crate) struct ExistsError;

impl From<ExistsError> for NetstackError {
    fn from(_: ExistsError) -> NetstackError {
        NetstackError::Exists
    }
}

/// Error when something unexpectedly doesn't exist, such as trying to
/// remove an element when the element is not present.
pub(crate) struct NotFoundError;

impl From<NotFoundError> for NetstackError {
    fn from(_: NotFoundError) -> NetstackError {
        NetstackError::NotFound
    }
}

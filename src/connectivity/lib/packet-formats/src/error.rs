// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the packet formats.

use net_types::ip::{Ip, IpAddress};
use net_types::MulticastAddress;
use thiserror::Error;

use crate::icmp::IcmpIpTypes;

/// Results returned from parsing functions in the netstack.
pub type ParseResult<T> = core::result::Result<T, ParseError>;

/// Results returned from IP packet parsing functions in the netstack.
pub type IpParseResult<I, T> = core::result::Result<T, IpParseError<I>>;

/// Error type for packet parsing.
#[derive(Error, Debug, PartialEq)]
pub enum ParseError {
    /// Operation is not supported.
    #[error("Operation is not supported")]
    NotSupported,
    /// Operation is not expected in this context.
    #[error("Operation is not expected in this context")]
    NotExpected,
    /// Checksum is invalid.
    #[error("Invalid checksum")]
    Checksum,
    /// Packet is not formatted properly.
    #[error("Packet is not formatted properly")]
    Format,
}

/// Action to take when an IP node fails to parse a received IP packet.
///
/// These actions are taken from [RFC 8200 section 4.2]. Although these actions
/// are defined by an IPv6 RFC, IPv4 nodes that fail to parse a received packet
/// will take similar actions.
///
/// [RFC 8200 section 4.2]: https://tools.ietf.org/html/rfc8200#section-4.2
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum IpParseErrorAction {
    /// Discard the packet and do nothing further.
    DiscardPacket,

    /// Discard the packet and send an ICMP response.
    DiscardPacketSendIcmp,

    /// Discard the packet and send an ICMP response if the packet's
    /// destination address was not a multicast address.
    DiscardPacketSendIcmpNoMulticast,
}

impl IpParseErrorAction {
    /// Determines whether or not an ICMP message should be sent.
    ///
    /// Returns `true` if the caller should send an ICMP response. The caller should
    /// send an ICMP response if an action is set to `DiscardPacketSendIcmp`, or
    /// if an action is set to `DiscardPacketSendIcmpNoMulticast` and `dst_addr`
    /// (the destination address of the original packet that lead to a parsing
    /// error) is not a multicast address.
    pub fn should_send_icmp<A: IpAddress>(&self, dst_addr: &A) -> bool {
        match *self {
            IpParseErrorAction::DiscardPacket => false,
            IpParseErrorAction::DiscardPacketSendIcmp => true,
            IpParseErrorAction::DiscardPacketSendIcmpNoMulticast => !dst_addr.is_multicast(),
        }
    }

    /// Determines whether or not an ICMP message should be sent even if the original
    /// packet's destination address is a multicast.
    ///
    /// Per [RFC 1122 section 3.2.2] and [RFC 4443 section 2.4], ICMP messages MUST NOT
    /// be sent in response to packets destined to a multicast or broadcast address.
    /// However, RFC 4443 section 2.4 includes an exception to this rule if certain
    /// criteria are met when parsing IPv6 extension header options.
    /// `should_send_icmp_to_multicast` returns `true` if the criteria are met.
    /// See RFC 4443 section 2.4 for more details about the exception.
    ///
    /// [RFC 1122 section 3.2.2]: https://tools.ietf.org/html/rfc1122#section-3.2.2
    /// [RFC 4443 section 2.4]: https://tools.ietf.org/html/rfc4443#section-2.4
    pub fn should_send_icmp_to_multicast(&self) -> bool {
        match *self {
            IpParseErrorAction::DiscardPacketSendIcmp => true,
            IpParseErrorAction::DiscardPacket
            | IpParseErrorAction::DiscardPacketSendIcmpNoMulticast => false,
        }
    }
}

/// Error type for IP packet parsing.
#[allow(missing_docs)]
#[derive(Error, Debug, PartialEq)]
pub enum IpParseError<I: IcmpIpTypes> {
    #[error("Parsing Error")]
    Parse { error: ParseError },
    /// For errors where an ICMP Parameter Problem error needs to be sent to the
    /// source of a packet.
    #[error("Parameter Problem")]
    ParameterProblem {
        /// The packet's source IP address.
        src_ip: I::Addr,

        /// The packet's destination IP address.
        dst_ip: I::Addr,

        /// The ICMPv4 or ICMPv6 parameter problem code that provides more
        /// granular information about the parameter problem encountered.
        code: I::ParameterProblemCode,

        /// The offset of the erroneous value within the IP packet.
        pointer: I::ParameterProblemPointer,

        /// Whether an IP node MUST send an ICMP response if [`action`]
        /// specifies it.
        ///
        /// See [`action`] for more details.
        ///
        /// [`action`]: crate::error::IpParseError::ParameterProblem::action
        must_send_icmp: bool,

        /// The length of the header up to the point of the parameter problem
        /// error.
        header_len: I::HeaderLen,

        /// The action IP nodes should take upon encountering this error.
        ///
        /// If [`must_send_icmp`] is `true`, IP nodes MUST send an ICMP response
        /// if `action` specifies it. Otherwise, the node MAY choose to discard
        /// the packet and do nothing further.
        ///
        /// If the packet was an IPv4 non-initial fragment, `action` will be
        /// [`IpParseErrorAction::DiscardPacket`].
        ///
        /// [`must_send_icmp`]: crate::error::IpParseError::ParameterProblem::must_send_icmp
        action: IpParseErrorAction,
    },
}

impl<I: Ip> From<ParseError> for IpParseError<I> {
    fn from(error: ParseError) -> Self {
        IpParseError::Parse { error }
    }
}

/// Error type for an unrecognized protocol code of type `T`.
#[derive(Debug, Eq, PartialEq)]
pub struct UnrecognizedProtocolCode<T>(pub T);

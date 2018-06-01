// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

mod address;

pub use self::address::*;

/// An IP protocol or next header number.
///
/// For IPv4, this is the protocol number. For IPv6, this is the next header
/// number.
#[allow(missing_docs)]
#[derive(Eq, PartialEq, Debug)]
#[repr(u8)]
pub enum IpProto {
    Tcp = IpProto::TCP,
    Udp = IpProto::UDP,
}

impl IpProto {
    const TCP: u8 = 6;
    const UDP: u8 = 17;

    /// Construct an `IpProto` from a `u8`.
    ///
    /// `from_u8` returns the `IpProto` with the numerical value `u`, or `None`
    /// if the value is unrecognized.
    pub fn from_u8(u: u8) -> Option<IpProto> {
        match u {
            Self::TCP => Some(IpProto::Tcp),
            Self::UDP => Some(IpProto::Udp),
            _ => None,
        }
    }
}

/// An IPv4 header option.
///
/// An IPv4 header option comprises metadata about the option (which is stored
/// in the kind byte) and the option itself. Note that all kind-byte-only
/// options are handled by the utilities in `wire::util::options`, so this type
/// only supports options with variable-length data.
///
/// See [Wikipedia] or [RFC 791] for more details.
///
/// [Wikipedia]: https://en.wikipedia.org/wiki/IPv4#Options
/// [RFC 791]: https://tools.ietf.org/html/rfc791#page-15
pub struct Ipv4Option {
    /// Whether this option needs to be copied into all fragments of a fragmented packet.
    pub copied: bool,
    // TODO(joshlf): include "Option Class"?
    /// The variable-length option data.
    pub data: Ipv4OptionData,
}

/// The data associated with an IPv4 header option.
///
/// `Ipv4OptionData` represents the variable-length data field of an IPv4 header
/// option.
#[allow(missing_docs)]
pub enum Ipv4OptionData {
    // The maximum header length is 60 bytes, and the fixed-length header is 20
    // bytes, so there are 40 bytes for the options. That leaves a maximum
    // options size of 1 kind byte + 1 length byte + 38 data bytes.
    /// Data for an unrecognized option kind.
    ///
    /// Any unrecognized option kind will have its data parsed using this
    /// variant. This allows code to copy unrecognized options into packets when
    /// forwarding.
    Unrecognized { kind: u8, len: u8, data: [u8; 38] },
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::enums::*;
use anyhow::{format_err, Context as _};
use core::convert::{TryFrom, TryInto};
use spinel_pack::*;
use std::io;
use std::num::NonZeroU8;

/// A type for decoding/encoding the Spinel header byte.
#[derive(Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct Header(u8);

impl Header {
    /// Creates a new instance of `Header` based on the given NLI and TID.
    ///
    /// Valid values for NLI are between 0 and 3, inclusive.
    /// Valid values for TID are `None` or between `Some(1)` and `Some(15)`, inclusive.
    ///
    /// If either the NLI and/or TID are invalid, then
    /// this constructor will return `None`.
    pub fn new(nli: u8, tid: Option<NonZeroU8>) -> Option<Header> {
        let tid = tid.map_or(0, NonZeroU8::get);

        if nli < 4 && tid < 16 {
            Some(Header(0x80 + (nli << 4) + tid))
        } else {
            None
        }
    }

    /// Network link ID.
    pub fn nli(&self) -> u8 {
        (self.0 & 0x30) >> 4
    }

    /// Transaction Id
    pub fn tid(&self) -> Option<NonZeroU8> {
        NonZeroU8::new(self.0 & 0x0F)
    }
}

impl std::fmt::Debug for Header {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Header").field("nli", &self.nli()).field("tid", &self.tid()).finish()
    }
}

impl From<Header> for u8 {
    fn from(header: Header) -> Self {
        header.0
    }
}

impl TryFrom<u8> for Header {
    type Error = anyhow::Error;
    fn try_from(x: u8) -> Result<Self, Self::Error> {
        if x & 0xC0 != 0x80 {
            Err(format_err!("Bad header byte"))?;
        }
        Ok(Header(x))
    }
}

impl TryPackAs<u8> for Header {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(1)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl TryPack for Header {
    fn pack_len(&self) -> io::Result<usize> {
        Ok(1)
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl<'a> TryUnpackAs<'a, u8> for Header {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let id: u8 = TryUnpackAs::<u8>::try_unpack_as(iter)?;

        id.try_into().context(UnpackingError::InvalidValue)
    }
}

impl<'a> TryUnpack<'a> for Header {
    type Unpacked = Header;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        TryUnpackAs::<u8>::try_unpack_as(iter)
    }
}

/// Struct that represents a decoded Spinel command frame with a
/// borrowed reference to the payload.
#[spinel_packed("CiD")]
#[derive(Debug, Clone, Copy)]
pub struct SpinelFrameRef<'a> {
    pub header: Header,
    pub cmd: Cmd,
    pub payload: &'a [u8],
}

/// Struct that represents a decoded Spinel property payload with a
/// borrowed reference to the value.
#[spinel_packed("iD")]
#[derive(Debug, Clone, Copy)]
pub struct SpinelPropValueRef<'a> {
    pub prop: Prop,
    pub value: &'a [u8],
}

/// Spinel Protocol Version struct, for determining device compatibility.
#[spinel_packed("ii")]
#[derive(Debug, Clone, Copy)]
pub struct ProtocolVersion(pub u32, pub u32);

/// The MAC section of a spinel network scan result
#[spinel_packed("ESSC")]
#[derive(Debug)]
pub struct NetScanResultMac {
    pub long_addr: EUI64,
    pub short_addr: u16,
    pub panid: u16,
    pub lqi: u8,
}

/// The NET section of a spinel network scan result
#[spinel_packed("iCUdd")]
#[derive(Debug)]
pub struct NetScanResultNet {
    pub net_type: u32,
    pub flags: u8,
    pub network_name: Vec<u8>,
    pub xpanid: Vec<u8>,
    pub steering_data: Vec<u8>,
}

/// A spinel network scan result
#[spinel_packed("Ccdd")]
#[derive(Debug)]
pub struct NetScanResult {
    pub channel: u8,
    pub rssi: i8,
    pub mac: NetScanResultMac,
    pub net: NetScanResultNet,
}

/// A spinel energy scan result
#[spinel_packed("Cc")]
#[derive(Debug)]
pub struct EnergyScanResult {
    pub channel: u8,
    pub rssi: i8,
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_header() {
        assert_eq!(Header::new(0, None), Some(Header(0x80)));
        assert_eq!(Header::new(3, None), Some(Header(0xb0)));
        assert_eq!(Header::new(4, None), None);
        assert_eq!(Header::new(0, NonZeroU8::new(1)), Some(Header(0x81)));
        assert_eq!(Header::new(0, NonZeroU8::new(15)), Some(Header(0x8F)));
        assert_eq!(Header::new(0, NonZeroU8::new(16)), None);

        assert_eq!(Header::new(3, NonZeroU8::new(15)), Some(Header(0xBF)));
        assert_eq!(Header::new(4, NonZeroU8::new(16)), None);

        assert_matches!(Header::try_from(0x00), Err(_));
        assert_matches!(Header::try_from(0xBF), Ok(Header(0xBF)));
        assert_matches!(Header::try_from(0xFF), Err(_));

        assert_eq!(Header(0x80).tid(), None);
        assert_eq!(Header(0x80).nli(), 0);
        assert_eq!(Header(0xBF).tid(), NonZeroU8::new(15));
        assert_eq!(Header(0xBF).nli(), 3);
    }
}

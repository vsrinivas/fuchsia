// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ot::ParseError;
use crate::prelude_internal::*;
use std::io::Write;

const TLV_ESCAPE_LENGTH: u8 = 255;

/// Represents a type-length-value (TLV) item.
#[derive(Debug)]
#[allow(missing_docs)]
pub enum MeshcopTlv<'a> {
    // TODO: Eventually support all of the TLV types natively.
    NetworkName(NetworkName),
    PanId(PanId),
    Channel(u8, u16),

    /// Active Timestamp. Value in ns.
    ActiveTimestamp(u64),

    /// Pending Timestamp. Value in ns.
    PendingTimestamp(u64),

    /// Delay Timer. Value in ms.
    DelayTimer(u32),
    Unknown(u8, &'a [u8]),
}

impl<'a> MeshcopTlv<'a> {
    /// Returns the type of this TLV.
    pub fn get_type(&self) -> u8 {
        match self {
            MeshcopTlv::PanId(_) => OT_MESHCOP_TLV_PANID.try_into().unwrap(),
            MeshcopTlv::Channel(_, _) => OT_MESHCOP_TLV_CHANNEL.try_into().unwrap(),
            MeshcopTlv::NetworkName(_) => OT_MESHCOP_TLV_NETWORKNAME.try_into().unwrap(),
            MeshcopTlv::ActiveTimestamp(_) => OT_MESHCOP_TLV_ACTIVETIMESTAMP.try_into().unwrap(),
            MeshcopTlv::PendingTimestamp(_) => OT_MESHCOP_TLV_PENDINGTIMESTAMP.try_into().unwrap(),
            MeshcopTlv::DelayTimer(_) => OT_MESHCOP_TLV_DELAYTIMER.try_into().unwrap(),
            MeshcopTlv::Unknown(x, _) => *x,
        }
    }

    /// Returns the encoded length of the value.
    pub fn value_len(&self) -> usize {
        match self {
            MeshcopTlv::PanId(_) => core::mem::size_of::<PanId>(),
            MeshcopTlv::Channel(_, _) => core::mem::size_of::<u8>() + core::mem::size_of::<u16>(),
            MeshcopTlv::NetworkName(x) => x.len(),
            MeshcopTlv::ActiveTimestamp(_) => core::mem::size_of::<u64>(),
            MeshcopTlv::PendingTimestamp(_) => core::mem::size_of::<u64>(),
            MeshcopTlv::DelayTimer(_) => core::mem::size_of::<u32>(),
            MeshcopTlv::Unknown(_, x) => x.len(),
        }
    }

    /// Returns the value of this TLV as a `u8`.
    pub fn value_as_u8(&self) -> Option<u8> {
        if self.value_len() != core::mem::size_of::<u8>() {
            return None;
        }

        let mut bytes = [0u8; 1];
        self.write_value_to(&mut bytes.as_mut_slice()).ok()?;
        Some(bytes[0])
    }

    /// Returns the value of this TLV as a `u16`.
    pub fn value_as_u16(&self) -> Option<u16> {
        if self.value_len() != core::mem::size_of::<u16>() {
            return None;
        }

        let mut bytes = [0u8; 2];
        self.write_value_to(&mut bytes.as_mut_slice()).ok()?;
        Some(u16::from_le_bytes(bytes))
    }

    /// Returns the value of this TLV as a `u32`.
    pub fn value_as_u32(&self) -> Option<u32> {
        if self.value_len() != core::mem::size_of::<u32>() {
            return None;
        }

        let mut bytes = [0u8; 4];
        self.write_value_to(&mut bytes.as_mut_slice()).ok()?;
        Some(u32::from_le_bytes(bytes))
    }

    /// Returns the value of this TLV as a `u64`.
    pub fn value_as_u64(&self) -> Option<u64> {
        if self.value_len() != core::mem::size_of::<u64>() {
            return None;
        }

        let mut bytes = [0u8; 8];
        self.write_value_to(&mut bytes.as_mut_slice()).ok()?;
        Some(u64::from_le_bytes(bytes))
    }

    /// Returns the value of this TLV as a `&[u8]`.
    pub fn value_as_slice(&self) -> Option<&'_ [u8]> {
        match self {
            MeshcopTlv::NetworkName(x) => Some(x.as_slice()),
            MeshcopTlv::Unknown(_, x) => Some(x),
            _ => None,
        }
    }

    /// Serialized length of this TLV.
    pub fn len(&self) -> usize {
        if self.value_len() > (TLV_ESCAPE_LENGTH as usize - 1) {
            3 + self.value_len()
        } else {
            2 + self.value_len()
        }
    }

    /// Writes this TLV to the given slice, returning the trimmed
    /// slice if the given slice was large enough to hold the value. If the given
    /// slice was too small, it remains unchanged and the method returns `None`.
    pub fn write_to<'b, T: Write>(&self, out: &mut T) -> std::io::Result<()> {
        let value_len = self.value_len();

        if value_len > (TLV_ESCAPE_LENGTH as usize - 1) {
            let value_len_le = u16::try_from(value_len).unwrap().to_le_bytes();
            out.write(&[self.get_type()])?;
            out.write(&value_len_le)?;
            self.write_value_to(out)
        } else {
            out.write(&[self.get_type()])?;
            out.write(&[value_len.try_into().unwrap()])?;
            self.write_value_to(out)
        }
    }

    /// Writes the value of this TLV to the given slice, returning the trimmed
    /// slice if the given slice was large enough to hold the value. If the given
    /// slice was too small, it remains unchanged and the method returns `None`.
    pub fn write_value_to<'b, T: Write>(&self, out: &mut T) -> std::io::Result<()> {
        match self {
            MeshcopTlv::NetworkName(x) => out.write_all(x.as_slice()),

            MeshcopTlv::ActiveTimestamp(a_u64) | MeshcopTlv::PendingTimestamp(a_u64) => {
                out.write_all(a_u64.to_le_bytes().as_slice())
            }
            MeshcopTlv::DelayTimer(a_u32) => out.write_all(a_u32.to_le_bytes().as_slice()),
            MeshcopTlv::PanId(a_u16) => out.write_all(a_u16.to_le_bytes().as_slice()),
            MeshcopTlv::Channel(page, index) => {
                out.write_all(&[*page])?;
                out.write_all(index.to_le_bytes().as_slice())
            }
            MeshcopTlv::Unknown(_, x) => out.write_all(x),
        }
    }

    /// Constructs a TLV from type and value
    pub fn from_type_and_value(tlv_type: u8, value: &'a [u8]) -> MeshcopTlv<'a> {
        match tlv_type as otMeshcopTlvType {
            OT_MESHCOP_TLV_NETWORKNAME if value.len() <= (OT_NETWORK_NAME_MAX_SIZE as usize) => {
                MeshcopTlv::NetworkName(NetworkName::try_from_slice(value).unwrap())
            }
            OT_MESHCOP_TLV_CHANNEL if value.len() == 3 => {
                let mut bytes = [0u8; 2];
                bytes.copy_from_slice(&value[1..]);
                MeshcopTlv::Channel(value[0], u16::from_le_bytes(bytes))
            }
            OT_MESHCOP_TLV_PANID if value.len() == 2 => {
                let mut bytes = [0u8; 2];
                bytes.copy_from_slice(value);
                MeshcopTlv::PanId(u16::from_le_bytes(bytes))
            }
            OT_MESHCOP_TLV_DELAYTIMER if value.len() == 4 => {
                let mut bytes = [0u8; 4];
                bytes.copy_from_slice(value);
                MeshcopTlv::DelayTimer(u32::from_le_bytes(bytes))
            }
            OT_MESHCOP_TLV_ACTIVETIMESTAMP if value.len() == 8 => {
                let mut bytes = [0u8; 8];
                bytes.copy_from_slice(value);
                MeshcopTlv::ActiveTimestamp(u64::from_le_bytes(bytes))
            }

            OT_MESHCOP_TLV_PENDINGTIMESTAMP if value.len() == 8 => {
                let mut bytes = [0u8; 8];
                bytes.copy_from_slice(value);
                MeshcopTlv::PendingTimestamp(u64::from_le_bytes(bytes))
            }

            OT_MESHCOP_TLV_NETWORKNAME
            | OT_MESHCOP_TLV_CHANNEL
            | OT_MESHCOP_TLV_PANID
            | OT_MESHCOP_TLV_DELAYTIMER
            | OT_MESHCOP_TLV_ACTIVETIMESTAMP
            | OT_MESHCOP_TLV_PENDINGTIMESTAMP => {
                // If we hit this fall-thru then that means the size was unexpected.
                // We don't want to fail hard here because this is technically not
                // a parse error (we can just pass it as `Unknown`), but we handle
                // it separately from the default case so that we can print an error.
                warn!("Unexpected TLV length {} for type {}", value.len(), tlv_type);
                MeshcopTlv::Unknown(tlv_type, value)
            }

            _ => MeshcopTlv::Unknown(tlv_type, value),
        }
    }

    fn parse_and_update(data: &'a [u8]) -> Result<(MeshcopTlv<'a>, &'a [u8]), ParseError> {
        if data.len() < 2 {
            return Err(ParseError);
        }
        let (value, ret) = if data[1] == TLV_ESCAPE_LENGTH {
            if data.len() < 3 {
                return Err(ParseError);
            }

            let len = u16::from_le_bytes([data[2], data[3]]) as usize;

            if data.len() < 3 + len {
                return Err(ParseError);
            }

            (&data[3..3 + len], &data[3 + len..])
        } else {
            let len = data[1] as usize;

            if data.len() < 2 + len {
                return Err(ParseError);
            }

            (&data[2..2 + len], &data[2 + len..])
        };

        Ok((MeshcopTlv::from_type_and_value(data[0], value), ret))
    }
}

/// An iterator type for parsing OpenThread TLV data.
#[derive(Debug)]
pub struct MeshcopTlvIterator<'a>(&'a [u8]);

impl<'a> Iterator for MeshcopTlvIterator<'a> {
    type Item = Result<MeshcopTlv<'a>, ParseError>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.0.len() < 2 {
            return None;
        }
        match MeshcopTlv::parse_and_update(self.0) {
            Ok((tlv, data)) => {
                self.0 = data;
                Some(Ok(tlv))
            }
            Err(x) => Some(Err(x)),
        }
    }
}

/// Extension trait for adding `tlvs()` method to `&[u8]`.
pub trait TlvIteratorExt {
    /// Returns an iterator over the TLV encoded data.
    fn meshcop_tlvs(&self) -> MeshcopTlvIterator<'_>;
}

impl TlvIteratorExt for [u8] {
    fn meshcop_tlvs(&self) -> MeshcopTlvIterator<'_> {
        MeshcopTlvIterator(self)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_tlv_deserialize_serialize() {
        let data = hex::decode("0e080000000000010000000300000b35060004001fffc00208029c6f4dbae059cb0708fd0087d0e40b384405105f7ddf9e9e9670d81331ad06754660320308626c61686e6574320102f09f04105d95f609e1e842c47a69ddd77e23e23d0c0402a0fff8").unwrap();

        let tlvs =
            data.meshcop_tlvs().collect::<Result<Vec<_>, _>>().expect("Failed to parse TLVs");

        let mut data2 = vec![];

        tlvs.into_iter().for_each(|x| x.write_to(&mut data2).unwrap());

        assert_eq!(data, data2);
    }
}

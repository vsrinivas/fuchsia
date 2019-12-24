// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Message encode/decode helpers

use anyhow::Error;
use std::convert::TryInto;

pub fn decode_fidl<T: fidl::encoding::Decodable>(bytes: &mut [u8]) -> Result<T, Error> {
    let mut value = T::new_empty();
    // WARNING: Since we are decoding without a transaction header, we have to
    // provide a context manually. This could cause problems in future FIDL wire
    // format migrations, which are driven by header flags.
    let context = fidl::encoding::Context {};
    fidl::encoding::Decoder::decode_with_context(&context, bytes, &mut [], &mut value)?;
    Ok(value)
}

/// The type of frame that can be received on a QUIC stream
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FrameType {
    Data,
}

/// Header for one frame of data on a QUIC stream
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct FrameHeader {
    pub(crate) frame_type: FrameType,
    pub(crate) length: usize,
}

pub(crate) const FRAME_HEADER_LENGTH: usize = 8;

impl FrameHeader {
    pub(crate) fn to_bytes(&self) -> Result<[u8; FRAME_HEADER_LENGTH], Error> {
        let length = self.length;
        if length > std::u32::MAX as usize {
            return Err(anyhow::format_err!("Message too long: {}", length));
        }
        let length = length as u32;
        let hdr: u64 = (length as u64)
            | (match self.frame_type {
                FrameType::Data => 0,
            } << 32);
        Ok(hdr.to_le_bytes())
    }

    pub(crate) fn from_bytes(bytes: &[u8]) -> Result<Self, Error> {
        let hdr: &[u8; FRAME_HEADER_LENGTH] = bytes[0..FRAME_HEADER_LENGTH].try_into()?;
        let hdr = u64::from_le_bytes(*hdr);
        let length = (hdr & 0xffff_ffff) as usize;
        let frame_type = match hdr >> 32 {
            0 => FrameType::Data,
            _ => return Err(anyhow::format_err!("Unknown frame type {}", hdr >> 32)),
        };
        Ok(FrameHeader { frame_type, length })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn roundtrip(h: FrameHeader) {
        assert_eq!(h, FrameHeader::from_bytes(&h.to_bytes().unwrap()).unwrap());
    }

    #[test]
    fn roundtrips() {
        roundtrip(FrameHeader { frame_type: FrameType::Data, length: 0 });
        roundtrip(FrameHeader { frame_type: FrameType::Data, length: std::u32::MAX as usize });
    }

    #[test]
    fn too_long() {
        FrameHeader { frame_type: FrameType::Data, length: (std::u32::MAX as usize) + 1 }
            .to_bytes()
            .expect_err("Should fail");
    }

    #[test]
    fn bad_frame_type() {
        assert!(format!(
            "{}",
            FrameHeader::from_bytes(&[0, 0, 0, 0, 11, 0, 0, 0]).expect_err("should fail")
        )
        .contains("Unknown frame type 11"));
    }
}

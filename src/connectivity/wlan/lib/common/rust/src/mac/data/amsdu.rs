// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        big_endian::BigEndianU16,
        buffer_reader::BufferReader,
        mac::{round_up, MacAddr},
    },
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

// IEEE Std 802.11-2016, 9.3.2.2.2
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AmsduSubframeHdr {
    // Note this is the same as the IEEE 802.3 frame format.
    pub da: MacAddr,
    pub sa: MacAddr,
    pub msdu_len: BigEndianU16,
}

pub struct AmsduSubframe<B> {
    pub hdr: LayoutVerified<B, AmsduSubframeHdr>,
    pub body: B,
}

/// Parse an A-MSDU subframe from the byte stream and advance the cursor in the `BufferReader` if
/// successful. Parsing is only successful if the byte stream starts with a valid subframe.
/// TODO(fxbug.dev/29615): The received AMSDU should not be greater than `max_amsdu_len`, specified in
/// HtCapabilities IE of Association. Warn or discard if violated.
impl<B: ByteSlice> AmsduSubframe<B> {
    pub fn parse(buffer_reader: &mut BufferReader<B>) -> Option<Self> {
        let hdr = buffer_reader.read::<AmsduSubframeHdr>()?;
        let msdu_len = hdr.msdu_len.to_native() as usize;
        if buffer_reader.bytes_remaining() < msdu_len {
            None
        } else {
            let body = buffer_reader.read_bytes(msdu_len)?;
            let base_len = std::mem::size_of::<AmsduSubframeHdr>() + msdu_len;
            let padded_len = round_up(base_len, 4);
            let padding_len = padded_len - base_len;
            if buffer_reader.bytes_remaining() == 0 {
                Some(Self { hdr, body })
            } else if buffer_reader.bytes_remaining() <= padding_len {
                // The subframe is invalid if EITHER one of the following is true
                // a) there are not enough bytes in the buffer for padding
                // b) the remaining buffer only contains padding bytes
                // IEEE 802.11-2016 9.3.2.2.2 `The last A-MSDU subframe has no padding.`
                None
            } else {
                buffer_reader.read_bytes(padding_len)?;
                Some(Self { hdr, body })
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {crate::mac::*, crate::test_utils::fake_frames::*};

    #[test]
    fn parse_data_amsdu() {
        let amsdu_data_frame = make_data_frame_amsdu();

        let msdus = MsduIterator::from_raw_data_frame(&amsdu_data_frame[..], false);
        assert!(msdus.is_some());
        let mut found_msdus = (false, false);
        for Msdu { dst_addr, src_addr, llc_frame } in msdus.unwrap() {
            match found_msdus {
                (false, false) => {
                    assert_eq!(dst_addr, [0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03]);
                    assert_eq!(src_addr, [0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab]);
                    assert_eq!(llc_frame.hdr.protocol_id.to_native(), 0x0800);
                    assert_eq!(llc_frame.body, MSDU_1_PAYLOAD);
                    found_msdus = (true, false);
                }
                (true, false) => {
                    assert_eq!(dst_addr, [0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04]);
                    assert_eq!(src_addr, [0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac]);
                    assert_eq!(llc_frame.hdr.protocol_id.to_native(), 0x0801);
                    assert_eq!(llc_frame.body, MSDU_2_PAYLOAD);
                    found_msdus = (true, true);
                }
                _ => panic!("unexepcted MSDU: {:x?}", llc_frame.body),
            }
        }
        assert_eq!(found_msdus, (true, true));
    }

    #[test]
    fn parse_data_amsdu_padding_too_short() {
        let amsdu_data_frame = make_data_frame_amsdu_padding_too_short();

        let msdus = MsduIterator::from_raw_data_frame(&amsdu_data_frame[..], false);
        assert!(msdus.is_some());
        let mut found_one_msdu = false;
        for Msdu { dst_addr, src_addr, llc_frame } in msdus.unwrap() {
            assert!(!found_one_msdu);
            assert_eq!(dst_addr, [0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03]);
            assert_eq!(src_addr, [0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab]);
            assert_eq!(llc_frame.hdr.protocol_id.to_native(), 0x0800);
            assert_eq!(llc_frame.body, MSDU_1_PAYLOAD);
            found_one_msdu = true;
        }
        assert!(found_one_msdu);
    }
}

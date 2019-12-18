// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{big_endian::BigEndianU16, mac::MacAddr},
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

// RFC 704, Appendix B.2
// https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.xhtml
pub const ETHER_TYPE_EAPOL: u16 = 0x888E;
pub const ETHER_TYPE_IPV4: u16 = 0x0800;

pub const MAX_ETH_FRAME_LEN: usize = 2048;

// IEEE Std 802.3-2015, 3.1.1
#[derive(FromBytes, AsBytes, Unaligned, Clone, Copy, Debug)]
#[repr(C, packed)]
pub struct EthernetIIHdr {
    pub da: MacAddr,
    pub sa: MacAddr,
    pub ether_type: BigEndianU16,
}

pub struct EthernetFrame<B: ByteSlice> {
    pub hdr: LayoutVerified<B, EthernetIIHdr>,
    pub body: B,
}

impl<B: ByteSlice> EthernetFrame<B> {
    pub fn parse(bytes: B) -> Option<Self> {
        let (hdr, body) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
        Some(Self { hdr, body })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn eth_hdr_big_endian() {
        let mut bytes: Vec<u8> = vec![
            1, 2, 3, 4, 5, 6, // dst_addr
            7, 8, 9, 10, 11, 12, // src_addr
            13, 14, // ether_type
            99, 99, // trailing bytes
        ];
        let (mut hdr, body) =
            LayoutVerified::<_, EthernetIIHdr>::new_unaligned_from_prefix(&mut bytes[..])
                .expect("cannot create ethernet header.");
        assert_eq!(hdr.da, [1u8, 2, 3, 4, 5, 6]);
        assert_eq!(hdr.sa, [7u8, 8, 9, 10, 11, 12]);
        assert_eq!(hdr.ether_type.to_native(), 13 << 8 | 14);
        assert_eq!(hdr.ether_type.0, [13u8, 14]);
        assert_eq!(body, [99, 99]);

        hdr.ether_type.set_from_native(0x888e);
        assert_eq!(hdr.ether_type.0, [0x88, 0x8e]);
        #[rustfmt::skip]
        assert_eq!(
            &[1u8, 2, 3, 4, 5, 6,
            7, 8, 9, 10, 11, 12,
            0x88, 0x8e,
            99, 99],
            &bytes[..]);
    }
}

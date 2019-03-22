// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mac::MacAddr,
    byteorder::{BigEndian, ByteOrder},
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

// RFC 704, Appendix B.2
// https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.xhtml
pub const ETHER_TYPE_EAPOL: u16 = 0x888E;

pub const MAX_ETH_FRAME_LEN: usize = 2048;

// IEEE Std 802.3-2015, 3.1.1
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct EthernetIIHdr {
    pub da: MacAddr,
    pub sa: MacAddr,
    pub ether_type_be: [u8; 2], // In network byte order (big endian).
}

impl EthernetIIHdr {
    pub fn ether_type(&self) -> u16 {
        BigEndian::read_u16(&self.ether_type_be)
    }
    pub fn set_ether_type(&mut self, val: u16) {
        BigEndian::write_u16(&mut self.ether_type_be, val)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::mac::*};

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
        assert_eq!(hdr.ether_type(), 13 << 8 | 14);
        assert_eq!(hdr.ether_type_be, [13u8, 14]);
        assert_eq!(body, [99, 99]);

        hdr.set_ether_type(0x888e);
        assert_eq!(hdr.ether_type_be, [0x88, 0x8e]);
        #[rustfmt::skip]
        assert_eq!(
            &[1u8, 2, 3, 4, 5, 6,
            7, 8, 9, 10, 11, 12,
            0x88, 0x8e,
            99, 99],
            &bytes[..]);
    }
}

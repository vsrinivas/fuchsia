// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    byteorder::{BigEndian, ByteOrder},
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

macro_rules! big_endian_inttype {
    ($struct_name:ident, $inttype:ty, $read_fn:ident, $write_fn:ident, $width:expr) => {
        #[repr(C)]
        #[derive(Clone, Copy, Debug, Default, AsBytes, FromBytes, Unaligned, PartialEq, Eq)]
        pub struct $struct_name(pub [u8; $width]);
        impl $struct_name {
            pub fn from_native(native: $inttype) -> Self {
                let mut buf = [0u8; $width];
                BigEndian::$write_fn(&mut buf, native);
                $struct_name(buf)
            }

            pub fn to_native(&self) -> $inttype {
                BigEndian::$read_fn(&self.0)
            }

            pub fn set_from_native(&mut self, value: $inttype) {
                BigEndian::$write_fn(&mut self.0, value);
            }
        }
    };
}

big_endian_inttype!(BigEndianU16, u16, read_u16, write_u16, 2);
big_endian_inttype!(BigEndianU32, u32, read_u32, write_u32, 4);
big_endian_inttype!(BigEndianU64, u64, read_u64, write_u64, 8);
big_endian_inttype!(BigEndianU128, u128, read_u128, write_u128, 16);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn big_endian_u16() {
        let mut x = BigEndianU16::from_native(0x1234);
        assert_eq!([0x12, 0x34], x.0);
        assert_eq!(0x1234, x.to_native());

        x.set_from_native(0x5678);
        assert_eq!([0x56, 0x78], x.0);
        assert_eq!(0x5678, x.to_native());
    }

    #[test]
    fn big_endian_u32() {
        let mut x = BigEndianU32::from_native(0x12345678);
        assert_eq!([0x12, 0x34, 0x56, 0x78], x.0);
        assert_eq!(0x12345678, x.to_native());

        x.set_from_native(0x12345678);
        assert_eq!([0x12, 0x34, 0x56, 0x78], x.0);
        assert_eq!(0x12345678, x.to_native());
    }

    #[test]
    fn big_endian_u64() {
        let mut x = BigEndianU64::from_native(0x0123456789abcdef);
        assert_eq!([0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef], x.0);
        assert_eq!(0x0123456789abcdef, x.to_native());

        x.set_from_native(0x0123456789abcdef);
        assert_eq!([0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef], x.0);
        assert_eq!(0x0123456789abcdef, x.to_native());
    }

    #[test]
    fn big_endian_u128() {
        let mut x = BigEndianU128::from_native(0x0123456789abcdef0000111122223333);
        assert_eq!(
            [
                0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x00, 0x00, 0x11, 0x11, 0x22, 0x22,
                0x33, 0x33,
            ],
            x.0
        );
        assert_eq!(0x0123456789abcdef0000111122223333, x.to_native());

        x.set_from_native(0x0123456789abcdef0000111122223333);
        assert_eq!(
            [
                0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x00, 0x00, 0x11, 0x11, 0x22, 0x22,
                0x33, 0x33,
            ],
            x.0
        );
        assert_eq!(0x0123456789abcdef0000111122223333, x.to_native());
    }
}

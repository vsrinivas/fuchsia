// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    byteorder::{BigEndian, ByteOrder},
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, AsBytes, FromBytes, Unaligned, PartialEq, Eq)]
pub struct BigEndianU16(pub [u8; 2]);

impl BigEndianU16 {
    pub fn from_native(native: u16) -> Self {
        let mut buf = [0, 0];
        BigEndian::write_u16(&mut buf, native);
        BigEndianU16(buf)
    }

    pub fn to_native(&self) -> u16 {
        BigEndian::read_u16(&self.0)
    }

    pub fn set_from_native(&mut self, value: u16) {
        BigEndian::write_u16(&mut self.0, value);
    }
}

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
}

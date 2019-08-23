// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! This module defines the `Uuid` type which represents a 128-bit Bluetooth UUID. It provides
///! convenience functions to support 16-bit, 32-bit, and 128-bit canonical formats as well as
///! string representation. It can be converted to/from a fuchsia.bluetooth.Uuid FIDL type.
use {
    byteorder::{ByteOrder, LittleEndian},
    fidl_fuchsia_bluetooth as fidl,
    std::fmt,
};

const NUM_UUID_BYTES: usize = 16;

#[derive(Clone, Debug, PartialEq)]
pub struct Uuid([u8; NUM_UUID_BYTES]);

fn base_uuid() -> Uuid {
    Uuid([
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
    ])
}

impl Uuid {
    pub fn new16(value: u16) -> Uuid {
        Uuid::new32(value as u32)
    }

    pub fn new32(value: u32) -> Uuid {
        let mut uuid = base_uuid();
        LittleEndian::write_u32(&mut uuid.0[(NUM_UUID_BYTES - 4)..NUM_UUID_BYTES], value);
        uuid
    }

    pub fn to_string(&self) -> String {
        format!("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                self.0[15], self.0[14], self.0[13], self.0[12], self.0[11], self.0[10], self.0[9], self.0[8], self.0[7],
                self.0[6], self.0[5], self.0[4], self.0[3], self.0[2], self.0[1], self.0[0])
    }
}

impl From<&fidl::Uuid> for Uuid {
    fn from(src: &fidl::Uuid) -> Uuid {
        Uuid(src.value)
    }
}

impl From<fidl::Uuid> for Uuid {
    fn from(src: fidl::Uuid) -> Uuid {
        Uuid::from(&src)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn uuid16_to_string() {
        let uuid = Uuid::new16(0x180d);
        assert_eq!("0000180d-0000-1000-8000-00805f9b34fb", uuid.to_string());
    }

    #[test]
    fn uuid32_to_string() {
        let uuid = Uuid::new32(0xAABBCCDD);
        assert_eq!("aabbccdd-0000-1000-8000-00805f9b34fb", uuid.to_string());
    }

    #[test]
    fn uuid128_to_string() {
        let uuid = Uuid([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
        assert_eq!("0f0e0d0c-0b0a-0908-0706-050403020100", uuid.to_string());
    }

    #[test]
    fn uuid_from_fidl() {
        let uuid = fidl::Uuid { value: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15] };
        let uuid: Uuid = uuid.into();
        assert_eq!("0f0e0d0c-0b0a-0908-0706-050403020100", uuid.to_string());
    }
}

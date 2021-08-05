// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::uapi::*;

#[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
pub struct DeviceType(dev_t);

impl DeviceType {
    pub const NONE: DeviceType = DeviceType(0);

    pub fn new(major: u32, minor: u32) -> DeviceType {
        // This encoding is part of the Linux UAPI. The encoded value is
        // returned to userspace in the stat struct.
        // See <https://man7.org/linux/man-pages/man3/makedev.3.html>.
        DeviceType(
            (((major & 0xfffff000) as u64) << 32)
                | (((major & 0xfff) as u64) << 8)
                | (((minor & 0xffffff00) as u64) << 12)
                | ((minor & 0xff) as u64),
        )
    }

    pub fn from_bits(dev: dev_t) -> DeviceType {
        DeviceType(dev)
    }

    pub fn bits(&self) -> dev_t {
        self.0
    }

    pub fn major(&self) -> u32 {
        ((self.0 >> 32 & 0xfffff000) | ((self.0 >> 8) & 0xfff)) as u32
    }

    pub fn minor(&self) -> u32 {
        ((self.0 >> 12 & 0xffffff00) | (self.0 & 0xff)) as u32
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_device_type() {
        let dev = DeviceType::new(21, 17);
        assert_eq!(dev.major(), 21);
        assert_eq!(dev.minor(), 17);

        let dev = DeviceType::new(0x83af83fe, 0xf98ecba1);
        assert_eq!(dev.major(), 0x83af83fe);
        assert_eq!(dev.minor(), 0xf98ecba1);
    }
}

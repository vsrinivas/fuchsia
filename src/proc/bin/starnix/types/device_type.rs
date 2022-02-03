// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::uapi::*;

#[derive(Copy, Clone, Debug, Default, Eq, Ord, PartialEq, PartialOrd)]
pub struct DeviceType(dev_t);

impl DeviceType {
    pub const NONE: DeviceType = DeviceType(0);
    pub const NULL: DeviceType = DeviceType::new(1, 3);
    pub const ZERO: DeviceType = DeviceType::new(1, 5);
    pub const FULL: DeviceType = DeviceType::new(1, 7);
    pub const RANDOM: DeviceType = DeviceType::new(1, 8);
    pub const URANDOM: DeviceType = DeviceType::new(1, 9);
    pub const KMSG: DeviceType = DeviceType::new(1, 11);

    pub const fn new(major: u32, minor: u32) -> DeviceType {
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

    pub const fn from_bits(dev: dev_t) -> DeviceType {
        DeviceType(dev)
    }

    pub const fn bits(&self) -> dev_t {
        self.0
    }

    #[allow(dead_code)]
    pub const fn major(&self) -> u32 {
        ((self.0 >> 32 & 0xfffff000) | ((self.0 >> 8) & 0xfff)) as u32
    }

    #[allow(dead_code)]
    pub const fn minor(&self) -> u32 {
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

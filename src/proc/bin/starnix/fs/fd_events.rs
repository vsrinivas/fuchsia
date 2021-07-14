// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::ops;

use crate::types::uapi;

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct FdEvents {
    mask: u32,
}

impl FdEvents {
    pub const POLLIN: FdEvents = FdEvents { mask: uapi::POLLIN };
    pub const POLLPRI: FdEvents = FdEvents { mask: uapi::POLLPRI };
    pub const POLLOUT: FdEvents = FdEvents { mask: uapi::POLLOUT };
    pub const POLLERR: FdEvents = FdEvents { mask: uapi::POLLERR };
    pub const POLLHUP: FdEvents = FdEvents { mask: uapi::POLLHUP };
    pub const POLLNVAL: FdEvents = FdEvents { mask: uapi::POLLNVAL };
    pub const POLLRDNORM: FdEvents = FdEvents { mask: uapi::POLLRDNORM };
    pub const POLLRDBAND: FdEvents = FdEvents { mask: uapi::POLLRDBAND };
    pub const POLLWRNORM: FdEvents = FdEvents { mask: uapi::POLLWRNORM };
    pub const POLLWRBAND: FdEvents = FdEvents { mask: uapi::POLLWRBAND };
    pub const POLLMSG: FdEvents = FdEvents { mask: uapi::POLLMSG };
    pub const POLLREMOVE: FdEvents = FdEvents { mask: uapi::POLLREMOVE };
    pub const POLLRDHUP: FdEvents = FdEvents { mask: uapi::POLLRDHUP };

    pub fn empty() -> FdEvents {
        Self::from(0)
    }

    pub fn from(mask: u32) -> FdEvents {
        FdEvents { mask }
    }

    pub fn mask(&self) -> u32 {
        self.mask
    }
}

impl ops::BitAnd for FdEvents {
    type Output = bool;

    // rhs is the "right-hand side" of the expression `a & b`
    fn bitand(self, rhs: Self) -> Self::Output {
        self.mask & rhs.mask != 0
    }
}

impl ops::BitOr for FdEvents {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self { mask: self.mask | rhs.mask }
    }
}

impl ops::BitOrAssign for FdEvents {
    fn bitor_assign(&mut self, rhs: Self) {
        self.mask |= rhs.mask;
    }
}

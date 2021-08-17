// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::Ordering;
use std::fmt;
use zerocopy::{AsBytes, FromBytes};

use crate::types::*;

#[derive(Hash, PartialEq, Eq, Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(transparent)]
pub struct FdNumber(i32);

impl FdNumber {
    pub const AT_FDCWD: FdNumber = FdNumber(AT_FDCWD);

    pub fn from_raw(n: i32) -> FdNumber {
        FdNumber(n)
    }
    pub fn raw(&self) -> i32 {
        self.0
    }
}

impl fmt::Display for FdNumber {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "fd({})", self.0)
    }
}

impl Ord for FdNumber {
    fn cmp(&self, other: &Self) -> Ordering {
        self.0.cmp(&other.0)
    }
}

impl PartialOrd for FdNumber {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

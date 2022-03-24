// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use zerocopy::{AsBytes, FromBytes};

use crate::fs::FsStr;
use crate::types::*;

#[derive(Hash, PartialEq, Eq, PartialOrd, Ord, Debug, Copy, Clone, AsBytes, FromBytes)]
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

    /// Parses a file descriptor number from a byte string.
    pub fn from_fs_str(s: &FsStr) -> Result<Self, Errno> {
        let name = std::str::from_utf8(s).map_err(|_| errno!(EINVAL))?;
        let num = name.parse::<i32>().map_err(|_| errno!(EINVAL))?;
        Ok(FdNumber(num))
    }
}

impl fmt::Display for FdNumber {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "fd({})", self.0)
    }
}

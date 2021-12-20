// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::FxfsError,
    anyhow::{ensure, Error},
    std::{
        cmp::Ord,
        fmt::Debug,
        ops::{Range, Sub},
    },
};

pub(crate) trait RangeExt<T> {
    /// Returns whether the range is valid (i.e. start <= end).
    fn valid(&self) -> bool;
    /// Returns the length of the range, or an error if the range is `!RangeExt::valid()`.
    /// Since this is intended to be used primarily for possibly-untrusted serialized ranges, the
    /// error returned is FxfsError::Inconsistent.
    fn length(&self) -> Result<T, Error>;
}

impl<T: Sub<Output = T> + Copy + Ord + Debug> RangeExt<T> for Range<T> {
    fn valid(&self) -> bool {
        self.start <= self.end
    }
    fn length(&self) -> Result<T, Error> {
        ensure!(self.valid(), FxfsError::Inconsistent);
        Ok(self.end - self.start)
    }
}

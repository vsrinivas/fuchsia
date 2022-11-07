// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::FxfsError,
    anyhow::{ensure, Error},
    std::{
        cmp::Ord,
        fmt::Debug,
        ops::{Range, Rem, Sub},
    },
};

pub(crate) trait RangeExt<T> {
    /// Returns whether the range is valid (i.e. start <= end).
    fn is_valid(&self) -> bool;
    /// Returns the length of the range, or an error if the range is `!RangeExt::is_valid()`.
    /// Since this is intended to be used primarily for possibly-untrusted serialized ranges, the
    /// error returned is FxfsError::Inconsistent.
    fn length(&self) -> Result<T, Error>;
    /// Returns true if the range is aligned to the given block size.
    fn is_aligned(&self, block_size: impl Into<T>) -> bool;
}

impl<T: Sub<Output = T> + Copy + Ord + Debug + Rem<Output = T> + PartialEq + Default> RangeExt<T>
    for Range<T>
{
    fn is_valid(&self) -> bool {
        self.start <= self.end
    }
    fn length(&self) -> Result<T, Error> {
        ensure!(self.is_valid(), FxfsError::Inconsistent);
        Ok(self.end - self.start)
    }
    fn is_aligned(&self, block_size: impl Into<T>) -> bool {
        let bs = block_size.into();
        self.start % bs == T::default() && self.end % bs == T::default()
    }
}

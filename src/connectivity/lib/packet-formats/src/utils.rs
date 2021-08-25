// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities useful when parsing and serializing wire formats.

use core::num::NonZeroU64;
use core::time::Duration;

/// A zero-valued `Duration`.
const ZERO_DURATION: Duration = Duration::from_secs(0);

/// A thin wrapper over a [`Duration`] that guarantees that the underlying
/// `Duration` is non-zero.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Hash)]
pub struct NonZeroDuration(Duration);

impl NonZeroDuration {
    /// Creates a non-zero without checking the value.
    ///
    /// # Safety
    ///
    /// If `d` is zero, unsafe code which relies on the invariant that
    /// `NonZeroDuration` values are always zero may cause undefined behavior.
    pub const unsafe fn new_unchecked(d: Duration) -> NonZeroDuration {
        NonZeroDuration(d)
    }

    /// Creates a new `NonZeroDuration` from the specified non-zero number of
    /// whole seconds.
    pub const fn from_nonzero_secs(secs: NonZeroU64) -> NonZeroDuration {
        NonZeroDuration(Duration::from_secs(secs.get()))
    }

    /// Creates a non-zero if the given value is not zero.
    pub fn new(d: Duration) -> Option<NonZeroDuration> {
        if d == ZERO_DURATION {
            return None;
        }

        Some(NonZeroDuration(d))
    }

    /// Returns the value as a [`Duration`].
    pub const fn get(&self) -> Duration {
        self.0
    }
}

impl From<NonZeroDuration> for Duration {
    fn from(NonZeroDuration(d): NonZeroDuration) -> Duration {
        d
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn non_zero_duration() {
        // `NonZeroDuration` should not hold a zero duration.
        assert_eq!(NonZeroDuration::new(Duration::from_secs(0)), None);
        let d = Duration::from_secs(1);

        // `new_unchecked` should hold the duration as is.
        assert_eq!(unsafe { NonZeroDuration::new_unchecked(d) }, NonZeroDuration(d));

        // `NonZeroDuration` should hold a non-zero duration.
        let non_zero = NonZeroDuration::new(d);
        assert_eq!(non_zero, Some(NonZeroDuration(d)));

        // `get` and `Into::into` should return the underlying duration.
        let non_zero = non_zero.unwrap();
        assert_eq!(d, non_zero.get());
        assert_eq!(d, non_zero.into());
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use core::fmt::{Debug, Formatter};

/// Type representing a set of radio channels.
#[repr(transparent)]
#[derive(Default, Clone, Copy, Eq, PartialEq)]
pub struct ChannelMask(u32);

/// The maximum number of individual channels that can be referenced by this API.
pub const MAX_NUM_CHANNELS: u8 = 32;

impl ChannelMask {
    /// Tries to create a new channel mask from an iterator of channel indexes.
    pub fn try_from<'a, T, I>(iter: I) -> Result<Self, ot::ChannelOutOfRange>
    where
        T: 'a + TryInto<ChannelIndex> + Copy,
        I: IntoIterator<Item = &'a T>,
    {
        let mut ret = Self::default();
        for &channel in iter {
            ret.try_insert(channel.try_into().map_err(|_| ot::ChannelOutOfRange)?)?;
        }
        Ok(ret)
    }

    /// Number of channels in the set.
    pub fn len(&self) -> usize {
        self.0.count_ones() as usize
    }

    /// Returns true if the given channel index is represented in this channel mask.
    pub fn contains(&self, index: ChannelIndex) -> bool {
        if index < MAX_NUM_CHANNELS {
            self.0 & (1u32 << index) != 0
        } else {
            false
        }
    }

    /// Try to insert the given channel into the channel mask.
    ///
    /// If the channel mask is out of range (0-31), the method will
    /// return `Err(ChannelOutOfRange)`.
    pub fn try_insert(&mut self, index: ChannelIndex) -> Result<(), ot::ChannelOutOfRange> {
        if index >= MAX_NUM_CHANNELS {
            Err(ot::ChannelOutOfRange)
        } else {
            let mask = 1u32 << index;
            self.0 |= mask;
            Ok(())
        }
    }

    /// Try to remove the given channel from the channel mask.
    ///
    /// If the channel mask is out of range (0-31), the method will
    /// return `Err(ChannelOutOfRange)`. Otherwise, returns `Ok(removed)`,
    /// where `removed` is a bool indicating if the channel was in
    /// the mask to begin with.
    pub fn try_remove(&mut self, index: ChannelIndex) -> Result<bool, ot::ChannelOutOfRange> {
        if index >= MAX_NUM_CHANNELS {
            Err(ot::ChannelOutOfRange)
        } else {
            let mask = 1u32 << index;
            let removed = self.0 & mask != 0;
            self.0 &= !mask;
            Ok(removed)
        }
    }
}

impl Debug for ChannelMask {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        write!(f, "{:?}", self.collect::<Vec<_>>())
    }
}

impl From<u32> for ChannelMask {
    fn from(mask: u32) -> Self {
        Self(mask)
    }
}

impl From<ChannelMask> for u32 {
    fn from(mask: ChannelMask) -> Self {
        mask.0
    }
}

impl Iterator for ChannelMask {
    type Item = ChannelIndex;
    fn next(&mut self) -> Option<Self::Item> {
        let channel: ChannelIndex = self.0.trailing_zeros().try_into().unwrap();
        match self.try_remove(channel) {
            Ok(true) => Some(channel),
            Ok(false) => {
                unreachable!(
                    "Bug in ChannelMask. Next Channel: {}, Full Mask: {:#08x}",
                    channel, self.0
                )
            }
            Err(ot::ChannelOutOfRange) => None,
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = self.len();
        (len, Some(len))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_channel_mask_iter() {
        let mut mask = ChannelMask::default();

        assert_eq!(mask.count(), 0);

        mask.try_insert(9).unwrap();
        mask.try_insert(13).unwrap();
        mask.try_insert(14).unwrap();

        assert!(mask.contains(9));
        assert!(mask.contains(13));
        assert!(mask.contains(14));
        assert!(!mask.contains(21));

        assert_eq!(mask.collect::<Vec<_>>(), vec![9, 13, 14]);
        assert_eq!(mask.len(), 3);

        assert_eq!(mask.try_remove(13), Ok(true));

        assert_eq!(mask.collect::<Vec<_>>(), vec![9, 14]);

        assert_eq!(mask.try_remove(13), Ok(false));

        assert_eq!(mask.collect::<Vec<_>>(), vec![9, 14]);

        assert_eq!(ChannelMask::from(0).collect::<Vec<_>>(), vec![]);
        assert_eq!(ChannelMask::from(0x0000FFFF).collect::<Vec<_>>(), (0..16).collect::<Vec<_>>());
        assert_eq!(ChannelMask::from(0xFFFF0000).collect::<Vec<_>>(), (16..32).collect::<Vec<_>>());
        assert_eq!(ChannelMask::from(0xFFFFFFFF).collect::<Vec<_>>(), (0..32).collect::<Vec<_>>());

        assert_eq!(ChannelMask::from(0xFFFFFFFF).contains(MAX_NUM_CHANNELS - 1), true);
        assert_eq!(ChannelMask::from(0xFFFFFFFF).contains(MAX_NUM_CHANNELS), false);
        assert_eq!(ChannelMask::from(0xFFFFFFFF).contains(255), false);

        assert_eq!(ChannelMask::from(0xFFFFFFFF).len(), 32);
        assert_eq!(ChannelMask::from(0x0000FFFF).len(), 16);
        assert_eq!(ChannelMask::from(0xFFFF0000).len(), 16);
        assert_eq!(ChannelMask::from(0).len(), 0);
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_bredr::{ChannelMode, ChannelParameters},
    fuchsia_zircon as zx,
    std::{cmp::PartialEq, collections::HashMap, fmt::Debug},
};

#[derive(Debug, PartialEq)]
pub struct IncrementedIdMap<T> {
    next_id: u32,
    map: HashMap<u32, T>,
}

impl<T: Debug + PartialEq> IncrementedIdMap<T> {
    pub fn new() -> IncrementedIdMap<T> {
        IncrementedIdMap { next_id: 0, map: HashMap::new() }
    }

    pub fn map(&self) -> &HashMap<u32, T> {
        &self.map
    }

    /// Returns id assigned.
    pub fn insert(&mut self, value: T) -> u32 {
        let id = self.next_id;
        self.next_id += 1;
        assert_eq!(None, self.map.insert(id, value));
        id
    }

    pub fn remove(&mut self, id: u32) -> Option<T> {
        self.map.remove(&id)
    }
}

#[derive(Debug, PartialEq)]
pub struct L2capChannel {
    pub socket: zx::Socket,
    pub mode: ChannelMode,
    pub max_tx_sdu_size: u16,
}

#[derive(Debug, PartialEq)]
pub struct SdpService {
    pub service_id: u64,
    pub params: ChannelParameters,
}

/// Tracks all state local to the command line tool.
pub struct ProfileState {
    pub channels: IncrementedIdMap<L2capChannel>,
    pub services: HashMap<u64, SdpService>,
}

impl ProfileState {
    pub fn new() -> ProfileState {
        ProfileState { channels: IncrementedIdMap::new(), services: HashMap::new() }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_incremented_id_map() {
        let mut numbers = IncrementedIdMap::<i32>::new();
        assert_eq!(0, numbers.insert(0));
        assert_eq!(1, numbers.insert(1));

        assert_eq!(2, numbers.map().len());
        assert_eq!(Some(&0i32), numbers.map().get(&0u32));
        assert_eq!(Some(&1i32), numbers.map().get(&1u32));
    }
}

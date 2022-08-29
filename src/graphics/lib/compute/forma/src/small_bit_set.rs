// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;

type Container = u32;

#[derive(Clone, Debug, Default)]
pub struct SmallBitSet {
    bit_set: Container,
}

impl SmallBitSet {
    pub fn clear(&mut self) {
        self.bit_set = 0;
    }

    pub const fn contains(&self, val: &u8) -> bool {
        (self.bit_set >> *val as Container) & 0b1 != 0
    }

    pub fn insert(&mut self, val: u8) -> bool {
        if val as usize >= mem::size_of_val(&self.bit_set) * 8 {
            return false;
        }

        self.bit_set |= 0b1 << u32::from(val);

        true
    }

    pub fn remove(&mut self, val: u8) -> bool {
        if val as usize >= mem::size_of_val(&self.bit_set) * 8 {
            return false;
        }

        self.bit_set &= !(0b1 << u32::from(val));

        true
    }

    pub fn first_empty_slot(&mut self) -> Option<u8> {
        let slot = self.bit_set.trailing_ones() as u8;

        self.insert(slot).then(|| slot)
    }
}

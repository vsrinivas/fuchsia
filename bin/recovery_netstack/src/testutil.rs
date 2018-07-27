// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Testing-related utilities.

use rand::{SeedableRng, XorShiftRng};
use std::collections::HashMap;

/// Create a new deterministic RNG from a seed.
pub fn new_rng(mut seed: u64) -> impl SeedableRng<[u32; 4]> {
    if seed == 0 {
        // XorShiftRng can't take 0 seeds
        seed = 1;
    }
    XorShiftRng::from_seed([
        seed as u32,
        (seed >> 32) as u32,
        seed as u32,
        (seed >> 32) as u32,
    ])
}

#[derive(Default, Debug)]
pub struct TestCounters {
    data: HashMap<String, usize>,
}

impl TestCounters {
    pub fn increment(&mut self, key: &str) {
        *(self.data.entry(key.to_string()).or_insert(0)) += 1;
    }

    pub fn get(&self, key: &str) -> &usize {
        self.data.get(key).unwrap_or(&0)
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for counting statistics

pub struct StatCounter(std::sync::atomic::AtomicU64);

impl StatCounter {
    pub fn inc(&self) {
        self.add(1);
    }
    pub fn add(&self, n: u64) {
        self.0.fetch_add(n, std::sync::atomic::Ordering::Relaxed);
    }
    pub fn fetch(&self) -> u64 {
        self.0.load(std::sync::atomic::Ordering::Relaxed)
    }
}

impl Default for StatCounter {
    fn default() -> StatCounter {
        StatCounter(std::sync::atomic::AtomicU64::new(0))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn simple() {
        let s = StatCounter::default();
        assert_eq!(s.fetch(), 0);
        s.inc();
        assert_eq!(s.fetch(), 1);
        s.add(41);
        assert_eq!(s.fetch(), 42);
    }
}

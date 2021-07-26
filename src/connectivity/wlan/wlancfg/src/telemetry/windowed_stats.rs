// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]

use num_traits::SaturatingAdd;
use std::collections::VecDeque;
use std::default::Default;

pub struct WindowedStats<T> {
    stats: VecDeque<T>,
    capacity: usize,
}

impl<T: Default + SaturatingAdd> WindowedStats<T> {
    pub fn new(capacity: usize) -> Self {
        let mut stats = VecDeque::with_capacity(capacity);
        stats.push_back(T::default());
        Self { stats, capacity }
    }

    pub fn windowed_stat(&self) -> T {
        let mut total = T::default();
        for item in self.stats.iter() {
            total = total.saturating_add(item);
        }
        total
    }

    pub fn saturating_add(&mut self, addition: &T) {
        // Safe to unwrap because `self.stats` should always have at least one entry.
        let latest = self.stats.back_mut().unwrap();
        *latest = latest.saturating_add(addition);
    }

    pub fn slide_window(&mut self) {
        if !self.stats.is_empty() && self.stats.len() >= self.capacity {
            let _ = self.stats.pop_front();
        }
        self.stats.push_back(T::default());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn windowed_stats_partial_period() {
        let mut windowed_stats = WindowedStats::<u32>::new(3);
        windowed_stats.saturating_add(&1u32);
        windowed_stats.saturating_add(&2u32);
        assert_eq!(windowed_stats.windowed_stat(), 3u32);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&3u32);
        assert_eq!(windowed_stats.windowed_stat(), 6u32);
    }

    #[test]
    fn windowed_stats_full_period() {
        let mut windowed_stats = WindowedStats::<u32>::new(3);
        windowed_stats.saturating_add(&1u32);
        assert_eq!(windowed_stats.windowed_stat(), 1u32);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&2u32);
        assert_eq!(windowed_stats.windowed_stat(), 3u32);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&3u32);
        assert_eq!(windowed_stats.windowed_stat(), 6u32);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&10u32);
        // Value 1 from the first window is excluded
        assert_eq!(windowed_stats.windowed_stat(), 15u32);
    }

    #[test]
    fn windowed_stats_large_number() {
        let mut windowed_stats = WindowedStats::<u32>::new(3);
        windowed_stats.saturating_add(&10u32);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&10u32);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&(u32::MAX - 20u32));
        assert_eq!(windowed_stats.windowed_stat(), u32::MAX);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&9u32);
        assert_eq!(windowed_stats.windowed_stat(), u32::MAX - 1);
    }

    #[test]
    fn windowed_stats_test_overflow() {
        let mut windowed_stats = WindowedStats::<u32>::new(3);
        // Overflow in a single window
        windowed_stats.saturating_add(&u32::MAX);
        windowed_stats.saturating_add(&1u32);
        assert_eq!(windowed_stats.windowed_stat(), u32::MAX);

        windowed_stats.slide_window();
        windowed_stats.saturating_add(&10u32);
        assert_eq!(windowed_stats.windowed_stat(), u32::MAX);
        windowed_stats.slide_window();
        windowed_stats.saturating_add(&5u32);
        assert_eq!(windowed_stats.windowed_stat(), u32::MAX);
        windowed_stats.slide_window();
        windowed_stats.saturating_add(&3u32);
        assert_eq!(windowed_stats.windowed_stat(), 18u32);
    }
}

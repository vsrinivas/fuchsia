// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{collections::VecDeque, time::Duration, time::Instant};

/// The short period used for throttling update checks.
const SHORT_PERIOD_DURATION: Duration = Duration::from_secs(5 * 60);
/// The long period used for throttling update checks.
const LONG_PERIOD_DURATION: Duration = Duration::from_secs(2 * 60 * 60);
/// The maximum number of update checks allowed over a short period.
const MAX_CHECKS_IN_SHORT_PERIOD: usize = 10;
/// The maximum number of update checks allowed over a long period.
const MAX_CHECKS_IN_LONG_PERIOD: usize = 30;

#[derive(Clone, Debug)]
pub struct UpdateCheckRateLimiter {
    // Stores the time of recent update checks, in reverse chronological order.
    recent_update_check_times: VecDeque<Instant>,
}

impl UpdateCheckRateLimiter {
    pub fn new() -> Self {
        Self { recent_update_check_times: VecDeque::new() }
    }

    #[cfg(test)]
    pub fn with_recent_update_check_times(recent_update_check_times: VecDeque<Instant>) -> Self {
        // TODO: replace with .iter().rev().is_sorted() when is_sorted is stable.
        for i in 1..recent_update_check_times.len() {
            assert!(recent_update_check_times[i - 1] >= recent_update_check_times[i]);
        }
        Self { recent_update_check_times }
    }

    /// Adds the time of the update check to the recent update check times.
    pub fn add_time(&mut self, time: Instant) {
        self.recent_update_check_times.push_front(time);
        self.recent_update_check_times.truncate(MAX_CHECKS_IN_LONG_PERIOD);
    }

    /// Given the current_time, returns whether the current update check should be rate limited.
    pub fn should_rate_limit(&self, current_time: Instant) -> bool {
        if let Some(check_time) = self.recent_update_check_times.get(MAX_CHECKS_IN_SHORT_PERIOD - 1)
        {
            if *check_time > current_time - SHORT_PERIOD_DURATION {
                return true;
            }
        }
        if let Some(check_time) = self.recent_update_check_times.get(MAX_CHECKS_IN_LONG_PERIOD - 1)
        {
            if *check_time > current_time - LONG_PERIOD_DURATION {
                return true;
            }
        }
        false
    }

    #[cfg(test)]
    pub fn get_recent_update_check_times(&self) -> VecDeque<Instant> {
        self.recent_update_check_times.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Test that we should rate limit if update check times exceeds limit in short period.
    #[test]
    fn test_should_rate_limit_short_period() {
        let now = Instant::now();
        let recent_update_check_times = [1, 10, 20, 30, 60, 100, 150, 200, 250, 299, 1000]
            .iter()
            .map(|&i| now - Duration::from_secs(i))
            .collect();
        assert!(UpdateCheckRateLimiter::with_recent_update_check_times(recent_update_check_times)
            .should_rate_limit(now));
    }

    // Test that we should rate limit if update check times exceeds limit in long period.
    #[test]
    fn test_should_rate_limit_long_period() {
        let now = Instant::now();
        let recent_update_check_times = [
            1, 10, 20, 30, 60, 100, 150, 200, 250, 301, 1000, 1500, 2000, 2500, 3000, 3500, 4000,
            4500, 5000, 5500, 6000, 6200, 6400, 6600, 6800, 7000, 7050, 7100, 7150, 7199,
        ]
        .iter()
        .map(|&i| now - Duration::from_secs(i))
        .collect();
        assert!(UpdateCheckRateLimiter::with_recent_update_check_times(recent_update_check_times)
            .should_rate_limit(now));
    }

    // Test that we should not rate limit if update check times is below the limit.
    #[test]
    fn test_should_not_rate_limit() {
        let now = Instant::now();
        let recent_update_check_times = [
            1, 10, 20, 30, 60, 100, 150, 200, 250, 301, 1000, 1500, 2000, 2500, 3000, 3500, 4000,
            4500, 5000, 5500, 6000, 6200, 6400, 6600, 6800, 7000, 7050, 7100, 7150, 7201,
        ]
        .iter()
        .map(|&i| now - Duration::from_secs(i))
        .collect();
        assert!(!UpdateCheckRateLimiter::with_recent_update_check_times(recent_update_check_times)
            .should_rate_limit(now));
    }

    #[test]
    fn test_add_time() {
        let now = Instant::now();
        let mut rate_limiter = UpdateCheckRateLimiter::new();
        for i in 0..100 {
            rate_limiter.add_time(now + Duration::from_secs(i));
        }
        assert_eq!(rate_limiter.recent_update_check_times.len(), MAX_CHECKS_IN_LONG_PERIOD);
        // This asserts that the recent update check times is in correct order.
        UpdateCheckRateLimiter::with_recent_update_check_times(
            rate_limiter.recent_update_check_times,
        );
    }

    #[test]
    fn test_keep_rate_limiting() {
        let mut now = Instant::now();
        let mut rate_limiter = UpdateCheckRateLimiter::new();
        for i in 0..100 {
            now += Duration::from_secs(10);
            assert_eq!(
                rate_limiter.should_rate_limit(now),
                i >= MAX_CHECKS_IN_SHORT_PERIOD,
                "i = {}",
                i
            );
            rate_limiter.add_time(now);
        }
    }
}

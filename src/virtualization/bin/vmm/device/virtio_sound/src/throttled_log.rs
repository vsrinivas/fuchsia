// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Don't complain if one of info/warn/err/debug are unused.
#![allow(unused)]

use {
    fuchsia_zircon as zx, once_cell::sync::Lazy, std::cell::RefCell, std::rc::Rc,
    std::sync::atomic::AtomicBool, std::sync::Mutex,
};

/// A simple rate limiter
struct RateLimiter<C: Clock> {
    inner: Mutex<TokenBucket<C>>,
}

// Each period, we reset tokens to tokens_per_period.
struct TokenBucket<C: Clock> {
    last_update: zx::Time,
    period: zx::Duration,
    tokens_per_period: i64,
    tokens: i64,
    clock: C,
}

trait Clock {
    fn now(&self) -> zx::Time;
}

impl<C: Clock> RateLimiter<C> {
    fn new(clock: C, tokens_per_period: i64, period: zx::Duration) -> Self {
        RateLimiter {
            inner: Mutex::new(TokenBucket {
                last_update: clock.now(),
                period,
                tokens_per_period,
                tokens: 0,
                clock,
            }),
        }
    }

    fn acquire_one(&self) -> bool {
        let mut tb = self.inner.lock().unwrap();
        if tb.period.into_nanos() == 0 {
            return true;
        }
        // Update available tokens.
        let now = tb.clock.now();
        if now - tb.last_update >= tb.period {
            tb.tokens = tb.tokens_per_period;
            tb.last_update = now;
        }
        // Acquire a token if available.
        if tb.tokens > 0 {
            tb.tokens -= 1;
            true
        } else {
            false
        }
    }
}

struct Monotonic;

impl Clock for Monotonic {
    fn now(&self) -> zx::Time {
        zx::Time::get_monotonic()
    }
}

// Allow bursts of up to 30 log messages, but throttle to 1 log/s on average.
static LIMITER: Lazy<RateLimiter<Monotonic>> =
    Lazy::new(|| RateLimiter::new(Monotonic, 30, zx::Duration::from_seconds(30)));

// Log everything in tests.
#[cfg(test)]
static LOG_EVERYTHING: Lazy<AtomicBool> = Lazy::new(|| AtomicBool::new(false));

#[cfg(not(test))]
static LOG_EVERYTHING: Lazy<AtomicBool> = Lazy::new(|| AtomicBool::new(true));

pub fn acquire_one() -> bool {
    if LOG_EVERYTHING.load(std::sync::atomic::Ordering::Relaxed) {
        true
    } else {
        LIMITER.acquire_one()
    }
}

pub fn log_everything(enabled: bool) {
    LOG_EVERYTHING.store(enabled, std::sync::atomic::Ordering::Relaxed);
}

macro_rules! debug {
    ($($arg:tt)*) => {
        if throttled_log::acquire_one() {
            tracing::debug!($($arg)*)
        }
    };
}

macro_rules! info {
    ($($arg:tt)*) => {
        if throttled_log::acquire_one() {
            tracing::info!($($arg)*)
        }
    };
}

macro_rules! warning {
    ($($arg:tt)*) => {
        if throttled_log::acquire_one() {
            tracing::warn!($($arg)*)
        }
    };
}

macro_rules! error {
    ($($arg:tt)*) => {
        if throttled_log::acquire_one() {
            tracing::error!($($arg)*)
        }
    };
}

pub(crate) use debug;
pub(crate) use error;
pub(crate) use info;
pub(crate) use warning;

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Clone)]
    struct FakeClock {
        t: Rc<RefCell<zx::Time>>,
    }

    impl FakeClock {
        fn new(t: zx::Time) -> Self {
            Self { t: Rc::new(RefCell::new(t)) }
        }
    }

    impl Clock for FakeClock {
        fn now(&self) -> zx::Time {
            *self.t.borrow()
        }
    }

    #[test]
    fn test() {
        let clock = FakeClock::new(zx::Time::from_nanos(0));
        let limiter = RateLimiter::<FakeClock>::new(clock.clone(), 2, zx::Duration::from_nanos(2));

        // Starts empty.
        assert!(!limiter.acquire_one());

        // No updates until one full period.
        *clock.t.borrow_mut() = zx::Time::from_nanos(1);
        assert!(!limiter.acquire_one());

        // Expected 2 tokens per period.
        *clock.t.borrow_mut() = zx::Time::from_nanos(2);
        assert!(limiter.acquire_one());
        assert!(limiter.acquire_one());
        assert!(!limiter.acquire_one());

        // Unused tokens don't carry over to the next period.
        *clock.t.borrow_mut() = zx::Time::from_nanos(4);
        assert!(limiter.acquire_one());
        *clock.t.borrow_mut() = zx::Time::from_nanos(6);
        assert!(limiter.acquire_one());
        assert!(limiter.acquire_one());
        assert!(!limiter.acquire_one());
    }
}
